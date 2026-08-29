#pragma once
//
// The guided first build: four steps that tick as the user genuinely performs
// them. Step state is DERIVED from live application state on every
// appStateChanged, never stored as a cursor - a stored cursor is a second
// source of truth that drifts from the first.
//
#include <QStringList>
#include <QWidget>

class MainWindow;
class QShowEvent;

class WalkthroughPanel : public QWidget {
    Q_OBJECT

public:
    WalkthroughPanel(MainWindow* window, QWidget* parent);

    int completedSteps() const { return myCompleted; }
    bool isFinished() const { return myFinished; }

    // The four step strings, exposed so gui_smoke's banned-word sweep can
    // reach them - they are painted, not put on any QAction text or tooltip,
    // so nothing else makes them visible to that check.
    QStringList stepTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    // A widget parented under a still-hidden top level (as this one is, built
    // during MainWindow's constructor) does not become genuinely visible - or
    // receive a real show event - until that top level is shown. That is
    // exactly the moment a returning user's already-learned progress needs to
    // be re-checked, since it may have changed since construction.
    void showEvent(QShowEvent* event) override;
    QSize sizeHint() const override { return QSize(260, 168); }

private:
    void refresh();
    void finish();
    QRect skipRect() const;

    MainWindow* myWindow = nullptr;
    int myCompleted = 0;
    bool myFinished = false;

    // The one interactive spot on this otherwise click-through panel (see
    // WalkthroughPanel.cpp for why it is a real child widget rather than a
    // hit-tested region inside this widget's own mousePressEvent). Owned by
    // Qt's parent-child mechanism, not by this pointer.
    QWidget* mySkip = nullptr;

    // Latches: a step stays ticked once reached, because the state that proves
    // it (a sketch in progress, a pending face) is transient by nature.
    bool myStartedSketch = false;
    bool myPlacedPoints = false;
    bool myClosedOutline = false;

    // NOT the stored cursor the design forbids: this is per-session state,
    // rebuilt every time the panel transitions into showing (a fresh build at
    // 0, or a restore-after-reset at the document's current count), and it is
    // read back through document().count() > myBodyBaseline on every refresh
    // rather than being trusted on its own. Its only job is telling "a body
    // that already existed when the guide reappeared" apart from "a body made
    // since" - without it, document().count() > 0 stays true forever once any
    // body exists, so a reset while one is still around would re-complete the
    // guide on the spot instead of genuinely restoring it.
    int myBodyBaseline = 0;
};
