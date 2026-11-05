#include "ItemsModel.h"

#include "ItemMoveText.h"

#include <QCoreApplication>
#include <QKeySequence>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace {
// The Qt drawer's own words, under its tr context, so a translation of one is
// a translation of the other.
QString tr(const char* text) { return QCoreApplication::translate("ItemsPanel", text); }

QString emptyText()
{
    return tr("Nothing to list yet — bodies, outlines and folders you make show up "
              "here\n\nPress Ctrl+K and click points on the ground to draw your "
              "first outline.");
}
QString waitingToExtrude() { return tr(", waiting to extrude"); }
QString filterHeldTip() { return tr("Shown open by the filter — clear the filter to fold it"); }
QString partialCountWords(int chosen, int total, const QString& folder)
{
    return chosen == 1 ? tr("1 of %1 bodies in %2 is selected").arg(total).arg(folder)
                       : tr("%1 of %2 bodies in %3 are selected").arg(chosen).arg(total).arg(folder);
}
QString kindName(ItemsModel::Kind kind)
{
    switch (kind) {
    case ItemsModel::Kind::Body: return QStringLiteral("body");
    case ItemsModel::Kind::Outline: return QStringLiteral("outline");
    case ItemsModel::Kind::Folder: return QStringLiteral("folder");
    }
    return QString();
}
QVariantMap targetMap(int parent, int before, int depth)
{
    return {{QStringLiteral("parent"), parent}, {QStringLiteral("before"), before}, {QStringLiteral("depth"), depth}};
}
// A key as the page reads KeyboardEvent.key.
QString pageKeyName(Qt::Key key)
{
    switch (key) {
    case Qt::Key_Up: return QStringLiteral("ArrowUp");
    case Qt::Key_Down: return QStringLiteral("ArrowDown");
    case Qt::Key_Left: return QStringLiteral("ArrowLeft");
    case Qt::Key_Right: return QStringLiteral("ArrowRight");
    case Qt::Key_Home: return QStringLiteral("Home");
    case Qt::Key_End: return QStringLiteral("End");
    case Qt::Key_F2: return QStringLiteral("F2");
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("Enter");
    default: return QKeySequence(key).toString();
    }
}
}  // namespace

ItemsModel::ItemsModel(Hooks hooks, QObject* parent) : QObject(parent), myHooks(std::move(hooks)) {}

QVariantMap ItemsModel::words() const
{
    return {
        {QStringLiteral("title"), tr("Items")},
        {QStringLiteral("filter"), QCoreApplication::translate("ItemsFilterField", "Filter by name")},
        {QStringLiteral("clearFilter"), tr("Clear filter")},
        {QStringLiteral("empty"), emptyText()},
        {QStringLiteral("eyeBody"), tr("Show or hide this body")},
        {QStringLiteral("eyeOutline"), tr("Show or hide this outline")},
        {QStringLiteral("eyeFolder"), tr("Show or hide everything in this folder")},
        //: The two columns at a row's end - the grain mark and the eye.
        {QStringLiteral("headName"), tr("Name")},
        {QStringLiteral("headTools"), tr("Grain · Eye")},
        {QStringLiteral("ledgerKeys"), tr("Alt+↑↓ moves")},
        // The drag ghost's words (DragGhost): "Shelf +2", and its cancel line.
        {QStringLiteral("ghostMore"), QCoreApplication::translate("DragGhost", "%1 +%2")},
        {QStringLiteral("releaseToCancel"), ItemMoveText::releaseToCancel()},
        {QStringLiteral("dragThreshold"), 6},
        {QStringLiteral("springMs"), 700},
    };
}

QVariantList ItemsModel::keys() const
{
    QVariantList list;
    for (const ItemsKeys::Key& entry : ItemsKeys::table()) {
        QVariantList combos;
        for (const QKeyCombination& k : entry.keys) {
            const Qt::KeyboardModifiers mods = k.keyboardModifiers();
            combos.push_back(QVariantMap{{QStringLiteral("key"), pageKeyName(k.key())},
                                         {QStringLiteral("shift"), bool(mods & Qt::ShiftModifier)},
                                         {QStringLiteral("alt"), bool(mods & Qt::AltModifier)},
                                         {QStringLiteral("ctrl"), bool(mods & Qt::ControlModifier)}});
        }
        list.push_back(QVariantMap{{QStringLiteral("command"), ItemsKeys::commandName(entry.command)},
                                   {QStringLiteral("fromFilter"), entry.fromFilterField},
                                   {QStringLiteral("keys"), combos}});
    }
    return list;
}

QStringList ItemsModel::paintedTexts() const
{
    QStringList texts;
    const QVariantMap w = words();
    for (auto it = w.cbegin(); it != w.cend(); ++it)
        if (it.value().typeId() == QMetaType::QString) texts << it.value().toString();
    texts << waitingToExtrude() << filterHeldTip() << tr("Nothing matches \"%1\"")
          << tr("%1 is hidden by the filter —") << tr(", in %1") << tr(", hidden") << tr(", partly hidden")
          << tr("%1, folder") << tr("Hide %1") << tr("Show %1") << tr("Grain along") << tr("Grain across")
          << tr("The grain runs along this body — click to turn it")
          << tr("The grain runs across this body — click to turn it back") << tr("Hide everything in %1")
          << tr("Show everything in %1") << tr("Open %1") << tr("Close %1") << tr("Close this folder")
          << tr("Open this folder") << tr("1 of %1 bodies in %2 is selected")
          << tr("%1 of %2 bodies in %3 are selected") << tr("New folder with these") << tr("Rename")
          << tr("Move up") << tr("Move down") << tr("Remove folder, keep contents")
          << tr("Delete folder and its contents") << tr("Move into") << tr("No folders yet")
          << tr("No other folder can take this") << tr("1 body") << tr("%1 bodies") << tr("%1 selected");
    texts << ItemMoveText::paintedTexts();
    return texts;
}

// --- state in ------------------------------------------------------------------

