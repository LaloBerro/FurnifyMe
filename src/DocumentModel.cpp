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
    // Keep the pairing map consistent with what actually exists - a removed
    // body's twin must read back unpaired (twinOf() == -1), not point at an
    // id nothing in the document owns any more.
    unpairInternal(id);
    ++myRevision;
    return true;
}

void DocumentModel::clear()
{
    mySolids.clear();
    myOutlines.clear();
    myTwin.clear();
    ++myRevision;
    // Ids are not reused: a stale id must never silently resolve to a new solid.
}

void DocumentModel::setSymmetry(bool on, const gp_Pln& plane)
{
    mySymmetryOn = on;
    mySymmetryPlane = plane;
    if (!on) myTwin.clear();
    ++myRevision;
}

void DocumentModel::unpairInternal(int id)
{
    const auto it = myTwin.find(id);
    if (it == myTwin.end()) return;
    const int other = it->second;
    myTwin.erase(id);
    myTwin.erase(other);
}

void DocumentModel::pairBodies(int idA, int idB)
{
    if (idA <= 0 || idB <= 0 || idA == idB) return;
    if (!contains(idA) || !contains(idB)) return;

    unpairInternal(idA);
    unpairInternal(idB);
    myTwin[idA] = idB;
    myTwin[idB] = idA;
}

int DocumentModel::twinOf(int id) const
{
    const auto it = myTwin.find(id);
    return it == myTwin.end() ? -1 : it->second;
}

bool DocumentModel::unpairAll()
{
    if (myTwin.empty()) return false;
    myTwin.clear();
    ++myRevision;
    return true;
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
    myUndo.push_back(State{mySolids, myOutlines, myTwin});
    if (myUndo.size() > kMaxHistory) myUndo.erase(myUndo.begin());

    // Anything redoable described a future that no longer follows from here.
    myRedo.clear();
}

bool DocumentModel::undo()
{
    if (myUndo.empty()) return false;

    myRedo.push_back(State{mySolids, myOutlines, myTwin});
    mySolids = myUndo.back().solids;
    myOutlines = myUndo.back().outlines;
    // symmetryOn/symmetryPlane are NOT part of State - see its own comment.
    // Only the pairing map moves with undo/redo.
    myTwin = myUndo.back().twin;
    myUndo.pop_back();
    ++myRevision;
    return true;
}

bool DocumentModel::redo()
{
    if (myRedo.empty()) return false;

    myUndo.push_back(State{mySolids, myOutlines, myTwin});
    mySolids = myRedo.back().solids;
    myOutlines = myRedo.back().outlines;
    myTwin = myRedo.back().twin;
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

    // Symmetry: plane and on/off travel as-is; pairs are re-expressed as
    // POSITIONS into serial.bodies (see DocumentMeta's own comment for why -
    // ids are never persisted). Each pair is emitted once, from the HIGHER
    // id's own position (the loop below skips until it reaches the id whose
    // twin is already smaller), walking mySolids in the same order they
    // were just pushed above so the positions agree with what was actually
    // written.
    meta.symmetryOn = mySymmetryOn;
    meta.symmetryPlane = mySymmetryPlane;
    std::unordered_map<int, std::size_t> positionOfId;
    for (std::size_t i = 0; i < mySolids.size(); ++i) positionOfId[mySolids[i].id] = i;
    for (std::size_t i = 0; i < mySolids.size(); ++i) {
        const int id = mySolids[i].id;
        const int twin = twinOf(id);
        if (twin <= 0 || twin >= id) continue;   // emit once - only from the HIGHER id of the pair
        const auto twinPos = positionOfId.find(twin);
        if (twinPos == positionOfId.end()) continue;
        meta.symmetryPairs.push_back({static_cast<int>(twinPos->second), static_cast<int>(i)});
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

    std::vector<int> bodyIds;
    bodyIds.reserve(serial.bodies.size());
    for (std::size_t i = 0; i < serial.bodies.size(); ++i) {
        const int id = addSolid(serial.bodies[i]);
        setItemName(id, meta.bodyNames[i]);
        setVisible(id, meta.bodyVisible[i]);
        bodyIds.push_back(id);
    }
    for (std::size_t i = 0; i < serial.outlineFaces.size(); ++i) {
        const TopoDS_Face face = TopoDS::Face(serial.outlineFaces[i]);
        const int id = addOutline(face, serial.outlinePlanes[i]);
        setItemName(id, meta.outlineNames[i]);
        setVisible(id, meta.outlineVisible[i]);
    }

    // Symmetry: setSymmetry() first (it clears myTwin outright when off, and
    // does nothing to it when on), THEN translate meta's position-based
    // pairs back into the ids addSolid() just assigned. A pair whose
    // position falls outside bodyIds - a corrupt or hand-edited file - is
    // skipped rather than refusing the whole load; everything else about the
    // document is still good.
    setSymmetry(meta.symmetryOn, meta.symmetryPlane);
    if (meta.symmetryOn) {
        for (const std::pair<int, int>& pair : meta.symmetryPairs) {
            if (pair.first < 0 || pair.first >= static_cast<int>(bodyIds.size())) continue;
            if (pair.second < 0 || pair.second >= static_cast<int>(bodyIds.size())) continue;
            pairBodies(bodyIds[pair.first], bodyIds[pair.second]);
        }
    }

    return true;
}

void DocumentModel::restoreFrom(const DocumentModel& snapshot)
{
    mySolids = snapshot.mySolids;
    myOutlines = snapshot.myOutlines;
    myVisibility = snapshot.myVisibility;
    // Symmetry travels with the rest of the document - `snapshot`'s ids are
    // copied in VERBATIM (unlike fromSerialized(), which reassigns fresh
    // ones), so its pairing map's ids already match snapshot.mySolids and
    // need no translation.
    mySymmetryOn = snapshot.mySymmetryOn;
    mySymmetryPlane = snapshot.mySymmetryPlane;
    myTwin = snapshot.myTwin;
    // Never shrink: `this`'s own counters may already be ahead of
    // `snapshot`'s (this document had more history before the restore than
    // the version ever saw), and `snapshot`'s may be ahead of `this`'s (the
    // version has more items than this document has ever held). Only the
    // larger of the two is safe - see the header comment.
    myNextId = std::max(myNextId, snapshot.myNextId);
    myNextName = std::max(myNextName, snapshot.myNextName);
    myNextOutlineName = std::max(myNextOutlineName, snapshot.myNextOutlineName);
    // Undo/redo are deliberately untouched - the caller's own checkpoint()
    // is what this mutation sits behind.
    ++myRevision;
}
