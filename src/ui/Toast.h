#pragma once
//
// A single outcome, delivered over the viewport instead of a modal dialog.
// ToastHost owns at most one Toast and reuses it for every message - a
// second show() call replaces the content and restarts the dismiss timer
// rather than stacking a second widget, so a burst of outcomes never queues
// into something the user has to click through one at a time.
//
// The Undo control follows WalkthroughPanel::syncSkipGeometry() exactly (see
// WalkthroughPanel.cpp for the full reasoning): Qt::WA_TransparentForMouseEvents
// excludes a widget's ENTIRE SUBTREE from hit-testing, so a click-through
// toast body cannot host a clickable child - the control has to be a
// SIBLING, parented to the same viewport, with its geometry and visibility
// derived from the toast's own move/show/hide rather than trusted to an
// event that might never arrive.
//
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

class OcctViewWidget;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;
class QTimer;

class Toast : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Note, Failure };

    explicit Toast(QWidget* parent);
    ~Toast() override;

    // Replaces whatever is currently shown. Does not itself start or stop a
    // dismiss timer - ToastHost owns that, since the duration depends on
    // kind and this widget has no notion of one.
    void setMessage(const QString& text, Kind kind, bool undo);

    QString text() const { return myText; }
    bool hasUndo() const { return myHasUndo; }

    // The sibling Undo control, or nullptr - never null in practice (it is
    // constructed once, alongside this widget, and only hidden), but callers
    // should still treat it as optional the way WalkthroughPanel::skipControl()
    // does.
    QWidget* undoControl() const { return myUndo; }

    // Drives the fade ToastHost animates. QPainter::setOpacity(), not a
    // QGraphicsEffect: this widget is a plain composited child of
    // OcctViewWidget, the way every overlay in this app is (see the class
    // comment on ToolCluster) - OcctViewWidget itself paints on screen via
    // OpenGL with Qt's own paint engine disabled (paintEngine() returns
    // nullptr, per CLAUDE.md), and QGraphicsEffect requires rendering its
    // source widget through Qt's normal offscreen raster path first, which
    // does not exist here. A prior version of this fade used
    // QGraphicsOpacityEffect and crashed - reliably, a few hundred
    // milliseconds into any later repaint-heavy stretch of gui_smoke - for
    // exactly that reason.
    void setOpacity(double opacity);
    double opacity() const { return myOpacity; }

    // Every string this widget can ever paint, so gui_smoke's banned-word
    // sweep has no blind spot here the way it has none for WalkthroughPanel
    // or HintBalloon. "Undo" is painted on the sibling control, never put on
    // a QAction or a tooltip, so nothing else makes it visible to that sweep.
    // The currently displayed message is included too, since - unlike the
    // other two widgets - this one has no fixed set of strings to enumerate
    // up front; whatever is live when the sweep runs is what a viewer would
    // actually be reading.
    QStringList paintedTexts() const;

    // Public, unlike most of this widget's overrides: ToastHost::reposition()
    // needs to size the widget to its current content before it can place it.
    QSize sizeHint() const override;

signals:
    void undoClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void syncUndoGeometry();
    QRect undoRect() const;
    // The one place "Undo" is spelled out - paintEvent() paints it at
    // undoRect() and paintedTexts() reads the same call, so the banned-word
    // sweep can never be guarding a different copy than the one on screen
    // (see WalkthroughPanel's "skip" label, painted by the panel itself for
    // exactly this reason).
    QString undoLabel() const;

    QString myText;
    Kind myKind = Kind::Note;
    bool myHasUndo = false;
    QPointer<QWidget> myUndo;   // sibling, not a child - see class comment
    double myOpacity = 1.0;     // read by paintEvent() via painter.setOpacity()
};

// Owns the single Toast, decides how long each kind stays up before it
// dismisses itself, and forwards the Undo control's click as undoRequested().
// Not a QWidget itself - a QObject parented into MainWindow's own tree so
// gui_smoke can find it with findChild<ToastHost*>(), the same way it finds
// HintBalloon and WalkthroughPanel by walking that tree.
class ToastHost : public QObject {
    Q_OBJECT

public:
    explicit ToastHost(OcctViewWidget* viewport, QWidget* parent = nullptr);

    // Kind decides the timeout - Note 4000 ms, Failure 8000 ms, long enough
    // for a sentence the user must actually read before it goes away.
    // Calling this while a toast is already up replaces its content and
    // restarts the timer; it never creates a second Toast and never stacks.
    void show(const QString& text, Toast::Kind kind, bool undo);

    QString currentText() const;   // empty when nothing is showing
    bool isShowing() const;
    Toast* toast() const { return myToast; }
    QWidget* undoControl() const;

    // The live dismiss countdown in ms, or -1 when nothing is showing.
    // Exposed so gui_smoke can assert the Note/Failure duration contract (the
    // whole reason a Failure outlives a Note) by reading the armed timer
    // rather than actually waiting 4-8 real seconds for it to elapse.
    int remainingMs() const;

signals:
    void undoRequested();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void reposition();
    void dismiss();
    // Animates myToast's opacity from its current value to `opacity` over
    // Theme::motionMs()/motionCurve(), then calls onFinished (which may be
    // empty) - or, when myViewport->animationsEnabled() is false, the way
    // OcctViewWidget::animateTo() already treats its own camera animation:
    // skips the animation entirely, sets the value directly, and calls
    // onFinished synchronously within this call. gui_smoke disables
    // animations on the real viewport for exactly this reason - a fade must
    // never turn a lifetime check (isShowing(), remainingMs()) into
    // something that has to wait on a timer it does not itself own.
    void fadeTo(double opacity, std::function<void()> onFinished);

    OcctViewWidget* myViewport = nullptr;
    // A QPointer, not a raw pointer: myToast is owned by the viewport (it is
    // parented to it, per the class comment above), not by ToastHost, so if
    // the viewport ever tore down first this would otherwise dangle - the
    // same reasoning as Toast::myUndo.
    QPointer<Toast> myToast;
    QTimer* myTimer = nullptr;
    // One QVariantAnimation, constructed once (see the constructor) and kept
    // for the lifetime of this host - not built fresh per fadeTo() call, and
    // deliberately left at the default KeepWhenStopped rather than
    // DeleteWhenStopped. A DeleteWhenStopped animation self-deletes the
    // instant it finishes on its own, including a fade that simply ran to
    // completion with nobody calling stop() on it - an earlier version of
    // this class used exactly that policy on a raw pointer, and a toast's
    // own dismiss timer firing while animations happened to be briefly
    // enabled left that pointer dangling until the next fadeTo() call
    // dereferenced it. Reusing one animation removes the question rather
    // than tracking it with a QPointer: valueChanged is connected once, in
    // the constructor, reading myToast live each time rather than a capture
    // that could go stale.
    QVariantAnimation* myFade = nullptr;
    // The callback for whichever fade is currently running, if any - read
    // and cleared by the one permanent `finished` connection made in the
    // constructor. fadeTo() itself sets this, never a second connection.
    std::function<void()> myFadeFinished;
};
