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

    // --- grid snapping ------------------------------------------------------
    {
        // Snapping happens in the plane's own coordinates, so it stays correct
        // when the sketch plane is not the XY plane.
        gp_Pnt snapped = SketchController::snapToPlaneGrid(gp_Pnt(12.4, 27.6, 0.0), xy, 10.0);
        checkPoint(snapped, 10.0, 30.0, 0.0, "snaps to the nearest 10mm grid intersection");

        snapped = SketchController::snapToPlaneGrid(gp_Pnt(30.0, -20.0, 0.0), xy, 10.0);
        checkPoint(snapped, 30.0, -20.0, 0.0, "a point already on the grid is unchanged");

        // The real coordinate from a manual test run, which is what motivated this.
        snapped = SketchController::snapToPlaneGrid(gp_Pnt(-4.35, -129.32, 0.0), xy, 10.0);
        checkPoint(snapped, 0.0, -130.0, 0.0, "negative coordinates round to nearest, not toward zero");

        snapped = SketchController::snapToPlaneGrid(gp_Pnt(15.0, 25.0, 0.0), xy, 10.0);
        checkPoint(snapped, 20.0, 30.0, 0.0, "exact half-steps round up");

        // A zero or negative step would divide by zero / invert the grid.
        snapped = SketchController::snapToPlaneGrid(gp_Pnt(3.7, 4.2, 0.0), xy, 0.0);
        checkPoint(snapped, 3.7, 4.2, 0.0, "a zero step leaves the point untouched");

        // Snapping must not lift the point off its plane.
        const gp_Pln raised(gp_Pnt(0.0, 0.0, 50.0), gp_Dir(0.0, 0.0, 1.0));
        snapped = SketchController::snapToPlaneGrid(gp_Pnt(12.4, 27.6, 50.0), raised, 10.0);
        checkPoint(snapped, 10.0, 30.0, 50.0, "snapped point stays on its own plane");
    }

    // --- straight continuation (Shift) ---------------------------------------
    {
        const gp_Pnt prev(50.0, 0.0, 0.0);
        const gp_Dir alongX(1.0, 0.0, 0.0);

        // A candidate already on the line is its own projection.
        checkPoint(SketchController::snapToDirection(prev, alongX, gp_Pnt(120.0, 0.0, 0.0)),
                   120.0, 0.0, 0.0, "a point already on the line is unchanged");

        // An off-line candidate lands on the line, at the foot of the
        // perpendicular - not at the nearest placed point.
        const gp_Pnt off =
            SketchController::snapToDirection(prev, alongX, gp_Pnt(120.0, 37.0, 0.0));
        checkPoint(off, 120.0, 0.0, 0.0, "an off-line point projects onto the line");
        check(gp_Vec(prev, off).Crossed(gp_Vec(alongX)).Magnitude() < 1.0e-9,
              "and is collinear with the direction it was snapped to (cross product ~ 0)");

        // The line extends BOTH ways: a cursor dragged back past `prev` still
        // continues the same straight run.
        checkPoint(SketchController::snapToDirection(prev, alongX, gp_Pnt(-30.0, 12.0, 0.0)),
                   -30.0, 0.0, 0.0, "the constraint is a line, not a ray - it extends backwards");

        // A direction that is not a world axis, on a plane that is not the
        // ground: the snapped point must stay ON the sketch plane.
        const gp_Pln vertical(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0));
        const gp_Pnt vPrev(10.0, 0.0, 10.0);
        const gp_Dir diagonal(1.0, 0.0, 1.0);
        const gp_Pnt vSnapped =
            SketchController::snapToDirection(vPrev, diagonal, gp_Pnt(60.0, 0.0, 20.0));
        check(vertical.Distance(vSnapped) < 1.0e-9,
              "a snapped point on a locked vertical plane stays on that plane");
        check(gp_Vec(vPrev, vSnapped).Crossed(gp_Vec(diagonal)).Magnitude() < 1.0e-9,
              "and lies on the diagonal it was snapped to");
        checkPoint(vSnapped, 40.0, 0.0, 40.0,
                   "at the foot of the perpendicular from the candidate");

        // The direction itself: fewer than two points means Shift has nothing
        // to continue, and two coincident points are the one way a
        // zero-length direction could reach gp_Dir's raising constructor.
        SketchController sketch;
        gp_Dir dir;
        check(!sketch.lastSegmentDirection(dir), "an empty sketch has no segment to continue");
        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        check(!sketch.lastSegmentDirection(dir), "nor does a sketch with one point");
        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        check(!sketch.lastSegmentDirection(dir),
              "two coincident points give no direction rather than a raised construction");
        sketch.addPoint(gp_Pnt(0.0, 80.0, 0.0));
        check(sketch.lastSegmentDirection(dir) && dir.IsEqual(gp_Dir(0.0, 1.0, 0.0), 1.0e-9),
              "two distinct points give the direction of the segment between them");
        // It follows the LAST segment, not the first.
        sketch.addPoint(gp_Pnt(60.0, 80.0, 0.0));
        check(sketch.lastSegmentDirection(dir) && dir.IsEqual(gp_Dir(1.0, 0.0, 0.0), 1.0e-9),
              "and it follows the most recent segment, not the first one drawn");
    }

    // --- closing the sketch by clicking the first point ----------------------
    {
        SketchController sketch;
        check(!sketch.isNearFirstPoint(gp_Pnt(0.0, 0.0, 0.0), 5.0),
              "an empty sketch has no first point to close on");

        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(50.0, 0.0, 0.0));
        check(!sketch.isNearFirstPoint(gp_Pnt(0.0, 0.0, 0.0), 5.0),
              "2 points cannot be closed by clicking the start");

        sketch.addPoint(gp_Pnt(50.0, 50.0, 0.0));
        check(sketch.isNearFirstPoint(gp_Pnt(2.0, 2.0, 0.0), 5.0),
              "clicking within tolerance of the first point closes a 3-point sketch");
        check(!sketch.isNearFirstPoint(gp_Pnt(20.0, 20.0, 0.0), 5.0),
              "a click far from the first point does not close");
        check(!sketch.isNearFirstPoint(gp_Pnt(48.0, 48.0, 0.0), 5.0),
              "clicking near the LAST point does not close");
    }

    // --- rubber-band preview -------------------------------------------------
    {
        SketchController sketch;
        check(sketch.previewShapeWithCursor(gp_Pnt(10.0, 10.0, 0.0)).IsNull(),
              "no rubber band before the first point is placed");

        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        check(!sketch.previewShapeWithCursor(gp_Pnt(10.0, 10.0, 0.0)).IsNull(),
              "one placed point plus the cursor draws a segment");
        check(sketch.previewShape().IsNull(),
              "the committed preview is still empty with a single point");

        sketch.addPoint(gp_Pnt(50.0, 0.0, 0.0));
        const TopoDS_Shape band = sketch.previewShapeWithCursor(gp_Pnt(50.0, 50.0, 0.0));
        check(!band.IsNull(), "rubber band follows the cursor past the second point");
        check(sketch.pointCount() == 2, "asking for the rubber band does not commit the cursor");
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

    // --- solid names ---------------------------------------------------------
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0);

        const int first = doc.addSolid(box);
        const int second = doc.addSolid(box);
        check(doc.nameOf(first) == "Body 01", "first solid is named Body 01");
        check(doc.nameOf(second) == "Body 02", "second solid is named Body 02");
        check(doc.nameOf(9999).empty(), "an unknown id has no name");

        check(doc.renameSolid(first, "Table Top"), "rename succeeds for a known id");
        check(doc.nameOf(first) == "Table Top", "the new name sticks");
        check(!doc.renameSolid(9999, "Nope"), "rename fails for an unknown id");

        // Names must not be recycled, for the same reason ids are not: a name
        // reappearing on a different solid is confusing in the Items panel.
        doc.removeSolid(second);
        const int third = doc.addSolid(box);
        check(doc.nameOf(third) == "Body 03", "names keep advancing after a removal");

        // Undo restores the solids it captured, names included.
        DocumentModel undoDoc;
        undoDoc.addSolid(box);
        undoDoc.renameSolid(1, "Renamed");
        undoDoc.checkpoint();
        undoDoc.addSolid(box);
        undoDoc.undo();
        check(undoDoc.count() == 1, "undo left one solid");
        check(undoDoc.nameOf(1) == "Renamed", "undo restores the name with the solid");
    }

    // --- setItemName (Milestone 3, Task 5: the Items drawer's rename) --------
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0);
        const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        SketchController itemNameSketch;
        itemNameSketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        itemNameSketch.addPoint(gp_Pnt(1.0, 0.0, 0.0));
        itemNameSketch.addPoint(gp_Pnt(1.0, 1.0, 0.0));
        itemNameSketch.addPoint(gp_Pnt(0.0, 1.0, 0.0));
        const TopoDS_Face face = itemNameSketch.closedFace();
        check(!face.IsNull(), "the fixture outline for the setItemName block closes");

        const int bodyId = doc.addSolid(box);
        const int outlineId = doc.addOutline(face, ground);

        // setItemName is "the one setter both a UI rename and a file load go
        // through" (DocumentModel.h's own comment) - it has to reach whichever
        // kind of item the id names, body or outline, through the SAME call.
        check(doc.setItemName(bodyId, "Table Top"), "renames a body through setItemName");
        check(doc.nameOf(bodyId) == "Table Top", "the body's new name sticks");
        check(doc.setItemName(outlineId, "Side Panel"),
              "renames an outline through setItemName");
        check(doc.outlineNameOf(outlineId) == "Side Panel", "the outline's new name sticks");
        check(!doc.setItemName(9999, "Nope"), "setItemName fails for an unknown id");

        // The revision bump - Task 5's fix. Every OTHER mutator in this file
        // bumps myRevision (see revision()'s own comment); setItemName did
        // not, which left a rename invisible to the dirty star, autosave's
        // arm and a toast's own revision guard. Both branches (body and
        // outline) have to bump it, and a failed rename must not.
        const int beforeBodyRename = doc.revision();
        doc.setItemName(bodyId, "Renamed Again");
        check(doc.revision() > beforeBodyRename, "renaming a body bumps the revision");
        const int beforeOutlineRename = doc.revision();
        doc.setItemName(outlineId, "Renamed Again Too");
        check(doc.revision() > beforeOutlineRename, "renaming an outline bumps the revision");
        const int beforeFailedRename = doc.revision();
        doc.setItemName(9999, "Nope");
        check(doc.revision() == beforeFailedRename,
              "a rename that fails (unknown id) does not bump the revision");
    }

    // --- undo / redo of document state --------------------------------------
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0);

        check(!doc.canUndo(), "a fresh document has nothing to undo");
        check(!doc.canRedo(), "a fresh document has nothing to redo");
        check(!doc.undo(), "undo on an empty history reports failure");

        doc.checkpoint();
        doc.addSolid(box);
        check(doc.count() == 1, "solid added after the checkpoint");
        check(doc.canUndo(), "the checkpoint is undoable");

        check(doc.undo(), "undo succeeds");
        check(doc.count() == 0, "undo removes the solid added after the checkpoint");
        check(doc.canRedo(), "undo makes a redo available");

        check(doc.redo(), "redo succeeds");
        check(doc.count() == 1, "redo puts the solid back");
        check(!doc.canRedo(), "nothing left to redo after redoing");

        // A new action after an undo must discard the redo branch, or redo would
        // resurrect a state that never followed from the current one.
        doc.undo();
        check(doc.canRedo(), "redo is available again after another undo");
        doc.checkpoint();
        doc.addSolid(box);
        check(!doc.canRedo(), "a new action discards the redo branch");

        // Ids must not be recycled across an undo either: a stale id resolving to
        // a different solid is the bug this guards against.
        DocumentModel fresh;
        fresh.checkpoint();
        const int firstId = fresh.addSolid(box);
        fresh.undo();
        fresh.checkpoint();
        const int secondId = fresh.addSolid(box);
        check(firstId != secondId, "ids are not reused after an undo");
    }

    // --- undo history is bounded ---------------------------------------------
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0);

        const std::size_t overflow = DocumentModel::kMaxHistory + 5;
        for (std::size_t i = 0; i < overflow; ++i) {
            doc.checkpoint();
            doc.addSolid(box);
        }
        check(doc.count() == overflow, "every solid was added");

        std::size_t undone = 0;
        while (doc.undo()) ++undone;
        check(undone == DocumentModel::kMaxHistory,
              "history is capped, so only the most recent steps can be undone");
        check(doc.count() == 5, "the states beyond the cap are gone for good");
    }

    // --- a sketch plane that is not the ground -------------------------------
    // A point unprojected onto a non-XY plane must land on that plane, and
    // snapping must not lift it off.
    {
        const gp_Pln vertical(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));   // the XZ plane
        const gp_Lin ray(gp_Pnt(50, -100, 30), gp_Dir(0, 1, 0));
        gp_Pnt hit;
        check(SketchController::intersectRayWithPlane(ray, vertical, hit),
              "a ray meeting a vertical plane intersects it");
        check(std::fabs(vertical.Distance(hit)) < 1e-9,
              "the hit lies on the plane it was cast at");

        const gp_Pnt snapped = SketchController::snapToPlaneGrid(hit, vertical, 10.0);
        check(std::fabs(vertical.Distance(snapped)) < 1e-9,
              "snapping keeps the point on its own plane");

        // The whole point of a locked face: an outline drawn on it extrudes
        // perpendicular to it, not straight up. The direction the app uses is
        // the plane's own axis, so assert that is what a vertical plane gives.
        SketchController sketch;
        sketch.setPlane(vertical);
        check(sketch.plane().Axis().Direction().IsParallel(gp_Dir(0, 1, 0), 1.0e-7),
              "the sketch pushes out along the locked plane's normal, not +Z");

        sketch.addPoint(gp_Pnt(0, 0, 0));
        sketch.addPoint(gp_Pnt(100, 0, 0));
        sketch.addPoint(gp_Pnt(100, 0, 60));
        sketch.addPoint(gp_Pnt(0, 0, 60));
        const TopoDS_Face face = sketch.closedFace();
        check(!face.IsNull(), "an outline drawn on a vertical plane closes into a face");

        const TopoDS_Shape body =
            ModelingOps::extrude(face, sketch.plane().Axis().Direction(), 18.0);
        check(!body.IsNull(), "and extrudes into a body");
        check(!body.IsNull() && std::fabs(ModelingOps::volume(body) - 100.0 * 60.0 * 18.0) < 1.0e-6,
              "whose volume is the outline's area times the height");
    }

    // --- outlines are document items -----------------------------------------
    // Phase 7, item 4. A closed outline stops being a bare member of
    // MainWindow and becomes something the document owns, names, numbers and
    // rolls back - which is the whole reason these checks live down here in
    // the Qt-free suite rather than only in gui_smoke.
    {
        DocumentModel doc;
        const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));

        SketchController sketch;
        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(340.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(340.0, 220.0, 0.0));
        sketch.addPoint(gp_Pnt(0.0, 220.0, 0.0));
        const TopoDS_Face face = sketch.closedFace();
        check(!face.IsNull(), "the fixture outline closes into a face");

        check(doc.addOutline(TopoDS_Face(), ground) == 0,
              "a null face makes no outline");
        check(doc.outlineCount() == 0, "and leaves the list empty");

        const int firstOutline = doc.addOutline(face, ground);
        check(firstOutline != 0, "a closed face becomes an outline item");
        check(doc.outlineCount() == 1, "which the document lists");
        check(doc.count() == 0, "and which is not counted as a body");
        check(doc.outlineNameOf(firstOutline) == "Outline 01",
              "named with the vocabulary's word and the bodies' numbering");
        check(doc.containsOutline(firstOutline), "and found by id");
        check(!doc.contains(firstOutline),
              "while contains() - the BODY lookup - does not claim it");

        gp_Pln stored;
        check(doc.outlinePlane(firstOutline, stored) &&
                  stored.Axis().Direction().IsEqual(ground.Axis().Direction(), 1.0e-9),
              "the plane it was drawn on is stored with it");
        gp_Pln untouched(gp_Pnt(1.0, 2.0, 3.0), gp_Dir(1.0, 0.0, 0.0));
        check(!doc.outlinePlane(firstOutline + 999, untouched) &&
                  std::fabs(untouched.Location().X() - 1.0) < 1.0e-12,
              "an unknown id reports failure and leaves the output alone");

        // Numbering continues across a second outline, and the two ids come
        // out of the same counter the bodies use - so an outline id can never
        // be mistaken for a body id.
        const int secondOutline = doc.addOutline(face, ground);
        check(doc.outlineNameOf(secondOutline) == "Outline 02", "the second is Outline 02");
        const int bodyId = doc.addSolid(
            ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0));
        check(bodyId != firstOutline && bodyId != secondOutline,
              "a body's id never collides with an outline's");
        check(doc.nameOf(bodyId) == "Body 01",
              "and the two kinds number independently");

        check(doc.removeOutline(secondOutline), "an outline can be removed by id");
        check(!doc.removeOutline(secondOutline), "and removing it twice reports failure");
        check(doc.outlineCount() == 1, "leaving the other one alone");

        // The revision has to move on an outline change, or a toast naming
        // one would survive the change that replaced it - the exact defect
        // ToastHost's revision stamp exists to end.
        const int beforeAdd = doc.revision();
        const int third = doc.addOutline(face, ground);
        check(doc.revision() > beforeAdd, "adding an outline bumps the revision");
        const int beforeRemove = doc.revision();
        doc.removeOutline(third);
        check(doc.revision() > beforeRemove, "and so does removing one");
    }

    // --- the extrude conversion is ONE change --------------------------------
    {
        DocumentModel doc;
        const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        SketchController sketch;
        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(40.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(40.0, 20.0, 0.0));
        sketch.addPoint(gp_Pnt(0.0, 20.0, 0.0));
        const TopoDS_Face face = sketch.closedFace();
        const TopoDS_Shape body = ModelingOps::extrude(face, gp_Dir(0.0, 0.0, 1.0), 18.0);
        check(!body.IsNull(), "the fixture outline extrudes into a body");

        const int outlineId = doc.addOutline(face, ground);
        check(doc.convertOutlineToBody(outlineId, TopoDS_Shape()) == 0,
              "a null body refuses the conversion");
        check(doc.outlineCount() == 1,
              "and leaves the outline exactly where it was - a refused conversion "
              "must not destroy the work it was given");
        check(doc.convertOutlineToBody(outlineId + 999, body) == 0,
              "an unknown outline refuses the conversion too");
        check(doc.count() == 0, "adding no body in the process");

        const int beforeRevision = doc.revision();
        // The commit idiom: the CALLER checkpoints, once, around the whole
        // conversion.
        doc.checkpoint();
        const int newBody = doc.convertOutlineToBody(outlineId, body);
        check(newBody != 0, "the conversion reports the new body's id");
        check(doc.outlineCount() == 0 && doc.count() == 1,
              "the outline is gone and the body is there");
        check(doc.revision() == beforeRevision + 1,
              "and the pair counted as ONE revision, not two");

        // The whole point of the single checkpoint: one undo walks both halves.
        check(doc.undo(), "one undo");
        check(doc.outlineCount() == 1 && doc.count() == 0,
              "puts the outline back AND removes the body");
        check(doc.outlineNameOf(doc.outlines().front().id) == "Outline 01",
              "with its name intact");
        gp_Pln restored;
        check(doc.outlinePlane(doc.outlines().front().id, restored) &&
                  restored.Axis().Direction().IsEqual(gp_Dir(0.0, 0.0, 1.0), 1.0e-9),
              "and its plane intact, so it still extrudes the same way");

        check(doc.redo(), "one redo");
        check(doc.outlineCount() == 0 && doc.count() == 1,
              "converts it again in a single step");
    }

    // --- the whole lifecycle, undone and redone ------------------------------
    // Points are not in the document at all; closing puts an outline in it;
    // extruding converts it; two undos walk back out; two redos walk back in.
    {
        DocumentModel doc;
        const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        SketchController sketch;
        sketch.addPoint(gp_Pnt(0.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(30.0, 0.0, 0.0));
        sketch.addPoint(gp_Pnt(30.0, 30.0, 0.0));
        check(doc.outlineCount() == 0 && doc.count() == 0 && !doc.canUndo(),
              "placing points puts nothing in the document and nothing on the stack");

        sketch.addPoint(gp_Pnt(0.0, 30.0, 0.0));
        const TopoDS_Face face = sketch.closedFace();
        doc.checkpoint();
        doc.addOutline(face, ground);
        check(doc.outlineCount() == 1 && doc.count() == 0,
              "closing the outline makes it an item");

        doc.checkpoint();
        doc.convertOutlineToBody(doc.outlines().front().id,
                                 ModelingOps::extrude(face, gp_Dir(0.0, 0.0, 1.0), 12.0));
        check(doc.outlineCount() == 0 && doc.count() == 1, "extruding converts it");

        doc.undo();
        check(doc.outlineCount() == 1 && doc.count() == 0,
              "the first undo restores the outline and removes the body");
        doc.undo();
        check(doc.outlineCount() == 0 && doc.count() == 0,
              "the second undo takes the outline away too");
        check(!doc.canUndo(), "and there is nothing left to undo");

        doc.redo();
        check(doc.outlineCount() == 1 && doc.count() == 0, "the first redo brings it back");
        doc.redo();
        check(doc.outlineCount() == 0 && doc.count() == 1, "and the second re-extrudes it");
        check(!doc.canRedo(), "with nothing left to redo");

        // clear() has to take both lists, or an outline would outlive the
        // document it belonged to.
        doc.checkpoint();
        doc.addOutline(face, ground);
        doc.clear();
        check(doc.outlineCount() == 0 && doc.count() == 0, "clear() empties both lists");
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
