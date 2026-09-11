#include "Joinery.h"

#include "ModelingOps.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_OBB.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Joinery {
namespace {

// The nearest real drill/dowel size at or below `wanted`, so a default is
// something a workshop actually owns rather than 5.94 mm.
double realDowelSize(double wanted)
{
    static const double kSizes[] = {4.0, 5.0, 6.0, 8.0, 10.0, 12.0};
    double best = kSizes[0];
    for (const double size : kSizes) {
        if (size <= wanted + 1.0e-9) best = size;
    }
    return best;
}

}  // namespace

Family familyOf(Kind kind)
{
    switch (kind) {
        case Kind::Dowel:
        case Kind::PocketScrew:
        case Kind::Biscuit:
        case Kind::Domino:
        case Kind::Screw:
            return Family::Fasteners;
        case Kind::Dado:
        case Kind::Rabbet:
        case Kind::Groove:
            return Family::Housing;
        case Kind::MortiseTenon:
        case Kind::HalfLap:
            return Family::Interlock;
    }
    return Family::Fasteners;
}

std::string kindName(Kind kind)
{
    switch (kind) {
        case Kind::Dowel:        return "Dowel";
        case Kind::PocketScrew:  return "Pocket screw";
        case Kind::Biscuit:      return "Biscuit";
        case Kind::Domino:       return "Domino";
        case Kind::Screw:        return "Screw";
        case Kind::Dado:         return "Dado";
        case Kind::Rabbet:       return "Rabbet";
        case Kind::Groove:       return "Groove";
        case Kind::MortiseTenon: return "Mortise and tenon";
        case Kind::HalfLap:      return "Half-lap";
    }
    return "Joint";
}

Parameters defaultsFor(Kind kind, double thinnerThicknessMm)
{
    const double t = thinnerThicknessMm > 1.0e-6 ? thinnerThicknessMm : 18.0;
    Parameters p;
    p.widthMm = t;
    p.insetMm = t / 2.0;
    p.thicknessMm = t / 3.0;

    switch (familyOf(kind)) {
        case Family::Fasteners: {
            p.sizeMm = realDowelSize(t / 3.0);
            p.count = 3;
            // Deep enough to hold, short enough to leave material: a third
            // of the board on the through side, and a shade under half the
            // board's own thickness on the end-grain side.
            p.depthAMm = std::max(t * 0.75, 10.0);
            p.depthBMm = std::max(t * 0.75, 10.0);
            // endMarginMm is left at the struct default (40 mm) - fasteners
            // have no board-derived reason to move it, and housings and
            // interlocks never read the field.
            p.angleDeg = kind == Kind::PocketScrew ? 15.0 : 0.0;
            break;
        }
        case Family::Housing: {
            // A third of the host deep is the standard rule of thumb; the
            // channel is exactly as wide as the piece that sits in it.
            p.depthAMm = t / 3.0;
            p.depthBMm = 0.0;
            p.stopped = false;
            p.stopMm = 10.0;
            break;
        }
        case Family::Interlock: {
            p.thicknessMm = t / 3.0;
            p.lengthMm = std::max(t * 1.5, 25.0);
            p.depthAMm = p.lengthMm + 2.0;   // mortise a hair deeper than the tenon
            p.haunched = false;
            break;
        }
    }
    return p;
}

gp_Pnt Contact::at(double u, double v) const
{
    return gp_Pnt(frame.Location().XYZ() + frame.XDirection().XYZ() * u +
                  frame.YDirection().XYZ() * v);
}

