#include "Toast.h"

#include "HintBalloon.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "ViewportOverlay.h"

#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>
#include <QRect>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int kPad = 14;
constexpr int kWidth = 360;
constexpr int kUndoWidth = 64;
constexpr int kUndoHeight = 26;
constexpr int kBottomMargin = 24;
constexpr int kClearance = 8;   // gap left when stepping around the guide

constexpr int kNoteMs = 4000;
constexpr int kFailureMs = 8000;

// The one interactive spot on an otherwise click-through toast. Follows
// WalkthroughPanel's SkipControl exactly (see WalkthroughPanel.cpp for the
// full reasoning, verified there against this machine's Qt 6.11.1): this is
// a SIBLING of Toast, parented to the same viewport, never a child - a child
// would be just as unreachable by a real click as the transparent toast body
// itself, since Qt::WA_TransparentForMouseEvents excludes a widget's entire
// subtree from hit-testing, not just the widget carrying it.
//
// Paints nothing of its own, exactly like SkipControl - Toast::paintEvent()
// draws the pill and the "Undo" label at this control's own rect (undoRect()),
// so there is exactly one place that string is spelled out (see
// Toast::undoLabel()) rather than one copy the sweep checks and a second one
// that is actually on screen.
class UndoControl : public QWidget {
public:
    UndoControl(std::function<void()> onClick, QWidget* parent)
        : QWidget(parent)
        , myOnClick(std::move(onClick))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        // Closes the same class of bug documented on HintBalloon: an
        // unhandled release would otherwise propagate to the viewport behind
        // this control and trigger a real pick underneath the toast.
        setAttribute(Qt::WA_NoMousePropagation);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        event->accept();
        if (myOnClick) myOnClick();
    }

private:
    std::function<void()> myOnClick;
};

}  // namespace

Toast::Toast(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // The body must never eat a click meant for the model behind it - only
    // the sibling Undo control (see UndoControl above) is ever clickable.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    myUndo = new UndoControl([this] { emit undoClicked(); }, parent);
    myUndo->hide();
    hide();

    // Milestone 5 item 2: this card's own corners over the GL surface, at
    // the same radius paintEvent() paints its card with. myUndo is NOT
    // masked - see UndoControl's own class comment: it paints nothing of its
    // own, so there is no card on it to round, and its 160ms dismiss fade
    // (Toast::paintEvent()'s ruled opacity exception) is unaffected either
    // way - a mask clips WHICH pixels show, never how opaque the ones inside
    // it are.
    Theme::installCardMask(this, 8);
}

Toast::~Toast()
{
    // Sibling, not a child - Qt's parent-child cascade does not clean it up
    // when this widget alone is destroyed, and it holds a raw `this` via its
    // click callback. QPointer makes the delete a safe no-op if the two are
    // instead torn down together by their shared parent, in either order -
    // see WalkthroughPanel::~WalkthroughPanel() for the same reasoning.
    delete myUndo;
}

void Toast::setMessage(const QString& text, Kind kind, bool undo)
{
    myText = text;
    myKind = kind;
    myHasUndo = undo;
    // Recorded for paintedTexts(), deduped so a repeated outcome does not
    // grow the list without bound.
    if (!text.isEmpty() && !myShownTexts.contains(text)) myShownTexts << text;
    // A fresh message supersedes whatever dismissal, if any, was still in
    // flight - see setDismissing() and the class comment on myDismissing.
    myDismissing = false;
    syncUndoGeometry();
    update();
}

void Toast::setDismissing(bool dismissing)
{
    myDismissing = dismissing;
    syncUndoGeometry();
}

void Toast::setUndoEnabled(bool enabled)
{
    if (myUndoEnabled == enabled) return;
    myUndoEnabled = enabled;
    // Same derived-visibility predicate as myDismissing, for the same
    // reason: a one-shot hide() here would be undone by the next
    // syncUndoGeometry() a resize or a move triggers.
    syncUndoGeometry();
    update();   // the pill is painted dimmed while the route is closed
}

QSize Toast::sizeHint() const
{
    const int textWidth = kWidth - kPad * 2 - (myHasUndo ? kUndoWidth + kPad : 0);
    // Measured with the same font paintEvent() draws the message in.
    const QFontMetrics metrics(Theme::bodyFont());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, std::max(textWidth, 1), 1000),
                                              Qt::TextWordWrap, myText);
    const int minHeight = myHasUndo ? kUndoHeight + kPad * 2 : 0;
    // Grown by Theme::surfaceShadowMargin() per side beyond the content size
    // computed above. That margin is zero - the family paints no shadow and
    // reserves no room for one (see Theme.h) - so this card's widget rect and
    // its painted card are the same rectangle; paintEvent() and undoRect()
    // apply the same zero on the inside. Kept as arithmetic so every member
    // of the family still reads as one scheme. ToastHost::reposition()
    // resizes this widget straight from this return value, the same way
    // WalkthroughPanel's constructor uses its own sizeHint() directly.
    const int margin = Theme::surfaceShadowMargin();
    return QSize(kWidth + margin * 2, std::max(bounds.height() + kPad * 2, minHeight) + margin * 2);
}

