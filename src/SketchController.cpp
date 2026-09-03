#include "SketchController.h"

#include "ModelingOps.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <ElSLib.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <Precision.hxx>
#include <TopoDS_Compound.hxx>

#include <cmath>

SketchController::SketchController()
    : myPlane(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0))
{
}

bool SketchController::intersectRayWithPlane(const gp_Lin& ray, const gp_Pln& plane, gp_Pnt& out)
{
    IntAna_IntConicQuad inter(ray, plane, Precision::Angular(), Precision::Confusion());
    if (!inter.IsDone() || inter.IsParallel() || inter.NbPoints() < 1) return false;

    out = inter.Point(1);
    return true;
}

gp_Pnt SketchController::snapToPlaneGrid(const gp_Pnt& point, const gp_Pln& plane, double step)
{
    if (step <= 0.0) return point;

    Standard_Real u = 0.0, v = 0.0;
    ElSLib::Parameters(plane, point, u, v);
    return ElSLib::Value(std::round(u / step) * step, std::round(v / step) * step, plane);
}

gp_Pnt SketchController::snapToDirection(const gp_Pnt& prev, const gp_Dir& dir,
                                         const gp_Pnt& candidate)
{
    // gp_Dir is unit length, so the dot product IS the parameter along the
    // line and no division by |dir|^2 is needed.
    const gp_Vec along(dir);
    return prev.Translated(along * gp_Vec(prev, candidate).Dot(along));
}

bool SketchController::snapToCompass(const gp_Pln& plane, const gp_Pnt& start,
                                     const gp_Pnt& candidate, gp_Dir& out)
{
    const gp_Vec raw(start, candidate);
    // gp_Dir's own constructor RAISES on a zero-length vector - refused here
    // for the same reason lastSegmentDirection() used to refuse a
    // coincident pair, one level up.
    if (raw.Magnitude() < Precision::Confusion()) return false;

    // The plane's own axes, read straight off its gp_Ax3 rather than
    // assumed to be world X/Y - the same care snapToPlaneGrid() takes via
    // ElSLib::Parameters(), so a locked, non-ground plane gets a dial in
    // its own coordinates instead of a meaningless one in world space.
    const gp_Vec u(plane.XAxis().Direction());
    const gp_Vec v(plane.Position().YDirection());
    const double du = raw.Dot(u);
    const double dv = raw.Dot(v);

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kStep = kPi / 4.0;   // 45 degrees
    const double angle = std::atan2(dv, du);
    // Round to the nearest 45-degree sector, symmetric across each boundary
    // at 22.5 + n*45. atan2's range is (-pi, pi], so the raw round can land
    // on -4 as easily as +4 right at the seam - both name the same 180
    // degree line, so folding into [0, 8) collapses them onto one sector
    // instead of leaving an unmatched -4.
    long sector = std::lround(angle / kStep);
    sector = ((sector % 8) + 8) % 8;

    const double snapped = static_cast<double>(sector) * kStep;
    out = gp_Dir(u * std::cos(snapped) + v * std::sin(snapped));
    return true;
}

bool SketchController::isNearFirstPoint(const gp_Pnt& candidate, double tolerance) const
{
    if (!canClose()) return false;
    return candidate.Distance(myPoints.front()) <= tolerance;
}

void SketchController::addPoint(const gp_Pnt& point)
{
    myPoints.push_back(point);
}

void SketchController::removeLastPoint()
{
    if (!myPoints.empty()) myPoints.pop_back();
}

void SketchController::reset()
{
    myPoints.clear();
}

TopoDS_Shape SketchController::previewShape() const
{
    if (myPoints.size() < 2) return TopoDS_Shape();

    // An open polyline: MakePolygon without Close(), so the segment the user has
    // not drawn yet is not implied.
    BRepBuilderAPI_MakePolygon poly;
    for (const gp_Pnt& p : myPoints) poly.Add(p);
    if (!poly.IsDone()) return TopoDS_Shape();
    return poly.Wire();
}

TopoDS_Shape SketchController::previewShapeWithCursor(const gp_Pnt& cursor) const
{
    if (myPoints.empty()) return TopoDS_Shape();

    BRepBuilderAPI_MakePolygon poly;
    for (const gp_Pnt& p : myPoints) poly.Add(p);
    poly.Add(cursor);
    if (!poly.IsDone()) return TopoDS_Shape();
    return poly.Wire();
}

TopoDS_Wire SketchController::closedWire() const
{
    if (!canClose()) return TopoDS_Wire();
    return ModelingOps::makePolygonWire(myPoints);
}

TopoDS_Face SketchController::closedFace() const
{
    const TopoDS_Wire wire = closedWire();
    if (wire.IsNull()) return TopoDS_Face();
    return ModelingOps::makeFaceFromWire(wire);
}
