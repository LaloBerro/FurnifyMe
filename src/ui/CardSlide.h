#pragma once
// A floating card arrives and leaves by sliding off its own edge instead of
// popping in and out of existence. One of these owns exactly one card, and
// nothing else may show or hide that card while it does.
//
// The rule it has to respect is CLAUDE.md's: a drawer's visibility is DERIVED
// from its QAction on every appStateChanged, never set once at the toggle.
// setShown() is therefore idempotent and is safe to call on every state
// change - it compares against the state it was last ASKED for, not against
// the card's live isVisible(), which is true for the whole of a slide-out.
//
// Three things the flight must not break, each of which is why this is a
// class rather than a QPropertyAnimation at each call site:
//
//  - ViewportOverlay::relayout() has the final say on where an anchored card
//    sits. A resize landing mid-flight has already moved the card to its new
//    home, so the flight is RE-AIMED at that home rather than tweening on
//    toward a position that no longer exists. Re-aimed and not stopped: the
//    card is in the air, and stopping drops it where the tween had got to,
//    which for a card on its way in is off the edge of the viewport. A
//    toggle handler calling relayout() right after opening a drawer is the
//    ordinary case, not the exotic one.
//  - An animation is parented and KeepWhenStopped. DeleteWhenStopped deletes
//    the animation on natural completion too, which dangles the pointer this
//    object keeps (the Toast fade's own finding, CLAUDE.md).
//  - With animations off (gui_smoke sets OcctViewWidget::setAnimationsEnabled
//    (false)) there is NO animation at all: setShown() is a plain setVisible()
//    that has already happened by the time it returns, the way
//    ToastHost::fadeTo() and OcctViewWidget::animateTo() already treat a
//    disabled animation. The suite's drawer checks stay synchronous.
#include <QObject>
#include <QPoint>
#include <QPointer>

class OcctViewWidget;
class QPropertyAnimation;
class QWidget;
class ViewportOverlay;

class CardSlide : public QObject {
    Q_OBJECT

public:
    // Which edge the card leaves through - a card must leave the way it came
    // in, and the way it came in is the edge it is anchored to. Only the rail
    // uses this today (Left): the drawers were animated too and the user
    // asked for that taken off, so Right is here for the anchor's sake and
    // has no caller.
    enum class From { Left, Right };

    CardSlide(QWidget* card, From from, OcctViewWidget* viewport,
              ViewportOverlay* overlay, QObject* parent = nullptr);

    // The ONE route by which this card is shown or hidden. Idempotent: a
    // repeat of the state last asked for does nothing, so the derived
    // appStateChanged block can call it on every state change.
    void setShown(bool shown);

    // What was last asked for - NOT the card's live visibility, which lags
    // by one flight on the way out.
    bool shown() const { return myShown; }

private:
    // Re-aims a live flight at wherever relayout() has just put the card.
    void settle();

    QPointer<QWidget> myCard;
    QPointer<OcctViewWidget> myViewport;
    QPointer<ViewportOverlay> myOverlay;
    From myFrom = From::Left;
    // One animation for this card's whole lifetime, parented to this object.
    QPropertyAnimation* mySlide = nullptr;
    // True while the running flight is a slide-OUT, so settle() and the
    // finished handler know whether to hide at the end of it.
    bool myLeaving = false;
    bool myShown = false;
};
