#include "HintBalloon.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 250;
}  // namespace

HintBalloon::HintBalloon(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    connect(myWindow, &MainWindow::appStateChanged, this, &HintBalloon::reconsider);
}

void HintBalloon::reconsider()
{
    const UserProgress& progress = myWindow->progress();

    // "Permanently silent once learned" is a property of the app's state, not
    // something that waits for the user to click the balloon away first: a
    // hint that crosses the learned threshold while it happens to be the one
    // on screen must not be left hanging there. Auto-dismissing it here is
    // what makes the two halves of the rule ("shown once per session" and
    // "gone for good once learned") agree with each other instead of only
    // one of them being enforced.
    if (!myText.isEmpty() && progress.hasLearned(myEvent.toStdString())) {
        dismiss();
    }

    const std::size_t selected = myWindow->view()->selectedSolidIds().size();
    const std::size_t bodies = myWindow->document().count();

    auto isDue = [&](const QString& event, bool conditionMet) {
        return conditionMet && !progress.hasLearned(event.toStdString()) &&
               !myShownThisSession.count(event);
    };

    const bool booleanDue = isDue(QStringLiteral("boolean.completed"), selected == 2);

    // The boolean hint is the most specific and time-limited of the three -
    // its condition (exactly two bodies selected) is true for a moment, not
    // for the rest of the session the way "a body exists" is. That is why it
    // alone is allowed to replace whatever lower-priority hint is currently
    // up: the other two would otherwise latch onto the very first body ever
    // made and sit there, unacknowledged, blocking this one from ever being
    // seen. It does not replace itself, and it never replaces another hint
    // that has not yet had its own turn shown to the user this session.
    if (booleanDue) {
        if (myEvent != QStringLiteral("boolean.completed")) {
            showHint(QStringLiteral("boolean.completed"),
                     tr("Two bodies selected — Union combines them, Subtract cuts the second "
                        "out of the first, Intersect keeps only the overlap."));
        }
        return;
    }

    if (!myText.isEmpty()) return;   // do not interrupt a hint already up

    if (isDue(QStringLiteral("faceMode.used"), bodies > 0)) {
        showHint(QStringLiteral("faceMode.used"),
                 tr("Switch to Select Faces to pick one face at a time instead of a "
                    "whole body."));
        return;
    }

    if (isDue(QStringLiteral("view.changed"), bodies > 0)) {
        showHint(QStringLiteral("view.changed"),
                 tr("Click an arm of the gizmo, top right, to look from that direction. "
                    "Keys 0 to 3 do the same."));
    }
}

void HintBalloon::showHint(const QString& event, const QString& text)
{
    myEvent = event;
    myText = text;
    myShownThisSession.insert(event);

    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, kWidth - kPad * 2, 1000),
                                              Qt::TextWordWrap, myText);
    resize(kWidth, bounds.height() + kPad * 2 + 22);
    if (parentWidget()) {
        move((parentWidget()->width() - width()) / 2,
             parentWidget()->height() - height() - 90);
    }
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
    dismiss();
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

    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, kPad, width() - kPad * 2, height() - kPad * 2 - 20),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, myText);

    painter.setPen(Theme::accent());
    painter.drawText(QRect(kPad, height() - 26, width() - kPad * 2, 20),
                     Qt::AlignRight | Qt::AlignVCenter, tr("got it"));
}

QStringList HintBalloon::paintedTexts() const
{
    // The full set this widget can ever paint - not just myText, which is
    // whatever happens to be up right now - so gui_smoke's banned-word sweep
    // has no blind spot here, the same way WalkthroughPanel::paintedTexts()
    // covers its own copy.
    return {
        tr("Two bodies selected — Union combines them, Subtract cuts the second "
           "out of the first, Intersect keeps only the overlap."),
        tr("Switch to Select Faces to pick one face at a time instead of a "
           "whole body."),
        tr("Click an arm of the gizmo, top right, to look from that direction. "
           "Keys 0 to 3 do the same."),
        tr("got it"),
    };
}
