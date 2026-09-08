#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>

#include <QColor>
#include <QStringList>
#include <QWidget>

#include <vector>

class MainWindow;
class OcctViewWidget;
class QHideEvent;
class QShowEvent;
class TopoDS_Shape;
class gp_Trsf;

// The body-transform gizmo we draw ourselves - Phase 1 of the split design
// (docs/superpowers/specs/2026-09-06-custom-gizmo-design.md), which is Move
// alone. Rotate and Scale join in Phase 2 and OCCT's AIS_Manipulator dies with
// them.
//
// Why ours at all: AIS_Manipulator has a real, measured styling wall (no
// setter reaches a per-axis colour at any access level, and a subclass that
// reached the proportions measured the arm getting THICKER as the radius
// shrank - both findings are recorded in CLAUDE.md). The only route to a gizmo
// wearing the axis card's own language is drawing one.
//
// THE SPLIT, and it is PullArrow's, deliberately:
//
//   GizmoRenderer   draws IN THE 3D SCENE, because a handle painted over the
//                   viewport would slide off the body the moment the camera
//                   turned. Sized in SCREEN PIXELS through
//                   OcctViewWidget::worldPerPixel() and rebuilt on
//                   cameraChanged, so it reads the same at any zoom - the
//                   solved-problem path, never OCCT's own zoom-persistence
//                   flags (see attachManipulator()'s own account of what those
//                   cost).
//   MoveTool        is the Qt half - the value chip beside the arm being
//                   dragged, and the application-wide Escape claim that
//                   cancels a live drag.
//
// GizmoRenderer is a BASE, not a copy target. Everything in it is the part
// Phase 2's rings and centre handle need unchanged - the context, the object
// list, the pose cache that stops a rebuild on every idle camera tick, the
// display discipline (mode -1, never pickable, the gizmo's own layer) and the
// theme re-application. What a subclass supplies is geometry and nothing
// else: buildStrokes() is handed the pose and adds shaded solids through
// addSolid(). A RotateGizmoRenderer is then three tori and a
// ScaleGizmoRenderer three cube tips, with no opportunity to forget one of
// the disciplines above.
// Everything a gizmo needs to know about where it stands and how the camera is
// looking at it. A struct rather than six arguments because Phase 2's two
// renderers take exactly the same set and a positional argument list of this
// length is how one of them ends up handed `up` where it wanted `right`.
struct GizmoPose {
    gp_Pnt pivot;
    // The camera's own frame. `right` and `up` span the VIEW PLANE, which is
    // where the drawing is laid out - see MoveGizmoRenderer's header.
    gp_Dir right{1.0, 0.0, 0.0};
    gp_Dir up{0.0, 0.0, 1.0};
    gp_Dir view{0.0, -1.0, 0.0};   // eye -> target
    // World units per LOGICAL screen pixel, and how many DEVICE pixels one of
    // those is worth. Both are needed and they are not interchangeable:
    // worldPerPixel() divides by the widget's logical height, while the two
    // things OCCT sizes for us rather than from our geometry - a line's width
    // and a label's height - are counted in DEVICE pixels. Without the ratio a
    // 150% display draws the whole drawing half again as big and its strokes
    // and letters exactly as before, which is a different drawing.
    double worldPerPixel = 1.0;
    double pixelRatio = 1.0;
};

class GizmoRenderer {
public:
    virtual ~GizmoRenderer() = default;

    void attach(const Handle(AIS_InteractiveContext)& context);
    // The Z-layer everything this gizmo draws goes into. It wants one of its
    // OWN, cleared of depth and shared with nothing: a gizmo stands at its
    // body's bounding-box centre, so the body's surface is nearer than every
    // stroke of it, and a handle a user can see through the thing it is
    // attached to is not a handle. Graphic3d_ZLayerId_Topmost clears depth too
    // but is SHARED - OCCT's dynamic highlight lives there, so does
    // AIS_Manipulator - and anything that joins arrives after that layer's one
    // depth clear at its own true depth, cropping whatever is drawn behind it.
    // See OcctViewWidget::initializeViewer(). Unset falls back to Topmost.
    void setZLayer(Graphic3d_ZLayerId layer) { myLayer = layer; }
    // Drops the context and everything built against it, WITHOUT touching the
    // viewer - PullArrowRenderer::detach()'s own contract, same one caller
    // (OcctViewWidget::releaseGlResources()).
    void detach();

