#include "DragGhost.h"

#include "session/ItemMoveText.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace {
constexpr int kPadX = 8;
constexpr int kPadY = 5;
constexpr int kIconPx = 16;
constexpr int kGap = 6;
constexpr int kRadius = 6;
const QPoint kCursorOffset(14, 10);
int indexOf(DragGhost::Variant v) { return static_cast<int>(v); }
}  // namespace

DragGhost::DragGhost(QWidget* viewport) : QWidget(viewport)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    Theme::makeSurfaceTransparent(this);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void DragGhost::begin(IconSet::Glyph glyph, const QString& name, int extraCount)
{
    myGlyph = glyph;
    myLabel = extraCount > 0 ? tr("%1 +%2").arg(name).arg(extraCount) : name;
    for (Variant v : {Variant::Live, Variant::Refused, Variant::Cancel})
        myPixmaps[indexOf(v)] = rasterise(v);
    myRasterCount += 3;
    myVariant = Variant::Live;
    resize(myPixmaps[0].deviceIndependentSize().toSize());
    show();
    raise();
    update();
}

void DragGhost::setVariant(Variant variant)
{
    if (variant == myVariant) return;
    myVariant = variant;
    resize(myPixmaps[indexOf(variant)].deviceIndependentSize().toSize());
    update();
}

void DragGhost::follow(const QPoint& viewportPos)
{
    move(viewportPos + kCursorOffset);
    raise();
}

void DragGhost::end()
{
    hide();
}

QPixmap DragGhost::rasterise(Variant variant) const
{
    const QString text = variant == Variant::Cancel ? ItemMoveText::releaseToCancel() : myLabel;
    const QFont font = Theme::labelFont();
    const QFontMetrics metrics(font);
    const int width = kPadX * 2 + kIconPx + kGap + metrics.horizontalAdvance(text);
    const int height = std::max(kIconPx, metrics.height()) + kPadY * 2;
    const qreal ratio = devicePixelRatioF();
    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    Theme::paintSurface(p, QRect(0, 0, width, height), kRadius);
    const QColor ink = variant == Variant::Refused ? Theme::dangerText() : Theme::text();
    const IconSet::Glyph glyph = variant == Variant::Refused ? IconSet::Glyph::Warning : myGlyph;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.save();
    p.translate(kPadX, (height - kIconPx) / 2.0);
    p.scale(kIconPx / 24.0, kIconPx / 24.0);
    QPen pen(ink, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    IconSet::paintGlyph(p, glyph);
    p.restore();
    p.setFont(font);
    p.setPen(ink);
    p.drawText(QRect(kPadX + kIconPx + kGap, 0, width, height), Qt::AlignVCenter | Qt::AlignLeft,
               text);
    return pixmap;
}

void DragGhost::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.drawPixmap(0, 0, myPixmaps[indexOf(myVariant)]);
}
