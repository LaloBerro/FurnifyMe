#include "DocumentModel.h"

#include "ModelingOps.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>

namespace {

// gp_Trsf <-> 12 doubles, row-major (3 rows x 4 columns) via the same
// Value()/SetValues() OCCT itself exposes. The only place a link group's
// placement crosses into DocumentMeta::LinkGroupRecord, so a general
// (not just translation-only) gp_Trsf round-trips exactly - propagation and
// a future rotate/scale placement need that, even though v1's own
// linkExisting() never builds anything but a translation.
std::array<double, 12> trsfToArray(const gp_Trsf& t)
{
    std::array<double, 12> a{};
    int k = 0;
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 4; ++col) a[k++] = t.Value(row, col);
    }
    return a;
}

gp_Trsf trsfFromArray(const std::array<double, 12>& a)
{
    gp_Trsf t;
    t.SetValues(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
    return t;
}

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
    // Same discipline for link groups (Milestone 4) - a removed member must
    // not leave the survivors' placements pointing at an id nothing in the
    // document owns, and a removed ANCHOR needs the same promotion rule
    // unlink() uses.
    unlinkGroupInternal(id);
    ++myRevision;
    return true;
}

void DocumentModel::clear()
{
    mySolids.clear();
    myOutlines.clear();
    myTwin.clear();
    myLinkGroups.clear();
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

DocumentModel::PairResult DocumentModel::pairWithMirror(const std::vector<int>& ids,
                                                         const gp_Pln& plane)
{
    PairResult result;

    // Resolve AND MIRROR every id BEFORE touching the document (fix round
    // 1). "Never half-done" extends past validation into the kernel call
    // itself: whether anything is mutated is gated on ACTUAL pairing
    // outcomes, not merely on ids that passed validation - a candidate that
    // validates cleanly but whose own ModelingOps::mirrorShape() call
    // refuses must not silently take a checkpoint and flip the mode for a
    // net paired == 0. `succeeded` is what actually gets committed below,
    // once the whole list's real outcome is known.
    struct Mirrored {
        int id;
        TopoDS_Shape shape;
    };
    std::vector<Mirrored> succeeded;
    std::unordered_set<int> seen;
    for (int id : ids) {
        if (id <= 0 || !contains(id)) continue;   // unknown/invalid - silently ignored
        if (!seen.insert(id).second) continue;    // duplicate within this call

        // v1 mirror/link exclusion, enforced from this side too - a linked
        // body (Milestone 4) cannot also take a mirror twin.
        if (isLinked(id)) {
            result.skippedLinked.push_back(id);
            continue;
        }

        // symmetryOn() gates every twinOf() read - CLAUDE.md's rule. A
        // pairing entry can survive in State (undo-tracked) even while the
        // live mode is off, and such an entry is inert, not "already
        // paired".
        if (symmetryOn() && twinOf(id) != -1) {
            result.skippedAlreadyPaired.push_back(id);
            continue;
        }

        const TopoDS_Shape shape = shapeOf(id);
        if (ModelingOps::boundingBoxStraddlesPlane(shape, plane)) {
            result.skippedStraddling.push_back(id);
            continue;
        }

        const ModelingOps::BooleanResult mirrored = ModelingOps::mirrorShape(shape, plane);
        if (!mirrored.ok) {
            // A kernel refusal on otherwise-valid geometry is rare, but
            // never-silent-failure applies here exactly as it does at this
            // engine's UI-facing edges: report it, don't just drop it and
            // let `paired` read low with no explanation.
            result.skippedFailed.push_back(id);
            continue;
        }
        succeeded.push_back(Mirrored{id, mirrored.shape});
    }

    if (succeeded.empty()) return result;   // no ACTUAL pairing to commit - no checkpoint, no mode change

    checkpoint();
    setSymmetry(true, plane);
    for (const Mirrored& m : succeeded) {
        // m.shape came straight from a successful mirrorShape() call, which
        // never returns ok == true with a null shape - see BooleanResult's
        // own contract - so addSolid() failing here is not expected. Kept
        // defensive anyway, matching this file's existing idiom elsewhere.
        const int twinId = addSolid(m.shape);
        if (twinId <= 0) continue;
        pairBodies(m.id, twinId);
        ++result.paired;
    }
    return result;
}

bool DocumentModel::unpairAll()
{
    if (myTwin.empty()) return false;
    myTwin.clear();
    ++myRevision;
    return true;
}

// --- link groups (Milestone 4) ----------------------------------------------

void DocumentModel::unlinkGroupInternal(int memberId)
{
    const auto found = myLinkGroups.find(memberId);
    if (found == myLinkGroups.end()) return;

    LinkGroup group = found->second;   // copy - about to erase the map entry it came from
    group.placement.erase(memberId);
    myLinkGroups.erase(memberId);

    if (group.placement.size() < 2) {
        // A "group" of fewer than two members is not a group - dissolve
        // the survivor(s) outright rather than leaving a one-member group
        // with nothing to stay in step with.
        for (const auto& kv : group.placement) myLinkGroups.erase(kv.first);
        return;
    }

    if (memberId == group.anchorId) {
        // Deterministic promotion: the lowest surviving id becomes anchor.
        // group.placement is a std::map<int, gp_Trsf>, so begin()->first is
        // exactly that id with no separate scan. Every remaining placement
        // is re-expressed relative to the NEW anchor's own frame:
        //   shape(X) = shape(oldAnchor).transformed(placement[X])
        //            = shape(newAnchor).transformed(placement[newAnchor]^-1)
        //                                .transformed(placement[X])
        // and "apply A, then B" is B.Multiplied(A) - the same composition
        // rule snapTransform() uses elsewhere in this codebase.
        const int newAnchorId = group.placement.begin()->first;
        const gp_Trsf oldAnchorToNew = group.placement.at(newAnchorId);
        const gp_Trsf newToOldAnchor = oldAnchorToNew.Inverted();
        std::map<int, gp_Trsf> reprojected;
        for (const auto& kv : group.placement) {
            reprojected[kv.first] = kv.second.Multiplied(newToOldAnchor);
        }
        reprojected[newAnchorId] = gp_Trsf();   // exact identity, no numerical drift
        group.anchorId = newAnchorId;
        group.placement = reprojected;
    }

    for (const auto& kv : group.placement) myLinkGroups[kv.first] = group;
}

bool DocumentModel::isLinked(int id) const
{
    return myLinkGroups.find(id) != myLinkGroups.end();
}

int DocumentModel::linkAnchorOf(int id) const
{
    const auto it = myLinkGroups.find(id);
    return it == myLinkGroups.end() ? -1 : it->second.anchorId;
}

bool DocumentModel::linkGroupOf(int id, LinkGroup& out) const
{
    const auto it = myLinkGroups.find(id);
    if (it == myLinkGroups.end()) return false;
    out = it->second;
    return true;
}

DocumentModel::LinkResult DocumentModel::createLinkedCopy(int sourceId, const gp_Trsf& offset)
{
    LinkResult result;
    if (sourceId <= 0 || !contains(sourceId)) {
        result.error = "unknown body";
        return result;
    }
    // v1 mirror/link exclusion, enforced from this side (pairWithMirror
    // enforces it from the mirror side, above).
    if (symmetryOn() && twinOf(sourceId) != -1) {
        result.error = "already mirrored - cannot also be linked";
        return result;
    }

    const TopoDS_Shape sourceShape = shapeOf(sourceId);

    // Resolve which group the copy joins, and its OWN placement within it,
    // before touching the kernel or the document - `sourceId` may already
    // be a member (not necessarily the anchor) of an existing group, in
    // which case the copy attaches to that group's existing anchor rather
    // than founding a redundant second one.
    int anchorId = sourceId;
    gp_Trsf basePlacement;   // identity - used when sourceId founds a fresh group
    const auto groupIt = myLinkGroups.find(sourceId);
    if (groupIt != myLinkGroups.end()) {
        anchorId = groupIt->second.anchorId;
        basePlacement = groupIt->second.placement.at(sourceId);
    }
    // "Apply basePlacement, then offset" - see unlinkGroupInternal()'s own
    // comment on the composition rule.
    const gp_Trsf newPlacement = offset.Multiplied(basePlacement);

    const ModelingOps::BooleanResult transformed = ModelingOps::transformShape(sourceShape, offset);
    if (!transformed.ok) {
        result.error = transformed.error;
        return result;
    }
    if (transformed.shape.IsNull()) {
        // Unreachable per transformShape()'s own contract (ok == true never
        // carries a null shape) - checked explicitly and LOCALLY anyway
        // (fix round 1), so this function's own guarantee that checkpoint()
        // is never taken on a path that ends up mutating nothing does not
        // rely on a cross-function contract holding. Past this line,
        // addSolid() below cannot fail (its own only refusal is a null
        // shape), so checkpoint() is genuinely "after all validation and
        // shape building" now, not merely positioned there.
        result.error = "transform produced no shape";
        return result;
    }

    checkpoint();
    const int newId = addSolid(transformed.shape);
    if (newId <= 0) {
        // Unreachable given the check just above - kept defensive anyway,
        // matching this file's existing idiom (pairWithMirror's own
        // addSolid() call carries the same comment).
        result.error = "failed to add the copy";
        return result;
    }

    LinkGroup group;
    if (groupIt != myLinkGroups.end()) {
        group = groupIt->second;
    } else {
        group.anchorId = anchorId;
        group.placement[anchorId] = gp_Trsf();
    }
    group.placement[newId] = newPlacement;
    for (const auto& kv : group.placement) myLinkGroups[kv.first] = group;

    result.ok = true;
    result.id = newId;
    return result;
}

DocumentModel::LinkResult DocumentModel::linkExisting(const std::vector<int>& ids)
{
    LinkResult result;
    if (ids.size() < 2) {
        result.error = "need at least two bodies to link";
        return result;
    }

    std::unordered_set<int> seen;
    for (int id : ids) {
        if (id <= 0 || !contains(id)) {
            result.error = "unknown body";
            return result;
        }
        if (!seen.insert(id).second) {
            result.error = "duplicate body in the list";
            return result;
        }
        if (isLinked(id)) {
            result.error = "already linked";
            return result;
        }
        if (symmetryOn() && twinOf(id) != -1) {
            result.error = "already mirrored - cannot also be linked";
            return result;
        }
    }

    const int anchorId = ids.front();
    const TopoDS_Shape anchorShape = shapeOf(anchorId);
    const gp_Pnt anchorCentre = ModelingOps::centreOfMass(anchorShape);

    // Resolve-before-mutate, all-or-nothing - the bevels' own discipline:
    // every replacement shape is built BEFORE anything is written, so one
    // kernel refusal refuses the whole call with nothing changed.
    struct Placed {
        int id;
        gp_Trsf placement;
        TopoDS_Shape shape;
    };
    std::vector<Placed> placed;
    placed.reserve(ids.size() - 1);
    for (std::size_t i = 1; i < ids.size(); ++i) {
        const int id = ids[i];
        const gp_Pnt oldCentre = ModelingOps::centreOfMass(shapeOf(id));
        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(anchorCentre, oldCentre));   // anchor -> this member's own old centre
        const ModelingOps::BooleanResult placedShape = ModelingOps::transformShape(anchorShape, trsf);
        if (!placedShape.ok) {
            result.error = placedShape.error;
            return result;
        }
        placed.push_back(Placed{id, trsf, placedShape.shape});
    }

    checkpoint();
    for (const Placed& p : placed) replaceSolid(p.id, p.shape);

    LinkGroup group;
    group.anchorId = anchorId;
    group.placement[anchorId] = gp_Trsf();
    for (const Placed& p : placed) group.placement[p.id] = p.placement;
    for (const auto& kv : group.placement) myLinkGroups[kv.first] = group;

    result.ok = true;
    result.id = anchorId;
    return result;
}

