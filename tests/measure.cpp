//
// Headless tests for number formatting. Formatting is logic, and logic in this
// project is testable without a window - which is why Measure is Qt-free and
// lives in furnify_geometry.
//
#include "Measure.h"
#include "ModelingOps.h"
#include "SketchController.h"

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

    // --- the display unit ---------------------------------------------------
    check(Measure::displayUnit() == Measure::Unit::Millimetres,
          "millimetres is the default, so an untouched caller is unaffected");
    check(Measure::formatLength(340.0) == "340 mm", "millimetres unchanged");

    Measure::setDisplayUnit(Measure::Unit::Centimetres);
    check(Measure::displayUnit() == Measure::Unit::Centimetres, "the unit round-trips");
    check(Measure::formatLength(0.0) == "0 cm", "zero has no decimal");
    check(Measure::formatLength(4.0) == "0.4 cm", "4 mm is 0.4 cm");
    check(Measure::formatLength(18.0) == "1.8 cm", "18 mm is 1.8 cm");
    check(Measure::formatLength(340.0) == "34 cm", "a whole value drops the .0");
    check(Measure::formatLength(1000.0) == "100 cm", "1,000 mm is 100 cm");
    check(Measure::formatLength(123456.0) == "12,345.6 cm",
          "thousands are separated in the displayed value");
    check(Measure::unitSuffix() == "cm", "the suffix follows the unit");

    // formatDimensions on the box built above, but now read in centimetres.
    {
        const TopoDS_Shape box =
            ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 340.0, 220.0, 18.0);
        checkEq(Measure::formatDimensions(box), "34 \xC3\x97 22 \xC3\x97 1.8 cm",
                "formatDimensions reads in the current display unit too");
    }

    // Input is read in the displayed unit, or a field that shows cm and reads
    // mm is a trap.
    double mm = 0.0;
    check(Measure::parseLength("4", mm) && mm == 40.0, "4 cm parses to 40 mm");
    check(Measure::parseLength(" 1.8 ", mm) && mm == 18.0, "spaces are tolerated");
    check(Measure::parseLength("-2", mm) && mm == -20.0, "a negative parses");

    double untouched = 99.0;
    check(!Measure::parseLength("", untouched) && untouched == 99.0,
          "empty is refused and leaves the value alone");
    check(!Measure::parseLength("abc", untouched) && untouched == 99.0, "letters refused");
    check(!Measure::parseLength("1.2.3", untouched) && untouched == 99.0,
          "two decimal points refused");
    check(!Measure::parseLength(".", untouched) && untouched == 99.0, "a bare point refused");
    check(!Measure::parseLength("1,2", untouched) && untouched == 99.0, "a comma refused");

    // A length field is not a general strtod - hex floats, scientific
    // notation, and the special non-finite spellings all read as garbage
    // here, not as numbers, even though the real strtod would happily accept
    // most of them.
    check(!Measure::parseLength("0x10", untouched) && untouched == 99.0,
          "a hex float is refused");
    check(!Measure::parseLength("1e3", untouched) && untouched == 99.0,
          "scientific notation is refused");
    check(!Measure::parseLength("1E3", untouched) && untouched == 99.0,
          "scientific notation is refused regardless of case");
    check(!Measure::parseLength("inf", untouched) && untouched == 99.0,
          "the word inf is refused");
    check(!Measure::parseLength("nan", untouched) && untouched == 99.0,
          "the word nan is refused");
    check(!Measure::parseLength("1p3", untouched) && untouched == 99.0,
          "a stray letter after a digit is refused");

    Measure::setDisplayUnit(Measure::Unit::Millimetres);
    check(Measure::parseLength("4", mm) && mm == 4.0, "4 mm parses to 4 mm");
    check(Measure::formatLength(340.0) == "340 mm", "switching back restores exactly");

    // --- a flat outline is measured in its OWN plane -------------------------
    // What the Items drawer paints beside "Outline 01". The world-axis box
    // this replaces reports one of a vertical outline's two real dimensions
    // as a zero, which is why formatDimensions() is the wrong tool here.
    {
        const char* times = "\xC3\x97";
        const std::string expected = std::string("340 ") + times + " 220 mm";

        // On the ground, (u, v) is (X, Y) and the answer is the obvious one.
        const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        SketchController flatSketch;
        flatSketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        flatSketch.addPoint(gp_Pnt(340.0, 0.0, 0.0));
        flatSketch.addPoint(gp_Pnt(340.0, 220.0, 0.0));
        flatSketch.addPoint(gp_Pnt(0.0, 220.0, 0.0));
        const TopoDS_Face flat = flatSketch.closedFace();
        check(!flat.IsNull(), "the ground fixture closes");
        checkEq(Measure::formatFaceExtents(flat, ground), expected,
                "a ground outline reads as its own width and depth");

        // The same outline stood up on a vertical plane: the numbers must not
        // change, and this is the case a world-axis bounding box gets wrong.
        const gp_Pln wall(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, -1.0, 0.0));
        SketchController wallSketch;
        wallSketch.setPlane(wall);
        wallSketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        wallSketch.addPoint(gp_Pnt(340.0, 0.0, 0.0));
        wallSketch.addPoint(gp_Pnt(340.0, 0.0, 220.0));
        wallSketch.addPoint(gp_Pnt(0.0, 0.0, 220.0));
        const TopoDS_Face vertical = wallSketch.closedFace();
        check(!vertical.IsNull(), "the wall fixture closes");
        // The SAME two numbers, and both of them real - which is the whole
        // claim. Their ORDER is the plane's own (u, v), not the world's, and
        // that is deliberate rather than incidental: it is the frame
        // ElSLib::Parameters gives, which is the frame snapToPlaneGrid snaps
        // in and the frame the cursor readout reports while the outline is
        // being drawn. A drawer that re-ordered them to look more like the
        // world would disagree with the status bar the user just read.
        const std::string onWall = Measure::formatFaceExtents(vertical, wall);
        checkEq(onWall, std::string("220 ") + times + " 340 mm",
                "a wall outline reads both real dimensions, in the plane's own u, v");
        check(onWall.find("340") != std::string::npos &&
                  onWall.find("220") != std::string::npos,
              "so neither of the two dimensions is lost");
        // The comparison that makes the point: the world-axis reading has a
        // zero in it, so this is not a distinction without a difference.
        check(Measure::formatDimensions(vertical).find(" 0 ") != std::string::npos ||
                  Measure::formatDimensions(vertical).find("0 mm") != std::string::npos,
              "while the world-axis reading of it carries a zero");

        check(Measure::formatFaceExtents(TopoDS_Shape(), ground).empty(),
              "a null shape formats as nothing");

        // It follows the display unit like every other length.
        Measure::setDisplayUnit(Measure::Unit::Centimetres);
        checkEq(Measure::formatFaceExtents(flat, ground),
                std::string("34 ") + times + " 22 cm",
                "and it reads in the display unit");
        Measure::setDisplayUnit(Measure::Unit::Millimetres);
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
