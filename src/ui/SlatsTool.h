#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include "ModelingOps.h"

#include <TopoDS_Shape.hxx>

// A QPointer needs the COMPLETE type it points at - the pointer's own
// data() static_casts through QObject* - and a forward declaration is not
// it. These headers compiled for two milestones only because something
// earlier in every translation unit happened to include the real header
// first; adding one new header to the moc build was enough to change that
// order and break it. A header that declares a QPointer<T> includes T.
#include <QLineEdit>
#include <QAbstractButton>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class MainWindow;
class OcctViewWidget;
class QAbstractButton;
class QHideEvent;
class QLineEdit;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

// Slats (improvements item 11) - the Qt half of the tool: pick the flat face
// of a panel and it fills with evenly spaced battens standing proud of it.
//
// The split is ReMeasureTool's, which is MitreTool's, which is PullArrow's.
// ModelingOps owns the layout - how many slats fit, where each one sits, and
// every refusal - MainWindow owns the gesture's state and its commit, and this
// widget is the chip: three numbers, a Flip, a live count, and the
// Enter/Escape claim.
//
// Its visibility is DERIVED: refresh() reads MainWindow::slatsActive() on
// every appStateChanged, and nothing else shows or hides it. MainWindow ends
// the gesture on any document change, sketch, render mode, compare pane,
// close question or trip to the library, so this widget never has to remember
// to.
//
// The contract, each clause ExtrudePreview's or ReMeasureTool's first:
//   - the ghost is MainWindow::slatsResult() - the very call the commit makes
//     - on the dedicated setModelingPreview() channel, never setPreview() and
//     never in the document;
//   - the three fields are read in the DISPLAYED UNIT (Measure::parseLength),
//     and seeded in it, so what they show and what they read are one number;
//   - input that does not read keeps the last good ghost and marks the field;
//     sizes the geometry refuses show NO ghost and the chip says why;
//   - the COUNT is not typed. It is derived from the width and the gap and
//     shown ("24 slats"), because the thing a user actually knows is how wide
//     a batten is, and the thing they cannot work out in their head is how
//     many of them fit;
//   - Enter and Escape are claimed application-wide while the chip is
//     visible, since orbiting the camera takes focus off the fields.
//
// The three fields and the Flip button are SIBLINGS parented to the viewport,
// never children of this card: the card is WA_TransparentForMouseEvents, and
// that attribute takes a widget's whole subtree out of hit-testing.
class SlatsTool : public QWidget {
    Q_OBJECT

public:
    SlatsTool(MainWindow* window, OcctViewWidget* view);
    ~SlatsTool() override;

    // THE predicate's mirror, connected to appStateChanged.
    void refresh();
    // Re-places the chip beside the face it is filling. cameraChanged.
    void reposition();
    // Re-places and re-raises, from ViewportOverlay::laidOut().
    void replace();

    QLineEdit* widthField() const { return myWidth; }
    QLineEdit* gapField() const { return myGap; }
    QLineEdit* depthField() const { return myDepth; }
    QAbstractButton* flipButton() const;

    // The plan the fields currently read, in millimetres.
    ModelingOps::SlatPlan plan() const { return myPlan; }
    int count() const { return myCount; }
    bool hasPreview() const { return myHasPreview; }
    QString reasonText() const { return myReason; }

    QStringList paintedTexts() const;

    // Escape's route: ends the gesture with nothing changed.
    void cancel();
    // Enter's route: MainWindow::slatsApply() with whatever the fields read.
    void commit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin();
    void end();
    void updatePreview();
    void applySize();
    void applyControlStyles();
    void syncControls();
    void markInvalid(bool invalid);
    void seedFields();

    // A length in the display unit with no unit suffix and no thousands
    // comma, so Measure::parseLength() can read straight back what this put
    // in the field - ReMeasureTool::plainLength()'s reason, one file over.
    static QString plainLength(double millimetres);

    QString countText() const;
    QString hintText() const;
    static QString labelFor(int index);

    int rowWidth() const;
    QRect labelRect(int index) const;
    QRect fieldRect(int index) const;
    QRect flipRect() const;
    QRect countRect() const;
    QRect hintRect() const;
    QRect reasonRect() const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    bool myActive = false;
    int myBodyId = 0;
    ModelingOps::SlatPlan myPlan;
    int myCount = 0;
    bool myHasPreview = false;
    bool myInvalid = false;
    QString myReason;

    // The last ghost built, keyed on what built it: appStateChanged fires on
    // every selection click and every other gizmo's refresh() clears the
    // shared preview channel on the way past, so the ghost is re-shown from
    // here rather than rebuilt by a fresh kernel pass each time.
    TopoDS_Shape myPreviewShape;
    int myPreviewRevision = -1;
    ModelingOps::SlatPlan myPreviewPlan;

    QPointer<QLineEdit> myWidth;
    QPointer<QLineEdit> myGap;
    QPointer<QLineEdit> myDepth;
    QPointer<QAbstractButton> myFlip;
};
