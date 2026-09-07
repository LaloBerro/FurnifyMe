#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
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
// display discipline (mode -1, never pickable, Topmost) and the theme
// re-application. What a subclass supplies is geometry and nothing else:
// buildStrokes() is handed a pivot, a view direction and a world-per-pixel and
// returns the lines to draw. A RotateGizmoRenderer is then three arcs and a
// ScaleGizmoRenderer one small box, each about thirty lines, with no
// opportunity to forget one of the disciplines above.
class GizmoRenderer {
public:
    virtual ~GizmoRenderer() = default;

    void attach(const Handle(AIS_InteractiveContext)& context);
    // Drops the context and everything built against it, WITHOUT touching the
    // viewer - PullArrowRenderer::detach()'s own contract, same one caller
    // (OcctViewWidget::releaseGlResources()).
    void detach();

    // Draws the gizmo at `pivot`. `viewDirection` lets a subclass turn its
    // geometry to face the eye; `worldPerPixel` sizes the whole thing in
    // LOGICAL screen pixels, and `pixelRatio` is how many device pixels one of
    // those is worth. Both are needed and they are not interchangeable:
    // worldPerPixel() divides by the widget's own logical height, while the
    // two things OCCT sizes for us rather than from our geometry - a line's
    // width and a label's height - are counted in DEVICE pixels. Without the
    // ratio a 150% display draws the whole drawing half again as big and its
    // strokes and letters exactly as before, which is a different drawing.
    // Replaces whatever was drawn before.
    //
    // Returns TRUE only when what is on screen actually changed - the caller
    // owns the frame and asks for one only on a true return, which is the
    // measured saving PullArrowRenderer's own header records (a rebuild
    // riding along with applyCameraState()'s redraw rather than forcing a
    // second vsync).
    bool show(const gp_Pnt& pivot, const gp_Dir& viewDirection, double worldPerPixel,
              double pixelRatio);
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

    const gp_Pnt& pivot() const { return myPivot; }

    // One drawn line. Public only so the file-local geometry helpers that
    // build rings and discs can name it; nothing outside constructs one.
    struct Stroke {
        gp_Pnt a;
        gp_Pnt b;
    };

protected:
    // THE subclass hook. Called with myPivot/myViewDirection/myWorldPerPixel
    // already set; adds whatever the tool draws through addStrokes().
    virtual void buildStrokes() = 0;

    // One AIS object per call, drawn in `colour` at `widthPx`. Lines only, and
    // that is not a stylistic preference: Graphic3d_ArrayOfTriangles draws
    // NOTHING AT ALL in this build (recorded twice in this tree - see
    // DimensionRenderer's arrowhead comment and SketchPointMarker's), so a
    // filled cone would be an invisible one. A cone is therefore its own
    // silhouette: a ring plus generatrices, which at eighteen screen pixels
    // reads as solid.
    //
    // It also keeps every colour this gizmo paints EXACT. A shaded AIS_Shape
    // cone would be lit, and a lit pixel is not the token pixel - which would
    // cost gui_smoke the one check that can say the arms are drawn in
    // Theme::gizmoAxisX/Y/Z rather than in something approximately like them.
    void addStrokes(const std::vector<Stroke>& strokes, const QColor& colour, double widthPx);

    // One AIS_TextLabel, same discipline as addStrokes(): mode -1, Topmost,
    // never pickable. `heightPx` is the font's EM box in DEVICE pixels, which
    // is the unit OCCT sizes a non-zoomable label in - see AxisCard::
    // letterEmPx() for why that is not the same number Qt was given.
    //
    // The face is DimensionRenderer::fontFamily(), deliberately shared rather
    // than resolved again here: that function spills the app's DM Sans out of
    // the Qt resource and registers it with Font_FontMgr exactly once, and two
    // registrations of one resource are two chances to end up with two family
    // names and a gizmo whose letters are not the app's font at all.
    void addLabel(const QString& text, const gp_Pnt& at, const QColor& colour, double heightPx);

    double worldPerPixel() const { return myWorldPerPixel; }
    double pixelRatio() const { return myPixelRatio; }
    const gp_Dir& viewDirection() const { return myViewDirection; }

private:
    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    gp_Pnt myPivot;
    // What the gizmo currently on screen was built from, so show() can tell a
    // call that changes nothing from one that does.
    gp_Dir myViewDirection{0.0, 0.0, -1.0};
    double myWorldPerPixel = 0.0;
    double myPixelRatio = 1.0;
    // Defeats show()'s pose cache for one call - the appearance changed, not
    // the geometry, and the cache key knows nothing about appearance.
    bool myForceRebuild = false;
};

// The MOVE tool's presentation: THE AXIS CARD'S OWN DRAWING, in the scene.
//
// Not "the card's language" or "the card's family" - the card's drawing. Every
// element of AxisGizmo::paintEvent() has its counterpart here at the card's own
// proportions, and both read one set of numbers (AxisCard, in AxisGizmo.h):
// three thin arms in Theme::gizmoAxisX/Y/Z at the card's stroke-to-arm ratio,
// filled cone tips at the card's cone-to-arm ratio, HOLLOW BALLS on the three
// negative directions at the card's ball-to-arm ratio, the neutral filled hub,
// and the small lowercase x/y/z past each cone in the card's own badge face.
// One number is chosen here rather than derived - how long an arm is - and
// everything else is that scale times a card number.
//
// gui_smoke pins it by MEASUREMENT, not by shared constants: it renders the
// card, dumps the scene, and compares five element-to-arm ratios read off the
// two RENDERINGS. Shared constants only stop the two drawings disagreeing about
// what the numbers are; the pin is what stops them disagreeing about how they
// are used.
//
// The three negative balls are grab targets too, exactly as the three cones
// are. A drag on one is measured against the SAME infinite world line the
// positive arm uses (armAxis()), so it needs no maths of its own - it simply
// resolves to a negative distance.
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

    // A handle's outer point - the cone's nominal tip on the positive side,
    // the ball's centre on the negative one. Where the value chip goes, and
    // where the hit test's span ends.
    gp_Pnt handleTip(int axis, bool positive) const;
    // Where a handle's GRABBABLE span starts, as a fraction of the arm. The
    // inner third is excluded on purpose: all six handles meet at the hub, so
    // near it the nearest-handle-wins rule would be decided by sub-pixel noise
    // and the user would get an axis at random.
    static constexpr double kGrabStartFraction = 0.3;
    gp_Pnt handleGrabStart(int axis, bool positive) const;

    // The positive spellings, kept because most callers only ever mean +axis.
    gp_Pnt armTip(int axis) const { return handleTip(axis, true); }
    gp_Pnt armGrabStart(int axis) const { return handleGrabStart(axis, true); }

protected:
    void buildStrokes() override;

private:
    // The arm's world length, derived from worldPerPixel() at the last build.
    double myArmLength = 1.0;
};

// The Qt half of the Move gesture - the value chip, the live ghost preview and
// the Escape claim.
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
    void onReleased(bool dragged);

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    // The body the gizmo stands on, re-derived from the live predicate on
    // every refresh() rather than trusted across a rebuild.
    int myBodyId = 0;
    // Which arm is being dragged (0/1/2) and how far, in millimetres. Both
    // are meaningful only while a drag is live.
    int myAxis = -1;
    double myDistance = 0.0;
    bool myHasPreview = false;
};
