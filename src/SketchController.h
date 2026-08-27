#pragma once
//
// Accumulates clicked points on a sketch plane and turns them into a wire/face.
// Qt-free on purpose: the ray/plane unprojection maths is the part most worth
// testing, and it needs no window.
//
// Milestone 1 sketches on a fixed XY plane at Z=0. Arbitrary planes are already
// representable here - only the UI for choosing one is missing.
//
#include <vector>

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

class SketchController {
public:
    SketchController();

    void setPlane(const gp_Pln& plane) { myPlane = plane; }
    const gp_Pln& plane() const { return myPlane; }

    // Intersects an unprojected screen ray with `plane`. False if the ray is
    // parallel to the plane (a grazing view angle), leaving `out` untouched.
    static bool intersectRayWithPlane(const gp_Lin& ray, const gp_Pln& plane, gp_Pnt& out);

    void addPoint(const gp_Pnt& point);
    void removeLastPoint();
    void reset();

    const std::vector<gp_Pnt>& points() const { return myPoints; }
    std::size_t pointCount() const { return myPoints.size(); }
    bool canClose() const { return myPoints.size() >= 3; }

    // Open polyline through the points so far, for live feedback while
    // sketching. Null shape with fewer than 2 points.
    TopoDS_Shape previewShape() const;

    // Closed wire / planar face from the accumulated points. Null if !canClose().
    TopoDS_Wire closedWire() const;
    TopoDS_Face closedFace() const;

private:
    gp_Pln myPlane;
    std::vector<gp_Pnt> myPoints;
};