    // Draws the gizmo at `pose.pivot`, in the camera frame `pose` carries.
    // Replaces whatever was drawn before.
    //
    // Returns TRUE only when what is on screen actually changed - the caller
    // owns the frame and asks for one only on a true return, which is the
    // measured saving PullArrowRenderer's own header records (a rebuild
    // riding along with applyCameraState()'s redraw rather than forcing a
    // second vsync).
    bool show(const GizmoPose& pose);
    // TRUE when something was actually removed - show()'s own contract.
    bool clear();
    bool isShowing() const { return !myObjects.empty(); }

    // Rebuilds whatever is on screen from the pose it was built with. Exists
    // for exactly one caller, a Theme edit: the axis hues are baked into the
    // AIS objects at build time and show() early-outs on an unchanged pose, so
    // without this a live gizmo would wear the old colours until the camera
    // happened to move far enough to defeat the cache. A no-op when nothing is
    // showing, so it cannot make a gizmo appear.
    void reapplyTheme();

    // Which handle the cursor is over (0/1/2), or -1. Driven by
    // OcctViewWidget's own screen-space hit test - these objects are never
    // pickable, so OCCT's hover pipeline cannot answer this - and read by
    // buildStrokes(), which draws the hovered handle brighter. Returns TRUE
    // when the change actually redrew something, so the caller knows to ask
    // for a frame.
    bool setHoveredAxis(int axis);
    int hoveredAxis() const { return myHoveredAxis; }

    const gp_Pnt& pivot() const { return myPose.pivot; }

protected:
    // THE subclass hook. Called with the pose already set; adds whatever the
    // tool draws through addSolid().
    virtual void buildStrokes() = 0;

    // Whether the drawn geometry depends on where the camera LOOKS, or only on
    // where it stands. A gizmo laid out along the WORLD axes (the 3D one) is
    // the latter: return false and show()'s rebuild key drops the camera-frame
    // terms, so an orbit at constant distance rebuilds nothing at all - the
    // zoom (worldPerPixel) and the pivot are the only things that can move it.
    virtual bool viewDependent() const { return true; }

    // One shaded 3D solid, drawn UNLIT in exactly `colour` - Unity's own gizmo
    // look: a flat solid-colour arrow whose 3D-ness shows through perspective
    // and self-occlusion, not through lighting. Unlit also keeps the token
    // colour EXACT in a Dump, which a lit mesh could not promise. The shape
    // must be meshed (BRepMesh_IncrementalMesh) before it is handed in; AIS
    // draws nothing for an untessellated face. Displayed in AIS_Shaded at
    // selection mode -1, never pickable, on drawLayer() - the same discipline
    // every gizmo object here has always kept.
    void addSolid(const TopoDS_Shape& shape, const QColor& colour);

    // Where both of those put what they draw: setZLayer()'s value, or Topmost
    // if the viewer never gave us a layer of our own.
    Graphic3d_ZLayerId drawLayer() const;

    const GizmoPose& pose() const { return myPose; }
    double worldPerPixel() const { return myPose.worldPerPixel; }
    double pixelRatio() const { return myPose.pixelRatio; }
    const gp_Dir& viewDirection() const { return myPose.view; }

private:
    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    // What the gizmo currently on screen was built from, so show() can tell a
    // call that changes nothing from one that does.
    GizmoPose myPose;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    // Defeats show()'s pose cache for one call - the appearance changed, not
    // the geometry, and the cache key knows nothing about appearance.
    bool myForceRebuild = false;
    int myHoveredAxis = -1;
};

// The MOVE tool's presentation: a TRUE 3D GIZMO, Unity's and Blender's own
// shape (user ruling, 2026-09-08, replacing the axis-card copy outright).
//
// Three shaded arrows along the WORLD axes - a thin cylinder shaft, a real
// cone of revolution at its end - and a sphere at the pivot, each an unlit
// solid in its axis token so the colours stay exact. Being genuine 3D
// geometry, it foreshortens naturally with the camera instead of always
// facing it, and a sphere is a perfect circle from every angle - the flat
// ring's cropped-arc failure cannot exist here by construction. It draws on
// the gizmo's own depth-cleared immediate Z-layer, so it stands on top of
// every body while its own parts still occlude each other correctly.
//
// Sized in SCREEN pixels (kArmPixels x Theme::gizmoScale(), converted through
// worldPerPixel()) and laid out along fixed world directions, so the only
// things that can move it are the pivot and the zoom - viewDependent() is
// false, and an orbit at constant distance rebuilds nothing.
//
// The DRAG is untouched by any of this: an arm drags along its TRUE world
// axis through armAxis(), and the hit band follows the drawn segment from
// grab-start to cone tip. Only the three positive handles exist (the negative
// stub-and-ball handles were removed by the same user ruling); the [axis][1]
// slots stay in the cache arrays so the shared accessors keep their shape,
// permanently marked not-drawn.
class MoveGizmoRenderer : public GizmoRenderer {
public:
    // World +X / +Y / +Z. The one place the axis-index -> gp_Dir mapping
    // lives, so the drawing, the hit test and the drag cannot each carry a
    // slightly different idea of what "Y" means.
    static gp_Dir armDirection(int axis);

