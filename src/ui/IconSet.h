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
enum class Glyph {
    Sketch, Extrude, Fuse, Cut, Intersect, Delete,
    Undo, Redo, Items, Snap, SelectSolid, SelectFace, SelectEdge,
    // The render-mode shutter (Task 7.2) - a camera body with a lens ring,
    // reintroduced on the same 24x24 grid the removed Screenshot glyph
    // above's comment describes, drawn fresh rather than resurrected: that
    // one was a toolbar-sized icon for an IconOnly rail chip, and this
    // shutter is a much bigger, round, standalone control, so the two would
    // never have shared a paintGlyph() case anyway.
    Camera,
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

}  // namespace IconSet
