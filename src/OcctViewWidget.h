#pragma once
// OCCT headers first: Handle() is a macro and collides with some Windows headers
// that Qt drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_ManipulatorMode.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Graphic3d_CLight.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_ToneMappingMethod.hxx>
#include <Graphic3d_TypeOfShadingModel.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "CameraController.h"
#include "DimensionRenderer.h"
#include "GridRenderer.h"
#include "PullArrow.h"

#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QPointer>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QSurfaceFormat>

#include <map>
#include <utility>
#include <vector>

// The Qt <-> OCCT bridge. Hosts a V3d_View inside a QOpenGLWidget - OCCT renders
// into the framebuffer object Qt hands it, and Qt composites that frame with the
// rest of the widget tree - and forwards Qt input to the OCCT camera and selector.
//
// It used to be a plain QWidget carrying WA_PaintOnScreen with a real WNT_Window
// (Xw_Window off Windows) attached to its own HWND. That made the 3D frame a
// native OS surface Qt could not see, which is what forced this app's whole
// opaque-paint stratum (no translucency over the viewport, ground fills under
// every floating card, window masks for rounded corners) and produced the
// stale-native-HWND pitfall the compare pane hit in Milestone 3. Hosting OCCT in
// a QOpenGLWidget puts the frame through Qt's own compositor instead. Phase 1 was
// the hosting swap ALONE - the masks and the opaque paint family stayed exactly
// where they were - and Phase 2 deleted them: see CLAUDE.md's "One opaque paint
// family" tombstone for the whole story.
//
// Three things carry the port, and each replaces something the old architecture
// did implicitly:
//   - initializeGL() attaches the view. initializeViewer() still exists and is
//     still lazy, but it no longer touches a window at all - it builds the
//     driver, viewer, view and interactive context, which need no GL context -
//     so the CLAUDE.md pitfall about an unconditional initializeViewer() forcing
//     winId()/native-window realization is retired by construction rather than
//     worked around.
//   - The window OCCT measures itself against is an Aspect_NeutralWindow sized
//     in DEVICE pixels through toDevicePixels(), exactly as the WNT_Window's own
//     client rect was. Aspect_Window::DevicePixelRatio() is deliberately left at
//     its 1.0 default (WNT_Window never overrode it either), so this file's one
//     logical<->device conversion point is still the only place the ratio is
//     applied.
//   - paintGL() is the only place OCCT draws a frame on its own schedule.
//     Everything that used to call V3d_View::Redraw() to put a scene change on
//     screen now calls scheduleRedraw(); the measuring probes, which need pixels
//     before they return, wrap themselves in GlScope instead (see below).
class OcctViewWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    // Solid/Face/Edge are the three modes the rail still shows: one AIS
    // selection mode activated on every body, chosen by the user, with the
    // hover highlight and the click both confined to it.
    //
    // Auto is the ONE behaviour that replaces all three (see
    // docs/superpowers/specs/2026-09-06-auto-selection-design.md). It
    // activates edge AND face selection on every body at once and lets the
    // cursor decide: within kAutoEdgeTolerancePx of an edge the edge glows,
    // otherwise the face does, and a click takes exactly what glows. Bodies
    // stay behind the double-click they already had.
    //
    // Phase 1 builds it INVISIBLY - no action, no chip and no menu entry
    // reaches this value, and setSelectionMode() is the only door. It is a
    // test seam this phase and the DEFAULT the next one, which is what
    // deletes the other three. Nothing in the three classic modes changes
    // shape to make room for it: every branch that names Auto is an
    // additional one.
    enum class SelectionMode { Solid, Face, Edge, Auto };

    // What a pick is OF - the kind Auto's hover arbitrates between and its
    // Shift accumulation locks onto. Body is what a double-click takes; None
    // is "the cursor is over nothing of ours", which is a real answer and not
    // an error.
    enum class PickKind { None, Body, Face, Edge };

    // `viewerOnly` is Milestone 3's compare pane: a second, read-only view
    // of a loaded version alongside the live one. It still gets a real
    // V3d_View/AIS_InteractiveContext - it displays real AIS_Shape
    // presentations - and RMB orbit / MMB pan / wheel zoom all still work
    // (it is a VIEWER, not a picture), but it never activates a selection
    // mode on anything it displays, never runs the hover-highlight MoveTo,
    // and never builds a work-plane grid - see initializeViewer(),
    // displaySolid(), setSolidVisible(), mouseMoveEvent() and
    // mouseReleaseEvent() for the four places that read this flag. A body
    // clicked in this view is therefore never added to
    // selectedSolidIds() - there is no picking to select it with.
    explicit OcctViewWidget(QWidget* parent = nullptr, bool viewerOnly = false);
    ~OcctViewWidget() override;

    bool isViewerOnly() const { return myViewerOnly; }

    // Sets the camera state immediately - no animation, no tween - and
    // pushes it straight onto the OCCT camera through the one function that
    // does that (applyCameraState(), which is what actually emits
    // cameraChanged()). animateTo() is the ordinary route for anything the
    // USER asked for (Fit All, a view snap); this is for the compare pane's
    // bidirectional camera sync, where a 250ms tween on every follow-frame
    // of an orbit would make the second view visibly lag the first one it
    // is supposed to be locked to.
    void setCameraStateNow(const CameraState& state);

    // The GL surface every OcctViewWidget in this process needs, in ONE place:
    // 24-bit depth and 8-bit stencil (OCCT's own minimum for a 3D view), and a
    // compatibility profile, which is what OCCT's ray-tracing tiers have
    // historically wanted - Phase 3 is what finalizes that choice against
    // measured tiers rather than assumption.
    //
    // Callers set it as the APPLICATION default before QApplication is
    // constructed (main.cpp and gui_smoke both do; a format chosen after the
    // first context exists is a format nothing reads), and this widget also
    // installs it on itself in its constructor so a third entry point cannot
    // silently get a viewport with no depth buffer. Two call sites, one
    // derivation - the discipline Theme::defaultSpec() already keeps for
    // colour.
    static QSurfaceFormat surfaceFormat();

    // gui_smoke's oracle for the hosting layer, and the two halves of one
    // question: is the window OCCT measures itself against still exactly the
    // widget's own size in DEVICE pixels?
    //
    // It replaces Milestone 3's GetClientRect pin on the native HWND, which
    // asked the same question of an architecture that no longer exists (there
    // is no HWND of our own any more, and asking Qt for one through winId()
    // would itself force the native window this migration exists to remove).
    // hostWindowSize() reads what the Aspect_NeutralWindow actually holds;
    // viewportDeviceSize() is this file's own logical->device conversion point
    // answering for the widget - so a check comparing the two tests the real
    // mechanism rather than re-deriving the ratio itself. Both are QSize()
    // before the GL context exists.
    QSize hostWindowSize() const;
    QSize viewportDeviceSize() const;

    // --- context lifetime, and the suite's oracle for it -------------------
    //
    // The one thing this migration had to OWN. OCCT holds real GPU resources
    // against the OpenGL context Qt gives this widget, and those resources can
    // only be released while that context is alive and current. There are
    // exactly two moments it can die under us - this widget being destroyed
    // (`delete myCompareView`, which MainWindow does on EVERY compare-pane
    // close, and application exit) and Qt destroying the context itself (a
    // driver reset, a reparent `AA_ShareOpenGLContexts` does not cover) - and
    // both now run releaseGlResources() first. Tearing an OpenGl_Window down
    // against a context that is already gone is a hard process crash, measured
    // during this migration; `= default` did exactly that, in member order,
    // with no context current.
    //
    // These three are a MONOTONIC SEQUENCE shared by the two events, so
    // "OCCT's resources were released BEFORE the context died" is a comparison
    // the suite can make rather than a hope. Process-wide, because the thing
    // being asserted is an ORDER between two objects' lifetimes.
    static long long lastGlReleaseTick();
    static long long lastGlContextDeathTick();
    static int glReleaseCount();

    // How many frames this widget has painted since the last time it
    // INVALIDATED the view - the depth of the path tracer's current
    // accumulation run, counted on our side of the wall because OCCT keeps its
    // own accumulation counter private (OpenGl_View::myAccumFrames has no
    // accessor at any level).
    //
    // It exists for one reason and it is worth stating plainly: it is the only
    // way the suite can prove the convergence timer ADVANCES the accumulation
    // rather than restarting it, WITHOUT racing the 4 s convergence window.
    // Every Invalidate() this class performs zeroes it - an Invalidate is
    // exactly "throw the accumulation away" - scheduleAccumulationFrame()
    // deliberately does not, and paintGL() increments it. So a tick wired to
    // scheduleRedraw() can never carry this past 1 however long it runs, while
    // a tick wired to scheduleAccumulationFrame() climbs by one per tick. A
    // revert therefore fails a bounded, deterministic assertion instead of one
    // that depends on whether a timer window happened to still be open.
    //
    // An Invalidate() is not the only thing that throws the accumulation away:
    // V3d_View::Dump() EMPTIES the buffer it reads (awaitPathTracingConvergence()
    // records the measurement), so saveSnapshot() zeroes this too. Without that
    // the counter kept climbing across an export while OCCT started again from
    // one sample, and a suite waiting on it to "recover" was already past its
    // target before the first fresh frame - the counter has to mean what its
    // name says or every assertion standing on it is measuring nothing.
    int accumulationDepth() const { return myAccumulationDepth; }

    // A total paintGL() count that NEVER resets - unlike accumulationDepth(),
    // which scheduleRedraw() zeroes (via Invalidate()) in the SAME call that
    // then leads to one new paint, so a repaint whose own trigger also
    // invalidated nets back to whatever accumulationDepth() already read
    // whenever that was nonzero - it cannot tell "a fresh paint just ran"
    // from "no paint ran at all" in that case. This is the oracle for the
    // question a scheduleRedraw()-triggered repaint actually needs answered:
    // did Qt's paintGL() run, regardless of why.
    int totalPaintCount() const { return myTotalPaintCount; }

    // --- Frame timing ----------------------------------------------------
    // One sample per paintGL(), so "how many frames did that gesture cost and
    // what did each one spend" is a MEASUREMENT rather than a story. Both
    // numbers are microseconds on one monotonic clock shared by every widget
    // in the process, so samples from the live view and the compare view can
    // be read on the same timeline.
    //
    //   beginUs  - when paintGL() started. Successive values are the
    //              present-to-present interval, which is what an apparent
    //              frame rate actually is.
    //   redrawUs - what V3d_View::Redraw() itself cost inside that paint.
    //              Everything between two beginUs values that is NOT this is
    //              Qt's compositing, the swap's vsync block, the overlay
    //              children's own repaints, and whatever ran in the event
    //              loop in between.
    //
    // Kept (rather than measured once and deleted) because the modeling-lag
    // investigation's whole finding is a RATIO between those two numbers, and
    // a ratio nothing can re-measure is a claim with a shelf life. The cost is
    // two clock reads per frame.
    struct PaintSample {
        long long beginUs = 0;
        long long redrawUs = 0;
    };
    static constexpr int kPaintSampleRing = 512;
    // Oldest first, at most kPaintSampleRing entries.
    std::vector<PaintSample> recentPaints() const;
    void clearPaintSamples();

    // Ticks left in the live path-tracing convergence window, 0 when no window
    // is open. Exposed alongside accumulationDepth() and for the same reason:
    // "the real timer is armed" and "the accumulation actually progressed" are
    // two claims, and a suite that could only make the second one would pass
    // against a hand-driven paint loop - which is precisely the coverage gap
    // the whole-branch review named.
    int pathTracingRefineTicksLeft() const { return myPathTracingRefineTicksLeft; }
    // Passes left in the IDLE refinement that carries on after the burst
    // window - the half the user gate on this phase asked for. Non-zero
    // whenever the render is still quietly polishing itself; both counters at
    // zero with the timer stopped is the only resting state where the picture
    // is genuinely final.
    int pathTracingIdleTicksLeft() const { return myPathTracingIdleTicksLeft; }
    // Whether either budget is still live - "is this render still improving".
    bool pathTracingStillRefining() const
    {
        return myPathTracingRefineTicksLeft > 0 || myPathTracingIdleTicksLeft > 0;
    }

    void displaySolid(int id, const TopoDS_Shape& shape);
    void removeSolid(int id);
    void clearSolids();
    // How many bodies are currently DISPLAYED, which is not the same question
    // as how many the document holds - the gap between the two is what a
    // context loss opens (releaseGlResources() empties this map) and what
    // MainWindow's glResourcesReleased() handler closes. outlineCount()'s
    // sibling, added for the same kind of pin.
    int displayedSolidCount() const { return static_cast<int>(mySolids.size()); }

    // Presentation state, not document state: it is deliberately not captured by
    // undo, because hiding something is not an edit.
    void setSolidVisible(int id, bool visible);
    bool isSolidVisible(int id) const;
    // The width of the crease/boundary lines displaySolid() draws on this
    // body's shaded faces, or -1 when it has none (Theme::edgeWidthPx() at
    // 0, or render mode suppressing it). Test-support, on the same terms as
    // hasPreview()/previewShape() below: a test can tell which of Theme's
    // token, render mode's suppression or the OCCT default is actually on
    // screen, rather than trusting a value nothing reads back from the AIS
    // object itself.
    double solidFaceBoundaryWidth(int id) const;

    // A closed outline that is a DOCUMENT ITEM (DocumentModel::Outline), not a
    // preview - Phase 7, item 4. It gets a channel of its own for the reason
    // setPreview()'s comment gives one paragraph down: that slot already has
    // writers, and an item that survives sketches, undo and redo cannot share
    // a slot with feedback that is wiped by the next gesture. The pending face
    // used to be shown THROUGH setPreview, and this replaces that entirely -
    // there is exactly one way to put a closed outline on screen.
    //
    // Displayed with selection mode -1: outlines are not pickable geometry
    // this phase (the drawer row is their handle), so nothing in the viewport
    // can hover, select or bevel one. They render in the sketch Z-layer, above
    // the work-plane grid they lie flat on.
    void displayOutline(int id, const TopoDS_Face& face);
    void removeOutline(int id);
    void clearOutlines();
    bool hasOutline(int id) const;
    int outlineCount() const { return static_cast<int>(myOutlines.size()); }
    // Presentation state, exactly as setSolidVisible() is, and not captured by
    // undo for the same reason.
    void setOutlineVisible(int id, bool visible);
    bool isOutlineVisible(int id) const;

    // Temporary, non-selectable feedback shape (the in-progress sketch).
    void setPreview(const TopoDS_Shape& shape, bool shaded = false);
    void clearPreview();
    // True while a preview shape is actually displayed. Exposed so a caller
    // like ExtrudePreview's own hasPreview() can be checked against the real
    // AIS state rather than trusted as a bare, uncrossed-checked flag - see
    // gui_smoke.cpp's extrude preview block.
    bool hasPreview() const;
    // The shape currently in that single preview slot, or a null shape. Two
    // features write it - MainWindow shows the closed face there, and
    // ExtrudePreview overwrites it with the body it would build - so a test
    // that only asked hasPreview() could not tell which of the two is
    // actually on screen, which is exactly the confusion that let cancelling
    // a preview erase the face.
    TopoDS_Shape previewShape() const;

    // The DEDICATED direct-modeling preview channel, and deliberately not
    // setPreview() above. That slot already has two writers - the in-progress
    // sketch outline and ExtrudePreview's body - and CLAUDE.md records what
    // that cost: cancelling the extrude preview cleared the slot and erased
    // the pending face with it. A third writer on the same slot would be the
    // same bug waiting for a different gesture, so the gizmos get their own.
    // Selection mode -1: feedback only, never pickable, never in the document.
    //
    // `replacesSolidId` names the body this preview stands in for, or -1.
    // That body's presentation is switched to wireframe for as long as the
    // preview is up and restored when it clears - a carve preview sits
    // INSIDE the body it carves, so without this the one operation the user
    // most needs to see would be hidden behind the shape it is changing.
    // SetDisplayMode is presentation state only: unlike Erase it does not
    // touch the selection, which the gizmo's own predicate depends on.
    void setModelingPreview(const TopoDS_Shape& shape, int replacesSolidId = -1);
    // Milestone 5's cross-body bevel: ONE gesture can need a preview per body
    // (one fillet/chamfer build per body it touches), and there is still one
    // dedicated channel, not one per body - it just carries more than one
    // entry at a time now. setModelingPreview() above is exactly the
    // one-entry case of this, kept as the convenience every single-body
    // gizmo (pull, transform, a same-body bevel) still calls; both go
    // through the identical implementation, so a caller previewing two
    // bodies cannot diverge from a caller previewing one.
    void setModelingPreviews(const std::vector<std::pair<int, TopoDS_Shape>>& previews);
    void clearModelingPreview();
    bool hasModelingPreview() const;
    // The one preview shape when exactly one body is being previewed (every
    // gizmo but a cross-body bevel), or a null shape when none is up or more
    // than one is - a multi-body caller built its own shapes and has no need
    // to ask this accessor for a body it did not name.
    TopoDS_Shape modelingPreviewShape() const;

    // The face-pull arrow, drawn in the scene so it stays glued to its face
    // under orbit. See PullArrow.h for the split between this presentation
    // and the Qt value chip.
    void showPullArrow(const gp_Pnt& centre, const gp_Dir& outward);
    void clearPullArrow();
    bool hasPullArrow() const { return myPullArrow.isShowing(); }
    // The outward tip in world space, for placing the value chip. False when
    // no arrow is up.
    bool pullArrowHead(gp_Pnt& out) const;
    // True between the press that grabbed the arrow and the release that ends
    // the pull. While it is true this widget picks nothing on release - see
    // mouseReleaseEvent().
    bool pullDragActive() const { return myPullDrag.active; }

    // The bevel arrow: the same double-headed arrow, perpendicular to a
    // selected edge along the bisector of its two faces' outward normals. See
    // BevelArrow.h for the split between this presentation and the Qt value
    // chip, and PullArrow.h for why the renderer itself is shared rather than
    // copied. The two arrows are never up at once - their predicates need
    // different selection modes - but nothing here depends on that.
    void showBevelArrow(const gp_Pnt& centre, const gp_Dir& outward);
    void clearBevelArrow();
    bool hasBevelArrow() const { return myBevelArrow.isShowing(); }
    // The outward tip in world space, for placing the value chip. False when
    // no arrow is up.
    bool bevelArrowHead(gp_Pnt& out) const;
    bool bevelDragActive() const { return myBevelDrag.active; }

    // Holds the edge-length annotation back while something else is already
    // saying something about that edge. Two annotations on one edge is noise,
    // and the bevel arrow's own value chip is the more specific of the two -
    // so MainWindow raises this for as long as the arrow is up rather than
    // anything reaching into DimensionRenderer directly. Setting it either way
    // re-derives what should be on screen right now, so the annotation comes
    // back on its own when the arrow goes.
    void setEdgeDimensionSuppressed(bool suppressed);
    bool edgeDimensionSuppressed() const { return myEdgeDimensionSuppressed; }

    // The transform gizmo. AIS_Manipulator is OCCT's own: it draws the three
    // arrows, the three rings and the three scale cubes, and it owns the drag
    // maths that turns a cursor position into a gp_Trsf. This widget wires it
    // to Qt's mouse events and nothing more - which is exactly why it lives
    // here and not in a widget of its own, the way PullArrow's value chip
    // needed to (a field has to take a keystroke; a manipulator does not).
    //
    // Attaching is idempotent per body, because the predicate that drives it
    // fires on every appStateChanged and a fresh manipulator on each of those
    // would reset its position mid-gesture.
    void attachManipulator(int solidId);
    void detachManipulator();
    bool hasManipulator() const { return !myManipulator.IsNull(); }
    // The body it is attached to, or -1.
    int manipulatorSolid() const { return myManipulatorSolid; }

    // Where the manipulator is and how big it is, in world units. Exposed so a
    // test can aim at the gizmo's OWN geometry - a hardcoded pixel is a probe
    // that silently stops hitting what it meant to the moment the camera or
    // the body moves.
    bool manipulatorFrame(gp_Ax2& position, double& size) const;

    // The share of the viewport's SMALLER dimension one arm of the transform
    // gizmo may occupy on screen. AIS_Manipulator's own AdjustSize sizes it
    // from the body's bounding box and then leaves it there, which is right at
    // the zoom the body was selected at and wrong at every other: a wardrobe,
    // or any body seen close up, gave a gizmo whose arms ran off all four
    // edges of the viewport with the body invisible behind it.
    //
    // A CAP, not a target. The bounding-box size still wins whenever it is the
    // smaller of the two, so a gizmo never grows to fill this - zooming out
    // shrinks it with the body, exactly as it should - and the clamp only bites
    // when the arms would otherwise be bigger than a hand can aim at.
    static constexpr double kGizmoMaxViewportFraction = 0.15;

    // The manipulation mode hover detection has armed right now: 0 none,
    // 1 Move along an axis, 2 Rotate, 3 Scale, 4 Move in a plane - the values
    // of OCCT's own AIS_ManipulatorMode. A test hovers candidate points and
    // reads this to find a handle, rather than guessing at the arrow lengths
    // the API keeps to itself.
    int manipulatorActiveMode() const;
    // 0, 1 or 2 for the armed part's axis, or -1.
    int manipulatorActiveAxis() const;
    // True between the press that grabbed a manipulator part and the release
    // that ends the gesture. While it is true this widget picks nothing on
    // release - the same rule pullDragActive() carries, for the same reason.
    bool gizmoDragActive() const { return myGizmoDragActive; }

    // The local transformation sitting on a body's PRESENTATION right now.
    // Outside an active gizmo drag it is the identity for every body, because
    // the gizmo moves the presentation and puts it back before it reports -
    // so this is how the "the viewport and the document must never disagree"
    // invariant is asserted. A volume check cannot answer it: a body drawn
    // 200 mm from where the document says it is has exactly the right volume.
    // False for an unknown id.
    bool solidPresentationTransform(int id, gp_Trsf& out) const;

    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const { return mySelectionMode; }

    // --- Auto selection (spec 2026-09-06, Phase 1) -------------------------
    //
    // How near an edge, in LOGICAL pixels, the cursor has to be for the edge
    // to win the hover over the face behind it. It is not a rule this file
    // enforces itself - it is pushed onto OCCT's own selector as its pixel
    // tolerance (in device pixels, converted at the one conversion point this
    // class has), which inflates every edge's sensitive by exactly this much
    // and leaves the arbitration to SelectMgr_SortCriterion. That matters
    // because arbitration is what makes "the highlight is the contract"
    // true by construction rather than by two code paths agreeing: hover and
    // click both go through the same MoveTo, so they cannot disagree about
    // what is under the cursor.
    //
    // OCCT decides the tie the way this behaviour needs, unprompted, and it
    // is worth recording WHY so nobody re-derives it: an edge lies on the
    // face it bounds, so the two candidates come back at the same depth,
    // SelectMgr_SortCriterion::IsCloserDepth() falls through its depth
    // comparison to selection priority, and StdSelect_BRepSelectionTool gives
    // an edge 4 against a face's 2. No explicit SetPriority() call is needed
    // or made; raising the tolerance is the whole mechanism.
    //
    // MEASURED, not chosen: see the auto-selection block in gui_smoke, which
    // walks the cursor away from a real edge one pixel at a time and reports
    // where the hover actually flips. The value below is what that probe
    // measured, and the probe pins both sides of it.
    static constexpr int kAutoEdgeTolerancePx = 8;

    // The CUSTOM selector tolerance Auto asks OCCT for, in logical pixels -
    // deliberately larger than the promise above, and the two are separate
    // numbers because they answer different questions. This one decides
    // whether the edge is a CANDIDATE at all; kAutoEdgeTolerancePx decides
    // whether it WINS, and preferDetectedEdge() enforces that half in screen
    // space so the promise is an exact pixel count rather than whatever
    // radius the selector's frustum scaling happens to produce.
    //
    // It has to be bigger because those two are not the same radius:
    // measured, the selector kept an edge in the candidate list to about
    // 0.75x the custom tolerance (8 -> 6 logical px), so asking for 8 would
    // have capped the promise below its own value. The cost of asking for
    // more is that faces are detected a little further outside the body's
    // silhouette in Auto than in the classic modes - forgiving rather than
    // wrong, and invisible this phase, since nothing reaches Auto.
    static constexpr int kAutoCandidateTolerancePx = 12;

    // "No custom tolerance" - OCCT's own sentinel, which is what the selector
    // holds until something sets one, and what Auto has to hand back on the
    // way out. Named rather than spelled -1 at the call site, because the
    // value is a promise to the three classic modes: they must pick exactly
    // as they always did, and a tolerance left raised behind them would be a
    // user-visible change made by a phase that is not allowed to make one.
    // The value actually restored is CAPTURED from OCCT at construction, not
    // taken from here - see applySelectionTolerance().
    static constexpr int kNoCustomTolerance = -1;

    // What a click RIGHT NOW would take, read off the live detection the last
    // hover left behind - which is precisely what the highlight is standing
    // on. The contract the spec states ("a click picks exactly what glows")
    // is therefore checkable rather than asserted: hover, read this, click,
    // and compare against selectionKind().
    PickKind hoveredKind() const;
    // The exact sub-shape under that highlight, or a null shape. The same
    // question one level finer, so a check can prove the click took THAT edge
    // rather than merely an edge.
    TopoDS_Shape hoveredShape() const;

    // The kind the current selection holds - DERIVED from the selection
    // itself, never stored. That is the whole of Auto's kind lock: there is
    // no cursor to keep in step with undo, a mode switch, a delete or a
    // context loss, because the answer is recomputed from what is actually
    // selected every time it is asked. PickKind::None when nothing is
    // selected, which is what makes the next pick free to be of any kind.
    PickKind selectionKind() const;

    // Why the last Shift-click in Auto did nothing, or an empty string. A
    // Shift-click whose kind differs from selectionKind() is a QUIET no-op -
    // it changes no selection and takes no checkpoint - so something has to
    // say why, or the app looks broken. Phase 2 paints this in the status
    // label; this phase records it and emits autoPickRefused() beside it, so
    // the copy exists, is swept for banned words, and has exactly one author.
    // Cleared by the next pick that actually lands.
    QString autoPickRefusalText() const { return myAutoRefusal; }

    // The CUSTOM selector tolerance OCCT is holding, in DEVICE pixels - what
    // this class asked for, read back off the selector rather than off a copy
    // of it. The oracle for the paragraph above: a check that only asked this
    // class what mode it thinks it is in would be its own oracle, while this
    // reads what OCCT was told - so a tolerance left raised behind Auto,
    // exactly the way this phase could silently change the three classic
    // modes, shows up as the mismatch it is.
    //
    // Not the same number as OCCT's own PixelTolerance(), which adds the
    // largest registered entity sensitivity on top and therefore moves when a
    // body is displayed. -1 before the context exists.
    int selectionPixelTolerance() const;

    // While sketching, a left click reports a point on `plane` instead of selecting.
    void setSketchMode(bool enabled, const gp_Pln& plane);
    bool sketchMode() const { return mySketchMode; }

    // Feedback for the in-progress outline, in a channel of its own -
    // setPreview() above is already shared by two features, and CLAUDE.md
    // records the bug that caused (cancelling the extrude preview erased
    // the pending face). Every placed point gets a small dot; the first
    // additionally gets a ring on top of its dot, because clicking it back
    // is what closes the outline. Rebuilds from scratch each call - the
    // point count here is small enough that caching would be complexity
    // with no payoff. Non-selectable, the same way the preview shape is.
    void setSketchPointMarkers(const std::vector<gp_Pnt>& points);
    void clearSketchPointMarkers();
    // Number of placed-point dots currently displayed - not the count of
    // marker objects, which is one more whenever the start ring is up too.
    int sketchPointMarkerCount() const;
    // Whether the first point's extra ring is currently displayed.
    bool hasSketchStartMarker() const;

    // The live snapped cursor point while sketching - a dot at exactly
    // where the next click will land, which matters most with Snap to Grid
    // on, where the pointer and the click site are not the same pixel.
    void setSketchCursorMarker(const gp_Pnt& point);
    void clearSketchCursorMarker();
    bool hasSketchCursorMarker() const;

    // The plane clicks are unprojected onto. The ground plane until a face is
    // locked. Setting it rebuilds the grid immediately, whether or not a
    // sketch is in progress: locking a face has to be visible before the
    // user starts drawing on it.
    //
    // gridPlane() (see the .cpp) reads this AND myWorkPlaneLocked to decide
    // where the grid itself is drawn, which is no longer always the same
    // plane clicks land on: a locked face still outranks everything, but an
    // UNLOCKED, effectively-orthographic Front/Back/Left/Right look shows the
    // matching world-aligned vertical plane instead of the ground grid
    // collapsed edge-on to a line. That is a purely visual substitution -
    // clicks still land on this plane exactly as they always have - so
    // setWorkPlane() itself stays the one place a click's plane is decided.
    void setWorkPlane(const gp_Pln& plane);
    const gp_Pln& workPlane() const { return mySketchPlane; }

    // Whether workPlane() is a locked face rather than the ground plane -
    // MainWindow is the owner of that fact (myFaceLocked) and reports it here
    // purely so gridPlane() can give a locked face priority over the
    // face-on-ortho substitution above. Call alongside setWorkPlane() at each
    // of MainWindow's two lock/unlock sites; it does not itself touch the
    // grid; the setWorkPlane() call that follows every real lock/unlock
    // already forces the rebuild.
    void setWorkPlaneLocked(bool locked) { myWorkPlaneLocked = locked; }
    bool isWorkPlaneLocked() const { return myWorkPlaneLocked; }

    // The straight-continuation anchor: the last placed point. While Shift
    // is held, the next segment snaps to the nearest of 8 compass
    // directions - 45-degree steps from the sketch plane's own +u axis,
    // measured from this anchor toward the cursor (SketchController::
    // snapToCompass(), computed fresh on every hit test rather than fixed at
    // the moment Shift went down). A reported sketch point - the hover that
    // drives the cursor marker, the status readout and the live dimension,
    // and the click that places the next point - is projected onto whichever
    // of the 8 lines that turns out to be, so all four agree by construction
    // rather than by four call sites each remembering to snap.
    //
    // MainWindow owns the point list and so owns this: it sets the anchor to
    // the sketch's own last point whenever the list changes, and clears it
    // when there is none. No points placed yet means no anchor and Shift
    // does nothing at all - the first point has no "previous point" to dial
    // a direction from. One point is enough, unlike the old previous-segment
    // rule, which needed two: the dial's centre is the anchor itself, not a
    // direction inherited from a segment that came before it.
    void setSketchStraightAnchor(const gp_Pnt& prev);
    void clearSketchStraightAnchor();
    bool hasSketchStraightAnchor() const { return myHasStraightAnchor; }

    // The point that CLOSES the outline - the first one - for as long as
    // clicking it would close it, and nothing otherwise. Shift's straight
    // constraint stands down within sketchCloseTolerance() of it, because a
    // constraint that makes the outline impossible to finish is not a
    // convenience: the projection moves the click off the very point it was
    // aimed at, so the close never fires and the key silently disables the
    // second of the two ways to finish a sketch. Set from the same place the
    // anchor is, off the same point list, so the two cannot disagree about
    // which sketch they describe.
    void setSketchCloseTarget(const gp_Pnt& first);
    void clearSketchCloseTarget();
    bool hasSketchCloseTarget() const { return myHasCloseTarget; }

    // How near the first point a click has to be to close the outline: half a
    // grid step while snapping, a flat 5 mm without it. It lives here rather
    // than in MainWindow because its inputs - mySnapEnabled and mySnapStep -
    // do, and because the straight constraint above has to consult the same
    // number MainWindow decides the close with. Two copies of it would be two
    // answers to "is this click on the start point".
    double sketchCloseTolerance() const;

    // The symmetry plane indicator (Milestone 3): a faint outline in the
    // sketch-work layer, screen-sized via worldPerPixel() (GridRenderer's own
    // idiom - a constant APPARENT size rather than a fixed number of
    // millimetres that shrinks to nothing as the camera pulls back), shown
    // for as long as symmetry is on. `plane` is captured BY VALUE, the same
    // rule every other work plane in this app follows.
    void setSymmetryIndicator(bool on, const gp_Pln& plane);
    // Whether the indicator is genuinely ON SCREEN right now - not merely
    // whether symmetry itself is on. The two differ for as long as render
    // mode is active (Milestone 3, item 5, fix round 1): "the viewport is
    // the furniture alone" applies to this indicator too, and
    // updateSymmetryIndicator() suppresses it structurally while
    // myRenderModeActive - see setRenderMode(). mySymmetryIndicatorOn alone
    // (the mode's own on/off, untouched by render mode) is what
    // setRenderMode() reads to decide whether to bring it back on exit.
    bool symmetryIndicatorShown() const { return mySymmetryIndicatorOn && !myRenderModeActive; }

    // --- Mirror plane placement (Milestone 4, Phase 3) ---------------------
    //
    // The RETROACTIVE half of live symmetry - pairing bodies that already
    // exist, as opposed to Milestone 3's creation-time pairing (which this
    // gesture feeds into: a successful confirm turns symmetryOn() on, so
    // every later extrude keeps mirroring the way it always did). See
    // CLAUDE.md's picked mockup, "Handle and keys": an accent-coloured plane
    // through the selection's own combined centre (restyling the symmetry
    // indicator's own drawing), ONE drag handle at its centre that slides
    // the plane along its own normal (PullArrowRenderer's axisParameterForRay
    // mathematics, one gizmo over), and X/Y/Z keys that jump the plane
    // straight to one of the three axis-aligned presets. MainWindow owns the
    // commit (DocumentModel::pairWithMirror) and the checkpoint/toast; this
    // widget owns only the gesture's own state and presentation - the same
    // split PullArrow/BevelArrow draw between themselves and OcctViewWidget.
    //
    // `ids` is captured BY VALUE at the call and never re-read from the live
    // selection afterwards - the gesture describes exactly the bodies
    // selected when it began, PullArrow's face and BevelArrow's edges are
    // captured the same way.
    //
    // The plane starts TANGENT to the combined bounding box (Bnd_Box,
    // unioned) on the POSITIVE side of the active axis, normal along world
    // X. It used to start at the combined CENTRE, and that default was
    // self-contradictory with the straddle rule this gesture commits
    // through: a plane through a lone body's own centre cuts that body, so
    // DocumentModel::pairWithMirror() skipped it and the headline flow -
    // select one body, S, Enter - refused deterministically, every time,
    // unless the user first discovered they had to drag a 14 px handle
    // clear of the body. Tangent instead, the ghost twin stands beside the
    // body the instant the plane appears and a lone body can never straddle
    // at spawn. Dragging still moves the plane anywhere, including back
    // through the body - the skip rule then applies exactly as designed and
    // the refusal toast says so. A no-op when `ids` is empty.
    void beginMirrorPlacement(const std::vector<int>& ids);
    // Ends the gesture with NOTHING changed - the drag, the orientation and
    // the twin preview are all discarded. Safe to call when nothing is
    // active.
    void cancelMirrorPlacement();
    // Ends the gesture WITHOUT itself changing the document - MainWindow
    // reads mirrorPlacementPlane()/mirrorPlacementIds() BEFORE calling this,
    // commits through DocumentModel::pairWithMirror(), and only then tears
    // the presentation down. Kept a distinct name from cancelMirrorPlacement()
    // so a caller can never blur "the user backed out" with "the gesture
    // committed", even though the viewport-side teardown the two perform is
    // identical.
    void endMirrorPlacement();
    bool mirrorPlacementActive() const { return myMirrorPlacement.active; }
    // The live plane, in world space - MainWindow's own read at Enter. A
    // default-constructed plane while nothing is active.
    gp_Pln mirrorPlacementPlane() const;
    // The ids captured at beginMirrorPlacement(), unchanged for the
    // gesture's whole life.
    std::vector<int> mirrorPlacementIds() const { return myMirrorPlacement.ids; }
    // Jumps the orientation straight to one of the three axis-aligned
    // presets - 0 = X, 1 = Y, 2 = Z, clamped - the X/Y/Z keys' one
    // implementation. Re-places the plane TANGENT on the new axis rather
    // than keeping the dragged offset: an offset measured along the old
    // normal has no honest meaning projected onto a different one, and the
    // spawn rule (see beginMirrorPlacement()) has to hold for every axis a
    // flip can land on, not only the one the gesture opened with. A no-op
    // while no gesture is active or the axis is unchanged.
    void setMirrorPlacementAxis(int axis);
    int mirrorPlacementAxis() const { return myMirrorPlacement.axis; }
    // The plane's signed offset from the selection's own combined centre,
    // along its CURRENT normal, in millimetres - the chip's own number.
    // Starts at the selection's own half-extent along that normal, which is
    // what puts the plane tangent to the box rather than through it.
    double mirrorPlacementOffset() const { return myMirrorPlacement.offset; }
    // The offset that puts the plane tangent to the selection's combined
    // bounding box on the positive side of `axis` - the spawn default and
    // what an X/Y/Z flip re-places to. Zero while no gesture is active.
    double mirrorPlacementTangentOffset(int axis) const;
    // The handle's world position, for placing the value chip and hit-testing
    // the drag - the plane's own location. False while no gesture is active.
    bool mirrorPlacementHandle(gp_Pnt& out) const;
    // True between the press that grabbed the handle and the release that
    // ends the drag - PullArrow's rule, one gizmo over: this widget picks
    // nothing on that release either (see mouseReleaseEvent()).
    bool mirrorPlacementDragActive() const { return myMirrorDrag.active; }

    // The Z-layer every piece of sketch work is displayed in - the in-progress
    // outline and the pending face (setPreview), the direct-modeling preview,
    // the point markers, the cursor marker and the dimension annotation.
    //
    // It is inserted immediately after GridRenderer::zLayer(), which is itself
    // immediately after Graphic3d_ZLayerId_Default, giving the render order
    // bodies -> grid -> sketch work. Depth testing stays ON and depth is NOT
    // cleared, so a body in front of a sketch line still occludes it; the only
    // thing the layer buys is that the grid, which writes no depth, can never
    // reject a sketch pixel. See GridRenderer::zLayer() for the whole argument
    // and for why Graphic3d_ZLayerId_Topmost is the wrong tool here.
    Graphic3d_ZLayerId sketchZLayer() const { return mySketchLayer; }
    // The grid's layer, forwarded so a test can assert the order of the three
    // without reaching through to the renderer.
    Graphic3d_ZLayerId gridZLayer() const { return myGridRenderer.zLayer(); }
    // The viewer's layers in RENDER ORDER, lowest first. Exposed because two
    // ids being different says nothing about which is drawn first, and draw
    // order is the entire contract here. Empty before the viewer exists.
    std::vector<Graphic3d_ZLayerId> zLayerOrder() const;
    // What a layer actually promises - the grid's depth write off, the sketch
    // layer's depth NOT cleared. Asserting the settings rather than the ids
    // is what makes the Topmost refusal a check instead of a comment.
    Graphic3d_ZLayerSettings zLayerSettings(Graphic3d_ZLayerId layer) const;

    // The single selected face, or a null face when the selection is not
    // exactly one face. Deliberately not "the first selected face": Lock to
    // Face is enabled off this, and locking one of several highlighted faces
    // would be a coin toss the user cannot see.
    TopoDS_Face selectedFace() const;

    // The single selected edge, or a null edge otherwise - the same rule as
    // selectedFace(), for the same reason. This is what the dimension falls
    // back to when the cursor leaves an edge the user has selected: a length
    // annotation over two highlighted edges could only ever measure one of
    // them, so it measures neither.
    TopoDS_Edge selectedEdge() const;

    // EVERY selected edge, which a bevel - unlike the dimension - can act on
    // all of at once. Shift-click accumulates them through the same
    // AIS_SelectionScheme_XOR the additive body pick uses.
    std::vector<TopoDS_Edge> selectedEdges() const;

    // The one the bevel arrow stands on: the edge the user picked LAST, so a
    // multi-edge gesture is measured where the hand last was rather than
    // wherever OCCT happens to iterate first. Falls back to the last entry of
    // selectedEdges() when the remembered edge is no longer selected (a
    // Shift-click that toggled it back off, a rebuild), and is null when
    // nothing is selected.
    TopoDS_Edge lastSelectedEdge() const;

    // Redraws whatever dimension is on screen without changing which span it
    // measures - for a display-unit switch, which changes the label's text
    // under an annotation nothing else would touch until the next mouse move.
    // The frame is this class's to ask for since the QOpenGLWidget migration,
    // and only when something moved - refresh() answers that, and returns
    // false outright when nothing is on screen to refresh. See
    // updateEdgeDimension() for why an unconditional redraw here is not free.
    void refreshDimension() { if (myDimension.refresh()) scheduleRedraw(); }

    // Screen position of a world point, in this widget's coordinates. False
    // when there is no view yet. Exposed for gui_smoke: a test that hardcodes
    // the pixel it clicks is a test that silently stops hitting what it meant
    // to the moment the camera or the model changes.
    bool projectToScreen(const gp_Pnt& world, QPoint& out) const;

    // Snapping applies to points reported while sketching, not to the camera.
    void setSnap(bool enabled, double step);
    bool snapEnabled() const { return mySnapEnabled; }
    double snapStep() const { return mySnapStep; }

    // The live length annotation - the last placed sketch point out to the
    // cursor while sketching, or a hovered edge in edge-selection mode. One
    // instance serves both call sites; see DimensionRenderer.
    DimensionRenderer& dimension() { return myDimension; }

    // The most recent point reported through sketchCursorMoved, so a test can
    // compute the true distance independently rather than trusting the
    // renderer as its own oracle. False before any cursor move on the sketch
    // plane has happened.
    bool lastHoverPoint(gp_Pnt& out) const;

    // World units per screen pixel at the camera's current distance from its
    // target - the same conversion panning already used internally, now
    // shared so DimensionRenderer's furniture (arrowheads, extension gaps,
    // the label) can be sized in constant screen pixels rather than a fixed
    // number of millimetres that shrinks to nothing as the camera pulls
    // back. Both dimension call sites (the live sketch segment in
    // MainWindow, the hovered edge here) read this.
    double worldPerPixel() const;

    // The height, in world units, that the LIVE OCCT camera actually shows at
    // the target's depth - Graphic3d_Camera::ViewDimensions(), which answers
    // correctly for both projections (perspective: the frustum's height at the
    // focal distance; orthographic: the parallel Scale, at every depth).
    //
    // Exposed for one check, and it is the only oracle that can make that
    // check mean anything. worldPerPixel() is computed from the turntable's
    // own distance and never reads the OCCT camera, so comparing it to itself
    // across a projection flip is true by arithmetic whatever OCCT was told.
    // This reads what OCCT was actually told, so a dropped or mis-ordered
    // SetScale() - which leaves the parallel camera at its 1000 default -
    // shows up as the mismatch it is. Returns 0 before the view exists.
    double cameraViewHeightAtTarget() const;

    // The eye-to-target direction the LIVE OCCT camera actually holds -
    // Graphic3d_Camera::Direction() - rather than CameraController's own
    // idea of it. Same oracle discipline as cameraViewHeightAtTarget()
    // above: comparing myCamera.viewDirection() to itself across a snap
    // flight proves nothing about what OCCT was actually told, so Task 6.2's
    // exact-axis checks read this. gp_Dir(0, 0, 1) before the view exists -
    // an arbitrary but harmless default, since no check runs without a view.
    gp_Dir liveCameraDirection() const;

    // The LIVE OCCT camera's up vector - Graphic3d_Camera::Up() - same oracle
    // discipline as liveCameraDirection() just above. Milestone 5 item 4
    // reads this to pin that Top/Bottom are genuinely SQUARED (world +Y up,
    // not merely straight down): comparing myCamera.upVector() to itself
    // would prove only that CameraController agrees with itself, never that
    // OCCT was actually told the squared pose. gp_Dir(0, 1, 0) before the
    // view exists - harmless, since no check runs without a view.
    gp_Dir liveCameraUp() const;

    // Document ids of the selected solids, deduplicated (face-mode selection can
    // hit several faces of one solid).
    std::vector<int> selectedSolidIds() const;
    void clearSelection();
    // Replaces the selection with exactly these solids. Refuses to select a
    // hidden solid - showing it again later must never silently resurrect a
    // selection the user did not make.
    void setSelectedSolids(const std::vector<int>& ids);

    // Renders the viewport straight to an image file. Independent of what is on
    // screen or on top of the window, unlike a screen grab.
    bool saveSnapshot(const QString& path);

    // MainWindow's furniture-thumbnail capture at save time - built on
    // saveSnapshot() itself (V3d_View::Dump has no in-memory sibling) through
    // a short-lived temp file this function owns start to finish, so
    // FurnitureStore's own thumbPath() never has to leave that class (its
    // exact directory layout is FurnitureStore's private business - see
    // FurnitureStore.h). A null image on any failure: Dump refusing, or the
    // PNG it wrote failing to reload.
    QImage captureThumbnail();

    void fitAll();
    void animateTo(const CameraState& goal);
    void setAnimationsEnabled(bool enabled) { myAnimationsEnabled = enabled; }
    bool animationsEnabled() const { return myAnimationsEnabled; }
    void setViewAxonometric();
    // Sketching happens on the XY plane, so a true top view makes clicking
    // accurate in a way the angled default cannot.
    void setViewTop();
    void setViewFront();
    void setViewRight();

    static constexpr double kFovyDeg = 45.0;

    CameraController& camera() { return myCamera; }
    const CameraController& camera() const { return myCamera; }

    // The user's chosen projection - the bar's Persp/Ortho toggle, and the one
    // route to it. Sets the base mode, DROPS any borrowed orthographic look,
    // and pushes both straight onto the OCCT camera.
    //
    // Dropping the loan is what makes the button always change what is on
    // screen. It used to be kept, on the reasoning that the toggle should
    // decide what a face-on look returns TO rather than end it early - which
    // is coherent, and wrong at the only moment it matters: with a loan
    // active, clicking Ortho->Persp left effectiveOrtho() true and the
    // viewport unmoved, twice running. The loan exists for gestures that were
    // not about projection; this gesture is nothing else.
    void setBaseProjection(CameraController::Projection projection);

    // Whether the camera is drawing orthographically RIGHT NOW - read off the
    // live OCCT camera, not off CameraController's own flags. Exposed for
    // gui_smoke for the same reason solidPresentationTransform() is: a check
    // that asked our own state machine whether it had told OCCT something
    // would be its own oracle, and the whole point of the write site in
    // applyCameraState() is that the two agree.
    bool viewIsOrthographic() const;

    // "Top", "Front", ... when the camera is axis-aligned; "Persp" otherwise.
    // The ONE source of that string.
    //
    // Nothing in the shell paints it any more - the bar's button showed it
    // until the Persp/Ortho toggle took that seat, and the projection is what
    // it reads now. It stays because it is the only place that answers "is the
    // camera square onto a world axis, and which one", which is what the
    // suite asserts a snap flight against; a check that recomputed that from
    // azimuth and elevation itself would be a second copy of the tolerance.
    // If a direction readout ever comes back, this is what it reads.
    QString viewDirectionName() const;

    // Whether the CURRENT camera is an effectively orthographic look square
    // onto a world axis - "Front", "Back", "Right" or "Left" if so, empty
    // otherwise (Top, Bottom, Persp, or a non-orthographic look). This is
    // gridPlane()'s own unlocked eligibility test (see faceOnOrthoPlane()
    // below and gridPlane()'s own comment), exposed so a caller outside this
    // class - the status label's cue - can read the same answer rather than
    // re-deriving "which views count" itself.
    QString faceOnOrthoDirection() const;

    // The vertical world plane a face-on Front/Back/Left/Right look is
    // squared onto - world XZ (normal +Y) for Front/Back, YZ (normal +X)
    // for Right/Left, both through the origin - or the ground plane when
    // faceOnOrthoDirection() is empty. This is gridPlane()'s own UNLOCKED
    // half, factored out so MainWindow::onStartSketch() can derive the
    // plane a new outline actually lands on by the identical rule the grid
    // is already drawn on, rather than a second construction of the same
    // two planes that could silently drift from this one.
    gp_Pln faceOnOrthoPlane() const;

    // The plane the ground grid is actually STANDING in right now - the one
    // GridRenderer last built from, not gridPlane()'s freshly-computed
    // answer, so a caller reads the state on screen rather than the formula
    // that produced it. Exposed for gui_smoke's pin on gridPlane()'s
    // priority order (locked face > sketch in progress > face-on ortho >
    // ground); the drawn grid's own APPEARANCE is still measured off Dump
    // pixels, per CLAUDE.md, and this answers a different question.
    gp_Pln drawnGridPlane() const { return myGridRenderer.builtPlane(); }

    // Re-dresses everything on the OCCT side of the bridge from the current
    // Theme spec: the background the view clears to, the two highlight
    // drawers, and the ground grid, which is rebuilt because its colours are
    // baked into the line segments at build time (see GridRenderer::invalidate).
    //
    // The Qt side needs nothing equivalent - every widget in the shell asks
    // Theme for its colours inside paintEvent(), so a repaint is enough. The
    // viewport is the exception because none of this is painted by Qt at all:
    // the background is a driver clear colour, the highlights are Prs3d
    // drawers held by the interactive context, and the grid is a presentation
    // built once out of coloured vertices.
    //
    // Deliberately does NOT touch the sketch markers or the previews. Those
    // exist only while a gesture is in progress, and MainWindow - which owns
    // that gesture's state - re-issues them, so this class does not have to
    // keep a copy of the points it was last handed just to be able to
    // recolour them.
    void applyTheme();

    void setWireframe(bool wireframe);
    bool isWireframe() const { return myWireframe; }
    // True if this solid's presentation is actually displayed in wireframe right
    // now - queries the live AIS state rather than the requested mode above, so
    // a solid silently reverting to shaded during a resync is observable even if
    // myWireframe itself was never touched.
    bool isSolidWireframe(int id) const;

    // --- Render mode (Milestone 3, item 5) ----------------------------------
    //
    // Strips this viewport down to the raw scene and dresses it as a studio
    // shot: clears and suppresses selection (a real ClearSelected() plus
    // every solid's own selection modes taken out of the context's pick
    // candidates - see the .cpp), hides the work-plane grid, forces the
    // bodies shaded whatever the wireframe toggle says (a render is never a
    // wireframe; the toggle's own state is untouched and honoured again on
    // exit), lays a matte shadow-catcher floor under the furniture, angles
    // the key light so the shadow falls beside the bodies instead of hiding
    // underneath them, switches the rendering pipeline to the best tier this
    // GPU sustains interactively, and swaps the flat viewport colour for a
    // flat light studio backdrop the floor blends into. Session-only:
    // MainWindow never persists this action's checked state, so the app
    // always starts in modeling however it was left.
    //
    // This class owns only the OCCT-side scene: the grid, selection, the
    // rendering params, the backdrop. Every Qt-side surface CLAUDE.md's
    // contract also hides - the rail, the drawers, the axis gizmo card, the
    // three gizmos - is MainWindow's, and is derived off the SAME predicates
    // that already govern them (canPullSelectedFace(), bevelTarget(),
    // transformableBodyId() all refuse while render mode is on, and the
    // rail/drawer visibility lambda reads it directly) rather than pushed
    // from here - CLAUDE.md's sibling-visibility law: derive, never set.
    //
    // A no-op on the compare pane (myViewerOnly) and a no-op when already in
    // the requested state.
    void setRenderMode(bool on);
    bool renderModeActive() const { return myRenderModeActive; }

    // The four tiers the first activation each session probes between, best
    // first - see setRenderMode()'s .cpp comment for how each is measured.
    // PathTracing sits above RayTracing (both drive Graphic3d_RM_RAYTRACING;
    // PathTracing additionally turns on global illumination and adaptive
    // screen sampling - a genuinely different, slower-to-converge look, not
    // a renamed tier), which is why it gets its own, more lenient timing
    // threshold below rather than sharing RayTracing's.
    enum class RenderTier { PathTracing, RayTracing, Shadows, Plain };
    // The tier chosen at the FIRST activation this session, and reused on
    // every activation after that ("cache the tier for the session" - the
    // brief's own words) - Plain before any activation has happened, which is
    // also RenderTier's own zero-cost default should nothing ever probe it.
    // MainWindow reads this the moment setRenderMode(true) returns, to name
    // it in the one Note toast render mode raises on entry.
    RenderTier renderModeTier() const { return myRenderTier; }
    bool renderModeTierProbed() const { return myRenderTierProbed; }

    // What the tier probe actually MEASURED, kept so the decision can be
    // audited rather than only its outcome reported. Phase 3 of the
    // QOpenGLWidget migration is what forced this into the open: "which tier
    // does this machine reach in the wrapped context, and how close was the
    // call" is a question the old surface could not answer at all - a session
    // could fall from PathTracing to Shadows because a probe went 40 ms over
    // a threshold, and nothing anywhere would say so. Every field is filled by
    // probeRenderTier() on the one run it makes; `attempted` is false for a
    // tier the probe never reached (it returned on an earlier one) and
    // `refused` is true only for a tier that threw a Standard_Failure, which
    // is a driver saying no rather than a driver being slow. Milliseconds are
    // -1 where nothing was timed, never 0, so "not measured" and "measured
    // instantly" cannot be confused.
    struct TierProbeTimings {
        bool probed = false;
        // Whether the probe could put a glFinish() between the redraw and the
        // clock. False makes every millisecond below a fiction - see
        // probeRenderTier()'s own comment - so it is recorded rather than
        // assumed, and gui_smoke asserts it.
        bool gpuSyncAvailable = false;
        bool pathTracingAttempted = false;
        bool pathTracingRefused = false;
        int pathTracingMs = -1;
        bool rayTracingAttempted = false;
        bool rayTracingRefused = false;
        int rayTracingMs = -1;
        bool shadowsAttempted = false;
        bool shadowsPixelsDiffered = false;
    };
    TierProbeTimings tierProbeTimings() const { return myTierProbeTimings; }

    // A plain-value read of exactly the Graphic3d_RenderingParams fields
    // saveRenderParams()/restoreRenderParams() round-trip - cameraViewHeight-
    // AtTarget()'s own shape: an oracle for gui_smoke's params round-trip
    // check, without putting Graphic3d_RenderingParams (or the V3d_View
    // handle it lives on) into this class's public surface. The three enum
    // fields come back as plain int so the caller need not include the OCCT
    // headers that declare them just to compare two snapshots for equality.
    struct RenderParamsProbe {
        int method = 0;
        int shadingModel = 0;
        int toneMappingMethod = 0;
        bool isGlobalIlluminationEnabled = false;
        bool adaptiveScreenSampling = false;
        bool isAntialiasingEnabled = false;
        bool isShadowEnabled = true;
        int shadowMapResolution = 1024;
        int nbRayTracingTiles = 256;
    };
    RenderParamsProbe renderParamsProbe() const;

    // The LIGHT half of the same round trip, and new with the user-feedback
    // round because that round is what first gave this class a reason to
    // move an ambient light and a cone angle at all. The failure it guards
    // is the one restoreRenderParams() already guards for the rendering
    // params and for the same reason: an ambient left at
    // kRasterAmbientGain, or a key left at kPathTracingKeyGain, has no
    // visible symptom inside render mode - it is only ORDINARY MODELING
    // that comes back washed out or unlit, once, forever, and the next
    // render-mode entry then saves the wrong "before" state on top of it.
    // Plain values, never a Graphic3d_CLight handle, on
    // renderParamsProbe()'s own terms.
    struct LightRigProbe {
        double keyIntensity = 0.0;
        double keySmoothness = 0.0;
        double ambientIntensity = 0.0;
        bool keyHeadlight = false;
    };
    LightRigProbe lightRigProbe() const;

    // gui_smoke's own oracle for "did the PathTracing tier actually change
    // what is on screen, compared with what the Shadows tier draws on the
    // IDENTICAL scene" - CLAUDE.md's zoom-persistence lesson applied to this
    // task: IsGlobalIlluminationEnabled/AdaptiveScreenSampling succeeding as
    // setters is not proof either one reached a pixel. Two real Dump()s -
    // PathTracing's own live tier, then a TEMPORARY application of Shadows,
    // restored back to PathTracing before this returns so the probe never
    // leaves the session actually rendering a different tier than the one it
    // already cached and told the user about - compared pixel by pixel,
    // probeShadowPixelsDiffer()'s own shape. Returns false (never throws,
    // never a hard failure of its own) when render mode is off, the cached
    // tier is not PathTracing, or either Dump fails to write - the CALLER
    // is what turns "false because PathTracing was never the chosen tier on
    // this GPU" into a pass-with-note rather than a failure, per this task's
    // ruling that only a chosen-but-provably-inert PathTracing tier may fail
    // the check built on this.
    bool probePathTracingChangedImage();

    // gui_smoke's oracle for whether the render-mode floor actually blends
    // into the studio backdrop - CLAUDE.md's own rule for this floor
    // ("calibrated against sampled Dump() pixels, not derived from the
    // lighting equations") extended to Task 7.1's PBR retune (0.875 -> 0.35
    // emission fraction, see the task report), which shipped without a
    // matching measurement. FORCES `forTier` for exactly one Dump() -
    // restored to the session's real cached tier before this returns, the
    // same discipline probePathTracingChangedImage() already follows, and
    // the reason a caller on a PathTracing-only machine can still measure
    // the Shadows-tier calibration this floor was actually retuned against.
    // `floorPointLogical` is a point the CALLER already knows lands on the
    // floor and clear of any body (this widget has no notion of where the
    // test built one); this function finds its own backdrop comparison
    // point by scanning the Dump's top rows for the pixel closest to
    // renderBackdropColour() - the "known grid line" technique gui_smoke's
    // own grid sweep already uses, rather than trusting one hardcoded corner
    // to sit above the floor's horizon regardless of camera framing.
    // `measured` is false - never a hard failure of its own - when render
    // mode is off, the Dump fails, or no floor is on screen to sample; the
    // CALLER is what turns that into a pass-with-note, `probePathTracing-
    // ChangedImage()`'s own rule.
    struct FloorBlendProbe {
        bool measured = false;
        int deltaR = 0;
        int deltaG = 0;
        int deltaB = 0;
    };
    FloorBlendProbe probeRenderFloorBlend(RenderTier forTier, const QPoint& floorPointLogical);

    // gui_smoke's oracle for "does the Shadows tier's floor still show the
    // body's cast shadow" - fix round 2's ruling that the Shadows tier
    // losing that contrast is not acceptable, resurrecting the tier probe's
    // OWN acceptance test (probeShadowPixelsDiffer(), private - already the
    // thing that decides whether the Shadows tier is even offered) as a
    // public, standalone check so a regression in the RESTORED
    // Milestone-3 floor material is caught the same way a regression in the
    // probe itself already would be. Forces Shadows temporarily, restored
    // to the session's real cached tier before returning, on every other
    // forcing probe's own rule. Returns false on any Dump failure, exactly
    // as probeShadowPixelsDiffer() already does for the tier probe itself.
    bool probeRenderFloorShadowContrast();

    // gui_smoke's oracle for "is anything in this render BLACK" - the
    // user-feedback round's own measurement, and the one number their
    // rejection was actually about: a studio shot's shadow sits well under
    // the lit floor but never at zero, and both tiers were measured failing
    // that (Whitted ray tracing's cast shadow read 0.32 of the lit floor;
    // path tracing's whole frame read 0.00, because no material carried a
    // BSDF at all). `lit` is the mean luminance of a small box around a
    // point the CALLER already knows lands on open floor - the same
    // contract probeRenderFloorBlend() uses, for the same reason: this
    // widget has no notion of where the test built a body. `darkest` is the
    // darkest small box anywhere below the frame's top fifth, which is the
    // cast shadow, the contact shadow or an unlit face, whichever this
    // scene makes darkest - deliberately not "the cast shadow"
    // specifically, since which of the three is darkest depends on the
    // body's own proportions, and a probe that had to find one of them by
    // name would be pinned to one test body. Forces `forTier` for exactly
    // one Dump and restores the session's cached tier before returning,
    // probeRenderFloorBlend()'s own rule.
    struct ShadowRatioProbe {
        bool measured = false;
        int lit = 0;
        int darkest = 0;
    };
    ShadowRatioProbe probeRenderShadowRatio(RenderTier forTier,
                                            const QPoint& litFloorPointLogical);

    // ~100 ms - the brief's own number for "is ray tracing still
    // interactive on this GPU", measured against a single redraw. A session
    // constant, not a setting: CLAUDE.md's ruling for this task is that nothing
    // about the tier probe is user-configurable.
    static constexpr int kRenderTierProbeThresholdMs = 100;

    // The path-tracing probe's own, more lenient threshold. A path tracer's
    // FIRST Redraw() after Method flips to RAYTRACING with global
    // illumination on pays for GLSL/compute shader compilation - a real,
    // one-time cost this task measured at several hundred milliseconds even
    // on capable hardware, that never recurs on later frames and therefore
    // says nothing about whether the tier stays interactive. Judging that
    // first frame against the plain tier-2 threshold would refuse GPUs that
    // are actually fine, so this is deliberately its own, wider number
    // rather than a second use of kRenderTierProbeThresholdMs - 1500 ms,
    // chosen to comfortably clear shader-compile cost while staying far
    // short of "the app looks hung."
    static constexpr int kPathTracingProbeThresholdMs = 1500;
    // True again since the user-feedback round found what was actually
    // black: not the floor's geometry and not its material's shape, but
    // Graphic3d_MaterialAspect::myBSDF, which no code path here had ever
    // written. OCCT's path tracer shades from the BSDF alone -
    // SetPBRMaterial() is an inline that assigns myPBRMaterial and touches
    // nothing else, and the classic ambient/diffuse/specular/emissive
    // fields the rasterized and Whitted-ray-traced pipelines read are
    // invisible to it - so every surface in the scene integrated an
    // all-zero BSDF and returned zero radiance. Measured on one scene and
    // camera: floor and bodies at (0,0,0) with the BSDF unset, a correct
    // studio render the moment they carry one, and unchanged black under
    // both a reversed floor face and a closed box slab, which is what
    // ruled the geometry out. See applyRenderBodyMaterials() and
    // applyRenderFloorMaterialForTier().
    static constexpr bool kPathTracingEnabled = true;

    // The studio rig's per-tier gains, every one read off sampled Dump()
    // pixels rather than derived from the lighting equations - CLAUDE.md's
    // rule for this floor, applied to the lights that fall on it. OCCT's
    // SetDefaultLights() rig is a directional key at intensity 20 beside an
    // ambient at 1, calibrated for flat modeling legibility and wildly
    // over-driven for a physically integrated render: with it unchanged the
    // path-traced floor clipped to (255,255,255) at every exposure worth
    // having.
    //
    // PathTracing's pair lands the lit floor on the backdrop tone exactly
    // (measured 194 against a 194 token) with the shadow at 0.65 of it -
    // inside the reference photograph's own 0.65-0.75 band - and they are
    // gains on top of myRenderLightStrength, not replacements for it, so
    // the Light strength control still opens and closes the key by the
    // factor it always did. kPathTracingKeyGain is quoted against the
    // DEFAULT strength of 2.0: 2.0 x 0.08 is the 0.16 that was measured.
    static constexpr double kPathTracingAmbientGain = 0.25;
    static constexpr double kPathTracingKeyGain = 0.08;
    // A directional light with a cone angle is an area light, and an area
    // light is what casts a penumbra - the second thing the user's
    // reference has and this app did not. 0.30 rad (about 17 degrees) is
    // the widest setting that still let the lit floor sit on the backdrop
    // tone: OCCT does not normalize the cone's solid angle, so a wider cone
    // is also a brighter light. It converges far faster than a hard
    // directional light too, which is not incidental - a delta light under
    // this path tracer still showed tile-sized variance after 24
    // accumulation passes where the soft one was clean.
    static constexpr double kPathTracingKeySmoothAngleRad = 0.30;
    // The rasterized and Whitted tiers keep their key exactly as Milestone 3
    // calibrated it and lift only the ambient fill, which is what their
    // near-black unlit faces needed - measured 0.55 of the lit floor before
    // and 0.72 (Shadows) / 0.65 (RayTracing) after. The floor does not move
    // with it: the Milestone-3 floor material's ambient reflectance is zero
    // by construction, so its calibrated blend against the backdrop is
    // untouched, which a measurement confirmed rather than assumed.
    static constexpr double kRasterAmbientGain = 2.0;
    // Path tracing writes its output through an sRGB encode the rasterized
    // and Whitted paths do not, and applies it to the BACKGROUND colour as
    // well - so the same Quantity_Color that rasterizes to the backdrop
    // token (194,191,186) path-traces to (227,225,222), a horizon line
    // across the top of every shot. Measured both ways (the token's own
    // linear value, and a second decode of it) and interpolated between
    // them: 0.632 of the linear value renders the token. A calibration
    // constant on exactly the terms the floor's emissive fraction is one,
    // and a FRACTION of the token rather than a colour, so it tracks an
    // Appearance edit instead of pinning one grey.
    static constexpr double kPathTracingBackdropGain = 0.632;

    // How long, in milliseconds, a PathTracing-tier activation keeps asking
    // Qt to repaint at rest once the camera stops moving - see
    // startPathTracingConvergence()'s own comment for why a live paint loop
    // has to ask for those frames at all. A session constant on the same
    // terms as the two thresholds above: nothing here is user-configurable.
    // 1500 until the user-feedback round, which is 30 accumulation passes
    // at the paint loop's 50 ms tick and is visibly grainy on a scene this
    // open - and grain is not a cosmetic complaint here, it is the thing
    // that destroys the subtle floor and face gradients the reference shot
    // is made of. Path-tracing variance falls as 1/sqrt(samples), so 80
    // passes is a little under half the noise of 30 for work the GPU only
    // does while the camera is at rest, in a mode the user entered
    // specifically to look at a picture.
    // specifically to look at a picture.
    //
    // IT IS NO LONGER WHERE THE POLISHING STOPS - it is where the FAST rate
    // stops. The user gate on this phase was "the render converges and looks
    // right, but visible fine grain remains at rest", and the diagnosis was
    // exactly this constant: the tick ran out after 80 passes and froze
    // whatever noise was left. Every path-traced viewport worth the name keeps
    // refining while nothing changes, so this window is now the BURST - the
    // responsive first polish after a camera move - and kPathTracingIdlePasses
    // below carries on afterwards at a wider interval.
    static constexpr int kPathTracingConvergeMs = 4000;
    // The burst's tick interval, and the floor under the idle one. Was a local
    // constant inside startPathTracingConvergence(); named here because the
    // burst budget above is quoted in passes against it and two rates now read
    // it.
    static constexpr int kPathTracingBurstIntervalMs = 50;
    // How many further accumulation passes the idle refinement drives once the
    // burst window is spent, before it stops for good. Not "forever": an app
    // sitting untouched in render mode should not hold the GPU indefinitely,
    // and past a certain depth another sample changes no pixel a person can
    // see. 1200 on top of the burst's 80 is a total of 1280, and path-tracing
    // variance falls as 1/sqrt(samples) - so the resting image ends up
    // sqrt(1280/80) = FOUR TIMES cleaner than the frozen frame the gate
    // rejected. At the 50 ms this machine's idle rate resolves to that is a
    // little over a minute of quiet polishing; see pathTracingIdleIntervalMs()
    // for why a slower GPU spends it at its own pace instead.
    static constexpr int kPathTracingIdlePasses = 1200;

    // The hard cap on how long an EXPORT will drive convergence before it
    // writes whatever it has - see awaitPathTracingConvergence(). The resting
    // window plus margin: a scene that converges normally never reaches this,
    // and a pathological one still exports rather than hanging on a file
    // dialog the user already dismissed.
    static constexpr int kExportConvergenceCapMs = kPathTracingConvergeMs + 1500;

    // --- Render settings (Task 7.2) -----------------------------------
    //
    // Six values a render-mode session can tune, applied onto the SAME
    // machinery setRenderMode(true) already builds rather than a second
    // scene or a second material/light path. Each setter is a live
    // re-application: it stores the member and, if render mode is
    // currently ON, re-derives whatever it drives right away - a slider
    // drag is therefore a live preview by construction, the same contract
    // AppearancePanel's own setters keep with Theme. Calling one while
    // render mode is OFF only records the value for the next entry - most
    // of them (light/background/material) are harmless either way, but FOV
    // is the one that must never leak outside render mode, which is why it
    // alone is read through effectiveFovyDeg() rather than applied
    // unconditionally (see that function).
    //
    // Persistence is MainWindow's job, exactly as Theme::Spec's is - this
    // class holds these six only as plain session state and neither reads
    // nor writes QSettings itself. Render mode ITSELF stays session-only
    // per CLAUDE.md; these six are not part of that rule and do carry
    // across sessions, the same way the unit choice and autosave do.
    //
    // Surface/Metal are PBR-material terms and apply ONLY on the DEEPEST
    // tier, PathTracing - renderMaterialControlsApply() below is the one
    // written-down copy of that, and usesPbrMaterials() in the .cpp is what
    // it reads. Fix round 2 scoped these to the two ray-traced tiers; the
    // user-feedback round then narrowed them to path tracing alone, for a
    // measured reason recorded at usesPbrMaterials() (Whitted ray tracing
    // does not tone-map and has no indirect bounce, so a PBR floor under it
    // rendered its cast shadow as the near-black the user rejected). This
    // comment claimed the older, wider rule for a whole branch after the
    // narrowing shipped.
    //
    // On RayTracing, Shadows and Plain the body stays Phong-shaded
    // (clearRenderBodyMaterials()'s own UnsetMaterial() contract,
    // unchanged) and these two controls deliberately NO-OP there instead of
    // growing a second, hand-tuned Phong specular mapping beside a floor
    // material this file's own history shows costs a real calibration pass
    // to get right (see applyRenderFloorMaterialForTier()'s block comment).
    // The control stays live and enabled regardless of tier - the value it
    // holds is real and takes effect the moment a session lands on a tier
    // that reads it - but the settings card raises a muted note saying so
    // while the active tier does not, because an enabled control that
    // silently does nothing reads exactly as broken as a disabled one that
    // will not say why.
    //
    // On the deepest tier itself, whether a live edit reaches
    // the next rendered frame turned out to be measured, not assumed - see
    // redrawRenderModeLive()'s own comment for the finding gui_smoke's own
    // per-control Dump checks pinned: renderSurfaceRoughness()/renderMetal()
    // always read back a live edit's new value correctly, but on this
    // OCCT 8.0.1 build/GPU/driver combination the already-ray-traced Dump
    // does not visibly move for it, across five distinct redraw/
    // invalidation strategies this task tried. Light angle (a light's
    // DIRECTION, a per-pixel geometric query) and the background override's
    // own clear-colour half both DO move a real pixel reliably on the same
    // session - it is specifically UNIFORM material/light-intensity
    // properties on an object already ray-traced once that this build's
    // pipeline does not appear to refresh. Ledgered as a measured,
    // environment-specific limitation, the same treatment this file already
    // gives the PathTracing GI floor defect and the AIS_Manipulator styling
    // wall - not evidence this task's own wiring is wrong, which the
    // read-back values already rule out.
    void setRenderSurfaceRoughness(double roughness01);
    double renderSurfaceRoughness() const { return myRenderRoughness; }
    void setRenderMetal(double metallic01);
    double renderMetal() const { return myRenderMetallic; }
    // Whether the CURRENT tier actually reads Surface and Metal - the same
    // usesPbrMaterials() gate the two setters above are wrapped in, exposed
    // so the settings card's own note is derived from the real condition
    // rather than from a second copy of the tier list.
    bool renderMaterialControlsApply() const;

    // Azimuth in degrees around the vertical axis of the studio key light
    // set on render-mode entry; its elevation is the Milestone-3 calibrated
    // constant and is not user-facing - the mockup shows one "Light angle"
    // row, not two. Tier-agnostic: a real Graphic3d_CLight direction, so it
    // moves a pixel on every tier, Phong included.
    void setRenderLightAngleDeg(double azimuthDeg);
    double renderLightAngleDeg() const { return myRenderLightAngleDeg; }
    // A multiplier on each light's own PRE-render-mode intensity (the
    // studio key's old hardcoded "doubled, by measurement" default is
    // exactly multiplier 2.0) - applied against the SAVED base in
    // myRenderSavedLights, never compounded against whatever the last
    // multiplier already wrote, so scrubbing the slider back and forth
    // stays exact rather than drifting.
    void setRenderLightStrength(double multiplier);
    double renderLightStrength() const { return myRenderLightStrength; }

    // An invalid QColor (the default) means "no override" - the backdrop
    // and the floor both fall back to renderBackdropColour()'s own
    // Theme-derived tone. A valid colour overrides it everywhere
    // renderBackdropColour() is read, background and floor alike, so the
    // two can never drift apart - CLAUDE.md's one-derivation law for that
    // function, extended rather than bypassed.
    void setRenderBackgroundOverride(const QColor& colour);
    void clearRenderBackgroundOverride();
    QColor renderBackgroundOverride() const { return myRenderBackgroundOverride; }
    // The EFFECTIVE backdrop - the override while one is set, the
    // Theme-derived studio tone otherwise. A thin public forward onto the
    // private renderBackdropColour() (still the one function both the clear
    // colour and the floor material read - the one-derivation law is
    // unchanged), exposed so MainWindow can seed the settings card's
    // swatch with whatever is actually on screen right now rather than
    // re-deriving the same blend a second time.
    QColor renderBackdropColour() const { return renderBackdropColourImpl(); }

    // Camera vertical FOV, degrees - overrides kFovyDeg while render mode
    // is ON only (effectiveFovyDeg()); setRenderMode(false) always reads
    // kFovyDeg again regardless of what this was left at, so there is no
    // separate "restore" step to get right a second time. worldPerPixel()
    // and applyCameraState() both read effectiveFovyDeg(), never kFovyDeg
    // directly, so the parallel-scale invariant that formula documents
    // keeps holding under a live FOV exactly as it did under a fixed one.
    void setRenderFov(double fovyDeg);
    double renderFov() const { return myRenderFovyDeg; }
    static constexpr double kMinRenderFovDeg = 20.0;
    static constexpr double kMaxRenderFovDeg = 120.0;

