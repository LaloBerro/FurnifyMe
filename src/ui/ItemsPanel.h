#pragma once
// Lists the document's bodies with a visibility toggle each. Reads the document
// rather than owning it, and is rebuilt when MainWindow announces a change -
// DocumentModel stays free of Qt and cannot emit signals of its own.
//
// A floating DRAWER since Phase 5, not a docked pane: a member of the
// Theme::paintSurface() family, parented to the viewport and anchored beside
// the tool rail by ViewportOverlay, which is also what puts it in
// occupiedRects() so the toast, the balloon and the guide step around it for
// free. Two consequences follow from being over OCCT's GL surface rather than
// inside a splitter, and both are CLAUDE.md rules that a dock never had to
// satisfy:
//
//   - It paints its own rounded card in paintEvent() - Theme::paintSurface() -
//     and its own child rows and labels paint no background of their own, so
//     the card shows through them rather than each stamping a rectangle of
//     its own colour. (Historical: before the QOpenGLWidget migration, this
//     card had to paint its ENTIRE rect opaquely, corners included, because an
//     unpainted pixel over the viewport's old native-window GL surface read
//     as whatever the driver had left there, which was black. Since the
//     migration, paintSurface() paints only the rounded shape and the drawer's
//     own corners genuinely composite through to the live scene - see
//     Theme::makeSurfaceTransparent() for what keeps the app-wide stylesheet
//     from painting a flat square there instead.)
//   - It carries Qt::WA_NoMousePropagation, so a press or release that lands
//     on the drawer never reaches the viewport underneath and re-picks behind
//     it.
//
// Its visibility is DERIVED from MainWindow's existing Items action and never
// set from anywhere else; see MainWindow's constructor.
#include <QColor>
#include <QIcon>
#include <QKeyCombination>
#include <QKeySequence>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "session/ItemsKeys.h"

#include <set>
#include <unordered_map>
#include <vector>

class DocumentModel;
class OcctViewWidget;
class QKeyEvent;
class QVBoxLayout;

class ItemsPanel : public QWidget {
    Q_OBJECT

public:
    // `document` is NOT const, since Milestone 3: the eye button is a write
    // path now, not only a read. DocumentModel owns visibility
    // (isVisible()/setVisible(), Task 1) and the view is a mirror of it -
    // this panel writes both on every toggle, in that order, so a save
    // captures exactly what the eye buttons show.
    ItemsPanel(DocumentModel* document, OcctViewWidget* view, QWidget* parent = nullptr);