namespace {

// The extreme value of a curve's projection onto `axis` over [t0, t1], for
// the maximum (`wantMax`) or the minimum. Coarse-sampled to bracket the
// extremum, then ternary-refined inside that bracket - a plain sample grid
// alone is only accurate to the sampling step, and this number IS the answer
// Tasks 3/4/5 lay items along, so "close" is not good enough. 32 intervals
// brackets any single arc of a contact boundary; 90 ternary steps shrink the
// bracket by (2/3)^90, far past double precision.
double curveExtreme(const BRepAdaptor_Curve& curve, const gp_Vec& axis, bool wantMax)
{
    const double t0 = curve.FirstParameter();
    const double t1 = curve.LastParameter();
    const double sign = wantMax ? 1.0 : -1.0;
    const auto value = [&curve, &axis, sign](double t) {
        const gp_Pnt p = curve.Value(t);
        return sign * (p.X() * axis.X() + p.Y() * axis.Y() + p.Z() * axis.Z());
    };

    constexpr int kIntervals = 32;
    int bestIndex = 0;
    double best = -std::numeric_limits<double>::max();
    for (int i = 0; i <= kIntervals; ++i) {
        const double t = t0 + (t1 - t0) * i / double(kIntervals);
        const double f = value(t);
        if (f > best) {
            best = f;
            bestIndex = i;
        }
    }

    double lo = t0 + (t1 - t0) * std::max(bestIndex - 1, 0) / double(kIntervals);
    double hi = t0 + (t1 - t0) * std::min(bestIndex + 1, kIntervals) / double(kIntervals);
    for (int step = 0; step < 90 && hi - lo > 0.0; ++step) {
        const double m1 = lo + (hi - lo) / 3.0;
        const double m2 = hi - (hi - lo) / 3.0;
        if (value(m1) < value(m2)) lo = m1;
        else hi = m2;
    }
    const double refined = std::max(value(0.5 * (lo + hi)), best);
    return sign * refined;
}

// The [min, max] extent of `shape` projected onto `axis`, measured from the
// shape's OWN geometry - deliberately not the world axis-aligned bounding
// box, whose eight corners projected onto an oblique axis measure the BOX's
// diagonal rather than the shape's extent along that direction (a 600x18
// board spun 45 degrees about Z has a roughly 437x437 world AABB, which
// projected onto the board's own thickness normal spans about 618 mm against
// a true 18 mm).
//
// Vertices are not enough on their own: an ARC that bulges past its own
// endpoints is invisible to a vertex walk, and a stadium-ended post under a
// board measured a 100 mm run where the truth was 140 - 29% short, reported
// as a success, which is the same "a wrong size is a refusal surfacing as a
// success" law in the permissive direction. Every boundary edge that is not
// a straight line therefore contributes its own extremes as well.
void projectedRange(const TopoDS_Shape& shape, const gp_Dir& axis, double& lo, double& hi)
{
    const gp_Vec along(axis);
    lo = std::numeric_limits<double>::max();
    hi = -std::numeric_limits<double>::max();
    for (TopExp_Explorer iv(shape, TopAbs_VERTEX); iv.More(); iv.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(iv.Current()));
        const double t = p.X() * axis.X() + p.Y() * axis.Y() + p.Z() * axis.Z();
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    for (TopExp_Explorer ie(shape, TopAbs_EDGE); ie.More(); ie.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(ie.Current()));
        // A line's extremes are its endpoints, already walked above.
        if (curve.GetType() == GeomAbs_Line) continue;
        lo = std::min(lo, curveExtreme(curve, along, false));
        hi = std::max(hi, curveExtreme(curve, along, true));
    }
    // Nothing walked leaves lo > hi, deliberately: that is planeExtent's own
    // "there is no region here" signal and must not be flattened to [0, 0],
    // which reads as a real but zero-sized region.
}

// Every direction the shape's OWN planar faces face, with opposite senses
// collapsed to one entry - the only set of directions along which a solid's
// extent means anything about the solid rather than about the world.
std::vector<gp_Dir> planeNormalsOf(const TopoDS_Shape& shape)
{
    std::vector<gp_Dir> dirs;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if (surface.GetType() != GeomAbs_Plane) continue;
        const gp_Dir n = surface.Plane().Axis().Direction();
        bool seen = false;
        for (const gp_Dir& d : dirs) {
            // IsParallel is true for 0 AND 180 degrees, which is what
            // "the same pair of faces" means here - a box has six faces
            // and three thicknesses.
            if (d.IsParallel(n, 1.0e-6)) {
                seen = true;
                break;
            }
        }
        if (!seen) dirs.push_back(n);
    }
    return dirs;
}

