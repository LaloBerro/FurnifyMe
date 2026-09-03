#pragma once
//
// Accumulates clicked points on a sketch plane and turns them into a wire/face.
// Qt-free on purpose: the ray/plane unprojection maths is the part most worth
// testing, and it needs no window.
//
// The plane defaults to XY at Z=0 - the ground - and is replaced wholesale
// when the user locks a face (see MainWindow::lockToFace). It is held BY
// VALUE, never as a reference to the face it came from: face indices are not
// stable across a rebuild, so a re-derived plane could move under the user.
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

    // Rounds a point to the nearest grid intersection *in the plane's own
    // coordinates*, so it stays correct for non-XY planes and never lifts the
    // point off its plane. A step <= 0 returns the point unchanged.
    static gp_Pnt snapToPlaneGrid(const gp_Pnt& point, const gp_Pln& plane, double step);

    // Projects `candidate` onto the LINE through `prev` along `dir` - the
    // straight continuation the user asks for by holding Shift. A line and
    // not a ray: extending the previous segment backwards through `prev` is
    // as much a straight continuation as extending it forwards, and refusing
    // the backward half would make the constraint snap away at exactly the
    // moment the cursor crossed the last point.
    //
    // `dir` is a gp_Dir, so it is unit length by construction and a
    // degenerate direction is not representable here at all - it is refused
    // one level up, by lastSegmentDirection(), which is where two coincident
    // points can actually occur.
    //
    // Plane-safe without knowing about the plane: `prev` lies on the sketch
    // plane and `dir` is derived from two points on it, so every point of the
    // line lies on it too. The headless suite asserts that on a locked
    // vertical plane rather than trusting the argument.
    static gp_Pnt snapToDirection(const gp_Pnt& prev, const gp_Dir& dir,
                                  const gp_Pnt& candidate);

    // The 8-direction dial Shift asks for while sketching (replaces the
    // earlier "continue the previous segment" rule). Snaps the vector from
    // `start` toward `candidate` to the nearest of 8 directions at 45-degree
    // steps, measured in the sketch PLANE's own (u, v) axes - `plane`'s own
    // XAxis is 0 degrees, not world X - so a locked, non-ground plane gets
    // the same dial in its own coordinates, the same discipline
    // snapToPlaneGrid() already follows. `start` is the point BEFORE the one
    // being placed - the previous click, not the segment before that one -
    // so every segment gets its own dial anchored where it begins.
    //
    // False (leaving `out` untouched) when `candidate` coincides with
    // `start`: a zero-length vector has no angle to snap to, and the caller
    // is expected to keep the raw, unsnapped point in that case - degenerate
    // input is refused here rather than guessed at, same as gp_Dir's own
    // raising constructor would refuse it one level up.
    static bool snapToCompass(const gp_Pln& plane, const gp_Pnt& start,
                              const gp_Pnt& candidate, gp_Dir& out);

    // True when `candidate` is within `tolerance` of the first point AND the
    // sketch already has enough points to close - clicking the start point is
    // the second way to finish a sketch, besides Enter.
    bool isNearFirstPoint(const gp_Pnt& candidate, double tolerance) const;

    void addPoint(const gp_Pnt& point);
    void removeLastPoint();
    void reset();

    const std::vector<gp_Pnt>& points() const { return myPoints; }
    std::size_t pointCount() const { return myPoints.size(); }
    bool canClose() const { return myPoints.size() >= 3; }

    // Open polyline through the points so far, for live feedback while
    // sketching. Null shape with fewer than 2 points.
    TopoDS_Shape previewShape() const;

    // Preview including a rubber-band segment out to the live cursor, so the
    // next edge is visible before it is committed. Does not modify the sketch.
    // Null shape until at least one point has been placed.
    TopoDS_Shape previewShapeWithCursor(const gp_Pnt& cursor) const;

    // Closed wire / planar face from the accumulated points. Null if !canClose().
    TopoDS_Wire closedWire() const;
    TopoDS_Face closedFace() const;

private:
    gp_Pln myPlane;
    std::vector<gp_Pnt> myPoints;
};
