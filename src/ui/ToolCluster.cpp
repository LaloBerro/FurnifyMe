#include "ToolCluster.h"

#include "Theme.h"
#include "ToolChip.h"

#include <QPainter>
#include <QVBoxLayout>

namespace {
// The gap between chip bodies is not the layout's spacing any more. Each
// chip's widget box is bigger than what it paints - it reserves
// Theme::surfaceShadowMargin() of shadow-only space on every side - so the
// PAINTED gap between two stacked chips is always
// spacing + 2 * surfaceShadowMargin(), and only the painted gap is anything
// a user can see.
//
// This file used to set a NEGATIVE spacing to cancel those two margins and
// land the painted gap back on the mockup's 3px. That never took effect and
// the comment claiming it did was wrong: QLayout::spacing() treats any
// negative insideSpacing as "unset" and returns the style's smart spacing
// instead, so the layout ran at the Windows style's 6px throughout, and the
// painted gap has been 12px since the chips grew their margin. Measured, not
// reasoned about: the rail's sizeHint came back 96px taller than its
// contents at spacing 0.
//
// A layout cannot express a negative gap at all, so 3px is simply not
// reachable through one; zero spacing is the tightest it goes, and the two
// shadow margins meeting give a 6px painted gap. That is the value the rail
// is built and captured against.
constexpr int kSpacing = 0;

// A separator's own height, and with it the whole painted band between the
// two chip bodies it divides: (shadow) + kSpacing + kSeparatorHeight +
// kSpacing + (shadow) = 13px, with the rule on the middle row - about 6px of
// air, one hairline, about 6px of air. Odd on purpose: height()/2.0 must
// land on a half-integer for the 1px rule to paint crisply on one row (see
// paintEvent below).
constexpr int kSeparatorHeight = 7;
// How far the rule stops short of the chip bodies it divides, so it reads as
// a group divider rather than a full-width cut across the rail.
constexpr int kSeparatorInset = 7;

// The card's own padding: how far the panel's edge stands off the chip
// bodies it holds. The chips add their own surfaceShadowMargin() inside it,
// so the visible inset from the rail's edge to a chip's body is this plus
// that - 8px.
constexpr int kCardPad = 5;
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
        // Half-integer centre for the same reason the app bar's bottom rule
        // needed one (Task 2): an antialiased 1px pen is centred on the
        // coordinate it is given, so an INTEGER y splits the rule across two
        // rows at half intensity and reads as a smudge instead of a line.
        // kSeparatorHeight is odd precisely so height()/2.0 lands on one.
        const double y = height() / 2.0;
        const int inset = Theme::surfaceShadowMargin() + kSeparatorInset;
        painter.setPen(QPen(Theme::border(), 1.0));
        painter.drawLine(QPointF(inset, y), QPointF(width() - inset, y));
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
    // child widget is not transparent - it is whatever the driver left
    // there, which reads as a solid black band down the viewport (caught in
    // the first capture of this task, green suite and all). Painting the
    // family's own card is the fix, and it is also what the design asks for.
    myLayout = new QVBoxLayout(this);
    myLayout->setContentsMargins(kCardPad, kCardPad, kCardPad, kCardPad);
    myLayout->setSpacing(kSpacing);
    myLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void ToolCluster::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The card fills this widget's ENTIRE bounds - it does NOT reserve
    // Theme::surfaceShadowMargin() the way a chip or the guide does, and
    // that is the one place this surface departs from the family.
    //
    // A painted shadow needs something behind it to darken. A chip has one:
    // it sits on this cluster's card, so its own margin composites over
    // panel() and the shadow reads correctly. A widget sitting DIRECTLY on
    // OCCT's on-screen GL surface has nothing behind it in its backing
    // store, so the semi-transparent black rings land on black and the
    // reserved margin renders as a hard black frame. On a 40px chip that is
    // a 3px halo nobody has picked out; on a rail spanning the whole
    // viewport it was a black rule down the left edge of the app, plainly
    // visible in this task's first capture. Filling the whole rect is what
    // removes it. The rounded corners still leave four small unpainted nubs
    // - every rounded card over this viewport has those, and they are dark
    // against a dark card rather than dark against the mid-grey grid.
    //
    // The half-pixel translate is the app bar's bottom-rule lesson again: an
    // antialiased 1px pen is centred on the path it is given, so a path on
    // integer edges splits the border across two columns at half intensity -
    // measured at #2e2e33 and #2a2a2f down the two sides instead of one
    // column of border(). Offset by half a pixel the stroke lands on exactly
    // the outermost column, and the fill it half-uncovers there is the same
    // half the stroke covers.
    painter.translate(0.5, 0.5);
    Theme::paintSurface(painter, rect().adjusted(0, 0, -1, -1), kCardRadius);
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