signals:
    // The OpenGL context this widget was rendering through has gone, and
    // everything OCCT held on it has been released with it - mySolids,
    // myOutlines, the markers, the manipulator and the render-mode state are
    // all empty, and the next initializeGL() will rebuild an EMPTY viewer.
    //
    // Emitted so the owner can put the document back on screen. Without it the
    // viewport stays permanently blank until the user stumbles into an
    // undo/open/symmetry toggle, and MainWindow's own Render mode action stays
    // checked over a widget whose myRenderModeActive was just cleared - a
    // single-source-of-truth break across the one event nobody drives.
    // Unreachable in ordinary use (AA_ShareOpenGLContexts covers the compare
    // pane's reparent, the only context-rebuilding event this app performs),
    // reachable on a driver reset.
    //
    // NOT emitted from the destructor's own releaseGlResources() call: this
    // object is being destroyed there, its owner is not going to redisplay
    // anything into it, and Qt has not yet disconnected its connections.
    void glResourcesReleased();

    void sketchPointPicked(const gp_Pnt& point);
    // Fired on every camera change so overlays (the axis gizmo) can repaint.
    void cameraChanged();
    // Live cursor position on the sketch plane, already snapped - drives the
    // rubber band and the coordinate readout.
    void sketchCursorMoved(const gp_Pnt& point);
    void selectionChanged();
    // A face CTRL+double-clicked in face-selection mode. MainWindow decides
    // what that means (it locks it); this widget knows nothing about locking.
    // The modifier is what leaves the plain double-click free for the body
    // route below - see mouseDoubleClickEvent().
    void faceDoubleClicked(const TopoDS_Face& face);
    // A body plainly double-clicked while its faces or edges were what was
    // being picked. MainWindow answers by switching to body selection - through
    // the same QAction the rail chip triggers, because the mode is that
    // action's checked state and nothing else may write it.
    void bodyDoubleClicked(int solidId);

    // A live face pull. `distance` is signed along the pulled face's outward
    // normal and measured from the press - positive grows, negative carves -
    // already snapped to the grid step when Snap to Grid is on. Emitted only
    // when the value actually changes, and never at all while the cursor ray
    // is too close to parallel with the arrow to mean anything (see
    // CameraController::axisParameterForRay), so the chip simply keeps the
    // last value rather than jumping.
    void pullDragged(double distance);
    // The end of that gesture. `dragged` is false for a press and release
    // that never moved - a click on the arrow, which is not a pull.
    void pullReleased(bool dragged);

    // A live bevel drag. `size` is signed along the edge's outward bisector
    // and measured from the press, already snapped to the grid step when Snap
    // to Grid is on. NEGATIVE means the drag went inward, into the body, which
    // is the rounding half of this gesture; positive means outward, which
    // flattens. The sign is the whole reason this signal carries one and the
    // chip does not: one axis, two operations.
    void bevelDragged(double size);
    // The end of that gesture, on the same terms as pullReleased().
    void bevelReleased(bool dragged);

    // The end of a transform-gizmo drag. `delta` is the whole accumulated
    // transform of the gesture, ALREADY SNAPPED when Snap to Grid is on -
    // this widget owns the snap state, so snapping here keeps the rule in one
    // place rather than handing a raw transform out and hoping the consumer
    // remembers. An identity `delta` means the drag netted nothing and must be
    // treated as a cancel; the presentation has already been put back either
    // way, so a consumer that ignores this signal entirely still leaves the
    // viewport agreeing with the document.
    void gizmoReleased(int solidId, const gp_Trsf& delta);

    // A live drag of the mirror-plane handle. `offset` is the plane's whole
    // signed distance from the selection's combined centre along its CURRENT
    // normal - not a delta - already snapped to the grid step when Snap to
    // Grid is on, pullDragged()'s own rule. Multiple drags can happen inside
    // one gesture (nudge, release, nudge again) before Enter, which is why
    // this is an absolute position rather than a per-drag distance.
    void mirrorPlaneDragged(double offset);
    // The end of that gesture, on pullReleased()'s own terms: `dragged` is
    // false for a press and release that never moved.
    void mirrorPlaneReleased(bool dragged);

    // A LEFT press landed in the viewport while render mode is active - the
    // brief's "a pick press in the viewport" exit. This widget does not know
    // what leaving render mode means beyond its own scene (MainWindow owns
    // the QAction, the toast and the rest of the chrome, and updateActions()
    // is the single authority that un-checks it), so it asks rather than
    // acts - see mousePressEvent(), which swallows the press unconditionally
    // once it emits this, so the click that exits never also performs
    // whatever pick or gesture it would ordinarily have started.
    void renderModeExitRequested();

    // A Shift-click in Auto that asked for a kind the selection is not
    // holding, and so did nothing at all. `reason` is the sentence
    // autoPickRefusalText() also stores - one author, two ways to read it -
    // and it is emitted rather than shown here because this widget owns no
    // status label. Nothing is connected to it in Phase 1: Auto is not
    // reachable from any control yet, and wiring a slot that can only fire in
    // a mode nothing can enter would be dead code pretending to be a feature.
    void autoPickRefused(const QString& reason);

