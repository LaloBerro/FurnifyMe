#include "DropIndicator.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

DropIndicator::DropIndicator(QWidget* host) : QWidget(host)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral("background: transparent;"));
    hide();
}

void DropIndicator::showLine(int y, int left, int right, bool refused)
{
    myShape = Shape::Line;
    myRefused = refused;
    const int half = kRingRadiusPx + 1;
    setGeometry(left - half, y - half, std::max(1, right - left + half + 1), half * 2 + 1);
    show();
    raise();
    update();
}

void DropIndicator::showRing(const QRect& rowRect, bool refused)
{
    myShape = Shape::Ring;
    myRefused = refused;
    setGeometry(rowRect.adjusted(-1, -1, 1, 1));
    show();
    raise();
    update();
}

void DropIndicator::clear()
{
    myShape = Shape::None;
    hide();
}

void DropIndicator::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    // Read at paint, never cached: a theme edit must reach a live drag.
    const QColor ink = myRefused ? Theme::dangerText() : Theme::accentText();
    if (myShape == Shape::Line) {
        const int half = kRingRadiusPx + 1;
        // The line itself is a filled rect on whole pixels, so its middle is
        // the exact token - which is what the suite's pixel checks read.
        p.fillRect(QRect(half * 2, half - kLineWidthPx / 2, width() - half * 2, kLineWidthPx), ink);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(ink, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(half, half), kRingRadiusPx, kRingRadiusPx);
    } else if (myShape == Shape::Ring) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath ring;
        ring.addRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 4.0, 4.0);
        p.strokePath(ring, QPen(ink, 2.0));
    }
}
