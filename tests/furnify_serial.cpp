//
// Covers FurnifySerial (the Qt-free BinTools_ShapeSet round-trip for a
// document's shapes) and DocumentModel's serialization additions: stored
// per-item names, presentation visibility kept out of undo, and
// toSerialized()/fromSerialized(). No window, no GPU - see CLAUDE.md's
// headless-test discipline.
//
#include "DocumentModel.h"
#include "FurnifySerial.h"
#include "ModelingOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <BinTools.hxx>
#include <BinTools_ShapeSet.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

TopoDS_Face makeSquareFace(double x0, double y0, double side, const gp_Pln& plane)
{
    std::vector<gp_Pnt> pts = {
        gp_Pnt(x0, y0, 0.0),
        gp_Pnt(x0 + side, y0, 0.0),
        gp_Pnt(x0 + side, y0 + side, 0.0),
        gp_Pnt(x0, y0 + side, 0.0),
    };
    // Fixture only builds faces on the ground plane (Z=0); callers that
    // want a non-ground plane pass it separately for the outline's stored
    // plane, which is fine since FurnifySerial treats the plane and the
    // face as independent fields.
    (void)plane;
    return ModelingOps::makeFaceFromWire(ModelingOps::makePolygonWire(pts));
}

}  // namespace

int main()
{
    const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));

    // --- FurnifySerial: round-trip a two-body + one-outline document -------
    {
        FurnifySerial::SerializedDocument doc;
        doc.bodies.push_back(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 20.0, 5.0));
        doc.bodies.push_back(ModelingOps::makeBox(gp_Pnt(50.0, 0.0, 0.0), 3.0, 3.0, 3.0));
        doc.outlineFaces.push_back(makeSquareFace(0.0, 0.0, 40.0, ground));
        doc.outlinePlanes.push_back(ground);

        std::ostringstream out(std::ios::binary);
        const FurnifySerial::SerialResult written = FurnifySerial::writeShapes(doc, out);
        check(written.ok, "writeShapes succeeds on a well-formed document");
        check(!out.str().empty(), "and produces a non-empty blob");

        FurnifySerial::SerializedDocument roundTripped;
        std::istringstream in(out.str(), std::ios::binary);
        const FurnifySerial::SerialResult read = FurnifySerial::readShapes(in, roundTripped);
        check(read.ok, "readShapes succeeds reading it back");
        check(roundTripped.bodies.size() == 2, "both bodies came back");
        check(roundTripped.outlineFaces.size() == 1, "the one outline face came back");
        check(roundTripped.outlinePlanes.size() == 1, "with its plane");

        check(std::fabs(ModelingOps::volume(roundTripped.bodies[0]) - 1000.0) < 1.0e-6,
              "the first body's volume survives exactly (10*20*5)");
        check(std::fabs(ModelingOps::volume(roundTripped.bodies[1]) - 27.0) < 1.0e-6,
              "the second body's volume survives exactly (3*3*3)");
        check(roundTripped.outlinePlanes[0].Axis().Direction().IsEqual(
                  ground.Axis().Direction(), 1.0e-9),
              "the outline's plane normal survives");
        check(roundTripped.outlinePlanes[0].Location().Distance(ground.Location()) < 1.0e-9,
              "and its origin survives");

        // Re-serializing the round-tripped document must produce byte-identical
        // output - the contract's own words: "compare re-serialized bytes equal".
        std::ostringstream out2(std::ios::binary);
        const FurnifySerial::SerialResult written2 = FurnifySerial::writeShapes(roundTripped, out2);
        check(written2.ok, "re-serializing the round-tripped document succeeds");
        check(out.str() == out2.str(), "and produces byte-identical output to the original write");
    }

    // --- an empty document round-trips too ----------------------------------
    {
        FurnifySerial::SerializedDocument empty;
        std::ostringstream out(std::ios::binary);
        check(FurnifySerial::writeShapes(empty, out).ok, "an empty document writes fine");

        FurnifySerial::SerializedDocument back;
        std::istringstream in(out.str(), std::ios::binary);
        check(FurnifySerial::readShapes(in, back).ok, "and reads back fine");
        check(back.bodies.empty() && back.outlineFaces.empty() && back.outlinePlanes.empty(),
              "with nothing in it");
    }

    // --- refusals: mismatched face/plane counts -----------------------------
    {
        FurnifySerial::SerializedDocument bad;
        bad.outlineFaces.push_back(makeSquareFace(0.0, 0.0, 10.0, ground));
        // outlinePlanes left empty - a mismatch.
        std::ostringstream out(std::ios::binary);
        const FurnifySerial::SerialResult result = FurnifySerial::writeShapes(bad, out);
        check(!result.ok, "writeShapes refuses when outline face/plane counts differ");
        check(!result.error.empty(), "and explains why");
    }

    // --- refusals: a null body ------------------------------------------------
    {
        FurnifySerial::SerializedDocument bad;
        bad.bodies.push_back(TopoDS_Shape());
        std::ostringstream out(std::ios::binary);
        check(!FurnifySerial::writeShapes(bad, out).ok, "writeShapes refuses a null body");
    }

    // --- refusal: not a FurnifyMe blob at all --------------------------------
    {
        std::istringstream garbage(std::string("not a shape blob, just text padding"),
                                   std::ios::binary);
        FurnifySerial::SerializedDocument doc;
        const FurnifySerial::SerialResult result = FurnifySerial::readShapes(garbage, doc);
        check(!result.ok, "readShapes refuses a stream with the wrong magic");
        check(!result.error.empty(), "and explains why");
        check(doc.bodies.empty() && doc.outlineFaces.empty() && doc.outlinePlanes.empty(),
              "and leaves `doc` empty, not partially populated");
    }

    // --- refusal: corrupt / truncated shapes.bin -----------------------------
    {
        FurnifySerial::SerializedDocument doc;
        doc.bodies.push_back(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0));
        doc.bodies.push_back(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0));
        std::ostringstream out(std::ios::binary);
        check(FurnifySerial::writeShapes(doc, out).ok, "fixture document writes fine");

        const std::string whole = out.str();
        check(whole.size() > 20, "the fixture blob is long enough to truncate meaningfully");
        const std::string truncated = whole.substr(0, whole.size() / 2);

        FurnifySerial::SerializedDocument recovered;
        std::istringstream in(truncated, std::ios::binary);
        const FurnifySerial::SerialResult result = FurnifySerial::readShapes(in, recovered);
        check(!result.ok, "readShapes refuses a truncated shapes.bin");
        check(!result.error.empty(), "and explains why");
        check(recovered.bodies.empty() && recovered.outlineFaces.empty(),
              "and leaves `doc` empty rather than half-populated");
    }

    // --- refusal: a future format version ------------------------------------
    {
        // Hand-build a header claiming a format version this build does not
        // understand, using the same primitives writeShapes itself uses -
        // the RED case this task's brief specifically asks to pin.
        std::ostringstream out(std::ios::binary);
        out.write(FurnifySerial::kMagic, 8);
        BinTools::PutInteger(out, FurnifySerial::kFormatVersion + 1);
        BinTools::PutInteger(out, 0);
        BinTools::PutInteger(out, 0);

        FurnifySerial::SerializedDocument doc;
        std::istringstream in(out.str(), std::ios::binary);
        const FurnifySerial::SerialResult result = FurnifySerial::readShapes(in, doc);
        check(!result.ok, "readShapes refuses a future format version");
        check(!result.error.empty(), "and explains why");
        check(doc.bodies.empty(), "and leaves `doc` empty");
    }

    // --- refusal: a body index that lands on the wrong sub-shape ------------
    // Review finding (Task 1 fix round 1): the body loop only checked
    // IsNull(), while the outline loop already checked ShapeType() == FACE.
    // An IN-RANGE index that happens to point at a face/edge/vertex from
    // BinTools_ShapeSet's shared sub-shape pool - rather than at a top-level
    // body - passed straight through as a "body" and would have reached
    // DocumentModel::addSolid unchecked: a corrupt file surfacing as a
    // successful load. Built by hand-assembling a stream with the SAME
    // primitives writeShapes uses (exactly like the future-format case
    // above), but pointing the one recorded body index at a FACE that was
    // added to the same shape set, never at the SOLID that was also added.
    {
        BinTools_ShapeSet shapeSet;
        const TopoDS_Shape solid = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0);
        const TopoDS_Shape face = makeSquareFace(0.0, 0.0, 10.0, ground);
        shapeSet.Add(solid);
        const int faceIndex = shapeSet.Add(face);

        std::ostringstream out(std::ios::binary);
        out.write(FurnifySerial::kMagic, 8);
        BinTools::PutInteger(out, FurnifySerial::kFormatVersion);
        BinTools::PutInteger(out, 1);  // bodyCount = 1
        BinTools::PutInteger(out, 0);  // outlineCount = 0
        BinTools::PutInteger(out, faceIndex);  // the corruption: a FACE claimed as the body
        shapeSet.Write(out);

        FurnifySerial::SerializedDocument doc;
        std::istringstream in(out.str(), std::ios::binary);
        const FurnifySerial::SerialResult result = FurnifySerial::readShapes(in, doc);
        check(!result.ok,
              "readShapes refuses a body index that resolves to a face, not a solid");
        check(!result.error.empty(), "and explains why");
        check(doc.bodies.empty(), "and leaves `doc` empty rather than handing back a fake body");
    }

    // --- DocumentModel: names are stored per item, not just generated -------
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0);
        const int bodyId = doc.addSolid(box);
        const int outlineId = doc.addOutline(makeSquareFace(0.0, 0.0, 10.0, ground), ground);

        check(doc.setItemName(bodyId, "Table Top"), "setItemName renames a body");
        check(doc.nameOf(bodyId) == "Table Top", "and it sticks");
        check(doc.setItemName(outlineId, "Base Outline"), "setItemName renames an outline too");
        check(doc.outlineNameOf(outlineId) == "Base Outline", "and it sticks");
        check(!doc.setItemName(99999, "Nope"), "setItemName refuses an unknown id");
    }

    // --- DocumentModel: names ARE snapshotted by the existing undo State ----
    // The brief's own claim ("snapshotted by the existing undo State") had
    // no assertion pinning it - review finding, Task 1 fix round 1. Unlike
    // visibility (deliberately EXCLUDED from State, checked just below),
    // a name is document content: setItemName -> checkpoint -> rename again
    // -> undo must restore the FIRST name, not merely undo the structural
    // change around it.
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 3.0, 3.0, 3.0);
        const int id = doc.addSolid(box);
        doc.setItemName(id, "First Name");

        doc.checkpoint();
        doc.setItemName(id, "Second Name");
        check(doc.nameOf(id) == "Second Name", "the rename after the checkpoint sticks");

        check(doc.undo(), "undo succeeds");
        check(doc.nameOf(id) == "First Name",
              "and restores the name as it stood AT the checkpoint - names are "
              "document state, not presentation state");
    }

    // --- DocumentModel: visibility is presentation state, not undo-tracked --
    {
        DocumentModel doc;
        const TopoDS_Shape box = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0);
        const int id = doc.addSolid(box);
        check(doc.isVisible(id), "a fresh item is visible by default");
        check(doc.isVisible(999999), "an unknown id also reads as visible");

        doc.checkpoint();
        doc.setVisible(id, false);
        check(!doc.isVisible(id), "setVisible hides it");
        doc.undo();
        check(!doc.isVisible(id),
              "undo does NOT restore visibility - it is presentation state, not a document edit");
    }

    // --- DocumentModel: toSerialized()/fromSerialized() round-trip ----------
    {
        DocumentModel doc;
        const TopoDS_Shape boxA = ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 10.0, 10.0, 10.0);
        const TopoDS_Shape boxB = ModelingOps::makeBox(gp_Pnt(20.0, 0.0, 0.0), 4.0, 4.0, 4.0);
        const int idA = doc.addSolid(boxA);
        const int idB = doc.addSolid(boxB);
        const int outlineId = doc.addOutline(makeSquareFace(0.0, 0.0, 30.0, ground), ground);

        doc.setItemName(idA, "Left Leg");
        doc.setItemName(idB, "Right Leg");
        doc.setItemName(outlineId, "Top Outline");
        doc.setVisible(idB, false);

        DocumentModel::DocumentMeta meta;
        const FurnifySerial::SerializedDocument serial = doc.toSerialized(meta);
        check(serial.bodies.size() == 2, "toSerialized carries both bodies");
        check(serial.outlineFaces.size() == 1, "and the one outline");
        check(meta.bodyNames.size() == 2 && meta.bodyNames[0] == "Left Leg" &&
                  meta.bodyNames[1] == "Right Leg",
              "body names come out in order");
        check(meta.bodyVisible.size() == 2 && meta.bodyVisible[0] == true &&
                  meta.bodyVisible[1] == false,
              "body visibility comes out in order");
        check(meta.outlineNames.size() == 1 && meta.outlineNames[0] == "Top Outline",
              "outline name comes out too");

        DocumentModel loaded;
        // Give it some prior state, to prove fromSerialized REPLACES rather
        // than merges.
        loaded.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0));
        check(loaded.fromSerialized(serial, meta), "fromSerialized succeeds on well-formed input");
        check(loaded.count() == 2, "the loaded document has exactly the saved bodies");
        check(loaded.outlineCount() == 1, "and the saved outline");
        check(!loaded.canUndo(), "a freshly loaded document has no undo history");

        // Names and visibility survive, matched by POSITION (fresh ids), not
        // by the original ids, since ids are never persisted.
        bool foundLeft = false, foundRight = false;
        bool rightIsHidden = false;
        for (const DocumentModel::Solid& s : loaded.solids()) {
            if (s.name == "Left Leg") { foundLeft = true; check(loaded.isVisible(s.id), "Left Leg stayed visible"); }
            if (s.name == "Right Leg") { foundRight = true; rightIsHidden = !loaded.isVisible(s.id); }
        }
        check(foundLeft && foundRight, "both names survive the round trip");
        check(rightIsHidden, "Right Leg's hidden visibility survives the round trip");
        check(loaded.outlines().size() == 1 && loaded.outlines().front().name == "Top Outline",
              "the outline's name survives");
        check(std::fabs(ModelingOps::volume(loaded.shapeOf(loaded.solids()[0].id)) - 1000.0) < 1.0e-6 ||
                  std::fabs(ModelingOps::volume(loaded.shapeOf(loaded.solids()[1].id)) - 1000.0) < 1.0e-6,
              "one loaded body has boxA's exact volume");
    }

    // --- fromSerialized refuses and leaves the document untouched -----------
    {
        DocumentModel doc;
        const int id = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 2.0, 2.0, 2.0));
        doc.setItemName(id, "Keep Me");

        FurnifySerial::SerializedDocument bad;
        bad.bodies.push_back(TopoDS_Shape());  // null - invalid
        DocumentModel::DocumentMeta meta;
        meta.bodyNames.push_back("X");
        meta.bodyVisible.push_back(true);

        check(!doc.fromSerialized(bad, meta), "fromSerialized refuses a null body");
        check(doc.count() == 1 && doc.nameOf(id) == "Keep Me",
              "and leaves the existing document completely untouched");

        // Mismatched name/visibility vector lengths must refuse too.
        FurnifySerial::SerializedDocument ok;
        ok.bodies.push_back(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0));
        DocumentModel::DocumentMeta mismatched;
        // bodyNames left empty while bodies has one entry.
        mismatched.bodyVisible.push_back(true);
        check(!doc.fromSerialized(ok, mismatched),
              "fromSerialized refuses when names/visible vectors don't match the shape count");
        check(doc.count() == 1, "still untouched");
    }

    // --- DocumentModel::restoreFrom - the "restore a version in place" path -
    // Milestone 3's Restore: one checkpoint, one undo brings back the WHOLE
    // pre-restore document, unlike fromSerialized() which clears history
    // outright (that is the "open a different furniture" path).
    {
        DocumentModel doc;
        const int before1 = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0));
        const int before2 = doc.addSolid(ModelingOps::makeBox(gp_Pnt(10.0, 0.0, 0.0), 6.0, 6.0, 6.0));
        doc.setItemName(before1, "Pre-restore A");
        doc.setItemName(before2, "Pre-restore B");
        check(doc.count() == 2, "the document starts with two bodies");

        // A "version" - a separate DocumentModel, exactly the shape
        // FurnitureStore::loadVersion hands back (its own ids counting again
        // from 1).
        DocumentModel version;
        const int vId = version.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0));
        version.setItemName(vId, "Version Body");
        check(!doc.canUndo(), "nothing checkpointed yet");

        doc.checkpoint();   // the caller's own checkpoint - restoreFrom() takes none of its own
        doc.restoreFrom(version);
        check(doc.count() == 1 && doc.solids().front().name == "Version Body",
              "restoreFrom replaces the whole document with the snapshot's content");
        check(doc.canUndo(), "the checkpoint taken before restoreFrom is still on the stack - "
                              "restoreFrom must NOT clear undo history the way fromSerialized does");

        check(doc.undo(), "one undo...");
        check(doc.count() == 2, "...brings back BOTH pre-restore bodies");
        bool foundA = false, foundB = false;
        for (const DocumentModel::Solid& s : doc.solids()) {
            if (s.name == "Pre-restore A") foundA = true;
            if (s.name == "Pre-restore B") foundB = true;
        }
        check(foundA && foundB, "...by name, not just by count");

        // Never-shrink id counters: create a NEW body after landing back on
        // the pre-restore state and confirm its id collides with nothing
        // already on the undo/redo stack (the pre-restore ids, and the
        // version's own low ids that briefly lived in `doc`).
        std::vector<int> seenIds;
        for (const DocumentModel::Solid& s : doc.solids()) seenIds.push_back(s.id);
        doc.checkpoint();
        const int freshId = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 2.0, 2.0, 2.0));
        check(std::find(seenIds.begin(), seenIds.end(), freshId) == seenIds.end(),
              "a body created after undoing a restore gets an id that never collides "
              "with an id already on the undo/redo stack");
    }
    {
        // Never-shrink the OTHER direction: a document with very little prior
        // history restoring from a version that has MORE items than this
        // document ever held must not let a later addSolid() collide with an
        // id the restore just introduced.
        DocumentModel doc;   // myNextId == 1, nothing created yet

        DocumentModel version;
        for (int i = 0; i < 5; ++i) {
            const int id = version.addSolid(ModelingOps::makeBox(
                gp_Pnt(double(i) * 20.0, 0.0, 0.0), 3.0, 3.0, 3.0));
            version.setItemName(id, "V" + std::to_string(i));
        }
        check(version.count() == 5, "the version holds five bodies, ids 1..5");

        doc.checkpoint();
        doc.restoreFrom(version);
        check(doc.count() == 5, "all five arrived");

        const int freshId = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0));
        check(doc.contains(freshId) && doc.count() == 6,
              "a body added after restoring from a larger version gets a genuinely fresh id "
              "(the document now holds six distinct bodies, not a collision)");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
