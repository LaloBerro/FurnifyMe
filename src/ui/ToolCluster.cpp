#include "ToolCluster.h"

#include "Theme.h"
#include "ToolChip.h"

#include <QPainter>
#include <QVBoxLayout>

namespace {
// The painted gap between two stacked chip bodies, in pixels - and now
// literally the layout's spacing, because a chip's widget rect and its
// painted card are the same rectangle again (Theme::surfaceShadowMargin() is
// zero; there is no shadow to reserve room for).
//
// The history matters, because this number has never been what the source
// said it was. This file used to set spacing to
// `kPaintedGap - 2 * surfaceShadowMargin()` = -3, to cancel the margin each
// neighbouring chip reserved, and its comment claimed a magnified crop had
// confirmed the 3px result. It had not, and could not have:
// QLayout::spacing() treats any negative insideSpacing as "unset" and
// returns the style's smart spacing instead, so the layout silently ran at
// the Windows style's 6px and the painted gap was 12px throughout. Measured,
// not reasoned about - the rail's sizeHint came back 96px taller than its
// contents. gui_smoke now samples the rows between two adjacent rail buttons
// and asserts exactly this many of them are card background, so the claim is
// checked rather than asserted in prose.
constexpr int kPaintedGap = 3;

// A separator's own height, and with it the whole painted band between the
// two chip bodies it divides: kPaintedGap + kSeparatorHeight + kPaintedGap =
// 13px, with the rule on its middle row - 6px of air, one hairline, 6px of
// air. Odd on purpose: height()/2.0 must land on a half-integer for the 1px
// rule to fill exactly one row.
constexpr int kSeparatorHeight = 7;
// How far the rule stops short of the chip bodies it divides, so it reads as
// a group divider rather than a full-width cut across the rail.
constexpr int kSeparatorInset = 7;

// The card's own padding: how far the panel's edge stands off the chip
// bodies it holds.
constexpr int kCardPad = 8;
constexpr int kCardRadius = 10;

// Decoration only. It carries WA_TransparentForMouseEvents so a click in the
// band between two groups clicks through to the viewport, the same as the
// gaps between chips do - CLAUDE.md's warning about that attribute is about
// interactive controls nested UNDER a transparent parent, and this widget has
// no children at all.
class Separator : public QWidget {
public:
    explicit Separator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(kSeparatorHeight);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        // Theme::drawCrispRule owns the half-pixel snap that keeps this one
        // row of border() rather than two rows at half intensity.
        const double y = height() / 2.0;
        Theme::drawCrispRule(painter, QPointF(kSeparatorInset, y),
                             QPointF(width() - kSeparatorInset, y), Theme::border());
    }
};
}  // namespace

ToolCluster::ToolCluster(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // A cluster IS a floating surface, one of the family the design lists
    // alongside the drawer, the guide, the balloon and the toast - it does
    // not merely hold widgets that are. That was invisible while a cluster
    // was exactly as big as the chips packed into it, because there was no
    // uncovered pixel to see; the rail, stretched to the viewport's full
    // height with slack between the select group and history, has hundreds
    // of them. Over OCCT's on-screen GL surface an unpainted region of a
    // child widget used to be black rather than transparent, back when the
    // viewport was a native window Qt's own backing store knew nothing
    // about (caught in the first capture of the task that added this card,
    // green suite and all) - painting the family's own card was the fix.
    // See Theme::makeSurfaceTransparent()'s own comment for what stands in
    // for that fill's job now.
    Theme::makeSurfaceTransparent(this);
    myLayout = new QVBoxLayout(this);
    myLayout->setContentsMargins(kCardPad, kCardPad, kCardPad, kCardPad);
    myLayout->setSpacing(kPaintedGap);
    myLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void ToolCluster::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    // The rounded panel and its crisp border, both opaque, no reserved
    // margin and no shadow - Theme::paintSurface() owns all of it, including
    // the crisp-border alignment this file used to do for itself with a
    // local half-pixel translate. Outside the rounded shape, nothing is
    // painted here at all: the corners genuinely composite through to the
    // live scene the rail floats on, kept from an opaque QSS stamp by
    // Theme::makeSurfaceTransparent() in the constructor.
    Theme::paintSurface(painter, rect(), kCardRadius);
}

void ToolCluster::addChip(ToolChip* chip)
{
    chip->setParent(this);
    myLayout->addWidget(chip);
    myChips.push_back(chip);
    adjustSize();
}

void ToolCluster::addSeparator()
{
    myLayout->addWidget(new Separator(this));
    adjustSize();
}

void ToolCluster::addStretch()
{
    // SetFixedSize pins the widget to sizeHint() in both directions, which
    // would leave a stretch item with nothing to distribute - the cluster
    // would still be exactly as tall as its chips and the "far end" the
    // stretch pushes to would be the last chip's bottom edge. SetMinimumSize
    // keeps the cluster from ever being squeezed below its contents while
    // letting its container (ViewportOverlay's LeftEdge anchor) give it the
    // viewport's full height, which is where the slack comes from.
    //
    // Clearing the explicit bounds first is not tidiness: SetFixedSize
    // applies itself by calling setFixedSize(), which sets the widget's
    // minimum AND maximum, and changing the constraint afterwards only ever
    // re-writes the minimum. The stale maximum survives, and it is a
    // maximum recorded before these last two chips were even added - so the
    // rail was silently capped shorter than its own contents and could not
    // be stretched to the viewport at all.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    myLayout->setSizeConstraint(QLayout::SetMinimumSize);
    myLayout->addStretch(1);
    adjustSize();
}
