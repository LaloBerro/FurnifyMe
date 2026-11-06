#pragma once

// THE DRAWERS' SIZES, now that the Qt drawers are gone (migration Phase 6).
// The pages draw the Settings, Versions, Joints and Items cards; their slots
// (HtmlCardSlot) still stand in ViewportOverlay at a size, and that size is
// still the one each Qt drawer measured: a base designed at the shipped type
// scale, grown by what a larger scale costs a specimen line, so the card
// neither jumps when the Settings drawer opens and closes nor squeezes a
// larger type. One function per card, the Qt drawers' own formulas.

#include <QSize>

namespace DrawerWidths {
int versions();
int joints();
int items();
// The Settings card: a fixed 296 x 380, through whole device pixels.
QSize settings();
}   // namespace DrawerWidths
