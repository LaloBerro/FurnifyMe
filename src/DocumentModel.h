#pragma once
//
// Owns the solids that make up the document. Deliberately free of both Qt and
// the OCCT visualization toolkits: it stores topology and nothing else, so it
// stays unit-testable without a window. The view keeps its own id -> AIS_Shape
// map and syncs from here.
//
// Ids are handles for the *current session only*. Milestone 1 has no history
// tree, so nothing persists them - see the topological naming note in CLAUDE.md
// before giving them any longer life.
//
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include "FurnifySerial.h"

class DocumentModel {
public:
    struct Solid {
        int id = 0;
        std::string name;
        TopoDS_Shape shape;
    };

    // A closed outline that is not a body yet - Phase 7, item 4. It is a
    // DOCUMENT ITEM, not a preview: it is listed in the Items drawer, it is
    // covered by checkpoint/undo/redo, and it survives starting or cancelling
    // another sketch. Extrude is what consumes it.
    //
    // The plane is stored BY VALUE alongside the face, and that is the whole
    // reason this struct exists rather than a bare face. CLAUDE.md's rule -
    // "a closed outline pins the plane it was drawn on" - used to be honoured
    // by refusing to move the sketch plane while an outline waited; storing
    // the plane makes the sweep direction a property of the outline itself,
    // so an extrude can never be re-aimed by anything that happens between
    // the close and the commit.
    struct Outline {
        int id = 0;
        std::string name;
        TopoDS_Face face;
        gp_Pln plane;
    };

    // Returns the id assigned to the new solid, or 0 if the shape was null.
    int addSolid(const TopoDS_Shape& shape);

    bool replaceSolid(int id, const TopoDS_Shape& shape);
    bool removeSolid(int id);
    void clear();

    // Null shape if the id is unknown.
    TopoDS_Shape shapeOf(int id) const;
    bool contains(int id) const;

    // Empty string if the id is unknown.
    std::string nameOf(int id) const;
    bool renameSolid(int id, const std::string& name);

    // --- symmetry (Milestone 3: live mirror twins) -------------------------
    //
    // Symmetry is DOCUMENT state, not a session preference: it is snapshotted
    // by checkpoint()/undo()/redo() and persisted in the manifest (Task 1
    // reserved the "symmetry" key), so a save/load or a version restore
    // brings the pairing back exactly as it stood. `plane` is captured BY
    // VALUE - the same rule the sketch plane and every Outline follow - so a
    // rebuild elsewhere can never move it out from under a live pairing.
    //
    // Turning it OFF unpairs everything: no checkpoint (this is a mode
    // switch, not an edit a Ctrl+Z should have its own entry for), but it
    // DOES bump revision() - the manifest's own "symmetry" block has
    // genuinely changed, so a furniture with autosave on has to notice.
    // Turning it back on does NOT re-pair what was unpaired; there is no
    // record of what used to go with what once the map is cleared.
    void setSymmetry(bool on, const gp_Pln& plane);
    bool symmetryOn() const { return mySymmetryOn; }
    gp_Pln symmetryPlane() const { return mySymmetryPlane; }

    // Pairs `idA` and `idB` as mirror twins, both directions. Either id's
    // PREVIOUS pairing (if any) is dropped first, so a body can never belong
    // to two pairs at once. A no-op for an unknown id, an id paired with
    // itself, or either id <= 0.
    void pairBodies(int idA, int idB);
    // `id`'s twin body id, or -1 when unpaired (including for an unknown id).
    int twinOf(int id) const;

    // Renames whichever kind of item `id` belongs to - a body or an
    // outline, since the two share one id space. Milestone 3 introduces
    // user-editable names (Task 5 wires the drawer's rename gesture); this
    // is the one setter both a UI rename and a file load go through, so
    // there is exactly one place a name can be written. False for an
    // unknown id, same as renameSolid.
    bool setItemName(int id, const std::string& name);

    // Presentation state - whether an item currently displays in the
    // viewport - deliberately kept OUTSIDE `State` and therefore outside
    // checkpoint()/undo()/redo(), for the same reason OcctViewWidget's own
    // setSolidVisible()/setOutlineVisible() are not undo-tracked: hiding a
    // body is not a document edit any more than orbiting the camera is.
    // It lives here (rather than only in the view) purely so
    // toSerialized()/fromSerialized() can round-trip it without the
    // geometry library reaching into Qt or the OCCT visualization
    // toolkits - reconciling this with the view's own tracking into one
    // source of truth belongs to whichever task wires FurnitureStore into
    // MainWindow. False for a known id hides it; querying an unknown id
    // answers true (the safe default - "not hidden" - for an item nobody
    // has touched).
    void setVisible(int id, bool visible);
    bool isVisible(int id) const;

