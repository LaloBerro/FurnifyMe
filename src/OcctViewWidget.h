#pragma once
// OCCT headers first: Handle() is a macro and collides with some Windows headers
// that Qt drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_ManipulatorMode.hxx>
#include <AIS_Shape.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "CameraController.h"
#include "DimensionRenderer.h"
#include "GridRenderer.h"
#include "PullArrow.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <map>
#include <vector>

// The Qt <-> OCCT bridge. Hosts a V3d_View on this widget's native window and
// forwards Qt input to the OCCT camera and selector.
class OcctViewWidget : public QWidget {
    Q_OBJECT

public:
    enum class SelectionMode { Solid, Face, Edge };

    explicit OcctViewWidget(QWidget* parent = nullptr);
    ~OcctViewWidget() override;

    // Qt must not paint here or it fights OpenGL for the surface.
    QPaintEngine* paintEngine() const override { return nullptr; }

    void displaySolid(int id, const TopoDS_Shape& shape);
    void removeSolid(int id);
    void clearSolids();

    // Presentation state, not document state: it is deliberately not captured by
    // undo, because hiding something is not an edit.
    void setSolidVisible(int id, bool visible);
    bool isSolidVisible(int id) const;

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
    void clearModelingPreview();
    bool hasModelingPreview() const;
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

    // The plane clicks are unprojected onto AND the plane the grid lies on -
    // one value, not two, because a grid that disagreed with where the next
    // point will land would be worse than no grid. The ground plane until a
    // face is locked. Setting it rebuilds the grid immediately, whether or
    // not a sketch is in progress: locking a face has to be visible before
    // the user starts drawing on it.
    void setWorkPlane(const gp_Pln& plane);
    const gp_Pln& workPlane() const { return mySketchPlane; }

    // The straight-continuation anchor: the last placed point and the
    // direction of the segment that led into it. While Shift is held, a
    // reported sketch point - the hover that drives the cursor marker, the
    // status readout and the live dimension, and the click that places the
    // next point - is projected onto that line, so all four agree by
    // construction rather than by four call sites each remembering to snap.
    //
    // MainWindow owns the point list and so owns this: it sets the anchor from
    // SketchController::lastSegmentDirection() whenever the list changes, and
    // clears it when there is nothing to continue. Fewer than two points, or
    // two coincident ones, means no anchor and Shift does nothing at all.
    void setSketchStraightAnchor(const gp_Pnt& prev, const gp_Dir& dir);
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
    // back to when the cursor leaves an edge the user has selected.
    TopoDS_Edge selectedEdge() const;

    // Redraws whatever dimension is on screen without changing which span it
    // measures - for a display-unit switch, which changes the label's text
    // under an annotation nothing else would touch until the next mouse move.
    void refreshDimension() { myDimension.refresh(); }

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

signals:
    void sketchPointPicked(const gp_Pnt& point);
    // Fired on every camera change so overlays (the axis gizmo) can repaint.
    void cameraChanged();
    // Live cursor position on the sketch plane, already snapped - drives the
    // rubber band and the coordinate readout.
    void sketchCursorMoved(const gp_Pnt& point);
    void selectionChanged();
    // A face double-clicked in face-selection mode. MainWindow decides what
    // that means (it locks it); this widget knows nothing about locking.
    void faceDoubleClicked(const TopoDS_Face& face);

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
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

    void initializeViewer();
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
    // Reads the accumulated transform, puts the PRESENTATION back to where the
    // document says it should be, snaps, and emits gizmoReleased(). The
    // presentation reset is unconditional and happens here rather than in the
    // consumer: a bake can be refused, and a viewport still showing the
    // dragged pose above a document that never changed is the one outcome
    // this gesture must not be able to produce.
    void endGizmoDrag();
    void applySelectionMode(const Handle(AIS_Shape)& shape);
    void applyCameraState();
    void stopCameraAnimation();
    // Shows or clears the edge dimension: the edge the last MoveTo detected
    // if there is one, otherwise the single selected edge. Clears outside
    // edge-selection mode. Every route that can change either of those two
    // inputs - a hover, a click that selects, a cleared selection, a body
    // that went away - calls this, because an annotation that only some of
    // them refresh is an annotation that is sometimes a lie.
    void updateEdgeDimension();

    Handle(V3d_Viewer) myViewer;
    Handle(V3d_View) myView;
    Handle(AIS_InteractiveContext) myContext;
    // See sketchZLayer(). Graphic3d_ZLayerId_UNKNOWN until the viewer exists,
    // and if the viewer ever refuses the layer everything below simply
    // displays into the default layer as it did before.
    Graphic3d_ZLayerId mySketchLayer = Graphic3d_ZLayerId_UNKNOWN;
    Handle(AIS_Shape) myPreview;
    // The direct-modeling channel, kept strictly apart from myPreview above.
    Handle(AIS_Shape) myModelingPreview;
    // The body myModelingPreview stands in for while it is up, or -1. Its
    // presentation is restored to the viewport's own display mode when the
    // preview clears - see setModelingPreview().
    int myModelingPreviewSolid = -1;

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

    bool myWireframe = false;

    SelectionMode mySelectionMode = SelectionMode::Solid;
    bool myInitialized = false;
    bool mySketchMode = false;
    gp_Pln mySketchPlane;
    bool mySnapEnabled = true;
    double mySnapStep = 10.0;      // matches the drawn grid

    // The straight-continuation anchor - see setSketchStraightAnchor().
    bool myHasStraightAnchor = false;
    gp_Pnt myStraightPrev{0.0, 0.0, 0.0};
    gp_Dir myStraightDir{1.0, 0.0, 0.0};
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
    bool myGizmoDragActive = false;
    gp_Trsf myGizmoDelta;
    gp_Ax2 myGizmoStartPosition;

    // The two axis drags, one per arrow. See AxisDrag above.
    AxisDrag myPullDrag;
    AxisDrag myBevelDrag;

    // While true, updateEdgeDimension() draws nothing - see
    // setEdgeDimensionSuppressed().
    bool myEdgeDimensionSuppressed = false;

    class QVariantAnimation* myCameraAnimation = nullptr;
    bool myAnimationsEnabled = true;
};