void ItemsModel::setState(Inputs inputs)
{
    myIn = std::move(inputs);
    // A drop deletes nothing it was carrying: a selection change mid-drag is
    // not a reason to drop the rows from under the hand (the Qt drawer holds
    // its rows still until the drag ends - finishDrag()).
    if (!myIn.shown || myDrag.active) {
        myStale = true;
        if (!myIn.shown) {
            // A drawer going away takes a live drag and an open menu with it.
            if (myDrag.active) endDrag(false);
            closeMenu();
            // A rename the drawer goes away under is over - its field went
            // with the card, and "one rename at a time" would refuse every
            // later one (review I3).
            myRenameId = 0;
        }
        return;
    }
    myStale = false;
    rebuild();
}

void ItemsModel::showSelection(const std::vector<int>& ids)
{
    // The newest body in the selection that was not there before is the one to
    // reveal, unless this drawer asked for the selection itself.
    int revealId = 0;
    if (!myRequesting)
        for (int id : ids)
            if (std::find(myPickBaseline.begin(), myPickBaseline.end(), id) == myPickBaseline.end()) revealId = id;
    myPickBaseline = ids;
    myIn.selected = ids;
    // A standing hidden-pick notice names a body that must still be selected.
    if (myHiddenPickId != 0 && std::find(ids.begin(), ids.end(), myHiddenPickId) == ids.end()) myHiddenPickId = 0;
    if (revealId != 0) reveal(revealId);
    if (myIn.shown && !myDrag.active) rebuild();
}

void ItemsModel::forgetFurniture()
{
    if (!myFilter.isEmpty()) setFilter(QString());
    // The open folders were the furniture that just closed (T-064): folder
    // ids restart with every document, so a kept fold would open whatever
    // folder the next one numbers the same.
    if (!myExpanded.empty()) {
        myExpanded.clear();
        if (myIn.shown) rebuild();
    }
}

void ItemsModel::resetPick()
{
    myPickBaseline.clear();
    myCurrentId = 0;
    myCurrentKind = Kind::Body;
    myHiddenPickId = 0;
    myRenameId = 0;
    // Through closeMenu(), which tells the page (review C1): a menu closed
    // only here stayed drawn, its layer taking every press.
    closeMenu();
    if (myIn.shown) rebuild();
}

// --- the tree ------------------------------------------------------------------

void ItemsModel::buildTree(int parent, int depth)
{
    const DocumentModel* doc = myIn.document;
    for (int id : doc->orderedChildren(parent)) {
        Row row;
        row.id = id;
        row.depth = depth;
        row.parent = parent;
        if (doc->groupExists(id)) {
            row.kind = Kind::Folder;
            row.name = QString::fromStdString(doc->groupNameOf(id));
            // The folder's eye READS the bodies under it: on only while every
            // one of them is on. An empty folder hides nothing.
            const std::vector<int> bodies = doc->bodiesUnderGroup(id);
            row.anyVisible = bodies.empty();
            for (int body : bodies) {
                if (doc->isVisible(body)) row.anyVisible = true;
                else row.visible = false;
            }
            myAll.push_back(row);
            buildTree(id, depth + 1);
        } else if (doc->containsOutline(id)) {
            row.kind = Kind::Outline;
            row.name = QString::fromStdString(doc->outlineNameOf(id));
            row.visible = row.anyVisible = doc->isVisible(id);
            row.onScreen = row.visible;
            myAll.push_back(row);
        } else if (doc->contains(id)) {
            row.kind = Kind::Body;
            row.name = QString::fromStdString(doc->nameOf(id));
            row.visible = row.anyVisible = doc->isVisible(id);
            row.onScreen = row.visible && (!myIn.onScreen || myIn.onScreen(id));
            row.across = doc->bodyGrainAcross(id);
            myAll.push_back(row);
        }
    }
}

void ItemsModel::applyVisibility()
{
    myVisible.clear();
    const bool filter = filtering();
    std::unordered_set<int> keep;
    if (filter) {
        for (Row& row : myAll) {
            row.matched = row.name.contains(myFilter, Qt::CaseInsensitive);
            if (!row.matched) continue;
            for (int folder = row.parent; folder > 0; folder = myIn.document->groupOf(folder)) keep.insert(folder);
        }
    }
    std::unordered_map<int, bool> openFolder;
    for (std::size_t i = 0; i < myAll.size(); ++i) {
        Row& row = myAll[i];
        if (filter) {
            const bool kept = row.kind == Kind::Folder && keep.count(row.id) > 0;
            row.shown = row.matched || kept;
            row.muted = row.shown && !row.matched;
            row.open = kept;
            row.held = row.kind == Kind::Folder;
        } else {
            row.matched = false;
            row.muted = false;
            row.held = false;
            row.shown = row.parent == 0 || openFolder[row.parent];
            if (row.kind == Kind::Folder) {
                row.open = isFolderOpen(row.id);
                openFolder[row.id] = row.shown && row.open;
            }
        }
        if (row.shown) myVisible.push_back(i);
    }
}

void ItemsModel::rebuild()
{
    myAll.clear();
    myVisible.clear();
    if (myIn.live && myIn.document) {
        buildTree(0, 0);
        // Folds of folders that are gone are forgotten (ids restart per
        // furniture, so a stale one would open a stranger's folder).
        for (auto it = myExpanded.begin(); it != myExpanded.end();)
            it = myIn.document->groupExists(*it) ? std::next(it) : myExpanded.erase(it);
        applyVisibility();
    }
    // A rename over a row that is no longer on screen is closed - a field over
    // nothing could never commit.
    if (myRenameId != 0 && visibleIndexOf(myRenameId) < 0) myRenameId = 0;
    publish();
}

bool ItemsModel::selected(int id) const
{
    return std::find(myIn.selected.begin(), myIn.selected.end(), id) != myIn.selected.end();
}

bool ItemsModel::carried(const Row& row) const
{
    if (!myDrag.active || !myIn.document) return false;
    for (int id : myDrag.ids) {
        if (id == row.id) return true;
        if (row.parent != 0 && myIn.document->groupExists(id) && myIn.document->groupContains(id, row.parent))
            return true;
    }
    return false;
}

