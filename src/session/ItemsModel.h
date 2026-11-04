#pragma once

// THE ITEMS DRAWER'S MODEL (migration Phase 3h): everything the page's Items
// card shows and decides, worked out here once and tested headless - the rows
// the list shows (visible rows only, in DocumentModel::orderedChildren()
// order, depth first), which folders are open, the filter, the keys' row and
// the Shift anchor, the selection marks, reveal and the hidden-pick foot, the
// row menu, the list's key commands, and a drag's dry run and words.
//
// It is ItemsPanel (the Qt drawer, kept until Phase 6) ported rule for rule;
// the two hold separate copies of the rules until the Qt one is deleted (the
// HubModel/VersionsModel precedent, ruling R1 of the plan). The words are the
// Qt drawer's own, under its tr context "ItemsPanel", and every move sentence
// is ItemMoveText's.
//
// IT WRITES NOTHING. Every change - a selection, an eye, a rename, a move, a
// folder made or removed - goes through a hook to the code the Qt drawer's
// signals reach, so MainWindow checkpoints and reports exactly as it does for
// the Qt drawer. A hidden drawer builds nothing (ruling R7), but a pick still
// opens the folders above its row, as the Qt drawer's does.
//
// QtCore + OCCT (furnify_session); tested headless by items_model_tests.
//
// OCCT first, by the Handle() rule - DocumentModel.h carries OCCT headers.
#include "DocumentModel.h"
#include "ItemsKeys.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <set>
#include <vector>

class ItemsModel : public QObject
{
    Q_OBJECT
    // Per row (visible rows only, top to bottom): id, kind ("body", "outline",
    // "folder"), name, depth, parent, eyeOn, onScreen, muted, grain ("along",
    // "across" or "" off a body), grainTip, grainName, marked, current,
    // pending, partial ("2/5" or ""), partialWords, open, held, match
    // ([start, length] or null), carried, name (accessible: accessibleName),
    // eyeName, eyeTip, twistyName, twistyTip, tip, and the drop targets:
    // above {parent, before, depth}, below {parent, before, depth}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    // filter, noMatch, foot, empty (bool), live, newFolder {enabled, tip},
    // ledger {bodies, selected}, current, scroll {id, seq}, rename {id, name,
    // seq} (id 0 when none).
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // The open row menu: {open, entries: [{id, label, enabled, tip, keys,
    // depth, children}]}; {open: false} when none.
    Q_PROPERTY(QVariantMap menu READ menu NOTIFY changed)
    // {active, refused, status, target {kind, parent, before}, ghost {kind,
    // name, more}}.
    Q_PROPERTY(QVariantMap drag READ drag NOTIFY changed)
    Q_PROPERTY(QVariantMap words READ words CONSTANT)
    // The list's key table (session/ItemsKeys), for the page: [{command,
    // fromFilter, keys: [{key, shift, alt, ctrl}]}].
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)

public:
    enum class Kind { Body, Outline, Folder };

    struct Hooks
    {
        // A click asks for the selection to BE these bodies.
        std::function<void(const std::vector<int>&)> select;
        // An outline row: the outline Extrude will consume.
        std::function<void(int)> activateOutline;
        // An eye: these items (bodies, or one outline) shown or hidden.
        std::function<void(const std::vector<int>&, bool outline, bool show)> setVisible;
        std::function<void(int)> toggleGrain;
        std::function<void(int id, bool outline, const QString& name)> renameItem;
        std::function<void(int id, const QString& name)> renameFolder;
        std::function<void(const std::vector<int>& ids, int parent, int beforeId)> move;
        // New Folder with These (the title chip and the body row's menu).
        std::function<void()> newFolder;
        std::function<void(int)> wrapFolder;
        std::function<void(int)> removeFolder;
        std::function<void(int)> deleteFolder;
        // A quiet refusal for the status bar's left channel, and its
        // withdrawal when the filter or the keys' row moves on.
        std::function<void(const QString&)> moveRefused;
        std::function<void()> withdrawRefusal;
        // The drag's status line changed (dragStatusText()).
        std::function<void()> dragStatusChanged;
    };

    struct Inputs
    {
        const DocumentModel* document = nullptr;
        // Whether a body is actually drawn (its eye and Isolate) - the view's
        // composed answer. Null: every visible body is.
        std::function<bool(int)> onScreen;
        std::vector<int> selected;
        int pendingOutline = 0;
        bool folderEditsAllowed = true;
        bool newFolderEnabled = false;
        QString newFolderTip;
        bool renameAllowed = true;
        bool live = false;
        bool shown = true;
    };

    explicit ItemsModel(Hooks hooks, QObject* parent = nullptr);

    QVariantList rows() const { return myRowMaps; }
    QVariantMap state() const { return myStateMap; }
    QVariantMap menu() const { return myMenuMap; }
    QVariantMap drag() const;
    QVariantMap words() const;
    QVariantList keys() const;

    // Every appStateChanged. A hidden drawer only remembers the inputs.
    void setState(Inputs inputs);
    // The viewport's selection changed: a pick that did not come from this
    // drawer reveals its newest body (opens its folders, scrolls to it, or
    // says the filter hides it).
    void showSelection(const std::vector<int>& ids);
    // A furniture swap: the filter goes (M2). A document swap: the pick
    // baseline and the keys' row go too.
    void forgetFurniture();
    void resetPick();
    // MainWindow's routes into a rename (F2, a new folder made from the
    // selection): opens the field over that row when it is on screen.
    void beginRenameFor(int id);
    void setFolderOpen(int folderId, bool open);
    bool isFolderOpen(int folderId) const { return myExpanded.count(folderId) > 0; }
    // The live drag's status line, for the status bar's right channel.
    QString dragStatusText() const { return myDrag.status; }
    bool dragActive() const { return myDrag.active; }
    void cancelDrag() { endDrag(false); }
    QString filterText() const { return myFilter; }
    int currentRowId() const { return myCurrentId; }
    // The fixed copy, for the vocabulary sweep.
    QStringList paintedTexts() const;

