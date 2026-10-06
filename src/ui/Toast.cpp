#include "Toast.h"

#include "HintBalloon.h"
#include "IconSet.h"
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
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int kPad = 14;
constexpr int kWidth = 360;
constexpr int kUndoWidth = 64;
constexpr int kUndoHeight = 26;
constexpr int kBottomMargin = Theme::Space::Wide;
constexpr int kClearance = 8;   // gap left when stepping around the guide
// How far a message lifts into place as it fades in. Six pixels: enough to
// read as movement at the fade's own 160 ms, small enough that a card caught
// at any point of the rise is still plainly where it belongs.
constexpr int kRisePx = 6;

constexpr int kNoteMs = 4000;
constexpr int kFailureMs = 8000;
// What a paused countdown resumes with at the least, once the pointer leaves.
// A Failure read for six seconds and left with 40 ms on the clock would
// otherwise vanish the instant the hand moved off it - which is the toast
// being dismissed early by the very hover that was meant to keep it.
constexpr int kResumeFloorMs = 1500;

// A Failure's leading warning mark (the 2026-10 review's F6): its side and the
// gap between it and the message. The message column narrows by both, and
// sizeHint() measures against that narrower column - the toast sizes itself
// from what it paints.
constexpr int kGlyphSize = 16;
constexpr int kGlyphGap = 8;

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
        // UndoControl paints nothing of its own - see the class comment -
        // so there is no card here for Theme::makeSurfaceTransparent() to
        // protect the corners of; WA_TranslucentBackground above already
        // keeps the QSS rule from stamping this widget opaque at all.
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
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    myUndo = new UndoControl([this] { emit undoClicked(); }, parent);
    myUndo->hide();
    hide();
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

int Toast::textColumnWidth(Kind kind, bool undo)
{
    return kWidth - kPad * 2 - (undo ? kUndoWidth + kPad : 0) -
           (kind == Kind::Failure ? kGlyphSize + kGlyphGap : 0);
}

QRect Toast::glyphRect() const
{
    // Kind-independent on purpose: gui_smoke reads the SAME rectangle out of a
    // Note and a Failure, and only one of them may carry ink there.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    return QRect(body.left() + kPad, body.center().y() - kGlyphSize / 2, kGlyphSize,
                 kGlyphSize);
}