    void refresh();
    // The rows the user can SEE, top to bottom. Every row of the whole tree
    // is built (see refresh()) and a folded folder's contents are HIDDEN rows,
    // so these count and index only the visible ones - which is what every
    // caller has always meant by "row 3". Read accessors for the suite, which
    // hit-tests the REAL row widget with childAt() rather than sending an
    // event at a pointer it merely hopes is reachable (CLAUDE.md's
    // childAt()-vs-sendEvent trap). rowTextAt() reports the name alone - no
    // dimension is painted on a row any more. grainMarkAt() is null for an
    // outline or a folder row (neither carries one - see addItemRow()); it is
    // asked of the panel rather than found by scanning for a button of the
    // right shape, which is the failure this file has been bitten by twice.
    // Null/0/false/empty out of range.
    int rowCount() const;
    QString rowTextAt(int index) const;
    QWidget* rowWidgetAt(int index) const;
    int rowIdAt(int index) const;
    class QPushButton* grainMarkAt(int index) const;
    bool rowIsOutlineAt(int index) const;
    bool rowIsGroupAt(int index) const;
    // How deep in the tree the row sits: 0 at the document's root.
    int rowDepthAt(int index) const;
    // The filter (T-063). Rows narrow to matches plus the folders above them;
    // those folders are force-opened without touching the user's own fold
    // state and drawn muted. Empty text restores everything as it was.
    class QLineEdit* filterField() const;
    bool filterActive() const { return !myFilterText.isEmpty(); }
    class QAction* clearFilterAction() const { return myClearFilterAction; }
    class QLabel* noMatchLabel() const { return myNoMatch; }
    // A visible row shown only because a match lies beneath it.
    bool rowIsMutedAt(int index) const;
    // The inline rename editor open over a row, or null - never the filter
    // field, which is always there.
    class QLineEdit* openRenameEditor() const;
    // Reveal and the partial mark (T-063).
    class QScrollArea* scrollArea() const { return myScroll; }
    // "2/5" on a folder row whose subtree is PARTLY selected; empty otherwise.
    QString partialCountAt(int index) const;
    // The label that shows it (finding F18): its tooltip spells the count out.
    class QLabel* partialCountLabelAt(int index) const;
    // Whether the row wears the selection mark - the fill and the 3 px bar
    // (rulings D3/D4). Never true for an outline row.
    bool rowIsMarkedAt(int index) const;
    // The card foot that says a pick is hidden by the filter.
    QWidget* hiddenPickLine() const { return myFoot; }
    // The drag (T-063): a custom in-process gesture, never QDrag. Read-only
    // views of the live target, for the suite and the status line.
    enum class DropKind { None, Line, Into, TopLevelEnd };
    bool dragActive() const { return myDragging; }
    DropKind dropKind() const { return myDropTarget.kind; }
    bool dropRefused() const { return myDropTarget.refused; }
    int dropParent() const { return myDropTarget.parent; }
    int dropBeforeId() const { return myDropTarget.beforeId; }
    class DropIndicator* dropIndicator() const { return myIndicator; }
    bool titleLit() const { return myTitleLit; }
    // The title row's band, in this card's coordinates: the drop target for
    // "the end of the top level" (the card sizes itself to its rows, so there
    // is almost no blank space below the last row to drop on).
    QRect titleRowRect() const;
    // Ends a live drag (or an armed press) without moving anything.
    void cancelDrag();
    // The live drag's status line - present tense, the same words its Note
    // will use - or empty when no drag is live. MainWindow puts it in the
    // status bar's RIGHT channel (mode and next step, the F9 ruling).
    QString dragStatusText() const { return myDragStatus; }
    class DragGhost* dragGhost() const { return myGhost; }
    // The keyboard (T-063). The focused row IS the selected row - there is no
    // focus-only cursor to lose, because no ring may show it.
    int currentRowId() const { return myCurrentId; }
    // What a keyboard move or Move up/down carries: the focused folder or
    // outline, or the selected bodies when the focused row is one of them.
    std::vector<int> movableIds() const;
    // A document swap: forget what the last pick left (ids restart per document).
    void resetPick();
    // A folder row's twisty, null for any other row - for the suite, which
    // reads its tooltip to see which way the folder currently points.
    class QPushButton* twistyAt(int index) const;

    // What a row IS, handed to every gesture that acts on one.
    enum class RowKind { Body, Outline, Folder };

    // THE LIST'S KEYS, in ONE table (ruling D19): claimsKey() and
    // keyPressEvent() look a key up here, the filter field asks it which of
    // its keys hand over to the list, the row menu reads the Move entries'
    // keys off it, and MainWindow hands it to the ShortcutSheet as its "Items
    // list" group - so a key the list answers is a key the sheet lists, and
    // neither can be written twice. Each entry is one row of the sheet.
    // The table itself lives in session/ItemsKeys since Phase 3h, so the
    // HTML drawer and the chrome's list hold read the same one; these are
    // its names here.
    using ListCommand = ItemsKeys::Command;
    using ListKey = ItemsKeys::Key;
    static const std::vector<ListKey>& listKeys();
    // The entry a key belongs to, in the list's scope or the filter field's -
    // null when it is not one of the drawer's keys. `mods` without Keypad.
    static const ListKey* listKeyFor(QKeyCombination combination, bool fromFilterField);
    static QString listKeyLabel(const ListKey& entry);
    // "Up, Down" - every combination, in the platform's own spelling.
    static QString listKeyText(const ListKey& entry);
    // The first combination of `command`, for the row menu's shortcut column.
    static QKeySequence listKeySequence(ListCommand command);

    // Builds the row's right-click menu into `menu`, every entry already
    // wired to the move, rename or folder route it names - showRowMenu()
    // only exec()s it, and the suite triggers entries without a modal loop.
    void populateRowMenu(class QMenu& menu, int rowId, RowKind kind);

    // The title row's New folder: a ToolChip mirroring MainWindow's action
    // (the Model menu's New Folder with These), so its enabled state and its
    // reason are the action's - and the row menu's own entry reads the same
    // enabled state, so the three cannot disagree (T-063). Called once.
    void setNewFolderAction(class QAction* action);
    class ToolChip* newFolderChip() const { return myNewFolderChip; }
    // Whether the window allows a folder to be made or removed right now (not
    // sketching, not in render mode, not at the library) - pushed in from
    // MainWindow::updateActions(), the single place availability is decided,
    // exactly as showPendingOutline() is. The row menu's folder-row entries
    // read it, so an entry is enabled exactly when its handler would act.
    void setFolderEditsAllowed(bool allowed) { myFolderEditsAllowed = allowed; }
    // A furniture swap (MainWindow::openFurniture()/showInitScreen()): the
    // filter is the last furniture's query, so it goes - a new furniture
    // never opens filtered (M2). Not on undo or redo, which keep the query.
    void forgetFurniture();

