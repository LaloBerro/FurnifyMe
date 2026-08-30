#include "IconSet.h"

#include "Theme.h"

#include <QPainter>
#include <QPixmap>

namespace IconSet {
namespace {

// All glyphs are drawn on a 24x24 grid and scaled by the pixmap size.
void paintGlyph(QPainter& p, Glyph glyph)
{
    switch (glyph) {
        case Glyph::Sketch:                       // pencil over a baseline
            p.drawLine(4, 20, 20, 20);
            p.drawLine(6, 17, 15, 5);
            p.drawLine(15, 5, 18, 8);
            p.drawLine(18, 8, 9, 20);
            break;
        case Glyph::Extrude:                      // face with an up arrow
            p.drawRect(5, 14, 14, 6);
            p.drawLine(12, 12, 12, 4);
            p.drawLine(12, 4, 9, 7);
            p.drawLine(12, 4, 15, 7);
            break;
        case Glyph::Fuse:                         // two overlapping squares
            p.drawRect(4, 8, 11, 11);
            p.drawRect(10, 4, 11, 11);
            break;
        case Glyph::Cut: {                        // square with a bite removed
            p.drawRect(4, 8, 11, 11);
            // Copy the pen and change only its style: p.setPen(Qt::DashLine) would
            // build a fresh black pen and lose the theme colour entirely.
            QPen dashed = p.pen();
            dashed.setStyle(Qt::DashLine);
            p.setPen(dashed);
            p.drawRect(10, 4, 11, 11);
            break;
        }
        case Glyph::Intersect: {                  // the shared region filled
            p.drawRect(4, 8, 11, 11);
            p.drawRect(10, 4, 11, 11);
            const QBrush brush = p.pen().color();
            p.fillRect(QRect(10, 8, 5, 7), brush);
            break;
        }
        case Glyph::Delete:                       // bin
            p.drawLine(4, 7, 20, 7);
            p.drawRect(7, 7, 10, 13);
            p.drawLine(10, 4, 14, 4);
            break;
        case Glyph::Undo:                         // arrow curving left
            p.drawArc(QRect(5, 7, 14, 12), 30 * 16, 150 * 16);
            p.drawLine(5, 13, 5, 8);
            p.drawLine(5, 8, 10, 10);
            break;
        case Glyph::Redo:                         // arrow curving right
            p.drawArc(QRect(5, 7, 14, 12), 0 * 16, 150 * 16);
            p.drawLine(19, 13, 19, 8);
            p.drawLine(19, 8, 14, 10);
            break;
        case Glyph::Items:                        // stacked list rows
            p.drawLine(5, 7, 19, 7);
            p.drawLine(5, 12, 19, 12);
            p.drawLine(5, 17, 19, 17);
            break;
        case Glyph::Snap:                         // grid with a marked node
            for (int i = 5; i <= 19; i += 7) {
                p.drawLine(i, 5, i, 19);
                p.drawLine(5, i, 19, i);
            }
            p.drawEllipse(QPoint(12, 12), 2, 2);
            break;
        case Glyph::SelectSolid:                  // filled cube
            p.drawRect(6, 6, 12, 12);
            p.fillRect(QRect(9, 9, 6, 6), QBrush(p.pen().color()));
            break;
        case Glyph::SelectFace:                   // cube with one face marked
            p.drawRect(6, 6, 12, 12);
            p.drawLine(6, 12, 18, 12);
            p.fillRect(QRect(7, 7, 10, 4), QBrush(p.pen().color()));
            break;
        case Glyph::SelectEdge: {                 // cube with one edge thickened
            p.drawRect(6, 6, 12, 12);
            QPen thick = p.pen();
            thick.setWidthF(3.4);
            p.setPen(thick);
            p.drawLine(6, 6, 18, 6);
            break;
        }
    }
}

QPixmap render(Glyph glyph, const QColor& colour, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / 24.0, size / 24.0);

    QPen pen(colour);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    paintGlyph(painter, glyph);
    return pixmap;
}

}  // namespace

QIcon icon(Glyph glyph)
{
    QIcon result;
    for (int size : {16, 24, 32, 48}) {
        result.addPixmap(render(glyph, Theme::text(), size), QIcon::Normal);
        result.addPixmap(render(glyph, Theme::textDisabled(), size), QIcon::Disabled);
    }
    return result;
}

}  // namespace IconSet
