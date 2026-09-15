//
// Re-Measure (improvements item 8): retype one of a body's three sizes and
// the body changes to match - proven headless before a single pixel of the
// gesture exists.
//
// Two things a careless implementation gets away with, and which the blocks
// below are shaped to catch:
//
//   - VOLUME CANNOT TELL THE THREE ANCHORS APART. Low, Centre and High all
//     produce a board of exactly the typed length, so every case here reads
//     the CENTRE OF MASS too: on a 600 board shortened to 450, Low leaves it
//     225 from the low end, Centre leaves it exactly where it was, and High
//     leaves it 225 from the high end. Three different numbers.
//   - AN ORIENTED QUANTITY MEASURED IN WORLD TERMS. The turned-board block
//     resizes a board rotated about a non-world axis: read the extent off a
//     world bounding box instead of ModelingOps::measuredBox() and the board
//     comes back 380 mm where 450 was typed.
//
// Run:  ctest --preset windows-headless --output-on-failure
//   or: ./build-headless/RelWithDebInfo/headless_resize_axis
//
#include "ModelingOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

const double kPi = 3.14159265358979323846;
constexpr double kExact = 1.0e-3;   // a thousandth of a millimetre

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n", ok ? "[ ok ]" : "[FAIL]", what.c_str(),
                actual, expected);
    if (!ok) ++g_failures;
}

TopoDS_Shape transformed(const TopoDS_Shape& s, const gp_Trsf& t)
{
    return BRepBuilderAPI_Transform(s, t, true).Shape();
}

// How far a point sits along `axis` - the one projection every centre-of-mass
// check is read through, so a turned board is measured in its own terms
// exactly as an axis-aligned one is.
double along(const gp_Pnt& p, const gp_Dir& axis)
{
    return gp_Vec(p.X(), p.Y(), p.Z()).Dot(gp_Vec(axis));
}

// The test's stand-in for the user's own pick: the planar face square to
// `axis` at one end of the shape. Deliberately a local copy rather than a
// hook into ModelingOps - what is under test is what the operation does with
// an end, not how a caller finds one.
TopoDS_Face endFace(const TopoDS_Shape& shape, const gp_Dir& axis, bool highEnd)
{
    TopoDS_Face best;
    double bestD = 0.0;
    bool any = false;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) continue;
        const gp_Pln pln = surface.Plane();
        if (std::fabs(gp_Vec(pln.Axis().Direction()).Dot(gp_Vec(axis))) < 0.999999) continue;
        const double d = along(pln.Location(), axis);
        if (!any || (highEnd ? d > bestD : d < bestD)) {
            bestD = d;
            best = face;
            any = true;
        }
    }
    return best;
}

// The extent of a shape's measured box along `axis`, whichever of its three
// axes that turns out to be - which is the whole point on a turned board,
// where the side that was typed at is not "width" in any world sense.
double measuredExtentAlong(const TopoDS_Shape& s, const gp_Dir& axis)
{
    const ModelingOps::MeasuredBox box = ModelingOps::measuredBox({s});
    const gp_Dir axes[3] = {box.widthAxis, box.depthAxis, box.heightAxis};
    const double sizes[3] = {box.width, box.depth, box.height};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(gp_Vec(axes[i]).Dot(gp_Vec(axis))) >= 0.999999) return sizes[i];
    }
    return -1.0;
}

// The three extents of a shape's measured box, large to small.
std::vector<double> sortedExtents(const TopoDS_Shape& s)
{
    const ModelingOps::MeasuredBox box = ModelingOps::measuredBox({s});
    std::vector<double> e{box.width, box.depth, box.height};
    std::sort(e.begin(), e.end(), std::greater<double>());
    return e;
}

