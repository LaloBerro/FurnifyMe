#pragma once
// Lists the document's solids with a visibility toggle each. Reads the document
// rather than owning it, and is rebuilt when MainWindow announces a change -
// DocumentModel stays free of Qt and cannot emit signals of its own.
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

private:
    const DocumentModel* myDocument = nullptr;
    OcctViewWidget* myView = nullptr;
    QVBoxLayout* myRows = nullptr;
    std::vector<QWidget*> myRowWidgets;   // parallel to the document's solids
    std::vector<int> myRowIds;
    std::vector<QString> myRowTexts;      // parallel too - what rowTextAt() reports
};
