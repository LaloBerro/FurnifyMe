//
// Booleans that take more than two bodies, and the region a preview draws.
//
// The user's report is the whole reason this file exists: "right now works
// poorly, for example i cant decide with one substract and which one keep".
// Two separate gaps behind that sentence, and only the first is about the UI:
//
//   - APPLYING. applyBoolean() takes exactly two shapes, so six dowel holes
//     are six operations, six checkpoints and six undos. OCCT's own
//     BRepAlgoAPI_BooleanOperation already takes ARGUMENT and TOOL LISTS, so
//     this is a generalization of the call it already makes, not a loop
//     around it - except for Intersect, which is a fold, for the reason its
//     own block below states.
//   - SHOWING. Nothing anywhere computes the volume an operation will act on,
//     so the tool commits blind. booleanRegion() is that volume, and every
//     check on it here is VOLUME ARITHMETIC rather than a picture: the
//     numbers are chosen so each operation's answer is a different number,
//     which is what stops a wrong implementation reading as right.
//
// The fixture, once, so every expected number below can be read off it:
//
//   base   box at (0,0,0)    100 x 100 x 20  = 200,000
//   toolA  box at (10,10,10)  20 x  20 x 20  =   8,000, overlapping the base
//                                                       by 20x20x10 = 4,000
//   toolB  box at (50,10,10)  20 x  20 x 20  =   8,000, overlapping by 4,000
//   far    box at (500,0,0)   20 x  20 x 20  =   8,000, touching nothing
//
// toolA and toolB do not touch each other, which is what makes the two-tool
// numbers add rather than needing an inclusion-exclusion term.
//
// Run:  ctest --preset windows-headless --output-on-failure
//   or: ./build-headless/RelWithDebInfo/headless_boolean_multi
//
#include "ModelingOps.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>

using ModelingOps::BooleanKind;
using ModelingOps::BooleanResult;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    std::printf("%s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

// A cubic millimetre either way. Every expected number here is a whole
// thousand, so this is three orders tighter than any wrong answer could be.
constexpr double kVolumeTolerance = 1.0;

void checkVolume(const TopoDS_Shape& shape, double expected, const std::string& what)
{
    const double actual = ModelingOps::volume(shape);
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s (%.1f, expected %.1f)", what.c_str(), actual,
                  expected);
    check(std::fabs(actual - expected) < kVolumeTolerance, buffer);
}

int solidCount(const TopoDS_Shape& shape)
{
    int n = 0;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) ++n;
    return n;
}

TopoDS_Shape boxAt(double x, double y, double z, double dx, double dy, double dz)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape();
}

}  // namespace

