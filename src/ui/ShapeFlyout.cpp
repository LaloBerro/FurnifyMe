#include "ShapeFlyout.h"

#include "IconSet.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kPad = 10;
constexpr int kRadius = 10;
constexpr int kTileWidth = 64;
constexpr int kTileHeight = 56;
constexpr int kTileGap = 8;

struct ShapeSpec {
    ModelingOps::PrimitiveKind kind;
    IconSet::Glyph glyph;
    const char* name;   // tr()'d at build time below
};

// The six, in the order the header's enum declares them - tileFor() indexes
// by that order, so the two cannot drift.
const ShapeSpec kShapes[] = {
    {ModelingOps::PrimitiveKind::Box, IconSet::Glyph::ShapeBox, "Box"},
    {ModelingOps::PrimitiveKind::Cylinder, IconSet::Glyph::ShapeCylinder, "Cylinder"},
    {ModelingOps::PrimitiveKind::Sphere, IconSet::Glyph::ShapeSphere, "Sphere"},
    {ModelingOps::PrimitiveKind::Cone, IconSet::Glyph::ShapeCone, "Cone"},
    {ModelingOps::PrimitiveKind::Wedge, IconSet::Glyph::ShapeWedge, "Wedge"},
    {ModelingOps::PrimitiveKind::Plank, IconSet::Glyph::ShapePlank, "Plank"},
};

// One tile: glyph over name, hover fill, painted through IconSet with
// state-dependent ink - WindowButtons' own route, for the same reason.
class ShapeTile : public QAbstractButton {
public:
    ShapeTile(const ShapeSpec& spec, QWidget* parent)
        : QAbstractButton(parent), myGlyph(spec.glyph)
    {
        setText(QCoreApplication::translate("ShapeFlyout", spec.name));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_NoSystemBackground);
        Theme::makeSurfaceTransparent(this);
        setFixedSize(Theme::wholeDevicePixels(QSize(kTileWidth, kTileHeight)));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool hot = underMouse() || isDown();
        if (hot) {
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), 8.0, 8.0);
            painter.fillPath(path, isDown() ? Theme::chipActive() : Theme::chipHover());
        }
        const QColor ink = hot ? Theme::accent() : Theme::textMuted();

        // The 24-grid glyph in a 22x22 box centred over the label.
        const double scale = 22.0 / 24.0;
        painter.save();
        painter.translate(width() / 2.0 - 11.0, 7.0);
        painter.scale(scale, scale);
        painter.setPen(QPen(ink, 1.5 / scale));
        painter.setBrush(Qt::NoBrush);
        IconSet::paintGlyph(painter, myGlyph);
        painter.restore();

        painter.setPen(hot ? Theme::text() : Theme::textMuted());
        painter.setFont(Theme::badgeFont());
        painter.drawText(QRect(0, height() - 18, width(), 16), Qt::AlignHCenter, text());
    }

private:
    IconSet::Glyph myGlyph;
};

}  // namespace

ShapeFlyout::ShapeFlyout(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // The floating-surface family's mouse rule - a press on this card's own
    // padding must never fall through to the viewport and re-pick.
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);
    outer->setSpacing(8);

    myCaption = new QLabel(tr("Add a shape"), this);
    myCaption->setAttribute(Qt::WA_TransparentForMouseEvents);
    outer->addWidget(myCaption);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kTileGap);
    grid->setVerticalSpacing(kTileGap);
    int index = 0;
    for (const ShapeSpec& spec : kShapes) {
        auto* tile = new ShapeTile(spec, this);
        const ModelingOps::PrimitiveKind kind = spec.kind;
        connect(tile, &QAbstractButton::clicked, this,
                [this, kind] { emit shapePicked(kind); });
        grid->addWidget(tile, index / 3, index % 3);
        myTiles.push_back(tile);
        ++index;
    }
    outer->addLayout(grid);

    setFixedSize(Theme::wholeDevicePixels(sizeHint()));
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ShapeFlyout::applyTheme);
    hide();
}

void ShapeFlyout::openAt(const QPoint& anchorTopRight)
{
    if (!parentWidget()) return;
    QPoint at = anchorTopRight;
    at.setX(std::min(at.x(), parentWidget()->width() - width() - 8));
    at.setY(std::clamp(at.y(), 8, std::max(8, parentWidget()->height() - height() - 8)));
    move(Theme::snapToDevicePixels(at.x(), 0, devicePixelRatioF()),
         Theme::snapToDevicePixels(at.y(), 0, devicePixelRatioF()));
    show();
    raise();
}

void ShapeFlyout::closeFlyout()
{
    hide();
}

QWidget* ShapeFlyout::tileFor(ModelingOps::PrimitiveKind kind) const
{
    const std::size_t index = static_cast<std::size_t>(kind);
    return index < myTiles.size() ? myTiles[index] : nullptr;
}

QStringList ShapeFlyout::paintedTexts() const
{
    QStringList texts;
    if (myCaption) texts << myCaption->text();
    for (QWidget* tile : myTiles) {
        if (auto* button = qobject_cast<QAbstractButton*>(tile)) texts << button->text();
    }
    return texts;
}

void ShapeFlyout::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kRadius);
    Theme::drawCrispBorder(painter, QRectF(rect()), Theme::border(), kRadius);
}

void ShapeFlyout::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    mySwallowNextRelease = false;
    QCoreApplication::instance()->installEventFilter(this);
}

void ShapeFlyout::hideEvent(QHideEvent* event)
{
    // The filter OUTLIVES visibility by exactly one release when an outside
    // press closed us - eventFilter() removes it after swallowing that
    // release. A hide from any other route (a pick, Escape, MainWindow's
    // environment close) has no pending release and drops it now.
    if (!mySwallowNextRelease) QCoreApplication::instance()->removeEventFilter(this);
    QWidget::hideEvent(event);
}

bool ShapeFlyout::eventFilter(QObject* watched, QEvent* event)
{
    if (mySwallowNextRelease) {
        if (event->type() == QEvent::MouseButtonRelease) {
            mySwallowNextRelease = false;
            QCoreApplication::instance()->removeEventFilter(this);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
    if (!isVisible()) return QObject::eventFilter(watched, event);

    switch (event->type()) {
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress: {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                if (event->type() == QEvent::KeyPress) closeFlyout();
                event->accept();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            // A press OUTSIDE closes - and its release is swallowed too, or
            // the viewport under it would pick on the release (ShortcutSheet's
            // own finding, kept as a law here).
            QWidget* w = qobject_cast<QWidget*>(watched);
            if (w && (w == this || isAncestorOf(w))) break;
            if (event->type() == QEvent::MouseButtonPress) {
                mySwallowNextRelease = true;
                hide();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void ShapeFlyout::applyTheme()
{
    if (myCaption) {
        myCaption->setStyleSheet(
            QStringLiteral("background: transparent; color: %1; font-size: %2pt; "
                           "font-weight: 600;")
                .arg(Theme::textMuted().name())
                .arg(Theme::labelFont().pointSizeF()));
    }
    update();
    for (QWidget* tile : myTiles) tile->update();
}