    const std::vector<Solid>& solids() const { return mySolids; }
    // BODIES, deliberately - every existing caller ("3 bodies in the
    // document", the export action's enabled state, the walkthrough's step)
    // means bodies, and outlines are counted by outlineCount().
    std::size_t count() const { return mySolids.size(); }

    // --- outlines ----------------------------------------------------------
    // Returns the id assigned to the new outline, or 0 if the face was null.
    // Ids come from the SAME counter bodies use, so an outline id can never
    // collide with a body id - the Items drawer, the viewport's own maps and
    // every accessor here are handed bare ints, and two id spaces would make
    // "which kind is 3" a question with two answers.
    int addOutline(const TopoDS_Face& face, const gp_Pln& plane);
    bool removeOutline(int id);
    bool containsOutline(int id) const;
    const std::vector<Outline>& outlines() const { return myOutlines; }
    std::size_t outlineCount() const { return myOutlines.size(); }
    // Null face / empty name for an unknown id.
    TopoDS_Face outlineFace(int id) const;
    std::string outlineNameOf(int id) const;
    // False, and `out` untouched, for an unknown id.
    bool outlinePlane(int id, gp_Pln& out) const;

    // Removes outline `outlineId` and adds `solid` as a body, in ONE
    // mutation, and returns the new body's id (0 if either half would fail,
    // in which case nothing changed at all). Deliberately takes no checkpoint
    // of its own: the caller checkpoints before mutating, exactly as every
    // other commit path in this app does, and one checkpoint around this one
    // call is what makes a single undo restore the outline AND remove the
    // body rather than leaving a document with both or neither.
    int convertOutlineToBody(int outlineId, const TopoDS_Shape& solid);

    // Bumped by every call that actually changes what this document holds -
    // add, replace, remove, clear, undo, redo, and every outline operation
    // (an outline is a document item, so a toast naming one has to be
    // dismissed by the next change exactly as a body's is). Not a version number
    // anybody persists: it exists so a surface that named one change (a
    // toast saying "Deleted Body 02" and offering Undo) can tell that the
    // document has moved on since, and stop describing one operation while
    // its control would perform another. Monotonic and never rolled back -
    // an undo is itself a move, not a return to a previous revision.
    int revision() const { return myRevision; }

    // --- undo / redo -------------------------------------------------------
    // A "simple shape stack", which is what the brief leaves in scope. Call
    // checkpoint() *before* mutating; it records the current solids AND
    // outlines and discards the redo branch. Snapshots are cheap: TopoDS_Shape
    // is a refcounted handle, so copying the vector shares the geometry rather
    // than duplicating it.
    //
    // Both lists move together, which is what makes the outline lifecycle
    // undoable end to end: one checkpoint around convertOutlineToBody() and a
    // single undo puts the outline back and takes the body away.
    static constexpr std::size_t kMaxHistory = 20;

    void checkpoint();
    // How many checkpoints are on the undo stack. canUndo() answers "is there
    // one at all", which cannot tell a gesture that took a checkpoint from one
    // that did not when the stack was already non-empty - and "this drag
    // netted nothing, so it must not have taken one" is exactly that question.
    std::size_t undoDepth() const { return myUndo.size(); }
    bool canUndo() const { return !myUndo.empty(); }
    bool canRedo() const { return !myRedo.empty(); }
    bool undo();
    bool redo();

    // --- serialization -------------------------------------------------------
    // Everything a save/load round-trip needs beyond raw geometry: the
    // labels and visibility the user set, positionally matched to
    // FurnifySerial::SerializedDocument's own vectors - index i of
    // bodyNames/bodyVisible describes serial.bodies[i], and
    // outlineNames/outlineVisible describe serial.outlineFaces[i] the same
    // way. Kept separate from SerializedDocument itself because
    // FurnifySerial is pure kernel geometry and knows nothing about labels
    // or presentation - see FurnifySerial.h.
    struct DocumentMeta {
        std::vector<std::string> bodyNames;
        std::vector<bool> bodyVisible;
        std::vector<std::string> outlineNames;
        std::vector<bool> outlineVisible;