QString ItemsModel::accessibleName(const Row& row, const QString& partialWords, bool pending) const
{
    const QString folder = row.parent > 0 ? QString::fromStdString(myIn.document->groupNameOf(row.parent)) : QString();
    const QString visibility = row.visible ? QString() : row.anyVisible ? tr(", partly hidden") : tr(", hidden");
    QString text = row.kind == Kind::Folder ? tr("%1, folder").arg(row.name) : row.name;
    if (!folder.isEmpty()) text += tr(", in %1").arg(folder);
    text += visibility;
    if (pending) text += waitingToExtrude();
    if (!partialWords.isEmpty()) text += QStringLiteral(", ") + partialWords;
    return text;
}

QVariantMap ItemsModel::rowMap(const Row& row) const
{
    const DocumentModel* doc = myIn.document;
    QVariantMap m;
    m.insert(QStringLiteral("id"), row.id);
    m.insert(QStringLiteral("kind"), kindName(row.kind));
    m.insert(QStringLiteral("name"), row.name);
    m.insert(QStringLiteral("depth"), row.depth);
    m.insert(QStringLiteral("parent"), row.parent);
    m.insert(QStringLiteral("eyeOn"), row.visible);
    m.insert(QStringLiteral("onScreen"), row.onScreen);
    m.insert(QStringLiteral("muted"), row.muted);
    m.insert(QStringLiteral("current"), row.id == myCurrentId);
    m.insert(QStringLiteral("carried"), carried(row));
    m.insert(QStringLiteral("open"), row.open);
    m.insert(QStringLiteral("held"), row.held);
    // The filter's highlight as a RANGE (ruling R6 of the plan): the page builds
    // text nodes, so a name is never markup.
    if (row.matched && filtering()) {
        const int at = row.name.indexOf(myFilter, 0, Qt::CaseInsensitive);
        m.insert(QStringLiteral("match"), QVariantList{at, static_cast<int>(myFilter.size())});
    } else {
        m.insert(QStringLiteral("match"), QVariant());
    }
    bool marked = false;
    bool pending = false;
    QString partial;
    QString partialWords;
    if (row.kind == Kind::Folder) {
        const std::vector<int> under = doc->bodiesUnderGroup(row.id);
        int chosen = 0;
        for (int id : under) chosen += selected(id) ? 1 : 0;
        marked = !under.empty() && chosen == static_cast<int>(under.size());
        if (chosen > 0 && !marked) {
            partial = tr("%1/%2").arg(chosen).arg(static_cast<int>(under.size()));
            partialWords = partialCountWords(chosen, static_cast<int>(under.size()), row.name);
        }
        m.insert(QStringLiteral("eyeName"), row.visible ? tr("Hide everything in %1").arg(row.name)
                                                        : tr("Show everything in %1").arg(row.name));
        m.insert(QStringLiteral("eyeTip"), tr("Show or hide everything in this folder"));
        m.insert(QStringLiteral("twistyName"), row.open ? tr("Close %1").arg(row.name) : tr("Open %1").arg(row.name));
        m.insert(QStringLiteral("twistyTip"),
                 row.held ? filterHeldTip() : (row.open ? tr("Close this folder") : tr("Open this folder")));
    } else if (row.kind == Kind::Outline) {
        // THE PENDING OUTLINE IS NOT MARKED (ruling D4): its name says it.
        pending = myIn.pendingOutline != 0 && row.id == myIn.pendingOutline;
        m.insert(QStringLiteral("eyeName"), row.visible ? tr("Hide %1").arg(row.name) : tr("Show %1").arg(row.name));
        m.insert(QStringLiteral("eyeTip"), tr("Show or hide this outline"));
    } else {
        marked = selected(row.id);
        m.insert(QStringLiteral("eyeName"), row.visible ? tr("Hide %1").arg(row.name) : tr("Show %1").arg(row.name));
        m.insert(QStringLiteral("eyeTip"), tr("Show or hide this body"));
    }
    m.insert(QStringLiteral("marked"), marked);
    m.insert(QStringLiteral("pending"), pending);
    m.insert(QStringLiteral("partial"), partial);
    m.insert(QStringLiteral("partialWords"), partialWords);
    m.insert(QStringLiteral("tip"), pending ? row.name + waitingToExtrude() : QString());
    m.insert(QStringLiteral("accessibleName"), accessibleName(row, partialWords, pending));
    if (row.kind == Kind::Body) {
        m.insert(QStringLiteral("grain"), row.across ? QStringLiteral("across") : QStringLiteral("along"));
        m.insert(QStringLiteral("grainTip"), row.across ? tr("The grain runs across this body — click to turn it back")
                                                        : tr("The grain runs along this body — click to turn it"));
        m.insert(QStringLiteral("grainName"), row.across ? tr("Grain across") : tr("Grain along"));
    } else {
        m.insert(QStringLiteral("grain"), QString());
    }
    // The drop targets (ruling R4): what a line above or below this row means,
    // worked out here so the page's zone geometry never asks which sibling
    // follows.
    m.insert(QStringLiteral("above"), targetMap(row.parent, row.id, row.depth));
    {
        int parent = row.parent;
        int before = 0;
        int depth = row.depth;
        bool done = false;
        if (row.kind == Kind::Folder && row.open && !filtering()) {
            const std::vector<int> children = doc->orderedChildren(row.id);
            if (!children.empty()) {
                parent = row.id;
                before = children.front();
                depth = row.depth + 1;
                done = true;
            }
        }
        if (!done) {
            const std::vector<int> siblings = doc->orderedChildren(row.parent);
            const auto at = std::find(siblings.begin(), siblings.end(), row.id);
            before = (at != siblings.end() && at + 1 != siblings.end()) ? *(at + 1) : 0;
        }
        m.insert(QStringLiteral("below"), targetMap(parent, before, depth));
    }
    return m;
}

