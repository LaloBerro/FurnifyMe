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
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <set>
#include <vector>

class DocumentModel;
class OcctViewWidget;
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
    int rowCount() const { return static_cast<int>(myRowList.size()); }
    // The name painted on one row - a read accessor for the suite, which is
    // preferable to it walking this panel's child widgets itself. No
    // dimension is painted on a row any more (sizes moved to the viewport's
    // own selection-sizes drawing, see CLAUDE.md's "Direct modeling" section);
    // this reports the name alone. Empty for an out-of-range index.
    QString rowTextAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size()) ? myRowList[index].text
                                                                       : QString();
    }
    // The row's own QWidget, id and whether it is an outline row - a read
    // accessor for the suite, on the same terms rowTextAt() is: it lets a
    // test hit-test the REAL widget with childAt()/doubleClickAt() rather
    // than sending an event straight at a pointer it merely hopes is
    // reachable (CLAUDE.md's childAt()-vs-sendEvent trap). Null/0/false for
    // an out-of-range index.
    QWidget* rowWidgetAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size())
                   ? myRowList[index].widget
                   : nullptr;
    }
    int rowIdAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size()) ? myRowList[index].id
                                                                        : 0;
    }
    // The grain mark on a BODY row, or null for an outline or a folder row
    // (neither carries one - see addItemRow()). Asked of the panel rather
    // than found by scanning for a button of the right shape, which is the
    // failure this file has already been bitten by twice.
    class QPushButton* grainMarkAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size())
                   ? myRowList[index].grain
                   : nullptr;
    }
    bool rowIsOutlineAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size())
                   ? myRowList[index].isOutline
                   : false;
    }
    // Folder rows (improvements item 10) - the suite asks the same way it
    // asks about outline rows, and for the same reason: a row's kind decides
    // what a click on it means.
    bool rowIsGroupAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size())
                   ? myRowList[index].isGroup
                   : false;
    }
    // How deep in the tree the row sits: 0 at the document's root.
    int rowDepthAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size())
                   ? myRowList[index].depth
                   : 0;
    }

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
    // It wears the SAME accent inset a selected body row wears. Two outlines
    // in the drawer and no mark on either is a choice the user cannot see -
    // and clicking a row is how that choice is made, so the row is exactly
    // where the feedback belongs.
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
    // A row was dragged onto a folder row (or onto empty space, which is the
    // root): `targetGroupId` is 0 for the root. MainWindow checkpoints and
    // performs the move - this panel only ran the gesture, exactly as it does
    // for a rename.
    void itemsDropped(std::vector<int> ids, int targetGroupId);
    // The + button in the title row, and the right-click menu's own entry:
    // make a folder out of whatever is selected (or an empty one when nothing
    // is).
    void newFolderRequested();
    // The right-click menu's other two, each the same route the Model menu
    // takes rather than a second implementation.
    void ungroupRequested();
    // The folder row's own Delete: the folder AND every body in it, which is
    // the one thing Ungroup deliberately does not do. Reported rather than
    // performed - MainWindow owns the checkpoint and the toast that carries
    // Undo, exactly as it does for a rename.
    void deleteGroupRequested(int groupId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Out of line: it asks the layout for a height at this card's fixed
    // width, because the empty-state message word-wraps and a QLayout's plain
    // sizeHint does not account for that.
    QSize sizeHint() const override;

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
    static int cardWidth();

    // One row's widgets, kept so applyTheme() can restyle in place. Replaces
    // the four parallel vectors this held before, which had to be indexed in
    // lockstep by every reader.
    struct Row {
        QWidget* widget = nullptr;
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
        class QPushButton* twisty = nullptr;
        QString text;      // what rowTextAt() reports
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
    // The + in the title row: one click makes a folder. It replaced the hover
    // x this card used to carry - the drawer is closed from its rail chip and
    // its menu entry, and the corner is worth more as the one control this
    // list actually needs.
    class QPushButton* myNewFolder = nullptr;
    // The tallest the card may grow before the rows start scrolling: a share
    // of the VIEWPORT's own height, read live, so the answer follows the
    // window rather than a number picked on one screen. Zero until the card
    // has a parent to ask.
    int rowsHeightCap() const;
    // The document's outlines first, then its bodies - the order the rows are
    // built in, which is what rowTextAt(index) reports against.
    std::vector<Row> myRowList;
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
    // The context menu the right button opens over the rows.
    void showRowMenu(const QPoint& globalPos, int rowId, bool isGroupRow);
    // Builds `groupId`'s children into the rows, depth first. Recursive: the
    // tree nests to any depth, and a loop with an explicit stack would be the
    // same thing written longer.
    void addItemRow(int id, const QString& itemName, bool visible, bool isOutline, int depth);
    void addGroupRow(int groupId, const QString& groupName, int depth);
    void buildGroupRows(int groupId, int depth);
    // The drag that moves a row into a folder. myDragId is what is being
    // dragged (0 = nothing), myDragStart where the press landed, and
    // myDragging only becomes true once the pointer has travelled - a press
    // that never moves is an ordinary click and must stay one.
    int myDragId = 0;
    bool myDragIsGroup = false;
    // Every row the drag is carrying. A drag that starts on a row which is
    // part of a multiple selection carries the whole of it - which is what
    // "grab them and insert them into a folder" means - and one that starts
    // anywhere else carries just that row.
    std::vector<int> myDragIds;
    QPoint myDragStart;
    bool myDragging = false;
    // The folder row the pointer is over mid-drag, so it can be marked - 0
    // for the root (anywhere else in the card). Read by restyleRows().
    int myDropTarget = 0;
    // Which folder a point in THIS panel's coordinates would drop into: a
    // folder row answers itself, an item row answers the folder it sits in,
    // and anything else is the root.
    int dropTargetAt(const QPoint& pos) const;
};