protected:
    // Qt's three GL callbacks, and the only places a current OpenGL context is
    // guaranteed without asking for one. initializeGL() is where the lazy
    // initializeViewer() contract lands: Qt calls it once, on the first show,
    // which is exactly when the old paintEvent() used to force the native
    // window into existence.
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // The live state of a drag measured along a scene arrow's axis. Two
    // gizmos have one - the face pull and the bevel - and the maths is
    // identical, so it is written once here rather than as two sets of five
    // parallel members that would have to be fixed twice.
    //
    // `hasPressParam` is false when the press landed at an angle
    // CameraController::axisParameterForRay refuses (within ~1.8 degrees of
    // looking straight down the arrow). The gesture is still CLAIMED in that
    // case - see mousePressEvent - and the first move that does resolve
    // anchors it, so the drag contributes nothing until then instead of
    // jumping by whatever the unmeasurable press would have implied.
    struct AxisDrag {
        bool active = false;
        bool moved = false;
        bool hasPressParam = false;
        double pressParam = 0.0;
        double value = 0.0;
    };

    // Builds the viewer, the view and the interactive context - and NOTHING
    // that needs an OpenGL context or a window. Still lazy, still called
    // defensively from every entry point that displays something, and now
    // genuinely safe to call before the widget has ever been shown, which is
    // what the old spelling could not promise (it reached winId()).
    void initializeViewer();
    // Attaches the view to the Aspect_NeutralWindow wrapping Qt's GL context.
    // Called from initializeGL() only - it is the one step that needs a bound
    // context - and re-run whenever Qt rebuilds the context under us.
    bool attachGlWindow();
    // Releases everything OCCT holds on the GPU, in the order OCCT's own
    // QOpenGLWidget sample tears it down, WITH THE OWNING CONTEXT ALIVE AND
    // CURRENT. See the public tick accessors above for why this exists and
    // when it runs. Afterwards this widget is exactly as it was before
    // initializeViewer(): no viewer, no view, no context, no presentations,
    // myInitialized false - so a later initializeGL() rebuilds from scratch
    // rather than reviving handles into a dead context. On the destructor path
    // that is the end of it; on the context-loss path the viewport would be
    // empty until the document is re-displayed - so the caller on that path
    // (initializeGL()'s aboutToBeDestroyed lambda) emits glResourcesReleased()
    // straight afterwards and MainWindow re-displays through the resync it
    // already runs on every undo, open and restore. This function itself does
    // NOT emit: it is also the destructor's teardown, where there is no owner
    // left to tell.
    void releaseGlResources();
    // Wraps the framebuffer object Qt is currently rendering into as OCCT's
    // default FBO, and syncs the neutral window to its size. Run before every
    // frame OCCT draws, because QOpenGLWidget recreates that FBO on resize and
    // on a device-pixel-ratio change without telling anyone.
    bool wrapDefaultFramebuffer();
    // Asks Qt for a frame. THE replacement for every V3d_View::Redraw() that
    // used to mean "put this scene change on screen": OCCT no longer owns the
    // surface, so it does not get to decide when a frame is presented - Qt
    // does, through paintGL(). A no-op with no view.
    void scheduleRedraw();
    // The ONE place this class throws OCCT's accumulated frame away:
    // V3d_View::Invalidate() plus the accumulationDepth() counter that mirrors
    // it. Four callers - scheduleRedraw(), resizeGL(), attachGlWindow() and
    // wrapDefaultFramebuffer()'s size change - and they are four because
    // "the scene moved" and "the surface moved" both end an accumulation run.
    // A bare Invalidate() anywhere else would leave the counter claiming a
    // depth the renderer no longer has.
    void invalidateAccumulation();
    // Asks Qt for a frame that ADDS to the path tracer's accumulation instead
    // of restarting it - scheduleRedraw() without the Invalidate().
    //
    // Phase 3 of the QOpenGLWidget migration is what forced this apart, and it
    // was measured rather than reasoned: a PrintWindow capture of the live
    // window beside the V3d_View::Dump the suite's probes read showed the same
    // scene converged in the Dump and heavily grained on screen. Phase 1
    // rewrote every "put this on screen" Redraw() into scheduleRedraw(), and
    // the path-tracing convergence timer's tick was one of them - so every
    // 50 ms tick told OCCT the scene had moved, the progressive accumulation
    // started over, and the render the user was looking at never got past its
    // first sample no matter how long they left it alone. The probes did not
    // notice because they call Redraw() directly inside a GlScope.
    //
    // The Invalidate() in scheduleRedraw() is right for every OTHER caller -
    // they are all telling us the scene genuinely changed. This one is not: it
    // is asking for one more sample of a scene that has not changed at all,
    // which is the entire point of a progressive renderer.
    void scheduleAccumulationFrame();
    // Makes this widget's GL context current for the life of the scope, with
    // OCCT's default framebuffer wrapper in step - what a SYNCHRONOUS
    // V3d_View::Redraw()/Dump() outside paintGL() needs, since Qt only
    // guarantees a current context inside its own three GL callbacks. Every
    // render-mode measuring probe and every snapshot path opens one.
    //
    // Nesting-safe and re-entrant from inside paintGL(): it only takes (and
    // only releases) the context when it was not already current, so a probe
    // calling another probe cannot leave the outer one running uncurrent.
    class GlScope {
    public:
        explicit GlScope(OcctViewWidget* view);
        ~GlScope();
        GlScope(const GlScope&) = delete;
        GlScope& operator=(const GlScope&) = delete;

    private:
        OcctViewWidget* myWidget = nullptr;
        bool myTook = false;
    };
    // The work plane, nudged a hair toward the eye. Locking a face makes the
    // grid exactly coplanar with a shaded face, and two coplanar surfaces are
    // a depth-buffer tie - stipple, and flicker under camera motion. See the
    // definition for why this is a geometric nudge rather than a ZLayer.
    gp_Pln gridPlane() const;
    // THE Qt/OCCT pixel boundary, in one place each way.
    //
    // Qt reports mouse positions and widget geometry in LOGICAL pixels. The
    // native window this widget handed to OCCT is sized in DEVICE pixels, so
    // V3d_View::Convert, ConvertWithProj and AIS_InteractiveContext::MoveTo
    // all speak device pixels. The two coincide at 100% display scaling and
    // diverge by exactly the scale factor at any other - which is why passing
    // a Qt position straight to MoveTo worked everywhere it was ever tested
    // and missed by half a viewport on a 150% display. The two directions err
    // OPPOSITE ways, and the comment here used to give only one of them:
    //   - a PICK hands OCCT a logical position where a device one is wanted,
    //     so it lands at 1/1.5 of the intended distance from the origin -
    //     UP AND LEFT of where the user clicked;
    //   - projectToScreen() returns a device position where Qt wants a
    //     logical one, so a widget placed at it lands 1.5x too far RIGHT AND
    //     DOWN. That is the half that surfaced this: the pull arrow's value
    //     chip appeared nowhere near its arrow.
    //
    // Everything OUTSIDE these two functions - every signal, every accessor,
    // every caller in gui_smoke - is logical, so a projected point can be
    // clicked and a clicked point can be projected without either side
    // knowing the ratio exists.
    QPoint toDevicePixels(const QPoint& logical) const;
    QPoint fromDevicePixels(int px, int py) const;
    // The unprojected cursor ray - V3d_View::ConvertWithProj, in one place.
    // Both the sketch unprojection and the pull-drag mapping start here, so
    // "where is the cursor pointing" cannot be answered two different ways.
    bool rayThroughPixel(int px, int py, gp_Lin& out) const;
    // `straight` is Shift's straight-continuation request - see
    // setSketchStraightAnchor(). It is a parameter rather than a member read
    // because the modifier belongs to the event that asked, and the two
    // callers (the sketch click, the sketch hover) each have one in hand;
    // reading QGuiApplication::keyboardModifiers() instead would answer from
    // the real keyboard, which no synthetic-event suite can drive.
    bool pointOnSketchPlane(int px, int py, gp_Pnt& out, bool straight = false) const;
    bool pickWorldPoint(int px, int py, gp_Pnt& out) const;
    // Puts `object` in mySketchLayer, if there is one. Called BEFORE Display,
    // never after: SetZLayer stores the id on the object's drawer and Display
    // reads it, so setting it first means the presentation is never computed
    // into the default layer and moved a frame later.
    void markInSketchLayer(const Handle(AIS_InteractiveObject)& object) const;
    // Whether `point` lands on `arrow`, tested in SCREEN space against the
    // arrow's own projected endpoints rather than through AIS - see the
    // comment on PullArrowLines in PullArrow.cpp for why these arrows must not
    // be AIS-pickable objects. Takes the renderer rather than reading a member,
    // because two gizmos are hit-tested exactly this way.
    bool arrowHit(const PullArrowRenderer& arrow, const QPoint& point) const;
    // Anchors `drag` at the press, and advances it on a move - the ONE
    // implementation of "turn a cursor position into a signed distance along a
    // scene arrow's axis, snapped". advanceAxisDrag() returns true when the
    // value actually changed, which is the caller's cue to emit. Both drag
    // gizmos go through these, so the near-parallel refusal, the late anchor
    // and the snap step cannot be remembered in one gesture and forgotten in
    // the other.
    void beginAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at);
    bool advanceAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at);
    // Whether the context's LAST detection landed on the manipulator. The
    // caller is responsible for the MoveTo that produced it, so the question
    // and the answer belong to the same event.
    bool detectedIsManipulator() const;
    // Puts the manipulator's four manipulation modes back into the context's
    // pick candidates. Two callers - the attach, and the restore after an
    // additive pick has taken it out for the duration - so the list of modes
    // lives in one place rather than being repeated and drifting.
    void activateManipulatorModes();
    // Re-derives the manipulator's world size from the camera so its on-screen
    // arms stay inside kGizmoMaxViewportFraction of the viewport's smaller
    // dimension - see that constant. Called from attachManipulator() and from
    // applyCameraState(), because the zoom is half of the arithmetic and the
    // camera is the only thing that moves it.
    //
    // Guarded twice. It does nothing while a gizmo drag is live - resizing the
    // thing under the user's hand mid-gesture would move the handle away from
    // the cursor that grabbed it - and it does not call SetSize() for a value
    // the manipulator already holds, since that recomputes every one of its
    // presentations and this runs on every frame of an orbit.
    void updateManipulatorSize();
    // Rebuilds the symmetry plane indicator from myCamera's current distance
    // (screen-sized, so it has to follow zoom the way updateManipulatorSize()
    // follows it) - guarded the same way, against rebuilding on a camera move
    // that would not visibly change its size. A no-op while the indicator is
    // off.
    void updateSymmetryIndicator();
    // Rebuilds the mirror-placement plane and its handle from the live
    // gesture state - updateSymmetryIndicator()'s own shape, one call site
    // over, but with no equal-guard: this runs on every drag/orientation
    // change, not on every camera tick, so the "did the screen size actually
    // change" cache that indicator needs would only be extra bookkeeping
    // here. A no-op while no gesture is active.
    void updateMirrorPlacementIndicator();
    // The FIXED line a mirror-placement drag is measured along - the
    // selection's own combined centre (never the current, possibly already
    // offset, plane location) and the current orientation's normal. Fixed
    // for exactly the reason PullArrowRenderer::axis() is: measuring from a
    // point that itself moves as the drag proceeds would make the drag
    // measure its own effect.
    gp_Lin mirrorPlacementAxisLine() const;
    // 0/1/2 -> world X/Y/Z, clamped. The one place the axis-index -> gp_Dir
    // mapping lives, so the placement, the drag and the preview cannot each
    // carry a slightly different idea of what "Y" means.
    static gp_Dir mirrorPlacementNormalFor(int axis);
    // Whether `point` lands on the handle, tested in SCREEN space against its
    // own projected position - arrowHit()'s shape, one gizmo over, and for
    // the same reason: an AIS object with a real ComputeSelection would join
    // the ordinary pick pipeline and compete with (or replace) the body
    // selection the gesture is standing on.
    bool mirrorHandleHit(const QPoint& point) const;
    // Reads the accumulated transform, puts the PRESENTATION back to where the
    // document says it should be, snaps, and emits gizmoReleased(). The
    // presentation reset is unconditional and happens here rather than in the
    // consumer: a bake can be refused, and a viewport still showing the
    // dragged pose above a document that never changed is the one outcome
    // this gesture must not be able to produce.
    void endGizmoDrag();
    void applySelectionMode(const Handle(AIS_Shape)& shape);
    // Pushes the selector tolerance the CURRENT mode wants onto the context -
    // kAutoEdgeTolerancePx (converted to device pixels through this class's
    // one conversion point) in Auto, OCCT's own default everywhere else.
    // Called from setSelectionMode() and again from resizeGL(), because the
    // logical->device ratio is not a constant: a window dragged to a display
    // at a different scale would otherwise keep a tolerance measured for the
    // old one, and an edge that used to be 8 px forgiving would silently
    // become 5.
    void applySelectionTolerance();
    // The kind of one detected/selected sub-shape. TopAbs_EDGE and
    // TopAbs_FACE map to themselves; everything else (a solid, a compound -
    // what a whole-body owner carries) is a Body. One mapping, so hover and
    // selection can never classify the same shape differently.
    static PickKind kindOfShape(const TopoDS_Shape& shape);
    // Auto's hover arbitration: with edges and faces both detectable, moves
    // OCCT's highlight onto an edge whenever one is in the candidate list AT
    // THE SAME DEPTH as what OCCT chose. A no-op in every other mode, and a
    // no-op when the edge already won. See the .cpp for the measurement that
    // made this necessary and for the two levers it rejects.
    void preferDetectedEdge(const QPoint& cursor);
    // The sentence a kind-locked refusal reports - see autoPickRefusalText().
    // One author for it, here, rather than one string per refusing branch.
    static QString autoKindRefusalText(PickKind held, PickKind asked);
    // The whole-body pick behind a double-click in Auto: `additive` toggles
    // the body in the selection, otherwise it replaces it. Returns false when
    // nothing of ours is under the detection. Auto has no whole-shape
    // selection mode activated, so this goes through the object's own global
    // owner - the identical route setSelectedSolids() already takes.
    bool selectDetectedBody(bool additive);
    void applyCameraState();
    void stopCameraAnimation();
    // Shows or clears the edge dimension: the edge the last MoveTo detected
    // if there is one, otherwise the single selected edge. Clears outside
    // edge-selection mode. Every route that can change either of those two
    // inputs - a hover, a click that selects, a cleared selection, a body
    // that went away - calls this, because an annotation that only some of
    // them refresh is an annotation that is sometimes a lie.
    void updateEdgeDimension();

    // --- Render mode's own private machinery --------------------------------
    //
    // The tier probe itself: path tracing timed against one redraw, then
    // plain ray tracing timed the same way, falling back to a shadow-mapped
    // directional light, falling back to plain rasterization - see the .cpp
    // for the full argument on each step and why each is measured rather
    // than trusted as setter data.
    RenderTier probeRenderTier();
    // Writes `tier`'s rendering params (Method, IsShadowEnabled, GI/adaptive
    // sampling/antialiasing for path tracing, the PBR shading model and
    // tone-mapping method that CLAUDE.md's brief for this task asks applied
    // "while render mode is on" regardless of which tier renders it, which
    // lights cast shadows) onto the live view WITHOUT timing or Dump-probing
    // anything - the cheap reapplication path a cached tier uses on every
    // activation after the first. Every field it writes is one
    // saveRenderParams()/restoreRenderParams() round trip covers - see those
    // two below.
    void applyRenderTier(RenderTier tier);
    // Snapshots every Graphic3d_RenderingParams field applyRenderTier() (or
    // this function's own PBR sibling) can write, from the LIVE view, before
    // setRenderMode(true) touches any of them - the lights discipline
    // (myRenderSavedLights) extended to the rendering pipeline's own state.
    // Captured fresh on every entry rather than once per process, on the
    // same reasoning: nothing outside this class ever changes these fields,
    // but reading them live rather than hardcoding "what render mode always
    // resets to" is what makes the restore provably correct instead of
    // merely assumed correct.
    void saveRenderParams();
    // Writes the snapshot back verbatim - setRenderMode(false)'s own half of
    // the round trip, replacing what used to be a bare
    // applyRenderTier(RenderTier::Plain) call. That call was never wrong for
    // the three fields it touched (Method/ShadowMapResolution/IsShadowEnabled
    // all happen to reset to Graphic3d_RenderingParams' own constructor
    // defaults), but it never touched ShadingModel, ToneMappingMethod,
    // IsGlobalIlluminationEnabled or AdaptiveScreenSampling at all - so a
    // session that ever reached the PathTracing tier would have left global
    // illumination and the PBR shading model switched on underneath ordinary
    // modeling forever after the first exit. gui_smoke's params round-trip
    // check exists because that class of bug produces no visible symptom
    // until the next render-mode entry reads the wrong "before" state.
    void restoreRenderParams();
    // Gives every currently displayed body a render-mode PBR material: a
    // light, matte, non-metallic look (see the .cpp for the measured
    // constants). Fix round 2's scoping ruling: this runs ONLY for the two
    // ray-traced tiers (PathTracing/RayTracing), where PBR shading is
    // genuinely handled - Shadows/Plain get clearRenderBodyMaterials()
    // instead, see applyRenderTier(). Undone by a plain UnsetMaterial() per
    // solid on exit or tier switch - correct BECAUSE no code path outside
    // this one ever calls AIS_InteractiveObject::SetMaterial() on a body
    // (displaySolid() only ever calls SetColor()), so "unset" really does
    // mean "back to whatever stood before render mode touched it," on the
    // same terms UnsetMaterial() documents for itself.
    void applyRenderBodyMaterials();
    // The Shadows/Plain half of the pair above - every displayed body back
    // to its ordinary Phong SetColor(), no PBR material at all. Called by
    // applyRenderTier() for the two rasterized, non-PBR tiers; also what
    // setRenderMode(false)'s own exit loop already did unconditionally
    // before fix round 2, now given a name so applyRenderTier() and the
    // exit path share the one implementation instead of two copies of
    // "loop mySolids, UnsetMaterial()".
    void clearRenderBodyMaterials();
    // While the PathTracing tier is active, repeatedly asks Qt to repaint at
    // rest. OCCT's own progressive accumulation - AdaptiveScreenSampling
    // folds more samples into the image on every Redraw() the camera and
    // scene stay still for - does not run itself; nothing schedules another
    // paintEvent once the gesture that triggered the first one (a click, an
    // orbit) lets go, so without this the studio shot would freeze on its
    // FIRST, noisiest frame instead of the "converges in ~1-2s at rest" look
    // the brief calls for.
    //
    // TWO RATES, ONE TIMER. kPathTracingConvergeMs of fast ticks
    // (kPathTracingBurstIntervalMs) is the responsive first polish after a
    // camera move; kPathTracingIdlePasses of wider ones then carry the image
    // the rest of the way, because a render that stops improving while the
    // user is still looking at it is exactly the grain the user gate on this
    // phase rejected. Both budgets are restarted, not topped up, on every
    // applyCameraState() - the one place every camera move already funnels
    // through - on the same reasoning a moved camera resets the path tracer's
    // own accumulation buffer.
    //
    // Still time-boxed rather than query-driven, and still for the same
    // reason: OCCT 8.0.1 exposes no "is this frame already converged" query at
    // this widget's disposal (checked; V3d_View offers Invalidate()/
    // IsInvalidated(), nothing PT-specific). The box is simply four times
    // deeper in samples than it was, which is twice as clean.
    void startPathTracingConvergence();
    void stopPathTracingConvergence();
    // The idle rate, derived from the tier probe's own measured frame cost -
    // see the definition. Never faster than the burst rate.
    int pathTracingIdleIntervalMs() const;
    // Every directional light this viewer owns, told to cast shadows or not.
    // One place, because both the tier-2 probe and applyRenderTier() need it.
    void setLightsCastShadows(bool cast);
    // Two real Dump()s - shadows off, then on - compared pixel by pixel.
    // CLAUDE.md's zoom-persistence lesson: Graphic3d_CLight::SetCastShadows()
    // succeeding is not proof a shadow actually reached the screen, so this
    // is the ONLY thing that accepts tier 2. False on any failure to render
    // or compare, which is what sends the probe on to plain rasterization.
    bool probeShadowPixelsDiffer();
    // The flat viewport colour outside render mode, the studio gradient
    // while it is on - one function, called from setRenderMode() on both
    // edges and from applyTheme(), which is what "re-derived on
    // themeChanged while active" (the brief's own words) actually is: a
    // theme edit repaints through applyTheme() regardless of render mode,
    // and this is the one place that reads myRenderModeActive to pick which
    // background that repaint means.
    void applyBackgroundForMode();
    // The flat studio colour render mode paints behind AND beneath the
    // furniture - one derivation serving the background and the floor, so
    // the two cannot drift apart and the floor's edge stays invisible.
    // Blended from the viewport token toward a warm white, so an Appearance
    // edit still moves it while the resting look stays the light neutral a
    // studio shot reads against. Named ...Impl() only because the public
    // renderBackdropColour() forward above already claims the plain name;
    // this is still the ONE implementation every internal caller (the
    // background clear colour, the floor material) and the public forward
    // both read.
    QColor renderBackdropColourImpl() const;
    // The shadow-catcher: a large matte plane a hair under the bodies'
    // lowest point, painted the backdrop colour, displayed with selection
    // mode -1 (the previews' own never-pickable path) for exactly as long
    // as render mode is on. This is the piece the first cut of render mode
    // was missing: a shadow needs a surface to land on, and outside render
    // mode this app deliberately has none. No-ops on an empty document -
    // a floor with nothing standing on it is just a wrong-coloured band.
    // Builds the geometry and then defers to applyRenderFloorMaterialForTier()
    // for whatever material the CURRENT myRenderTier calls for - see that
    // function's own comment for why that is always the right material even
    // before the first tier probe has run.
    void showRenderFloor();
    void hideRenderFloor();
    // Fix round 2's per-tier scoping, applied to the floor: `pbrTier` true
    // (PathTracing/RayTracing) gets the pure-Emission PBR material fix round
    // 1 landed; false (Shadows/Plain) gets the ORIGINAL, Milestone-3-
    // calibrated Phong material verbatim (ambient 0, diffuse 118-grey,
    // specular 0, emissive 87.5% of the backdrop) - the controller's
    // explicit ruling that the Shadows tier's shadow IS the tier, and a
    // flat pure-Emission floor traded it away for nothing this tier can
    // afford to lose. Called from showRenderFloor() (using whatever
    // myRenderTier already is - Plain's own zero-cost default before the
    // first probe, which is correctly the Phong branch) and from
    // applyRenderTier() on every tier switch, so a probe trying PathTracing
    // then falling back to Shadows leaves the floor in the material that
    // tier actually needs, not whichever ran last. A no-op when there is no
    // floor on screen.
    void applyRenderFloorMaterialForTier(bool pbrTier);

    // The camera's live vertical FOV: kFovyDeg outside render mode,
    // myRenderFovyDeg while it is on - see setRenderFov()'s own comment.
    // The ONE place both states are decided, so exiting render mode needs
    // no separate restore step: the very next read simply stops seeing the
    // override.
    double effectiveFovyDeg() const
    {
        return myRenderModeActive ? myRenderFovyDeg : kFovyDeg;
    }
    // The studio key's direction at a given azimuth, holding its
    // Milestone-3 calibrated elevation fixed - see setRenderLightAngleDeg()'s
    // own comment for why azimuth is the only axis this task exposes.
    // Reproduces the original hardcoded gp_Dir(-0.45, 0.35, -0.82) exactly
    // at this class's own default azimuth.
    gp_Dir studioKeyDirectionForAzimuth(double azimuthDeg) const;
    // Applies myRenderLightAngleDeg/myRenderLightStrength onto every light
    // in myRenderSavedLights - called once from setRenderMode(true), right
    // after that vector is populated, and again by the two setters
    // whenever render mode is already active. A no-op with nothing to do
    // when myRenderSavedLights is empty (render mode is off).
    void applyRenderLightAngleAndStrength();
    // The same application for a tier that is not (yet) myRenderTier - the
    // tier probe and every forcing measurement probe switch tiers on a live
    // view, and the studio rig is per-tier since the user-feedback round
    // (see kPathTracingAmbientGain and its neighbours). applyRenderTier()
    // calls this; applyRenderLightAngleAndStrength() is the myRenderTier
    // spelling the live setters use.
    void applyRenderLightsForTier(RenderTier tier);
    // The view's clear colour for one tier - split out of
    // applyBackgroundForMode() because path tracing needs the backdrop
    // pre-scaled (kPathTracingBackdropGain), and because applyRenderTier()
    // has to be able to set it WITHOUT also rebuilding the floor, which is
    // what applyBackgroundForMode() does and what would recurse from there.
    void applyRenderBackgroundColourForTier(RenderTier tier);
    // Drives the on-screen path-tracing accumulation buffer through enough
    // passes to settle, bounded by kExportConvergenceCapMs. A no-op on every
    // other tier and outside render mode. See saveSnapshot() for why an
    // export has to do this at all - the offscreen 2x path renders exactly
    // one sample per pixel and no parameter reaches it, so "export what the
    // user is looking at" and "export at 2x" turned out to be different
    // pictures - and see this function's own body for why the criterion is a
    // pass count rather than two settling samples: reading the pixels resets
    // the buffer being read, which makes a sampling loop agree with itself
    // immediately and at the wrong level.
    void awaitPathTracingConvergence();
    // The redraw every live render-settings setter needs after mutating a
    // material or a light property - the textbook-correct sequence
    // (Redisplay/settle-redraws/BVH-invalidate/camera-poke/a genuine
    // Method round trip), kept as the implementation even though it was
    // measured NOT to move a ray-traced Dump's pixels on this session for
    // UNIFORM material/intensity edits - see this function's own .cpp
    // comment for the full finding, six mechanisms deep as of fix round 1
    // (which added and then reverted a Remove()+Display() structure-
    // recreation attempt, on a code reviewer's specific suggestion - also
    // measured ineffective, and not kept in the tree unused, the
    // AIS_Manipulator styling wall's own precedent for a tried-and-failed
    // approach).
    void redrawRenderModeLive();

    Handle(V3d_Viewer) myViewer;
    Handle(V3d_View) myView;
    Handle(AIS_InteractiveContext) myContext;
    // The window OCCT measures itself against - see the class comment. Sized in
    // DEVICE pixels through toDevicePixels(), never logical ones, and its
    // DevicePixelRatio() left at the 1.0 default so this file's one conversion
    // point stays the only place the ratio is applied. Null until the first
    // initializeGL().
    Handle(Aspect_NeutralWindow) myHostWindow;
    // The QOpenGLContext the view was last attached to, compared by IDENTITY
    // rather than by native window handle: a context rebuilt on the SAME
    // top-level window - the actual shape of a context loss - leaves that
    // handle unchanged and would sail straight through a handle comparison,
    // leaving the view rendering through a dangling HGLRC. A QPointer so the
    // comparison can never be against a freed object. Null before the first
    // attach and after releaseGlResources().
    QPointer<QOpenGLContext> myAttachedContext;
    // See sketchZLayer(). Graphic3d_ZLayerId_UNKNOWN until the viewer exists,
    // and if the viewer ever refuses the layer everything below simply
    // displays into the default layer as it did before.
    Graphic3d_ZLayerId mySketchLayer = Graphic3d_ZLayerId_UNKNOWN;
    Handle(AIS_Shape) myPreview;
    // The direct-modeling channel, kept strictly apart from myPreview above.
    // Milestone 5's cross-body bevel is the reason these are vectors rather
    // than one shape and one id: a gesture spanning N bodies previews N
    // shapes at once, and every single-body gizmo (pull, transform, a
    // same-body bevel) is simply the N == 1 case of the same storage -
    // there is no second, single-shape member to keep in step with this one.
    // Parallel arrays, index for index: myModelingPreviewSolids[i] is the
    // body myModelingPreviews[i] stands in for while it is up, or -1. Its
    // presentation is restored to the viewport's own display mode when the
    // preview clears - see setModelingPreviews().
    std::vector<Handle(AIS_Shape)> myModelingPreviews;
    std::vector<int> myModelingPreviewSolids;

    // The sketch point markers - see setSketchPointMarkers()'s comment for
    // why these are not the preview slot above. One object per placed
    // point (each is a single-point marker; see SketchPointMarker in the
    // .cpp), plus one more for the first point's ring and one for the live
    // cursor dot.
    std::vector<Handle(AIS_InteractiveObject)> myPlacedMarkers;
    Handle(AIS_InteractiveObject) myFirstPointMarker;
    Handle(AIS_InteractiveObject) myCursorMarker;

    CameraController myCamera;
    GridRenderer myGridRenderer;
    DimensionRenderer myDimension;
    PullArrowRenderer myPullArrow;
    // The same renderer class, a second instance - see PullArrow.h.
    PullArrowRenderer myBevelArrow;

    std::map<int, Handle(AIS_Shape)> mySolids;
    // The outline items - see displayOutline(). Keyed by the same document id
    // space bodies use, and deliberately a SEPARATE map: nothing that walks
    // mySolids (selection, the manipulator, the wireframe toggle, the
    // selection-mode activation) should ever find an outline in it.
    std::map<int, Handle(AIS_Shape)> myOutlines;

    bool myWireframe = false;

    SelectionMode mySelectionMode = SelectionMode::Solid;
    // The one piece of Auto state that is genuinely remembered rather than
    // derived, and it is a MESSAGE, not a mode: why the last Shift-click did
    // nothing. Everything else about Auto's kind lock comes out of
    // selectionKind(), which reads the live selection.
    QString myAutoRefusal;
    // The kind the selection held before the CURRENT click changed it -
    // gesture-local memory for the one event that needs it, the double-click.
    // See its record site in mouseReleaseEvent(); selectionKind() is still
    // the only authority on what is held now.
    PickKind myAutoKindBeforeClick = PickKind::None;
    // Set by an Auto double-click that took a body, cleared by the release
    // that trails it - the one release that must not re-pick. See both sites.
    bool myAutoBodyPickTaken = false;
    // OCCT's own starting selector tolerance, captured at context creation -
    // see applySelectionTolerance().
    int myDefaultPixelTolerance = kNoCustomTolerance;
    bool myInitialized = false;
    bool mySketchMode = false;
    gp_Pln mySketchPlane;
    // See setWorkPlaneLocked() - whether mySketchPlane is a locked face
    // rather than the ground plane. False by default, which is the ground
    // plane's own state.
    bool myWorkPlaneLocked = false;
    bool mySnapEnabled = true;
    double mySnapStep = 10.0;      // matches the drawn grid

    // The straight-continuation anchor - see setSketchStraightAnchor(). The
    // direction is no longer stored: the 8-direction dial is derived fresh
    // from this anchor and the live cursor on every hit test, inside
    // pointOnSketchPlane().
    bool myHasStraightAnchor = false;
    gp_Pnt myStraightPrev{0.0, 0.0, 0.0};
    // The outline's closing point - see setSketchCloseTarget().
    bool myHasCloseTarget = false;
    gp_Pnt myCloseTarget{0.0, 0.0, 0.0};

    gp_Pnt myLastHoverPoint{0.0, 0.0, 0.0};
    bool myHasLastHoverPoint = false;

    QPoint myLastPos;
    bool myOrbiting = false;
    bool myPanningDrag = false;

    // True only for the duration of applyCameraState()'s cameraChanged()
    // emission, so a slot that changes the scene can skip its own viewer
    // update and let that function's redraw carry it - see showPullArrow().
    bool myApplyingCamera = false;

    // The transform gizmo and the live drag on it. myGizmoDelta is the WHOLE
    // transform from the press, not an increment: AIS_Manipulator recomputes
    // it from the original pick on every move (Transform() sets the object's
    // local transformation to `delta * startTrsf`), so the last one it handed
    // back is the accumulated answer. myGizmoStartPosition is the manipulator's
    // own frame at the press, and it is the pivot snapTransform() decomposes
    // about - the rotation and the scale both leave it fixed.
    Handle(AIS_Manipulator) myManipulator;
    int myManipulatorSolid = -1;
    // The size AIS_Manipulator's own AdjustSize derived from the body's
    // bounding box at the moment of the attach, and the size actually installed
    // by the last updateManipulatorSize(). The first is the ceiling the clamp
    // never grows past; the second is the equal-guard, so an orbit that leaves
    // the zoom alone costs no presentation rebuilds at all. Both are 0 while
    // nothing is attached.
    double myManipulatorNaturalSize = 0.0;
    double myManipulatorAppliedSize = 0.0;
    // Where the gizmo stands, from the body's own bounding box at the attach.
    // The clamp needs its DEPTH, and AIS_Manipulator::Position() cannot answer
    // that during the attach itself - see attachManipulator().
    gp_Pnt myManipulatorCentre;
    bool myGizmoDragActive = false;
    gp_Trsf myGizmoDelta;
    gp_Ax2 myGizmoStartPosition;

    // The two axis drags, one per arrow. See AxisDrag above.
    AxisDrag myPullDrag;
    AxisDrag myBevelDrag;
    // The mirror-placement handle's own drag - the same AxisDrag shape, a
    // third time over. Its `value` is a DELTA from the press, exactly as the
    // other two are; myMirrorDragOffsetStart is what turns that delta back
    // into the plane's ABSOLUTE offset (see mirrorPlaneDragged()'s own
    // comment for why this one is not a fresh-each-gesture distance).
    AxisDrag myMirrorDrag;
    double myMirrorDragOffsetStart = 0.0;

    // While true, updateEdgeDimension() draws nothing - see
    // setEdgeDimensionSuppressed().
    bool myEdgeDimensionSuppressed = false;

    // The owner MoveTo() detected on the PREVIOUS hover move, so
    // mouseMoveEvent() can tell whether the hover target actually changed -
    // see the comment at that call site. AIS_InteractiveContext::MoveTo(...,
    // Standard_True) asks OCCT for its own immediate redraw, which is a
    // hardware-dependent shortcut (see the SetImmediateModeDrawToFront
    // finding on this header): on hardware where the separate immediate-mode
    // framebuffer fails to allocate, OCCT falls back to drawing straight into
    // the bound default framebuffer and the highlight reaches the screen
    // regardless of what this app does; where that allocation succeeds, a
    // genuine Qt-driven repaint is the only thing that puts it there.
    // updateEdgeDimension() used to be the sole source of one on this path,
    // and it bails out before ever comparing the hover target at all the
    // moment the dimension is suppressed (an edge selected, its bevel arrow
    // up) or the mode is not Edge - so a hover that changed which owner is
    // detected could go unscheduled entirely, in every selection mode, any
    // time the annotation itself had nothing to say. Compared by handle
    // identity, not by shape - it is the OWNER MoveTo() tracks, and comparing
    // it directly means a genuine "the scene's dynamic highlight changed"
    // signal in either direction (something now detected, something no
    // longer detected, or a different something), never "on every idle
    // mouse move over the same one" - the redraw storm the migration's fix
    // round already removed once.
    Handle(SelectMgr_EntityOwner) myLastHoverOwner;

    // The edge the last pick actually added to the selection. OCCT's
    // InitSelected order is the context's, not the user's, so "the edge you
    // picked last" cannot be read back out of the selection - it has to be
    // remembered as it happens. Validated against the live selection on every
    // read (see lastSelectedEdge()), never trusted across a rebuild.
    TopoDS_Edge myLastPickedEdge;

    class QVariantAnimation* myCameraAnimation = nullptr;
    bool myAnimationsEnabled = true;

    // The symmetry plane indicator - see setSymmetryIndicator().
    bool mySymmetryIndicatorOn = false;
    gp_Pln mySymmetryIndicatorPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)};
    Handle(AIS_InteractiveObject) mySymmetryIndicator;
    // The world half-span it was last built at - 0 forces the next
    // updateSymmetryIndicator() to rebuild regardless of the equal-guard.
    double mySymmetryIndicatorBuiltHalfSpan = 0.0;

    // The mirror-placement gesture's own live state - see
    // beginMirrorPlacement(). `centre` is the FIXED combined centre computed
    // once at begin(); `axis` is 0/1/2 for X/Y/Z; `offset` is the plane's
    // current signed distance from `centre` along the current normal.
    // `halfExtent` is the combined box's half-size on each world axis,
    // captured once beside `centre` - what mirrorPlacementTangentOffset()
    // reads to place the plane clear of the selection on spawn and on every
    // X/Y/Z flip.
    struct MirrorPlacement {
        bool active = false;
        std::vector<int> ids;
        gp_Pnt centre{0.0, 0.0, 0.0};
        gp_XYZ halfExtent{0.0, 0.0, 0.0};
        int axis = 0;
        double offset = 0.0;
    };
    MirrorPlacement myMirrorPlacement;
    // What updateMirrorPlacementIndicator() last actually built - the
    // equal-guard updateSymmetryIndicator() and updateManipulatorSize() both
    // carry, extended past their own screen-size check to the plane's own
    // location and normal, which (unlike the passive symmetry indicator) can
    // change on every drag step and every orientation flip without the
    // screen size changing at all. A camera orbit with no drag in progress is
    // therefore free, exactly as it is for those two.
    double myMirrorPlacementBuiltHalfSpan = 0.0;
    gp_Pnt myMirrorPlacementBuiltOrigin{0.0, 0.0, 0.0};
    gp_Dir myMirrorPlacementBuiltNormal{1.0, 0.0, 0.0};
    // The plane rectangle - SymmetryPlaneObject, restyled - and the handle
    // marker, both rebuilt by updateMirrorPlacementIndicator(). Separate
    // objects from mySymmetryIndicator: the two can never be up at once (this
    // one only exists mid-gesture, and confirming it is what turns the OTHER
    // one on) but sharing a handle would still make one's teardown the
    // other's, which is exactly the kind of channel-sharing bug CLAUDE.md's
    // setModelingPreview/setPreview split exists to avoid repeating.
    Handle(AIS_InteractiveObject) myMirrorPlacementPlaneObject;
    Handle(AIS_InteractiveObject) myMirrorPlacementHandleObject;

    // See the constructor's own comment - the compare pane's flag.
    bool myViewerOnly = false;

    // Render mode (Milestone 3, item 5) - see setRenderMode(). Session-only
    // in the sense that matters: nothing here is ever read from or written
    // to QSettings, so a fresh OcctViewWidget always starts with all three
    // false/Plain regardless of what a previous session left.
    bool myRenderModeActive = false;
    bool myRenderTierProbed = false;
    RenderTier myRenderTier = RenderTier::Plain;
    // The probe's own working, kept beside its answer - see TierProbeTimings.
    TierProbeTimings myTierProbeTimings;
    // See showRenderFloor(). Null whenever render mode is off.
    Handle(AIS_Shape) myRenderFloor;
    // Every directional light's direction and intensity as they stood at
    // entry, restored on exit - render mode swaps in one angled studio key
    // direction and doubles the strength (the default rig is tuned for flat
    // modeling legibility and reads dim as a photograph), and the modeling
    // look outside render mode must come back exactly as it was.
    struct SavedLight {
        Handle(Graphic3d_CLight) light;
        gp_Dir direction;
        Standard_ShortReal intensity;
        // OCCT's default directional light is a HEADLIGHT - its direction is
        // read in view space and follows the camera, which is what keeps the
        // modeling view legible from every angle. A studio key has to stand
        // still in the world while the camera orbits the shot, so render
        // mode turns the flag off and this remembers it was on.
        bool headlight;
        // The cone angle, which render mode opens for the path-traced tier
        // (see kPathTracingKeySmoothAngleRad) and which is no more part of
        // the modeling look than the direction is.
        Standard_ShortReal smoothness;
    };
    std::vector<SavedLight> myRenderSavedLights;

    // The ambient half of the same round trip. It was left out until the
    // user-feedback round because nothing here had ever moved it - the fill
    // that lifts an unlit face off black is the one thing the rasterized
    // tiers were missing, and the path-traced tier needs it turned DOWN
    // rather than up, so both directions belong to this class now and both
    // are put back on the way out.
    struct SavedAmbient {
        Handle(Graphic3d_CLight) light;
        Standard_ShortReal intensity;
    };
    std::vector<SavedAmbient> myRenderSavedAmbients;

    // saveRenderParams()/restoreRenderParams()'s own snapshot - every
    // Graphic3d_RenderingParams field applyRenderTier() or this task's PBR
    // application can write, captured from the live view at every
    // setRenderMode(true) and written back verbatim at setRenderMode(false).
    // Deliberately a field-by-field copy rather than a whole
    // Graphic3d_RenderingParams (which the type is copyable enough to allow)
    // - naming exactly what is touched is what lets gui_smoke's round-trip
    // check assert on the same fields this class actually promises to
    // restore, rather than a struct-wide memcmp that would also pass by
    // accident if an unrelated field this class never reads happened to
    // match.
    struct RenderParamsSnapshot {
        Graphic3d_RenderingMode method = Graphic3d_RM_RASTERIZATION;
        Graphic3d_TypeOfShadingModel shadingModel = Graphic3d_TypeOfShadingModel_Phong;
        Graphic3d_ToneMappingMethod toneMappingMethod = Graphic3d_ToneMappingMethod_Disabled;
        bool isGlobalIlluminationEnabled = false;
        bool adaptiveScreenSampling = false;
        bool isAntialiasingEnabled = false;
        bool isShadowEnabled = true;
        int shadowMapResolution = 1024;
        // -1 (unlimited) under PathTracing, the constructor default (256)
        // everywhere else - see applyRenderTier()'s own comment for why this
        // one is load-bearing rather than cosmetic: the default caps how
        // many screen TILES a single Redraw() renders, and a viewport with
        // more tiles than that budget is left with real, unrendered black
        // pixels - not merely noisy ones - until enough redraws have
        // accumulated to cover every tile at least once.
        int nbRayTracingTiles = 256;
    };
    RenderParamsSnapshot myRenderSavedParams;

    // The path-tracing progressive-refine loop - see
    // startPathTracingConvergence()'s own comment. Null until the first
    // PathTracing-tier activation this session; a QTimer child of `this`,
    // cleaned up by Qt's own parent/child ownership.
    class QTimer* myPathTracingRefineTimer = nullptr;
    // The burst budget (fast rate) and the idle one (wide rate) that follows
    // it. One timer, two budgets, so there is one tick and one place a revert
    // can be caught.
    int myPathTracingRefineTicksLeft = 0;
    int myPathTracingIdleTicksLeft = 0;

    // Frames painted since the last Invalidate - see accumulationDepth().
    int myAccumulationDepth = 0;

    // Total frames painted, ever - never reset. See totalPaintCount().
    int myTotalPaintCount = 0;

    // The paint-timing ring - see PaintSample. A plain vector used as a ring so
    // a long gesture cannot grow it without bound; myPaintSampleNext is the
    // next slot to overwrite once it has filled.
    std::vector<PaintSample> myPaintSamples;
    int myPaintSampleNext = 0;

    // --- Render settings (Task 7.2) -------------------------------------
    // Plain session state - see the six accessors' own comments above for
    // what each drives and MainWindow for how it persists. Defaults
    // reproduce exactly what render mode did before this task existed:
    // roughness 0.55/metallic 0.0 (applyRenderBodyMaterials()'s old
    // hardcoded pair), light strength 2.0 ("doubled, by measurement"), no
    // background override, and kFovyDeg. myRenderLightAngleDeg's default is
    // computed in the constructor (std::atan2 is not a constexpr-friendly
    // call) to reproduce the old hardcoded gp_Dir(-0.45, 0.35, -0.82)'s own
    // azimuth exactly rather than an approximated literal.
    double myRenderRoughness = 0.55;
    double myRenderMetallic = 0.0;
    double myRenderLightAngleDeg = 142.0;
    double myRenderLightStrength = 2.0;
    QColor myRenderBackgroundOverride;   // invalid = no override
    double myRenderFovyDeg = kFovyDeg;
};
