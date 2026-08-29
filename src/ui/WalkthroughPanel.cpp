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

namespace {
constexpr int kPad = 14;
constexpr int kTitle = 30;
constexpr int kStep = 26;
}  // namespace

WalkthroughPanel::WalkthroughPanel(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedSize(sizeHint());
    connect(myWindow, &MainWindow::appStateChanged, this, &WalkthroughPanel::refresh);
    refresh();
}

void WalkthroughPanel::refresh()
{
    // Even "finished" is derived from live state on every call, never latched
    // for good - a returning user (progress restored from an earlier session,
    // or already past the threshold some other way, including a mid-session
    // "Show tips again" reset while a body still exists) always resolves
    // correctly, and it is what makes showEvent() safe to route through here
    // too, see the header.
    if (myWindow->progress().hasLearned("walkthrough.done")) {
        myFinished = true;
        hide();
        return;
    }

    // Derive from live state; latch the transient ones.
    if (myWindow->isSketching()) myStartedSketch = true;
    if (myWindow->sketch().pointCount() >= 3) { myStartedSketch = true; myPlacedPoints = true; }
    if (myWindow->hasPendingFace()) {
        myStartedSketch = true; myPlacedPoints = true; myClosedOutline = true;
    }
    const bool madeBody = myWindow->document().count() > 0;
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

void WalkthroughPanel::mousePressEvent(QMouseEvent* event)
{
    if (skipRect().contains(event->position().toPoint())) finish();
}

void WalkthroughPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
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

    const QString steps[4] = {
        tr("Press Ctrl+K to start an outline"),
        tr("Click at least 3 points on the ground"),
        tr("Press Enter to close the outline"),
        tr("Press E and give it a height"),
    };
    const bool done[4] = {myStartedSketch, myPlacedPoints, myClosedOutline,
                          myWindow->document().count() > 0};

    int y = kTitle;
    for (int i = 0; i < 4; ++i) {
        const QRect line(kPad, y, width() - kPad * 2, kStep);
        painter.setPen(done[i] ? Theme::accent() : Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft,
                         (done[i] ? QStringLiteral("\u2713  ") : QStringLiteral("\u2022  ")) +
                             steps[i]);
        y += kStep;
    }
}