QSize Toast::sizeHint() const
{
    const int textWidth = textColumnWidth(myKind, myHasUndo);
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

    // A FAILURE IS NOT TOLD APART BY COLOUR ALONE (the 2026-10 review's F6).
    // The stripe above was the only difference between the two kinds, and a
    // user who cannot separate that red from that violet - or who simply
    // does not look at the left edge - could not tell a refusal from a
    // report. So a Failure carries two more channels: a leading warning mark,
    // which is a SHAPE, and its whole border in danger(), which is an
    // outline the eye meets wherever it lands on the card. A Note is
    // exactly what it was.
    const bool failure = myKind == Kind::Failure;
    if (failure) {
        Theme::drawCrispBorder(painter, QRectF(body), Theme::danger(), 8);

        const QRect mark = glyphRect();
        constexpr double kGrid = 24.0;   // IconSet's design grid
        painter.save();
        painter.translate(mark.topLeft());
        painter.scale(mark.width() / kGrid, mark.height() / kGrid);
        QPen pen(Theme::danger(), 2.0 * kGrid / mark.width());
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        IconSet::paintGlyph(painter, IconSet::Glyph::Warning);
        painter.restore();
    }

    const int textLeft = body.left() + kPad + (failure ? kGlyphSize + kGlyphGap : 0);
    const int textWidth = textColumnWidth(myKind, myHasUndo) - (kWidth - body.width());
    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(textLeft, body.top(), textWidth, body.height()),
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
        // accentText(), not accent(): the violet is a fill colour, and as
        // words on chipHover it measured 1.80:1 (the 2026-10 review's F5).
        painter.setPen(myUndoEnabled ? Theme::accentText() : Theme::textDisabled());
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

ToastHost::ToastHost(QWidget* host, QWidget* parent)
    : QObject(parent)
    , myHost(host)
    , myViewport(qobject_cast<OcctViewWidget*>(host))
{
    myToast = new Toast(host);
    // Starts fully transparent: myToast is also hidden at this point, so
    // this matters only as the starting value the first show()'s fade-in
    // runs from.
    myToast->setOpacity(0.0);

    connect(myToast, &Toast::undoClicked, this, [this] {
        emit undoRequested();
        dismiss();
    });

    // The hover pause's three inputs - see countdownPaused() in the header.
    // Filters only observe; every event still reaches its widget.
    if (myHost) myHost->installEventFilter(this);
    myToast->installEventFilter(this);
    if (myToast->undoControl()) myToast->undoControl()->installEventFilter(this);

    myTimer = new QTimer(this);
    myTimer->setSingleShot(true);
    connect(myTimer, &QTimer::timeout, this, &ToastHost::dismiss);

    // One animation for the whole lifetime of this host - see the header
    // comment on myFade for why. valueChanged reads myToast live (not a
    // captured pointer) so it is always correct regardless of which fadeTo()
    // call is currently in flight.
    // The arrival rise (see the header). Built beside the fade, runs with it.
    myRise = new QPropertyAnimation(this);
    myRise->setPropertyName("pos");
    myRise->setDuration(Theme::motionMs());
    myRise->setEasingCurve(Theme::motionCurve());

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
    // FAILURE. This app has no modal dialogs and no error log - a toast is
    // where a refusal is SEEN, and the status line that keeps its reason
    // afterwards is written from this toast (failureShown()) - so a
    // preference that could reach one would turn every kernel refusal into an
    // operation that simply did nothing, which is exactly what CLAUDE.md's
    // "never surface a failed boolean as a success" forbids one layer down.
    //
    // Dropped rather than shown-and-hidden: nothing is repositioned, no timer
    // is armed, and myStamp is left alone, so a live message that a document
    // change is about to dismiss is not replaced by one that was never shown.
    if (kind == Toast::Kind::Note && !myNotesEnabled) return;

    myStamp = documentStamp;
    // Whether this message is ARRIVING or merely replacing the one already
    // on screen - a replacement must not jump back down six pixels and climb
    // again, since the card never left.
    const bool arriving = !myToast->isVisible();
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

    // The rise rides with the fade, and only on a genuine arrival. Skipped
    // outright when animations are off, exactly as the fade is - the suite
    // reads this card's position against the solver's own answer, and a
    // toast caught mid-rise would be six pixels low.
    if (arriving && myRise && animationsOn()) {
        const QPoint home = myToast->pos();
        myRise->setTargetObject(myToast);
        myRise->setStartValue(home + QPoint(0, kRisePx));
        myRise->setEndValue(home);
        myToast->move(home + QPoint(0, kRisePx));
        myRise->start(QAbstractAnimation::KeepWhenStopped);
    }

    // Armed here, unconditionally and synchronously - never inside fadeTo()
    // or its callback. A fade must not change when the dismiss countdown
    // starts, since remainingMs() and the Note/Failure duration contract
    // both read this timer directly.
    myPaused = false;
    myTimer->start(kind == Toast::Kind::Failure ? kFailureMs : kNoteMs);
    if (kind == Toast::Kind::Failure) emit failureShown(text);
    // A message arriving under a pointer that is already resting on the
    // toast's place is being read from its first frame.
    syncHoverPause();
}

bool ToastHost::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (watched == myHost) {
        if (type == QEvent::MouseMove) {
            myPointer = static_cast<QMouseEvent*>(event)->position().toPoint();
            myPointerInViewport = true;
            syncHoverPause();
        } else if (type == QEvent::Leave) {
            myPointerInViewport = false;
            syncHoverPause();
        }
    } else if (myToast && watched == myToast) {
        if (type == QEvent::Enter || type == QEvent::HoverEnter) {
            myOverToast = true;
            syncHoverPause();
        } else if (type == QEvent::Leave || type == QEvent::HoverLeave) {
            myOverToast = false;
            syncHoverPause();
        }
    } else if (myToast && watched == myToast->undoControl()) {
        if (type == QEvent::Enter || type == QEvent::HoverEnter) {
            myOverUndo = true;
            syncHoverPause();
        } else if (type == QEvent::Leave || type == QEvent::HoverLeave) {
            myOverUndo = false;
            syncHoverPause();
        }
    }
    return QObject::eventFilter(watched, event);
}

