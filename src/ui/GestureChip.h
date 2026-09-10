#pragma once
// The shared half of the gesture value chips - PullArrow's, BevelArrow's and
// MoveTool's cards (the branch review found the third near-verbatim copy and
// retired the duplication here). What is shared is EXACTLY what was
// triplicated byte for byte: the metrics all three agree on, the
// flip-beside-anchor placement with its clamps and device-pixel snap, the
// paintSurface frame with the danger outline option, and the label row.
//
// What deliberately STAYS per-widget is everything that genuinely differs -
// the middle row (a QLineEdit field on the arrows, a painted value on
// MoveTool), the hint row's own height and line count, and each card's
// width policy - because flattening those would change pixels the suite
// pins, not remove duplication.
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <algorithm>

namespace GestureChip {

// The metrics all three cards share (each card's own width policy and
// middle/hint rows stay its own - see the header comment).
constexpr int kPad = 10;
constexpr int kLabelHeight = 18;
constexpr int kHintGap = 4;
constexpr int kRadius = 8;
// How far the chip stands off its anchor's projected point, and how far it
// is kept inside the viewport's own edges.
constexpr int kChipGap = 18;
constexpr int kEdgeInset = 8;

// Beside the projected anchor `at`, flipped to the other side rather than
// clamped when that would run off the right edge - a value chip that walks
// away from the thing it labels stops labelling it (the three copies'
// shared comment). Clamped inside the viewport, then snapped to whole
// DEVICE pixels in the window's own coordinates - Theme's position rule,
// snapped LAST so it cannot push the card back outside the edges the
// clamps just brought it inside. Returns the top-left to move() to.
inline QPoint placeBeside(const QWidget* chip, const QWidget* viewport, const QPoint& at)
{
    int x = at.x() + kChipGap;
    if (x + chip->width() > viewport->width() - kEdgeInset)
        x = at.x() - kChipGap - chip->width();
    x = std::clamp(x, kEdgeInset,
                   std::max(kEdgeInset, viewport->width() - chip->width() - kEdgeInset));
    int y = at.y() - chip->height() / 2;
    y = std::clamp(y, kEdgeInset,
                   std::max(kEdgeInset, viewport->height() - chip->height() - kEdgeInset));
    const QPoint origin = viewport->mapTo(viewport->window(), QPoint(0, 0));
    const double dpr = chip->devicePixelRatioF();
    return QPoint(Theme::snapToDevicePixels(x, origin.x(), dpr),
                  Theme::snapToDevicePixels(y, origin.y(), dpr));
}

// The card's surface inside the (historical, now zero) shadow margin, with
// the danger outline ExtrudePreview established for an invalid value.
// Returns the body rect every row is laid out against.
inline QRect paintFrame(QPainter& painter, const QWidget* chip, bool invalid = false)
{
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = chip->rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, kRadius);
    if (invalid) {
        QPainterPath outline;
        outline.addRoundedRect(body, kRadius, kRadius);
        painter.setPen(QPen(Theme::danger(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }
    return body;
}

// The label row - the one row all three cards paint identically.
inline void paintLabel(QPainter& painter, const QRect& body, const QString& text)
{
    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2,
                           kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, text);
}

}  // namespace GestureChip