    // Highlights the rows for these solids. Called when the viewport selection
    // changes, so the two views of the document never disagree.
    void showSelection(const std::vector<int>& ids);

    // Highlights the outline row Extrude would consume. Pushed in from
    // MainWindow::updateActions() - the single place that decides what is
    // available - rather than derived here, exactly as the toast's Undo
    // enabled state is: this panel has the document but not the notion of
    // which outline is pending, and giving it one would be a second answer to
    // a question MainWindow already answers.
    //
    // It does NOT wear the selection mark any more (ruling D4): the list
    // showed three filled rows while the status bar said "2 bodies selected",
    // and the fill is also the focus cue, so two rows looked focused. Its row
    // says it instead - accessible name and tooltip end ", waiting to
    // extrude" - and design batch C tints its glyph (ItemsRowWidget::
    // pendingOutline() is the hook).
    void showPendingOutline(int id);

    // Opens the inline rename gesture (ui/InlineRename) over `id`'s row -
    // whichever kind it is, body or outline. The caller (MainWindow, for F2)
    // supplies `isOutline` rather than this panel guessing it from the id
    // space, because ids alone cannot say which list an id belongs to and a
    // wrong guess would open the edit over the wrong row. A no-op if `id`
    // does not name a live row - the row this task's own double-click path
    // hands in always does, since it reads the id straight off the row it
    // just hit-tested.
    //
    // Renaming itself is NOT done here: this panel writes DocumentModel's
    // VISIBILITY directly (the eye button always has), but a rename is a
    // checkpointed document change that reports through a toast, and this
    // panel has neither a checkpoint stack nor a toast host of its own -
    // MainWindow does both, and is the single place every other checkpointed
    // commit in this app goes through. renameCommitted() is the handoff.
    void beginRenameForItem(int id, bool isOutline);
    // The same gesture over a FOLDER row. A separate entry point rather than
    // a third value threaded through the one above: the two report through
    // different signals, because what MainWindow does with the new name
    // differs (setItemName against setGroupName), and a caller that got the
    // flag wrong would rename nothing and say nothing.
    void beginRenameForGroup(int id);
    // Whether `groupId` is open in this drawer. Expansion is SESSION state -
    // where the user last looked, not a property of the furniture - so it
    // lives here and is persisted nowhere, the same ruling the Settings
    // drawer's current tab already carries.
    bool isGroupExpanded(int groupId) const;
    void setGroupExpanded(int groupId, bool expanded);

