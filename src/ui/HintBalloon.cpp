#include "HintBalloon.h"

#include "AxisGizmo.h"
#include "DocumentModel.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 250;

const QString kBooleanEvent = QStringLiteral("boolean.completed");
const QString kFaceModeEvent = QStringLiteral("faceMode.used");
const QString kViewChangedEvent = QStringLiteral("view.changed");
}  // namespace

HintBalloon::HintBalloon(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    connect(myWindow, &MainWindow::appStateChanged, this, &HintBalloon::reconsider);
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
        // True while a body exists and the camera is not aligned to a named
        // view. AxisGizmo::labelText() already answers exactly this question
        // - "Persp" covers both "never touched" and free-orbited, and it
        // reports one of Top/Front/Right/... the moment a gizmo click or a
        // 0-3 key snaps the camera to one, which is the action this hint
        // teaches. A gizmo that is somehow not found is treated as "not yet
        // aligned" so the hint stays available rather than silently vanishing.
        const AxisGizmo* gizmo = myWindow->findChild<AxisGizmo*>();
        return myWindow->document().count() > 0 &&
               (!gizmo || gizmo->labelText() == QStringLiteral("Persp"));
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

    if (!myText.isEmpty()) return;   // do not interrupt a hint already up

    if (isDue(kFaceModeEvent)) {
        showHint(kFaceModeEvent);
        return;
    }

    if (isDue(kViewChangedEvent)) {
        showHint(kViewChangedEvent);
    }
}

void HintBalloon::reposition()
{
    if (myText.isEmpty() || !parentWidget()) return;

    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, kWidth - kPad * 2, 1000),
                                              Qt::TextWordWrap, myText);
    resize(kWidth, bounds.height() + kPad * 2 + 22);
    move((parentWidget()->width() - width()) / 2,
         parentWidget()->height() - height() - 90);
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

void HintBalloon::mousePressEvent(QMouseEvent* event)
{
    dismiss();
    event->accept();
}

void HintBalloon::mouseReleaseEvent(QMouseEvent* event)
{
    // See the header: this exists purely to stop the release from
    // propagating to the viewport underneath once dismiss() has already
    // done its job on the press.
    event->accept();
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