QRect Toast::undoRect() const
{
    // Local coordinates within this (now grown) widget - kPad from the
    // visible card's right edge and vertically centred within the card, not
    // flush against this widget's own outer bounds. syncUndoGeometry() below
    // still just translates this by pos(), unchanged - the margin lives
    // entirely in this one formula, the same pattern as
    // WalkthroughPanel::skipRect().
    const int margin = Theme::surfaceShadowMargin();
    return QRect(width() - margin - kPad - kUndoWidth,
                margin + (height() - margin * 2 - kUndoHeight) / 2,
                kUndoWidth, kUndoHeight);
}

void Toast::syncUndoGeometry()
{
    if (!myUndo) return;
    // mySkip's counterpart: shares this widget's parent, so undoRect() -
    // defined in this widget's own local coordinates - needs translating by
    // pos() to land in that shared coordinate space.
    myUndo->setGeometry(undoRect().translated(pos()));
    // Visibility is DERIVED here, not left to a hide event that may never
    // arrive - see WalkthroughPanel::syncSkipGeometry() for why that matters:
    // hide() on a widget that was never shown delivers no QHideEvent at all.
    // myDismissing is part of the same predicate for the same reason: this
    // widget stays isVisible() == true for the whole fade a dismiss() kicks
    // off, and this function reruns on every viewport resize for as long as
    // that is true (see ToastHost::reposition()), so a one-shot hide() at
    // the top of dismiss() alone is not enough - the next resize would
    // re-derive visibility from isVisible() && myHasUndo and re-show it.
    myUndo->setVisible(isVisible() && myHasUndo && !myDismissing && myUndoEnabled);
    myUndo->raise();
}

void Toast::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncUndoGeometry();
}

void Toast::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myUndo) myUndo->hide();
}

void Toast::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncUndoGeometry();
}

void Toast::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncUndoGeometry();
}

void Toast::setOpacity(double opacity)
{
    myOpacity = opacity;
    update();
}

void Toast::paintEvent(QPaintEvent* /*event*/)
{
    if (myText.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The fade - see the header comment on setOpacity() for why this is a
    // plain QPainter opacity rather than a QGraphicsEffect. Applies to
    // everything drawn below with this same QPainter: the panel, the
    // message, and the Undo pill alike.
    //
    // This is the SINGLE ruled exception to "no widget paints a translucent
    // pixel over the GL surface" (CLAUDE.md's opaque-family section) - every
    // other floating card is fully opaque, always. It survives review
    // because it is not a static translucent surface sitting over the
    // viewport, which is the case the project's own probe found unreliable:
    // it is a 160 ms, Theme::motionMs()-driven transition that starts and
    // ends fully opaque, so the window during which any blending is visible
    // is transient rather than a resting state. It is also invisible to the
    // suite's opacity/colour sweeps, which is exactly why it needed calling
    // out here rather than being caught by them: every gui_smoke probe calls
    // OcctViewWidget::setAnimationsEnabled(false), which makes
    // ToastHost::fadeTo() skip straight to the end value instead of animating
    // through it, so a probe never observes myOpacity at anything but 0 or 1.
    painter.setOpacity(myOpacity);

    // `body` is the visible card, inset from this widget's own bounds by
    // Theme::surfaceShadowMargin() - see sizeHint() for the growth and
    // undoRect() for the sibling pill that also has to agree on where it
    // landed.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    // The kind-tinted left stripe - accent() for a Note, danger() for a
    // Failure. The pre-Graphite border used textMuted() for failures, which
    // read QUIETER than a routine note - backwards, and danger() documents
    // itself as the failure colour. Painted
    // over the shared base rather than replacing it, so the two kinds still
    // read differently at a glance the way the original spec called for.
    // Clipped to the card's own rounded outline so the stripe's outer
    // corners follow paintSurface()'s radius instead of a hard square
    // corner poking past it.
    {
        // 6px rather than the 3 this shipped with. At 3 the stripe read as a
        // border artefact at a glance - the thing a user's eye skips - and the
        // kind of a message is the first thing they need from it. It stays
        // clear of the text, which starts kPad (14) in from the same edge.
        constexpr int kStripeWidth = 6;
        QPainterPath cardPath;
        cardPath.addRoundedRect(body, 8, 8);
        painter.save();
        painter.setClipPath(cardPath);
        painter.fillRect(QRect(body.left(), body.top(), kStripeWidth, body.height()),
                         myKind == Kind::Failure ? Theme::danger() : Theme::accent());
        painter.restore();
    }

    const int textWidth = body.width() - kPad * 2 - (myHasUndo ? kUndoWidth + kPad : 0);
    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), textWidth, body.height()),
                     Qt::TextWordWrap | Qt::AlignVCenter | Qt::AlignLeft, myText);

    // Painted here rather than by the sibling UndoControl - see that class's
    // comment for why, and undoLabel() for why this is the only place the
    // word "Undo" is spelled out.
    if (myHasUndo) {
        const QRect r = undoRect();
        QPainterPath pill;
        pill.addRoundedRect(r, 5.0, 5.0);
        painter.fillPath(pill, Theme::chipHover());
        painter.setFont(Theme::labelFont());
        // Dimmed while the Undo route itself is closed (mid-sketch, say):
        // the control is hidden from hit-testing by syncUndoGeometry(), and
        // this is what stops the pill from still looking clickable. The
        // message keeps saying what it said - only the offer is withdrawn.
        painter.setPen(myUndoEnabled ? Theme::accent() : Theme::textDisabled());
        painter.drawText(r, Qt::AlignCenter, undoLabel());
    }
}