    // The fixed app copy this panel paints, for the vocabulary sweep -
    // title, tooltips, the empty-state message. Item NAMES are deliberately
    // excluded, on the same terms InitScreen's furniture names and
    // VersionsPanel's version names already are: they are the user's own
    // words, not this app's, and a sweep that flagged them would be
    // flagging content this panel is required to show unmangled.
    QStringList paintedTexts() const;

signals:
    // An eye button changed an item's visibility. MainWindow answers by
    // re-deriving the session filter over it (applyIsolation()) and by
    // updateActions(), whose appStateChanged refreshes this panel's own
    // dimming - the eye's direct view write stays for immediacy, but the
    // composed answer is re-derived at the one writer.
    void visibilityToggled();
    // A row's grain mark was clicked: that body should run the other way.
    // Announced rather than written here, because the document owns it and
    // MainWindow is the one writer - the eye button's own arrangement one
    // line up, and for the same reason.
    void grainToggled(int bodyId);
    void solidActivated(int id);
    // WHAT THE SELECTION SHOULD BE after a click on a body row, worked out
    // here rather than at the receiving end: plain click replaces, Ctrl
    // toggles, Shift takes the run of rows between the last click and this
    // one. The panel is the only thing that knows the row ORDER a Shift-range
    // is measured in, so it is the only thing that can answer it.
    void selectionRequested(std::vector<int> ids);
    // An outline row was clicked. A separate signal rather than one id
    // channel with a kind flag: the two do genuinely different things -
    // a body row changes the viewport selection, an outline row changes which
    // outline Extrude will consume - and a receiver that had to branch on a
    // flag could get the branch wrong in a way the compiler could not see.
    void outlineActivated(int id);
    // A rename was committed - InlineRename already trimmed the text and
    // refused an empty/whitespace one silently, so `newName` here is always
    // non-empty. MainWindow is what actually checkpoints, writes the name and
    // reports the toast; this panel only ran the UI gesture.
    void renameCommitted(int id, bool isOutline, QString newName);
    // A folder row was clicked: MainWindow answers by selecting every body
    // under it, so everything downstream sees the ordinary body selection it
    // always saw (see DocumentModel::Group's own comment).
    void groupActivated(int id);
    void groupRenameCommitted(int id, QString newName);
    // EVERY route that moves rows - a drop, Alt+arrow, Move up/Move down -
    // asks for the move this one way (T-063). `parent` 0 is the root;
    // `beforeId` is DocumentModel's: a sibling to go in front of, 0 the end,
    // or DocumentModel::kIntoFolder. MainWindow dry-runs it, checkpoints and
    // reports; this panel only ran the gesture.
    void itemsMoveRequested(std::vector<int> ids, int parent, int beforeId);
    // dragStatusText() changed - a drag began, moved to a new target, or ended.
    void dragStatusChanged();
    // The right-click menu's New folder with these: put the selected bodies
    // in a new folder. The user's rule (2026-10-07): with nothing selected it
    // is disabled, and it NEVER makes an empty folder - MainWindow's handler
    // refuses an empty selection even when this arrives past a disabled
    // entry. (The title row's chip triggers MainWindow's action directly.)
    void newFolderRequested();
    // The same entry over a FOLDER row (ruling RF2): put THAT folder inside a
    // new one, made in its place - same parent, same position - so it acts on
    // what was right-clicked rather than on the bodies a right press happened
    // to select. Over an outline row the entry is disabled outright.
    void wrapFolderRequested(int folderId);
    // The right-click menu's "Remove folder, keep contents", carrying the
    // folder it names (ruling RF1): the right-clicked folder row's own id, or
    // a body row's own folder. Never the selection's shared folder - that is
    // the Model menu's Ctrl+Shift+G, which keeps its selection meaning. The
    // same handler the Model menu's entry ends in, so one author writes the Note.
    void removeFolderRequested(int folderId);
    // The filter's text changed (setFilter()): MainWindow withdraws a move
    // refusal the old filter caused (ruling D1 - the RF3 sentence stayed up
    // after the filter it blamed was gone).
    void filterChanged();
    // The keyboard's current row moved to another row - a step, a click, an
    // outline row included (ruling DA1): MainWindow withdraws a move refusal
    // written for the row the keys left.
    void currentRowChanged();
    // A move the list will not make, with the reason for the status bar's left
    // channel - a quiet refusal, not a Failure (ruling RF3: Alt+Up/Down while
    // the filter hides where the row would land).
    void moveRefused(QString reason);
    // The folder row's own Delete: the folder AND every body in it, which is
    // the one thing Ungroup deliberately does not do. Reported rather than
    // performed - MainWindow owns the checkpoint and the toast that carries
    // Undo, exactly as it does for a rename.
    void deleteGroupRequested(int groupId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The list's keys (T-063): claimed from the shortcut map through
    // ShortcutOverride, and acted on, only while the list ITSELF holds focus.
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Out of line: it asks the layout for a height at this card's fixed
    // width, because the empty-state message word-wraps and a QLayout's plain
    // sizeHint does not account for that.
    QSize sizeHint() const override;
    // A CLOSED drawer defers its rebuild (see refresh()); these two bring it
    // up to date on the way back on screen, setVisible() before the overlay
    // measures it and showEvent() for a parent shown with it still open.
    void showEvent(class QShowEvent* event) override;
    // The drawer leaving the screen by ANY route - its own setVisible(false),
    // or a parent hiding it (Close furniture hides the whole editor, which
    // never reaches this panel's setVisible()) - takes a live drag with it
    // (ruling R10a). Qt's hideChildren() sends this event either way.
    void hideEvent(class QHideEvent* event) override;

public:
    // A panel dying mid-drag still puts back the app-wide things the drag
    // put up - the override cursor above all, which is a stack.
    ~ItemsPanel() override;
    void setVisible(bool visible) override;

private:
    // Restyles what is on screen from the live Theme - the title's stylesheet,
    // each row's name-label stylesheet, each eye button's rasterised icon -
    // and re-derives the card's width for the current type scale.
    //
    // It does NOT rebuild the rows, and that is the point. It used to call
    // refresh(), which tears every row down and constructs it again; a theme
    // edit fires once per mouse MOVE inside the colour picker, so a drag was
    // destroying and rebuilding the whole list per frame. Nothing about a
    // theme change alters WHICH bodies exist - only how they are painted -
    // so the rebuild belongs to documentChanged and the restyle belongs here.
    void applyTheme();
    // The card's width for the current type scale: the shipped 200, plus
    // however much wider a specimen name measures now than it did under
    // defaultSpec(). Exactly 200 at the shipped look, by construction, and
    // stable against the actual body names - a drawer that resized itself to
    // the longest name would move the viewport's usable area around under
    // the user, which is the reason this card was fixed-width to begin with.
    // It was 240 while rows also painted a dimension; the user narrowed it
    // once rows became a name and an eye button.
public:
    // Public since Phase 3h: the HTML card's slot is this wide.
    static int cardWidth();

private:

    // One row's widgets, kept so applyTheme() can restyle in place. Replaces
    // the four parallel vectors this held before, which had to be indexed in
    // lockstep by every reader.
    struct Row {
        class ItemsRowWidget* widget = nullptr;
        class QLabel* name = nullptr;
        class QPushButton* eye = nullptr;
        // The grain mark, on BODY rows only - null on an outline and on a
        // folder. See addItemRow() for why those two do not carry one.
        class QPushButton* grain = nullptr;
        int id = 0;
        // Outline rows come first and are not part of the viewport selection -
        // showSelection() must not highlight one, and the eye toggles a
        // different channel. Carried on the row rather than derived by asking
        // the document again, so a row can never be styled as one kind and
        // toggled as the other.
        bool isOutline = false;
        // A FOLDER row (improvements item 10). Three kinds now, and each is
        // carried on the row rather than re-derived by asking the document
        // again, so a row can never be styled as one kind and acted on as
        // another.
        bool isGroup = false;
        int depth = 0;     // 0 at the document's root
        int parentId = 0;        // the folder it sits in, 0 at the root
        bool onScreen = true;    // its item is actually drawn (eye and Isolate)
        bool twistyOpen = false; // what the twisty currently shows
        bool twistyHeld = false; // ...and whether the filter is holding it open
        bool grainAcross = false; // what the grain mark currently shows
        QString style;           // the row look restyleRows() last set
        class QLabel* count = nullptr;   // folder rows: "2/5" while partly selected
        class QPushButton* twisty = nullptr;
        QString text;      // what rowTextAt() reports
        bool matched = false;   // its name holds the filter's query
        bool muted = false;     // shown only because a match lies beneath it
        QString nameSheet;      // the stylesheet last applied to `name`
        // The accessible name nameRow() composed, before writeRowName() adds
        // what the selection says: the pending outline's ", waiting to
        // extrude" (D4) and a folder's partial count in words (F18).
        QString baseName;
        QString partialWords;   // "2 of 4 bodies in Carcass are selected", or empty
        bool pending = false;   // the outline Extrude will consume
    };

    DocumentModel* myDocument = nullptr;
    OcctViewWidget* myView = nullptr;
    class QLabel* myTitle = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRows = nullptr;
    // The rows scroll inside the card once there are more of them than fit
    // (improvements item 12). WITH NO SCROLLBAR DRAWN, by the user's own call:
    // the bar is turned off rather than styled thin, so the card looks exactly
    // as it always did and the wheel does the work. A QScrollArea still
    // scrolls with its bar off - the policy governs the bar, not the area.
    class QScrollArea* myScroll = nullptr;
    // New folder in the title row: one click files the selection in a folder.
    // It replaced the hover x this card used to carry - the drawer is closed
    // from its rail chip and its menu entry, and the corner is worth more as
    // the one control this list actually needs. A ToolChip mirroring
    // MainWindow's action (setNewFolderAction()), null until that is handed in.
    class QHBoxLayout* myTitleRow = nullptr;
    class ToolChip* myNewFolderChip = nullptr;
    class QAction* myNewFolderAction = nullptr;
    // See setFolderEditsAllowed(). True until told otherwise, so a panel built
    // without a window (none ships) still offers what its document allows.
    bool myFolderEditsAllowed = true;
    // The tallest the card may grow before the rows start scrolling: a share
    // of the VIEWPORT's own height, read live, so the answer follows the
    // window rather than a number picked on one screen. Zero until the card
    // has a parent to ask.
    int rowsHeightCap() const;
    // EVERY row of the tree, open or folded, in the order the document
    // resolves (DocumentModel::orderedChildren()), depth first.
    std::vector<Row> myRowList;
    // Indices into myRowList of the rows on screen, top to bottom - what
    // rowCount()/rowTextAt(index) and the rest report against.
    std::vector<std::size_t> myVisible;
    const Row* visibleRow(int index) const;
    // Shows exactly the rows whose ancestors are open, hides the rest, and
    // rebuilds myVisible. Cheap: no widget is created or destroyed.
    void applyRowVisibility();
    // Re-measures the card after its visible rows changed (see refresh()'s
    // comment for why both layouts are invalidated by hand).
    void relayoutCard();
    // `heldByFilter`: the filter is holding the folder open, so the twisty
    // is inert and its tooltip says so (ruling R7a).
    void syncTwisty(Row& row, bool open, bool heldByFilter = false);
    static QString filterHeldTip();
    // THE ONE PLACE a row's accessible names are written (T-063): the row
    // ("Side left, in Carcass, hidden"), its eye ("Hide Side left"), its
    // grain mark and its twisty. Run on every build - a fresh row and a
    // reused one alike - so a rename, a move or an eye click is heard.
    // `shown`/`anyShown`: for a folder, whether every / any body under it is
    // on (an item passes its own visibility twice).
    void nameRow(Row& row, bool shown, bool anyShown);
    // Writes the row's accessible name (and the pending outline's tooltip)
    // from baseName plus the selection's words - only when they moved.
    void writeRowName(Row& row);
    static QString twistyName(bool open, const QString& folderName);
    // The twisty's two chevrons, rasterised once per theme (applyTheme()
    // drops them) rather than once per twisty that changes.
    const QIcon& chevron(bool open);
    QIcon myChevronOpen;
    QIcon myChevronClosed;

    // --- the filter (T-063) ---------------------------------------------------
    class ItemsFilterField* myFilter = nullptr;
    class QAction* myClearFilterAction = nullptr;
    class QLabel* myNoMatch = nullptr;
    // A theme's ledger (themes part 2, Ultraviolet): the body count in the
    // title row, a rule under it, the column headings and the footer. Hidden
    // in every theme whose list does not ask for them.
    class QLabel* myBodyCount = nullptr;
    class QWidget* myTitleRule = nullptr;
    class QWidget* myHeadings = nullptr;
    class QLabel* myHeadName = nullptr;
    class QLabel* myHeadTools = nullptr;
    class QWidget* myLedgerFoot = nullptr;
    class QLabel* myLedgerSelected = nullptr;
    class QLabel* myLedgerKeys = nullptr;
    void updateLedger();
    void styleLedger();
    QString myFilterText;
    void setFilter(const QString& text);
    bool rowMatches(const Row& row) const;
    // The ONE place a row's name is coloured (this) and worded (the next) -
    // plain text, or the filter's escaped highlight - each a no-op for a row
    // that already reads right, so an unchanged row costs nothing.
    void styleNameColour(Row& row);
    // The words half: plain name or escaped highlight. Returns whether the
    // label still differs from what it should say; writes it only if `apply`.
    bool styleNameWords(Row& row, bool apply);
    // Writes the words applyRowVisibility() left for the event loop (rows
    // that could not be on screen at the next paint). ensureFresh() runs it
    // too, so no reader ever sees them stale.
    void flushNameWords();
    bool myWordsPending = false;
    QColor nameColour(const Row& row) const;

    // A pick that did NOT come from this drawer reveals its row; one that did
    // must not scroll the list under the hand that made it.
    bool myRequestingSelection = false;
    // The selection the last showSelection() saw; a reveal is for a body that
    // is new against THIS, never against what a rebuild re-applied.
    std::vector<int> myPickBaseline;
    void requestSelection(const std::vector<int>& ids);
    void applySelection(const std::vector<int>& ids);
    void reveal(int bodyId);
    void scrollToRow(int id);
    QWidget* myFoot = nullptr;
    class QLabel* myFootText = nullptr;
    class QPushButton* myFootClear = nullptr;
    int myHiddenPickId = 0;
    void showHiddenPick(int bodyId);
    void hideHiddenPick();

    // --- reuse across rebuilds (review ruling R5a) -----------------------------
    // refresh() keys the rows it had by kind and id, and addItemRow()/
    // addGroupRow() take a row back out of that map instead of building one
    // whenever their item already had a row - so a rebuild writes only what
    // changed and creates a widget only for a new item.
    static qint64 rowKey(RowKind kind, int id);
    static qint64 rowKey(const Row& row);
    bool takeReusable(RowKind kind, int id, Row& out);
    std::unordered_map<qint64, Row>* myReusable = nullptr;   // live only inside refresh()
    static void setGrainLook(class QPushButton* grain, bool across);
    // The empty state's label, while the list is empty - null otherwise.
    class QLabel* myEmpty = nullptr;
    // A closed drawer marks itself stale instead of rebuilding; any reader
    // (setVisible(true), showEvent(), a suite accessor) brings it up to date.
    // A live DRAG marks it stale too, and nothing brings it up to date until
    // the drag ends (finishDrag()): the rows hold still under the hand.
    bool myRowsDirty = false;
    bool myForceBuild = false;
    void flushPendingRefresh();
    void ensureFresh() const;
    // What the rows currently say, so refresh() can tell a call that would
    // rebuild them identically from one that would not. See refresh().
    QString myRowSignature;
    bool myRowsBuilt = false;

    // The two things a row can be marked for, remembered so either can be
    // restyled without the caller having to re-supply the other. Both are
    // pushed in - the viewport's selection through showSelection(), the
    // pending outline through showPendingOutline() - and restyleRows() is the
    // one place either turns into a stylesheet.
    std::vector<int> mySelectedIds;
    int myPendingOutlineId = 0;
    void restyleRows();

    // --- folders (improvements item 10) --------------------------------------
    // Which folders the user has OPENED. A set of the open ones, not the
    // closed ones, because folders start FOLDED (the user's own call): a
    // drawer whose folders all opened themselves is the long list folders
    // exist to shorten. A folder nothing has an opinion about yet is
    // therefore closed, and stays closed until its twisty is clicked.
    std::set<int> myExpanded;
    // Where a Shift-range starts: the last body row clicked without Shift.
    int myAnchorRowId = 0;
    // The selection a click asks for, from the modifiers it was made with.
    std::vector<int> selectionFor(int id, Qt::KeyboardModifiers mods) const;
    // The context menu the right button opens over the rows: populateRowMenu()
    // then exec(). Every kind of row passes its own id - an outline row once
    // passed 0, which left its Rename disabled.
    void showRowMenu(const QPoint& globalPos, int rowId, RowKind kind);
    // What the menu's moves carry for this row: the selection when the row is
    // a selected body, the row alone otherwise.
    std::vector<int> idsForMenu(int rowId, RowKind kind) const;

    // --- the keyboard (T-063) -------------------------------------------------
    // The focused row. Set by every route that selects a row from this list
    // (activateRow()) and by a viewport pick the list reveals.
    int myCurrentId = 0;
    RowKind myCurrentKind = RowKind::Body;
    // Whether the list itself - not a child such as the filter field or a
    // rename editor, whose ignored keys propagate up here - holds focus. Read
    // off the window rather than hasFocus(), which is false in any window
    // that is not the OS-active one.
    bool holdsFocus() const;
    // The keys the list owns while it holds focus - listKeys()' list-scope
    // entries: arrows, Shift+Up/Down, Alt+arrows, Home, End, F2. Never a letter.
    bool claimsKey(const QKeyEvent* key) const;
    int visibleIndexOf(int id) const;
    void stepTo(int visibleIndex, Qt::KeyboardModifiers mods);
    void foldOrParent();
    void unfoldOrChild();
    void renameCurrent();
    // Alt+Up/Down/Left/Right, through the one move signal.
    void moveByCommand(ListCommand command);
    // Ruling D1: the folder directly above `id` among its siblings as the
    // list SHOWS them - the previous visible row at the same depth, with no
    // shallower row between - or 0 when that row is not a folder.
    int folderAbove(int id) const;
    // Ruling D2: Down or Enter in the filter field.
    void enterListFromFilter();
    // Finding F7: the current row, told to a screen reader - Focus, then
    // Selection (SelectionAdd for a Shift step).
    void announceCurrent(bool extend);
    Row* rowById(int id);
    // Builds `groupId`'s children into the rows, depth first. Recursive: the
    // tree nests to any depth, and a loop with an explicit stack would be the
    // same thing written longer.
    void addItemRow(int id, const QString& itemName, bool visible, bool isOutline, int depth,
                    int parentId);
    void addGroupRow(int groupId, const QString& groupName, int depth, int parentId);
    void buildGroupRows(int groupId, int depth);
    // The press that MAY become a drag. A row selects on RELEASE (the graft
    // from W): a press that starts a drag must not change the selection, or
    // organising the list would cancel whatever gesture is live in the
    // viewport. myDragging only becomes true once the pointer has travelled
    // kDragThresholdPx - a press that never moves is an ordinary click.
    int myPressId = 0;
    RowKind myPressKind = RowKind::Body;
    Qt::KeyboardModifiers myPressMods = Qt::NoModifier;
    bool myPressArmed = false;
    // Every row the drag carries: a row already part of a multiple selection
    // carries the selection; any other row carries itself.
    std::vector<int> myDragIds;
    QPoint myDragStart;
    bool myDragging = false;

    struct DropTarget {
        DropKind kind = DropKind::None;
        int rowIndex = -1;          // into myRowList
        bool lineAtBottom = false;  // the line is drawn under the row, not over it
        int parent = 0;
        int beforeId = 0;           // DocumentModel's: sibling, 0 = end, kIntoFolder
        int depth = 0;              // the line's indent
        bool refused = false;
        bool sameSpot(const DropTarget& other) const
        {
            return kind == other.kind && rowIndex == other.rowIndex &&
                   lineAtBottom == other.lineAtBottom && parent == other.parent &&
                   beforeId == other.beforeId;
        }
    };
    DropTarget myDropTarget;
    // Geometry only - which target the point (THIS card's coordinates) is
    // over, with the 4 px hysteresis read off myDropTarget. Only the part of
    // the list the scroll viewport SHOWS is read (ruling P10): a row scrolled
    // out of sight keeps its geometry under the filter field. Refusal is
    // decided by the dry run in updateDrag().
    DropTarget dropTargetAt(const QPoint& pos) const;
    void showDropTarget(const DropTarget& target);
    class DropIndicator* myIndicator = nullptr;
    bool myTitleLit = false;
    bool myTitleRefused = false;
    void setTitleLit(bool lit, bool refused);
    void styleTitle();
    // The folder count's look - accentText words, or a theme's badge.
    static QString countSheet();
    // The grain mark before the eye in a themed shell, after it in Graphite.
    void orderRowButtons(Row& row);
    void activateRow(int id, RowKind kind, Qt::KeyboardModifiers mods);
    // activateRow()'s work; activateRow() announces when the keys' row moved.
    void selectRow(int id, RowKind kind, Qt::KeyboardModifiers mods);
    // Whether the row is already part of what is selected - a body selected,
    // the pending outline, a folder whose every body is selected.
    bool rowInSelection(int id, RowKind kind) const;
    void beginDrag(const QPoint& panelPos, const QPoint& globalPos);
    void updateDrag(const QPoint& panelPos, const QPoint& globalPos);
    void finishDrag(bool commit);
    // The app-wide Escape claim, installed ONLY while a drag is live
    // (ExtrudePreview's shape through KeyClaim). Its own object, so the
    // app-wide filter never runs this panel's row filter a second time.
    class DragKeyClaim;
    DragKeyClaim* myKeyClaim = nullptr;
    class DragGhost* myGhost = nullptr;
    QString myDragStatus;
    bool myDragOutside = false;
    void setDragStatus(const QString& text);
    // A row this drag carries, or one inside a folder it carries.
    bool isCarried(const Row& row) const;
    // Spring-loading: a folded folder hovered for 700 ms opens (and stays
    // open). Auto-scroll: a hand held in the list's top or bottom band
    // scrolls it. Both re-aim the drag from where the hand last was. Both
    // timers stop on every route a drag ends by (finishDrag()).
    class QTimer* mySpringTimer = nullptr;
    int mySpringFolder = 0;
    class QTimer* myAutoScrollTimer = nullptr;
    int myAutoScrollStep = 0;
    QPoint myLastDragPos;
    QPoint myLastDragGlobal;
    void springOpen();
    void updateAutoScroll(const QPoint& panelPos);
    void autoScrollTick();
    // A refresh asked for mid-drag is DEFERRED to the drag's end, through the
    // same myRowsDirty flag a closed drawer uses - one pending mark, not two.
    // A rebuild under a live drag renumbers the rows and forgets the target,
    // so the ring under a still hand would vanish; finishDrag() runs it.
};
