#pragma once
// The adaptive work-plane grid. Replaces OCCT's finite ActivateGrid patch:
// three concentric bands of line segments whose colours blend toward the
// viewport background with distance, so there is never a visible edge.
// App-layer only - it builds OCCT presentation objects.
//
// The bands are built in the SUPPLIED plane's own coordinates rather than in
// world XY. Locking a face makes that face the sketch plane, and a world-XY
// grid drawn across a vertical face teaches the user nothing about where
// their next point will land. The adaptive step, the three bands and the
// distance fade are unchanged by that - only the frame they are built in
// moves.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

class GridRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);
    // `plane` is the work plane the grid lies on - the ground plane by
    // default, a locked face's own plane while one is locked.
    void update(double cameraDistance, const gp_Pnt& cameraTarget, const gp_Pln& plane);

    // Forces the next update() to rebuild, whatever the camera is doing.
    //
    // The cache below is keyed on everything that changes the grid's
    // GEOMETRY - the step, the plane, the centre, the extent - because those
    // were the only things that could change it. The colours are read from
    // Theme inside rebuild(), which means they are correct at build time and
    // frozen afterwards: a theme edit moves gridMinor(), gridMajor(), the two
    // axis tints and the viewport colour the outer bands fade toward, and not
    // one of them touches the cache key. The grid would then keep the old
    // palette until the camera happened to cross a level boundary. This is
    // the caller's way to say the built grid is stale for a reason this class
    // cannot see - it does not repaint, it only drops the cache, so the next
    // update() does the work exactly once.
    void invalidate();

    static double minorStepFor(double cameraDistance);

    // First line position at or below -limit on the absolute grid of `step`.
    static double firstLineAtOrBelow(double limit, double step);

private:
    Handle(AIS_InteractiveContext) myContext;
    Handle(AIS_InteractiveObject) myGrid;
    double myBuiltStep = 0.0;
    gp_Pnt myBuiltCenter{0.0, 0.0, 0.0};
    double myBuiltExtent = 0.0;
    // Built in the plane's frame, so a change of plane must force a rebuild
    // that no amount of camera-motion caching can skip.
    gp_Pln myBuiltPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)};

    // `center` is in `plane`'s own (u, v) coordinates, not in world space.
    void rebuild(double minorStep, double centerU, double centerV, double extent,
                 const gp_Pln& plane);
};
