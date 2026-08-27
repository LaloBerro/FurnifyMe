//
// Covers the two app-layer pieces that were deliberately kept free of Qt:
// SketchController (including the ray/plane unprojection maths, the part most
// likely to be subtly wrong) and DocumentModel.
//
#include "DocumentModel.h"
#include "ModelingOps.h"
#include "SketchController.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <gp_Dir.hxx>
#include <gp_Lin.hxx>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkPoint(const gp_Pnt& actual, double x, double y, double z, const std::string& what)
{
    const bool ok = actual.Distance(gp_Pnt(x, y, z)) <= 1.0e-9;
    std::printf("%-6s %s (got %.6f, %.6f, %.6f)\n", ok ? "[ ok ]" : "[FAIL]", what.c_str(),
                actual.X(), actual.Y(), actual.Z());
    if (!ok) ++g_failures;
}

}  // namespace

int main()
{
    const gp_Pln xy(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));

    // --- ray/plane intersection -------------------------------------------
    {
        // Straight down onto the plane from above.
        gp_Pnt hit;
        const gp_Lin down(gp_Pnt(3.0, 4.0, 50.0), gp_Dir(0.0, 0.0, -1.0));
        check(SketchController::intersectRayWithPlane(down, xy, hit), "vertical ray hits XY plane");
        checkPoint(hit, 3.0, 4.0, 0.0, "vertical ray lands directly below its origin");

        // Oblique ray: from (0,0,10) heading down-and-along at 45 degrees, so it
        // must travel 10 in X while descending 10 in Z.
        gp_Pnt oblique;
        const gp_Lin slanted(gp_Pnt(0.0, 0.0, 10.0), gp_Dir(1.0, 0.0, -1.0));
        check(SketchController::intersectRayWithPlane(slanted, xy, oblique), "oblique ray hits");
        checkPoint(oblique, 10.0, 0.0, 0.0, "oblique ray lands at x = 10");

        // A ray parallel to the plane must fail rather than return a garbage point.
        gp_Pnt unused(-99.0, -99.0, -99.0);
        const gp_Lin parallel(gp_Pnt(0.0, 0.0, 5.0), gp_Dir(1.0, 0.0, 0.0));
        check(!SketchController::intersectRayWithPlane(parallel, xy, unused),
              "ray parallel to the plane reports no hit");
        checkPoint(unused, -99.0, -99.0, -99.0, "failed intersection leaves the output untouched");
    }

    // --- sketch accumulation ----------------------------------------------
    {
        SketchController sketch;
        check(sketch.pointCount() == 0, "new sketch is empty");
        check(!sketch.canClose(), "cannot close an empty sketch");
        check(sketch.previewShape().IsNull(), "no preview with no points");

        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        check(sketch.previewShape().IsNull(), "no preview with a single point");

        sketch.addPoint(gp_Pnt(10.0, 0.0, 0.0));
        check(!sketch.previewShape().IsNull(), "preview appears at 2 points");
        check(!sketch.canClose(), "2 points is not closable");
        check(sketch.closedFace().IsNull(), "no face from 2 points");

        sketch.addPoint(gp_Pnt(10.0, 10.0, 0.0));
        check(sketch.canClose(), "3 points is closable");

        sketch.addPoint(gp_Pnt(0.0, 10.0, 0.0));
        const TopoDS_Face face = sketch.closedFace();
        check(!face.IsNull(), "square face built from 4 points");

        const TopoDS_Shape solid = ModelingOps::extrude(face, sketch.plane().Axis().Direction(), 5.0);
        check(std::fabs(ModelingOps::volume(solid) - 500.0) < 1.0e-6,
              "extruding the sketched square gives volume 10*10*5");

        sketch.removeLastPoint();
        check(sketch.pointCount() == 3, "undo removes exactly one point");

        sketch.reset();
        check(sketch.pointCount() == 0, "reset clears the sketch");
    }

    // --- document -----------------------------------------------------------
    {
        DocumentModel doc;
        check(doc.count() == 0, "new document is empty");
        check(doc.addSolid(TopoDS_Shape()) == 0, "a null shape is rejected and gets no id");

        const TopoDS_Shape boxA = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0);
        const TopoDS_Shape boxB = ModelingOps::makeBox(gp_Pnt(20.0, 0.0, 0.0), 5.0, 5.0, 5.0);

        const int idA = doc.addSolid(boxA);
        const int idB = doc.addSolid(boxB);
        check(idA != 0 && idB != 0 && idA != idB, "ids are non-zero and distinct");
        check(doc.count() == 2, "document holds 2 solids");
        check(std::fabs(ModelingOps::volume(doc.shapeOf(idA)) - 1000.0) < 1.0e-6,
              "shapeOf returns the right solid");

        check(doc.removeSolid(idA), "removing a known id succeeds");
        check(!doc.removeSolid(idA), "removing it twice fails");
        check(!doc.contains(idA), "removed id is gone");
        check(doc.contains(idB), "the other solid survives");

        // Ids must not be recycled: a stale id silently resolving to a different
        // solid is exactly the class of bug the naming problem creates later.
        const int idC = doc.addSolid(boxA);
        check(idC != idA, "ids are not reused after removal");

        doc.clear();
        check(doc.count() == 0, "clear empties the document");
        check(doc.addSolid(boxA) != idC, "ids still advance after clear");
    }

    // --- compound for STEP export -------------------------------------------
    {
        const TopoDS_Shape one = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0);
        const TopoDS_Shape two = ModelingOps::makeBox(gp_Pnt(5.0, 0.0, 0.0), 2.0, 2.0, 2.0);

        check(ModelingOps::makeCompound({}).IsNull(), "empty compound is null");
        check(ModelingOps::countSolids(ModelingOps::makeCompound({one})) == 1,
              "single-shape compound passes the shape through");

        const TopoDS_Shape both = ModelingOps::makeCompound({one, two});
        check(ModelingOps::countSolids(both) == 2, "compound holds both solids");
        check(std::fabs(ModelingOps::volume(both) - 9.0) < 1.0e-6,
              "compound volume is the sum of its parts");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