QString Toast::undoLabel() const
{
    return tr("Undo");
}

QStringList Toast::paintedTexts() const
{
    // Every message this widget has been given this run, not just the live
    // one - see the header for what that does and does not cover.
    QStringList texts{ undoLabel() };
    texts << myShownTexts;
    if (!myText.isEmpty() && !texts.contains(myText)) texts << myText;
    return texts;
}

ToastHost::ToastHost(OcctViewWidget* viewport, QWidget* parent)
    : QObject(parent)
    , myViewport(viewport)
{
    myToast = new Toast(viewport);
    // Starts fully transparent: myToast is also hidden at this point, so
    // this matters only as the starting value the first show()'s fade-in
    // runs from.
    myToast->setOpacity(0.0);

    connect(myToast, &Toast::undoClicked, this, [this] {
        emit undoRequested();
        dismiss();
    });

    myTimer = new QTimer(this);
    myTimer->setSingleShot(true);
    connect(myTimer, &QTimer::timeout, this, &ToastHost::dismiss);

    // One animation for the whole lifetime of this host - see the header
    // comment on myFade for why. valueChanged reads myToast live (not a
    // captured pointer) so it is always correct regardless of which fadeTo()
    // call is currently in flight.
    myFade = new QVariantAnimation(this);
    myFade->setDuration(Theme::motionMs());
    myFade->setEasingCurve(Theme::motionCurve());
    connect(myFade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (myToast) myToast->setOpacity(value.toDouble());
    });
    connect(myFade, &QVariantAnimation::finished, this, [this] {
        if (myFadeFinished) {
            const std::function<void()> callback = std::move(myFadeFinished);
            myFadeFinished = nullptr;
            callback();
        }
    });

    // No event filter on the viewport any more. Repositioning on a raw
    // resize event ran BEFORE ViewportOverlay had moved the walkthrough
    // guide this toast steps around (filters run last-installed-first, and
    // the overlay installs its own first), so a shrink placed the toast
    // against the guide's pre-resize rectangle and the guide then landed on
    // top of it. MainWindow drives replace() from
    // ViewportOverlay::laidOut() instead - see that signal's comment - which
    // is by construction after every anchored widget is at its final
    // rectangle, and from appStateChanged(), which covers a guide appearing
    // underneath a toast that is already up.
}

