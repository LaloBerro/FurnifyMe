//
// Selection sizes (improvements item 5): the box the viewport measures a
// selection by - proven headless before a single dimension line is drawn.
//
// Every board here is 600 x 300 x 18 - three extents far apart, so a
// derivation that swaps width and depth, or reads a world extent where the
// board's own was wanted, lands on a visibly different number rather than one
// that happens to coincide.
//
// Run:  ctest --preset windows-headless --output-on-failure
//   or: ./build-headless/RelWithDebInfo/headless_measured_box
//
#include "ModelingOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

const double kPi = 3.14159265358979323846;
constexpr double kExact = 1.0e-3;   // the contract: to a thousandth of a millimetre

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

// |a . b| == 1 - parallel either way round.
bool parallel(const gp_Dir& a, const gp_Dir& b, double tol = 1.0e-9)
{
    return std::fabs(std::fabs(a.Dot(b)) - 1.0) <= tol;
}

TopoDS_Shape transformed(const TopoDS_Shape& s, const gp_Trsf& t)
{
    return BRepBuilderAPI_Transform(s, t, true).Shape();
}

void checkBoard(const ModelingOps::MeasuredBox& box, const std::string& name)
{
    check(box.ok, name + ": measured");
    checkNear(box.width, 600.0, kExact, name + ": width reads the board's own 600");
    checkNear(box.depth, 300.0, kExact, name + ": depth reads the board's own 300");
    checkNear(box.height, 18.0, kExact, name + ": height reads the board's own 18");
}

}  // namespace

