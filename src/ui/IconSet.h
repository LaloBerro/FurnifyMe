#pragma once
// Monochrome glyphs painted with QPainter. Qt's SVG support lives in the
// separate qtsvg module, which is not installed, so the icons are drawn in code
// - which also means they are crisp at any DPI and recolour with the theme.
#include <QIcon>

namespace IconSet {

enum class Glyph {
    Sketch, Extrude, Fuse, Cut, Intersect, Delete,
    Undo, Redo, Items, Snap, SelectSolid, SelectFace, SelectEdge,
    DisplayMode, Screenshot, Fit,
};

// Returns an icon with Normal and Disabled modes already filled in.
QIcon icon(Glyph glyph);

}  // namespace IconSet
