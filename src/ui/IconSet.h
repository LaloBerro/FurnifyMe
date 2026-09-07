#pragma once
// Monochrome glyphs painted with QPainter. Qt's SVG support lives in the
// separate qtsvg module, which is not installed, so the icons are drawn in code
// - which also means they are crisp at any DPI and recolour with the theme.
#include <QIcon>

namespace IconSet {

// DisplayMode, Screenshot and Fit used to be here: the rail carried them as
// icons until Task 3 folded Wireframe and Fit All into the app bar as text
// buttons and left Save Screenshot menu-only, which left all three glyphs
// unreferenced by anything the app actually builds. Removed rather than kept
// dead, per the same rule that keeps `chrome()` truthful in Theme.h - an enum
// value with no caller is exactly the kind of claim this file cannot back up.
//
// Milestone 5, item 3 moves Wireframe and Fit All back onto icon-only chips -
// the app bar itself is a floating pill now and has no room left for text
// buttons - so `Wireframe` and `FitAll` return under those names, drawn fresh
// rather than resurrected byte-for-byte (the old `DisplayMode`/`Fit` cases are
// gone with the commit that deleted them; these are new glyphs on the same
// 24x24 grid). `Projection` is genuinely new: the Persp/Ortho toggle never had
// an icon of its own before, because it painted its own word ("Persp"/"Ortho")
// as a bar button's text - an icon-only chip has no room for that word, so it
// needs a glyph that means "projection" rather than either of the two words.
enum class Glyph {
    Sketch, Extrude, Fuse, Cut, Intersect, Delete,
    Undo, Redo, Items, Snap,
    // A filled cube. It was SelectSolid, the rail's body-selection chip,
    // until the auto-selection spec's Phase 2 deleted the three selection
    // modes and their three chips - but the glyph itself never belonged to
    // that chip alone: ItemsPanel's per-row visibility button has always
    // drawn it too, and still does. Renamed rather than deleted, because a
    // glyph named after a control that no longer exists is worse than either
    // keeping it or dropping it. SelectFace and SelectEdge WERE dead the
    // moment their chips went and are gone outright, the same way
    // DisplayMode, Screenshot and Fit went above.
    Body,
    // The render-mode shutter (Task 7.2) - a camera body with a lens ring,
    // reintroduced on the same 24x24 grid the removed Screenshot glyph
    // above's comment describes, drawn fresh rather than resurrected: that
    // one was a toolbar-sized icon for an IconOnly rail chip, and this
    // shutter is a much bigger, round, standalone control, so the two would
    // never have shared a paintGlyph() case anyway.
    Camera,
    // The four view controls (Milestone 5, item 3) that moved off the old
    // text-button app bar onto icon-only chips under the axis gizmo. The
    // unit chip is not here - it paints its own text ("mm"/"cm") as the
    // glyph instead of a drawn icon, through ToolChip's text-glyph
    // constructor, since a unit is a word, not a shape.
    Wireframe,   // half-shaded circle - "edges only, see through the rest"
    FitAll,      // frame corners - "frame everything"
    Projection,  // a perspective frustum - "how depth is drawn"
};

// Returns an icon with Normal and Disabled modes already filled in.
QIcon icon(Glyph glyph);

// The APPLICATION icon: the wordmark's accent mark on a rounded Graphite tile.
// The window's title bar and the taskbar show appIcon(); the committed
// assets/icon.ico that the executable itself carries is the same painting,
// written out by tools/make_icon.cpp (built only under
// -DFURNIFYME_BUILD_ICON_TOOL=ON), so the look has one implementation rather
// than a picture and a piece of code that can drift apart.
//
// This is a TEMPORARY mark, and it is deliberately the one thing the shell
// already has that reads as this app: the bar's "▰ FurnifyMe". The parallelogram
// is painted as geometry rather than as the character U+25B0, because an icon
// is the one surface with no font stack behind it - a family without that glyph
// would put a box on the taskbar with nothing to fall back to.
QPixmap appIconPixmap(int px);
QIcon appIcon();

// The same artwork appIcon() prefers (assets/Icon.png, bundled as
// :/icons/app.png), scaled fresh to `px` rather than picked from appIcon()'s
// own fixed size set (16/24/32/48/64/128/256 - none of them small enough for
// a mark sitting beside a wordmark at chip height). Falls back to the painted
// tile the same way appIcon() does, so a broken resource build still shows
// something rather than a blank square. One implementation of "the user's
// mark, at an arbitrary size" rather than two - AppBar's pill is the first
// caller that needs a size appIcon() never offers.
QPixmap appMarkPixmap(int px);

}  // namespace IconSet
