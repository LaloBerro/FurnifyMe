#pragma once
// What the viewport shows while a heavy furniture is being opened.
//
// Opening a big document is SYNCHRONOUS - the file is decoded, then every
// body is tessellated for display - so for a few seconds the editor window is
// up with nothing in it. That blank window is what this replaces: the app
// mark, the furniture's name, and a bar that fills as the bodies arrive.
//
// It is NOT a second window and NOT a QSplashScreen. This app's cards live
// over the viewport (the close question, the name question), and a separate
// splash would be a top-level window the editor/selector handoff would then
// have to know about - see EditorSelectorHandoff.h for why a third window is
// not free. The whole widget IS the scrim: it covers the viewport edge to
// edge, paints the wash and the card, and swallows every mouse event that
// reaches it.
//
// HOW IT REPAINTS DURING A BLOCKING LOAD: step() pumps the event loop with
// QEventLoop::ExcludeUserInputEvents. Excluding input is the whole of what
// makes that safe - the paint happens, the timers do not fire and no click,
// key or wheel is delivered, so nothing can re-enter the load that is
// half-way through replacing the document. A click that lands during the load
// is simply not delivered at all, rather than being delivered to a window in
// a state no gesture expects.
#include <QString>
#include <QStringList>
#include <QWidget>

class QHideEvent;

class LoadingCard : public QWidget {
    Q_OBJECT

public:
    explicit LoadingCard(QWidget* viewport);

    // Opens the card for `title` (the furniture's own name) with the bar at
    // zero and no total yet - the file has to be read before anybody knows
    // how many bodies there are.
    void begin(const QString& title, const QString& step);
    // How many steps the bar is measuring, once that is known. 0 leaves the
    // bar in its "working" state rather than showing a fraction of nothing.
    void setTotal(int total);
    // One step done: advances the bar, repaints and pumps the event loop so
    // the repaint actually reaches the screen mid-load.
    void step(const QString& stepText = QString());
    void end();

    bool isBusy() const { return !isHidden(); }

    // Re-fits the scrim to the viewport - from ViewportOverlay::laidOut() and
    // from the viewport's own resize.
    void replace();

    QString titleText() const { return myTitle; }
    QString stepText() const { return myStep; }
    // This app's own copy on the card, for gui_smoke's banned-word sweep. The
    // furniture's NAME is the user's word and is not here.
    QStringList paintedTexts() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void layoutCard();
    void pump();

    QRect myCard;
    QString myTitle;
    QString myStep;
    int myDone = 0;
    int myTotal = 0;
};
