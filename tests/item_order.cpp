//
// The Items drawer's stored sibling order (T-063): DocumentModel's sparse,
// per-parent sequence, the one mutator that moves rows, and the steps the
// keyboard takes. Qt-free, so it runs wherever the other headless tests do.
//
#include "DocumentModel.h"
#include "ModelingOps.h"

#include <cstdio>
#include <string>
#include <vector>

#include <BRepBuilderAPI_MakeFace.hxx>
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

// The names a parent's rows read, in the order orderedChildren() resolves -
// a string so a failing check prints exactly what the drawer would show.
std::string namesIn(const DocumentModel& doc, int parent)
{
    std::string out;
    for (int id : doc.orderedChildren(parent)) {
        if (!out.empty()) out += ", ";
        if (doc.groupExists(id)) out += doc.groupNameOf(id);
        else if (doc.containsOutline(id)) out += doc.outlineNameOf(id);
        else out += doc.nameOf(id);
    }
    return out;
}

int body(DocumentModel& doc, const char* name, double x, int folder = 0)
{
    const int id = doc.addSolid(ModelingOps::makeBox(gp_Pnt(x, 0.0, 0.0), 18.0, 300.0, 400.0));
    doc.setItemName(id, name);
    if (folder != 0) doc.setItemGroup(id, folder);
    return id;
}

int outline(DocumentModel& doc, const char* name)
{
    const gp_Pln plane;
    const int id = doc.addOutline(BRepBuilderAPI_MakeFace(plane, 0.0, 100.0, 0.0, 50.0).Face(), plane);
    doc.setItemName(id, name);
    return id;
}

int folderNamed(const DocumentModel& doc, const std::string& name)
{
    for (const DocumentModel::Group& g : doc.groups())
        if (g.name == name) return g.id;
    return 0;
}

}  // namespace

