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

// "340 mm", "1,200 mm", "18.5 mm". Rounded to one decimal place with a trailing
// ".0" dropped, thousands separated by a plain comma. Formatted by hand rather
// than through std::locale, whose output would depend on the machine's regional
// settings and make these tests pass or fail by geography.
std::string formatLength(double millimetres);

// "340 x 220 x 18 mm" with U+00D7 between the numbers, always in X, Y, Z order
// so the three values always mean the same thing. Empty string for a null or
// void shape.
std::string formatDimensions(const TopoDS_Shape& shape);

}  // namespace Measure
