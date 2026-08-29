#include "Measure.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

namespace Measure {
namespace {

// U+00D7 MULTIPLICATION SIGN, written as UTF-8 bytes so the file's own encoding
// cannot change what ships.
const char* kTimes = "\xC3\x97";

// Groups the integer part in threes: 1234567 -> "1,234,567".
std::string groupThousands(const std::string& digits)
{
    std::string out;
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && i >= lead && (i - lead) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

}  // namespace

Extents extentsOf(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return Extents{};

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return Extents{};

    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return Extents{xmax - xmin, ymax - ymin, zmax - zmin};
}

std::string formatLength(double millimetres)
{
    // Round to a tenth first, so every later decision sees the same number the
    // user will read.
    double value = std::round(millimetres * 10.0) / 10.0;
    if (value == 0.0) value = 0.0;   // collapses -0.0, which would print "-0"

    const bool negative = value < 0.0;
    const double magnitude = std::fabs(value);
    const long long whole = static_cast<long long>(magnitude);
    const int tenth = static_cast<int>(std::llround((magnitude - whole) * 10.0));

    char digits[32];
    std::snprintf(digits, sizeof(digits), "%lld", whole);

    std::string out;
    if (negative) out.push_back('-');
    out += groupThousands(digits);
    if (tenth != 0) {
        out.push_back('.');
        out.push_back(static_cast<char>('0' + tenth));
    }
    out += " mm";
    return out;
}

std::string formatDimensions(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return std::string();

    const Extents e = extentsOf(shape);
    if (e.x == 0.0 && e.y == 0.0 && e.z == 0.0) return std::string();

    // Each number is formatted by formatLength so the rounding rule lives in
    // exactly one place, then the trailing unit is stripped from all but the
    // last - "340 x 220 x 18 mm", not "340 mm x 220 mm x 18 mm".
    auto bare = [](const std::string& withUnit) {
        const std::size_t space = withUnit.rfind(' ');
        return space == std::string::npos ? withUnit : withUnit.substr(0, space);
    };

    return bare(formatLength(e.x)) + " " + kTimes + " " + bare(formatLength(e.y)) +
           " " + kTimes + " " + formatLength(e.z);
}

}  // namespace Measure