int main()
{
    using ModelingOps::MeasuredBox;
    using ModelingOps::measuredBox;

    // --- refusals ------------------------------------------------------------
    {
        const MeasuredBox none = measuredBox({});
        check(!none.ok, "empty selection: refused");
        check(!none.error.empty(), "empty selection: refusal says why");
        const MeasuredBox nul = measuredBox({TopoDS_Shape()});
        check(!nul.ok, "a null shape: refused");
    }

    const TopoDS_Shape board = ModelingOps::makeBox(gp_Pnt(100.0, -50.0, 20.0), 600.0, 300.0, 18.0);

    // --- one axis-aligned board -----------------------------------------------
    {
        const MeasuredBox box = measuredBox({board});
        checkBoard(box, "axis-aligned board");
        check(box.worldAligned, "axis-aligned board: the world frame is chosen");
        check(parallel(box.widthAxis, gp_Dir(1, 0, 0)), "axis-aligned board: width runs along X");
        check(parallel(box.depthAxis, gp_Dir(0, 1, 0)), "axis-aligned board: depth runs along Y");
        check(box.heightAxis.IsEqual(gp_Dir(0, 0, 1), 1.0e-9), "axis-aligned board: height points up");
        checkNear(box.centre.X(), 400.0, kExact, "axis-aligned board: centre X");
        checkNear(box.centre.Y(), 100.0, kExact, "axis-aligned board: centre Y");
        checkNear(box.centre.Z(), 29.0, kExact, "axis-aligned board: centre Z");
        const gp_Pnt lo = box.corner(-1, -1, -1);
        const gp_Pnt hi = box.corner(1, 1, 1);
        checkNear(lo.Distance(hi), std::sqrt(600.0 * 600 + 300.0 * 300 + 18.0 * 18), kExact,
                  "axis-aligned board: opposite corners span the diagonal");
        const gp_Vec cross = gp_Vec(box.widthAxis).Crossed(gp_Vec(box.depthAxis));
        check(cross.Dot(gp_Vec(box.heightAxis)) > 0.999999,
              "axis-aligned board: width x depth = height (right-handed)");
    }

    // --- the same board turned 30 degrees about Z -----------------------------
    {
        gp_Trsf rz;
        rz.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 30.0 * kPi / 180.0);
        const MeasuredBox box = measuredBox({transformed(board, rz)});
        checkBoard(box, "board turned 30 about Z");
        check(!box.worldAligned, "board turned 30 about Z: the oriented frame is chosen");
        check(parallel(box.widthAxis, gp_Dir(std::cos(kPi / 6), std::sin(kPi / 6), 0), 1e-6),
              "board turned 30 about Z: width runs along the board's own long side");
        check(box.heightAxis.IsEqual(gp_Dir(0, 0, 1), 1.0e-6),
              "board turned 30 about Z: height still points up");
    }

    // --- tilted about a non-world axis by a non-90 angle ----------------------
    {
        gp_Trsf rk;
        rk.SetRotation(gp_Ax1(gp_Pnt(10, 20, 30), gp_Dir(1, 2, 3)), 37.0 * kPi / 180.0);
        const MeasuredBox box = measuredBox({transformed(board, rk)});
        check(box.ok, "board tilted 37 about (1,2,3): measured");
        check(!box.worldAligned, "board tilted 37 about (1,2,3): the oriented frame is chosen");
        // Which of the board's sides ends up "height" is whichever of its own
        // axes the tilt left closest to world Z - derived here from the same
        // transform, never assumed.
        const gp_Dir ax[3] = {gp_Dir(1, 0, 0).Transformed(rk), gp_Dir(0, 1, 0).Transformed(rk),
                              gp_Dir(0, 0, 1).Transformed(rk)};
        const double ext[3] = {600.0, 300.0, 18.0};
        int h = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(ax[i].Z()) > std::fabs(ax[h].Z())) h = i;
        double rest[2];
        int n = 0;
        for (int i = 0; i < 3; ++i)
            if (i != h) rest[n++] = ext[i];
        checkNear(box.height, ext[h], kExact, "board tilted 37 about (1,2,3): height is the side closest to Z");
        checkNear(box.width, std::max(rest[0], rest[1]), kExact,
                  "board tilted 37 about (1,2,3): width is the longer remaining side");
        checkNear(box.depth, std::min(rest[0], rest[1]), kExact,
                  "board tilted 37 about (1,2,3): depth is the shorter remaining side");
        check(parallel(box.heightAxis, ax[h], 1e-6), "board tilted 37 about (1,2,3): height runs along that side");
        check(box.heightAxis.Z() > 0.0, "board tilted 37 about (1,2,3): height axis points up");
    }

    // --- a group of three axis-aligned pieces: a small cabinet ------------------
    {
        const MeasuredBox box = measuredBox({ModelingOps::makeBox(gp_Pnt(0, 0, 0), 18, 300, 700),
                                             ModelingOps::makeBox(gp_Pnt(618, 0, 0), 18, 300, 700),
                                             ModelingOps::makeBox(gp_Pnt(0, 0, 700), 636, 300, 18)});
        check(box.ok, "cabinet group: measured");
        checkNear(box.width, 636.0, kExact, "cabinet group: overall width is the union's world X extent");
        checkNear(box.depth, 300.0, kExact, "cabinet group: overall depth is the union's world Y extent");
        checkNear(box.height, 718.0, kExact, "cabinet group: overall height is the union's world Z extent");
        check(box.worldAligned, "cabinet group: the world frame is chosen");
        checkNear(box.centre.X(), 318.0, kExact, "cabinet group: centre X");
        checkNear(box.centre.Z(), 359.0, kExact, "cabinet group: centre Z");
    }

    // --- prefer-world: shapes whose oriented box is free to turn ---------------
    // A sphere pins no axes at all, so OCCT's optimal OBB comes back turned
    // (measured: X axis (0.104, 0.994, -0.027)) at the same volume. With the
    // rule, the world frame wins; without it the lines would be drawn skewed.
    {
        const TopoDS_Shape knob = ModelingOps::makePrimitive(ModelingOps::PrimitiveKind::Sphere, gp_Pnt(0, 0, 0));
        const MeasuredBox box = measuredBox({knob});
        check(box.ok, "sphere: measured");
        check(box.worldAligned, "sphere: the world frame is preferred when it fits just as well");
        check(parallel(box.widthAxis, gp_Dir(1, 0, 0)), "sphere: width axis is world X, not a turned frame");
        check(parallel(box.depthAxis, gp_Dir(0, 1, 0)), "sphere: depth axis is world Y, not a turned frame");
        checkNear(box.width, 300.0, kExact, "sphere: width reads its diameter");
    }
    {
        // A group of axis-aligned pieces that includes free-turning ones - two
        // ball knobs stacked on a cone foot.
        const MeasuredBox box = measuredBox(
            {ModelingOps::makePrimitive(ModelingOps::PrimitiveKind::Sphere, gp_Pnt(0, 0, 0)),
             ModelingOps::makePrimitive(ModelingOps::PrimitiveKind::Sphere, gp_Pnt(0, 0, 300)),
             ModelingOps::makePrimitive(ModelingOps::PrimitiveKind::Cone, gp_Pnt(0, 0, -400))});
        check(box.ok, "axis-aligned free-turning group: measured");
        check(box.worldAligned, "axis-aligned free-turning group: the world frame is preferred");
        check(parallel(box.widthAxis, gp_Dir(1, 0, 0)) || parallel(box.widthAxis, gp_Dir(0, 1, 0)),
              "axis-aligned free-turning group: width axis is a world axis");
        check(box.heightAxis.IsEqual(gp_Dir(0, 0, 1), 1e-12),
              "axis-aligned free-turning group: height axis is exactly world Z");
        checkNear(box.height, 1000.0, kExact, "axis-aligned free-turning group: overall height");
    }

    // --- the tolerance, both directions -----------------------------------------
    {
        // 0.1 degree: the world box is +0.35% - inside 1%, so the world wins.
        gp_Trsf tiny;
        tiny.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0.1 * kPi / 180.0);
        const MeasuredBox box = measuredBox({transformed(board, tiny)});
        check(box.ok && box.worldAligned, "board turned 0.1 about Z: inside the tolerance, world frame");
        // 2 degrees: +8.7% - well outside, so the board's own sides win.
        gp_Trsf two;
        two.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 2.0 * kPi / 180.0);
        const MeasuredBox turned = measuredBox({transformed(board, two)});
        checkBoard(turned, "board turned 2 about Z");
        check(!turned.worldAligned, "board turned 2 about Z: outside the tolerance, oriented frame");
    }

    // --- the diagonal consequence the header states -----------------------------
    {
        const MeasuredBox box = measuredBox({ModelingOps::makeBox(gp_Pnt(0, 0, 0), 100, 100, 18),
                                             ModelingOps::makeBox(gp_Pnt(200, 200, 0), 100, 100, 18),
                                             ModelingOps::makeBox(gp_Pnt(400, 400, 0), 100, 100, 18)});
        check(box.ok && !box.worldAligned, "diagonal group: measured along the diagonal it fits");
        checkNear(box.width, 500.0 * std::sqrt(2.0), kExact, "diagonal group: width along the diagonal");
    }

    // --- the call counter ---------------------------------------------------------
    {
        const long long before = ModelingOps::measuredBoxCallCount();
        measuredBox({board});
        check(ModelingOps::measuredBoxCallCount() == before + 1, "call counter: one call counts one");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
