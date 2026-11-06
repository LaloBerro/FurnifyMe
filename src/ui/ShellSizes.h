#pragma once
// THE SHELL'S SLOTS BEFORE THE PAGE MEASURES THEM (migration Phase 7b). The
// pill, the rail, the view cluster, the window buttons and render mode's Back
// chip are page cards since Phase 3i/7a, and since 7b their Qt widgets are
// gone - so each slot starts at the size its Qt widget had, and takes the
// page's own measurement the moment the page reports one. The numbers are the
// deleted widgets' own (ToolCluster, ToolChip, AppBar, WindowButtons), kept
// here so the viewport's minimum size and the overlay's stacking mean what
// they always did in a window with no page (gui_smoke, a page reloading).
#include "Theme.h"
#include "Themes.h"
#include "ThemeShell.h"

#include <QFontMetrics>
#include <QList>
#include <QMenu>
#include <QSize>
#include <QString>

namespace ShellSizes {

// ToolChip's icon-only side: a theme's own chip, else 34.
inline int chipSide()
{
    return Themes::shell().chip.on ? Themes::shell().chip.size : 34;
}

// ToolCluster: 8 px of card padding, a 3 px gap between items, 7 px per
// separator.
inline QSize cluster(int chips, int separators)
{
    constexpr int kCardPad = 8;
    constexpr int kGap = 3;
    constexpr int kSeparator = 7;
    const int items = chips + separators;
    const int height = 2 * kCardPad + chips * chipSide() + separators * kSeparator +
                       (items > 1 ? (items - 1) * kGap : 0);
    return Theme::wholeDevicePixels(QSize(2 * kCardPad + chipSide(), height));
}

// WindowButtons' Card look: three 36 x 26 chips, 4 px apart, in 6 px.
inline QSize windowButtons()
{
    return Theme::wholeDevicePixels(QSize(2 * 6 + 3 * 36 + 2 * 4, 2 * 6 + 26));
}

// AppBar: the 20 px mark, the wordmark and the menu titles in a pill of 6 px
// top and bottom whose ends' radius is half its height.
inline QSize pill(const QList<QMenu*>& menus)
{
    const QFontMetrics body(Theme::bodyFont());
    const int row = std::max(20, body.height() + 12);
    const int height = row + 2 * 6;
    int width = height;  // the two half-height ends
    width += 20 + Theme::Space::Tight + body.horizontalAdvance(QStringLiteral("FurnifyMe")) + Theme::Space::Tight;
    for (const QMenu* menu : menus) {
        QString title = menu->title();
        title.remove(QLatin1Char('&'));
        width += body.horizontalAdvance(title) + 24;
    }
    return Theme::wholeDevicePixels(QSize(width, height));
}

}  // namespace ShellSizes
