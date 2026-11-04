// The Items drawer's model for the page (ItemsModel) - migration Phase 3h,
// headless, on a real DocumentModel. Every rule here is one ItemsPanel keeps;
// the checks name the ruling they hold the HTML drawer to.
//
// OCCT first, by the Handle() rule - DocumentModel.h carries OCCT headers.
#include "DocumentModel.h"
#include "session/ItemMoveText.h"
#include "session/ItemsKeys.h"
#include "session/ItemsModel.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pln.hxx>

#include <QCoreApplication>

#include <cstdio>
#include <vector>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const QString& what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    if (!ok) ++gFailures;
}

QString ids(const std::vector<int>& v)
{
    QStringList parts;
    for (int id : v) parts << QString::number(id);
    return QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
}

// What the hooks were asked, as MainWindow would be.
struct Calls
{
    std::vector<std::vector<int>> selects;
    std::vector<int> outlines;
    struct Visible { std::vector<int> ids; bool outline; bool show; };
    std::vector<Visible> visibles;
    std::vector<int> grains;
    struct Rename { int id; bool outline; QString name; };
    std::vector<Rename> renames;
    std::vector<std::pair<int, QString>> folderRenames;
    struct Move { std::vector<int> ids; int parent; int before; };
    std::vector<Move> moves;
    int newFolders = 0;
    std::vector<int> wraps, removes, deletes;
    QStringList refusals;
    int withdrawals = 0;
    int dragLines = 0;
};

QVariantMap rowOf(const ItemsModel& model, int id)
{
    for (const QVariant& v : model.rows())
        if (v.toMap().value(QStringLiteral("id")).toInt() == id) return v.toMap();
    return {};
}

QStringList rowNames(const ItemsModel& model)
{
    QStringList names;
    for (const QVariant& v : model.rows()) names << v.toMap().value(QStringLiteral("name")).toString();
    return names;
}