// One anchor on one board, end to end: the typed size lands, the two sides
// nobody typed at are untouched, the result is one solid, and the centre of
// mass is where THIS anchor puts it and not where either of the other two
// would.
void checkOneResize(const TopoDS_Shape& board, const gp_Dir& axis, double newSize,
                    ModelingOps::ResizeAnchor anchor, double expectedCentre,
                    double otherA, double otherB, const std::string& name)
{
    const ModelingOps::BooleanResult result =
        ModelingOps::resizeAlongAxis(board, axis, newSize, anchor);
    check(result.ok, name + ": accepted");
    if (!result.ok) {
        std::printf("       (refused: %s)\n", result.error.c_str());
        return;
    }
    check(ModelingOps::countSolids(result.shape) == 1, name + ": the result is one body");
    checkNear(measuredExtentAlong(result.shape, axis), newSize, kExact,
              name + ": the resized side reads the typed size");
    checkNear(ModelingOps::volume(result.shape), newSize * otherA * otherB, 1.0,
              name + ": the volume is the typed size times the other two sides");
    checkNear(along(ModelingOps::centreOfMass(result.shape), axis), expectedCentre, kExact,
              name + ": the centre of mass is where this anchor puts it");

    // A scale would move the other two sides; moving a face cannot.
    const std::vector<double> got = sortedExtents(result.shape);
    bool hasA = false, hasB = false;
    for (double v : got) {
        if (std::fabs(v - otherA) < kExact) hasA = true;
        if (std::fabs(v - otherB) < kExact) hasB = true;
    }
    check(hasA, name + ": the first untyped side is untouched");
    check(hasB, name + ": the second untyped side is untouched");
}

}  // namespace

