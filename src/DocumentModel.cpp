#include "DocumentModel.h"

#include <algorithm>
#include <cstdio>

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>

namespace {

std::string defaultName(int index)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "Body %02d", index);
    return std::string(buffer);
}

// The vocabulary's word, and the same two-digit numbering bodies wear - see
// CLAUDE.md's table. "Outline" is what the user is taught to call the closed
// shape from the moment they draw it, so the item it becomes must not be
// named anything else.
std::string defaultOutlineName(int index)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "Outline %02d", index);
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
    myOutlines.clear();
    ++myRevision;
    // Ids are not reused: a stale id must never silently resolve to a new solid.
}

int DocumentModel::addOutline(const TopoDS_Face& face, const gp_Pln& plane)
{
    if (face.IsNull()) return 0;

    const int id = myNextId++;
    myOutlines.push_back(Outline{id, defaultOutlineName(myNextOutlineName++), face, plane});
    ++myRevision;
    return id;
}

bool DocumentModel::removeOutline(int id)
{
    const auto it = std::find_if(myOutlines.begin(), myOutlines.end(),
                                 [id](const Outline& o) { return o.id == id; });
    if (it == myOutlines.end()) return false;

    myOutlines.erase(it);
    ++myRevision;
    return true;
}

bool DocumentModel::containsOutline(int id) const
{
    return std::any_of(myOutlines.begin(), myOutlines.end(),
                       [id](const Outline& o) { return o.id == id; });
}

TopoDS_Face DocumentModel::outlineFace(int id) const
{
    for (const Outline& o : myOutlines) {
        if (o.id == id) return o.face;
    }
    return TopoDS_Face();
}

std::string DocumentModel::outlineNameOf(int id) const
{
    for (const Outline& o : myOutlines) {
        if (o.id == id) return o.name;
    }
    return std::string();
}

bool DocumentModel::outlinePlane(int id, gp_Pln& out) const
{
    for (const Outline& o : myOutlines) {
        if (o.id == id) {
            out = o.plane;
            return true;
        }
    }
    return false;
}

int DocumentModel::convertOutlineToBody(int outlineId, const TopoDS_Shape& solid)
{
    // Both halves are checked BEFORE either is performed. A conversion that
    // removed the outline and then failed to add the body would destroy the
    // user's work on a null shape, and the caller - which has already taken
    // its checkpoint - would have nothing to tell it that happened.
    if (solid.IsNull()) return 0;
    const auto it = std::find_if(myOutlines.begin(), myOutlines.end(),
                                 [outlineId](const Outline& o) { return o.id == outlineId; });
    if (it == myOutlines.end()) return 0;

    myOutlines.erase(it);
    const int id = myNextId++;
    mySolids.push_back(Solid{id, defaultName(myNextName++), solid});
    // ONE bump for the pair. The conversion is one change to the document, and
    // a toast stamped with the revision before it must be dismissed by it
    // exactly once.
    ++myRevision;
    return id;
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
    myUndo.push_back(State{mySolids, myOutlines});
    if (myUndo.size() > kMaxHistory) myUndo.erase(myUndo.begin());

    // Anything redoable described a future that no longer follows from here.
    myRedo.clear();
}

bool DocumentModel::undo()
{
    if (myUndo.empty()) return false;

    myRedo.push_back(State{mySolids, myOutlines});
    mySolids = myUndo.back().solids;
    myOutlines = myUndo.back().outlines;
    myUndo.pop_back();
    ++myRevision;
    return true;
}

bool DocumentModel::redo()
{
    if (myRedo.empty()) return false;

    myUndo.push_back(State{mySolids, myOutlines});
    mySolids = myRedo.back().solids;
    myOutlines = myRedo.back().outlines;
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

bool DocumentModel::setItemName(int id, const std::string& name)
{
    if (renameSolid(id, name)) return true;

    for (Outline& o : myOutlines) {
        if (o.id == id) {
            o.name = name;
            return true;
        }
    }
    return false;
}

void DocumentModel::setVisible(int id, bool visible)
{
    myVisibility[id] = visible;
}

bool DocumentModel::isVisible(int id) const
{
    const auto it = myVisibility.find(id);
    return it == myVisibility.end() ? true : it->second;
}

FurnifySerial::SerializedDocument DocumentModel::toSerialized(DocumentMeta& meta) const
{
    FurnifySerial::SerializedDocument serial;
    meta = DocumentMeta{};

    serial.bodies.reserve(mySolids.size());
    meta.bodyNames.reserve(mySolids.size());
    meta.bodyVisible.reserve(mySolids.size());
    for (const Solid& s : mySolids) {
        serial.bodies.push_back(s.shape);
        meta.bodyNames.push_back(s.name);
        meta.bodyVisible.push_back(isVisible(s.id));
    }

    serial.outlineFaces.reserve(myOutlines.size());
    serial.outlinePlanes.reserve(myOutlines.size());
    meta.outlineNames.reserve(myOutlines.size());
    meta.outlineVisible.reserve(myOutlines.size());
    for (const Outline& o : myOutlines) {
        serial.outlineFaces.push_back(o.face);
        serial.outlinePlanes.push_back(o.plane);
        meta.outlineNames.push_back(o.name);
        meta.outlineVisible.push_back(isVisible(o.id));
    }

    return serial;
}

bool DocumentModel::fromSerialized(const FurnifySerial::SerializedDocument& serial,
                                   const DocumentMeta& meta)
{
    // Validate everything BEFORE mutating `this` - a refused load must
    // leave the document exactly as it was.
    if (serial.outlineFaces.size() != serial.outlinePlanes.size()) return false;
    if (serial.bodies.size() != meta.bodyNames.size()) return false;
    if (serial.bodies.size() != meta.bodyVisible.size()) return false;
    if (serial.outlineFaces.size() != meta.outlineNames.size()) return false;
    if (serial.outlineFaces.size() != meta.outlineVisible.size()) return false;

    for (const TopoDS_Shape& s : serial.bodies) {
        if (s.IsNull()) return false;
    }
    for (const TopoDS_Shape& s : serial.outlineFaces) {
        if (s.IsNull() || s.ShapeType() != TopAbs_FACE) return false;
    }

    mySolids.clear();
    myOutlines.clear();
    myUndo.clear();
    myRedo.clear();
    myVisibility.clear();
    ++myRevision;

    for (std::size_t i = 0; i < serial.bodies.size(); ++i) {
        const int id = addSolid(serial.bodies[i]);
        setItemName(id, meta.bodyNames[i]);
        setVisible(id, meta.bodyVisible[i]);
    }
    for (std::size_t i = 0; i < serial.outlineFaces.size(); ++i) {
        const TopoDS_Face face = TopoDS::Face(serial.outlineFaces[i]);
        const int id = addOutline(face, serial.outlinePlanes[i]);
        setItemName(id, meta.outlineNames[i]);
        setVisible(id, meta.outlineVisible[i]);
    }

    return true;
}
