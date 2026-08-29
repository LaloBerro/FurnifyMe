#include "WalkthroughPanel.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "SketchController.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QFont>
#include <QHideEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>

#include <functional>
#include <utility>

namespace {
constexpr int kPad = 14;
constexpr int kTitle = 30;
constexpr int kStep = 26;

// The one interactive spot on an otherwise click-through overlay.
//
// This is NOT a child of WalkthroughPanel. Qt::WA_TransparentForMouseEvents
// excludes a widget's ENTIRE SUBTREE from hit-testing, not just the widget
// carrying it - verified directly against this machine's Qt 6.11.1:
// QWidgetPrivate::childAtRecursiveHelper simply `continue`s past a
// transparent widget without ever descending into its children. A child
// placed here would therefore have been exactly as unreachable by a real
// click as the transparent parent itself - dead code in the running app,
// even though it responds fine to a synthetic event sent straight to it in
// a test, which is the blind spot that let that version through review.
// (An earlier attempt at this used event->ignore() from a mousePressEvent()
// override instead of transparency; that was also rejected, empirically -
// Qt6 re-marks a widget mouse press as accepted after the handler returns
// regardless of ignore(), even for a stock unmodified QWidget, so it does
// not achieve real click-through either.)
//
// So this is a SIBLING: parented to the same viewport as WalkthroughPanel,
// positioned over skipRect() by the panel itself (see syncSkipGeometry()). A
// sibling is hit-tested on its own, independent of whatever attributes its
// neighbour carries - z-order between the two does not affect that, since
// hit-testing only picks among widgets that are not excluded from it in the
// first place, and the panel excludes itself. syncSkipGeometry() does still
// raise() this above the panel on every move, but ViewportOverlay::relayout()
// raises the panel right back above it immediately afterward, since the
// panel is itself one of the overlay's anchored entries - so in practice the
// panel ends up on top after every relayout. That is harmless only because
// of the same transparency (paint order does not matter either: this widget
// paints nothing of its own, so the parent's own paintEvent - which draws
// the "skip" label at that same rect - is what the user sees regardless of
// which one is stacked above the other).
class SkipControl : public QWidget {
public:
    SkipControl(std::function<void()> onClick, QWidget* parent)
        : QWidget(parent)
        , myOnClick(std::move(onClick))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
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

WalkthroughPanel::WalkthroughPanel(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedSize(sizeHint());

    // The panel sits directly over the viewport it is teaching someone to
    // click and drag in. Transparent to mouse events for its whole subtree
    // is what actually keeps it out of the way - see SkipControl above for
    // why the one interactive control has to live outside that subtree
    // rather than inside it.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    mySkip = new SkipControl([this] { finish(); }, parent);

    // See myBodyBaseline in the header: captured here for the same reason
    // the restore branch in refresh() captures it again later - whenever the
    // panel is about to start showing, a body that already exists must not
    // be mistaken for one made since. document().count() is always 0 at
    // construction (DocumentModel never starts pre-populated), so this is
    // the fresh-user case; refresh() below may immediately decide this
    // panel is not showing at all (an already-learned user), in which case
    // the value simply goes unused until a restore captures it again.
    myBodyBaseline = static_cast<int>(myWindow->document().count());
    syncSkipGeometry();

    connect(myWindow, &MainWindow::appStateChanged, this, &WalkthroughPanel::refresh);
    refresh();
}

WalkthroughPanel::~WalkthroughPanel()
{
    // mySkip is a sibling, not a child, so Qt's parent-child cascade does not
    // clean it up when this panel alone is destroyed - and it holds a raw
    // `this` pointer via its click callback, so leaving it alive after this
    // panel is gone would be a dangling-pointer crash waiting to happen the
    // next time someone clicked it. Nothing destroys the panel alone today,
    // which is exactly why this needs to hold regardless: the alternative is
    // a latent bug waiting for the first caller that does.
    //
    // Safe if the two are instead destroyed together, in either order, by
    // their shared parent tearing down: mySkip is a QPointer, so if it goes
    // first this becomes a harmless delete of a null pointer rather than a
    // double free.
    delete mySkip;
}

void WalkthroughPanel::refresh()
{
    // Even "finished" is derived from live state on every call, never latched
    // for good - a returning user (progress restored from an earlier session,
    // or already past the threshold some other way) always resolves
    // correctly, and it is what makes showEvent() safe to route through here
    // too, see the header.
    if (myWindow->progress().hasLearned("walkthrough.done")) {
        myFinished = true;
        hide();
        return;
    }

    // hasLearned() just went false while the panel was still finished/hidden
    // from an earlier completion - "Show tips again" resetting progress out
    // from under it is the case that matters, but anything that flips the
    // flag back off should restore the same way. Start the four steps over
    // with a fresh baseline (see myBodyBaseline in the header) rather than
    // re-deriving against the old one in this same call: document().count()
    // is almost certainly still > 0 from before the reset, and treating that
    // as "step 4 already done" would silently re-finish the guide on the
    // spot - a reset immediately followed by re-completion is the bug this
    // replaces, not a variant of the fix. The steps get their real derivation
    // on the next refresh(), against the baseline captured here.
    if (myFinished) {
        myFinished = false;
        myStartedSketch = false;
        myPlacedPoints = false;
        myClosedOutline = false;
        myCompleted = 0;
        myBodyBaseline = static_cast<int>(myWindow->document().count());
        show();
        return;
    }

    // Derive from live state; latch the transient ones. Step 4 is "a body
    // was made since the panel appeared" - document().count() alone would
    // stay true forever once any body exists, which is exactly wrong across
    // a restore (see above).
    if (myWindow->isSketching()) myStartedSketch = true;
    if (myWindow->sketch().pointCount() >= 3) { myStartedSketch = true; myPlacedPoints = true; }
    if (myWindow->hasPendingFace()) {
        myStartedSketch = true; myPlacedPoints = true; myClosedOutline = true;
    }
    const bool madeBody = static_cast<int>(myWindow->document().count()) > myBodyBaseline;
    if (madeBody) { myStartedSketch = true; myPlacedPoints = true; myClosedOutline = true; }

    myCompleted = (myStartedSketch ? 1 : 0) + (myPlacedPoints ? 1 : 0) +
                  (myClosedOutline ? 1 : 0) + (madeBody ? 1 : 0);

    if (madeBody) {
        finish();
        return;
    }
    update();
}

void WalkthroughPanel::finish()
{
    myFinished = true;
    // Record it up to the threshold so hasLearned("walkthrough.done") is true.
    // The guide is a one-shot rather than something you get better at, so it
    // reuses the counter as a flag rather than introducing a second concept.
    for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
        myWindow->recordProgress("walkthrough.done");
    }
    hide();
}

QRect WalkthroughPanel::skipRect() const
{
    return QRect(width() - kPad - 34, 8, 34, 18);
}

void WalkthroughPanel::syncSkipGeometry()
{
    if (!mySkip) return;
    // mySkip shares this widget's parent, so skipRect() - defined in this
    // widget's own local coordinates - needs translating by pos() to land in
    // that shared coordinate space.
    mySkip->setGeometry(skipRect().translated(pos()));
    // Belt and suspenders, not a guarantee: ViewportOverlay::relayout() will
    // usually raise() this panel again right after moving it, putting the
    // panel back above mySkip - see the class comment on SkipControl for why
    // that stacking does not actually matter.
    mySkip->raise();
}

void WalkthroughPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncSkipGeometry();
    if (mySkip) mySkip->show();
    // May decide this panel is not actually showing after all (an
    // already-learned user reaching this via the deferred-show path - see
    // the class comment) and hide both again immediately; hideEvent() below
    // is what makes that keep mySkip in sync in that case too.
    refresh();
}

void WalkthroughPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // A floating skip button over a dismissed guide would be worse than no
    // button at all.
    if (mySkip) mySkip->hide();
}

void WalkthroughPanel::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncSkipGeometry();
}

void WalkthroughPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncSkipGeometry();
}

QStringList WalkthroughPanel::paintedTexts() const
{
    return {
        tr("Make your first body"),
        tr("skip"),
        tr("Press Ctrl+K to start an outline"),
        tr("Click at least 3 points on the ground"),
        tr("Press Enter to close the outline"),
        tr("Press E and give it a height"),
    };
}

QStringList WalkthroughPanel::stepTexts() const
{
    return paintedTexts().mid(2);
}

void WalkthroughPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10.0, 10.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::accent(), 1.0));
    painter.drawPath(panel);

    const QStringList texts = paintedTexts();

    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, 0, width() - kPad * 2, kTitle),
                     Qt::AlignVCenter | Qt::AlignLeft, texts[0]);

    painter.setFont(font());
    painter.setPen(Theme::textMuted());
    painter.drawText(skipRect(), Qt::AlignCenter, texts[1]);

    const bool done[4] = {myStartedSketch, myPlacedPoints, myClosedOutline,
                          static_cast<int>(myWindow->document().count()) > myBodyBaseline};

    int y = kTitle;
    for (int i = 0; i < 4; ++i) {
        const QRect line(kPad, y, width() - kPad * 2, kStep);
        painter.setPen(done[i] ? Theme::accent() : Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft,
                         (done[i] ? QStringLiteral("✓  ") : QStringLiteral("•  ")) +
                             texts[2 + i]);
        y += kStep;
    }
}
