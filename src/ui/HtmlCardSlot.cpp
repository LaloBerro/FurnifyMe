#include "HtmlCardSlot.h"

#include "Theme.h"

#include <algorithm>

HtmlCardSlot::HtmlCardSlot(const QString& name, QSize size, QWidget* viewport)
    : QWidget(viewport), myName(name)
{
    setObjectName(QStringLiteral("htmlCardSlot_") + name);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    Theme::makeSurfaceTransparent(this);
    setFixedSize(Theme::wholeDevicePixels(size));
}

void HtmlCardSlot::setContentHeight(int logicalHeight)
{
    const int h = Theme::wholeDevicePixels(std::max(1, logicalHeight));
    if (h == height()) return;
    setFixedHeight(h);
    emit contentHeightChanged();
}

void HtmlCardSlot::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    emit placed();
}

void HtmlCardSlot::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit placed();
}

void HtmlCardSlot::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    emit placed();
}

void HtmlCardSlot::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit placed();
}
