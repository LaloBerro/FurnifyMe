#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include "ModelingOps.h"

#include <TopoDS_Face.hxx>
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
class QPushButton;
class QResizeEvent;
class QShowEvent;

// Mitre end (improvements item 4) - the Qt half of the gesture the user picked
// ("C+": the protractor dial, plus a chip with a typed angle and Flip).
//
// The split is PullArrow's. OcctViewWidget draws the dial IN THE SCENE and
// owns its drag (a dial painted over the viewport would slide off the board
// the moment the camera turned); this widget is the chip - "Mitre [45°]
// ⇄ Flip  Enter · Esc" - and owns everything the gesture DOES: the live
// preview, the dial's angle, Flip, and the Enter/Escape claim.
//
// Its visibility is DERIVED: refresh() reads MainWindow::mitreEndActive() on
// every appStateChanged, and nothing else shows or hides it. MainWindow owns
// the gesture's state and ends it on any selection change, document change,
// sketch, render mode, compare pane or trip to the library - so this widget
// never has to remember to.
//
// The contract, each clause ExtrudePreview's or PullArrow's first:
//   - the preview is built by ModelingOps::mitreEnd() - the call the commit
//     makes - and shown on the dedicated setModelingPreview() channel, never
//     setPreview() and never in the document;
//   - typed input is read EXACTLY (Measure::parseAngle); input that does not
//     read, or reads outside 1..89, keeps the last good preview and marks the
//     field; an angle the geometry refuses shows NO ghost, and the chip says
//     why in a reason row;
//   - dragging the dial writes the FIELD, and the field is what previews and
//     commits - one value, one path;
//   - Enter and Escape are claimed application-wide while the chip is
//     visible (KeyClaim), because the press that drags the dial or orbits
//     takes focus off the field.
//
// The field and the Flip button are SIBLINGS parented to the viewport, never
// children of this card: the card is WA_TransparentForMouseEvents, and that
// attribute takes a widget's whole subtree out of hit-testing. Both carry
// WA_NoMousePropagation so a click on them never reaches the viewport's pick.
class MitreTool : public QWidget {
    Q_OBJECT

public:
    MitreTool(MainWindow* window, OcctViewWidget* view);
    ~MitreTool() override;

    // THE predicate's mirror, connected to appStateChanged - see the class
    // comment.
    void refresh();
    // Re-places the chip against the dial's projected extent. cameraChanged.
    void reposition();
    // Re-places and re-raises, from ViewportOverlay::laidOut().
    void replace();

    QLineEdit* field() const;
    QPushButton* flipButton() const;

    // Every string this chip paints, for gui_smoke's banned-word sweep - read
    // from the same functions paintEvent() draws through.
    QStringList paintedTexts() const;

    // The last angle that actually read, in degrees, and which edge of the end
    // keeps the board's length (ModelingOps::MitreSide).
    double angle() const { return myAngle; }
    ModelingOps::MitreSide side() const { return mySide; }
    // True while a preview ghost is on screen for this gesture.
    bool hasPreview() const { return myHasPreview; }
    // Why the current value shows no ghost, or empty.
    QString reasonText() const { return myReason; }

    // Flip's route (the button calls it): the NEXT edge of the end keeps the
    // length - left, top, right, bottom, left (ModelingOps::nextMitreSide()).
    void flip();
    // Escape's route: ends the gesture with nothing changed.
    void cancel();
    // Enter's route: MainWindow::mitreEndBy() with whatever the field reads.
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
    void pushDial();
    void applySize();
    void applyControlStyles();
    void syncControls();
    void markInvalid(bool invalid);
    void onDialDragged(double angleDeg);
    // Puts the live kind of cut into the Flip button's and the field's tooltips.
    void applySideTooltips();
    // The live frame for the current side, cached on the revision and side.
    bool frame(ModelingOps::MitreFrame& out);

    QString labelText() const;
    QString hintText() const;
    static QString flipText();

    int rowWidth() const;
    QRect labelRect() const;
    QRect fieldRect() const;
    QRect flipRect() const;
    QRect hintRect() const;
    QRect reasonRect() const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    bool myActive = false;
    TopoDS_Face myFace;
    int myBodyId = 0;
    double myAngle = 45.0;
    ModelingOps::MitreSide mySide = ModelingOps::MitreSide::WidthA;
    bool myHasPreview = false;
    bool myInvalid = false;
    QString myReason;

    // The last preview built, keyed on what built it - appStateChanged fires
    // on every selection click and the other gizmos' own refresh() clears the
    // shared preview channel on the way past, so the ghost is re-shown from
    // here rather than rebuilt by a fresh boolean each time.
    TopoDS_Shape myPreviewShape;
    int myPreviewRevision = -1;
    double myPreviewAngle = -1.0;
    ModelingOps::MitreSide myPreviewSide = ModelingOps::MitreSide::WidthA;

    ModelingOps::MitreFrame myFrame;
    int myFrameRevision = -1;
    ModelingOps::MitreSide myFrameSide = ModelingOps::MitreSide::WidthA;
    bool myFrameOk = false;

    QPointer<QLineEdit> myField;       // siblings, not children - see the class comment
    QPointer<QPushButton> myFlipButton;
};
