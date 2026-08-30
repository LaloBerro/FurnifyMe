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
    ItemsPanel(const DocumentModel* document, OcctViewWidget* view, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const { return static_cast<int>(myRowWidgets.size()); }
    // The name and dimension text painted on one row, concatenated - a read
    // accessor for the suite, which is preferable to it walking this panel's
    // child widgets itself. Empty for an out-of-range index.
    QString rowTextAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(myRowTexts.size()) ? myRowTexts[index]
                                                                          : QString();
    }

    // Highlights the rows for these solids. Called when the viewport selection
    // changes, so the two views of the document never disagree.
    void showSelection(const std::vector<int>& ids);

signals:
    void solidActivated(int id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Out of line: it asks the layout for a height at this card's fixed
    // width, because the empty-state message word-wraps and a QLayout's plain
    // sizeHint does not account for that.
    QSize sizeHint() const override;

private:
    const DocumentModel* myDocument = nullptr;
    OcctViewWidget* myView = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRows = nullptr;
    std::vector<QWidget*> myRowWidgets;   // parallel to the document's solids
    std::vector<int> myRowIds;
    std::vector<QString> myRowTexts;      // parallel too - what rowTextAt() reports
};