// A board's own material thickness - the smallest side of the solid's own
// ORIENTED bounding box, not of the world axis-aligned one. The world AABB
// reports (18+300)/sqrt(2) = 224.86 mm for an 18 mm panel spun 45 degrees,
// which defaultsFor() would turn into a 224.86 mm wide dado and a 12 mm dowel
// drilled 168 mm into an 18 mm board - one Rotate gesture away.
//
// Bnd_OBB rather than a walk of the solid's own planar face normals, which
// was this file's previous answer and is wrong for anything round: a
// cylinder's only planar faces are its two end discs, so a 40 mm leg 700 mm
// tall measured 700. The OBB is orientation-independent by construction,
// reproduces an axis-aligned box exactly, and answers 40 for the leg.
double thicknessOf(const TopoDS_Shape& shape)
{
    Bnd_OBB obb;
    // Shape tolerance deliberately NOT added in: it inflates every side by the
    // B-rep's own fuzz, which is invisible at 18 mm and is the whole answer at
    // 0.15 mm. This is a measurement of the wood, not a containment box.
    BRepBndLib::AddOBB(shape, obb, Standard_True /* use triangulation */,
                       Standard_True /* optimal */,
                       Standard_False /* no shape tolerance */);
    if (obb.IsVoid()) return 0.0;
    return 2.0 * std::min({obb.XHSize(), obb.YHSize(), obb.ZHSize()});
}

double faceArea(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

// The shared region's own in-plane axis: the direction of its LONGEST
// boundary edge, with any component along `normal` removed. This is what
// makes the contact's frame the region's own frame rather than a world-
// derived one - gp_Ax3(point, dir) picks its X and Y arbitrarily out of
// world space, so the extents measured against it are the region's bounding
// box IN A WORLD ORIENTATION: a true 300 x 18 contact measures 224.86 x
// 224.86 at 45 degrees of in-plane rotation, right at 0 and 90 degrees and
// nowhere in between. A contact region is a rectangle in practice, its
// longest edge runs ALONG the joint, so taking that edge as u makes
// runsAlongU() and runLength() mean exactly what their names say at every
// orientation.
//
// False when no boundary edge has a straight in-plane extent at all - a
// circle's single edge has both its vertices at the same point. That is a
// curved contact boundary, and the caller refuses rather than reporting a
// zero-size contact as a success.
bool regionAxis(const TopoDS_Shape& region, const gp_Dir& normal, gp_Dir& out)
{
    const gp_Vec along(normal);
    double bestLength = 0.0;
    gp_Vec best;
    for (TopExp_Explorer ie(region, TopAbs_EDGE); ie.More(); ie.Next()) {
        TopoDS_Vertex v0, v1;
        TopExp::Vertices(TopoDS::Edge(ie.Current()), v0, v1);
        if (v0.IsNull() || v1.IsNull()) continue;
        gp_Vec chord(BRep_Tool::Pnt(v0), BRep_Tool::Pnt(v1));
        chord -= along * chord.Dot(along);
        const double length = chord.Magnitude();
        if (length > bestLength + 1.0e-9) {
            bestLength = length;
            best = chord;
        }
    }
    if (bestLength <= 1.0e-9) return false;
    out = gp_Dir(best);
    return true;
}

// A point genuinely ON the shared region, which is where every side-or-inside
// question about this contact gets asked.
//
// NOT the region's area centroid, which was this file's previous answer and is
// simply not on the region for anything non-convex: an L-shaped contact face
// of 11,952 mm2 and a C-shaped one of 25,000 mm2 - a notched or rabbeted board
// butting a panel, which this app's own Subtract and face pull produce
// routinely - both put the area-weighted centre in the notch, so every
// classifier probe came back "outside everything" and a real contact was
// refused outright with "these two pieces don't meet". A hole through the
// middle does it too; that was the only case the old comment named, and it
// understated the problem by a lot.
//
// ModelingOps::pointOnFace is the sampler, reused rather than reimplemented:
// it learned exactly this lesson on exactly this kind of face (the
// slab-with-a-through-hole), and one sampler means one set of pitfalls.
// The region's LARGEST face is probed, so the point sits on the dominant
// patch when a boolean has split the region into several.
bool regionProbePoint(const TopoDS_Shape& region, gp_Pnt& out)
{
    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer it(region, TopAbs_FACE); it.More(); it.Next()) {
        faces.push_back(TopoDS::Face(it.Current()));
    }
    std::sort(faces.begin(), faces.end(),
              [](const TopoDS_Face& l, const TopoDS_Face& r) {
                  return faceArea(l) > faceArea(r);
              });
    for (const TopoDS_Face& face : faces) {
        if (ModelingOps::pointOnFace(face, out)) return true;
    }
    return false;
}