QVariantMap entryOf(const ItemsModel& model, const QString& id)
{
    for (const QVariant& v : model.menu().value(QStringLiteral("entries")).toList())
        if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap();
    return {};
}
}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // A cabinet: Carcass { Side left, Side right, Drawer { Front } }, then a
    // loose Shelf and Back, and one outline.
    DocumentModel doc;
    auto board = [&](const char* name, double x) {
        const int id = doc.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(x, 0.0, 0.0), 18.0, 300.0, 600.0).Shape());
        doc.setItemName(id, name);
        return id;
    };
    const int sideL = board("Side left", 0.0);
    const int sideR = board("Side right", 100.0);
    const int front = board("Front", 200.0);
    const int shelf = board("Shelf", 300.0);
    const int back = board("Back", 400.0);
    const gp_Pln ground;
    const int outline = doc.addOutline(BRepBuilderAPI_MakeFace(ground, 0.0, 100.0, 0.0, 50.0).Face(), ground);
    const int carcass = doc.createGroup("Carcass");
    const int drawer = doc.createGroup("Drawer", carcass);
    doc.setItemGroup(sideL, carcass);
    doc.setItemGroup(sideR, carcass);
    doc.setItemGroup(front, drawer);
    // Drawer last inside Carcass (a stored order), so a Shift range between
    // the sides and the loose boards crosses a FOLDED folder.
    doc.moveItems({drawer}, carcass, 0);

    Calls calls;
    ItemsModel* modelPtr = nullptr;
    ItemsModel::Inputs in;
    in.document = &doc;
    in.live = true;
    in.shown = true;
    in.newFolderEnabled = false;
    in.newFolderTip = QStringLiteral("Select bodies first");

    ItemsModel::Hooks hooks;
    // The viewport answers a selection by announcing it, and the window then
    // pushes its state - MainWindow's order.
    hooks.select = [&](const std::vector<int>& v) {
        calls.selects.push_back(v);
        in.selected = v;
        modelPtr->showSelection(v);
        modelPtr->setState(in);
    };
    hooks.activateOutline = [&](int id) { calls.outlines.push_back(id); };
    hooks.setVisible = [&](const std::vector<int>& v, bool outlineRow, bool show) {
        calls.visibles.push_back({v, outlineRow, show});
    };
    hooks.toggleGrain = [&](int id) { calls.grains.push_back(id); };
    hooks.renameItem = [&](int id, bool o, const QString& n) { calls.renames.push_back({id, o, n}); };
    hooks.renameFolder = [&](int id, const QString& n) { calls.folderRenames.emplace_back(id, n); };
    hooks.move = [&](const std::vector<int>& v, int p, int b) { calls.moves.push_back({v, p, b}); };
    hooks.newFolder = [&] { ++calls.newFolders; };
    hooks.wrapFolder = [&](int id) { calls.wraps.push_back(id); };
    hooks.removeFolder = [&](int id) { calls.removes.push_back(id); };
    hooks.deleteFolder = [&](int id) { calls.deletes.push_back(id); };
    hooks.moveRefused = [&](const QString& r) { calls.refusals << r; };
    hooks.withdrawRefusal = [&] { ++calls.withdrawals; };
    hooks.dragStatusChanged = [&] { ++calls.dragLines; };

    ItemsModel model(hooks);
    modelPtr = &model;
    int changes = 0;
    QObject::connect(&model, &ItemsModel::changed, [&] { ++changes; });
    model.setState(in);

    // --- the tree --------------------------------------------------------------
    {
        QStringList expected;
        for (int id : doc.orderedChildren(0)) {
            if (doc.groupExists(id)) expected << QString::fromStdString(doc.groupNameOf(id));
            else if (doc.containsOutline(id)) expected << QString::fromStdString(doc.outlineNameOf(id));
            else expected << QString::fromStdString(doc.nameOf(id));
        }
        check(rowNames(model) == expected,
              "folders start FOLDED: the rows are the root's children in orderedChildren() order - " +
                  rowNames(model).join(QStringLiteral(", ")));
    }
    check(rowOf(model, sideL).isEmpty(), "a body in a folded folder is not a row");
    check(rowOf(model, carcass).value(QStringLiteral("kind")).toString() == QStringLiteral("folder") &&
              rowOf(model, outline).value(QStringLiteral("kind")).toString() == QStringLiteral("outline") &&
              rowOf(model, shelf).value(QStringLiteral("kind")).toString() == QStringLiteral("body"),
          "each row says what it is");
    model.toggleFold(carcass);
    check(rowOf(model, sideL).value(QStringLiteral("depth")).toInt() == 1 &&
              rowOf(model, drawer).value(QStringLiteral("depth")).toInt() == 1 &&
              rowOf(model, sideL).value(QStringLiteral("parent")).toInt() == carcass,
          "opening Carcass shows its children one level in");
    check(rowNames(model).indexOf(QStringLiteral("Drawer")) == rowNames(model).indexOf(QStringLiteral("Side right")) + 1,
          "a stored order is the order the rows are in");
    check(rowOf(model, front).isEmpty(), "and Drawer, inside it, is still folded");
    check(rowOf(model, carcass).value(QStringLiteral("open")).toBool() &&
              rowOf(model, carcass).value(QStringLiteral("twistyName")).toString() == QStringLiteral("Close Carcass"),
          "the open folder's twisty says what a click will do, with the folder named");
    check(model.isFolderOpen(carcass), "the fold is held by folder id");
    check(rowOf(model, shelf).value(QStringLiteral("grain")).toString() == QStringLiteral("along") &&
              rowOf(model, outline).value(QStringLiteral("grain")).toString().isEmpty() &&
              rowOf(model, carcass).value(QStringLiteral("grain")).toString().isEmpty(),
          "only a body row carries the grain mark");
    check(rowOf(model, sideL).value(QStringLiteral("name")).toString() == QStringLiteral("Side left") &&
              rowOf(model, sideL).value(QStringLiteral("accessibleName")).toString() ==
                  QStringLiteral("Side left, in Carcass") &&
              rowOf(model, sideL).value(QStringLiteral("eyeName")).toString() == QStringLiteral("Hide Side left"),
          "a row's accessible name, and its eye's, are the Qt drawer's words");
    check(rowOf(model, shelf).value(QStringLiteral("above")).toMap().value(QStringLiteral("before")).toInt() == shelf,
          "a row's \"above\" target goes in front of it");

    // --- marks -----------------------------------------------------------------
    in.selected = {sideL};
    model.setState(in);
    check(rowOf(model, sideL).value(QStringLiteral("marked")).toBool(), "a selected body's row is marked");
    check(rowOf(model, carcass).value(QStringLiteral("partial")).toString() == QStringLiteral("1/3") &&
              !rowOf(model, carcass).value(QStringLiteral("marked")).toBool(),
          "a partly selected folder reads 1/3 and wears no fill - \"" +
              rowOf(model, carcass).value(QStringLiteral("partial")).toString() + "\"");
    check(rowOf(model, carcass).value(QStringLiteral("partialWords")).toString() ==
              QStringLiteral("1 of 3 bodies in Carcass is selected"),
          "and says the count in words (F18)");
    in.selected = {sideL, sideR, front};
    model.setState(in);
    check(rowOf(model, carcass).value(QStringLiteral("marked")).toBool() &&
              rowOf(model, carcass).value(QStringLiteral("partial")).toString().isEmpty(),
          "every body under it selected: the folder is marked, no count");
    in.selected = {};
    in.pendingOutline = outline;
    model.setState(in);
    {
        const QVariantMap r = rowOf(model, outline);
        check(r.value(QStringLiteral("pending")).toBool() && !r.value(QStringLiteral("marked")).toBool(),
              "the pending outline is NOT marked (ruling D4)");
        check(r.value(QStringLiteral("accessibleName")).toString().endsWith(QStringLiteral(", waiting to extrude")) &&
                  r.value(QStringLiteral("tip")).toString().endsWith(QStringLiteral(", waiting to extrude")),
              "its name and tooltip say it instead");
    }

    // --- clicks ----------------------------------------------------------------
    calls.selects.clear();
    model.activate(shelf, false, false);
    check(calls.selects.size() == 1 && calls.selects.back() == std::vector<int>{shelf},
          "a plain click selects that body alone");
    check(model.currentRowId() == shelf, "and it is the keys' row");
    model.activate(sideL, true, false);
    check(calls.selects.back() == std::vector<int>({shelf, sideL}), "Ctrl adds - " + ids(calls.selects.back()));
    model.activate(shelf, true, false);
    check(calls.selects.back() == std::vector<int>({sideL}), "Ctrl again takes it out");
    model.activate(sideL, false, false);
    model.activate(back, false, true);
    // Visible order: Carcass, Side left, Side right, Drawer (folded, Front
    // inside), Outline, Shelf, Back.
    check(calls.selects.back() == std::vector<int>({sideL, sideR, shelf, back}),
          "Shift takes the VISIBLE body rows between the anchor and the click, never Front in folded "
          "Drawer - " + ids(calls.selects.back()));
    model.activate(carcass, false, false);
    check(calls.selects.back() == doc.bodiesUnderGroup(carcass), "a folder click selects every body under it");
    in.selected = {shelf};
    model.setState(in);
    model.activate(carcass, true, false);
    {
        std::vector<int> want = {shelf};
        for (int b : doc.bodiesUnderGroup(carcass)) want.push_back(b);
        check(calls.selects.back() == want, "Ctrl on a folder adds its bodies - " + ids(calls.selects.back()));
    }
    const std::size_t selectsBefore = calls.selects.size();
    model.activate(outline, false, false);
    check(calls.outlines.size() == 1 && calls.outlines.back() == outline && calls.selects.size() == selectsBefore,
          "an outline row chooses the outline, and selects no body");

    // --- the filter ------------------------------------------------------------
    calls.withdrawals = 0;
    model.toggleFold(carcass);   // fold Carcass again: the user's own fold
    check(!model.isFolderOpen(carcass), "Carcass folded by hand");
    model.setFilter(QStringLiteral("SIDE"));
    check(rowNames(model) == QStringList({QStringLiteral("Carcass"), QStringLiteral("Side left"),
                                           QStringLiteral("Side right")}),
          "the filter is case-blind and keeps the folders above a match - " + rowNames(model).join(", "));
    {
        const QVariantMap c = rowOf(model, carcass);
        check(c.value(QStringLiteral("muted")).toBool() && c.value(QStringLiteral("held")).toBool() &&
                  c.value(QStringLiteral("open")).toBool(),
              "a folder kept for a match is muted and held open");
        check(c.value(QStringLiteral("twistyTip")).toString() ==
                  QStringLiteral("Shown open by the filter — clear the filter to fold it"),
              "and its twisty says why it is inert (R7a)");
        const QVariantList match = rowOf(model, sideL).value(QStringLiteral("match")).toList();
        check(match.size() == 2 && match[0].toInt() == 0 && match[1].toInt() == 4,
              "a match carries its range, never markup");
    }
    check(!model.isFolderOpen(carcass), "holding a folder open writes no fold");
    check(calls.withdrawals == 1, "a filter change withdraws a standing move refusal");
    model.toggleFold(carcass);
    check(!model.isFolderOpen(carcass), "the twisty of a held folder does nothing");
    model.setFilter(QStringLiteral("(l"));
    check(model.rows().isEmpty() && model.state().value(QStringLiteral("noMatch")).toString() ==
                                         QStringLiteral("Nothing matches \"(l\""),
          "a query is literal, and matching nothing says so");
    model.clearFilter();
    check(rowOf(model, sideL).isEmpty() && rowOf(model, carcass).value(QStringLiteral("open")).toBool() == false,
          "an emptied filter puts the user's folds back");

    // --- reveal ----------------------------------------------------------------
    // A click on empty space first: a reveal is for a body NEW to the selection.
    model.showSelection({});
    model.showSelection({front});
    check(model.isFolderOpen(carcass) && model.isFolderOpen(drawer),
          "a viewport pick opens every folder above its row, and they stay open");
    check(model.currentRowId() == front && model.state().value(QStringLiteral("scroll")).toMap().value(
                                               QStringLiteral("id")).toInt() == front,
          "and the keys' row moves there, scrolled to");
    model.setFilter(QStringLiteral("shelf"));
    model.showSelection({front, sideR});
    check(model.state().value(QStringLiteral("foot")).toString() ==
              QStringLiteral("Side right is hidden by the filter —"),
          "a pick the filter hides says so at the foot - \"" + model.state().value("foot").toString() + "\"");
    model.clearFilter();
    check(model.state().value(QStringLiteral("foot")).toString().isEmpty(), "clearing the filter retires the foot");

    // --- eyes and grain --------------------------------------------------------
    model.toggleEye(sideL);
    check(calls.visibles.size() == 1 && calls.visibles.back().ids == std::vector<int>{sideL} &&
              !calls.visibles.back().show && !calls.visibles.back().outline,
          "a body's eye asks to hide it");
    model.toggleEye(carcass);
    check(calls.visibles.back().ids == doc.bodiesUnderGroup(carcass) && !calls.visibles.back().show,
          "a folder's eye speaks for every body under it");
    doc.setVisible(sideL, false);
    model.setState(in);
    check(!rowOf(model, carcass).value(QStringLiteral("eyeOn")).toBool() &&
              rowOf(model, carcass).value(QStringLiteral("accessibleName")).toString().contains(
                  QStringLiteral(", partly hidden")),
          "a folder with something hidden is not \"on\", and is partly hidden");
    model.toggleEye(carcass);
    check(calls.visibles.back().show, "and its eye then shows everything");
    model.toggleEye(outline);
    check(calls.visibles.back().outline && calls.visibles.back().ids == std::vector<int>{outline},
          "an outline's eye is the outline's");
    doc.setVisible(sideL, true);
    model.toggleGrain(shelf);
    check(calls.grains == std::vector<int>{shelf}, "the grain mark asks for its body");

    // --- rename ----------------------------------------------------------------
    model.setState(in);
    model.beginRenameFor(sideL);
    check(model.state().value(QStringLiteral("rename")).toMap().value(QStringLiteral("id")).toInt() == sideL &&
              model.state().value(QStringLiteral("rename")).toMap().value(QStringLiteral("name")).toString() ==
                  QStringLiteral("Side left"),
          "a rename opens over the row with its plain name");
    model.commitRename(sideL, QStringLiteral("   "));
    check(calls.renames.empty(), "a blank name renames nothing (InlineRename's rule)");
    model.beginRenameFor(sideL);
    model.commitRename(sideL, QStringLiteral("  Left side  "));
    check(calls.renames.size() == 1 && calls.renames.back().name == QStringLiteral("Left side") &&
              !calls.renames.back().outline,
          "the name is trimmed and reaches the item hook");
    check(model.state().value(QStringLiteral("rename")).toMap().value(QStringLiteral("id")).toInt() == 0,
          "and the field closes");
    model.beginRenameFor(drawer);
    model.commitRename(drawer, QStringLiteral("Drawers"));
    check(calls.folderRenames.size() == 1 && calls.folderRenames.back().first == drawer,
          "a folder renames through its own hook");
    model.toggleFold(carcass);   // fold: Side left is no longer a row
    model.beginRenameFor(sideL);
    check(model.state().value(QStringLiteral("rename")).toMap().value(QStringLiteral("id")).toInt() == 0,
          "a row inside a folded folder cannot be renamed blind");
    model.toggleFold(carcass);

    // --- the row menu ----------------------------------------------------------
    in.selected = {shelf};
    in.newFolderEnabled = true;
    model.setState(in);
    model.openMenu(shelf);
    check(model.menu().value(QStringLiteral("open")).toBool(), "a right press opens the menu");
    check(entryOf(model, QStringLiteral("folder")).value(QStringLiteral("label")).toString() ==
                  QStringLiteral("New folder with these") &&
              entryOf(model, QStringLiteral("folder")).value(QStringLiteral("enabled")).toBool(),
          "over a body: New folder with these, enabled as the action is");
    {
        const QVariantList into = entryOf(model, QStringLiteral("into")).value(QStringLiteral("children")).toList();
        QStringList names;
        for (const QVariant& c : into) names << c.toMap().value(QStringLiteral("label")).toString();
        check(names == QStringList({QStringLiteral("Carcass"), QStringLiteral("Drawer")}),
              "Move into lists every folder that can take it - " + names.join(", "));
    }
    check(entryOf(model, QStringLiteral("ungroup")).value(QStringLiteral("enabled")).toBool() == false,
          "a loose body has no folder to remove");
    check(entryOf(model, QStringLiteral("delete")).isEmpty(), "Delete folder is a folder row's alone");
    model.chooseMenu(QStringLiteral("folder"));
    check(calls.newFolders == 1 && !model.menu().value(QStringLiteral("open")).toBool(),
          "choosing an entry acts and closes the menu");
    model.openMenu(outline);
    check(!entryOf(model, QStringLiteral("folder")).value(QStringLiteral("enabled")).toBool() &&
              !entryOf(model, QStringLiteral("ungroup")).value(QStringLiteral("enabled")).toBool(),
          "over an outline: no New folder, no Remove folder (R12c)");
    model.closeMenu();
    model.openMenu(drawer);
    check(entryOf(model, QStringLiteral("delete")).value(QStringLiteral("label")).toString() ==
              QStringLiteral("Delete folder and its contents"),
          "over a folder: Delete folder and its contents");
    model.chooseMenu(QStringLiteral("ungroup"));
    check(calls.removes == std::vector<int>{drawer}, "Remove folder carries the right-clicked folder (RF1)");
    model.openMenu(drawer);
    model.chooseMenu(QStringLiteral("folder"));
    check(calls.wraps == std::vector<int>{drawer}, "New folder with these over a folder wraps it (RF2)");
    model.setFilter(QStringLiteral("side"));
    model.openMenu(sideL);
    check(!entryOf(model, QStringLiteral("up")).value(QStringLiteral("enabled")).toBool() &&
              entryOf(model, QStringLiteral("up")).value(QStringLiteral("tip")).toString() ==
                  ItemMoveText::filteredReorderRefusal(),
          "while filtered, Move up is disabled with the RF3 reason");
    model.chooseMenu(QStringLiteral("up"));
    check(calls.moves.empty(), "and choosing it anyway moves nothing");
    model.closeMenu();
    model.clearFilter();

    // --- the keys --------------------------------------------------------------
    calls.selects.clear();
    model.activate(shelf, false, false);
    model.key(QStringLiteral("step"), true);
    check(calls.selects.back() == std::vector<int>{back}, "Down selects the next row, as a click does");
    model.key(QStringLiteral("step"), false);
    check(calls.selects.back() == std::vector<int>{shelf}, "Up the one before");
    model.key(QStringLiteral("extendStep"), true);
    check(calls.selects.back() == std::vector<int>({shelf, back}), "Shift+Down extends");
    model.activate(sideL, false, false);
    model.key(QStringLiteral("foldOrParent"), false);
    check(model.currentRowId() == carcass, "Left on a row in a folder goes to the folder");
    model.key(QStringLiteral("foldOrParent"), false);
    check(!model.isFolderOpen(carcass), "Left on an open folder folds it");
    model.key(QStringLiteral("unfoldOrChild"), true);
    check(model.isFolderOpen(carcass), "Right on a folded folder opens it");
    model.key(QStringLiteral("unfoldOrChild"), true);
    check(model.currentRowId() == doc.orderedChildren(carcass).front(),
          "Right on an open folder goes to its first row");
    model.activate(shelf, false, false);
    calls.moves.clear();
    model.key(QStringLiteral("moveDown"), true);
    {
        const DocumentModel::MoveTarget t = doc.siblingStepTarget({shelf}, +1);
        check(calls.moves.size() == 1 && calls.moves.back().parent == t.parent && calls.moves.back().before == t.beforeId,
              "Alt+Down asks for DocumentModel's sibling step");
    }
    model.setFilter(QStringLiteral("e"));
    model.activate(shelf, false, false);
    calls.refusals.clear();
    calls.moves.clear();
    model.key(QStringLiteral("moveUp"), false);
    check(calls.moves.empty() && calls.refusals == QStringList{ItemMoveText::filteredReorderRefusal()},
          "Alt+Up while filtered refuses with the RF3 sentence (Review Focus 4)");
    model.key(QStringLiteral("enterList"), true);
    check(model.currentRowId() != carcass && !rowOf(model, model.currentRowId()).value("muted").toBool(),
          "Enter from the filter lands on the first MATCHING row, never a muted folder (DA2)");
    model.clearFilter();
    model.activate(shelf, false, false);
    calls.refusals.clear();
    model.key(QStringLiteral("moveIntoFolderAbove"), true);
    const int aboveShelf = [&] {
        const QVariantList rows = model.rows();
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].toMap().value("id").toInt() == shelf && i > 0) return rows[i - 1].toMap().value("id").toInt();
        return 0;
    }();
    if (doc.groupExists(aboveShelf))
        check(!calls.moves.empty() && calls.moves.back().parent == aboveShelf,
              "Alt+Right moves into the folder above");
    else
        check(calls.refusals == QStringList{ItemMoveText::noFolderAboveRefusal()},
              "Alt+Right with no folder above refuses quietly - " + calls.refusals.join("|"));
    model.beginRenameFor(0);
    model.activate(shelf, false, false);
    model.key(QStringLiteral("rename"), true);
    check(model.state().value(QStringLiteral("rename")).toMap().value(QStringLiteral("id")).toInt() == shelf,
          "F2 opens the rename over the keys' row");
    model.cancelRename();

    // --- the drag --------------------------------------------------------------
    in.selected = {sideL, sideR};
    model.setState(in);
    calls.moves.clear();
    calls.dragLines = 0;
    model.beginDrag(sideL, true);
    check(model.drag().value(QStringLiteral("active")).toBool() &&
              rowOf(model, sideL).value(QStringLiteral("carried")).toBool() &&
              rowOf(model, sideR).value(QStringLiteral("carried")).toBool() &&
              !rowOf(model, shelf).value(QStringLiteral("carried")).toBool(),
          "a press on a selected body of several carries the selection");
    check(model.drag().value(QStringLiteral("ghost")).toMap().value(QStringLiteral("more")).toInt() == 1,
          "the ghost names one and counts the rest");
    model.dragOver(QStringLiteral("into"), drawer, DocumentModel::kIntoFolder);
    {
        const DocumentModel::MoveResult dry = doc.checkMove({sideL, sideR}, drawer, DocumentModel::kIntoFolder);
        check(!model.drag().value(QStringLiteral("refused")).toBool() &&
                  model.dragStatusText() == ItemMoveText::dragStatus(doc, dry),
              "over a folder: the dry run's own words - \"" + model.dragStatusText() + "\"");
    }
    check(calls.dragLines > 0, "the status line is announced");
    model.dragOver(QStringLiteral("outside"), 0, 0);
    check(model.dragStatusText() == ItemMoveText::releaseToCancel(), "outside the card: release cancels");
    model.endDrag(true);
    check(calls.moves.empty() && !model.drag().value(QStringLiteral("active")).toBool() &&
              model.dragStatusText().isEmpty(),
          "a release outside moves nothing and puts everything back");
    model.beginDrag(carcass, true);
    model.dragOver(QStringLiteral("into"), drawer, DocumentModel::kIntoFolder);
    check(model.drag().value(QStringLiteral("refused")).toBool(), "a folder into its own subfolder is refused");
    model.endDrag(false);
    model.beginDrag(shelf, true);
    model.dragOver(QStringLiteral("into"), carcass, DocumentModel::kIntoFolder);
    model.endDrag(true);
    check(calls.moves.size() == 1 && calls.moves.back().ids == std::vector<int>{shelf} &&
              calls.moves.back().parent == carcass,
          "a press on an unselected row carries it alone, and a release over a target moves it once");

    // --- a hidden drawer -------------------------------------------------------
    in.shown = false;
    model.setState(in);
    const int before = changes;
    doc.setItemName(back, "Back panel");
    model.setState(in);
    check(changes == before, "a hidden drawer builds nothing");
    model.setFolderOpen(carcass, false);
    model.setFolderOpen(drawer, false);
    model.showSelection({front});
    check(model.isFolderOpen(carcass) && model.isFolderOpen(drawer),
          "but a pick still opens the folders above its row (R7)");
    in.shown = true;
    model.setState(in);
    check(rowNames(model).contains(QStringLiteral("Back panel")), "showing it reads the document again");
    const int settled = changes;
    model.setState(in);
    check(changes == settled, "an identical state changes nothing");

    // --- words -----------------------------------------------------------------
    {
        const QVariantMap w = model.words();
        check(w.value(QStringLiteral("title")).toString() == QStringLiteral("Items") &&
                  w.value(QStringLiteral("filter")).toString() == QStringLiteral("Filter by name") &&
                  w.value(QStringLiteral("clearFilter")).toString() == QStringLiteral("Clear filter"),
              "the title, the filter's placeholder and Clear filter are the Qt drawer's");
        check(w.value(QStringLiteral("empty")).toString().startsWith(
                  QStringLiteral("Nothing to list yet — bodies, outlines and folders you make show up here")),
              "the empty state too");
        check(model.keys().size() == static_cast<int>(ItemsKeys::table().size()),
              "the key table crosses whole");
    }
    for (const QString& text : model.paintedTexts())
        if (text.isEmpty()) check(false, "an empty painted text");

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