void ItemsModel::publish()
{
    QVariantList rows;
    for (std::size_t i : myVisible) rows.push_back(rowMap(myAll[i]));

    QVariantMap state;
    state.insert(QStringLiteral("live"), myIn.live);
    state.insert(QStringLiteral("filter"), myFilter);
    state.insert(QStringLiteral("empty"), myIn.live && myAll.empty());
    state.insert(QStringLiteral("noMatch"), filtering() && myVisible.empty() && !myAll.empty()
                                                ? tr("Nothing matches \"%1\"").arg(myFilter)
                                                : QString());
    state.insert(QStringLiteral("foot"), myHiddenPickId != 0 && myIn.document && myIn.document->contains(myHiddenPickId)
                                             ? tr("%1 is hidden by the filter —")
                                                   .arg(QString::fromStdString(myIn.document->nameOf(myHiddenPickId)))
                                             : QString());
    state.insert(QStringLiteral("current"), myCurrentId);
    state.insert(QStringLiteral("newFolder"), QVariantMap{{QStringLiteral("enabled"), myIn.newFolderEnabled},
                                                          {QStringLiteral("tip"), myIn.newFolderTip}});
    const int bodies = myIn.document ? static_cast<int>(myIn.document->solids().size()) : 0;
    state.insert(QStringLiteral("ledger"),
                 QVariantMap{{QStringLiteral("bodies"), bodies == 1 ? tr("1 body") : tr("%1 bodies").arg(bodies)},
                             {QStringLiteral("selected"), tr("%1 selected").arg(myIn.selected.size())}});
    state.insert(QStringLiteral("scroll"),
                 QVariantMap{{QStringLiteral("id"), myScrollId}, {QStringLiteral("seq"), myScrollSeq}});
    {
        const Row* r = myRenameId ? rowById(myRenameId) : nullptr;
        state.insert(QStringLiteral("rename"), QVariantMap{{QStringLiteral("id"), r ? myRenameId : 0},
                                                           {QStringLiteral("name"), r ? r->name : QString()},
                                                           {QStringLiteral("seq"), myRenameSeq}});
    }

    const bool moved = rows != myRowMaps || state != myStateMap;
    myRowMaps = rows;
    myStateMap = state;
    if (moved) emit changed();
}

QVariantMap ItemsModel::drag() const
{
    return {{QStringLiteral("active"), myDrag.active},
            {QStringLiteral("refused"), myDrag.refused},
            {QStringLiteral("status"), myDrag.status},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("kind"), myDrag.target.kind},
                                                   {QStringLiteral("parent"), myDrag.target.parent},
                                                   {QStringLiteral("before"), myDrag.target.beforeId}}},
            {QStringLiteral("ghost"), QVariantMap{{QStringLiteral("kind"), myDrag.ghostKind},
                                                  {QStringLiteral("name"), myDrag.ghostName},
                                                  {QStringLiteral("more"), myDrag.more}}}};
}

const ItemsModel::Row* ItemsModel::rowById(int id) const
{
    for (const Row& row : myAll)
        if (row.id == id) return &row;
    return nullptr;
}

int ItemsModel::visibleIndexOf(int id) const
{
    if (id == 0) return -1;
    for (std::size_t v = 0; v < myVisible.size(); ++v)
        if (myAll[myVisible[v]].id == id) return static_cast<int>(v);
    return -1;
}

const ItemsModel::Row* ItemsModel::visibleRow(int index) const
{
    if (index < 0 || index >= static_cast<int>(myVisible.size())) return nullptr;
    return &myAll[myVisible[static_cast<std::size_t>(index)]];
}

ItemsModel::Kind ItemsModel::kindOf(int id) const
{
    if (const Row* row = rowById(id)) return row->kind;
    if (myIn.document && myIn.document->groupExists(id)) return Kind::Folder;
    if (myIn.document && myIn.document->containsOutline(id)) return Kind::Outline;
    return Kind::Body;
}

// --- clicks --------------------------------------------------------------------

std::vector<int> ItemsModel::selectionFor(int id, bool ctrl, bool shift) const
{
    if (ctrl) {
        std::vector<int> ids = myIn.selected;
        const auto at = std::find(ids.begin(), ids.end(), id);
        if (at != ids.end()) ids.erase(at);
        else ids.push_back(id);
        return ids;
    }
    // Shift takes every BODY row between the anchor and this one, in the order
    // the list SHOWS - a folded folder's bodies are not swept.
    if (shift && myAnchorId != 0) {
        int from = -1;
        int to = -1;
        for (std::size_t v = 0; v < myVisible.size(); ++v) {
            const Row& row = myAll[myVisible[v]];
            if (row.kind != Kind::Body) continue;
            if (row.id == myAnchorId) from = static_cast<int>(v);
            if (row.id == id) to = static_cast<int>(v);
        }
        if (from >= 0 && to >= 0) {
            if (from > to) std::swap(from, to);
            std::vector<int> ids;
            for (int v = from; v <= to; ++v) {
                const Row& row = myAll[myVisible[static_cast<std::size_t>(v)]];
                if (row.kind == Kind::Body) ids.push_back(row.id);
            }
            return ids;
        }
    }
    return {id};
}

void ItemsModel::request(const std::vector<int>& ids)
{
    // Not a pick from elsewhere: nothing reveals under the hand that clicked.
    const bool was = myRequesting;
    myRequesting = true;
    if (myHooks.select) myHooks.select(ids);
    myRequesting = was;
}

void ItemsModel::selectRow(int id, Kind kind, bool ctrl, bool shift)
{
    if (kind != Kind::Body) {
        myCurrentId = id;
        myCurrentKind = kind;
    }
    if (kind == Kind::Folder) {
        std::vector<int> ids = myIn.document ? myIn.document->bodiesUnderGroup(id) : std::vector<int>();
        if (ctrl) {
            std::vector<int> merged = myIn.selected;
            for (int body : ids)
                if (std::find(merged.begin(), merged.end(), body) == merged.end()) merged.push_back(body);
            ids.swap(merged);
        }
        request(ids);
        return;
    }
    if (kind == Kind::Outline) {
        if (myHooks.activateOutline) myHooks.activateOutline(id);
        return;
    }
    const std::vector<int> ids = selectionFor(id, ctrl, shift);
    // FOCUS AND SELECTION NEVER SEPARATE: a Ctrl+click that takes this row out
    // must not make it the keys' row.
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        myCurrentId = id;
        myCurrentKind = Kind::Body;
    } else if (myCurrentKind != Kind::Body || myCurrentId == id ||
               std::find(ids.begin(), ids.end(), myCurrentId) == ids.end()) {
        myCurrentId = ids.empty() ? 0 : ids.back();
        myCurrentKind = Kind::Body;
    }
    request(ids);
    if (!shift) myAnchorId = id;
}

