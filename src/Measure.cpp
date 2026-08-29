#include "Measure.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Measure {
namespace {

// U+00D7 MULTIPLICATION SIGN, written as UTF-8 bytes so the file's own encoding
// cannot change what ships.
const char* kTimes = "\xC3\x97";

Unit g_unit = Unit::Millimetres;

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

// True when `text` is nothing but an optional leading '+'/'-' followed by
// digits with at most one '.', and at least one digit somewhere. Checked by
// hand, before ever calling std::strtod, so "0x10" (hex float), "1e3" /
// "1E3" (scientific notation) and "inf" / "nan" are rejected by construction
// rather than by a growing pile of guards after the fact - and so strtod's
// locale-dependent decimal separator is never in play, since only plain
// ASCII digits and '.' reach it.
bool looksLikePlainNumber(const std::string& text)
{
    std::size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;

    bool sawDigit = false;
    bool sawDot = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.') {
            if (sawDot) return false;
            sawDot = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            sawDigit = true;
        } else {
            return false;
        }
    }
    return sawDigit;
}

// Renders a value already in the display unit, with the given suffix. The
// rounding rule - one decimal place, trailing ".0" dropped - is the single
// path every length in the app goes through, in whichever unit is current.
std::string formatValue(double value, const char* suffix)
{
    // Round to a tenth first, so every later decision sees the same number the
    // user will read.
    double rounded = std::round(value * 10.0) / 10.0;
    if (rounded == 0.0) rounded = 0.0;   // collapses -0.0, which would print "-0"

    const bool negative = rounded < 0.0;
    const double magnitude = std::fabs(rounded);
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
    out.push_back(' ');
    out += suffix;
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

void setDisplayUnit(Unit unit)
{
    g_unit = unit;
}

Unit displayUnit()
{
    return g_unit;
}

std::string unitSuffix()
{
    return g_unit == Unit::Centimetres ? "cm" : "mm";
}

std::string formatLength(double millimetres)
{
    const double value = g_unit == Unit::Centimetres ? millimetres / 10.0 : millimetres;
    return formatValue(value, g_unit == Unit::Centimetres ? "cm" : "mm");
}

bool parseLength(const std::string& text, double& out)
{
    // Trim surrounding whitespace by hand - std::strtod does not skip trailing
    // whitespace, and leading whitespace it does skip would otherwise hide a
    // string that is nothing but spaces.
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    const std::string trimmed = text.substr(begin, end - begin);
    if (trimmed.empty()) return false;

    // Reject anything that is not plain sign-digits-dot-digits before ever
    // reaching std::strtod (never atof, which reports no error and returns 0
    // for garbage) - see looksLikePlainNumber for what that rules out.
    if (!looksLikePlainNumber(trimmed)) return false;

    const char* start = trimmed.c_str();
    char* endPtr = nullptr;
    const double parsed = std::strtod(start, &endPtr);
    if (endPtr != start + trimmed.size()) return false;  // paranoia: should be unreachable
    if (!std::isfinite(parsed)) return false;  // a very long digit run can still overflow

    out = g_unit == Unit::Centimetres ? parsed * 10.0 : parsed;
    return true;
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