void ToastHost::show(const QString& text, Toast::Kind kind, bool undo, int documentStamp)
{
    // THE drop site for View -> Show notifications, and the asymmetry in it is
    // a law rather than a preference: a Note is the app telling the user
    // something went right, which they are entitled to switch off, while a
    // FAILURE IS A REFUSAL AND A REFUSAL THAT REPORTS NOWHERE IS A SILENT
    // FAILURE. This app has no modal dialogs, no error log and no status line
    // that persists - a toast is the only surface a refusal has - so a
    // preference that could reach one would turn every kernel refusal into an
    // operation that simply did nothing, which is exactly what CLAUDE.md's
    // "never surface a failed boolean as a success" forbids one layer down.
    //
    // Dropped rather than shown-and-hidden: nothing is repositioned, no timer
    // is armed, and myStamp is left alone, so a live message that a document
    // change is about to dismiss is not replaced by one that was never shown.
    if (kind == Toast::Kind::Note && !myNotesEnabled) return;

    myStamp = documentStamp;
    myToast->setMessage(text, kind, undo);
    reposition();
    myToast->show();
    myToast->raise();
    if (myToast->undoControl()) myToast->undoControl()->raise();

    // fadeTo() stops whatever fade is already running before doing anything
    // else, so this is correct whether the toast was hidden (a real fade-in
    // from 0), already fully shown (a no-op fade to the value it is already
    // at), or mid a dismiss() fade-out that this call is interrupting (a
    // smooth reversal back up to fully shown, rather than an abrupt jump).
    fadeTo(1.0, nullptr);

    // Armed here, unconditionally and synchronously - never inside fadeTo()
    // or its callback. A fade must not change when the dismiss countdown
    // starts, since remainingMs() and the Note/Failure duration contract
    // both read this timer directly.
    myTimer->start(kind == Toast::Kind::Failure ? kFailureMs : kNoteMs);
}

void ToastHost::documentMovedTo(int documentStamp)
{
    // A toast that names an operation and offers to undo it must not outlive
    // that operation. Nothing used to dismiss one when the document moved on,
    // so "Deleted Body 02 - Undo" survived a Ctrl+Z and its pill then popped
    // the checkpoint BEFORE the one it named: the label described one change
    // and the control performed another. The stamp is DocumentModel's own
    // revision as of the message (see show()); any change to it means this
    // message is describing the past.
    if (myStamp < 0 || documentStamp == myStamp) return;
    myStamp = -1;
    if (isShowing()) dismiss();
}

void ToastHost::setUndoEnabled(bool enabled)
{
    if (myToast) myToast->setUndoEnabled(enabled);
}

void ToastHost::setNotesEnabled(bool enabled)
{
    // Stored only. A Note already on screen when the user switches
    // notifications off is left to time out on its own: it is four seconds
    // old at most, and yanking a message away mid-read is a worse surprise
    // than one more message.
    myNotesEnabled = enabled;
}

void ToastHost::replace()
{
    if (!isShowing()) return;
    reposition();
    myToast->raise();
    if (myToast->undoControl()) myToast->undoControl()->raise();
}

QString ToastHost::currentText() const
{
    return (myToast && myToast->isVisible()) ? myToast->text() : QString();
}

bool ToastHost::isShowing() const
{
    return myToast && myToast->isVisible();
}

QWidget* ToastHost::undoControl() const
{
    return myToast ? myToast->undoControl() : nullptr;
}

int ToastHost::remainingMs() const
{
    return myTimer ? myTimer->remainingTime() : -1;
}

void ToastHost::dismiss()
{
    myTimer->stop();
    if (!myToast) return;

    // Folded into the DERIVED visibility syncUndoGeometry() computes, not a
    // one-shot hide() - a visible, still-clickable Undo pill for the whole
    // fade is exactly how an ordinary impatient double-click on Undo undid
    // two operations, and a one-shot hide() alone is not enough to prevent
    // it: this widget stays isVisible() == true until the fade actually
    // finishes, and ToastHost::reposition() re-derives the control's
    // visibility on every viewport resize for as long as that holds, which
    // would silently re-show a control a one-shot hide() had already closed.
    // See the class comment on Toast::myDismissing.
    myToast->setDismissing(true);

    // hide() only once the fade-out actually finishes - synchronously, when
    // animations are disabled (see fadeTo()), so isShowing() still flips the
    // instant dismiss() returns in that case, exactly as it did before the
    // fade existed. gui_smoke disables animations on the real viewport, so
    // every lifetime check in the suite still reads a synchronous dismiss().
    QPointer<Toast> toast = myToast;
    fadeTo(0.0, [toast] {
        if (toast) toast->hide();
    });
}

void ToastHost::fadeTo(double opacity, std::function<void()> onFinished)
{
    if (!myToast || !myFade) return;

    // Stops whatever fade might already be running, including one this call
    // is about to replace, before anything else - so a stale callback for
    // the fade being interrupted can never fire once this one is set below.
    myFade->stop();
    myFadeFinished = nullptr;

    if (!myViewport || !myViewport->animationsEnabled()) {
        // Mirrors OcctViewWidget::animateTo()'s own handling of a disabled
        // camera animation: skip it entirely and apply the end state
        // directly, synchronously, rather than starting the animation and
        // waiting on the event loop to notice it finished. gui_smoke relies
        // on exactly this - it disables animations on the real viewport
        // specifically so a fade cannot turn a lifetime check into
        // something that has to wait.
        myToast->setOpacity(opacity);
        if (onFinished) onFinished();
        return;
    }

    myFadeFinished = std::move(onFinished);
    myFade->setStartValue(myToast->opacity());
    myFade->setEndValue(opacity);
    myFade->start();
}

