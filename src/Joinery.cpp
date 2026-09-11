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
#include <Bnd_Box.hxx>
#include <Bnd_OBB.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Lin.hxx>
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
            if (kind == Kind::HalfLap) {
                // A half-lap removes half of each piece where they cross -
                // the spec's own default - and nothing about it is a tenon:
                // the tenon numbers this branch used to share proposed a
                // 29 mm deep lap in an 18 mm rail. One number can only say
                // "half of it"; defaultsForContact() says half of EACH.
                p.depthAMm = t / 2.0;
                p.depthBMm = t / 2.0;
                p.thicknessMm = t / 2.0;   // the lap depth, the field's own reading
            } else {
                p.thicknessMm = t / 3.0;
                p.lengthMm = std::max(t * 1.5, 25.0);
                p.depthAMm = p.lengthMm + 2.0;   // mortise a hair deeper than the tenon
            }
            p.haunched = false;
            break;
        }
    }
    return p;
}

Parameters defaultsForContact(Kind kind, const Contact& contact)
{
    const double pieceA = contact.thicknessAMm;
    const double pieceB = contact.thicknessBMm;
    const bool measuredA = pieceA > 1.0e-6;
    const bool measuredB = pieceB > 1.0e-6;
    // The thinner MEASURED piece; 0 when neither was, which defaultsFor()
    // already reads as "assume an 18 mm board".
    double thinner = 0.0;
    if (measuredA && measuredB) {
        thinner = std::min(pieceA, pieceB);
    } else if (measuredA) {
        thinner = pieceA;
    } else if (measuredB) {
        thinner = pieceB;
    }
    Parameters p = defaultsFor(kind, thinner);

    switch (familyOf(kind)) {
        case Family::Fasteners:
            // The thinner piece IS the rule for a fastener - a dowel a third
            // of the wood it is driven into - so there is nothing to refine.
            break;
        case Family::Housing:
            // The channel is cut in A (housingRegion() puts depthAMm there),
            // a third of the HOST deep; it is as wide as B, the housed piece.
            if (measuredA) p.depthAMm = pieceA / 3.0;
            if (measuredB) p.widthMm = pieceB;
            break;
        case Family::Interlock:
            if (kind == Kind::HalfLap) {
                if (measuredA) p.depthAMm = pieceA / 2.0;
                if (measuredB) p.depthBMm = pieceB / 2.0;
            } else if (measuredA && p.depthAMm > pieceA) {
                // interlockRegion() puts the mortise depth on A's side of the
                // contact and the tenon's length on B's, so A is the host: a
                // through mortise at most, and the tenon a hair shorter.
                p.depthAMm = pieceA;
                p.lengthMm = pieceA - std::min(2.0, pieceA * 0.5);
            }
            break;
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

// How far a ray can travel through `shape` before it needs to stop looking -
// twice the bounding box's diagonal, so no real exit surface is ever beyond
// it. A search limit, not a measurement, which is why the world AABB is fine
// here where it is wrong everywhere else in this file.
double boundingReach(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return 1.0;
    Standard_Real x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    return 2.0 * std::sqrt(dx * dx + dy * dy + dz * dz) + 1.0;
}

// The distance from `from` to the first surface of `solid` along `direction`.
// False when the ray reaches nothing within `reachMm` - which for a point
// genuinely inside a closed solid should not happen, so the caller treats it
// as "do not trust this measurement" rather than as a zero.
bool firstSurfaceAhead(const TopoDS_Shape& solid, const gp_Pnt& from,
                       const gp_Dir& direction, double reachMm, double& out)
{
    IntCurvesFace_ShapeIntersector inter;
    inter.Load(solid, 1.0e-7);
    inter.Perform(gp_Lin(from, direction), 1.0e-9, reachMm);
    if (inter.NbPnt() < 1) return false;
    double nearest = std::numeric_limits<double>::max();
    for (int i = 1; i <= inter.NbPnt(); ++i) {
        nearest = std::min(nearest, static_cast<double>(inter.WParameter(i)));
    }
    if (nearest >= std::numeric_limits<double>::max()) return false;
    out = nearest;
    return true;
}

// How much wood is behind the joint on one side: the material depth measured
// along the contact normal, from a point `insetMm` inside the piece, out to
// the first surface the material ends at.
//
// This is the LOCAL question, and it is the one a consumer wants. A hollow
// carcase with 18 mm walls is 300 mm across as a solid and 18 mm of wood at
// any joint on it - the whole-solid measure reported 300, which defaultsFor()
// would have turned into a 300 mm dado. A panel rabbeted to 18 mm where the
// shelf lands is 18 there and 36 elsewhere, and the joint is cut where the
// shelf lands.
bool materialDepthBehind(const TopoDS_Shape& solid, const gp_Pnt& inside,
                         const gp_Dir& away, double insetMm, double reachMm,
                         double& out)
{
    double ahead = 0.0;
    if (!firstSurfaceAhead(solid, inside, away, reachMm, ahead)) return false;
    out = insetMm + ahead;
    return true;
}

// The same question for an OVERLAP, where neither piece's face is on the lap
// plane: the material each solid carries THROUGH an interior point of the lap,
// along the lap depth, which for two crossing rails is each rail's own
// thickness - the number a half-lap's defaults split in half.
bool materialDepthThrough(const TopoDS_Shape& solid, const gp_Pnt& inside,
                          const gp_Dir& axis, double reachMm, double& out)
{
    double ahead = 0.0, behind = 0.0;
    if (!firstSurfaceAhead(solid, inside, axis, reachMm, ahead)) return false;
    if (!firstSurfaceAhead(solid, inside, gp_Dir(axis.Reversed()), reachMm, behind))
        return false;
    out = ahead + behind;
    return true;
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

    // Hoisted out of the O(Fa x Fb) candidate loop: none of these depends on
    // the candidate pair. The two thicknesses here are WHOLE-SOLID measures -
    // the smallest side of each piece's oriented bounding box - and they are
    // not what lands in `Contact`: they size the probe offset, they cap the
    // at-the-joint measurement below (so an END-grain contact reports the
    // board's own thickness rather than its length), and they stand in if a
    // ray cast ever fails to find a surface.
    const double thicknessA = thicknessOf(a);
    const double thicknessB = thicknessOf(b);
    const double reachA = boundingReach(a);
    const double reachB = boundingReach(b);

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
                // Where b's own face sits, measured along `normal` from the
                // region - which is `signedGap` or its negation depending on
                // which way round `normal` came out, and is what the material
                // measurement below has to start from.
                double bFaceOffset = 0.0;
                if (aBehind && bAhead && !aAhead && !bBehind) {
                    normal = pa.Axis().Direction();
                    bFaceOffset = signedGap;
                } else if (aAhead && bBehind && !aBehind && !bAhead) {
                    normal = gp_Dir(pa.Axis().Direction().Reversed());
                    bFaceOffset = -signedGap;
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

                // How much wood is behind the joint on each side, measured
                // along the normal from inside each piece, and capped by that
                // piece's own whole-solid thickness so a board meeting the
                // joint END-ON reports its 18 mm rather than its 600 mm of
                // length. The local measurement is what fixes a hollow carcase
                // (300 mm as a solid, 18 mm of wood at every joint on it) and
                // a rabbeted panel (36 mm as a solid, 18 mm where the shelf
                // actually lands); the cap is what keeps a butt joint's own
                // board thickness right.
                const gp_Dir intoA(normal.Reversed());
                double localA = thicknessA;
                double localB = thicknessB;
                double measuredDepth = 0.0;
                if (materialDepthBehind(a, onRegion.Translated(gp_Vec(intoA) * probeMm),
                                        intoA, probeMm, reachA, measuredDepth)) {
                    localA = std::min(thicknessA, measuredDepth);
                }
                if (materialDepthBehind(
                        b, onRegion.Translated(gp_Vec(normal) * bProbeMm), normal,
                        bProbeMm - bFaceOffset, reachB, measuredDepth)) {
                    localB = std::min(thicknessB, measuredDepth);
                }

                // Which piece meets the contact end-on, from FACE COVERAGE: how
                // much of each piece's own contacting face the region covers. A
                // rail's end face IS the region; a stile's edge or a panel's face
                // is far larger than it. See kEndOnCoverageRatio for the rule,
                // why it is relative, and its one named limit. Everything it
                // reads is already here - the winning pair's own two faces and
                // the region's real area - so this is a ratio, not a measurement.
                // Clamped to 1: the region can exceed a face by the boolean's
                // own tolerance, never by a real amount.
                const double faceAreaA = faceArea(fa);
                const double faceAreaB = faceArea(fb);
                const double coverageA =
                    faceAreaA > 1.0e-12 ? std::min(1.0, area / faceAreaA) : 0.0;
                const double coverageB =
                    faceAreaB > 1.0e-12 ? std::min(1.0, area / faceAreaB) : 0.0;
                const bool aEndOn = coverageA > kEndOnMinCoverage &&
                                    coverageA > kEndOnCoverageRatio * coverageB;
                const bool bEndOn = coverageB > kEndOnMinCoverage &&
                                    coverageB > kEndOnCoverageRatio * coverageA;

                bestArea = area;
                result.ok = true;
                result.error.clear();
                result.contact.type = Contact::Type::Face;
                result.contact.frame = gp_Ax3(origin, normal, xDir);
                result.contact.uMin = 0.0;
                result.contact.uMax = u1 - u0;
                result.contact.vMin = 0.0;
                result.contact.vMax = v1 - v0;
                result.contact.thicknessAMm = localA;
                result.contact.thicknessBMm = localB;
                result.contact.endOn = aEndOn == bEndOn ? Contact::EndOn::Neither
                                       : aEndOn         ? Contact::EndOn::A
                                                        : Contact::EndOn::B;
                result.contact.coverageA = coverageA;
                result.contact.coverageB = coverageB;
                // The exact planar area of THIS candidate region - already
                // computed above to decide whether it beats the running
                // best, not a new measurement. Written every time a new
                // best is found, same as every other field here, so it
                // never lags behind whichever candidate actually won.
                result.contact.regionAreaMm2 = area;
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

                        // The same at-the-joint measurement as the Face branch,
                        // taken THROUGH an interior point of the lap along the
                        // lap depth: for two crossing rails that is each rail's
                        // own thickness, which is the number a half-lap's
                        // defaults split in half. The centre of mass is inside
                        // the lap for every crossing this app makes; where it
                        // is not, the whole-solid measure stands rather than a
                        // point off the geometry being trusted.
                        //
                        // NOT capped by the whole-solid thickness, unlike the
                        // Face branch. The cap exists there to stop an END-grain
                        // contact reporting a board's length; a lap has no end
                        // grain - the depth direction IS the material a half-lap
                        // splits - and capping it would report a 60 x 300 x 100
                        // rail as 60 mm thick at a lap that has 100 mm to halve.
                        double localA = thicknessA;
                        double localB = thicknessB;
                        BRepClass3d_SolidClassifier insideLap(lap);
                        insideLap.Perform(seed, 1.0e-6);
                        if (insideLap.State() == TopAbs_IN) {
                            double through = 0.0;
                            if (materialDepthThrough(a, seed, depth, reachA, through))
                                localA = through;
                            if (materialDepthThrough(b, seed, depth, reachB, through))
                                localB = through;
                        }

                        result.ok = true;
                        result.contact.type = Contact::Type::Overlap;
                        result.contact.frame = gp_Ax3(origin, depth, xDir);
                        result.contact.uMin = 0.0;
                        result.contact.uMax = u1 - u0;
                        result.contact.vMin = 0.0;
                        result.contact.vMax = v1 - v0;
                        result.contact.thicknessAMm = localA;
                        result.contact.thicknessBMm = localB;
                        // A lap has no end grain - both rails carry on past
                        // it the same way - so there is no host to name.
                        result.contact.endOn = Contact::EndOn::Neither;
                        // Left at the sentinel, EXPLICITLY rather than by
                        // omission: the only already-computed area-shaped
                        // quantity here is the lap SOLID's own volume
                        // (`volProps.Mass()`, mm3, from
                        // BRepGProp::VolumeProperties above), and writing a
                        // volume into a field compared against `uLength() *
                        // vLength()` (mm2) would be exactly the
                        // wrong-unit-but-plausible-looking number this
                        // sentinel exists to prevent. A real footprint area
                        // - the lap's own cross-section, perpendicular to
                        // the lap depth - is not computed anywhere today and
                        // would need a genuinely new measurement (a section
                        // cut through the lap solid), which this task does
                        // not add.
                        result.contact.regionAreaMm2 = kUnmeasuredRegionAreaMm2;
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

std::string validityOf(Kind kind, const Contact& contact)
{
    const bool crossing = contact.type == Contact::Type::Overlap;
    if (kind == Kind::HalfLap) {
        if (!crossing) return "the pieces aren't crossing";
    } else if (crossing) {
        return "the pieces overlap rather than meet";
    }

    // Enough room for the joint at all: a run shorter than three sizes has
    // nowhere to put a row, and a contact narrower than the joint's own
    // thickness cannot hold it.
    const double run = contact.runLength();
    const double across =
        contact.runsAlongU() ? contact.vLength() : contact.uLength();
    if (run < 30.0) return "the contact is too small for a joint";
    switch (familyOf(kind)) {
        case Family::Fasteners:
            if (across < 6.0) return "the contact is too narrow for fasteners";
            break;
        case Family::Housing:
            if (across < 6.0) return "the contact is too narrow to house a piece";
            break;
        case Family::Interlock:
            if (across < 9.0) return "the contact is too narrow for an interlock";
            break;
    }
    return std::string();
}

std::vector<Kind> validKindsFor(const Contact& contact)
{
    static const Kind kAll[] = {Kind::Dowel,  Kind::PocketScrew,  Kind::Biscuit,
                                Kind::Domino, Kind::Screw,        Kind::Dado,
                                Kind::Rabbet, Kind::Groove,       Kind::MortiseTenon,
                                Kind::HalfLap};
    std::vector<Kind> offered;
    for (const Kind kind : kAll) {
        if (validityOf(kind, contact).empty()) offered.push_back(kind);
    }
    return offered;
}

std::string regionShortfallCaveat(const Contact& contact)
{
    // Unmeasured (today, always an Overlap contact) - nothing to say, not
    // "it fills its rectangle". See kUnmeasuredRegionAreaMm2's own comment.
    if (contact.regionAreaMm2 < 0.0) return std::string();

    const double rectArea = contact.uLength() * contact.vLength();
    // Nothing to compare a real area against - validityOf's own size floor
    // refuses a contact this small anyway, so this is a defensive no-op
    // guard, not a case this function expects to matter in practice.
    if (rectArea <= 1.0e-9) return std::string();

    // The region can never be LARGER than the rectangle that bounds it, so
    // this fraction is at most 1.0 for any real measurement; the >=
    // comparison below still reads correctly if floating-point noise ever
    // pushes it a hair over. A genuine rectangle's own measured area agrees
    // with uLength() * vLength() to a handful of parts in a million (both
    // sides come from exact geometry, no tessellation on either one), while
    // a real shortfall - an L-shaped 90,000 mm2 rectangle over an
    // 11,952 mm2 region, 87% short - is nowhere near that band. The
    // tolerance is therefore about floating-point noise, not taste: unlike
    // a plausibility threshold with no safe case to anchor it against, a
    // rectangle either IS its own region or it is not.
    constexpr double kFillFraction = 1.0 - 1.0e-4;
    const double fraction = contact.regionAreaMm2 / rectArea;
    if (fraction >= kFillFraction) return std::string();

    return "this contact isn't a plain rectangle — part of the joint may "
           "land where the two pieces don't actually touch";
}

namespace {

// The row a family of fasteners runs along, in contact coordinates: the
// long axis carries the items, the short one carries the inset.
//
// Works entirely in (u, v) and never reads `contact.frame` - which is what
// makes this arithmetic frame-independent: the same joint tilted into an
// oblique frame lays out at the identical u/v positions, because nothing
// here has an opinion about where the frame points in the world. See
// `layout()`'s own comment (and `Contact`'s) for the bounding-rectangle
// caveat this inherits: `runMin`/`acrossMin` come straight from the
// contact's bounding rectangle, so an item this places can land off a
// non-rectangular region.
void fastenerRow(const Contact& contact, const Parameters& params,
                 std::vector<Item>& out)
{
    const bool alongU = contact.runsAlongU();
    const double runMin = alongU ? contact.uMin : contact.vMin;
    const double runLen = contact.runLength();
    const double acrossMin = alongU ? contact.vMin : contact.uMin;
    const double acrossLen = alongU ? contact.vLength() : contact.uLength();

    const int count = std::max(1, params.count);
    // A margin wider than the joint would put the first item past the last;
    // clamped so every item stays inside the contact whatever is typed.
    const double margin = std::min(params.endMarginMm, runLen / 2.0 * 0.9);
    const double first = runMin + margin;
    const double span = std::max(runLen - 2.0 * margin, 0.0);
    const double step = count > 1 ? span / double(count - 1) : 0.0;
    const double across =
        acrossMin + std::clamp(params.insetMm, 0.0, std::max(acrossLen, 0.0));

    for (int i = 0; i < count; ++i) {
        const double along = count > 1 ? first + step * double(i) : runMin + runLen / 2.0;
        Item item;
        item.u = alongU ? along : across;
        item.v = alongU ? across : along;
        item.sizeMm = params.sizeMm;
        item.depthAMm = params.depthAMm;
        item.depthBMm = params.depthBMm;
        item.angleDeg = params.angleDeg;
        out.push_back(item);
    }
}

// A housing is ONE region: the full run of the contact (less the stop, if
// it is blind), as wide as the piece being housed, cut `depthAMm` into the
// host.
//
// Like fastenerRow above, this reads the contact's BOUNDING RECTANGLE
// (uMin/uMax/vMin/vMax, runLength()) - see the long comment on `Contact` and
// on `layout()` in the header. A housing spanning the full run of a
// non-rectangular contact (an L, a C, a stadium) can therefore describe a
// channel partly cut through thin air; nothing here invents a containment
// test to catch that, by the same ruling layout() already documents.
void housingRegion(const Contact& contact, const Parameters& params,
                   std::vector<Item>& out)
{
    const bool alongU = contact.runsAlongU();
    // stopMm is clamped into [0, runLength] BEFORE the span arithmetic, not
    // after: a negative stop must not lengthen the channel past the joint
    // it crosses (measured: -10 mm used to yield a 310 mm span on a 300 mm
    // contact), and a stop at or past the whole run must floor the span at
    // zero rather than drive it negative before std::max catches it. When
    // `stopped` is false, stopMm is ignored outright, unclamped and
    // unread - a blind flag with a stray value in an unrelated field must
    // never leak into a through housing.
    const double stop = params.stopped
                            ? std::clamp(params.stopMm, 0.0, contact.runLength())
                            : 0.0;
    const double runLen = std::max(contact.runLength() - stop, 0.0);
    // The clamp bound is the ACROSS extent - the dimension a channel's width
    // is measured against - not the run itself: mirrors interlockRegion's
    // own thickness clamp three lines below rather than hardcoding uLength
    // or vLength, so it stays right whichever way the contact runs.
    // (Previously bounded against max(uLength, vLength), which is the ALONG
    // dimension whenever the contact is longer than it is wide - measured,
    // a 250 mm widthMm passed through uncapped on an 18 mm wide contact.)
    const double width = std::clamp(
        params.widthMm, 0.0,
        std::max(alongU ? contact.vLength() : contact.uLength(), 0.0));
    Item item;
    item.u = alongU ? contact.uMin + runLen / 2.0
                    : contact.uMin + contact.uLength() / 2.0;
    item.v = alongU ? contact.vMin + contact.vLength() / 2.0
                    : contact.vMin + runLen / 2.0;
    item.spanUMm = alongU ? runLen : width;
    item.spanVMm = alongU ? width : runLen;
    item.sizeMm = width;
    item.depthAMm = params.depthAMm;
    item.depthBMm = 0.0;
    out.push_back(item);
}

// An interlock is ONE region too, but centred and inset from both ends -
// a tenon leaves shoulders, a half-lap takes the whole overlap.
//
// Same bounding-rectangle caveat as housingRegion just above: centred on
// contact.uMin + contact.uLength() / 2.0, which is the bounding rectangle's
// own centre and not guaranteed to sit on a non-rectangular region.
void interlockRegion(Kind kind, const Contact& contact, const Parameters& params,
                     std::vector<Item>& out)
{
    const bool alongU = contact.runsAlongU();
    const double runLen = contact.runLength();
    const bool lap = kind == Kind::HalfLap;
    // A tenon is inset from both ends by a shoulder; a half-lap fills its
    // overlap outright - along the run AND across it, since a lap removes
    // material over the whole crossing, not over a tenon-thick strip of it.
    const double shoulder = lap ? 0.0 : runLen * 0.15;
    const double span = std::max(runLen - 2.0 * shoulder, 0.0);
    const double acrossLen = std::max(alongU ? contact.vLength() : contact.uLength(), 0.0);
    const double thickness = lap ? acrossLen : std::clamp(params.thicknessMm, 0.0, acrossLen);

    Item item;
    item.u = contact.uMin + contact.uLength() / 2.0;
    item.v = contact.vMin + contact.vLength() / 2.0;
    item.spanUMm = alongU ? span : thickness;
    item.spanVMm = alongU ? thickness : span;
    item.sizeMm = thickness;
    item.depthAMm = params.depthAMm;
    // A tenon's second depth is its length into B; a half-lap's is the half
    // it removes from B.
    item.depthBMm = lap ? params.depthBMm : params.lengthMm;
    out.push_back(item);
}

}  // namespace

std::vector<Item> layout(Kind kind, const Parameters& params, const Contact& contact,
                         const std::vector<Adjustment>& adjustments)
{
    std::vector<Item> items;
    switch (familyOf(kind)) {
        case Family::Fasteners:
            fastenerRow(contact, params, items);
            break;
        case Family::Housing:
            housingRegion(contact, params, items);
            break;
        case Family::Interlock:
            interlockRegion(kind, contact, params, items);
            break;
    }

    // Adjustments are applied in the contact's own coordinates, which is
    // what lets a hand-placed dowel keep its intent when the pieces move.
    for (const Adjustment& adj : adjustments) {
        if (adj.index < 0 || adj.index >= static_cast<int>(items.size())) continue;
        items[static_cast<std::size_t>(adj.index)].u += adj.du;
        items[static_cast<std::size_t>(adj.index)].v += adj.dv;
    }

    // The world position is DERIVED from the contact frame, last, so an
    // adjustment cannot leave the two disagreeing.
    for (Item& item : items) {
        item.centre = contact.at(item.u, item.v);
        item.axis = contact.frame.Direction();
    }
    return items;
}

namespace {

// A candidate axis has to swamp the other two before its word is trusted.
// The reader measures from this word with a pencil and a square, so a
// confidently wrong name is worse here than anywhere else in this feature -
// on a board the transform gizmo has spun 40 degrees, no face is honestly
// "front" any more.
//
// The threshold sits at cos(25 degrees) ~ 0.9063, deliberately chosen
// between the two cases that settle it: a direction 0.95 along an axis
// (about 18 degrees off it) still reads as that axis's word, while one
// split evenly between two axes (0.7071/0.7071 each - a board tilted
// diagonally, or a frame genuinely tipped in 3D) clears neither candidate
// and must not be decided by whichever component happens to edge out.
// 0.9063 sits roughly in the middle of that band, not against either edge
// of it.
constexpr double kDominantAxisCos = 0.9063;

// Not one of the six named edges, on purpose - a caller checking for a real
// edge word will not mistake this for one, and a reader marking wood sees a
// sentence explaining why no single edge will do, rather than a word that
// merely happens to be wrong. "no single edge" is the substring every
// reader of this code (and the suite) can search for.
// The clause is separated by an EM DASH, the app's copy rule (CLAUDE.md,
// "Clauses are separated by an em dash") - the joints drawer is the first
// surface that paints this sentence, so it has to read like the rest of it.
const char* const kNoDominantEdge =
    "no single edge — the piece is angled across more than one face";

// A world direction as a word a person can find on the actual board, or the
// honest sentence above when no face is clearly "the" one. ONE function -
// every surface that names a reference edge (today: readout() below; any
// later one) calls this rather than re-deriving the mapping, so the word in
// a drawer and the word anywhere else can never disagree.
std::string edgeName(const gp_Dir& dir)
{
    const double x = dir.X(), y = dir.Y(), z = dir.Z();
    const double ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
    if (std::max({ax, ay, az}) < kDominantAxisCos) return kNoDominantEdge;
    if (az >= ax && az >= ay) return z >= 0.0 ? "top" : "bottom";
    if (ay >= ax) return y >= 0.0 ? "back" : "front";
    return x >= 0.0 ? "right" : "left";
}

}  // namespace

Readout readout(Kind kind, const Parameters& params, const Contact& contact,
                const std::vector<Item>& items)
{
    Readout out;
    const bool alongU = contact.runsAlongU();
    const double runMin = alongU ? contact.uMin : contact.vMin;
    const gp_Dir runDir = alongU ? contact.frame.XDirection() : contact.frame.YDirection();
    // Measured from the LOW end of the run, so the named edge is the one
    // the numbers grow away from.
    out.referenceEdgeA = edgeName(gp_Dir(runDir.Reversed()));
    out.referenceEdgeB = out.referenceEdgeA;

    for (const Item& item : items) {
        out.alongMm.push_back((alongU ? item.u : item.v) - runMin);
    }
    out.insetMm = params.insetMm;
    out.depthAMm = params.depthAMm;
    switch (familyOf(kind)) {
        case Family::Fasteners:
            out.depthBMm = params.depthBMm;
            out.widthMm = params.thicknessMm;
            break;
        case Family::Housing:
            // The channel is cut in A alone; B sits in it and is not cut.
            out.depthBMm = 0.0;
            out.widthMm = params.widthMm;
            break;
        case Family::Interlock:
            if (kind == Kind::HalfLap) {
                // A half-lap takes material out of BOTH pieces, each to its
                // own depth, across the WHOLE overlap. Its thicknessMm is half
                // a board by default - a number that describes nothing a
                // person marks - so the width is the lap's own span across
                // the overlap, read off the item layout() cut (interlockRegion
                // gives a lap the full across extent), and the second depth is
                // what comes out of B.
                out.depthBMm = params.depthBMm;
                out.widthMm = items.empty() ? 0.0 : items.front().sizeMm;
            } else {
                // A mortise and tenon: the tenon is thicknessMm thick, and the
                // second depth is how far it reaches into B - its length.
                out.depthBMm = params.lengthMm;
                out.widthMm = params.thicknessMm;
            }
            break;
    }
    return out;
}

Derivation derive(Kind kind, const Parameters& params,
                  const std::vector<Adjustment>& adjustments,
                  const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    Derivation out;
    const ContactResult contact = findContact(a, b);
    if (!contact.ok) {
        out.error = contact.error;
        return out;
    }
    const std::string why = validityOf(kind, contact.contact);
    if (!why.empty()) {
        out.error = why;
        return out;
    }
    out.contact = contact.contact;
    out.items = layout(kind, params, out.contact, adjustments);
    out.readout = readout(kind, params, out.contact, out.items);
    out.ok = true;
    return out;
}

}  // namespace Joinery