int main()
{
    using ModelingOps::ResizeAnchor;
    using ModelingOps::ResizeCheck;

    // 600 x 300 x 18, three extents far apart, at an origin nobody could
    // mistake for the world's: X 100..700, Y -50..250, Z 20..38.
    const TopoDS_Shape board =
        ModelingOps::makeBox(gp_Pnt(100.0, -50.0, 20.0), 600.0, 300.0, 18.0);
    const gp_Dir X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

    // --- the three anchors, shrinking, on the long side ----------------------
    // 600 -> 450 from x = 100..700, centre 400.
    checkOneResize(board, X, 450.0, ResizeAnchor::Low, 325.0, 300.0, 18.0,
                   "board 600 -> 450, anchor Low");
    checkOneResize(board, X, 450.0, ResizeAnchor::Centre, 400.0, 300.0, 18.0,
                   "board 600 -> 450, anchor Centre");
    checkOneResize(board, X, 450.0, ResizeAnchor::High, 475.0, 300.0, 18.0,
                   "board 600 -> 450, anchor High");

    // --- growing, all three anchors -----------------------------------------
    // 600 -> 800: Low puts the centre at 100 + 400, High at 700 - 400, Centre
    // leaves it at 400.
    checkOneResize(board, X, 800.0, ResizeAnchor::Low, 500.0, 300.0, 18.0,
                   "board 600 -> 800, anchor Low");
    checkOneResize(board, X, 800.0, ResizeAnchor::Centre, 400.0, 300.0, 18.0,
                   "board 600 -> 800, anchor Centre");
    checkOneResize(board, X, 800.0, ResizeAnchor::High, 300.0, 300.0, 18.0,
                   "board 600 -> 800, anchor High");

    // --- the other two axes --------------------------------------------------
    // Depth 300 -> 200 from y = -50..250, centre 100.
    checkOneResize(board, Y, 200.0, ResizeAnchor::Low, 50.0, 600.0, 18.0,
                   "board depth 300 -> 200, anchor Low");
    checkOneResize(board, Y, 200.0, ResizeAnchor::Centre, 100.0, 600.0, 18.0,
                   "board depth 300 -> 200, anchor Centre");
    checkOneResize(board, Y, 200.0, ResizeAnchor::High, 150.0, 600.0, 18.0,
                   "board depth 300 -> 200, anchor High");
    // Height 18 -> 30 from z = 20..38, centre 29. A shelf keeps the bottom
    // where it is with anchor Low - the picture behind the whole anchor idea.
    checkOneResize(board, Z, 30.0, ResizeAnchor::Low, 35.0, 600.0, 300.0,
                   "board height 18 -> 30, anchor Low");
    checkOneResize(board, Z, 30.0, ResizeAnchor::Centre, 29.0, 600.0, 300.0,
                   "board height 18 -> 30, anchor Centre");
    checkOneResize(board, Z, 30.0, ResizeAnchor::High, 23.0, 600.0, 300.0,
                   "board height 18 -> 30, anchor High");

    // --- a board turned about a NON-WORLD axis -------------------------------
    //
    // THE world-axis catcher. Turned 30 degrees about Z and then 37 about
    // (1, 2, 3), the board's world bounding box has nothing to do with its
    // own sides; measuredBox() reads 600 x 300 x 18 regardless, and the resize
    // has to be measured in exactly those terms.
    {
        gp_Trsf rz;
        rz.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 30.0 * kPi / 180.0);
        gp_Trsf skew;
        skew.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), 37.0 * kPi / 180.0);
        const TopoDS_Shape turned = transformed(transformed(board, rz), skew);

        const ModelingOps::MeasuredBox box = ModelingOps::measuredBox({turned});
        check(box.ok, "turned board: measured");
        check(!box.worldAligned, "turned board: measured in its own frame, not the world's");

        // Whichever of the three axes carries the 600 side - which one that is
        // called depends on how the board happens to lie once it is turned.
        gp_Dir longAxis = box.widthAxis;
        double longExtent = box.width;
        const gp_Dir axes[3] = {box.widthAxis, box.depthAxis, box.heightAxis};
        const double sizes[3] = {box.width, box.depth, box.height};
        for (int i = 0; i < 3; ++i) {
            if (sizes[i] > longExtent) {
                longExtent = sizes[i];
                longAxis = axes[i];
            }
        }
        checkNear(longExtent, 600.0, kExact, "turned board: its long side still reads 600");

        const double lowEnd = along(ModelingOps::centreOfMass(turned), longAxis) - 300.0;
        checkOneResize(turned, longAxis, 450.0, ResizeAnchor::Low, lowEnd + 225.0, 300.0, 18.0,
                       "turned board 600 -> 450, anchor Low");
        checkOneResize(turned, longAxis, 450.0, ResizeAnchor::Centre, lowEnd + 300.0, 300.0, 18.0,
                       "turned board 600 -> 450, anchor Centre");
    }

    // --- a MIRRORED body -----------------------------------------------------
    //
    // A mirror is a negative-determinant transform, and BRepBuilderAPI_Transform
    // toggles a mirrored shape's face-orientation flags - the case that once
    // made a "grow" pull land net inward. resizeAlongAxis() inherits
    // pullFace()'s classifier probe, so this has to grow outward exactly as
    // the unmirrored board does.
    {
        const ModelingOps::BooleanResult mirrored =
            ModelingOps::mirrorShape(board, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)));
        check(mirrored.ok, "mirrored board: mirrored");
        // x = 100..700 reflected through x = 0 is -700..-100, centre -400.
        checkNear(along(ModelingOps::centreOfMass(mirrored.shape), X), -400.0, kExact,
                  "mirrored board: sits where the mirror put it");
        // Low keeps the low end (x = -700); the high end comes in to -250.
        checkOneResize(mirrored.shape, X, 450.0, ResizeAnchor::Low, -475.0, 300.0, 18.0,
                       "mirrored board 600 -> 450, anchor Low");
        checkOneResize(mirrored.shape, X, 800.0, ResizeAnchor::Centre, -400.0, 300.0, 18.0,
                       "mirrored board 600 -> 800, anchor Centre");
    }

    // --- no change at all is not a refusal -----------------------------------
    {
        const ModelingOps::BooleanResult same =
            ModelingOps::resizeAlongAxis(board, X, 600.0, ResizeAnchor::Centre);
        check(same.ok, "typing the size it already is: accepted");
        checkNear(ModelingOps::volume(same.shape), 600.0 * 300.0 * 18.0, 1.0,
                  "typing the size it already is: the body is unchanged");
    }

    // --- the refusals, each by name ------------------------------------------
    {
        const ModelingOps::BooleanResult nullBody =
            ModelingOps::resizeAlongAxis(TopoDS_Shape(), X, 450.0, ResizeAnchor::Centre);
        check(!nullBody.ok, "a null body: refused");
        check(nullBody.shape.IsNull(), "a null body: the refusal carries a null shape");
        check(!nullBody.error.empty(), "a null body: the refusal says why");
        check(ModelingOps::checkResize(TopoDS_Shape(), X, 450.0, ResizeAnchor::Centre) ==
                  ResizeCheck::NotMeasurable,
              "a null body: checkResize reports NotMeasurable");
    }
    {
        const ModelingOps::BooleanResult zero =
            ModelingOps::resizeAlongAxis(board, X, 0.0, ResizeAnchor::Centre);
        check(!zero.ok, "a size of zero: refused");
        check(zero.shape.IsNull(), "a size of zero: the refusal carries a null shape");
        check(ModelingOps::checkResize(board, X, 0.0, ResizeAnchor::Centre) ==
                  ResizeCheck::SizeNotPositive,
              "a size of zero: checkResize reports SizeNotPositive");
        check(ModelingOps::checkResize(board, X, -450.0, ResizeAnchor::Centre) ==
                  ResizeCheck::SizeNotPositive,
              "a negative size: checkResize reports SizeNotPositive");
        check(!ModelingOps::resizeAlongAxis(board, X, -450.0, ResizeAnchor::Centre).ok,
              "a negative size: refused");
    }
    {
        // A direction that is not one of the board's own three sides.
        const gp_Dir diagonal(1, 1, 1);
        check(ModelingOps::checkResize(board, diagonal, 450.0, ResizeAnchor::Centre) ==
                  ResizeCheck::AxisNotASide,
              "a direction that is not a side: checkResize reports AxisNotASide");
        const ModelingOps::BooleanResult skew =
            ModelingOps::resizeAlongAxis(board, diagonal, 450.0, ResizeAnchor::Centre);
        check(!skew.ok, "a direction that is not a side: refused");
        check(skew.shape.IsNull(),
              "a direction that is not a side: the refusal carries a null shape");
    }
    {
        // A MITRED END - the expected, documented refusal. The high end of the
        // board is cut off at 45 degrees, so there is no single flat face
        // square to X there any more.
        const TopoDS_Face high = endFace(board, X, true);
        check(!high.IsNull(), "mitred end: the board has a flat high end to cut");
        const ModelingOps::BooleanResult mitred =
            ModelingOps::mitreEnd(board, high, 45.0, ModelingOps::MitreSide::ThicknessA);
        check(mitred.ok, "mitred end: the mitre was made");
        if (mitred.ok) {
            check(ModelingOps::checkResize(mitred.shape, X, 450.0, ResizeAnchor::Low) ==
                      ResizeCheck::EndNotFlat,
                  "mitred end: moving the mitred end reports EndNotFlat");
            const ModelingOps::BooleanResult refused =
                ModelingOps::resizeAlongAxis(mitred.shape, X, 450.0, ResizeAnchor::Low);
            check(!refused.ok, "mitred end: moving the mitred end is refused");
            check(refused.shape.IsNull(), "mitred end: the refusal carries a null shape");
            check(!refused.error.empty(), "mitred end: the refusal says why");
            check(ModelingOps::checkResize(mitred.shape, X, 450.0, ResizeAnchor::Centre) ==
                      ResizeCheck::EndNotFlat,
                  "mitred end: Centre moves both ends, so it reports EndNotFlat too");
            // ...and the SQUARE end can still be moved: the refusal is about
            // the end that has to move, not about the body.
            const ModelingOps::BooleanResult allowed =
                ModelingOps::resizeAlongAxis(mitred.shape, X, 450.0, ResizeAnchor::High);
            check(allowed.ok, "mitred end: moving the square end instead is accepted");
            if (allowed.ok)
                checkNear(measuredExtentAlong(allowed.shape, X), 450.0, kExact,
                          "mitred end: the square end moved to the typed size");
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
