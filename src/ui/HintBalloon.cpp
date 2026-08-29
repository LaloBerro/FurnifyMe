#include "HintBalloon.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "Toast.h"
#include "UserProgress.h"
#include "WalkthroughPanel.h"

#include <algorithm>

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 250;
constexpr int kClearance = 8;   // gap left when stepping around the guide

const QString kBooleanEvent = QStringLiteral("boolean.completed");
const QString kFaceModeEvent = QStringLiteral("faceMode.used");
const QString kViewChangedEvent = QStringLiteral("view.changed");
}  // namespace

HintBalloon::HintBalloon(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // Closes the whole class of "unhandled mouse event bubbles to the
    // viewport behind this balloon" bugs in one line, rather than overriding
    // press, release, wheel, and whatever else individually: without it, any
    // mouse event this widget does not explicitly accept - and QWidget's
    // defaults leave plenty unaccepted, release included - propagates to the
    // parent. That parent is the viewport, whose own mouseReleaseEvent()
    // performs a real pick and unconditionally emits selectionChanged() on
    // every left-button release; a "got it" click was quietly re-firing
    // selection handling underneath the balloon and could raise the next due
    // hint in the same gesture. A click-to-dismiss balloon has no reason to
    // let any mouse event reach whatever is behind it, so this closes the
    // whole event class rather than enumerating members of it as bugs turn up.
    setAttribute(Qt::WA_NoMousePropagation);
    hide();
    connect(myWindow, &MainWindow::appStateChanged, this, &HintBalloon::reconsider);
    // No connection to cameraChanged any more: no predicate here reads the
    // camera, so there is nothing an orbit frame could change. See the
    // header for why the view hint retires on its event instead.
    connect(myWindow, &MainWindow::progressReset, this, &HintBalloon::onProgressReset);
    // Repositioning only happened inside showHint(), so a window resize while
    // a hint was up left it stranded wherever the viewport used to end - see
    // eventFilter() below.
    if (parent) parent->installEventFilter(this);
}

bool HintBalloon::conditionHolds(const QString& event) const
{
    // Each predicate answers "is this still the teaching moment?", not
    // merely "has the underlying capability existed at some point" - that
    // distinction is what lets reconsider() reuse the exact same check both
    // to raise a hint and to retire one already up.
    if (event == kBooleanEvent) {
        // True for as long as - and only as long as - exactly two bodies are
        // selected. Performing the operation changes the selection (down to
        // one result), and so does simply changing the selection by hand;
        // either one makes this false and the hint go away.
        return myWindow->view()->selectedSolidIds().size() == 2;
    }
    if (event == kFaceModeEvent) {
        // True while a body exists and the user has not yet tried face
        // selection. The moment they do, this goes false - the hint has done
        // its job.
        return myWindow->document().count() > 0 &&
               myWindow->view()->selectionMode() != OcctViewWidget::SelectionMode::Face;
    }
    if (event == kViewChangedEvent) {
        // True while a body exists and the user has not yet looked from a
        // named direction. Reading the recorded event rather than the camera
        // pose is what keeps this hint's retirement identical in kind to the
        // other two: whatever route records view.changed - the View menu, a
        // 0-3 key, a click on the gizmo - retires the hint by the same rule,
        // and there is no camera pose that can disagree with the event. The
        // pose-based version could not: pressing 0 records the event but
        // leaves the camera at azimuth -45 / elevation 30, which the gizmo
        // labels "Persp", so the balloon sat there after the user had done
        // exactly what it taught.
        return myWindow->document().count() > 0 &&
               myWindow->progress().count(event.toStdString()) == 0;
    }
    return false;
}

QString HintBalloon::textForEvent(const QString& event) const
{
    if (event == kBooleanEvent) {
        return tr("Two bodies selected — Union combines them, Subtract cuts the second "
                  "out of the first, Intersect keeps only the overlap.");
    }
    if (event == kFaceModeEvent) {
        return tr("Switch to Select Faces to pick one face at a time instead of a "
                  "whole body.");
    }
    if (event == kViewChangedEvent) {
        return tr("Click an arm of the gizmo, top right, to look from that direction. "
                  "Keys 0 to 3 do the same.");
    }
    return QString();
}