public slots:
    void activate(int id, bool ctrl, bool shift);
    void toggleFold(int folderId);
    void openFolder(int folderId);   // spring-loading: opens, never closes
    void toggleEye(int id);
    void toggleGrain(int id);
    void setFilter(const QString& text);
    void clearFilter();
    void beginRename(int id) { beginRenameFor(id); }
    void commitRename(int id, const QString& text);
    void cancelRename();
    void openMenu(int id);
    void chooseMenu(const QString& entry);
    void closeMenu();
    void newFolder();
    // One of the key table's commands (ItemsKeys::commandName()); `forward`
    // is Down/End for the two-way ones.
    void key(const QString& command, bool forward);
    void beginDrag(int id, bool plain);
    // kind: "none", "line", "into", "top" or "outside".
    void dragOver(const QString& kind, int parent, int beforeId);
    void endDrag(bool commit);

signals:
    void changed();

private:
    struct Row
    {
        int id = 0;
        Kind kind = Kind::Body;
        QString name;
        int depth = 0;
        int parent = 0;
        bool visible = true;     // the document's own flag (a folder: every body under it)
        bool anyVisible = true;  // a folder: some body under it
        bool onScreen = true;
        bool across = false;
        bool matched = false;
        bool muted = false;
        bool shown = false;      // on screen in the list (folds and filter)
        bool open = false;
        bool held = false;
    };
    struct Target
    {
        QString kind = QStringLiteral("none");
        int parent = 0;
        int beforeId = 0;
    };
    struct Drag
    {
        bool active = false;
        std::vector<int> ids;
        bool refused = false;
        QString status;
        Target target;
        QString ghostKind;
        QString ghostName;
        int more = 0;
    };

    bool filtering() const { return !myFilter.isEmpty(); }
    void buildTree(int parent, int depth);
    void applyVisibility();
    void rebuild();
    void publish();
    QVariantMap rowMap(const Row& row) const;
    const Row* rowById(int id) const;
    int visibleIndexOf(int id) const;
    const Row* visibleRow(int index) const;
    Kind kindOf(int id) const;
    std::vector<int> selectionFor(int id, bool ctrl, bool shift) const;
    void selectRow(int id, Kind kind, bool ctrl, bool shift);
    void request(const std::vector<int>& ids);
    bool rowInSelection(int id, Kind kind) const;
    bool selected(int id) const;
    void stepTo(int index, bool shift);
    int folderAbove(int id) const;
    std::vector<int> movableIds() const;
    std::vector<int> idsForMenu(int id, Kind kind) const;
    void moveByCommand(ItemsKeys::Command command);
    void refuse(const QString& reason);
    void reveal(int bodyId);
    bool hiddenByFilter(int bodyId) const;
    void bumpScroll(int id);
    bool carried(const Row& row) const;
    QString accessibleName(const Row& row, const QString& partialWords, bool pending) const;

    Hooks myHooks;
    Inputs myIn;
    bool myStale = false;   // inputs arrived while hidden or mid-drag
    std::vector<Row> myAll;
    std::vector<std::size_t> myVisible;
    std::set<int> myExpanded;
    QString myFilter;
    int myCurrentId = 0;
    Kind myCurrentKind = Kind::Body;
    int myAnchorId = 0;
    std::vector<int> myPickBaseline;
    bool myRequesting = false;
    int myHiddenPickId = 0;
    int myScrollId = 0;
    int myScrollSeq = 0;
    int myRenameId = 0;
    int myRenameSeq = 0;
    // The open menu: the row it is about and what each entry does.
    int myMenuRow = 0;
    Kind myMenuKind = Kind::Body;
    std::vector<int> myMenuIds;
    DocumentModel::MoveTarget myMenuUp;
    DocumentModel::MoveTarget myMenuDown;
    int myMenuRemove = 0;
    bool myMenuOpen = false;
    Drag myDrag;

    QVariantList myRowMaps;
    QVariantMap myStateMap;
    QVariantMap myMenuMap;
};