int main()
{
    using MoveCheck = DocumentModel::MoveCheck;
    constexpr int kInto = DocumentModel::kIntoFolder;

    // --- the automatic order, and what a stored one does to it --------------
    {
        DocumentModel doc;
        const int carcass = doc.createGroup("Carcass");
        body(doc, "Shelf", 0.0);
        const int back = body(doc, "Back", 100.0);
        outline(doc, "Sketch A");
        check(namesIn(doc, 0) == "Carcass, Sketch A, Shelf, Back",
              "with nothing stored the root reads folders, outlines, then bodies by creation (" +
                  namesIn(doc, 0) + ")");
        check(!doc.hasStoredOrder(0), "...and nothing is stored for it");

        doc.checkpoint();
        const int before = doc.revision();
        const DocumentModel::MoveResult moved = doc.moveItems({back}, 0, carcass);
        check(moved.check == MoveCheck::Moved, "Back moved above Carcass is Moved");
        check(namesIn(doc, 0) == "Back, Carcass, Sketch A, Shelf",
              "...and the root reads Back first (" + namesIn(doc, 0) + ")");
        check(doc.revision() == before + 1, "one move bumps revision() exactly once");
        check(doc.hasStoredOrder(0), "the root has a stored sequence now");

        const bool undone = doc.undo();
        check(undone && namesIn(doc, 0) == "Carcass, Sketch A, Shelf, Back" &&
                  !doc.hasStoredOrder(0),
              "one undo puts the automatic order back and stores nothing (" + namesIn(doc, 0) +
                  ")");
        const bool redone = doc.redo();
        check(redone && namesIn(doc, 0) == "Back, Carcass, Sketch A, Shelf",
              "redo brings the move back");

        body(doc, "Top", 200.0);
        check(namesIn(doc, 0) == "Back, Carcass, Sketch A, Shelf, Top",
              "a body made after a reorder lands at the END of its parent (" + namesIn(doc, 0) +
                  ")");

        doc.checkpoint();
        doc.removeSolid(back);
        check(namesIn(doc, 0) == "Carcass, Sketch A, Shelf, Top",
              "a deleted body drops out of the sequence when it is read");
        doc.undo();
        check(namesIn(doc, 0) == "Back, Carcass, Sketch A, Shelf, Top",
              "...and undoing the delete puts it back in its exact place");

        const int stamp = doc.revision();
        const DocumentModel::MoveResult same = doc.moveItems({back}, 0, carcass);
        check(same.check == MoveCheck::NoChange, "moving Back to where it already is is NoChange");
        check(doc.revision() == stamp, "...and NoChange does not move revision()");
    }

    // --- into a folder, out of one, and the folder that would hold itself ----
    {
        DocumentModel doc;
        const int carcass = doc.createGroup("Carcass");
        const int drawer = doc.createGroup("Drawer", carcass);
        const int doors = doc.createGroup("Doors");
        const int side = body(doc, "Side left", 0.0, carcass);
        const int front = body(doc, "Drawer front", 100.0, drawer);
        const int shelf = body(doc, "Shelf", 200.0);
        body(doc, "Door left", 300.0, doors);

        DocumentModel::MoveResult r = doc.checkMove({shelf}, doors, kInto);
        check(r.check == MoveCheck::Moved && r.carried == std::vector<int>{shelf} &&
                  r.fromFolder == 0,
              "a dry run of Shelf into Doors says Moved, carrying Shelf from the root");
        check(doc.groupOf(shelf) == 0, "...and a dry run changes nothing");

        r = doc.moveItems({shelf}, doors, kInto);
        check(doc.groupOf(shelf) == doors && namesIn(doc, doors) == "Shelf, Door left",
              "into Doors with no stored order: the folder keeps the automatic order, creation "
              "order, Shelf before Door left (" + namesIn(doc, doors) + ")");
        check(!doc.hasStoredOrder(doors),
              "an into-folder drop on a folder with no stored order stores none");

        r = doc.checkMove({carcass}, drawer, kInto);
        check(r.check == MoveCheck::IntoItself && r.offendingFolder == carcass,
              "Carcass into Drawer, a folder it holds, is IntoItself naming Carcass");
        check(doc.checkMove({carcass}, carcass, kInto).check == MoveCheck::IntoItself,
              "Carcass into itself is IntoItself too");
        check(doc.checkMove({9999}, 0, 0).check == MoveCheck::Unknown,
              "an id the document does not know is Unknown");
        check(doc.checkMove({shelf}, 0, front).check == MoveCheck::Unknown,
              "a beforeId that is not a child of that parent is Unknown");
        check(doc.checkMove({shelf}, 0, kInto).check == MoveCheck::Unknown,
              "into the root is Unknown - the root is not a folder");

        // REVIEW FOCUS 1: a folder carried together with one of its own bodies.
        r = doc.moveItems({drawer, front}, doors, kInto);
        check(r.check == MoveCheck::Moved && r.carried == std::vector<int>{drawer},
              "carrying Drawer AND Drawer front carries the folder alone");
        check(doc.groupOf(drawer) == doors && doc.groupOf(front) == drawer,
              "...so Drawer is in Doors and Drawer front is still inside Drawer");

        r = doc.moveItems({side}, 0, 0);
        check(r.check == MoveCheck::Moved && r.fromFolder == carcass && doc.groupOf(side) == 0,
              "Side left moved out of Carcass to the root, fromFolder naming Carcass");
        const std::vector<int> root = doc.orderedChildren(0);
        check(!root.empty() && root.back() == side, "...at the very end of the root");
    }

    // --- REVIEW FOCUS 4: the mirrored twin follows its source -----------------
    {
        DocumentModel doc;
        const int drawer = doc.createGroup("Drawer");
        const int elsewhere = doc.createGroup("Elsewhere");
        const int left = body(doc, "Side left", 0.0);
        const int right = body(doc, "Side right", 100.0, elsewhere);
        const int top = body(doc, "Top", 200.0, drawer);
        const gp_Pln mirror(gp_Pnt(50.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0));
        doc.setSymmetry(true, mirror);
        doc.pairBodies(left, right);

        DocumentModel::MoveResult r = doc.moveItems({left}, drawer, top);
        check(r.check == MoveCheck::Moved && r.twins == std::vector<int>{right},
              "moving Side left carries its mirrored twin");
        check(doc.groupOf(right) == drawer,
              "the mirrored twin follows its source into Drawer, out of the folder it lived in");
        check(namesIn(doc, drawer) == "Side left, Side right, Top",
              "the twin sits right behind its source, both above Top (" + namesIn(doc, drawer) +
                  ")");

        r = doc.checkMove({left, right}, 0, 0);
        check(r.twins.empty() && r.carried.size() == 2,
              "a twin carried in its own right is not added a second time");

        // Mirroring off WITH a pairing still in the map, so the symmetryOn()
        // term is what stops the twin following: checkpoint the paired state,
        // switch the mode off (which unpairs), then undo - the pairing map
        // rides in State, the mode does not.
        doc.checkpoint();
        doc.setSymmetry(false, mirror);
        const bool undone = doc.undo();
        check(undone && !doc.symmetryOn() && doc.twinOf(left) == right,
              "set-up: mirroring is off yet Side left still has its twin on record");
        r = doc.checkMove({left}, 0, 0);
        check(r.twins.empty(), "with mirroring off nothing follows, twin on record or not");
    }

    // --- the keyboard's steps ---------------------------------------------------
    {
        DocumentModel doc;
        const int carcass = doc.createGroup("Carcass");
        const int a = body(doc, "A", 0.0, carcass);
        body(doc, "B", 100.0, carcass);
        const int c = body(doc, "C", 200.0, carcass);
        const int shelf = body(doc, "Shelf", 300.0);

        check(!doc.siblingStepTarget({a}, -1).ok, "the first row in a folder has nowhere up to go");
        DocumentModel::MoveTarget down = doc.siblingStepTarget({a}, +1);
        check(down.ok && down.parent == carcass && down.beforeId == c,
              "A one place down goes in front of C");
        doc.moveItems({a}, down.parent, down.beforeId);
        check(namesIn(doc, carcass) == "B, A, C", "...and Carcass reads B, A, C");
        down = doc.siblingStepTarget({a}, +1);
        check(down.ok && down.beforeId == 0, "one more down is the end of the folder");
        doc.moveItems({a}, down.parent, down.beforeId);
        check(namesIn(doc, carcass) == "B, C, A", "...B, C, A");
        check(!doc.siblingStepTarget({a}, +1).ok, "the last row has nowhere down to go");
        check(!doc.siblingStepTarget({a, shelf}, -1).ok,
              "rows in two different parents step nowhere together");

        const DocumentModel::MoveTarget out = doc.outOfFolderTarget({a});
        check(out.ok && out.parent == 0 && out.beforeId == shelf,
              "out of Carcass lands just after it - in front of Shelf");
        doc.moveItems({a}, out.parent, out.beforeId);
        check(namesIn(doc, 0) == "Carcass, A, Shelf",
              "...the root reads Carcass, A, Shelf (" + namesIn(doc, 0) + ")");
        check(!doc.outOfFolderTarget({shelf}).ok, "a row at the root has no folder to leave");
    }

    // --- a duplicate lands after its source, and a restore brings the order ---
    {
        DocumentModel doc;
        const int a = body(doc, "A", 0.0);
        const int b = body(doc, "B", 100.0);
        const int early = body(doc, "A copy", 200.0);
        check(!doc.placeAfter(early, a),
              "with no stored order placeAfter() does nothing - the automatic order stands");
        doc.moveItems({b}, 0, a);
        const int copy = body(doc, "A copy 2", 300.0);
        check(doc.placeAfter(copy, a), "with a stored order the duplicate goes in after its source");
        check(namesIn(doc, 0) == "B, A, A copy 2, A copy",
              "...directly after A (" + namesIn(doc, 0) + ")");

        DocumentModel version = doc;
        DocumentModel live;
        const int other = live.createGroup("Other");
        body(live, "Z", 0.0, other);
        live.checkpoint();
        live.restoreFrom(version);
        check(namesIn(live, 0) == "B, A, A copy 2, A copy",
              "restoreFrom() brings the version's order with it (" + namesIn(live, 0) + ")");
        check(folderNamed(live, "Other") == 0,
              "...and the version's folders, not the live document's");
    }

    // --- the order in DocumentMeta: round trip, absent key, every refusal ----
    {
        DocumentModel doc;
        const int carcass = doc.createGroup("Carcass");
        const int drawer = doc.createGroup("Drawer", carcass);
        body(doc, "Side left", 0.0, carcass);
        const int top = body(doc, "Top", 100.0, carcass);
        const int shelf = body(doc, "Shelf", 200.0);
        outline(doc, "Sketch A");
        doc.moveItems({shelf}, 0, carcass);    // root:    Shelf, Carcass, Sketch A
        doc.moveItems({top}, carcass, drawer); // Carcass: Top, Drawer, Side left

        DocumentModel::DocumentMeta meta;
        const FurnifySerial::SerializedDocument serial = doc.toSerialized(meta);
        check(meta.order.size() == 2,
              "two parents carry a stored sequence, so two records are written (" +
                  std::to_string(meta.order.size()) + ")");

        DocumentModel back;
        const bool loaded = back.fromSerialized(serial, meta);
        check(loaded, "a document with an order block loads");
        check(namesIn(back, 0) == "Shelf, Carcass, Sketch A",
              "...the root reads as it was saved (" + namesIn(back, 0) + ")");
        check(namesIn(back, folderNamed(back, "Carcass")) == "Top, Drawer, Side left",
              "...and so does Carcass (" + namesIn(back, folderNamed(back, "Carcass")) + ")");

        DocumentModel::DocumentMeta older = meta;
        older.order.clear();
        DocumentModel flat;
        const bool flatLoaded = flat.fromSerialized(serial, older);
        check(flatLoaded && namesIn(flat, 0) == "Carcass, Sketch A, Shelf",
              "a file with no order block loads in the automatic order (" + namesIn(flat, 0) + ")");

        // Every refusal leaves a target that already holds a body untouched.
        const auto refuses = [&](const DocumentModel::DocumentMeta& bad, const std::string& what) {
            DocumentModel target;
            const int keep = body(target, "Keep", 0.0);
            const bool accepted = target.fromSerialized(serial, bad);
            check(!accepted && target.count() == 1 && target.contains(keep),
                  what + " - refused, the target untouched");
        };
        using Ref = DocumentModel::DocumentMeta::ItemRef;
        {
            DocumentModel::DocumentMeta bad = meta;
            bad.order[0].parentIndex = 7;
            refuses(bad, "a sequence for a folder that does not exist");
        }
        {
            DocumentModel::DocumentMeta bad = meta;
            bad.order.push_back(bad.order[0]);
            refuses(bad, "two sequences for one parent");
        }
        {
            DocumentModel::DocumentMeta bad = meta;
            bad.order[0].items.push_back(bad.order[0].items[0]);
            refuses(bad, "one item named twice in a sequence");
        }
        {
            DocumentModel::DocumentMeta bad = meta;
            bad.order[0].items[0].index = 99;
            refuses(bad, "an item index past the end of its list");
        }
        {
            // order[0] is the ROOT's record (std::map emits parent 0 first);
            // body position 0 is Side left, which lives in Carcass.
            DocumentModel::DocumentMeta bad = meta;
            Ref stray;
            stray.kind = Ref::Kind::Body;
            stray.index = 0;
            bad.order[0].items.push_back(stray);
            refuses(bad, "a sequence naming a body that lives in another folder");
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
