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
    myNatural = Theme::wholeDevicePixels(size);
    setFixedSize(myNatural);
}

void HtmlCardSlot::applyNatural()
{
    if (myStretchable) {
        setMinimumSize(myNatural);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        resize(std::max(width(), myNatural.width()), std::max(height(), myNatural.height()));
    } else {
        setFixedSize(myNatural);
    }
    updateGeometry();
}

void HtmlCardSlot::setContentHeight(int logicalHeight)
{
    const int h = Theme::wholeDevicePixels(std::max(1, logicalHeight));
    if (h == myNatural.height() && h == height()) return;
    // The width stays whatever it is now - a drawer's width follows the type
    // scale through setFixedWidth(), which a height report must not undo.
    if (!myStretchable) myNatural.setWidth(width());
    myNatural.setHeight(h);
    applyNatural();
    emit contentHeightChanged();
}

void HtmlCardSlot::setContentSize(QSize logicalSize)
{
    const QSize s = Theme::wholeDevicePixels(QSize(std::max(1, logicalSize.width()), std::max(1, logicalSize.height())));
    if (s == myNatural && s == size()) return;
    myNatural = s;
    applyNatural();
    emit contentHeightChanged();
}

void HtmlCardSlot::setStretchable(bool stretchable)
{
    if (stretchable == myStretchable) return;
    myStretchable = stretchable;
    applyNatural();
}

void HtmlCardSlot::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    emit placed();
}

void HtmlCardSlot::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // A fixed slot's size IS its natural size (setFixedWidth() included); a
    // stretchable one keeps the natural size it was given, so the stretch
    // never becomes the minimum.
    if (!myStretchable) myNatural = size();
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
