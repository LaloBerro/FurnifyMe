#pragma once
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class MainWindow;
class OcctViewWidget;
class QAbstractButton;
class QCheckBox;
class QHideEvent;
class QLabel;
class QShowEvent;

// THE BOOLEAN CHIP - the small card a live Subtract, Union or Intersect puts
// at the bottom of the viewport.
//
// It is deliberately the SMALLEST chip in this app, and that is the picked
// design rather than an omission. Option B of the mockup round put the
// gesture's real question - which body survives - on the WOOD, as badges you
// click (BooleanBadgeRenderer), precisely so there is no list on a panel to
// read and match back to bodies by name. What is left for a chip is only what
// has nowhere else to live: the verb, one switch, and the two keys.
//
// The split is SlatsTool's, which is ReMeasureTool's, which is MitreTool's,
// which is PullArrow's: ModelingOps owns the geometry and every refusal
// (applyBooleanMulti, booleanRegion), MainWindow owns the gesture's state and
// its commit, and this widget owns the chip and the application-wide
// Enter/Escape claim.
//
// Its visibility is DERIVED: refresh() reads MainWindow::booleanActive() on
// every appStateChanged, and nothing else shows or hides it. MainWindow ends
// the gesture on any document change, a sketch, a pending outline, render
// mode, a compare pane, the close question or a trip to the library, so this
// widget never has to remember to.
//
// Enter and Escape are claimed application-wide while it is visible, with a
// ShortcutOverride claim so QShortcutMap cannot take the key first. Not
// optional, and for ExtrudePreview's own measured reason: an RMB orbit - the
// entire point of a LIVE region you can look at from another angle - takes
// focus off this card, and a focus-scoped filter would stop working the
// moment it did.
class BooleanTool : public QWidget {
    Q_OBJECT

public:
    BooleanTool(MainWindow* window, OcctViewWidget* view);
    ~BooleanTool() override;

    // THE predicate's mirror, connected to appStateChanged.
    void refresh();
    // Re-places the card at the bottom of the viewport. cameraChanged and
    // ViewportOverlay::laidOut().
    void replace();

    QAbstractButton* keepToolBox() const;

    QStringList paintedTexts() const;

    // Escape's route: ends the gesture with nothing changed.
    void cancel();
    // Enter's route: MainWindow::booleanApply().
    void commit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyStyles();
    void syncControls();
    void layoutCard();
    QString titleText() const;
    QString hintText() const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;
    QPointer<QCheckBox> myKeepTool;
    bool myClaimInstalled = false;
    QString myTitle;
    QString myHint;
};