int main()
{
    const TopoDS_Shape base = boxAt(0, 0, 0, 100, 100, 20);
    const TopoDS_Shape toolA = boxAt(10, 10, 10, 20, 20, 20);
    const TopoDS_Shape toolB = boxAt(50, 10, 10, 20, 20, 20);
    const TopoDS_Shape far = boxAt(500, 0, 0, 20, 20, 20);

    checkVolume(base, 200000.0, "fixture: the base is 100 x 100 x 20");
    checkVolume(toolA, 8000.0, "fixture: tool A is 20 x 20 x 20");
    checkVolume(toolB, 8000.0, "fixture: tool B is 20 x 20 x 20");

    // --- Subtract, two tools in ONE build ---------------------------------
    {
        const BooleanResult cut =
            ModelingOps::applyBooleanMulti(BooleanKind::Cut, base, {toolA, toolB});
        check(cut.ok, "subtract: two tools in one build is accepted");
        if (cut.ok) {
            // Each tool sinks 10 mm into a 20 mm slab over a 20 x 20 footprint,
            // so each takes 4,000 out and they do not touch each other.
            checkVolume(cut.shape, 200000.0 - 4000.0 - 4000.0,
                        "subtract: both tools came out in one operation");
            check(solidCount(cut.shape) == 1,
                  "subtract: and the result is still ONE body, not a compound of pieces");
        }

        // The SAME two tools one at a time must land on the same volume - the
        // multi-tool path is a generalization, not a different operation. This
        // is what would catch a tool list that silently dropped an entry.
        const BooleanResult first = ModelingOps::applyBoolean(BooleanKind::Cut, base, toolA);
        check(first.ok, "subtract: the first tool alone is accepted");
        if (first.ok) {
            const BooleanResult second =
                ModelingOps::applyBoolean(BooleanKind::Cut, first.shape, toolB);
            check(second.ok, "subtract: the second tool alone is accepted");
            if (second.ok && cut.ok) {
                checkVolume(second.shape, ModelingOps::volume(cut.shape),
                            "subtract: one build equals two builds, to the cubic millimetre");
            }
        }
    }

    // --- Union, two tools in one build ------------------------------------
    {
        const BooleanResult fuse =
            ModelingOps::applyBooleanMulti(BooleanKind::Fuse, base, {toolA, toolB});
        check(fuse.ok, "union: two tools in one build is accepted");
        if (fuse.ok) {
            // 200,000 + 8,000 + 8,000 less the 4,000 each one already shared
            // with the base.
            checkVolume(fuse.shape, 200000.0 + 8000.0 + 8000.0 - 4000.0 - 4000.0,
                        "union: both tools joined, each counted once");
            check(solidCount(fuse.shape) == 1, "union: and the result is one body");
        }
    }

    // --- Intersect is a FOLD, and that is a ruling, not an implementation
    // detail. OCCT's multi-tool Common answers base AND (A OR B) - the union
    // of the two overlaps, 8,000 here. The word "Intersect" means the volume
    // common to EVERY body picked, which for three bodies is
    // ((base AND A) AND B) and is EMPTY here, since A and B do not touch. The
    // two readings differ by 8,000 mm3 on this very fixture, so the check
    // below can tell them apart - which is exactly why the fixture is shaped
    // this way.
    {
        const BooleanResult common =
            ModelingOps::applyBooleanMulti(BooleanKind::Common, base, {toolA, toolB});
        check(!common.ok,
              "intersect: three bodies with no volume common to ALL of them is refused");
        check(common.shape.IsNull(),
              "intersect: and the refusal carries a null shape, never an empty body");
        check(!common.error.empty(), "intersect: and it says why");

        // Two bodies that DO share a volume still work, and give the overlap.
        const BooleanResult pair =
            ModelingOps::applyBooleanMulti(BooleanKind::Common, base, {toolA});
        check(pair.ok, "intersect: two bodies that overlap are accepted");
        if (pair.ok) checkVolume(pair.shape, 4000.0, "intersect: and it is the shared volume");
    }

    // --- AN EMPTY RESULT IS A REFUSAL, on every kind -----------------------
    //
    // This is a real hole in the two-body path, not a hypothetical: Intersect
    // on two bodies that do not touch builds cleanly and hands back an empty
    // compound, which the app would have added to the document as a body with
    // no volume - a body the user can select, name and never see. "Never
    // surface a failed boolean as a success" (CLAUDE.md) covers a refusal the
    // kernel reports; this is the case where the kernel reports success and
    // the ANSWER is empty.
    {
        const BooleanResult miss =
            ModelingOps::applyBooleanMulti(BooleanKind::Common, base, {far});
        check(!miss.ok, "intersect: two bodies that never touch are refused");
        check(miss.shape.IsNull(), "intersect: with a null shape");

        // Subtracting a body that misses is NOT empty and NOT a refusal - the
        // base comes back whole. Kept beside the check above so the empty
        // rule cannot quietly widen into "anything that misses is refused".
        const BooleanResult cutMiss =
            ModelingOps::applyBooleanMulti(BooleanKind::Cut, base, {far});
        check(cutMiss.ok, "subtract: a tool that misses is accepted");
        if (cutMiss.ok)
            checkVolume(cutMiss.shape, 200000.0, "subtract: and the base is untouched");
    }

    // --- the region a preview draws ---------------------------------------
    {
        // SUBTRACT: the region is what comes OUT - the base's own share of the
        // tools, 4,000 each. Not the tools themselves (16,000), which is the
        // wrong answer a careless implementation gives and which this number
        // is chosen to separate from.
        const BooleanResult cutRegion =
            ModelingOps::booleanRegion(BooleanKind::Cut, base, {toolA, toolB});
        check(cutRegion.ok, "region: subtract has a region");
        if (cutRegion.ok) {
            checkVolume(cutRegion.shape, 8000.0,
                        "region: subtract highlights the material coming out, not the whole tool");
            check(solidCount(cutRegion.shape) == 2,
                  "region: and it is two separate pieces, one per tool");
        }

        // UNION: the same overlap, because that is the doubled wood that
        // becomes one piece. Same number, different meaning - which is why the
        // colour and not the shape is what changes in the viewport.
        const BooleanResult fuseRegion =
            ModelingOps::booleanRegion(BooleanKind::Fuse, base, {toolA, toolB});
        check(fuseRegion.ok, "region: union has a region");
        if (fuseRegion.ok)
            checkVolume(fuseRegion.shape, 8000.0,
                        "region: union highlights the doubled material");

        // INTERSECT: the region is the RESULT - what survives - so it follows
        // the fold and refuses on the same three bodies the operation does.
        const BooleanResult commonRegion =
            ModelingOps::booleanRegion(BooleanKind::Common, base, {toolA});
        check(commonRegion.ok, "region: intersect on an overlapping pair has a region");
        if (commonRegion.ok)
            checkVolume(commonRegion.shape, 4000.0,
                        "region: intersect highlights what is left, which IS the result");

        // A tool that misses has no region on any kind, and that is not a
        // failure - there is simply nothing to draw. The distinction matters
        // because the UI must show an empty highlight rather than a refusal.
        const BooleanResult noRegion =
            ModelingOps::booleanRegion(BooleanKind::Cut, base, {far});
        check(noRegion.ok, "region: a tool that misses still answers");
        if (noRegion.ok)
            check(ModelingOps::volume(noRegion.shape) < kVolumeTolerance,
                  "region: with nothing in it - an empty highlight, not a refusal");
    }

    // --- refusals -----------------------------------------------------------
    {
        const BooleanResult noTools = ModelingOps::applyBooleanMulti(BooleanKind::Cut, base, {});
        check(!noTools.ok, "refusal: no tools at all is refused");
        check(noTools.shape.IsNull(), "refusal: with a null shape");

        const BooleanResult nullBase =
            ModelingOps::applyBooleanMulti(BooleanKind::Cut, TopoDS_Shape(), {toolA});
        check(!nullBase.ok, "refusal: a null base is refused");

        const BooleanResult nullTool =
            ModelingOps::applyBooleanMulti(BooleanKind::Cut, base, {toolA, TopoDS_Shape()});
        check(!nullTool.ok,
              "refusal: ONE null tool refuses the whole call - a partial build leaves the "
              "user working out which tools took");
        check(nullTool.shape.IsNull(), "refusal: with a null shape");
    }

    std::printf("\n%s (%d failure%s, %d checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s", g_checks);
    return g_failures == 0 ? 0 : 1;
}
