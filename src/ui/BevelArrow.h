#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <QPointer>
#include <QStringList>
#include <QWidget>

class MainWindow;
class OcctViewWidget;
class QHideEvent;
class QLineEdit;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

// Where a bevel gesture is measured: the edge's midpoint, and the direction
// "outward" means for that edge.
//
// The axis is PERPENDICULAR to the edge, along the bisector of the two
// adjacent faces' OUTWARD normals. Both halves matter and both are earned:
//
//   - Outward, derived properly. BRepAdaptor_Surface never applies
//     TopAbs_Orientation, so on a REVERSED face the surface normal points
//     INTO the body - three of six faces of a plain box are REVERSED. Get it
//     wrong and the bisector points inward, so dragging away from the body
//     rounds it and dragging into it flattens it: the gesture reads exactly
//     backwards. This is Phase 4's lesson (lockToFace) and Task 2's
//     (PullArrow::begin), applied a third time rather than assumed.
//   - Perpendicular. The two normals are perpendicular to the edge on a box,
//     so their sum already is - but on a body whose faces meet the edge at an
//     angle it is not, and an axis with a component ALONG the edge would slide
//     the arrow off the edge it belongs to as the drag went on. The component
//     along the edge is removed explicitly.
//
// Qt-free, and deliberately a free function rather than a member: MainWindow's
// predicate and the widget both need the answer, and two copies of a
// derivation whose sign convention is this easy to get backwards is exactly
// how the two would drift.
namespace BevelAxis {

// False - leaving both outputs untouched - unless `edge` is straight, belongs
// to `body`, has exactly two adjacent faces, and those two faces' outward
// normals actually define a bisector (they are not opposed, which is what a
// seam edge or a zero-thickness sliver would give).
bool derive(const TopoDS_Shape& body, const TopoDS_Edge& edge,
            gp_Pnt& centre, gp_Dir& outward);

}   // namespace BevelAxis

// The bevel gesture's value chip - the Qt half of "select an edge, drag one
// way to round it, the other way to flatten it".
//
// The 3D half is drawn by OcctViewWidget through the same double-headed
// arrow renderer the face pull uses (see PullArrowRenderer, whose header
// records why that renderer is shared rather than copied). This class is
// PullArrow's shape almost line for line, and deliberately so: the field
// contract, the application-wide Enter/Escape claim, the dedicated preview
// channel, the comma-stripped field text and the "press claims the gesture"
// rule are all the same rules, and the long-form reasons for every one of
// them are recorded on PullArrow and ExtrudePreview.
//
// What is genuinely different here is that ONE gesture selects between TWO
// kernel operations. The drag's sign picks the kind - inward (against the
// bisector) rounds, outward flattens - and the field then carries the SIZE
// alone, unsigned. A signed field would have been less code and a worse
// control: "-20" reading as a 20 mm radius is a rule the user has to be told,
// while a chip that names the kind in words is one they can read.
class BevelArrow : public QWidget {
    Q_OBJECT

public:
    BevelArrow(MainWindow* window, OcctViewWidget* view);
    ~BevelArrow() override;

    // THE predicate, in one place, used to show and to hide - connected to
    // MainWindow::appStateChanged(), never driven from an event. Reads
    // MainWindow::bevelTarget(), which updateActions() and the status label
    // read too, so the arrow and the teaching text can never disagree about
    // whether a bevel is possible.
    void refresh();

    // Re-places the chip against the arrow's projected head and rebuilds the
    // arrow at the new scale. Driven by OcctViewWidget::cameraChanged.
    void reposition();
    // Re-places AND re-raises, from ViewportOverlay::laidOut() - the one
    // moment every anchored cluster is at its final rectangle.
    void replace();

    // Out of line for the same reason PullArrow::field() is: QPointer's
    // converting operator needs a complete QLineEdit.
    QLineEdit* field() const;

    // Every string this chip paints, for gui_smoke's banned-word sweep.
    QStringList paintedTexts() const;

    // "Fillet" or "Chamfer" - the kind, in the user's words.
    QString kindText() const;
    // "R 20 mm" or "C 2 cm" - the size the field currently holds, prefixed by
    // the kind's initial and formatted through Measure, so it follows the
    // display unit like every other length in the app.
    QString valueText() const;
    bool isFillet() const { return myFillet; }

    bool hasPreview() const { return myHasPreview; }
    // The last size that actually previewed, in millimetres, always positive.
    double size() const { return mySize; }

    // Drops the preview and returns the size to zero, leaving the arrow and
    // the selection alone - the edge is still selected, so the predicate still
    // holds and the user can simply drag again.
    void cancel();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin(const TopoDS_Edge& edge, int bodyId, const gp_Pnt& centre,
               const gp_Dir& outward);
    void end();
    QRect fieldRect() const;
    QRect hintRect(int line) const;
    // The one place each painted string is spelled out - paintEvent() draws
    // through these and paintedTexts() reports them, so the sweep can never be
    // guarding a different copy than the one on screen.
    QString hintText(int line) const;
    QString textForSize(double millimetres) const;
    void syncFieldGeometry();
    void syncFieldTooltip();
    void updatePreview();
    void commit();
    void markInvalid(bool invalid);
    void onDragged(double millimetres);
    void onReleased(bool dragged);

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    // The edge being bevelled and the document body it belongs to. Both are
    // re-derived from the live selection on every refresh() rather than
    // trusted across a rebuild - edge indices are no more stable than face
    // indices (CLAUDE.md's topological-naming warning).
    TopoDS_Edge myEdge;
    int myBodyId = 0;
    gp_Pnt myCentre;
    gp_Dir myOutward{0.0, 0.0, 1.0};

    // Which operation the drag has chosen. Fillet by default: a rounded edge
    // is the commoner furniture detail, and the first thing the chip says has
    // to be something rather than nothing.
    bool myFillet = true;
    bool myHasPreview = false;
    bool myInvalid = false;
    double mySize = 0.0;
    QPointer<QLineEdit> myField;   // sibling, not a child - see PullArrow.h
};