        // Symmetry (Milestone 3). `symmetryPairs` is POSITION-based - a pair
        // (i, j) means "serial.bodies[i] and serial.bodies[j] are twins" -
        // the same rule bodyNames/bodyVisible follow and for the same
        // reason: ids are session-only handles (see the header note at the
        // top of this file) and cannot be persisted directly. Always empty
        // when symmetryOn is false - see setSymmetry()'s own unpairing rule.
        bool symmetryOn = false;
        gp_Pln symmetryPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)};
        std::vector<std::pair<int, int>> symmetryPairs;
    };

    // Walks mySolids/myOutlines in order, building the kernel-side shapes
    // and the matching names/visibility. Does not touch undo history or the
    // revision counter - reading the document out is not a change to it.
    FurnifySerial::SerializedDocument toSerialized(DocumentMeta& meta) const;

    // Replaces the WHOLE document - bodies, outlines, names, visibility -
    // with what `serial`/`meta` describe. Everything is validated BEFORE
    // any mutation, so a refused load leaves `this` exactly as it was; the
    // caller (FurnitureStore::loadFurniture) additionally loads into a
    // scratch instance and only swaps on success, but that discipline is
    // not what makes this safe to call directly - the validation here does.
    // Ids are freshly assigned through the normal addSolid()/addOutline()
    // counters (ids are session-only handles, never persisted - see the
    // header note above), and the loaded names/visibility are applied
    // after. Undo history is cleared: a freshly loaded document has
    // nothing to undo back past.
    //
    // Refuses (false, `this` untouched): outlineFaces/outlinePlanes size
    // mismatch, any null body or outline face, an outline "face" whose
    // shape type is not actually TopAbs_FACE, or a names/visible vector
    // whose length does not match its shapes vector.
    bool fromSerialized(const FurnifySerial::SerializedDocument& serial, const DocumentMeta& meta);

    // Replaces the whole document's content - bodies, outlines, names,
    // visibility - with `snapshot`'s, WITHOUT touching undo/redo history.
    // This is the "restore a version IN PLACE" path (Milestone 3's Restore),
    // as opposed to fromSerialized()'s "open a different furniture" path,
    // which clears history outright because nothing before it should be
    // undoable there. The caller checkpoints FIRST, exactly as every other
    // commit path in this app does - checkpoint() then mutate - so this is
    // the mutation that sits behind that checkpoint: one undo brings back
    // everything this replaced.
    //
    // `snapshot` is typically a freshly loaded scratch document (the same
    // shape FurnitureStore::loadVersion hands back), whose own ids count
    // again from 1 - copying them in verbatim could collide with ids this
    // document's own undo stack still references (see the header note above
    // on why undo never rolls myNextId back). The id/name counters therefore
    // advance to cover whichever of the two is larger, never shrink; `this`'s
    // solids/outlines/visibility are replaced outright since `snapshot` is
    // trusted to already be internally consistent (it came from a successful
    // fromSerialized() or an equally-valid live document).
    void restoreFrom(const DocumentModel& snapshot);

private:
    // Everything a checkpoint restores. One struct rather than two parallel
    // stacks: two stacks could be pushed to in different numbers by two
    // different commit paths, and a document half-restored is worse than one
    // not restored at all.
    struct State {
        std::vector<Solid> solids;
        std::vector<Outline> outlines;
        // Symmetry rides along in the same State: pairing changes happen
        // exclusively inside checkpointed commits (extrude, an edit that
        // follows a twin, a boolean, a delete), so undoing one of those must
        // restore the pairing exactly as it stood, the same way it restores
        // names. setSymmetry() itself never checkpoints - see its header -
        // so an on/off flip is only ever captured incidentally, by whatever
        // checkpoint happens next.
        bool symmetryOn = false;
        gp_Pln symmetryPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)};
        std::unordered_map<int, int> twin;
    };

    // Drops `id`'s existing pairing, both directions, if it has one. The one
    // implementation pairBodies() and removeSolid() both call, so a removed
    // or re-paired body can never leave a stale half-entry pointing at it.
    void unpairInternal(int id);

    std::vector<Solid> mySolids;
    std::vector<Outline> myOutlines;
    bool mySymmetryOn = false;
    gp_Pln mySymmetryPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)};
    // Both directions, so twinOf() is a single lookup either way round.
    std::unordered_map<int, int> myTwin;
    int myNextId = 1;
    int myRevision = 0;   // see revision() - monotonic, never rolled back
    // Like ids, never rolled back by undo: a name reappearing on a different
    // solid would be confusing in the Items panel.
    int myNextName = 1;
    // The same rule, counted separately so bodies and outlines each number
    // from 01 - "Outline 01" beside "Body 01" is the drawer users read.
    int myNextOutlineName = 1;

    // Ids are never rolled back with the state: reusing an id would let a stale
    // reference resolve to a different solid.
    std::vector<State> myUndo;
    std::vector<State> myRedo;

    // See setVisible()/isVisible(): presentation state, deliberately not
    // part of State and therefore not undo-tracked. An id absent here reads
    // as visible (true) - see isVisible()'s doc comment.
    std::unordered_map<int, bool> myVisibility;
};