bool DocumentModel::unlink(int memberId)
{
    if (!isLinked(memberId)) return false;
    checkpoint();
    unlinkGroupInternal(memberId);
    ++myRevision;
    return true;
}

bool DocumentModel::propagateLinkedEdit(int editedMemberId, const TopoDS_Shape& newShape)
{
    if (newShape.IsNull()) return false;
    const auto found = myLinkGroups.find(editedMemberId);
    if (found == myLinkGroups.end()) return false;

    const LinkGroup& group = found->second;
    const auto editedPlacement = group.placement.find(editedMemberId);
    if (editedPlacement == group.placement.end()) return false;   // defensive; kept in sync by construction

    // The anchor's own new shape - `newShape` itself when the EDITED member
    // IS the anchor (its own placement is always identity, so re-deriving it
    // through a transform would be a needless kernel round trip), otherwise
    // `newShape` walked back through placement(editedMemberId)^-1.
    TopoDS_Shape anchorShape;
    if (editedMemberId == group.anchorId) {
        anchorShape = newShape;
    } else {
        const gp_Trsf inv = editedPlacement->second.Inverted();
        const ModelingOps::BooleanResult anchorResult = ModelingOps::transformShape(newShape, inv);
        if (!anchorResult.ok) return false;
        anchorShape = anchorResult.shape;
    }

    // Resolve every member's new shape BEFORE writing any of them. The
    // edited member's OWN entry always reuses `newShape` exactly (fix round
    // 1) - writing it back through the anchor and forward again would be an
    // avoidable BRepBuilderAPI_Transform round trip and a needless
    // floating-point drift source on repeated propagated edits to the same
    // member.
    std::vector<std::pair<int, TopoDS_Shape>> resolved;
    resolved.reserve(group.placement.size());
    for (const auto& kv : group.placement) {
        if (kv.first == editedMemberId) {
            resolved.push_back({kv.first, newShape});
            continue;
        }
        if (kv.first == group.anchorId) {
            resolved.push_back({kv.first, anchorShape});
            continue;
        }
        const ModelingOps::BooleanResult memberResult =
            ModelingOps::transformShape(anchorShape, kv.second);
        if (!memberResult.ok) return false;
        resolved.push_back({kv.first, memberResult.shape});
    }

    // ONE State mutation, written directly rather than through N calls to
    // replaceSolid() - see the header's own comment on why this function,
    // alone among the checkpointed commit paths, never calls checkpoint()
    // itself: the caller is already inside its own checkpointed edit, and
    // from that edit's point of view this IS one change, not N.
    for (const auto& kv : resolved) {
        for (Solid& s : mySolids) {
            if (s.id == kv.first) {
                s.shape = kv.second;
                break;
            }
        }
    }
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
    myUndo.push_back(State{mySolids, myOutlines, myTwin, myLinkGroups});
    if (myUndo.size() > kMaxHistory) myUndo.erase(myUndo.begin());

    // Anything redoable described a future that no longer follows from here.
    myRedo.clear();
}

