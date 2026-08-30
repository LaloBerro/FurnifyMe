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
};

// Returns an icon with Normal and Disabled modes already filled in.
QIcon icon(Glyph glyph);

}  // namespace IconSet
