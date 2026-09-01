#include "HintBalloon.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "Toast.h"
#include "UserProgress.h"
#include "ViewportOverlay.h"

#include <algorithm>
#include <vector>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 250;
constexpr int kClearance = 8;   // gap left when stepping around an obstacle

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
    // No filter on the viewport's resize any more. It ran BEFORE
    // ViewportOverlay had moved the walkthrough guide this balloon steps
    // around - Qt runs event filters last-installed-first and the overlay
    // installs its own first - so a shrink placed the balloon against the
    // guide's pre-resize rectangle and the guide then landed on top of it.
    // MainWindow drives reposition() from ViewportOverlay::laidOut()
    // instead, which is by construction after every anchored widget is at
    // its final rectangle. See that signal's comment.
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
        // Amended for the rail: the chip this used to name painted its own
        // label ("Select Faces") right on the viewport. The rail is
        // icon-only - the label moved into the chip's tooltip - so the old
        // wording pointed at text that appears nowhere on screen. The fix is
        // to locate the control instead of merely naming it.
        return tr("Switch to Select Faces — on the rail at the left edge — to "
                  "pick one face at a time instead of a whole body.");
    }
    if (event == kViewChangedEvent) {
        return tr("Click an arm of the gizmo, top right, to look from that direction. "
                  "Keys 0 to 3 do the same.");
    }
    return QString();
}

