#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include "ModelingOps.h"

#include <TopoDS_Shape.hxx>

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class MainWindow;
class OcctViewWidget;
class QHideEvent;
class QLineEdit;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

// Re-Measure (improvements item 8) - the Qt half of the gesture the user
// picked: right-click one of the size numbers drawn around a selected body
// and type the size you want.
//
// The split is MitreTool's, which is PullArrow's. OcctViewWidget owns what
// needs the camera - which number is under the cursor, where it projects, and
// the PIN on the dimension line; MainWindow owns the gesture's state and its
// commit; and this widget is the chip - "Width [450] Enter · Esc" - which
// owns the live preview, the typed value and the Enter/Escape claim.
//
// Its visibility is DERIVED: refresh() reads MainWindow::reMeasureActive() on
// every appStateChanged, and nothing else shows or hides it. MainWindow ends
// the gesture on any selection change, document change, sketch, render mode,
// compare pane, close question or trip to the library - so this widget never
// has to remember to.
//
// The contract, each clause ExtrudePreview's or MitreTool's first:
//   - the preview is built by MainWindow::reMeasureResult() - the call the
//     commit itself makes - and shown on the dedicated setModelingPreview()
//     channel, never setPreview() and never in the document;
//   - typed input is read in the DISPLAYED UNIT (Measure::parseLength), so
//     with centimetres chosen typing 45 means 450 mm - and the field is
//     seeded in that same unit, so what it shows and what it reads can never
//     be two different numbers;
//   - input that does not read, or reads at or below zero, keeps the last
//     good preview and marks the field; a size the geometry refuses shows NO
//     ghost, and the chip says why in a reason row;
//   - moving the pin re-previews immediately, because which end stays put
//     changes the shape and not merely the bookkeeping;
//   - Enter and Escape are claimed application-wide while the chip is visible
//     (KeyClaim), because the press that moves the pin or orbits the camera
//     takes focus off the field.
//
// The field is a SIBLING parented to the viewport, never a child of this
// card: the card is WA_TransparentForMouseEvents, and that attribute takes a
// widget's whole subtree out of hit-testing. It carries WA_NoMousePropagation
// so a click in it never reaches the viewport's pick.
class ReMeasureTool : public QWidget {
    Q_OBJECT

public:
    ReMeasureTool(MainWindow* window, OcctViewWidget* view);
    ~ReMeasureTool() override;

    // THE predicate's mirror, connected to appStateChanged - see the class
    // comment.
    void refresh();
    // Re-places the chip against the size number it belongs to. cameraChanged.
    void reposition();
    // Re-places and re-raises, from ViewportOverlay::laidOut().
    void replace();

    QLineEdit* field() const;

    // Every string this chip paints, for gui_smoke's banned-word sweep - read
    // from the same functions paintEvent() draws through.
    QStringList paintedTexts() const;

    // The last size that actually read, in MILLIMETRES (the field is typed in
    // the display unit; this is what it means). NOT size() - that is
    // QWidget's own, and this class is a widget.
    double typedSize() const { return mySize; }
    // True while a preview ghost is on screen for this gesture.
    bool hasPreview() const { return myHasPreview; }
    // Why the current value shows no ghost, or empty.
    QString reasonText() const { return myReason; }

    // Escape's route: ends the gesture with nothing changed.
    void cancel();
    // Enter's route: MainWindow::reMeasureTo() with whatever the field reads.
    void commit();
    // The pin moved (a click on one of its three marks), so the ghost has to
    // be rebuilt around the other end. MainWindow's own route in.
    void anchorChanged();

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

    // A length in the display unit with no unit suffix and no thousands
    // comma, so Measure::parseLength() can read straight back what this put
    // in the field - ExtrudePreview::defaultHeightText()'s idiom, plus the
    // comma, which a body over 999 mm long would otherwise carry into a
    // field that refuses commas by grammar.
    static QString plainLength(double millimetres);

    QString labelText() const;
    QString hintText() const;

    int rowWidth() const;
    QRect labelRect() const;
    QRect fieldRect() const;
    QRect hintRect() const;
    QRect reasonRect() const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    bool myActive = false;
    int myBodyId = 0;
    int mySizeIndex = -1;
    double mySize = 0.0;
    bool myHasPreview = false;
    bool myInvalid = false;
    QString myReason;

    // The last preview built, keyed on what built it - appStateChanged fires
    // on every selection click and the other gizmos' own refresh() clears the
    // shared preview channel on the way past, so the ghost is re-shown from
    // here rather than rebuilt by a fresh kernel pass each time.
    TopoDS_Shape myPreviewShape;
    int myPreviewRevision = -1;
    double myPreviewSize = -1.0;
    int myPreviewAnchor = -1;

    QPointer<QLineEdit> myField;   // a sibling, not a child - see the class comment
};
