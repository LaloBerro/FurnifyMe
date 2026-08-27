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