    // The line a drag along `axis` is measured against: the world axis through
    // the pivot. FIXED for the life of a gesture, exactly as
    // PullArrowRenderer::axis() is - a line measured from a point that itself
    // moved as the drag proceeded would make the drag measure its own effect.
    // Direction-agnostic on purpose: the negative ball's drag is the positive
    // arm's drag with the other sign, and one line is what makes that true.
    gp_Lin armAxis(int axis) const;

    // A handle's outer point AS DRAWN - the cone's tip, in world coordinates.
    // Where the value chip goes, and where the hit test's span ends.
    gp_Pnt handleTip(int axis, bool positive) const;
    // Where a handle's GRABBABLE span starts, as a fraction of the drawn arm.
    // The inner third is excluded on purpose: all six handles meet at the hub,
    // so near it the nearest-handle-wins rule would be decided by sub-pixel
    // noise and the user would get an axis at random.
    static constexpr double kGrabStartFraction = 0.3;
    gp_Pnt handleGrabStart(int axis, bool positive) const;
    // TRUE for the three positive handles, FALSE always for the negative
    // slots, which no longer draw anything - and a handle that is not drawn
    // must not grab (moveGizmoAxisAt() reads this as its gate).
    bool handleDrawn(int axis, bool positive) const;

    // The positive spellings, kept because most callers only ever mean +axis.
    gp_Pnt armTip(int axis) const { return handleTip(axis, true); }
    gp_Pnt armGrabStart(int axis) const { return handleGrabStart(axis, true); }

protected:
    void buildStrokes() override;
    bool viewDependent() const override { return false; }

private:
    // Where the last build actually PUT each handle, indexed
    // [axis][positive ? 0 : 1]. Cached so the hit test asks about the drawing
    // that is on screen rather than about a pose that has moved on since.
    gp_Pnt myTip[3][2];
    gp_Pnt myGrabStart[3][2];
    bool myDrawn[3][2] = {{false, false}, {false, false}, {false, false}};
};

// The ROTATE tool's presentation (custom gizmo, Phase 2): three unlit tori,
// one per axis, each lying in the plane its axis is normal to - Blender's own
// rotate gizmo. Real 3D rings, so they occlude each other correctly and can
// never crop: what the eye sees is the torus's actual silhouette. Grabbing a
// ring rotates about its axis; the hit test samples the drawn ring in screen
// space (OcctViewWidget::rotateGizmoAxisAt()), the same never-pickable
// discipline every gizmo object here keeps.
class RotateGizmoRenderer : public GizmoRenderer {
public:
    // A point on the drawn ring of `axis` at `angleRad`, in world
    // coordinates - the hit test and the value chip both walk the ring
    // through this, so neither can disagree with the drawing about where it
    // is.
    gp_Pnt ringPoint(int axis, double angleRad) const;
    double ringRadius() const { return myRadius; }

protected:
    void buildStrokes() override;
    bool viewDependent() const override { return false; }

private:
    double myRadius = 0.0;
};

// The SCALE tool's presentation (custom gizmo, Phase 2): the Move gizmo's
// three arms wearing CUBE tips instead of cones - Unity's own scale language -
// plus the neutral pivot sphere. The kernel can only express UNIFORM scale
// (gp_Trsf has no per-axis form; GTransform would convert faces to NURBS), so
// dragging ANY cube scales the whole body uniformly; three handles rather
// than one centre cube because a handle out on an arm gives the drag a line
// to be measured along, which a centre grab cannot.
class ScaleGizmoRenderer : public GizmoRenderer {
public:
    // The same handle accessors MoveGizmoRenderer exposes, so
    // OcctViewWidget's one screen-space hit test serves both gizmos.
    gp_Pnt handleTip(int axis, bool positive) const;
    gp_Pnt handleGrabStart(int axis, bool positive) const;
    bool handleDrawn(int axis, bool positive) const;

protected:
    void buildStrokes() override;
    bool viewDependent() const override { return false; }

private:
    gp_Pnt myTip[3][2];
    gp_Pnt myGrabStart[3][2];
    bool myDrawn[3][2] = {{false, false}, {false, false}, {false, false}};
};

