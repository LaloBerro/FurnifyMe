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
#include <QWidget>

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

    QString myText;
    Kind myKind = Kind::Note;
    bool myHasUndo = false;
    QPointer<QWidget> myUndo;   // sibling, not a child - see class comment
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

signals:
    void undoRequested();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void reposition();
    void dismiss();

    OcctViewWidget* myViewport = nullptr;
    Toast* myToast = nullptr;
    QTimer* myTimer = nullptr;
};