void ItemsModel::activate(int id, bool ctrl, bool shift)
{
    if (!myIn.live || !myIn.document) return;
    const int before = myCurrentId;
    selectRow(id, kindOf(id), ctrl, shift);
    // The keys' row moved (ruling DA1): a refusal written for the row they
    // left comes down.
    if (myCurrentId != before && myHooks.withdrawRefusal) myHooks.withdrawRefusal();
    if (myIn.shown && !myDrag.active) rebuild();
}

std::vector<int> ItemsModel::representingRows() const
{
    // In the list's own order (folded rows included): a folder every body of
    // which is selected stands for them all; a selected body no such folder
    // already holds stands for itself.
    std::vector<int> rows;
    if (!myIn.document) return rows;
    std::set<int> covered;
    std::vector<int> order;
    std::function<void(int)> walk = [&](int parent) {
        for (int id : myIn.document->orderedChildren(parent)) {
            order.push_back(id);
            if (myIn.document->groupExists(id)) walk(id);
        }
    };
    walk(0);
    for (int id : order) {
        if (covered.count(id)) continue;
        if (myIn.document->groupExists(id)) {
            const int parent = myIn.document->groupOf(id);
            if (parent != 0 && covered.count(parent)) {
                covered.insert(id);
                continue;
            }
            if (rowInSelection(id, Kind::Folder)) {
                rows.push_back(id);
                covered.insert(id);
                for (int body : myIn.document->bodiesUnderGroup(id)) covered.insert(body);
            }
        } else if (selected(id)) {
            rows.push_back(id);
            covered.insert(id);
        }
    }
    return rows;
}

bool ItemsModel::rowInSelection(int id, Kind kind) const
{
    if (kind == Kind::Body) return selected(id);
    if (kind == Kind::Outline) return myIn.pendingOutline != 0 && id == myIn.pendingOutline;
    const std::vector<int> under = myIn.document ? myIn.document->bodiesUnderGroup(id) : std::vector<int>();
    return !under.empty() &&
           std::all_of(under.begin(), under.end(), [this](int body) { return selected(body); });
}

// --- folds, the filter, eyes ---------------------------------------------------

void ItemsModel::setFolderOpen(int folderId, bool open)
{
    if (open == isFolderOpen(folderId)) return;
    if (open) myExpanded.insert(folderId);
    else myExpanded.erase(folderId);
    if (myIn.shown) rebuild();
}

void ItemsModel::toggleFold(int folderId)
{
    // INERT while the filter holds the folder open (R7a): a fold the user
    // cannot see would be acted on the moment the filter cleared.
    if (filtering()) return;
    setFolderOpen(folderId, !isFolderOpen(folderId));
}

void ItemsModel::openFolder(int folderId)
{
    if (filtering() || !myIn.document || !myIn.document->groupExists(folderId)) return;
    setFolderOpen(folderId, true);
}

void ItemsModel::setFilter(const QString& text)
{
    const QString query = text.trimmed();
    if (query == myFilter) return;
    myFilter = query;
    // Any edit to the query retires a standing hidden-pick notice.
    myHiddenPickId = 0;
    if (myHooks.withdrawRefusal) myHooks.withdrawRefusal();
    if (myIn.shown) rebuild();
}

void ItemsModel::clearFilter()
{
    const int pending = myHiddenPickId;
    setFilter(QString());
    myHiddenPickId = 0;
    if (pending != 0) reveal(pending);
    if (myIn.shown) rebuild();
}

void ItemsModel::toggleEye(int id)
{
    if (!myIn.live || !myIn.document || !myHooks.setVisible) return;
    const DocumentModel* doc = myIn.document;
    if (doc->groupExists(id)) {
        const std::vector<int> bodies = doc->bodiesUnderGroup(id);
        bool all = true;
        for (int body : bodies) all = all && doc->isVisible(body);
        myHooks.setVisible(bodies, false, !all);
        return;
    }
    if (doc->containsOutline(id)) {
        myHooks.setVisible({id}, true, !doc->isVisible(id));
        return;
    }
    if (doc->contains(id)) myHooks.setVisible({id}, false, !doc->isVisible(id));
}

void ItemsModel::toggleGrain(int id)
{
    if (myIn.live && myIn.document && myIn.document->contains(id) && myHooks.toggleGrain) myHooks.toggleGrain(id);
}

// --- reveal --------------------------------------------------------------------

bool ItemsModel::hiddenByFilter(int bodyId) const
{
    if (!filtering() || !myIn.document) return false;
    return !QString::fromStdString(myIn.document->nameOf(bodyId)).contains(myFilter, Qt::CaseInsensitive);
}

void ItemsModel::bumpScroll(int id)
{
    myScrollId = id;
    ++myScrollSeq;
}

void ItemsModel::reveal(int bodyId)
{
    if (!myIn.document || !myIn.document->contains(bodyId)) return;
    myCurrentId = bodyId;
    myCurrentKind = Kind::Body;
    // Every ancestor opens and STAYS open (A's rule) - even while the drawer is
    // hidden, so the row is there when it opens (ruling R7).
    for (int folder = myIn.document->groupOf(bodyId); folder > 0; folder = myIn.document->groupOf(folder))
        myExpanded.insert(folder);
    // A reveal never fails silently: hidden by the filter, the foot says so.
    if (hiddenByFilter(bodyId)) {
        myHiddenPickId = bodyId;
        return;
    }
    myHiddenPickId = 0;
    bumpScroll(bodyId);
}

// --- rename --------------------------------------------------------------------