// The Qt half of ALL THREE body-tool gestures since Phase 2 - the value chip,
// the live ghost preview and the Escape claim, for whichever of Move, Rotate
// and Scale is active (MainWindow::bodyTool() decides, and showGizmo() raises
// that tool's renderer). The class keeps its Phase 1 name because the suite
// and MainWindow address it by it; it is the ONE transform chip, not a
// Move-only one.
//
// Its contract is PullArrow's, and the long-form reasons for every rule are
// recorded there and on ExtrudePreview. The two real differences:
//
//   - THERE IS NOTHING TO TYPE. A move is two numbers (which axis, how far)
//     and the axis can only be chosen by grabbing an arm, so a field that
//     could only carry half the gesture would be a worse control than the
//     drag itself. That means no QLineEdit, no sibling, and no Enter claim -
//     only Escape, and only while a drag is actually live.
//   - THE CHIP IS THE DRAG'S OWN. It appears when a drag produces a distance
//     and goes when the drag ends, rather than standing over the viewport for
//     as long as a body is selected: a permanent card reading "0 mm" beside
//     every selected body says nothing and covers something.
//
// Visibility is DERIVED in one place (updateVisibility(), off
// OcctViewWidget::moveDragActive()), never toggled from the event that
// happened to raise it - the rule this tree keeps for every surface over the
// viewport.
class MoveTool : public QWidget {
    Q_OBJECT

public:
    MoveTool(MainWindow* window, OcctViewWidget* view);
    ~MoveTool() override;

    // THE predicate, in one place, used to show the gizmo and to retire it.
    // Connected to MainWindow::appStateChanged(), never driven from an event.
    // Reads MainWindow::moveToolBodyId(), which updateActions() and the status
    // label read too, so the arms, the enabled state and the teaching text can
    // never disagree about whether a move is possible.
    void refresh();

    // Re-places the chip against the dragged arm's projected tip, and rebuilds
    // the gizmo itself at the new scale. Driven by
    // OcctViewWidget::cameraChanged.
    void reposition();
    // Re-places AND re-raises, from ViewportOverlay::laidOut() - the one
    // moment every anchored cluster is at its final rectangle. See
    // ExtrudePreview::replace() for why a raw resize event is the wrong hook.
    void replace();

    // Every string this chip paints, for gui_smoke's banned-word sweep.
    QStringList paintedTexts() const;

    // The body the gizmo is standing on, or 0.
    int bodyId() const { return myBodyId; }
    bool hasPreview() const { return myHasPreview; }
    // The last distance that actually previewed, in millimetres, along
    // axis() - 0 when no drag is live.
    double distance() const { return myDistance; }
    int axis() const { return myAxis; }

    // Drops the live drag and its preview, leaving the gizmo and the selection
    // alone - the body is still selected, so the predicate still holds and the
    // user can simply drag again. Escape's own route.
    void cancel();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin(int bodyId);
    void end();
    // Puts the gizmo where the body is RIGHT NOW. The pivot is re-derived from
    // the document on every call rather than remembered: a commit replaces the
    // body, and a handle left at the old bounding box would stand beside the
    // body instead of on it.
    void showGizmo();
    void updateVisibility();
    void updatePreview();
    void commit();
    void applySize();
    // The one place each painted string is spelled out - paintEvent() draws
    // through these and paintedTexts() reports them, so the sweep can never be
    // guarding a different copy than the one on screen.
    QString labelText() const;
    QString valueText() const;
    QString hintText() const;
    void onDragged(int axis, double millimetres);
    void onRotateDragged(int axis, double degrees);
    void onScaleDragged(int axis, double factor);
    void onReleased(bool dragged);
    // The delta the live drag has produced, as one gp_Trsf about the body's
    // own pivot - the ONE derivation updatePreview() and commit() both read,
    // switched on the active tool, so the ghost and the checkpoint can never
    // be two different transforms.
    bool dragTransform(gp_Trsf& out) const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    // The body the gizmo stands on, re-derived from the live predicate on
    // every refresh() rather than trusted across a rebuild.
    int myBodyId = 0;
    // Which arm is being dragged (0/1/2) and the drag's value - millimetres
    // for Move, degrees for Rotate, a factor for Scale (myFactor). All
    // meaningful only while a drag is live.
    int myAxis = -1;
    double myDistance = 0.0;
    double myFactor = 1.0;
    bool myHasPreview = false;
};
