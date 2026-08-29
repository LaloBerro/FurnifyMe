#include "ToolCluster.h"

#include "Theme.h"
#include "ToolChip.h"

#include <QVBoxLayout>

namespace {
// The mockup's painted gap between chip bodies, in pixels - what the layout
// spacing controlled directly before chips grew a Theme::surfaceShadowMargin()
// margin on every side. Now each chip's own widget bounding box is bigger
// than what it paints, so a widget-to-widget gap of `spacing` reads on
// screen as spacing + 2*surfaceShadowMargin() between the actual painted
// bodies (surfaceShadowMargin() worth of empty, shadow-only margin
// contributed by each of the two neighbouring chips). Negative spacing here
// is deliberate and correct, not a mistake: it pulls the bounding boxes back
// together by exactly the margin both chips added, so the PAINTED gap - the
// only thing anyone looking at the app actually sees - lands on the
// mockup's value again. The two chips' shadow rings do overlap in that
// reclaimed strip, but neither chip paints anything opaque there, so the
// only effect is the two faint shadows compositing into one, not a visible
// seam or clipped content - confirmed in the fix-round crop.
constexpr int kPaintedGap = 3;
}  // namespace

ToolCluster::ToolCluster(QWidget* parent)
    : QWidget(parent)
{
    // No background of its own: the chips carry the visuals and the gaps
    // between them show the 3D view through.
    setAttribute(Qt::WA_NoSystemBackground);
    myLayout = new QVBoxLayout(this);
    myLayout->setContentsMargins(0, 0, 0, 0);
    myLayout->setSpacing(kPaintedGap - 2 * Theme::surfaceShadowMargin());
    myLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void ToolCluster::addChip(ToolChip* chip)
{
    chip->setParent(this);
    myLayout->addWidget(chip);
    adjustSize();
}
