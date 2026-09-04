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
// "Derived" turned out to include one more piece of state than it first
// looked like: myDismissing. A fade-out leaves this widget isVisible() ==
// true for the whole fade (it is not actually hidden until the fade
// finishes), so a control whose visibility was only ever `isVisible() &&
// myHasUndo` stayed reachable through the fade too - and a one-shot hide()
// called once at the top of dismiss() is not enough on its own, because
// ToastHost::reposition() re-derives this control's geometry (and, through
// syncUndoGeometry(), its visibility) on every viewport resize for as long
// as the toast is still isVisible(). A resize landing mid-fade re-showed a
// control the one-shot hide() had already closed. myDismissing folds into
// the same predicate syncUndoGeometry() already reads, so no later sync -
// from a resize or anything else - can undo it.
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
    // kind and this widget has no notion of one. Also clears myDismissing:
    // a new message supersedes whatever dismissal, if any, was in progress.
    void setMessage(const QString& text, Kind kind, bool undo);

    // ToastHost calls this at the top of dismiss(), before the fade starts.
    // Folded into the same predicate syncUndoGeometry() already derives the
    // control's visibility from, rather than a one-shot hide() - see the
    // class comment for why a one-shot action here is exactly the bug this
    // exists to close. Undoing it is symmetric with setMessage() above: a
    // fresh show() clears it, a dismiss() sets it.
    void setDismissing(bool dismissing);

    // Whether the Undo the message offers is currently allowed at all.
    // MainWindow derives this from myUndoAction's own enabled state inside
    // updateActions() - CLAUDE.md's single place that decides what is
    // available - so the pill cannot offer a route the menu entry and the
    // chip both refuse. Folded into the same derived predicate
    // syncUndoGeometry() computes, so the control is not merely ignored on
    // click but genuinely absent from hit-testing, and paintEvent() dims the
    // pill to match rather than silently leaving a live-looking control.
    void setUndoEnabled(bool enabled);
    bool undoEnabled() const { return myUndoEnabled; }

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
    // comment on ToolCluster) - and OcctViewWidget renders its content through
    // OpenGL rather than through Qt's raster paint engine, while
    // QGraphicsEffect requires rendering its source widget through that raster
    // path first. A prior version of this fade used QGraphicsOpacityEffect and
    // crashed - reliably, a few hundred milliseconds into any later
    // repaint-heavy stretch of gui_smoke - for exactly that reason.
    //
    // The reason used to be written as "paintEngine() returns nullptr, per
    // CLAUDE.md", which was true while the viewport was a WA_PaintOnScreen
    // widget owning a native GL surface. Since the QOpenGLWidget migration it
    // is not: the widget overrides no paint engine and Qt composites its
    // framebuffer normally. The CONCLUSION is unchanged - the source widget
    // still never goes through the offscreen raster path a QGraphicsEffect
    // needs - which is why this stays a QPainter::setOpacity() fade.
    void setOpacity(double opacity);
    double opacity() const { return myOpacity; }

    // Every string this widget can ever paint, so gui_smoke's banned-word
    // sweep has no blind spot here the way it has none for WalkthroughPanel
    // or HintBalloon. "Undo" is painted on the sibling control, never put on
    // a QAction or a tooltip, so nothing else makes it visible to that sweep.
    // Unlike those two widgets this one has no fixed set of strings to
    // enumerate up front - its messages are composed at the call site - so
    // it records every message it has been given this run and returns all of
    // them, not just whichever one happens to be live when the sweep runs.
    // HONEST LIMIT: a message string that was never actually shown during a
    // run is not covered by that sweep. Reaching every call site would mean
    // a second, hand-written copy of the app's messages here, which is
    // exactly the drift the sweep exists to catch; the suite instead earns
    // coverage by triggering the outcomes it cares about.
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
    bool myUndoEnabled = true;   // see setUndoEnabled()
    // Every distinct message this widget has been asked to show, in order,
    // for paintedTexts() - see the comment there for what it does and does
    // not cover.
    QStringList myShownTexts;
    QPointer<QWidget> myUndo;   // sibling, not a child - see class comment
    double myOpacity = 1.0;     // read by paintEvent() via painter.setOpacity()
    // Part of the same derived state syncUndoGeometry() reads myHasUndo and
    // isVisible() from - true for as long as a dismiss() is in flight
    // (through the whole fade, not just once it finishes), so a later sync
    // triggered by something else entirely (ToastHost::reposition() on a
    // viewport resize, which moves/resizes this widget and so re-runs
    // syncUndoGeometry() via moveEvent()/resizeEvent()) cannot re-show a
    // control dismiss() already closed the door on.
    bool myDismissing = false;
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
    //
    // A Kind::Note is DROPPED entirely while notesEnabled() is false - the
    // user has said they do not want to be told about the things that went
    // right. A Kind::Failure is shown regardless, and every caller may rely on
    // that: this app has no modal dialogs and no error log, so a toast is the
    // only place a refusal can appear, and a refusal that reports NOWHERE is a
    // silent failure - the one thing CLAUDE.md's kernel rules refuse to allow
    // anywhere else either.
    //
    // `documentStamp` is DocumentModel::revision() at the moment the message
    // was composed, or -1 for a message that does not describe a document
    // change at all. A toast that names one operation and offers Undo must
    // not outlive that operation: with a stamp recorded here,
    // documentMovedTo() below dismisses it the moment the document moves on
    // (a Ctrl+Z, another delete, anything), so the label can never describe
    // one checkpoint while the pill pops a different one.
    void show(const QString& text, Toast::Kind kind, bool undo, int documentStamp = -1);

    // Called by MainWindow on every documentChanged(). Dismisses a stamped
    // toast whose stamp no longer matches - see show() above.
    void documentMovedTo(int documentStamp);

    // Forwarded to the Toast - see Toast::setUndoEnabled().
    void setUndoEnabled(bool enabled);

    // View -> Show notifications, pushed here by MainWindow::updateActions()
    // the way setUndoEnabled() is, because that is the single place that
    // decides what is available. False silences Kind::Note ONLY; a
    // Kind::Failure is shown whatever this says. See show() for why that
    // asymmetry is a law rather than a preference.
    void setNotesEnabled(bool enabled);
    bool notesEnabled() const { return myNotesEnabled; }

    // Re-places (and re-raises) a live toast. Driven by
    // ViewportOverlay::laidOut(), so it runs AFTER the overlay has moved the
    // walkthrough guide this toast steps around rather than before it, and
    // by MainWindow::appStateChanged(), which is when a guide can appear
    // underneath an already-showing toast (Show tips again) - the case
    // HintBalloon::reconsider() already handled for itself and this class
    // did not.
    void replace();

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
    // Places the toast centred along the bottom edge, then steps it clear of
    // whatever else is anchored in that strip - the walkthrough guide on the
    // right, the Snap/Select chip cluster on the left. Both are z-above it
    // after any relayout(), so an overlap is not merely untidy: it is text
    // the user cannot read and a control they cannot reach.
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
    // DocumentModel::revision() as of the live message, or -1 when the
    // message does not describe a document change - see show().
    int myStamp = -1;
    // View -> Show notifications. Default true, which is what the app ships
    // with and what a window built before updateActions() first runs must
    // behave as.
    bool myNotesEnabled = true;
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
