#include "IconSet.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>

namespace IconSet {

// All glyphs are drawn on a 24x24 grid and scaled by the pixmap size.
// Outside the anonymous namespace since Milestone 5's window controls -
// see the header: it draws with the caller's current pen.
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
        // THE THREE BOOLEANS, told apart by SHAPE rather than by line style.
        //
        // They used to be two solid squares, one solid and one dashed, and
        // two solid with a small filled patch between - all three genuinely
        // different in the code and all three near-identical on the rail,
        // where the glyph is 24 px and the pen is one pixel wide: a dashed
        // rectangle at that size is a slightly fainter rectangle, and a 5x7
        // fill reads as a smudge. Magnifying the rail is what showed it; at
        // 1:1 they simply looked like three copies of one icon.
        //
        // Now the RESULT of each operation is the shape drawn, which is the
        // one thing that cannot be confused: one blob, a notched piece beside
        // a ghost, a lone lens.
        case Glyph::Fuse: {                       // ONE outline round both
            QPainterPath a;
            a.addRect(4, 8, 11, 11);
            QPainterPath b;
            b.addRect(10, 4, 11, 11);
            // United, so there is no seam through the middle: a Union makes
            // one piece, and the icon is one piece.
            p.drawPath(a.united(b));
            break;
        }
        case Glyph::Cut: {                        // the base, with a real bite out
            QPainterPath a;
            a.addRect(4, 8, 11, 11);
            QPainterPath b;
            b.addRect(10, 4, 11, 11);
            p.drawPath(a.subtracted(b));
            // The tool that took it, as a ghost. Copy the pen and change only
            // its style: p.setPen(Qt::DotLine) would build a fresh black pen
            // and lose the theme colour entirely.
            QPen ghost = p.pen();
            ghost.setStyle(Qt::DotLine);
            p.setPen(ghost);
            p.drawRect(10, 4, 11, 11);
            break;
        }
        case Glyph::Intersect: {                  // the shared lens alone, solid
            QPainterPath a;
            a.addRect(4, 8, 11, 11);
            QPainterPath b;
            b.addRect(10, 4, 11, 11);
            const QPen solid = p.pen();
            QPen ghost = p.pen();
            ghost.setStyle(Qt::DotLine);
            p.setPen(ghost);
            p.drawRect(4, 8, 11, 11);
            p.drawRect(10, 4, 11, 11);
            p.setPen(solid);
            const QPainterPath lens = a.intersected(b);
            p.fillPath(lens, QBrush(solid.color()));
            p.drawPath(lens);
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
        case Glyph::Body:                         // filled cube
            p.drawRect(6, 6, 12, 12);
            p.fillRect(QRect(9, 9, 6, 6), QBrush(p.pen().color()));
            break;
        case Glyph::Camera: {                     // a camera body, lens and shutter button
            p.drawRoundedRect(QRectF(3.5, 8.0, 17.0, 12.0), 2.0, 2.0);
            p.drawLine(8, 8, 10, 5);
            p.drawLine(10, 5, 15, 5);
            p.drawLine(15, 5, 17, 8);
            p.drawEllipse(QPoint(12, 14), 4, 4);
            p.drawEllipse(QPoint(18, 10), 1, 1);
            break;
        }
        case Glyph::Wireframe:                    // half-shaded circle
            p.drawEllipse(QPoint(12, 12), 8, 8);
            p.drawLine(12, 4, 12, 20);
            break;
        case Glyph::ChevronRight:                 // a collapsed folder's twisty
            p.drawLine(10, 6, 16, 12);
            p.drawLine(16, 12, 10, 18);
            break;
        case Glyph::ChevronDown:                  // an open folder's twisty
            p.drawLine(6, 10, 12, 16);
            p.drawLine(12, 16, 18, 10);
            break;
        case Glyph::Folder:                       // a tabbed folder outline
            p.drawLine(3, 7, 9, 7);
            p.drawLine(9, 7, 11, 10);
            p.drawLine(11, 10, 21, 10);
            p.drawLine(21, 10, 21, 19);
            p.drawLine(21, 19, 3, 19);
            p.drawLine(3, 19, 3, 7);
            break;
        case Glyph::FitAll:                       // frame corners
            p.drawLine(4, 8, 4, 4);  p.drawLine(4, 4, 8, 4);
            p.drawLine(16, 4, 20, 4); p.drawLine(20, 4, 20, 8);
            p.drawLine(20, 16, 20, 20); p.drawLine(20, 20, 16, 20);
            p.drawLine(8, 20, 4, 20); p.drawLine(4, 20, 4, 16);
            break;
        case Glyph::Projection:                   // a perspective frustum
            p.drawLine(8, 6, 16, 6);
            p.drawLine(16, 6, 20, 19);
            p.drawLine(20, 19, 4, 19);
            p.drawLine(4, 19, 8, 6);
            break;
        case Glyph::Shapes:                       // cube + a small plus
            p.drawLine(3, 8, 10, 4);
            p.drawLine(10, 4, 17, 8);
            p.drawLine(17, 8, 17, 15);
            p.drawLine(17, 15, 10, 19);
            p.drawLine(10, 19, 3, 15);
            p.drawLine(3, 15, 3, 8);
            p.drawLine(3, 8, 10, 12);
            p.drawLine(10, 12, 17, 8);
            p.drawLine(10, 12, 10, 19);
            p.drawLine(20, 15, 20, 21);
            p.drawLine(17, 18, 23, 18);
            break;
        case Glyph::ShapeBox:                     // the primitive cube
            p.drawLine(4, 8, 12, 4);
            p.drawLine(12, 4, 20, 8);
            p.drawLine(20, 8, 20, 16);
            p.drawLine(20, 16, 12, 20);
            p.drawLine(12, 20, 4, 16);
            p.drawLine(4, 16, 4, 8);
            p.drawLine(4, 8, 12, 12);
            p.drawLine(12, 12, 20, 8);
            p.drawLine(12, 12, 12, 20);
            break;
        case Glyph::ShapeCylinder: {
            p.drawEllipse(QRectF(5.0, 3.0, 14.0, 6.0));
            p.drawLine(5, 6, 5, 18);
            p.drawLine(19, 6, 19, 18);
            p.drawArc(QRectF(5.0, 15.0, 14.0, 6.0), 180 * 16, 180 * 16);
            break;
        }
        case Glyph::ShapeSphere:
            p.drawEllipse(QRectF(4.0, 4.0, 16.0, 16.0));
            p.drawArc(QRectF(4.0, 9.0, 16.0, 6.0), 180 * 16, 180 * 16);
            break;
        case Glyph::ShapeCone:
            p.drawLine(12, 4, 19, 17);
            p.drawLine(12, 4, 5, 17);
            p.drawArc(QRectF(5.0, 14.0, 14.0, 6.0), 180 * 16, 180 * 16);
            break;
        case Glyph::ShapeWedge:                   // the ramp
            p.drawLine(4, 18, 20, 18);
            p.drawLine(4, 18, 4, 7);
            p.drawLine(4, 7, 20, 18);
            p.drawLine(4, 7, 9, 5);
            p.drawLine(9, 5, 22, 15);
            p.drawLine(22, 15, 20, 18);
            break;
        case Glyph::ShapePlank:                   // a thin board
            p.drawLine(3, 13, 12, 9);
            p.drawLine(12, 9, 21, 13);
            p.drawLine(21, 13, 12, 17);
            p.drawLine(12, 17, 3, 13);
            p.drawLine(3, 13, 3, 15);
            p.drawLine(3, 15, 12, 19);
            p.drawLine(12, 19, 21, 15);
            p.drawLine(21, 15, 21, 13);
            p.drawLine(12, 17, 12, 19);
            break;
        case Glyph::Minimize:                     // a single baseline
            p.drawLine(5, 12, 19, 12);
            break;
        case Glyph::Maximize:                     // an empty frame
            p.drawRect(5, 5, 14, 14);
            break;
        case Glyph::Restore:                      // two offset frames
            p.drawLine(8, 5, 19, 5);
            p.drawLine(19, 5, 19, 16);
            p.drawRect(5, 8, 11, 11);
            break;
        case Glyph::Close:                        // the X
            p.drawLine(5, 5, 19, 19);
            p.drawLine(5, 19, 19, 5);
            break;
    }
}

