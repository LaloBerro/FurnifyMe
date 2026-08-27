//
// Milestone 1's first deliverable: proves the OCCT kernel integration is correct
// with no window and no GPU. If this passes, every later bug is a UI bug.
//
// Run:  ctest --test-dir build --output-on-failure
//   or: ./build/headless_geometry
//
#include "ModelingOps.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n",
                ok ? "[ ok ]" : "[FAIL]", what.c_str(), actual, expected);
    if (!ok) ++g_failures;
}

}  // namespace

int main()
{
    using namespace ModelingOps;

    // 1. Square wire, 20 x 20, on the XY plane at Z = 0.
    const std::vector<gp_Pnt> square = {
        gp_Pnt(0.0,  0.0,  0.0),
        gp_Pnt(20.0, 0.0,  0.0),
        gp_Pnt(20.0, 20.0, 0.0),
        gp_Pnt(0.0,  20.0, 0.0),
    };
    const TopoDS_Wire wire = makePolygonWire(square);
    check(!wire.IsNull(), "square wire built from 4 points");

    // 2. Face from the wire.
    const TopoDS_Face face = makeFaceFromWire(wire);
    check(!face.IsNull(), "planar face built from the wire");

    // 3. Extrude 10mm into a solid.
    const TopoDS_Shape solidA = extrude(face, gp_Dir(0.0, 0.0, 1.0), 10.0);
    check(!solidA.IsNull(),            "profile extruded 10mm");
    check(countSolids(solidA) == 1,    "extrusion yields exactly 1 solid");
    check(countFaces(solidA) == 6,     "extruded box has 6 faces");
    checkNear(volume(solidA), 4000.0, 1.0e-6, "extruded box volume is 20*20*10");

    // 4. Second box, offset so it removes one corner quadrant clean through Z.
    const TopoDS_Shape solidB = makeBox(gp_Pnt(10.0, 10.0, -5.0), 20.0, 20.0, 20.0);
    check(!solidB.IsNull(), "offset tool box built");

    // 5. Cut B from A -> L-shaped prism.
    const BooleanResult cut = applyBoolean(BooleanKind::Cut, solidA, solidB);
    check(cut.ok, "cut completed" + (cut.ok ? std::string() : ": " + cut.error));
    if (!cut.ok) {
        std::printf("\nboolean failed, aborting: %s\n", cut.error.c_str());
        return 1;
    }

    // 6. Expected topology. UnifySameDomain should leave no junk seam faces:
    //    an L-shaped prism is 6 sides + top + bottom.
    check(countSolids(cut.shape) == 1, "cut result is a single solid");
    check(countFaces(cut.shape) == 8,  "L-shaped prism has 8 faces (no junk seams)");

    // 7. Volume is positive and sane: (20*20 - 10*10) * 10.
    const double v = volume(cut.shape);
    check(v > 0.0, "cut volume is positive");
    checkNear(v, 3000.0, 1.0e-6, "cut volume matches the L-profile");

    // Tessellation must not throw or invalidate the shape.
    tessellate(cut.shape, 0.1);
    check(countFaces(cut.shape) == 8, "shape survives tessellation");

    // 8. STEP export produces a non-empty file.
    const std::string stepPath = "out.step";
    std::remove(stepPath.c_str());
    const StepResult step = exportStep(cut.shape, stepPath);
    check(step.ok, "STEP written" + (step.ok ? std::string() : ": " + step.error));

    std::ifstream written(stepPath, std::ios::binary | std::ios::ate);
    check(written.good() && written.tellg() > 0, "out.step exists and is non-empty");

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
