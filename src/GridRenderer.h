#pragma once
// The adaptive work-plane grid. Replaces OCCT's finite ActivateGrid patch:
// line segments with PER-VERTEX colours that dissolve continuously into the
// viewport background toward the edge, so there is never a visible boundary.
// (It used to be three flat-coloured concentric bands, and both band seams
// plus the outer cutoff read as hard rings - the gradient replaced them.)
// App-layer only - it builds OCCT presentation objects.
//
// The grid is built in the SUPPLIED plane's own coordinates rather than in
// world XY. Locking a face makes that face the sketch plane, and a world-XY
// grid drawn across a vertical face teaches the user nothing about where
// their next point will land. The adaptive step and the edge fade are
// unchanged by that - only the frame they are built in moves.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

class GridRenderer {
public:
    // Also creates the Z-layer the grid is drawn in - see zLayer().
    void attach(const Handle(AIS_InteractiveContext)& context);
    // Drops the context and everything built against it, WITHOUT touching the
    // viewer - the caller has already torn that down, or is about to. Called
    // from OcctViewWidget::releaseGlResources(), which is the one place that
    // knows a GL context is dying; after it, this renderer is exactly as it
    // was before attach() and the next attach()/update() pair rebuilds.
    void detach();
    // `plane` is the work plane the grid lies on - the ground plane by
    // default, a locked face's own plane while one is locked. `density`
    // is Theme::gridDensity() - see minorStepFor() below for what it does to
    // the grid; the caller reads it live at every update() so a theme edit's
    // invalidate()+update() pair rebuilds against the value the user just set.
    // Returns TRUE when it actually rebuilt or redisplayed something, so the
    // caller knows whether a frame is owed. This class no longer redraws the
    // viewer itself: OCCT does not own the surface since the QOpenGLWidget
    // migration, so a synchronous UpdateCurrentViewer() from an ordinary Qt
    // slot would draw into Qt's framebuffer with no Qt context current and
    // nothing to composite it - see OcctViewWidget::scheduleRedraw().
    bool update(double cameraDistance, const gp_Pnt& cameraTarget, const gp_Pln& plane,
                double density);

    // Render mode (Milestone 3, item 5) hides the grid outright rather than
    // merely not rebuilding it - the viewport is meant to be the furniture
    // alone. Idempotent, and safe before anything has ever been built: a
    // hide with no myGrid yet is remembered (myVisible) and honoured by the
    // NEXT update()'s own Display call rather than needing a redundant show
    // here. Restoring visibility does not force a rebuild - the cached grid
    // (if any) is simply redisplayed, and the next camera move corrects it
    // exactly as it always does.
    // TRUE when the visibility actually changed - update()'s own contract.
    bool setVisible(bool visible);
    bool isVisible() const { return myVisible; }

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

    // The plane the grid was actually BUILT in - not the one last handed to
    // update(), which may have been cached away. This is what a caller
    // asking "where is the grid standing right now" has to read, and what
    // gui_smoke reads to pin gridPlane()'s own priority rules without a
    // test-only hook on the widget.
    gp_Pln builtPlane() const { return myBuiltPlane; }

    // The Z-layer the grid is displayed in: a layer of this class's own,
    // inserted immediately AFTER Graphic3d_ZLayerId_Default, with depth
    // testing ON and depth WRITING OFF. OcctViewWidget then puts every piece
    // of sketch work - the outline, the markers, the pending face, the
    // dimension - in a third layer after this one. The three-layer order is
    // Default (bodies) -> grid -> sketch work.
    //
    // Why not the obvious underlay before Default. Three things have to hold
    // at once and only this arrangement gets all three:
    //
    //  1. Sketch work draws over the grid, unconditionally. An outline is
    //     EXACTLY coplanar with the grid it is drawn on, which is a
    //     depth-buffer tie - and gridPlane()'s nudge toward the eye tips that
    //     tie the grid's way, so depth alone always loses this. Draw order is
    //     the only thing that can settle it: the sketch layer is rendered
    //     after this one and this one writes no depth, so nothing the grid
    //     draws can reject a sketch pixel.
    //  2. A body in front of a sketch line still occludes it. So the sketch
    //     layer keeps depth testing and does not clear depth - which is
    //     exactly what Graphic3d_ZLayerId_Topmost would have done, and why it
    //     is not used. Grid-under, not sketch-over-everything.
    //  3. The grid on a LOCKED FACE still draws over that face. This is the
    //     one an underlay cannot do: an underlay is rendered before the
    //     bodies, so the face would simply paint over it and the locked-face
    //     grid would vanish. Rendered after the bodies with depth testing on,
    //     the nudge does its original job - the grid is a hair nearer than the
    //     face, so it passes - while a body genuinely in front of the ground
    //     grid still rejects it. Both mechanisms stay: the layer settles draw
    //     ORDER, the nudge settles the DEPTH tie.
    //
    // Graphic3d_ZLayerId_UNKNOWN before attach(), or if the viewer refuses to
    // make the layer - in which case the grid stays in the default layer and
    // behaves exactly as it did before this existed.
    Graphic3d_ZLayerId zLayer() const { return myLayer; }

    // The minor grid step at a given camera distance and density multiplier.
    // `density` scales the distance thresholds below which the grid holds a
    // finer step - a HIGHER density pushes those thresholds OUT, so the finer
    // step covers a wider range of camera distances and the grid reads as
    // denser at any fixed zoom; a LOWER density pulls them IN and the grid
    // coarsens sooner. 1.0 reproduces exactly the thresholds this function
    // has always used. The one-argument overload is 1.0's shorthand, kept so
    // every existing caller and pinned test stays exactly as it was.
    static double minorStepFor(double cameraDistance, double density);
    static double minorStepFor(double cameraDistance) { return minorStepFor(cameraDistance, 1.0); }

    // First line position at or below -limit on the absolute grid of `step`.
    static double firstLineAtOrBelow(double limit, double step);

private:
    Handle(AIS_InteractiveContext) myContext;
    Handle(AIS_InteractiveObject) myGrid;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    // See setVisible(). True by default - the grid is on until something
    // (render mode) asks otherwise.
    bool myVisible = true;
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
