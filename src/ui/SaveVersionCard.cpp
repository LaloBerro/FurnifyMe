#include "SaveVersionCard.h"

#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 230;
constexpr int kLabelHeight = 20;
constexpr int kFieldHeight = 26;
constexpr int kHintGap = 6;
constexpr int kHintHeight = 16;
// The same top-edge offset ExtrudePreview's own kMargin targets - see there
// for why both files subtract Theme::surfaceShadowMargin() from it.
constexpr int kMargin = 16;
}  // namespace

SaveVersionCard::SaveVersionCard(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    myField = new QLineEdit(view);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    myField->setPlaceholderText(tr("Version name"));
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &SaveVersionCard::applyTheme);
    // Enter and Escape are claimed application-wide while visible - see
    // eventFilter() - not wired to the field's own returnPressed().
    connect(myField, &QLineEdit::textChanged, this, [this](const QString&) { markInvalid(false); });
    markInvalid(false);

    syncFieldGeometry();
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this,
                          &SaveVersionCard::onAppStateChanged);
}

SaveVersionCard::~SaveVersionCard()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    delete myField;   // sibling, not a child - see the header
}

void SaveVersionCard::begin()
{
    reposition();
    show();
    raise();
    if (myField) myField->raise();

    if (myField) {
        myField->blockSignals(true);
        myField->clear();
        myField->blockSignals(false);
        myField->setFocus(Qt::OtherFocusReason);
    }
    markInvalid(false);
}

void SaveVersionCard::cancel()
{
    markInvalid(false);
    hide();
}

void SaveVersionCard::onAppStateChanged()
{
    // Mirrors ExtrudePreview::onAppStateChanged(): the predicate that gates
    // opening this panel can go false while it is already open, and nothing
    // else would tell it to close.
    //
    // The ruling (fix round 1, Important 2), stated explicitly because a
    // silent discard is exactly the defect that round found: an ORDINARY
    // selection change - clicking a body, which used to flip
    // canOpenSaveVersion() false through the transform-gizmo term that has
    // since been removed from it - must NOT be able to close this card,
    // because it carries no genuine key-claim conflict and the user did
    // nothing that should cost them a typed name. What canOpenSaveVersion()
    // can STILL go false on are the three real application-wide key claims -
    // a sketch started (hasPendingFace()), a face pulled
    // (canPullSelectedFace()), an edge bevelled (canBevelSelectedEdge()) -
    // and each of those is a DELIBERATE gesture the user made while looking
    // at this very card, not an incidental side effect of using the
    // viewport. Closing on one of those, discarding whatever name was typed,
    // is the same rule ExtrudePreview already applies to its own typed
    // height for the identical reason: two application-wide Enter/Escape
    // claims can never coexist, so one of them has to yield, and there is no
    // queue to put the abandoned one on. See gui_smoke.cpp for both halves
    // of this pinned explicitly - a stray body click leaves the card open
    // with its text intact, and starting a sketch closes it.
    if (isVisible() && myWindow && !myWindow->canOpenSaveVersion()) {
        cancel();
    }
}

QLineEdit* SaveVersionCard::field() const
{
    return myField;
}

void SaveVersionCard::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    if (myField) myField->raise();
}

void SaveVersionCard::reposition()
{
    if (!myView) return;
    // Top-centre, exactly where ExtrudePreview stands when it is open - the
    // two can never be open at once (see the header's disjointness note),
    // so there is no collision to worry about between them.
    const int x = (myView->width() - QWidget::width()) / 2;
    move(x, kMargin - Theme::surfaceShadowMargin());
}

QRect SaveVersionCard::fieldRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight,
                QWidget::width() - margin * 2 - kPad * 2, kFieldHeight);
}

QRect SaveVersionCard::hintRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight + kFieldHeight + kHintGap,
                QWidget::width() - margin * 2 - kPad * 2, kHintHeight);
}

QString SaveVersionCard::labelText() const
{
    return tr("Version name");
}

QString SaveVersionCard::hintText() const
{
    return tr("Enter saves — Esc cancels");
}

void SaveVersionCard::syncFieldGeometry()
{
    if (!myField) return;
    myField->setGeometry(fieldRect().translated(pos()));
    myField->setVisible(isVisible());
    myField->raise();
}

void SaveVersionCard::commit()
{
    if (!myField || !myWindow) return;

    const QString name = myField->text().trimmed();
    if (name.isEmpty()) {
        // Refused, not cancelled - the panel stays open so the user can
        // simply type a name, same as ExtrudePreview keeping its last good
        // preview rather than clearing on an empty field.
        markInvalid(true);
        return;
    }

    if (myWindow->saveVersion(name)) {
        hide();
        return;
    }
    // A duplicate name - MainWindow has already shown the Failure toast
    // naming the clash. The panel stays open, invalid, so the user can
    // retype without reopening the whole gesture.
    markInvalid(true);
}

void SaveVersionCard::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    // Square corners, not rounded - this field is a sibling sitting directly
    // on OCCT's on-screen GL surface with no card behind it; see
    // ExtrudePreview::markInvalid() for why an unpainted rounded corner
    // there reads as black rather than transparent.
    myField->setStyleSheet(QStringLiteral(
                               "QLineEdit { background-color: %1; color: %2; "
                               "border: 1px solid %3; border-radius: 0px; padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

void SaveVersionCard::applyTheme()
{
    const QFontMetrics label(Theme::labelFont());
    const QFontMetrics badge(Theme::badgeFont());
    const int content = std::max(label.horizontalAdvance(labelText()),
                                 badge.horizontalAdvance(hintText()));

    const int margin = Theme::surfaceShadowMargin();
    setFixedSize(Theme::wholeDevicePixels(
        QSize(std::max(kWidth, content + kPad * 2) + margin * 2,
              kPad * 2 + kLabelHeight + kFieldHeight + kHintGap + kHintHeight +
                  margin * 2)));

    if (myField) myField->setFont(Theme::bodyFont());
    syncFieldGeometry();
    update();
}

QStringList SaveVersionCard::paintedTexts() const
{
    return {labelText(), hintText()};
}

void SaveVersionCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    if (myInvalid) {
        QPainterPath invalidOutline;
        invalidOutline.addRoundedRect(body, 8, 8);
        painter.setPen(QPen(Theme::danger(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(invalidOutline);
    }

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(hintRect(), Qt::AlignVCenter | Qt::AlignLeft, hintText());
}

void SaveVersionCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncFieldGeometry();
    QCoreApplication::instance()->installEventFilter(this);
}

void SaveVersionCard::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void SaveVersionCard::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncFieldGeometry();
}

void SaveVersionCard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncFieldGeometry();
}

bool SaveVersionCard::eventFilter(QObject* watched, QEvent* event)
{
    // Exactly ExtrudePreview::eventFilter()'s shape - see there for the full
    // reasoning (an RMB orbit moves focus off the field, and nothing else
    // would claim Enter/Escape back for this panel).
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window())
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier) return QWidget::eventFilter(watched, event);

    const int key = keyEvent->key();
    const bool commits = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool cancels = key == Qt::Key_Escape;
    if (!commits && !cancels) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }

    if (commits)
        commit();
    else
        cancel();
    return true;
}