void ItemsModel::beginRenameFor(int id)
{
    if (id == 0 || !myIn.shown || !myIn.renameAllowed) return;
    // One rename at a time.
    if (myRenameId != 0) return;
    // A row inside a folded folder exists but is not on screen: an editor
    // opened there could never take focus.
    if (visibleIndexOf(id) < 0) return;
    myRenameId = id;
    ++myRenameSeq;
    // On screen first (ruling RF4).
    bumpScroll(id);
    publish();
}

void ItemsModel::commitRename(int id, const QString& text)
{
    if (id == 0 || id != myRenameId) return;
    myRenameId = 0;
    const QString name = text.trimmed();
    const Row* row = rowById(id);
    if (!name.isEmpty() && row) {
        // Refused silently when empty (InlineRename's rule); otherwise the
        // window checkpoints, writes and reports.
        if (row->kind == Kind::Folder) {
            if (myHooks.renameFolder) myHooks.renameFolder(id, name);
        } else if (myHooks.renameItem) {
            myHooks.renameItem(id, row->kind == Kind::Outline, name);
        }
    }
    if (myIn.shown) rebuild();
}

void ItemsModel::cancelRename()
{
    if (myRenameId == 0) return;
    myRenameId = 0;
    publish();
}

// --- the row menu --------------------------------------------------------------

std::vector<int> ItemsModel::idsForMenu(int id, Kind kind) const
{
    if (kind == Kind::Body && selected(id)) return myIn.selected;
    return {id};
}

void ItemsModel::openMenu(int id)
{
    if (!myIn.live || !myIn.document || myDrag.active) return;
    const DocumentModel* doc = myIn.document;
    const Kind kind = kindOf(id);
    // THE MENU ACTS ON THE SELECTION (R9a): a right press selects its row -
    // unless it is already part of the selection.
    if (!rowInSelection(id, kind)) {
        selectRow(id, kind, false, false);
    } else {
        myCurrentId = id;
        myCurrentKind = kind;
    }
    myMenuRow = id;
    myMenuKind = kind;
    myMenuIds = idsForMenu(id, kind);
    myMenuUp = doc->siblingStepTarget(myMenuIds, -1);
    myMenuDown = doc->siblingStepTarget(myMenuIds, +1);
    const bool folderRow = kind == Kind::Folder;
    const bool liveFolder = folderRow && doc->groupExists(id);
    myMenuRemove = kind == Kind::Outline || id == 0 ? 0 : (folderRow ? (liveFolder ? id : 0) : doc->groupOf(id));
    const bool filter = filtering();

    auto entry = [](const QString& eid, const QString& label, bool enabled) {
        return QVariantMap{{QStringLiteral("id"), eid}, {QStringLiteral("label"), label},
                           {QStringLiteral("enabled"), enabled}, {QStringLiteral("tip"), QString()},
                           {QStringLiteral("keys"), QString()}, {QStringLiteral("depth"), 0}};
    };
    QVariantList entries;
    const bool folderEnabled = kind == Kind::Outline ? false
                               : folderRow           ? (myIn.folderEditsAllowed && liveFolder)
                                                     : myIn.newFolderEnabled;
    entries.push_back(entry(QStringLiteral("folder"), tr("New folder with these"), folderEnabled));
    entries.push_back(entry(QStringLiteral("rename"), tr("Rename"), id != 0 && myIn.renameAllowed));
    QVariantMap up = entry(QStringLiteral("up"), tr("Move up"), myMenuUp.ok && !filter);
    up.insert(QStringLiteral("keys"), ItemsKeys::sequence(ItemsKeys::Command::MoveUp).toString(QKeySequence::NativeText));
    QVariantMap down = entry(QStringLiteral("down"), tr("Move down"), myMenuDown.ok && !filter);
    down.insert(QStringLiteral("keys"),
                ItemsKeys::sequence(ItemsKeys::Command::MoveDown).toString(QKeySequence::NativeText));
    if (filter) {
        up.insert(QStringLiteral("tip"), ItemMoveText::filteredReorderRefusal());
        down.insert(QStringLiteral("tip"), ItemMoveText::filteredReorderRefusal());
    }
    entries.push_back(up);
    entries.push_back(down);
    // MOVE INTO (D1): every folder checkMove() answers Moved for, depth first.
    {
        const int above = filter ? 0 : folderAbove(id);
        std::vector<std::pair<int, int>> folders;
        const std::function<void(int, int)> walk = [&](int parent, int depth) {
            for (int child : doc->orderedChildren(parent)) {
                if (!doc->groupExists(child)) continue;
                folders.emplace_back(child, depth);
                walk(child, depth + 1);
            }
        };
        walk(0, 0);
        QVariantList children;
        for (const auto& [folderId, depth] : folders) {
            if (doc->checkMove(myMenuIds, folderId, DocumentModel::kIntoFolder).check != DocumentModel::MoveCheck::Moved)
                continue;
            QVariantMap child = entry(QStringLiteral("into:%1").arg(folderId),
                                      QString::fromStdString(doc->groupNameOf(folderId)), true);
            child.insert(QStringLiteral("depth"), depth);
            if (folderId == above)
                child.insert(QStringLiteral("keys"), ItemsKeys::sequence(ItemsKeys::Command::MoveIntoFolderAbove)
                                                         .toString(QKeySequence::NativeText));
            children.push_back(child);
        }
        if (children.isEmpty())
            children.push_back(entry(QStringLiteral("none"),
                                     folders.empty() ? tr("No folders yet") : tr("No other folder can take this"),
                                     false));
        QVariantMap into = entry(QStringLiteral("into"), tr("Move into"), !filter);
        if (filter) into.insert(QStringLiteral("tip"), ItemMoveText::filteredIntoRefusal());
        into.insert(QStringLiteral("children"), children);
        entries.push_back(into);
    }
    entries.push_back(entry(QStringLiteral("ungroup"), tr("Remove folder, keep contents"),
                            myIn.folderEditsAllowed && myMenuRemove != 0));
    if (folderRow) entries.push_back(entry(QStringLiteral("delete"), tr("Delete folder and its contents"), true));

    myMenuOpen = true;
    myMenuMap = {{QStringLiteral("open"), true}, {QStringLiteral("row"), id}, {QStringLiteral("entries"), entries}};
    if (myIn.shown) rebuild();
    emit changed();
}

