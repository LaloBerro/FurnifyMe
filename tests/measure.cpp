//
// Headless tests for number formatting. Formatting is logic, and logic in this
// project is testable without a window - which is why Measure is Qt-free and
// lives in furnify_geometry.
//
#include "Measure.h"
#include "ModelingOps.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void checkEq(const std::string& actual, const std::string& expected, const std::string& what)
{
    const bool ok = actual == expected;
    std::printf("%-6s %s (got \"%s\", expected \"%s\")\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual.c_str(), expected.c_str());
    if (!ok) ++g_failures;
}

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}
}  // namespace

int main()
{
    // --- formatLength ---------------------------------------------------------
    checkEq(Measure::formatLength(340.0), "340 mm", "a whole value has no decimal part");
    checkEq(Measure::formatLength(18.5), "18.5 mm", "a half millimetre keeps one decimal");
    checkEq(Measure::formatLength(1200.0), "1,200 mm", "four digits are comma separated");
    checkEq(Measure::formatLength(999.0), "999 mm", "three digits are not separated");
    checkEq(Measure::formatLength(1000.0), "1,000 mm", "the separator starts at four digits");
    checkEq(Measure::formatLength(1234567.0), "1,234,567 mm", "separators repeat every three");
    checkEq(Measure::formatLength(1234.5), "1,234.5 mm", "separators and a decimal coexist");

    // Rounding, stated precisely because the panel and the status bar must never
    // disagree about how thick a body is.
    checkEq(Measure::formatLength(18.04), "18 mm", "18.04 rounds down and drops the .0");
    checkEq(Measure::formatLength(18.05), "18.1 mm", "18.05 rounds up to one decimal");
    checkEq(Measure::formatLength(0.04), "0 mm", "a value below half a tenth reads as zero");
    checkEq(Measure::formatLength(-0.01), "0 mm", "a tiny negative never renders as -0");
    checkEq(Measure::formatLength(0.0), "0 mm", "zero is plain");

    // --- extentsOf and formatDimensions --------------------------------------
    {
        const TopoDS_Shape box =
            ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 340.0, 220.0, 18.0);

        const Measure::Extents e = Measure::extentsOf(box);
        check(std::abs(e.x - 340.0) < 1e-6 && std::abs(e.y - 220.0) < 1e-6 &&
              std::abs(e.z - 18.0) < 1e-6,
              "extentsOf returns the box's own width, depth and height");

        const std::string dims = Measure::formatDimensions(box);
        checkEq(dims, "340 \xC3\x97 220 \xC3\x97 18 mm",
                "dimensions read X by Y by Z with a real multiplication sign");
        check(dims.find('x') == std::string::npos,
              "the separator is U+00D7, not the letter x");

        // A box away from the origin has the same extents: it is a size, not a
        // position.
        const TopoDS_Shape moved =
            ModelingOps::makeBox(gp_Pnt(-500.0, 900.0, 40.0), 340.0, 220.0, 18.0);
        checkEq(Measure::formatDimensions(moved), dims,
                "position does not change the reported dimensions");
    }

    // --- degenerate input -----------------------------------------------------
    {
        const TopoDS_Shape nothing;
        checkEq(Measure::formatDimensions(nothing), "",
                "a null shape formats as an empty string, not 0 by 0 by 0");
        const Measure::Extents e = Measure::extentsOf(nothing);
        check(e.x == 0.0 && e.y == 0.0 && e.z == 0.0, "a null shape has zero extents");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