// The [uMin,uMax] x [vMin,vMax] extent of `shape` in `frame`'s (u, v) axes.
// NOT the world AABB's two diagonal corners: that trick only recovers the
// right answer when `frame`'s X/Y directions happen to be world-axis-aligned.
// Exact in whatever frame it is handed - which is why the frame has to be the
// region's own (see regionAxis) - and curve-aware, through projectedRange, so
// a rounded boundary is measured rather than cut short at its endpoints.
bool planeExtent(const TopoDS_Shape& shape, const gp_Ax3& frame,
                  double& uMin, double& uMax, double& vMin, double& vMax)
{
    const gp_Dir x = frame.XDirection();
    const gp_Dir y = frame.YDirection();
    projectedRange(shape, x, uMin, uMax);
    projectedRange(shape, y, vMin, vMax);
    if (uMin > uMax || vMin > vMax) return false;
    // projectedRange answers in absolute projections; (u, v) are measured from
    // the frame's own origin.
    const gp_Pnt o = frame.Location();
    const double uo = o.X() * x.X() + o.Y() * x.Y() + o.Z() * x.Z();
    const double vo = o.X() * y.X() + o.Y() * y.Y() + o.Z() * y.Z();
    uMin -= uo;
    uMax -= uo;
    vMin -= vo;
    vMax -= vo;
    return true;
}

// The refusal for a contact region this cannot measure: a curved boundary,
// whose extents collapse to a point. A flat board on a round leg genuinely
// touches along a line rather than over an area, so refusing is the correct
// answer and not a placeholder - reporting ok == true with uLength() == 0
// would be a refusal surfacing as a success.
const char* const kCurvedRegion =
    "these two pieces meet on a curved region - only a straight-edged contact "
    "can be measured";

}  // namespace

