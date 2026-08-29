#pragma once
//
// Formats measurements for display. Qt-free and in furnify_geometry so the
// rounding and separator rules are covered by headless tests - the status bar
// and the items panel must never disagree about how thick a body is.
//
#include <string>

#include <TopoDS_Shape.hxx>

namespace Measure {

// Bounding-box size along each world axis, in millimetres. All zero for a null
// or void shape.
struct Extents {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Extents extentsOf(const TopoDS_Shape& shape);

// The unit every user-facing length is displayed and typed in. Millimetres by
// default, so a caller that never touches this behaves exactly as before this
// setting existed.
enum class Unit { Millimetres, Centimetres };

void setDisplayUnit(Unit unit);
Unit displayUnit();

// "340 mm", "1,200 mm", "18.5 mm" - or the same value in centimetres once
// setDisplayUnit(Centimetres) is in effect: "34 cm", "120 cm", "1.85 cm".
// Always takes millimetres regardless of the display unit - the unit lives in
// the formatter, not the argument, so an existing call site never needs to
// change what it passes. Rounded to one decimal place (in the displayed unit)
// with a trailing ".0" dropped, thousands separated by a plain comma applied
// to the value as displayed. Formatted by hand rather than through
// std::locale, whose output would depend on the machine's regional settings
// and make these tests pass or fail by geography.
std::string formatLength(double millimetres);

// "340 x 220 x 18 mm" with U+00D7 between the numbers, always in X, Y, Z order
// so the three values always mean the same thing. Empty string for a null or
// void shape. Each number goes through formatLength, so it reads in the
// current display unit too.
std::string formatDimensions(const TopoDS_Shape& shape);

// Parses a number the user typed **in the current display unit** and returns
// millimetres in `out`. Accepted grammar, after trimming surrounding spaces:
// an optional leading '+' or '-', then digits with at most one '.', and
// nothing else - so a plain decimal only. Returns false and leaves `out`
// untouched for anything outside that grammar, including but not limited to
// "" (empty), "abc" (non-numeric), "1.2.3" (two decimal points), "." (no
// digit), "1,2" (a comma), "1e3" (scientific notation) and "0x10" (a hex
// float) - std::strtod alone would accept the last two, and its decimal
// separator is locale-dependent, which is exactly why the grammar is checked
// by hand before the string ever reaches it. Never uses atof, which reports
// no error and silently returns 0 for garbage.
bool parseLength(const std::string& text, double& out);

// "mm" or "cm", for a label that needs the unit alone rather than baked into
// a formatted number.
std::string unitSuffix();

}  // namespace Measure