void HintBalloon::reconsider()
{
    // The init screen's own gate (Milestone 3, item 2), on the same terms
    // as WalkthroughPanel's - see that class for the fuller reasoning. Every
    // one of the three conditions below already requires document().count()
    // > 0 or a live selection, both of which showInitScreen() forces to
    // nothing, so this has never actually been reachable in practice; it is
    // still made explicit rather than left to that coincidence, because a
    // future hint with no document requirement would otherwise be free to
    // pop up over the gallery with nothing here to stop it. dismiss() rather
    // than a bare hide(): a hint the gallery caught mid-display must not be
    // left remembering state a real dismissal would have cleared.
    if (myWindow->isShowingInitScreen()) {
        dismiss();
        return;
    }

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
    // Grown by Theme::surfaceShadowMargin() per side beyond the content size
    // computed above. That margin is zero - the family paints no shadow and
    // reserves no room for one (see Theme.h) - so this balloon's widget rect
    // and its painted card are the same rectangle, and paintEvent() applies
    // the same zero on the inside. No sibling control depends on this
    // widget's geometry, unlike
    // WalkthroughPanel's skip pill or Toast's Undo pill, so there is nothing
    // else here to keep in step.
    const int margin = Theme::surfaceShadowMargin();
    // Through Theme::wholeDevicePixels() - see Theme.h. This card sizes
    // itself from MEASURED TEXT, so its height is as arbitrary a number as
    // this app produces, and the position snap below only makes the near edge
    // whole: a whole origin with a fractional extent still lands the far edge
    // between device rows.
    resize(Theme::wholeDevicePixels(
        QSize(kWidth + margin * 2, bounds.height() + kPad * 2 + 22 + margin * 2)));

    int x = (parentWidget()->width() - width()) / 2;
    int y = parentWidget()->height() - height() - 90;

    // Everything the overlay has anchored - the walkthrough guide bottom
    // right, the tool rail down the whole left edge, the axis gizmo top
    // right - asked for as ViewportOverlay::occupiedRects() rather than by
    // naming the widget classes this file happens to know about. It used to
    // name WalkthroughPanel by type, which meant it could not see the rail
    // at all: ToastHost was already asking the overlay, and the two now
    // avoid the same set of obstacles by the same route, so a surface added
    // later is stepped around for free instead of becoming the next
    // collision to discover.
    std::vector<QRect> obstacles;
    if (const ViewportOverlay* overlay = parentWidget()->findChild<ViewportOverlay*>())
        obstacles = overlay->occupiedRects();

    // Anything pinned to the LEFT half raises a floor this balloon may not
    // cross, exactly as ToastHost's band solver treats one. Stepping left is
    // the wrong move for a left-edge obstacle and there is now a permanent
    // one: the rail. Without the floor, "step left of the guide" put the
    // balloon at x=8 - underneath the rail - at viewport widths around
    // 585-640, which is reachable the moment Show tips again restores the
    // guide on a narrow window.
    //
    // Only obstacles sharing this balloon's own horizontal BAND raise it, the
    // same rule ToastHost's solver applies. Without that clause every
    // left-hand card is treated as a full-height wall, and the items drawer
    // is not one: it is a top-left card, the balloon lives at the bottom, and
    // on any viewport tall enough for the two never to meet it was still
    // shoving the balloon out to the drawer's right edge - far enough, on a
    // narrow window, to hang it off the right of the viewport. The rail is a
    // genuine spine and shares every band there is, so it is unaffected;
    // that is the difference the band test is there to draw.
    auto floorForBand = [&](int bandTop) {
        int floorX = 0;
        for (const QRect& obstacle : obstacles) {
            if (obstacle.bottom() < bandTop || obstacle.top() > bandTop + height() - 1)
                continue;
            if (obstacle.center().x() < parentWidget()->width() / 2)
                floorX = std::max(floorX, obstacle.right() + 1 + kClearance);
        }
        return floorX;
    };

    int leftFloor = floorForBand(y);
    x = std::max(x, leftFloor);

    // Overlap with what is left would be worse than it looks, because
    // ViewportOverlay::relayout() raises every anchored widget back above
    // this one while the balloon is still the click target underneath. So
    // step aside - to the left of an obstacle when that fits without
    // crossing the floor, above it when it does not.
    auto stepAside = [&](const QRect& obstacle) {
        if (!QRect(x, y, width(), height()).intersects(obstacle)) return;
        const int beside = obstacle.left() - kClearance - width();
        if (beside >= kClearance && beside >= leftFloor) {
            x = beside;
        } else {
            // Never above the top edge: on a viewport too short for both,
            // a balloon nudged off-screen teaches nobody anything.
            y = std::max(0, obstacle.top() - kClearance - height());
            // The floor is a property of the band, so moving bands re-asks
            // for it: the row this balloon has just climbed into can be
            // occupied on the left by things the row below was not.
            leftFloor = floorForBand(y);
            x = std::max(x, leftFloor);
        }
    };

    for (const QRect& obstacle : obstacles) {
        // Left-half obstacles are the floor's business, not the step's -
        // stepping "to the left of" the rail is off the viewport.
        if (obstacle.center().x() < parentWidget()->width() / 2) continue;
        stepAside(obstacle);
    }

    // The toast places itself and is not an overlay entry, so it is still
    // named here - it lands in this same bottom strip whenever an outcome is
    // reported while a hint is up.
    const Toast* toast = parentWidget()->findChild<Toast*>();
    if (toast && toast->isVisible()) stepAside(toast->geometry());

    // Never off either edge, whatever the floor and the steps above worked
    // out between them - but clamped against the FLOOR, not against zero.
    // Clamping to zero ran after the floor and could undo it: once the
    // viewport is narrower than leftFloor + width(), the right-edge limit
    // goes below the floor, the min() picks it, and the max(0, ...) parked
    // the balloon at x=0 - underneath the rail, which relayout() then raises
    // back on top of it. That is reachable below about 322px of viewport
    // width with only the rail in the way, and much sooner than that with the
    // items drawer open, since the drawer's right edge is the floor then.
    // When the two genuinely cannot both be satisfied the floor wins: a
    // balloon whose right end runs past the viewport edge is still readable
    // and still clickable, while one under the rail is neither.
    const int rightLimit = std::max(leftFloor, parentWidget()->width() - width());
    x = std::max(leftFloor, std::min(x, rightLimit));


    // Whole DEVICE pixels, in the window's own coordinates - the position half
    // of Theme's rule. Snapped last, after every avoidance clamp above, and
    // always downward, so it cannot push the balloon back over an obstacle
    // the clamps just moved it off - and never below the left floor, which is
    // the one clamp that must survive (see the paragraph above).
    {
        const QPoint origin = parentWidget()->mapTo(window(), QPoint(0, 0));
        const double dpr = devicePixelRatioF();
        x = std::max(leftFloor, Theme::snapToDevicePixels(x, origin.x(), dpr));
        y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    }

    move(x, y);
    // Re-raised here as well as re-placed: this runs from
    // ViewportOverlay::laidOut(), immediately after the overlay has raise()d
    // every anchored widget - including the guide - back above this one.
    if (isVisible()) raise();
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

void HintBalloon::paintEvent(QPaintEvent* /*event*/)
{
    if (myText.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // `body` is the visible card, inset from this widget's own bounds by
    // Theme::surfaceShadowMargin() - see reposition() for the growth.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top() + kPad, body.width() - kPad * 2,
                           body.height() - kPad * 2 - 20),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, myText);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::accent());
    painter.drawText(QRect(body.left() + kPad, height() - margin - 26, body.width() - kPad * 2, 20),
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
