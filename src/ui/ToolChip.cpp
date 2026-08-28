#include "ToolChip.h"

#include "Theme.h"

#include <QAction>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kIcon = 16;
constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kGap = 8;
constexpr int kRadius = 6;
}  // namespace

ToolChip::ToolChip(QAction* action, IconSet::Glyph glyph, QWidget* parent)
    : QAbstractButton(parent)
    , myAction(action)
    , myGlyph(glyph)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setIcon(IconSet::icon(glyph));

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
    const QFontMetrics metrics(font());
    int width = kPadX + kIcon + kGap + metrics.horizontalAdvance(text()) + kPadX;
    if (!myShortcut.isEmpty()) width += kGap + metrics.horizontalAdvance(myShortcut) + 8;
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

    painter.setPen(isEnabled() ? Theme::text() : Theme::textDisabled());
    const int textLeft = kPadX + kIcon + kGap;
    painter.drawText(QRect(textLeft, 0, width() - textLeft - kPadX, height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text());

    if (!myShortcut.isEmpty()) {
        painter.setPen(Theme::textMuted());
        painter.drawText(QRect(0, 0, width() - kPadX, height()),
                         Qt::AlignVCenter | Qt::AlignRight, myShortcut);
    }
}

void ToolChip::enterEvent(QEnterEvent* /*event*/) { myHovered = true;  update(); }
void ToolChip::leaveEvent(QEvent* /*event*/)      { myHovered = false; update(); }