void ItemsModel::closeMenu()
{
    if (!myMenuOpen) return;
    myMenuOpen = false;
    myMenuMap = {{QStringLiteral("open"), false}};
    emit changed();
}

void ItemsModel::chooseMenu(const QString& entryId)
{
    if (!myMenuOpen) return;
    const int rowId = myMenuRow;
    const Kind kind = myMenuKind;
    const std::vector<int> ids = myMenuIds;
    const DocumentModel::MoveTarget up = myMenuUp;
    const DocumentModel::MoveTarget down = myMenuDown;
    const int remove = myMenuRemove;
    // Whether the entry is enabled is read off what the menu showed.
    bool enabled = false;
    for (const QVariant& v : myMenuMap.value(QStringLiteral("entries")).toList()) {
        const QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("id")).toString() == entryId) enabled = e.value(QStringLiteral("enabled")).toBool();
        for (const QVariant& c : e.value(QStringLiteral("children")).toList())
            if (c.toMap().value(QStringLiteral("id")).toString() == entryId)
                enabled = e.value(QStringLiteral("enabled")).toBool() && c.toMap().value(QStringLiteral("enabled")).toBool();
    }
    closeMenu();
    // A choice past a disabled entry is refused here too, while filtered with
    // the reason (RF3), rather than acting blind.
    if (entryId == QLatin1String("up") || entryId == QLatin1String("down")) {
        if (filtering()) {
            refuse(ItemMoveText::filteredReorderRefusal());
            return;
        }
        const DocumentModel::MoveTarget& t = entryId == QLatin1String("up") ? up : down;
        if (enabled && t.ok && myHooks.move) myHooks.move(ids, t.parent, t.beforeId);
        return;
    }
    if (entryId.startsWith(QLatin1String("into:"))) {
        if (filtering()) {
            refuse(ItemMoveText::filteredIntoRefusal());
            return;
        }
        const int folder = entryId.mid(5).toInt();
        if (enabled && myHooks.move) myHooks.move(ids, folder, DocumentModel::kIntoFolder);
        return;
    }
    if (!enabled) return;
    if (entryId == QLatin1String("folder")) {
        if (kind == Kind::Folder) {
            if (myHooks.wrapFolder) myHooks.wrapFolder(rowId);
        } else if (kind == Kind::Body && myHooks.newFolder) {
            myHooks.newFolder();
        }
    } else if (entryId == QLatin1String("rename")) {
        beginRenameFor(rowId);
    } else if (entryId == QLatin1String("ungroup")) {
        if (remove != 0 && myHooks.removeFolder) myHooks.removeFolder(remove);
    } else if (entryId == QLatin1String("delete")) {
        if (kind == Kind::Folder && myHooks.deleteFolder) myHooks.deleteFolder(rowId);
    }
}

void ItemsModel::newFolder()
{
    // The title chip mirrors the action: it acts only while the action would.
    if (myIn.live && myIn.newFolderEnabled && myHooks.newFolder) myHooks.newFolder();
}

// --- the keys ------------------------------------------------------------------

void ItemsModel::refuse(const QString& reason)
{
    if (myHooks.moveRefused) myHooks.moveRefused(reason);
}

void ItemsModel::stepTo(int index, bool shift)
{
    const Row* row = visibleRow(index);
    if (!row) return;
    const int id = row->id;
    const Kind kind = row->kind;
    const int before = myCurrentId;
    selectRow(id, kind, false, shift);
    if (myCurrentId != before && myHooks.withdrawRefusal) myHooks.withdrawRefusal();
    bumpScroll(id);
    if (myIn.shown) rebuild();
}

int ItemsModel::folderAbove(int id) const
{
    const int at = visibleIndexOf(id);
    const Row* row = visibleRow(at);
    if (!row) return 0;
    for (int v = at - 1; v >= 0; --v) {
        const Row* previous = visibleRow(v);
        if (previous->depth > row->depth) continue;
        if (previous->depth < row->depth) return 0;
        return previous->kind == Kind::Folder ? previous->id : 0;
    }
    return 0;
}

std::vector<int> ItemsModel::movableIds() const
{
    if (myCurrentId == 0) return {};
    // A body row moves only while it is SELECTED.
    if (myCurrentKind == Kind::Body && !selected(myCurrentId)) return {};
    return idsForMenu(myCurrentId, myCurrentKind);
}

void ItemsModel::moveByCommand(ItemsKeys::Command command)
{
    if (!myIn.document) return;
    const std::vector<int> ids = movableIds();
    if (ids.empty()) return;
    if (command == ItemsKeys::Command::MoveIntoFolderAbove) {
        if (filtering()) {
            refuse(ItemMoveText::filteredIntoRefusal());
            return;
        }
        const int folder = folderAbove(myCurrentId);
        if (folder == 0) {
            refuse(ItemMoveText::noFolderAboveRefusal());
            return;
        }
        if (myHooks.move) myHooks.move(ids, folder, DocumentModel::kIntoFolder);
        return;
    }
    // Alt+Up/Down while filtered is refused (RF3); Alt+Left is not - "just
    // after its folder" names a place the user can see.
    if (command != ItemsKeys::Command::MoveOutOfFolder && filtering()) {
        refuse(ItemMoveText::filteredReorderRefusal());
        return;
    }
    const DocumentModel::MoveTarget target =
        command == ItemsKeys::Command::MoveOutOfFolder
            ? myIn.document->outOfFolderTarget(ids)
            : myIn.document->siblingStepTarget(ids, command == ItemsKeys::Command::MoveUp ? -1 : +1);
    if (target.ok && myHooks.move) myHooks.move(ids, target.parent, target.beforeId);
}