ContactResult findContact(const TopoDS_Shape& a, const TopoDS_Shape& b,
                          double toleranceMm)
{
    ContactResult result;
    if (a.IsNull() || b.IsNull()) {
        result.error = "one of the pieces is missing";
        return result;
    }

    // Hoisted out of the O(Fa x Fb) candidate loop: neither depends on the
    // candidate pair, and each builds a bounding box of a whole solid.
    const double thicknessA = thicknessOf(a);
    const double thicknessB = thicknessOf(b);

    // How far off the shared region the side-or-inside probes are placed.
    // Derived from the WOOD, never from the caller's `toleranceMm`, which is
    // documented as purely permissive and must stay that way: at
    // `2 * toleranceMm` the probe outgrew the piece it was probing once the
    // tolerance reached half a thickness, so RAISING the tolerance refused a
    // joint it had accepted - measured, a flush 300 x 18 joint was found at
    // tolerance 8.9 and refused at 9.0, and a 6 mm back panel flipped at 3.0.
    // A monotonic parameter that silently stops being monotonic is a trap for
    // six later callers.
    //
    // 1 mm off the contact is local by any furniture standard, and a quarter
    // of the thinner piece keeps the probe inside wood thinner than 4 mm -
    // which is the other end of the same band: a 0.2 mm piece used to be
    // refused for being thinner than the probe.
    const double probeMm =
        std::max(1.0e-4, std::min(1.0, 0.25 * std::min(thicknessA, thicknessB)));

    // Set when a candidate is a genuine, separating contact whose region
    // cannot be measured (a curved boundary). Reported in place of the
    // generic refusal, so the message says WHY rather than claiming the
    // pieces do not meet when they demonstrably do.
    std::string unmeasurable;

    try {
        // Built once and re-Performed: the classifier's setup walks the whole
        // solid, and it is asked one question per candidate pair.
        BRepClass3d_SolidClassifier insideA(a);
        BRepClass3d_SolidClassifier insideB(b);
        const auto occupies = [](BRepClass3d_SolidClassifier& classifier,
                                 const gp_Pnt& point) {
            classifier.Perform(point, 1.0e-6);
            return classifier.State() == TopAbs_IN;
        };

        // The best PLANAR face pair: parallel planes within tolerance whose
        // faces genuinely share area. Projecting b's face onto a's plane
        // before the common is what lets a 0.05 mm gap still count - a
        // boolean between two faces in different planes shares nothing.
        //
        // The sentinel starts NEGATIVE, deliberately not 0.0: this field also
        // decides which candidate wins, and a 0.0 start let "is this the best
        // so far" silently double as the degeneracy guard below (any
        // exact-zero area already failed `area <= bestArea`). A negative
        // sentinel means the FIRST candidate, area or not, must clear
        // `1.0e-6` on its own.
        double bestArea = -1.0;
        for (TopExp_Explorer ia(a, TopAbs_FACE); ia.More(); ia.Next()) {
            const TopoDS_Face fa = TopoDS::Face(ia.Current());
            BRepAdaptor_Surface sa(fa);
            if (sa.GetType() != GeomAbs_Plane) continue;
            const gp_Pln pa = sa.Plane();

            for (TopExp_Explorer ib(b, TopAbs_FACE); ib.More(); ib.Next()) {
                const TopoDS_Face fb = TopoDS::Face(ib.Current());
                BRepAdaptor_Surface sb(fb);
                if (sb.GetType() != GeomAbs_Plane) continue;
                const gp_Pln pb = sb.Plane();

                // Parallel, either way round - a contact has no preferred
                // side.
                if (std::fabs(std::fabs(pa.Axis().Direction().Dot(
                                  pb.Axis().Direction())) -
                              1.0) > 1.0e-6)
                    continue;
                const double gap = pa.Distance(pb.Location());
                if (gap > toleranceMm) continue;

                // Slide b's face onto a's plane, then intersect them.
                gp_Trsf onto;
                const gp_Vec shift(pa.Axis().Direction());
                const double signedGap =
                    gp_Vec(pa.Location(), pb.Location()).Dot(shift);
                onto.SetTranslation(shift * -signedGap);
                const TopoDS_Shape moved =
                    BRepBuilderAPI_Transform(fb, onto, Standard_True).Shape();

                BRepAlgoAPI_Common common(fa, moved);
                if (!common.IsDone()) continue;
                const TopoDS_Shape region = common.Shape();
                const double area = faceArea(region);
                if (area <= bestArea || area < 1.0e-6) continue;

                // Is this plane actually a separating surface, HERE? Asked
                // with a point classification a hair either side of a point
                // genuinely ON the shared region, which answers "which side
                // of this plane does each solid occupy at this contact"
                // exactly.
                //
                // Two earlier criteria got this wrong in the same way, by
                // asking a LOCAL question with a GLOBAL measurement. Comparing
                // the two whole solids' extents along the normal is right for
                // convex-against-convex and refuses a real joint the moment
                // the host has a step in it - a board butting flat on a
                // rabbet measured "these two pieces don't meet" because the
                // step was 400 mm away from the contact. And the sign of the
                // two PLANES' offset carries no information at all for a
                // flush contact, where it is exactly 0.0, so the normal fell
                // through to the plane's own geometric direction and came
                // back the same for findContact(a, b) and findContact(b, a) -
                // one of the two necessarily backwards against the documented
                // "Z points from bodyA into bodyB".
                //
                // The precedent is ModelingOps::pullFace, which used to read
                // TopAbs_Orientation to decide which side of a face is
                // outward, got it wrong on mirrored bodies, and was fixed by
                // replacing the flag reasoning with a classifier probe at a
                // genuine on-face point. A classifier costs more than a
                // projection; this runs once when a joint is created.
                gp_Pnt onRegion;
                if (!regionProbePoint(region, onRegion)) continue;

                // a's own face LIES on this plane, so a's probe only has to
                // clear zero. b's face sits `signedGap` away, up to
                // `toleranceMm`, so b's probe has to clear that gap first or
                // it lands in the slop between the two faces and reports
                // "outside everything". Two offsets, not one - and b's extra
                // reach is the gap that is actually there, never the tolerance
                // that was merely allowed, so a flush joint answers the same
                // at every tolerance.
                const double aProbeMm = probeMm;
                const double bProbeMm = probeMm + std::fabs(signedGap);
                const bool aAhead = occupies(insideA, onRegion.Translated(shift * aProbeMm));
                const bool aBehind = occupies(insideA, onRegion.Translated(shift * -aProbeMm));
                const bool bAhead = occupies(insideB, onRegion.Translated(shift * bProbeMm));
                const bool bBehind = occupies(insideB, onRegion.Translated(shift * -bProbeMm));

                gp_Dir normal;
                if (aBehind && bAhead && !aAhead && !bBehind) {
                    normal = pa.Axis().Direction();
                } else if (aAhead && bBehind && !aBehind && !bAhead) {
                    normal = gp_Dir(pa.Axis().Direction().Reversed());
                } else {
                    // The two solids are on the SAME side here (two rails of
                    // equal thickness crossing at the same height share their
                    // top and bottom planes exactly), or neither is - an
                    // interpenetrating Overlap wearing a coincidentally
                    // coplanar face, not a contact.
                    continue;
                }

                // The frame's X comes from the REGION's own geometry, never
                // from gp_Ax3's world-derived default pick.
                gp_Dir xDir;
                if (!regionAxis(region, normal, xDir)) {
                    unmeasurable = kCurvedRegion;
                    continue;
                }
                const gp_Ax3 measured(pa.Location(), normal, xDir);
                double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
                if (!planeExtent(region, measured, u0, u1, v0, v1)) continue;
                if (u1 - u0 <= 1.0e-6 || v1 - v0 <= 1.0e-6) {
                    unmeasurable = kCurvedRegion;
                    continue;
                }

                // ONE origin convention, honoured by both contact types: the
                // region's own (uMin, vMin) corner, so uMin == vMin == 0.
                const gp_Pnt origin(measured.Location().XYZ() +
                                    measured.XDirection().XYZ() * u0 +
                                    measured.YDirection().XYZ() * v0);

                bestArea = area;
                result.ok = true;
                result.error.clear();
                result.contact.type = Contact::Type::Face;
                result.contact.frame = gp_Ax3(origin, normal, xDir);
                result.contact.uMin = 0.0;
                result.contact.uMax = u1 - u0;
                result.contact.vMin = 0.0;
                result.contact.vMax = v1 - v0;
                result.contact.thicknessAMm = thicknessA;
                result.contact.thicknessBMm = thicknessB;
            }
        }
        if (result.ok) return result;

        // No shared face: do the solids INTERSECT? That is the crossing-rails
        // case, and it is what a half-lap is cut from.
        BRepAlgoAPI_Common solids(a, b);
        if (solids.IsDone()) {
            GProp_GProps volProps;
            BRepGProp::VolumeProperties(solids.Shape(), volProps);
            if (volProps.Mass() > 1.0e-6) {
                const TopoDS_Shape lap = solids.Shape();

                // The lap's frame comes from the LAP's own geometry. A
                // hardcoded world +Z returns the top of the world bounding
                // box and the rails' thickness as one extent the moment the
                // rails are not lying flat, and a world AABB inflates a
                // 60 x 60 footprint to 84.85 x 84.85 under in-plane rotation.
                // The lap's THINNEST direction over its own face normals IS
                // the lap depth, so that is the frame's Z.
                const std::vector<gp_Dir> dirs = planeNormalsOf(lap);
                double thinnest = std::numeric_limits<double>::max();
                gp_Dir depth;
                for (const gp_Dir& d : dirs) {
                    double lo = 0.0, hi = 0.0;
                    projectedRange(lap, d, lo, hi);
                    if (hi - lo < thinnest) {
                        thinnest = hi - lo;
                        depth = d;
                    }
                }

                gp_Dir xDir;
                if (!dirs.empty() && regionAxis(lap, depth, xDir)) {
                    // Z's sense: from a's own centre of mass toward b's, when
                    // that is decisive. For a symmetric crossing it is not
                    // (both centres sit at the same depth) and the lap's own
                    // face normal stands, which is deterministic for a given
                    // pair of shapes.
                    GProp_GProps massA, massB;
                    BRepGProp::VolumeProperties(a, massA);
                    BRepGProp::VolumeProperties(b, massB);
                    const gp_Vec aToB(massA.CentreOfMass(), massB.CentreOfMass());
                    if (aToB.Dot(gp_Vec(depth)) < -1.0e-9) depth.Reverse();

                    GProp_GProps lapProps;
                    BRepGProp::VolumeProperties(lap, lapProps);
                    const gp_Pnt seed = lapProps.CentreOfMass();
                    const gp_Ax3 measured(seed, depth, xDir);
                    double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
                    if (planeExtent(lap, measured, u0, u1, v0, v1) &&
                        u1 - u0 > 1.0e-6 && v1 - v0 > 1.0e-6) {
                        // Same origin convention as the Face branch, one
                        // dimension richer: the lap's (uMin, vMin) corner on
                        // its own minimum-depth face, so the lap region spans
                        // [0, lap depth] along Z from the frame's plane.
                        double wLo = 0.0, wHi = 0.0;
                        projectedRange(lap, depth, wLo, wHi);
                        const double seedW = seed.X() * depth.X() +
                                             seed.Y() * depth.Y() +
                                             seed.Z() * depth.Z();
                        const gp_Pnt origin(measured.Location().XYZ() +
                                            measured.XDirection().XYZ() * u0 +
                                            measured.YDirection().XYZ() * v0 +
                                            depth.XYZ() * (wLo - seedW));

                        result.ok = true;
                        result.contact.type = Contact::Type::Overlap;
                        result.contact.frame = gp_Ax3(origin, depth, xDir);
                        result.contact.uMin = 0.0;
                        result.contact.uMax = u1 - u0;
                        result.contact.vMin = 0.0;
                        result.contact.vMax = v1 - v0;
                        result.contact.thicknessAMm = thicknessA;
                        result.contact.thicknessBMm = thicknessB;
                        return result;
                    }
                }
                unmeasurable = kCurvedRegion;
            }
        }
    } catch (const Standard_Failure& e) {
        // OCCT booleans THROW on near-tangent geometry - the documented weak
        // spot - and ModelingOps catches Standard_Failure at exactly this
        // boundary for exactly this reason. A fresh result, so the refusal
        // carries an empty contact as the contract demands.
        ContactResult failed;
        failed.error = std::string("contact: kernel exception - ") +
                       (e.GetMessageString() ? e.GetMessageString() : "unknown");
        return failed;
    }

    result.error = unmeasurable.empty() ? "these two pieces don't meet" : unmeasurable;
    return result;
}

}  // namespace Joinery
