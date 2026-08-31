#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <vector>

class MainWindow;
class OcctViewWidget;
class QHideEvent;
class QLineEdit;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

// The face pull, split the way the gesture actually splits.
//
// PullArrowRenderer draws the double-headed arrow IN THE 3D SCENE, on the
// same Graphic3d_AspectLine3d path DimensionRenderer already uses. That is
// not a stylistic choice: an arrow painted over the viewport would slide off
// the face the moment the camera turned, and the whole point of this gesture
// is that the arrow is glued to the face it pulls. It is sized in screen
// pixels through OcctViewWidget::worldPerPixel(), so it reads the same at any
// zoom (DimensionRenderer's rule), and rebuilt on cameraChanged so its
// arrowheads stay square to the eye.
//
// It is SHARED, not copied: the bevel gesture (Task 4) wants exactly this
// shape - a double-headed drag arrow at a point, along a direction, sized in
// screen pixels - so OcctViewWidget holds a second instance of this class for
// it rather than a second copy of the class. The name is the face pull's
// because that is where it was first needed; everything it does is generic,
// and the early-out below is a measured 33.3 ms -> 0.17 ms fix that must not
// exist in two places to be maintained in two places.
//
// PullArrow is the Qt half - the value chip beside the arrow's outward head.
// A field drawn in 3D cannot take focus or a keystroke, so the number the
// user types has to be a real QLineEdit. Its entire contract is
// ExtrudePreview's, for the same reasons, and the comments there are the
// long form of every rule repeated here: the preview is built by the SAME
// ModelingOps call the commit uses, invalid input keeps the last good
// preview and marks the field, and Enter/Escape are claimed
// application-wide while the chip is visible (an RMB orbit - the entire
// reason a live preview exists - takes focus off the field).
//
// The field is a SIBLING parented to the viewport, never a child of this
// panel: Qt::WA_TransparentForMouseEvents excludes a widget's ENTIRE SUBTREE
// from hit-testing, so an interactive control under a click-through panel is
// unreachable by any real click. It carries Qt::WA_NoMousePropagation so a
// click into it cannot bubble to the viewport underneath and re-pick.
class PullArrowRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);

    // Draws the arrow centred on `centre`, along `outward`. `viewDirection`
    // only orients the arrowheads' two strokes so they fan out across the
    // screen rather than edge-on to it; `worldPerPixel` sizes the whole
    // thing in screen pixels. Replaces whatever was drawn before.
    // `updateViewer` false leaves the redraw to the caller, for the one
    // caller that is about to redraw anyway - see
    // OcctViewWidget::applyCameraState(). UpdateCurrentViewer() blocks on
    // vsync in this build (~16 ms), so an arrow that forced its own frame on
    // every camera step doubled the cost of an orbit.
    void show(const gp_Pnt& centre, const gp_Dir& outward, const gp_Dir& viewDirection,
              double worldPerPixel, bool updateViewer = true);
    void clear(bool updateViewer = true);
    bool isShowing() const { return !myObjects.empty(); }

    // Rebuilds whatever is on screen, from the parameters it was built with.
    //
    // Exists for exactly one caller: a Theme edit. The arrow's colour is
    // Theme::accent() baked into the AIS object at build time, and show()
    // early-outs on an unchanged pose - so a theme change alone left a live
    // arrow wearing the old accent until the camera happened to move far
    // enough to defeat the cache. A no-op when nothing is showing, so it
    // cannot make an arrow appear.
    void reapplyTheme();

    // The line a drag is measured against: the outward normal through the
    // face centre. Meaningful only while showing.
    gp_Lin axis() const { return gp_Lin(myCentre, myOutward); }
    // The two tips. The value chip is placed beside the outward one.
    gp_Pnt head() const;
    gp_Pnt tail() const;

private:
    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    gp_Pnt myCentre;
    gp_Dir myOutward{0.0, 0.0, 1.0};
    double myHalfLength = 1.0;
    // What the arrow currently on screen was built from, so show() can tell a
    // call that changes nothing from one that does - see the early-out there.
    gp_Dir myViewDirection{0.0, 0.0, -1.0};
    double myWorldPerPixel = 0.0;
    // Defeats show()'s pose cache for one call - the appearance changed, not
    // the geometry, and the cache key knows nothing about appearance. Set by
    // reapplyTheme() and cleared by the show() it drives.
    bool myForceRebuild = false;
};

class PullArrow : public QWidget {
    Q_OBJECT

public:
    PullArrow(MainWindow* window, OcctViewWidget* view);
    ~PullArrow() override;

    // THE predicate, in one place, used to show and to hide. Connected to
    // MainWindow::appStateChanged(), never driven from an event: a gizmo
    // that appeared on a click and disappeared on some other click would be
    // two rules that drift. Reads MainWindow::canPullSelectedFace(), which
    // updateActions() and the status label read too, so the arrow, the
    // enabled state and the teaching text can never disagree about whether a
    // pull is possible.
    void refresh();

    // Re-places the chip against the arrow's projected head, and rebuilds the
    // arrow itself at the new scale. Driven by OcctViewWidget::cameraChanged.
    void reposition();
    // Re-places AND re-raises, from ViewportOverlay::laidOut() - the one
    // moment every anchored cluster is at its final rectangle and already
    // raise()d. See ExtrudePreview::replace() for why a raw resize event is
    // the wrong hook.
    void replace();

    // Defined in the .cpp: QPointer<QLineEdit>'s converting operator needs a
    // complete QLineEdit, and this header only forward-declares it (the same
    // reason ExtrudePreview::field() is out of line).
    QLineEdit* field() const;

    // Every string this chip paints, for gui_smoke's banned-word sweep.
    QStringList paintedTexts() const;

    bool hasPreview() const { return myHasPreview; }
    // The last distance that actually previewed, in millimetres.
    double distance() const { return myDistance; }

    // Drops the preview and returns the value to zero, leaving the arrow and
    // the selection alone - the face is still selected, so the predicate
    // still holds and the user can simply drag again.
    void cancel();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin(const TopoDS_Face& face, int bodyId);
    void end();
    QRect fieldRect() const;
    QRect hintRect() const;
    // The one place each painted string is spelled out - paintEvent() draws
    // through these and paintedTexts() reports them, so the sweep can never
    // be guarding a different copy than the one on screen.
    QString labelText() const;
    QString hintText() const;
    // A distance in millimetres, spelled in whatever unit the field reads -
    // "30" in millimetres, "3" in centimetres. Because a drag writes the
    // field and the field is what commits, the number the user sees is
    // exactly the number that gets built.
    QString textForDistance(double millimetres) const;
    void syncFieldGeometry();
    void updatePreview();
    void commit();
    void markInvalid(bool invalid);
    void onDragged(double millimetres);
    void onReleased(bool dragged);

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    // The face being pulled and the document body it belongs to. Both are
    // re-derived from the live selection on every refresh() rather than
    // trusted across a rebuild - face indices are not stable (CLAUDE.md's
    // topological-naming warning), and a pull commits by replacing the body
    // these came from.
    TopoDS_Face myFace;
    int myBodyId = 0;
    gp_Pnt myCentre;
    gp_Dir myOutward{0.0, 0.0, 1.0};

    bool myHasPreview = false;
    bool myInvalid = false;
    double myDistance = 0.0;
    QPointer<QLineEdit> myField;   // sibling, not a child - see the class comment
};