void ItemsModel::key(const QString& commandName, bool forward)
{
    ItemsKeys::Command command;
    if (!myIn.live || !myIn.document || myDrag.active || !ItemsKeys::commandFromName(commandName, &command)) return;
    const int count = static_cast<int>(myVisible.size());
    if (count == 0) return;
    const int at = visibleIndexOf(myCurrentId);
    const int last = count - 1;
    const int next = !forward ? (at <= 0 ? 0 : at - 1) : (at < 0 ? 0 : std::min(at + 1, last));
    switch (command) {
    case ItemsKeys::Command::Step: stepTo(next, false); break;
    case ItemsKeys::Command::ExtendStep: stepTo(next, true); break;
    case ItemsKeys::Command::FirstOrLast: stepTo(forward ? last : 0, false); break;
    case ItemsKeys::Command::FoldOrParent: {
        const Row* row = visibleRow(at);
        if (!row) break;
        if (row->kind == Kind::Folder && isFolderOpen(row->id) && !filtering()) {
            setFolderOpen(row->id, false);
            break;
        }
        if (row->parent != 0) stepTo(visibleIndexOf(row->parent), false);
        break;
    }
    case ItemsKeys::Command::UnfoldOrChild: {
        const Row* row = visibleRow(at);
        if (!row || row->kind != Kind::Folder) break;
        const int folderId = row->id;
        if (!isFolderOpen(folderId) && !filtering()) {
            setFolderOpen(folderId, true);
            break;
        }
        const Row* child = visibleRow(at + 1);
        if (child && child->parent == folderId) stepTo(at + 1, false);
        break;
    }
    case ItemsKeys::Command::Rename:
        if (at >= 0) beginRenameFor(myCurrentId);
        break;
    case ItemsKeys::Command::MoveUp:
    case ItemsKeys::Command::MoveDown:
    case ItemsKeys::Command::MoveOutOfFolder:
    case ItemsKeys::Command::MoveIntoFolderAbove: moveByCommand(command); break;
    case ItemsKeys::Command::EnterList: {
        // While a query stands, the first MATCHING row (DA2), never a muted
        // folder - selecting it would select bodies the filter hides.
        for (int v = 0; v < count; ++v) {
            if (!filtering() || !visibleRow(v)->muted) {
                stepTo(v, false);
                break;
            }
        }
        break;
    }
    }
}

// --- the drag ------------------------------------------------------------------

void ItemsModel::beginDrag(int id, bool plain)
{
    if (!myIn.live || !myIn.document || myDrag.active ||
        !(myIn.document->contains(id) || myIn.document->containsOutline(id) || myIn.document->groupExists(id)))
        return;
    closeMenu();
    myDrag = Drag{};
    myDrag.active = true;
    const Kind kind = kindOf(id);
    // A press on a row that is ALREADY part of a multiple selection carries
    // the whole of it; one anywhere else carries just that row. A folder row
    // counts as part of it when every body under it is selected (T-064, the
    // design review's F10), and what is carried is the selection's REPRESENTING
    // rows: a wholly selected folder goes whole, never its bodies pulled out.
    const std::size_t under =
        kind == Kind::Folder ? myIn.document->bodiesUnderGroup(id).size() : std::size_t(1);
    const bool partOfMore = plain && rowInSelection(id, kind) && kind != Kind::Outline &&
                            myIn.selected.size() > under;
    if (partOfMore) myDrag.ids = representingRows();
    else myDrag.ids = {id};
    // The ghost names the first carried row in LIST order.
    int first = myDrag.ids.front();
    for (std::size_t i : myVisible)
        if (std::find(myDrag.ids.begin(), myDrag.ids.end(), myAll[i].id) != myDrag.ids.end()) {
            first = myAll[i].id;
            break;
        }
    myDrag.ghostKind = kindName(kindOf(first));
    myDrag.ghostName = ItemMoveText::nameOf(*myIn.document, first);
    myDrag.more = static_cast<int>(myDrag.ids.size()) - 1;
    publish();
    emit changed();
}

void ItemsModel::dragOver(const QString& kind, int parent, int beforeId)
{
    if (!myDrag.active || !myIn.document) return;
    Target target;
    target.kind = kind;
    target.parent = parent;
    target.beforeId = beforeId;
    QString status;
    bool refused = false;
    if (kind == QLatin1String("outside")) {
        status = ItemMoveText::releaseToCancel();
    } else if (kind == QLatin1String("none")) {
        status = filtering() ? ItemMoveText::filteredDropHint() : ItemMoveText::noTargetHint();
    } else {
        // A filtered list hides where a reorder lands: only into-folder drops.
        if (filtering() && kind != QLatin1String("into")) {
            target.kind = QStringLiteral("none");
            status = ItemMoveText::filteredDropHint();
        } else {
            if (kind == QLatin1String("top")) {
                target.parent = 0;
                target.beforeId = 0;
            }
            const DocumentModel::MoveResult dry = myIn.document->checkMove(myDrag.ids, target.parent, target.beforeId);
            refused = dry.check == DocumentModel::MoveCheck::IntoItself || dry.check == DocumentModel::MoveCheck::Unknown;
            status = refused ? ItemMoveText::refusal(*myIn.document, dry) : ItemMoveText::dragStatus(*myIn.document, dry);
        }
    }
    const bool same = target.kind == myDrag.target.kind && target.parent == myDrag.target.parent &&
                      target.beforeId == myDrag.target.beforeId && refused == myDrag.refused && status == myDrag.status;
    myDrag.target = target;
    myDrag.refused = refused;
    if (same) return;
    const bool line = status != myDrag.status;
    myDrag.status = status;
    emit changed();
    if (line && myHooks.dragStatusChanged) myHooks.dragStatusChanged();
}

void ItemsModel::endDrag(bool commit)
{
    if (!myDrag.active) return;
    const Drag drag = myDrag;
    myDrag = Drag{};
    const bool hadLine = !drag.status.isEmpty();
    if (myStale && myIn.shown) {
        myStale = false;
        rebuild();
    } else {
        publish();
    }
    emit changed();
    if (hadLine && myHooks.dragStatusChanged) myHooks.dragStatusChanged();
    // A release with no target - outside the card, or over blank space -
    // cancels. A refused target still asks, so the window's Failure says why.
    const QString& kind = drag.target.kind;
    if (commit && kind != QLatin1String("none") && kind != QLatin1String("outside") && myHooks.move)
        myHooks.move(drag.ids, drag.target.parent, drag.target.beforeId);
}
