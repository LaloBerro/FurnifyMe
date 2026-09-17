#include "CardSlide.h"

#include "OcctViewWidget.h"
#include "Theme.h"
#include "ViewportOverlay.h"

#include <QPropertyAnimation>
#include <QWidget>

namespace {
// A card leaves through its own edge, so the off-screen position is its own
// width past that edge - never a fixed number of pixels, which would leave a
// wide drawer's far half still on screen.
QPoint parkedPosition(const QWidget* card, CardSlide::From from, const QPoint& home)
{
    switch (from) {
        case CardSlide::From::Left:
            return QPoint(-card->width(), home.y());
        case CardSlide::From::Top:
            // Its own HEIGHT above the top edge, and the home x kept: a card
            // leaving upward must not also drift sideways, and the app bar's
            // x is the same kEdgeMargin the rail below it uses, so a drift
            // would be visible against the rail for the whole flight.
            return QPoint(home.x(), -card->height());
        case CardSlide::From::Right:
            break;
    }
    return QPoint(card->parentWidget() ? card->parentWidget()->width()
                                       : home.x() + card->width(),
                  home.y());
}
}  // namespace

CardSlide::CardSlide(QWidget* card, From from, OcctViewWidget* viewport,
                     ViewportOverlay* overlay, QObject* parent)
    : QObject(parent ? parent : card), myCard(card), myViewport(viewport),
      myOverlay(overlay), myFrom(from)
{
    myShown = card && card->isVisible();

    mySlide = new QPropertyAnimation(card, "pos", this);
    mySlide->setDuration(Theme::motionMs());
    mySlide->setEasingCurve(Theme::motionCurve());
    connect(mySlide, &QPropertyAnimation::finished, this, [this] {
        if (myLeaving && myCard) myCard->hide();
        myLeaving = false;
    });

    // relayout() wins, always - see the header. Connected to the overlay
    // rather than watching the card's own move events, because a move this
    // object made is not a relayout and the two would be indistinguishable
    // from inside a moveEvent.
    if (overlay)
        connect(overlay, &ViewportOverlay::laidOut, this, [this] { settle(); });
}

void CardSlide::setShown(bool shown)
{
    if (!myCard) return;
    // Idempotent against what was ASKED for. isVisible() cannot answer this:
    // it is still true for the whole of a slide-out, so comparing against it
    // would restart the flight on every appStateChanged.
    if (shown == myShown && myCard->isVisible() == (shown || myLeaving)) return;
    myShown = shown;

    const bool animate = myViewport && myViewport->animationsEnabled();
    if (!animate) {
        // Synchronous, exactly as before this class existed - the suite's
        // drawer checks read the answer the instant setShown() returns.
        mySlide->stop();
        myLeaving = false;
        myCard->setVisible(shown);
        return;
    }

    mySlide->stop();

    if (shown) {
        myLeaving = false;
        myCard->setVisible(true);
        // The overlay is the only thing that knows where this card belongs,
        // and a card being shown for the first time has never been placed at
        // all. Ask it, THEN read the home position off the card - deriving a
        // destination here instead would be a second copy of relayout()'s
        // arithmetic.
        if (myOverlay) myOverlay->relayout();
        const QPoint home = myCard->pos();
        mySlide->setStartValue(parkedPosition(myCard, myFrom, home));
        mySlide->setEndValue(home);
        myCard->move(mySlide->startValue().toPoint());
        mySlide->start(QAbstractAnimation::KeepWhenStopped);
        myCard->raise();
        return;
    }

    if (!myCard->isVisible()) return;  // nothing to fly out
    myLeaving = true;
    const QPoint home = myCard->pos();
    mySlide->setStartValue(home);
    mySlide->setEndValue(parkedPosition(myCard, myFrom, home));
    mySlide->start(QAbstractAnimation::KeepWhenStopped);
}

void CardSlide::settle()
{
    if (!myCard || mySlide->state() != QAbstractAnimation::Running) return;

    // relayout() has just moved the card to its real home, and the flight in
    // the air was aimed at the OLD one. Re-aim rather than stop: the card is
    // mid-flight, and stopping would drop it wherever the tween had got to -
    // which is off the edge for a card on its way in. The destination is
    // relayout()'s, always; only the path to it is this object's.
    const QPoint home = myCard->pos();
    const QPoint current = mySlide->currentValue().toPoint();
    mySlide->stop();
    myCard->move(current);
    mySlide->setStartValue(current);
    mySlide->setEndValue(myLeaving ? parkedPosition(myCard, myFrom, home) : home);
    mySlide->start(QAbstractAnimation::KeepWhenStopped);
}