namespace {

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

QPixmap appIconPixmap(int px)
{
    QPixmap pixmap(px, px);
    // Transparent outside the tile, so the rounded corners read as rounded
    // wherever the OS paints it. This is the one painted surface in the project
    // that MAY carry alpha: it is never composited over OCCT's GL surface -
    // Windows draws it, in its own title bar and its own taskbar - so the
    // opaque-family rule that governs every widget does not reach here.
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Everything below is expressed on the same 24x24 grid the glyphs use, so
    // one number changes the mark at every size it is ever asked for.
    painter.scale(px / 24.0, px / 24.0);

    // The tile: the shell's own panel colour with the family's border, at the
    // rail's and the drawer's radius rather than the glyph grid's - an icon is
    // a card, and this is the card family's corner.
    const QRectF tile(0.5, 0.5, 23.0, 23.0);
    QPainterPath card;
    card.addRoundedRect(tile, 5.0, 5.0);
    painter.fillPath(card, Theme::panel());
    QPen edge(Theme::border());
    edge.setWidthF(1.0);
    painter.setPen(edge);
    painter.drawPath(card);

    // The mark: U+25B0 BLACK PARALLELOGRAM as geometry - the same shape the app
    // bar paints in accent() at the head of the wordmark. Leaning right, wider
    // than tall, centred in the tile.
    QPainterPath mark;
    mark.moveTo(9.0, 7.5);
    mark.lineTo(19.0, 7.5);
    mark.lineTo(15.0, 16.5);
    mark.lineTo(5.0, 16.5);
    mark.closeSubpath();
    painter.fillPath(mark, Theme::accent());

    return pixmap;
}

QIcon appIcon()
{
    // The user's own artwork (assets/Icon.png, Milestone 5 item 1), bundled
    // through the same resource system the font uses. Scaled per size by Qt
    // from the 2000px original - at these target sizes a high-quality
    // downscale of real artwork beats a painted glyph.
    //
    // Built ONCE per process (the modeling-lag investigation's ledgered
    // sibling of the appMarkPixmap() defect it fixed): decoding and
    // smooth-scaling the 2000px source seven times costs real milliseconds,
    // and every MainWindow/SelectorWindow construction - including each
    // editor<->selector handoff - was paying it. A QIcon is cheap to copy;
    // the artwork in the binary cannot change mid-run, so a function-local
    // static is the honest lifetime.
    static const QIcon cached = [] {
        const QPixmap art(QStringLiteral(":/icons/app.png"));
        QIcon result;
        if (!art.isNull()) {
            for (int size : {16, 24, 32, 48, 64, 128, 256})
                result.addPixmap(
                    art.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return result;
        }
        // Fallback only - the painted tile from before the artwork existed,
        // kept so a broken resource build still shows SOMETHING in the title
        // bar.
        for (int size : {16, 24, 32, 48, 64, 128, 256})
            result.addPixmap(appIconPixmap(size));
        return result;
    }();
    return cached;
}

QPixmap appMarkPixmap(int px)
{
    // CACHED, and the cache is the whole point of this function's shape.
    //
    // AppBar::paintEvent() calls this on every repaint, and the artwork behind
    // ":/icons/app.png" is 2000x2000 - so the uncached form decoded two
    // thousand rows of PNG and ran a SmoothTransformation downscale of them to
    // 20x20, per frame. Measured at 17.9 ms for one AppBar paint against 0.02
    // ms for a ToolChip and 0.08 ms for the whole axis gizmo.
    //
    // That is not a bar-only cost, which is why it was worth a measurement to
    // find. The viewport is a QOpenGLWidget - a TEXTURE widget - and Qt cannot
    // partially update a window that holds one: the moment ANY raster overlay
    // child is dirty, every visible overlay widget in the window repaints, the
    // app bar included. The axis gizmo repaints on every cameraChanged, so an
    // orbit drag paid this once per frame and modeling ran at 21 ms/frame
    // against render mode's 4 ms - the heavier mode being the smoother one,
    // which is exactly the report that started this.
    //
    // QPixmapCache rather than a function-local static: it is cleared by Qt at
    // shutdown, so no QPixmap outlives the QGuiApplication that must exist to
    // hold one. A handful of sizes at most ever land in it.
    const QString key = QStringLiteral("furnifyme:appmark:%1").arg(px);
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) return cached;

    const QPixmap art(QStringLiteral(":/icons/app.png"));
    const QPixmap mark =
        art.isNull() ? appIconPixmap(px)
                     : art.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmapCache::insert(key, mark);
    return mark;
}

}  // namespace IconSet
