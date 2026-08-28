#include "DocumentModel.h"

#include <algorithm>

int DocumentModel::addSolid(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0;

    const int id = myNextId++;
    mySolids.push_back(Solid{id, shape});
    return id;
}

bool DocumentModel::replaceSolid(int id, const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return false;

    for (Solid& s : mySolids) {
        if (s.id == id) {
            s.shape = shape;
            return true;
        }
    }
    return false;
}

bool DocumentModel::removeSolid(int id)
{
    const auto it = std::find_if(mySolids.begin(), mySolids.end(),
                                 [id](const Solid& s) { return s.id == id; });
    if (it == mySolids.end()) return false;

    mySolids.erase(it);
    return true;
}

void DocumentModel::clear()
{
    mySolids.clear();
    // Ids are not reused: a stale id must never silently resolve to a new solid.
}

TopoDS_Shape DocumentModel::shapeOf(int id) const
{
    for (const Solid& s : mySolids) {
        if (s.id == id) return s.shape;
    }
    return TopoDS_Shape();
}

bool DocumentModel::contains(int id) const
{
    return std::any_of(mySolids.begin(), mySolids.end(),
                       [id](const Solid& s) { return s.id == id; });
}

void DocumentModel::checkpoint()
{
    myUndo.push_back(mySolids);
    if (myUndo.size() > kMaxHistory) myUndo.erase(myUndo.begin());

    // Anything redoable described a future that no longer follows from here.
    myRedo.clear();
}

bool DocumentModel::undo()
{
    if (myUndo.empty()) return false;

    myRedo.push_back(mySolids);
    mySolids = myUndo.back();
    myUndo.pop_back();
    return true;
}

bool DocumentModel::redo()
{
    if (myRedo.empty()) return false;

    myUndo.push_back(mySolids);
    mySolids = myRedo.back();
    myRedo.pop_back();
    return true;
}
