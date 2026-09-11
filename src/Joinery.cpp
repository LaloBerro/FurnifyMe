#include "Joinery.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

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

// The smallest bounding-box extent of a shape - a board's own thickness,
// which is what every default is measured against.
double thicknessOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return 0.0;
    Standard_Real x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return std::min({x1 - x0, y1 - y0, z1 - z0});
}

double faceArea(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

// The [min, max] extent of a shape's bounding box projected onto `axis`.
// A genuine touching contact is a SEPARATING plane - each solid sits on
// its own side of it, meeting only within tolerance. Two rails of equal
// thickness crossing at the same height share their top and bottom planes
// exactly (both boxes span the same Z range) without either one actually
// separating anything there - both solids extend the same distance past
// that plane on the SAME side. That is an interpenetrating Overlap
// wearing a coincidentally-coplanar face, not a face contact, and this is
// what tells the two apart: a real contact plane straddles almost none of
// either solid's own extent past it.
void projectedRange(const TopoDS_Shape& shape, const gp_Dir& axis, double& lo, double& hi)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    lo = 0.0;
    hi = 0.0;
    if (box.IsVoid()) return;
    Standard_Real x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    lo = std::numeric_limits<double>::max();
    hi = -std::numeric_limits<double>::max();
    for (int i = 0; i < 8; ++i) {
        const double x = (i & 1) ? x1 : x0;
        const double y = (i & 2) ? y1 : y0;
        const double z = (i & 4) ? z1 : z0;
        const double t = x * axis.X() + y * axis.Y() + z * axis.Z();
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
}

}  // namespace

ContactResult findContact(const TopoDS_Shape& a, const TopoDS_Shape& b,
                          double toleranceMm)
{
    ContactResult result;
    if (a.IsNull() || b.IsNull()) {
        result.error = "one of the pieces is missing";
        return result;
    }

    // The best PLANAR face pair: parallel planes within tolerance whose
    // faces genuinely share area. Projecting b's face onto a's plane before
    // the common is what lets a 0.05 mm gap still count - a boolean between
    // two faces in different planes shares nothing at all.
    double bestArea = 0.0;
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

            // Parallel, either way round - a contact has no preferred side.
            if (std::fabs(std::fabs(pa.Axis().Direction().Dot(pb.Axis().Direction())) -
                          1.0) > 1.0e-6)
                continue;
            const double gap = pa.Distance(pb.Location());
            if (gap > toleranceMm) continue;

            // Reject a coplanar pair that does not actually separate the
            // two solids - see projectedRange()'s comment. Two boards that
            // merely touch project to adjoining (or, within tolerance,
            // barely overlapping) ranges along the plane's own normal;
            // two solids that share this plane while both extending past
            // it on the same side are interpenetrating, not touching.
            double loA, hiA, loB, hiB;
            projectedRange(a, pa.Axis().Direction(), loA, hiA);
            projectedRange(b, pa.Axis().Direction(), loB, hiB);
            const double straddle = std::min(hiA, hiB) - std::max(loA, loB);
            if (straddle > toleranceMm) continue;

            // Slide b's face onto a's plane, then intersect them.
            gp_Trsf onto;
            const gp_Vec shift(pa.Axis().Direction());
            const double signedGap =
                gp_Vec(pa.Location(), pb.Location()).Dot(shift);
            onto.SetTranslation(shift * -signedGap);
            const TopoDS_Shape moved = BRepBuilderAPI_Transform(fb, onto, Standard_True).Shape();

            BRepAlgoAPI_Common common(fa, moved);
            if (!common.IsDone()) continue;
            const double area = faceArea(common.Shape());
            if (area <= bestArea || area < 1.0e-6) continue;

            Bnd_Box box;
            BRepBndLib::Add(common.Shape(), box);
            if (box.IsVoid()) continue;

            // The contact's own frame: origin at the shared region's corner,
            // Z along a's face normal pointing toward b.
            const gp_Dir normal =
                signedGap >= 0.0 ? pa.Axis().Direction()
                                 : gp_Dir(pa.Axis().Direction().Reversed());
            gp_Ax3 frame(pa.Location(), normal);
            Standard_Real x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            const gp_Pnt lo(x0, y0, z0), hi(x1, y1, z1);
            const gp_Vec toLo(frame.Location(), lo), toHi(frame.Location(), hi);
            const double u0 = toLo.Dot(gp_Vec(frame.XDirection()));
            const double u1 = toHi.Dot(gp_Vec(frame.XDirection()));
            const double v0 = toLo.Dot(gp_Vec(frame.YDirection()));
            const double v1 = toHi.Dot(gp_Vec(frame.YDirection()));

            bestArea = area;
            result.ok = true;
            result.error.clear();
            result.contact.type = Contact::Type::Face;
            result.contact.frame = frame;
            result.contact.uMin = std::min(u0, u1);
            result.contact.uMax = std::max(u0, u1);
            result.contact.vMin = std::min(v0, v1);
            result.contact.vMax = std::max(v0, v1);
            result.contact.thicknessAMm = thicknessOf(a);
            result.contact.thicknessBMm = thicknessOf(b);
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
            Bnd_Box box;
            BRepBndLib::Add(solids.Shape(), box);
            Standard_Real x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            gp_Ax3 frame(gp_Pnt(x0, y0, z1), gp_Dir(0.0, 0.0, 1.0));
            result.ok = true;
            result.contact.type = Contact::Type::Overlap;
            result.contact.frame = frame;
            result.contact.uMin = 0.0;
            result.contact.uMax = x1 - x0;
            result.contact.vMin = 0.0;
            result.contact.vMax = y1 - y0;
            result.contact.thicknessAMm = thicknessOf(a);
            result.contact.thicknessBMm = thicknessOf(b);
            return result;
        }
    }

    result.error = "these two pieces don't meet";
    return result;
}

}  // namespace Joinery