void HintBalloon::reconsider()
{
    const UserProgress& progress = myWindow->progress();

    // A shown hint is dismissed by any one of three equally valid triggers:
    // a click ("got it", handled in mousePressEvent), its event reaching the
    // learned threshold, or - the case that used to be missing - the
    // situation that raised it no longer holding. All three retire the same
    // way, through dismiss(); this is the one spot that checks the latter
    // two on every call, so a hint can never sit on screen after its own
    // reason for existing has gone away.
    if (!myText.isEmpty() &&
        (progress.hasLearned(myEvent.toStdString()) || !conditionHolds(myEvent))) {
        dismiss();
    }

    auto isDue = [&](const QString& event) {
        return conditionHolds(event) && !progress.hasLearned(event.toStdString()) &&
               !myShownThisSession.count(event);
    };

    // The boolean hint is the most specific and time-limited of the three -
    // its condition is true for a moment, not for the rest of the session
    // the way "a body exists" is. That is why it alone is allowed to replace
    // whatever lower-priority hint is currently up: the other two would
    // otherwise latch onto the very first body ever made and sit there,
    // unacknowledged, blocking this one from ever being seen. isDue() already
    // guarantees it is never already the one showing - showHint() marks an
    // event shown the instant it appears, so isDue() for the event already
    // on screen is always false.
    if (isDue(kBooleanEvent)) {
        showHint(kBooleanEvent);
        return;
    }

    if (!myText.isEmpty()) {
        // Do not interrupt a hint already up - but do re-place it. The
        // walkthrough panel shares this viewport and can appear underneath a
        // balloon that is already showing (Show tips again restores it), and
        // appStateChanged is exactly when that happens.
        reposition();
        return;
    }

    if (isDue(kFaceModeEvent)) {
        showHint(kFaceModeEvent);
        return;
    }

    if (isDue(kViewChangedEvent)) {
        showHint(kViewChangedEvent);
    }
}

void HintBalloon::onProgressReset()
{
    myShownThisSession.clear();
    // Except the one actually on screen: it has had its turn and is still
    // having it, and leaving it out of the set would let the reconsider()
    // that follows this "raise" it a second time on top of itself.
    if (!myEvent.isEmpty()) myShownThisSession.insert(myEvent);
}

void HintBalloon::reposition()
{
    if (myText.isEmpty() || !parentWidget()) return;

    // Measured with the same font paintEvent() draws the message in.
    const QFontMetrics metrics(Theme::bodyFont());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, kWidth - kPad * 2, 1000),
                                              Qt::TextWordWrap, myText);
    resize(kWidth, bounds.height() + kPad * 2 + 22);

    int x = (parentWidget()->width() - width()) / 2;
    int y = parentWidget()->height() - height() - 90;

    // The walkthrough guide occupies the bottom-right corner of this same
    // viewport, and below roughly 800 px of viewport width the centred
    // balloon runs straight into it. That is reachable in practice, not a
    // theoretical narrow-window case: Show tips again restores the guide
    // while a hint is up. Overlap would be worse than it looks, because
    // ViewportOverlay::relayout() raises the guide back above the balloon
    // while the balloon is still the click target underneath it. So step
    // aside - to the left of an obstacle when that fits, above it when it
    // does not. A toast lands in the same bottom strip whenever an outcome
    // is reported while a hint is already up, so it steps aside by the same
    // rule rather than a second mechanism invented just for it.
    auto stepAside = [&](const QRect& obstacle) {
        if (!QRect(x, y, width(), height()).intersects(obstacle)) return;
        const int beside = obstacle.left() - kClearance - width();
        if (beside >= kClearance) {
            x = beside;
        } else {
            // Never above the top edge: on a viewport too short for both,
            // a balloon nudged off-screen teaches nobody anything.
            y = std::max(0, obstacle.top() - kClearance - height());
        }
    };

    const WalkthroughPanel* guide = parentWidget()->findChild<WalkthroughPanel*>();
    if (guide && guide->isVisible()) stepAside(guide->geometry());

    const Toast* toast = parentWidget()->findChild<Toast*>();
    if (toast && toast->isVisible()) stepAside(toast->geometry());

    move(x, y);
}

void HintBalloon::showHint(const QString& event)
{
    myEvent = event;
    myText = textForEvent(event);
    myShownThisSession.insert(event);

    reposition();
    show();
    raise();
    update();
}

void HintBalloon::dismiss()
{
    myText.clear();
    myEvent.clear();
    hide();
}

void HintBalloon::mousePressEvent(QMouseEvent* /*event*/)
{
    // WA_NoMousePropagation (see the constructor) is what stops this and
    // every other mouse event from reaching the viewport behind the balloon;
    // nothing here needs to touch accept()/ignore() to make that true.
    dismiss();
}

bool HintBalloon::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void HintBalloon::paintEvent(QPaintEvent* /*event*/)
{
    if (myText.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8.0, 8.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::accent(), 1.0));
    painter.drawPath(panel);

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, kPad, width() - kPad * 2, height() - kPad * 2 - 20),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, myText);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::accent());
    painter.drawText(QRect(kPad, height() - 26, width() - kPad * 2, 20),
                     Qt::AlignRight | Qt::AlignVCenter, tr("got it"));
}

QStringList HintBalloon::paintedTexts() const
{
    // The full set this widget can ever paint - not just myText, which is
    // whatever happens to be up right now - so gui_smoke's banned-word sweep
    // has no blind spot here, the same way WalkthroughPanel::paintedTexts()
    // covers its own copy. Every string below is read from textForEvent(),
    // the same source paintEvent() ends up drawing through myText, so
    // editing a hint's wording can never leave a stale copy that only this
    // sweep checks.
    return {
        textForEvent(kBooleanEvent),
        textForEvent(kFaceModeEvent),
        textForEvent(kViewChangedEvent),
        tr("got it"),
    };
}
