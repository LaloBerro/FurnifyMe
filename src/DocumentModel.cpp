#include "DocumentModel.h"

#include <algorithm>
#include <cstdio>

namespace {

std::string defaultName(int index)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "Body %02d", index);
    return std::string(buffer);
}

}  // namespace

int DocumentModel::addSolid(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0;

    const int id = myNextId++;
    mySolids.push_back(Solid{id, defaultName(myNextName++), shape});
    ++myRevision;
    return id;
}

bool DocumentModel::replaceSolid(int id, const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return false;

    for (Solid& s : mySolids) {
        if (s.id == id) {
            s.shape = shape;
            ++myRevision;
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
    ++myRevision;
    return true;
}

void DocumentModel::clear()
{
    mySolids.clear();
    ++myRevision;
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
    ++myRevision;
    return true;
}

bool DocumentModel::redo()
{
    if (myRedo.empty()) return false;

    myUndo.push_back(mySolids);
    mySolids = myRedo.back();
    myRedo.pop_back();
    ++myRevision;
    return true;
}

std::string DocumentModel::nameOf(int id) const
{
    for (const Solid& s : mySolids) {
        if (s.id == id) return s.name;
    }
    return std::string();
}

bool DocumentModel::renameSolid(int id, const std::string& name)
{
    for (Solid& s : mySolids) {
        if (s.id == id) {
            s.name = name;
            return true;
        }
    }
    return false;
}
