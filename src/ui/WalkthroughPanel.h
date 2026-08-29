#pragma once
//
// The guided first build: four steps that tick as the user genuinely performs
// them. Step state is DERIVED from live application state on every
// appStateChanged, never stored as a cursor - a stored cursor is a second
// source of truth that drifts from the first.
//
#include <QWidget>

class MainWindow;
class QShowEvent;

class WalkthroughPanel : public QWidget {
    Q_OBJECT

public:
    WalkthroughPanel(MainWindow* window, QWidget* parent);

    int completedSteps() const { return myCompleted; }
    bool isFinished() const { return myFinished; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
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

    // Latches: a step stays ticked once reached, because the state that proves
    // it (a sketch in progress, a pending face) is transient by nature.
    bool myStartedSketch = false;
    bool myPlacedPoints = false;
    bool myClosedOutline = false;
};
