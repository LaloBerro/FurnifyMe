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
#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "FurnifySerial.h"
#include "Joinery.h"

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

    // Retroactive pairing (Milestone 4): builds a mirror twin for each of
    // `ids` (ModelingOps::mirrorShape) and pairs it with its source, all
    // inside ONE checkpoint that also sets the symmetry plane and turns
    // symmetryOn() on - built ON the Milestone 3 twin engine
    // (pairBodies()/twinOf()/setSymmetry()) rather than changing any of its
    // rules; see CLAUDE.md's "Live symmetry is twins, not replay".
    struct PairResult {
        // How many of `ids` actually got a fresh twin and pairing.
        int paired = 0;
        // ids skipped because ModelingOps::boundingBoxStraddlesPlane() found
        // them straddling `plane` - mirroring one would build a twin
        // overlapping the body itself, not a second piece of furniture (the
        // same rule creation-time pairing already follows in MainWindow).
        std::vector<int> skippedStraddling;
        // ids skipped because they already have a live twin
        // (symmetryOn() && twinOf(id) != -1). pairBodies() would silently
        // drop an existing pairing and re-point it; a retroactive-pairing
        // gesture must never do that without saying so, so these are
        // reported rather than acted on.
        std::vector<int> skippedAlreadyPaired;
        // ids that passed every check above but whose OWN
        // ModelingOps::mirrorShape() call refused - a kernel-level failure,
        // not a validation failure (fix round 1). Never-silent-failure
        // applies inside this engine, not only at its UI-facing edges: an id
        // that reaches the kernel and still gets no twin must say why, not
        // just show up as a lower-than-expected `paired` count.
        std::vector<int> skippedFailed;
        // ids skipped because they belong to a link group (Milestone 4:
        // isLinked(id)) - the spec's v1 mirror-and-link exclusion, enforced
        // from this side too (linkExisting()/createLinkedCopy() refuse a
        // mirror-paired id the other way round). A body cannot be asked to
        // carry both propagation rules at once, so it is reported here
        // exactly like skippedAlreadyPaired rather than silently mirrored.
        std::vector<int> skippedLinked;
    };

    // An id outside the document (unknown, <= 0, or repeated within `ids`)
    // is silently ignored - the three skip lists above are for ids that ARE
    // real bodies but cannot be paired as asked. Every candidate that passes
    // validation is MIRRORED before anything is mutated (fix round 1: this
    // used to gate the checkpoint on validated candidates rather than actual
    // outcomes), and the checkpoint/setSymmetry(true, ...) is gated on
    // ACTUAL pairing outcomes: if not one of `ids` ends up with a twin - be
    // it because every id was invalid, every id failed validation, or every
    // validated id's own mirror call refused - no checkpoint is taken and
    // symmetryOn()/symmetryPlane() are left exactly as they were, so a
    // genuine no-op can never dirty the document.
    //
    // This call checkpoints ITSELF - the one place in this file that does,
    // because only it knows in advance whether the call nets a genuine
    // no-op. A caller wiring this into the UI must run any pre-commit hooks
    // of its own (render mode's exit, in particular) BEFORE calling this,
    // not after: MainWindow's checkpointDocument() choke point cannot be
    // layered on top of an already-self-checkpointing call without
    // double-checkpointing one gesture.
    PairResult pairWithMirror(const std::vector<int>& ids, const gp_Pln& plane);

    // Drops EVERY pairing, leaving symmetryOn()/symmetryPlane() untouched -
    // the plane-change rule (fix round 1): a new plane invalidates every
    // existing pairing's MEANING (each one was computed against the OLD
    // plane), so changing the plane unpairs everything, the same rule
    // setSymmetry(false, ...) already follows for turning the mode off. No
    // checkpoint, for the same reason - a mode/plane change is not an edit -
    // but it does bump revision() when it actually drops anything, so a
    // dirty furniture is written back.
    //
    // Returns whether anything was actually unpaired, so a caller (only
    // MainWindow's plane-pick route needs this) can announce it - or stay
    // silent - rather than reporting an unpairing that changed nothing.
    bool unpairAll();

    // --- link groups (Milestone 4: linked copies) --------------------------
    //
    // Where mirror symmetry (above) keeps exactly two bodies in step across
    // one plane, a link group keeps N bodies in step across an arbitrary
    // placement each - "the same shape placed differently", per the spec.
    // The mechanism is the same philosophy CLAUDE.md states for symmetry
    // ("twins, not replay"): nothing records the EDIT that changed a member,
    // only the placements between members, and propagateLinkedEdit()
    // re-derives every shape from whichever member's edit just landed. A
    // link group never stores a shape of its own - the anchor's shape is a
    // real body like any other, and `placement[anchorId]` is always
    // identity, kept as an explicit entry rather than an implicit special
    // case so every member (anchor included) is one lookup away.
    //
    // `placement[m]` maps the ANCHOR's frame to member `m`'s frame: member
    // m's shape is always the anchor's shape transformed by `placement[m]`.
    // v1 (this task) only ever builds translation-only placements
    // (createLinkedCopy takes a caller-supplied gp_Trsf offset, which need
    // not be translation-only, but linkExisting's own placements always
    // are, per the spec) - general rotation/scale placements are accepted
    // by propagateLinkedEdit() and the query API without restriction, since
    // gp_Trsf itself does not know which flavour it is.
    struct LinkGroup {
        int anchorId = 0;
        std::map<int, gp_Trsf> placement;   // keyed by member id, anchor included
    };

    // Outcome of createLinkedCopy()/linkExisting(): `ok` false always
    // carries an empty `id`/non-empty `error`, the same refusal contract
    // BooleanResult uses. `id` is the new copy's id for createLinkedCopy(),
    // and the anchor's id (informational only) for linkExisting().
    struct LinkResult {
        bool ok = false;
        int id = 0;
        std::string error;
    };

    // Adds a new body that is `sourceId`'s own shape transformed by
    // `offset`, and records it as a member of `sourceId`'s link group -
    // founding a fresh one (with `sourceId` as anchor) if `sourceId` was not
    // already in one. One checkpoint. Refuses (no checkpoint, no mutation,
    // `id == 0`): an unknown/invalid `sourceId`, a `sourceId` that already
    // has a live mirror twin (symmetryOn() && twinOf() != -1 - the v1
    // mirror/link exclusion, enforced from this side), or a kernel-level
    // transform failure (ModelingOps::transformShape, e.g. `offset`'s scale
    // factor <= 0).
    LinkResult createLinkedCopy(int sourceId, const gp_Trsf& offset);

    // Links `ids` into one group: `ids.front()` becomes the anchor, and
    // every other id's shape is REPLACED by the anchor's shape translated
    // centre-to-centre (ModelingOps::centreOfMass of each original shape) -
    // a translation-only placement, v1's whole scope per the spec. One
    // checkpoint. Resolve-before-mutate and ALL-OR-NOTHING, the bevels'
    // own discipline: every id is validated and every replacement shape is
    // built BEFORE anything is written, so one bad id refuses the whole
    // call with nothing changed. Refuses (no checkpoint, no mutation):
    // fewer than two ids, an unknown/invalid or duplicated id, an id
    // already in a link group, an id with a live mirror twin (the same v1
    // exclusion createLinkedCopy() enforces), or a kernel-level transform
    // failure.
    LinkResult linkExisting(const std::vector<int>& ids);

    // Removes `memberId` from its link group, if it has one - false, no
    // checkpoint, for an id that is not linked. One checkpoint otherwise.
    // Neither `memberId`'s own shape nor any other member's shape changes;
    // only the bookkeeping is undone. A group left with fewer than two
    // members dissolves outright (no group is a group of one).
    //
    // Unlinking the ANCHOR needs a rule, since every other member's
    // placement is expressed relative to it: the DETERMINISTIC choice made
    // here is to promote the lowest surviving member id to anchor (ids are
    // assigned in creation order and never reused - see the header note at
    // the top of this file - so "lowest id" is "oldest surviving member",
    // not an arbitrary tiebreak) and re-express every remaining placement
    // relative to the new anchor's own frame. The promoted member's own
    // placement becomes an exact identity rather than a numerically-close
    // one, so re-promoting it again later never drifts.
    bool unlink(int memberId);

    // The propagation engine: `editedMemberId` just became `newShape`
    // (typically the result of a pull/bevel/transform run on that member's
    // OWN current shape) - this re-derives the anchor's shape as
    // `placement(editedMemberId)^-1 . newShape`, then every OTHER member as
    // `placement(member) . anchor`, through ModelingOps::transformShape.
    // `editedMemberId`'s OWN entry is `newShape` itself, written back
    // exactly as given rather than round-tripped through the anchor and
    // forward again (fix round 1) - avoids an avoidable kernel transform and
    // the floating-point drift it would add across repeated edits to the
    // same member. False, nothing changed, for an id with no link group, a
    // null `newShape`, or a kernel-level transform failure on any member
    // (resolve-before-mutate: every member's new shape is built before any
    // is written, so a mid-list failure leaves the document untouched).
    //
    // Unlike every other checkpointed API on this page, this call takes NO
    // checkpoint of its own - the header's own general rule
    // ("call checkpoint() before mutating") does not apply here because
    // this is always run from INSIDE an edit that already checkpointed
    // itself (a pull, a bevel, a transform) before producing `newShape` in
    // the first place; a second checkpoint here would split one gesture
    // into two undo steps. This is the same asymmetry pairWithMirror's own
    // header calls out in the opposite direction - THAT call checkpoints
    // itself because it is its own gesture with no earlier checkpoint to
    // sit behind. All of the shape writes below happen in ONE pass, so the
    // whole propagated edit is one State mutation for the checkpoint the
    // caller already took to restore.
    bool propagateLinkedEdit(int editedMemberId, const TopoDS_Shape& newShape);

    // True when `id` belongs to a link group (a body with no group answers
    // false, including an unknown id).
    bool isLinked(int id) const;
    // `id`'s group's anchor id, or -1 when `id` is not linked (including an
    // unknown id). Answers `id` itself when `id` IS the anchor.
    int linkAnchorOf(int id) const;
    // Copies `id`'s whole LinkGroup into `out` and returns true; false (`out`
    // untouched) when `id` is not linked. Every member id in the same group
    // answers with an equal LinkGroup - see the struct's own comment on why
    // it is duplicated per member rather than looked up through the anchor.
    bool linkGroupOf(int id, LinkGroup& out) const;

    // --- joinery (Task 7: the Joint record) ---------------------------------
    //
    // A planned wood joint between two pieces (spec:
    // docs/superpowers/specs/2026-09-10-joinery-design.md). It records the
    // RELATIONSHIP and never a world position - where its dowels fall is
    // re-derived from the live bodies every time it is read, which is what
    // makes a joint follow its pieces and break loudly when they part.
    struct Joint {
        int id = 0;
        Joinery::Kind kind = Joinery::Kind::Dowel;
        int bodyA = 0;
        int bodyB = 0;
        Joinery::Parameters params;
        std::vector<Joinery::Adjustment> adjustments;
    };

    // Returns the new joint's id, or 0 when refused: either body unknown,
    // or the two the same piece. Takes NO checkpoint - the caller owns the
    // gesture's one checkpoint, the same contract every other mutator here
    // keeps.
    int addJoint(Joinery::Kind kind, int bodyA, int bodyB,
                 const Joinery::Parameters& params);
    bool removeJoint(int jointId);
    bool updateJointParameters(int jointId, const Joinery::Parameters& params);
    bool setJointAdjustments(int jointId, const std::vector<Joinery::Adjustment>& adj);
    const std::vector<Joint>& joints() const { return myJoints; }
    // Every joint touching `bodyId`, from either side.
    std::vector<Joint> jointsOn(int bodyId) const;

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

        // Link groups (Milestone 4), position-based for exactly the reason
        // symmetryPairs is - ids are session-only handles that cannot be
        // persisted directly. One record per DISTINCT group (never one per
        // member - see LinkGroup's own comment on the in-memory map's
        // per-member duplication, which this does NOT mirror): every
        // position in `memberPositions` indexes into `serial.bodies`,
        // `placements` is its own gp_Trsf stored as the 12 values
        // `Value(row, col)`/`SetValues()` read and write (row-major, 3 rows
        // x 4 columns), and the two arrays are parallel - placements[i] is
        // memberPositions[i]'s own placement, identity for whichever
        // position equals `anchorPosition`. Always empty when nothing is
        // linked, the same "absent means off" rule symmetry's own fields
        // follow, so an unlinked document persists nothing new here and an
        // OLDER file with no key at all round-trips as "no groups" for
        // free.
        struct LinkGroupRecord {
            int anchorPosition = -1;
            std::vector<int> memberPositions;
            std::vector<std::array<double, 12>> placements;
        };
        std::vector<LinkGroupRecord> linkGroups;
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
    // shape type is not actually TopAbs_FACE, a names/visible vector whose
    // length does not match its shapes vector, or (Milestone 4, fix round 1)
    // a body position named in BOTH `symmetryPairs` and a `linkGroups`
    // record - the v1 mirror/link exclusion, enforced here too: a body
    // cannot load simultaneously mirror-paired and linked, and guessing
    // which membership to keep would be the exact silent data loss the
    // rest of this function's validate-before-mutate discipline exists to
    // prevent.
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
        // The PAIRING MAP rides along in State: pairing changes happen
        // exclusively inside checkpointed commits (extrude, an edit that
        // follows a twin, a boolean, a delete), so undoing one of those must
        // restore the pairing exactly as it stood, the same way it restores
        // names.
        //
        // symmetryOn/symmetryPlane deliberately do NOT - fix round 1. The
        // mode is a session setting, not document content, the same rule
        // visibility already follows (see setVisible()'s own comment): it
        // is set outside any checkpoint (setSymmetry() never takes one), so
        // treating it as undoable content let an undo landing after "turn
        // symmetry off" silently turn it back ON and resurrect whatever
        // pairing that checkpoint had captured - a mode switch resurrected
        // by a Ctrl+Z aimed at something else entirely. Every reader of a
        // pairing (MainWindow's twin-follow hook, the delete and boolean
        // special cases) is gated on symmetryOn() as well as twinOf() for
        // exactly this reason: a pairing entry surviving in State is inert
        // the moment the live mode is off, whatever undo does to it.
        std::unordered_map<int, int> twin;
        // Link groups (Milestone 4) ride along in State for the same reason
        // the pairing map does: linking/unlinking/propagating all happen
        // inside checkpointed commits, so undoing one must restore group
        // membership and every placement exactly as they stood.
        std::map<int, LinkGroup> linkGroups;
        // Joints ride in State for the reason the pairing map and the link
        // groups do: they are created, edited and destroyed exclusively
        // inside checkpointed commits, so an undo must restore them exactly
        // as they stood - including the joints a deleted body took with it.
        std::vector<Joint> joints;
    };

    // Drops `id`'s existing pairing, both directions, if it has one. The one
    // implementation pairBodies() and removeSolid() both call, so a removed
    // or re-paired body can never leave a stale half-entry pointing at it.
    void unpairInternal(int id);

    // Removes `memberId` from its link group (promoting a new anchor, or
    // dissolving the group outright, exactly as unlink()'s own header
    // describes) with NO checkpoint and no revision bump - the one
    // implementation unlink() and removeSolid() both call, matching
    // unpairInternal()'s own split between "what changes the bookkeeping"
    // and "who checkpoints for it". A no-op for an id with no group.
    void unlinkGroupInternal(int memberId);

    std::vector<Solid> mySolids;
    std::vector<Outline> myOutlines;
    bool mySymmetryOn = false;
    gp_Pln mySymmetryPlane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)};
    // Both directions, so twinOf() is a single lookup either way round.
    std::unordered_map<int, int> myTwin;
    // Keyed by MEMBER id, anchor included - the same "duplicate for O(1)
    // lookup from any member" idiom myTwin uses, one level richer: every id
    // in a group maps to an equal copy of the whole LinkGroup (not just the
    // other end of a pair), so "which group is X in, and where does every
    // member sit" is one lookup regardless of which member id is in hand.
    std::map<int, LinkGroup> myLinkGroups;
    // Live joints, mirroring the myTwin/myLinkGroups idiom: this is the
    // working set every accessor and mutator reads and writes, and State's
    // own `joints` field is only ever a snapshot taken of it (checkpoint())
    // or written back into it (undo()/redo()).
    std::vector<Joint> myJoints;
    int myNextId = 1;
    // Its own counter, never rolled back by undo - the same rule myNextId
    // itself follows (see the header note at the top of this file): a
    // stale joint id must never resolve to a different joint. Kept
    // separate from myNextId (bodies/outlines) because a joint is not an
    // item in that id space - it never appears in the Items drawer and
    // never collides with a body or outline id, but nothing requires the
    // two counters to share a sequence either, and keeping them apart means
    // a change to one can never silently perturb the other.
    int myNextJointId = 1;
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
