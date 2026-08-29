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

#include <TopoDS_Shape.hxx>

class DocumentModel {
public:
    struct Solid {
        int id = 0;
        std::string name;
        TopoDS_Shape shape;
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
    std::size_t count() const { return mySolids.size(); }

    // Bumped by every call that actually changes what solids this document
    // holds - add, replace, remove, clear, undo, redo. Not a version number
    // anybody persists: it exists so a surface that named one change (a
    // toast saying "Deleted Body 02" and offering Undo) can tell that the
    // document has moved on since, and stop describing one operation while
    // its control would perform another. Monotonic and never rolled back -
    // an undo is itself a move, not a return to a previous revision.
    int revision() const { return myRevision; }

    // --- undo / redo -------------------------------------------------------
    // A "simple shape stack", which is what the brief leaves in scope. Call
    // checkpoint() *before* mutating; it records the current solids and discards
    // the redo branch. Snapshots are cheap: TopoDS_Shape is a refcounted handle,
    // so copying the vector shares the geometry rather than duplicating it.
    static constexpr std::size_t kMaxHistory = 20;

    void checkpoint();
    bool canUndo() const { return !myUndo.empty(); }
    bool canRedo() const { return !myRedo.empty(); }
    bool undo();
    bool redo();

private:
    std::vector<Solid> mySolids;
    int myNextId = 1;
    int myRevision = 0;   // see revision() - monotonic, never rolled back
    // Like ids, never rolled back by undo: a name reappearing on a different
    // solid would be confusing in the Items panel.
    int myNextName = 1;

    // Ids are never rolled back with the state: reusing an id would let a stale
    // reference resolve to a different solid.
    std::vector<std::vector<Solid>> myUndo;
    std::vector<std::vector<Solid>> myRedo;
};
