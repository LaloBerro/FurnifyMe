#include "WalkthroughPanel.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "SketchController.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>

#include <functional>
#include <utility>

namespace {
constexpr int kPad = 14;
constexpr int kTitle = 30;
constexpr int kStep = 26;

// The one interactive spot on an otherwise click-through overlay. Qt's mouse
// hit-testing skips a widget carrying Qt::WA_TransparentForMouseEvents
// entirely - including the initial press of a drag, which is what a real
// click-through needs (a widget that merely calls event->ignore() from
// mousePressEvent is not equivalent: Qt6 still treats the press as delivered
// for grab purposes regardless of ignore(), so a would-be RMB-orbit or
// sketch click started over that widget would still die there). A plain
// child widget is exempt from its parent's transparency and is hit-tested on
// its own, which is what lets this one small region stay clickable while
// everything else in the panel passes through to the viewport underneath.
// It paints nothing of its own, so the parent's own paintEvent - which draws
// the "skip" label at the same rect - remains what the user actually sees.
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
    // click and drag in - see SkipControl's comment for why this, and not
    // event->ignore(), is what actually keeps it out of the way.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    mySkip = new SkipControl([this] { finish(); }, this);
    mySkip->setGeometry(skipRect());

    connect(myWindow, &MainWindow::appStateChanged, this, &WalkthroughPanel::refresh);
    refresh();
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

void WalkthroughPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

QStringList WalkthroughPanel::stepTexts() const
{
    return {
        tr("Press Ctrl+K to start an outline"),
        tr("Click at least 3 points on the ground"),
        tr("Press Enter to close the outline"),
        tr("Press E and give it a height"),
    };
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

    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, 0, width() - kPad * 2, kTitle),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Make your first body"));

    painter.setFont(font());
    painter.setPen(Theme::textMuted());
    painter.drawText(skipRect(), Qt::AlignCenter, tr("skip"));

    const QStringList steps = stepTexts();
    const bool done[4] = {myStartedSketch, myPlacedPoints, myClosedOutline,
                          static_cast<int>(myWindow->document().count()) > myBodyBaseline};

    int y = kTitle;
    for (int i = 0; i < 4; ++i) {
        const QRect line(kPad, y, width() - kPad * 2, kStep);
        painter.setPen(done[i] ? Theme::accent() : Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft,
                         (done[i] ? QStringLiteral("✓  ") : QStringLiteral("•  ")) +
                             steps[i]);
        y += kStep;
    }
}
