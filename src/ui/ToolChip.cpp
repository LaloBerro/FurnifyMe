#include "ToolChip.h"

#include "Theme.h"

#include <QAction>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kIcon = 16;
constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kGap = 8;
constexpr int kRadius = 6;
constexpr double kFocusRingWidth = 2.0;
}  // namespace

ToolChip::ToolChip(QAction* action, IconSet::Glyph glyph, QWidget* parent)
    : QAbstractButton(parent)
    , myAction(action)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setIcon(IconSet::icon(glyph));
    // Every chip is keyboard-reachable, not just clickable - a focus ring
    // that can never actually receive focus would be dead code.
    setFocusPolicy(Qt::StrongFocus);
    // Baseline for this widget's own font() (what the sweep in gui_smoke
    // checks): the chip's own text is a chip label. A per-widget stylesheet
    // wins over the app-wide one regardless of selector specificity, so this
    // sticks reliably rather than fighting the cascade.
    setStyleSheet(QStringLiteral("font-size: %1pt;").arg(Theme::labelFont().pointSizeF()));

    if (myAction) {
        connect(this, &QAbstractButton::clicked, myAction, &QAction::trigger);
        connect(myAction, &QAction::changed, this, &ToolChip::syncFromAction);
        syncFromAction();
    }
}

void ToolChip::syncFromAction()
{
    setText(myAction->text().remove(QLatin1Char('&')));
    setEnabled(myAction->isEnabled());
    setCheckable(myAction->isCheckable());
    setChecked(myAction->isChecked());
    setToolTip(myAction->toolTip());

    myShortcut = myAction->shortcut().toString(QKeySequence::NativeText);
    updateGeometry();
    update();
}

QSize ToolChip::sizeHint() const
{
    // Measured with the same fonts paintEvent() actually draws with below -
    // the label at labelFont(), the shortcut badge at badgeFont().
    const QFontMetrics labelMetrics(Theme::labelFont());
    const QFontMetrics badgeMetrics(Theme::badgeFont());
    int width = kPadX + kIcon + kGap + labelMetrics.horizontalAdvance(text()) + kPadX;
    if (!myShortcut.isEmpty()) width += kGap + badgeMetrics.horizontalAdvance(myShortcut) + 8;
    return QSize(width, kIcon + kPadY * 2);
}

void ToolChip::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor background = Theme::chip();
    if (!isEnabled())        background = Theme::chip().darker(115);
    else if (isChecked())    background = Theme::chipActive();
    else if (isDown())       background = Theme::chipActive();
    else if (myHovered)      background = Theme::chipHover();

    QPainterPath path;
    path.addRoundedRect(rect(), kRadius, kRadius);
    painter.fillPath(path, background);

    if (isChecked()) {
        painter.setPen(QPen(Theme::accent(), 1.5));
        painter.drawPath(path);
    }

    const QRect iconRect(kPadX, (height() - kIcon) / 2, kIcon, kIcon);
    icon().paint(&painter, iconRect, Qt::AlignCenter,
                 isEnabled() ? QIcon::Normal : QIcon::Disabled);

    painter.setFont(Theme::labelFont());
    painter.setPen(isEnabled() ? Theme::text() : Theme::textDisabled());
    const int textLeft = kPadX + kIcon + kGap;
    painter.drawText(QRect(textLeft, 0, width() - textLeft - kPadX, height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text());

    if (!myShortcut.isEmpty()) {
        painter.setFont(Theme::badgeFont());
        painter.setPen(Theme::textMuted());
        painter.drawText(QRect(0, 0, width() - kPadX, height()),
                         Qt::AlignVCenter | Qt::AlignRight, myShortcut);
    }

    // Keyboard focus must be visible - a focus state nobody can see is an
    // accessibility defect, not a polish item. Drawn last, inset from the
    // checked-state border above rather than traced over it: a checked AND
    // focused chip must show both rings, not just one overdrawing the other.
    //
    // Deliberately window()->focusWidget() rather than hasFocus(): hasFocus()
    // (and QApplication::focusWidget()) answer for the whole application, and
    // stay false for every widget in a window that is not the OS-active one -
    // which this app's own test harness relies on (gui_smoke drives real
    // MainWindows shown with WA_ShowWithoutActivating specifically so it
    // never steals OS focus while it runs). setFocus() still updates this
    // top-level's own remembered focus child immediately, regardless of
    // activation, and that is what a chip's ring should reflect.
    //
    // Presence does not depend on window()->isActiveWindow() - only colour
    // and weight do. An earlier version added "&& isActiveWindow()" to the
    // condition above, which fixes the wrong problem: it makes the ring
    // disappear entirely (still correct for a user who moved to another
    // application) but also disappears while gui_smoke's own window, which
    // is never OS-activated by design, is the one under test - breaking the
    // very check this exists to satisfy. Painting a dimmer ring instead
    // solves both: it stops shouting focus at someone who has moved on
    // without ever going fully invisible.
    if (this == window()->focusWidget()) {
        const bool active = window()->isActiveWindow();
        const QColor ringColor = active ? Theme::focusRing() : Theme::focusRingMuted();
        const double ringWidth = active ? kFocusRingWidth : kFocusRingWidth - 0.5;
        QPainterPath ring;
        ring.addRoundedRect(rect().adjusted(2, 2, -2, -2), kRadius - 2, kRadius - 2);
        painter.setPen(QPen(ringColor, ringWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(ring);
    }
}

void ToolChip::enterEvent(QEnterEvent* /*event*/) { myHovered = true;  update(); }
void ToolChip::leaveEvent(QEvent* /*event*/)      { myHovered = false; update(); }

void ToolChip::focusInEvent(QFocusEvent* event)
{
    QAbstractButton::focusInEvent(event);
    update();
}

void ToolChip::focusOutEvent(QFocusEvent* event)
{
    QAbstractButton::focusOutEvent(event);
    update();
}
