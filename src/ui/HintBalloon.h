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
// reconsider() only runs when MainWindow::appStateChanged fires, so the two
// live predicates that are not driven by anything already wired to that
// signal need their own path in: switching selection mode now explicitly
// calls updateActions() (see MainWindow::onSelectionModeChanged), and camera
// changes are routed through onCameraChanged() below rather than through
// reconsider() itself - cameraChanged fires on every frame of an orbit or
// pan drag, and reconsider()'s full pass (selection queries, a findChild(),
// trig in AxisGizmo::labelText()) is not something to pay for at that rate.
// onCameraChanged() only pays for any of that when the view hint is actually
// the one currently up, which is rare - see the .cpp.
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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reconsider();
    void onCameraChanged();
    void showHint(const QString& event);
    void dismiss();
    void reposition();

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
