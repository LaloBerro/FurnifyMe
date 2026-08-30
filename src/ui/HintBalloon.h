#pragma once
//
// A small balloon that teaches one capability the first time it becomes
// available, and never again once the user has done it three times. It owns
// both the widget and the policy: which hint is due is decided here, from
// UserProgress plus live application state.
//
// A shown hint is dismissed by exactly three, equally valid triggers: the
// user clicks it away ("got it"), its event reaches the learned threshold, or
// the situation that made it relevant stops holding - the user actually did
// the thing it was teaching. All three are handled through one mechanism:
// conditionHolds() is the single live predicate per hint, and reconsider()
// re-evaluates it - not just the learned threshold - against whatever is
// currently on screen, every time it runs.
//
// reconsider() only runs when MainWindow::appStateChanged fires, so a live
// predicate driven by nothing already wired to that signal would linger no
// matter how correct it is: switching selection mode explicitly calls
// updateActions() (see MainWindow::onSelectionModeChanged), and every route
// that changes the camera to a named direction - the View menu and a click
// on the axis gizmo alike - goes through MainWindow::recordViewChanged(),
// which records the event and calls updateActions() too.
//
// That last one is why no predicate here reads the camera. The view hint
// used to retire on the view-name label != "Persp" - a string that lived on
// AxisGizmo then and is OcctViewWidget::viewLabelText() now - which disagreed
// with the event it is governed by in both directions: pressing 0 records
// view.changed but leaves the camera at a pose that label calls "Persp", and
// nothing about a free orbit records anything. All three hints now retire by
// the same single rule - their event, not the state that happens to
// accompany it.
//
#include <QString>
#include <QWidget>

#include <set>

class MainWindow;

class HintBalloon : public QWidget {
    Q_OBJECT

public:
    HintBalloon(MainWindow* window, QWidget* parent);

    // The text currently shown, or empty when no hint is up.
    QString currentHint() const { return myText; }

    // Every string this balloon paints, so gui_smoke's banned-word sweep can
    // reach it - the sweep only walks action text and widget tooltips
    // otherwise, and this widget paints its own copy rather than exposing it
    // through either of those. Sourced from textForEvent(), the same
    // function paintEvent()'s myText is populated from via showHint(), so
    // there is exactly one copy of each hint's wording in the whole class -
    // never a second one that only the sweep sees.
    QStringList paintedTexts() const;

    // Re-places (and re-raises) whatever hint is currently up, without
    // changing which one it is. Public so ToastHost can nudge a balloon that
    // is already visible when a toast appears (or moves) underneath it -
    // reconsider() is what normally drives this, but it only runs on
    // appStateChanged, which a toast showing does not by itself emit - and
    // so MainWindow can drive it from ViewportOverlay::laidOut(), which is
    // the only moment the guide this balloon steps around is guaranteed to
    // be at its final rectangle. A no-op when nothing is up.
    void reposition();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void reconsider();
    // Help -> Show tips again. Clearing the store is not enough on its own:
    // myShownThisSession would still hold every hint already seen, so none of
    // them could come back until a restart - the guide would return and the
    // hints would not, which is exactly what that menu entry promises not to
    // do. MainWindow announces the reset and this decides what it costs;
    // MainWindow never touches this set.
    void onProgressReset();
    void showHint(const QString& event);
    void dismiss();

    // The live predicate behind an event's hint: is the teaching moment
    // still relevant right now? Read twice per reconsider() call - once
    // against whatever hint is currently up (to decide whether it should be
    // dismissed) and once per candidate event (to decide whether a new one
    // should be raised) - so there is exactly one definition of "due" per
    // hint, not a separate notion for showing versus keeping shown.
    bool conditionHolds(const QString& event) const;

    // The one copy of a hint's wording, keyed by its event name.
    QString textForEvent(const QString& event) const;

    MainWindow* myWindow = nullptr;
    QString myText;
    QString myEvent;     // the progress event this hint teaches
    // Per-hint, not global: a single flag would mean the first balloon shown
    // silenced the other two for the rest of the session.
    std::set<QString> myShownThisSession;
};