bool DocumentModel::undo()
{
    if (myUndo.empty()) return false;

    myRedo.push_back(State{mySolids, myOutlines, myTwin, myLinkGroups});
    mySolids = myUndo.back().solids;
    myOutlines = myUndo.back().outlines;
    // symmetryOn/symmetryPlane are NOT part of State - see its own comment.
    // Only the pairing map and the link groups move with undo/redo.
    myTwin = myUndo.back().twin;
    myLinkGroups = myUndo.back().linkGroups;
    myUndo.pop_back();
    ++myRevision;
    return true;
}

bool DocumentModel::redo()
{
    if (myRedo.empty()) return false;

    myUndo.push_back(State{mySolids, myOutlines, myTwin, myLinkGroups});
    mySolids = myRedo.back().solids;
    myOutlines = myRedo.back().outlines;
    myTwin = myRedo.back().twin;
    myLinkGroups = myRedo.back().linkGroups;
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
    // Both branches bump myRevision - Task 5's addition. Every other mutator
    // in this file does; this one did not, which left a rename invisible to
    // the dirty star, autosave's arm, and a toast's own revision-guard
    // (documentMovedTo()) - a renamed body would not mark the furniture dirty
    // and a rename toast's Undo pill could survive a change that came after
    // it. The bump lives HERE rather than inside renameSolid() itself:
    // renameSolid() is also called directly by the headless suite
    // (unrelated to this task, and predating it), and leaving it revision-free
    // keeps that surface's behaviour exactly as it was.
    if (renameSolid(id, name)) {
        ++myRevision;
        return true;
    }

    for (Outline& o : myOutlines) {
        if (o.id == id) {
            o.name = name;
            ++myRevision;
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

    // Link groups (Milestone 4): one record per DISTINCT group, emitted the
    // first time mySolids' own order reaches any of its members - so which
    // group is "first" is deterministic run to run, the same property the
    // byte-identical round-trip check above relies on for pairs.
    std::unordered_set<int> emittedAnchors;
    for (std::size_t i = 0; i < mySolids.size(); ++i) {
        const int id = mySolids[i].id;
        const auto groupIt = myLinkGroups.find(id);
        if (groupIt == myLinkGroups.end()) continue;
        const LinkGroup& group = groupIt->second;
        if (!emittedAnchors.insert(group.anchorId).second) continue;   // already written

        const auto anchorPos = positionOfId.find(group.anchorId);
        if (anchorPos == positionOfId.end()) continue;   // defensive - should never miss

        DocumentMeta::LinkGroupRecord record;
        record.anchorPosition = static_cast<int>(anchorPos->second);
        record.memberPositions.reserve(group.placement.size());
        record.placements.reserve(group.placement.size());
        for (const auto& kv : group.placement) {
            const auto pos = positionOfId.find(kv.first);
            if (pos == positionOfId.end()) continue;   // defensive - should never miss
            record.memberPositions.push_back(static_cast<int>(pos->second));
            record.placements.push_back(trsfToArray(kv.second));
        }
        if (record.memberPositions.size() >= 2) meta.linkGroups.push_back(std::move(record));
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

    // Milestone 4, fix round 1 (Finding 2): a body position named in BOTH
    // symmetryPairs and a link group would load simultaneously mirror-paired
    // and linked - the v1 exclusion commitReplaceBody()'s own comment
    // documents as "mutually exclusive by construction... enforced both
    // directions", but that enforcement lives only in the gesture layer
    // (pairWithMirror()/createLinkedCopy()/linkExisting()), never at load
    // time, so a hand-edited or corrupted manifest could still describe
    // one. Refused outright, the same validate-before-mutate law every
    // other structural check above follows - guessing which membership to
    // keep and silently dropping the other is exactly the silent data loss
    // the load laws forbid, the same reasoning a vector-length mismatch is
    // refused rather than truncated to the shorter length.
    {
        std::unordered_set<int> mirroredPositions;
        for (const std::pair<int, int>& pair : meta.symmetryPairs) {
            mirroredPositions.insert(pair.first);
            mirroredPositions.insert(pair.second);
        }
        if (!mirroredPositions.empty()) {
            for (const DocumentMeta::LinkGroupRecord& record : meta.linkGroups) {
                for (int pos : record.memberPositions) {
                    if (mirroredPositions.count(pos) > 0) return false;
                }
            }
        }
    }

    mySolids.clear();
    myOutlines.clear();
    myUndo.clear();
    myRedo.clear();
    myVisibility.clear();
    // Unconditionally, not left to setSymmetry() below - setSymmetry(true, ...)
    // deliberately leaves myTwin untouched (see its own comment), which is
    // correct when it is called mid-document but wrong here: loading a
    // SECOND document into a REUSED DocumentModel (MainWindow::openFurniture()
    // switching furniture without reconstructing its own document, or a
    // second loadFurniture()/loadVersion() call onto the same instance) must
    // not carry the OLD document's pairing map forward. Ids are never
    // reused, so a stale entry can never mispair a live body - but it is
    // still stale state this function's own job is to replace wholesale, not
    // merge onto.
    myTwin.clear();
    // Same reasoning for link groups (Milestone 4) - a second load onto the
    // same instance must not carry the OLD document's groups forward.
    myLinkGroups.clear();
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

    // Link groups (Milestone 4): translate meta's position-based records
    // back into the ids addSolid() just assigned, the same
    // forward-compatible rule symmetryPairs follows just above - a
    // malformed record (a position out of range, mismatched array sizes,
    // or fewer than two members after filtering) is skipped rather than
    // refusing the whole load. An empty/absent `meta.linkGroups` (an older
    // save with no such key) naturally round-trips as "nothing linked",
    // since the vector default-constructs empty and this loop simply never
    // runs.
    for (const DocumentMeta::LinkGroupRecord& record : meta.linkGroups) {
        if (record.memberPositions.size() != record.placements.size()) continue;
        if (record.memberPositions.size() < 2) continue;
        if (record.anchorPosition < 0 ||
            record.anchorPosition >= static_cast<int>(bodyIds.size())) {
            continue;
        }

        std::map<int, gp_Trsf> placement;
        bool malformed = false;
        bool anchorSeen = false;
        for (std::size_t i = 0; i < record.memberPositions.size(); ++i) {
            const int pos = record.memberPositions[i];
            if (pos < 0 || pos >= static_cast<int>(bodyIds.size())) {
                malformed = true;
                break;
            }
            const int id = bodyIds[static_cast<std::size_t>(pos)];
            placement[id] = trsfFromArray(record.placements[i]);
            if (pos == record.anchorPosition) anchorSeen = true;
        }
        if (malformed || !anchorSeen || placement.size() < 2) continue;

        LinkGroup group;
        group.anchorId = bodyIds[static_cast<std::size_t>(record.anchorPosition)];
        group.placement = placement;
        for (const auto& kv : placement) myLinkGroups[kv.first] = group;
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
    // Link groups travel the same way symmetry pairing does - `snapshot`'s
    // ids are copied in verbatim, so no position translation is needed.
    myLinkGroups = snapshot.myLinkGroups;
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