void ToastHost::syncHoverPause()
{
    const bool overCard = myPointerInViewport && myToast && myToast->geometry().contains(myPointer);
    const bool hovered = isShowing() && (overCard || myOverToast || myOverUndo);
    if (hovered && !myPaused && myTimer->isActive()) {
        myPausedRemainingMs = myTimer->remainingTime();
        myTimer->stop();
        myPaused = true;
    } else if (!hovered && myPaused) {
        myPaused = false;
        myTimer->start(std::max(myPausedRemainingMs, kResumeFloorMs));
    }
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
    // Frozen while a reader holds it - the countdown is the number left when
    // the pointer arrived, not a timer that is no longer running.
    if (myPaused) return myPausedRemainingMs;
    return myTimer ? myTimer->remainingTime() : -1;
}

void ToastHost::dismiss()
{
    myTimer->stop();
    // A dismissal from anywhere - Undo, the document moving on - ends the
    // pause with the message. The two Enter flags go too: the toast and its
    // pill are about to hide, and a hidden widget gets no Leave.
    myPaused = false;
    myOverToast = false;
    myOverUndo = false;
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

    if (!animationsOn()) {
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

bool ToastHost::animationsOn() const
{
    return myViewport ? myViewport->animationsEnabled() : myAnimationsEnabled;
}

void ToastHost::reposition()
{
    if (!myHost || !myToast) return;
    // Through Theme::wholeDevicePixels() - see Theme.h. This card's width is
    // its message's measured width, which is as arbitrary a number as exists
    // in this app, and its far edge landing between device rows leaves a row
    // Qt flushes but the widget's own logical clip cannot reach: black over
    // the GL surface. Measured, not theorised - the whole-window black-run
    // sweep caught a 630-device-pixel 0,0,0 line along this card's bottom edge
    // at 175% scaling, dead centre, after the rail's had already been fixed.
    myToast->resize(Theme::wholeDevicePixels(myToast->sizeHint()));
    const int centred = (myHost->width() - myToast->width()) / 2;
    const int limitX = std::max(0, myHost->width() - myToast->width());
    int x = centred;
    int y = myHost->height() - myToast->height() - kBottomMargin;

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
    if (const ViewportOverlay* overlay = myHost->findChild<ViewportOverlay*>())
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
            if (obstacle.center().x() < myHost->width() / 2)
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
        const QPoint origin = myHost->mapTo(myHost->window(), QPoint(0, 0));
        const double dpr = myHost->devicePixelRatioF();
        x = Theme::snapToDevicePixels(x, origin.x(), dpr);
        y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    }

    // The solver has just answered where this card belongs, so a rise still
    // in the air is aimed at a position that no longer applies. Stopped
    // rather than re-aimed: six pixels is not a flight worth rescuing, and
    // the card is one move away from correct either way.
    if (myRise) myRise->stop();
    myToast->move(x, y);
    // The card moved under a pointer that may not have: whether it is being
    // read is a question about the NEW rectangle.
    syncHoverPause();

    // A hint balloon may already be up and have no way to know this toast
    // just appeared (or just moved) underneath it - reconsider() is what
    // normally re-places a live balloon, but it only runs on
    // appStateChanged, which showing a toast does not itself emit. Nudge it
    // directly rather than waiting for the next unrelated state change.
    if (HintBalloon* balloon = myHost->findChild<HintBalloon*>()) {
        balloon->reposition();
    }
}