void ToastHost::reposition()
{
    if (!myViewport || !myToast) return;
    // Through Theme::wholeDevicePixels() - see Theme.h. This card's width is
    // its message's measured width, which is as arbitrary a number as exists
    // in this app, and its far edge landing between device rows leaves a row
    // Qt flushes but the widget's own logical clip cannot reach: black over
    // the GL surface. Measured, not theorised - the whole-window black-run
    // sweep caught a 630-device-pixel 0,0,0 line along this card's bottom edge
    // at 175% scaling, dead centre, after the rail's had already been fixed.
    myToast->resize(Theme::wholeDevicePixels(myToast->sizeHint()));
    const int centred = (myViewport->width() - myToast->width()) / 2;
    const int limitX = std::max(0, myViewport->width() - myToast->width());
    int x = centred;
    int y = myViewport->height() - myToast->height() - kBottomMargin;

    // Everything the overlay has anchored: the walkthrough guide bottom
    // right, the Snap/Select cluster bottom left, the gizmo and unit readout
    // top right, and so on. All of them are permanent or instructional, and
    // all of them are z-ABOVE this widget after the next relayout() - so the
    // transient toast is always the one that steps aside, and an overlap is
    // not untidiness but unreadable text over an unreachable control. Asking
    // the overlay for its own rectangles, rather than naming the widget types
    // this file happens to know about, means a cluster added later is stepped
    // around for free.
    std::vector<QRect> obstacles;
    if (const ViewportOverlay* overlay = myViewport->findChild<ViewportOverlay*>())
        obstacles = overlay->occupiedRects();

    // Solve one horizontal band at a time rather than nudging past obstacles
    // one at a time: an obstacle to the left raises the floor, one to the
    // right lowers the ceiling, and if the two meet there is no room at this
    // height at all. A per-obstacle "step left" rule cannot express that -
    // stepping left is exactly the wrong move for the bottom-LEFT cluster,
    // which is how a message at 800x500 ended up with a third of itself
    // under Snap/Select. When a band has no room the search moves up to the
    // next one and asks again, because the row above can be just as occupied
    // as the row below: a single one-shot hop landed the toast squarely on
    // the left- and right-centre clusters.
    for (int attempt = 0; attempt < 8; ++attempt) {
        int minX = 0;
        int maxX = limitX;
        int highestTop = -1;
        for (const QRect& obstacle : obstacles) {
            // Only obstacles sharing this band matter.
            if (obstacle.bottom() < y || obstacle.top() > y + myToast->height() - 1) continue;
            highestTop = highestTop < 0 ? obstacle.top() : std::min(highestTop, obstacle.top());
            if (obstacle.center().x() < myViewport->width() / 2)
                minX = std::max(minX, obstacle.right() + 1 + kClearance);
            else
                maxX = std::min(maxX, obstacle.left() - kClearance - myToast->width());
        }

        if (minX <= maxX) {
            x = std::min(std::max(centred, minX), maxX);
            break;
        }

        // No room beside them at this height. Move to the band above the
        // highest thing blocking this one and try again - never off the top
        // edge, since a toast nudged out of the viewport reports nothing to
        // anybody.
        const int next = std::max(0, highestTop - kClearance - myToast->height());
        if (next >= y) { x = centred; break; }   // no upward progress left
        y = next;
        x = centred;
    }


    // Whole DEVICE pixels, in the window's own coordinates - the position half
    // of Theme's rule; the size half is at the resize above. Snapped last,
    // after every avoidance clamp, and always downward, so it cannot push the
    // card back over an obstacle the clamps just moved it off.
    {
        const QPoint origin = myViewport->mapTo(myViewport->window(), QPoint(0, 0));
        const double dpr = myViewport->devicePixelRatioF();
        x = Theme::snapToDevicePixels(x, origin.x(), dpr);
        y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    }

    myToast->move(x, y);

    // A hint balloon may already be up and have no way to know this toast
    // just appeared (or just moved) underneath it - reconsider() is what
    // normally re-places a live balloon, but it only runs on
    // appStateChanged, which showing a toast does not itself emit. Nudge it
    // directly rather than waiting for the next unrelated state change.
    if (HintBalloon* balloon = myViewport->findChild<HintBalloon*>()) {
        balloon->reposition();
    }
}

