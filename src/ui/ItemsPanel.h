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
//   - It paints its ENTIRE rect, opaquely, in paintEvent(). An unpainted
//     region of a child widget over that surface is not transparent - it is
//     whatever the driver left there, which reads as black. Its own child
//     rows and labels therefore paint no background of their own and let this
//     card show through, rather than each stamping a rectangle of its own
//     colour.
//   - It carries Qt::WA_NoMousePropagation, so a press or release that lands
//     on the drawer never reaches the viewport underneath and re-picks behind
//     it.
//
// Its visibility is DERIVED from MainWindow's existing Items action and never
// set from anywhere else; see MainWindow's constructor.
#include <QSize>
#include <QString>
#include <QWidget>

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
    // The name and dimension text painted on one row, concatenated - a read
    // accessor for the suite, which is preferable to it walking this panel's
    // child widgets itself. Empty for an out-of-range index.
    QString rowTextAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowList.size()) ? myRowList[index].text
                                                                       : QString();
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

signals:
    void solidActivated(int id);
    // An outline row was clicked. A separate signal rather than one id
    // channel with a kind flag: the two do genuinely different things -
    // a body row changes the viewport selection, an outline row changes which
    // outline Extrude will consume - and a receiver that had to branch on a
    // flag could get the branch wrong in a way the compiler could not see.
    void outlineActivated(int id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Out of line: it asks the layout for a height at this card's fixed
    // width, because the empty-state message word-wraps and a QLayout's plain
    // sizeHint does not account for that.
    QSize sizeHint() const override;

private:
    // Restyles what is on screen from the live Theme - the title's stylesheet,
    // each row's two label stylesheets, each eye button's rasterised icon -
    // and re-derives the card's width for the current type scale.
    //
    // It does NOT rebuild the rows, and that is the point. It used to call
    // refresh(), which tears every row down and constructs it again; a theme
    // edit fires once per mouse MOVE inside the colour picker, so a drag was
    // destroying and rebuilding the whole list per frame. Nothing about a
    // theme change alters WHICH bodies exist - only how they are painted -
    // so the rebuild belongs to documentChanged and the restyle belongs here.
    void applyTheme();
    // The card's width for the current type scale: the shipped 240, plus
    // however much wider a specimen row measures now than it did under
    // defaultSpec(). Exactly 240 at the shipped look, by construction, and
    // stable against the actual body names - a drawer that resized itself to
    // the longest name would move the viewport's usable area around under
    // the user, which is the reason this card was fixed-width to begin with.
    static int cardWidth();

    // One row's widgets, kept so applyTheme() can restyle in place. Replaces
    // the four parallel vectors this held before, which had to be indexed in
    // lockstep by every reader.
    struct Row {
        QWidget* widget = nullptr;
        class QLabel* name = nullptr;
        class QLabel* size = nullptr;
        class QPushButton* eye = nullptr;
        int id = 0;
        // Outline rows come first and are not part of the viewport selection -
        // showSelection() must not highlight one, and the eye toggles a
        // different channel. Carried on the row rather than derived by asking
        // the document again, so a row can never be styled as one kind and
        // toggled as the other.
        bool isOutline = false;
        QString text;      // what rowTextAt() reports
    };

    DocumentModel* myDocument = nullptr;
    OcctViewWidget* myView = nullptr;
    class QLabel* myTitle = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRows = nullptr;
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
};
