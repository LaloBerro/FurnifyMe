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
#include <vector>

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

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

private:
    // Everything a checkpoint restores. One struct rather than two parallel
    // stacks: two stacks could be pushed to in different numbers by two
    // different commit paths, and a document half-restored is worse than one
    // not restored at all.
    struct State {
        std::vector<Solid> solids;
        std::vector<Outline> outlines;
    };

    std::vector<Solid> mySolids;
    std::vector<Outline> myOutlines;
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
};
