#pragma once
//
// Lists a furniture's saved versions - ItemsPanel's drawer idiom, not a new
// one: a floating, opaquely-painted card parented to the viewport, anchored
// beside the rail through the same ViewportOverlay::Anchor::TopLeft ItemsPanel
// already uses (entries sharing an anchor stack downward in the order they
// were added, so this lands below the items drawer for free - see
// ViewportOverlay::relayout()). Its visibility is DERIVED from
// MainWindow's View -> Versions action, both directions, exactly as the
// items drawer's is; nothing else may show or hide it.
//
// Reads MainWindow/FurnitureStore rather than owning anything: a version list
// is file data, not document state, so there is no "VersionsModel" to hold -
// refresh() re-reads FurnitureStore::versions(currentFurnitureId()) on every
// MainWindow::appStateChanged(), the same signal ItemsPanel's own refresh()
// already follows.
//
// Each row carries three controls: Compare, Restore, and a Delete that needs
// a SECOND click within kDeleteConfirmMs of the first - "Delete — click
// again" - because a version is file data with no Undo once it is gone, and
// a single accidental click must not be able to remove one. The row's own
// QTimer disarms that confirmation on its own if the second click never
// comes; deleteArmedMsFor() exposes the live countdown so a test can assert
// the armed timer rather than waiting kDeleteConfirmMs real milliseconds.
//
// A version's NAME is USER TEXT, exactly as a furniture's own name is (see
// InitScreen's precedent) - it is painted on every row but deliberately
// EXCLUDED from anything the vocabulary sweep walks, so a name spelled with
// one of CLAUDE.md's banned words is not a bug the sweep can even see: the
// row shows the user's own words unmangled, and the sweep has nothing to
// trip on because it never reads them in the first place.
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class MainWindow;
class OcctViewWidget;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

class VersionsPanel : public QWidget {
    Q_OBJECT

public:
    VersionsPanel(MainWindow* window, OcctViewWidget* view, QWidget* parent = nullptr);

    // How long a first Delete click stays armed before it disarms itself -
    // public so gui_smoke asserts against the real number rather than a
    // second copy of it.
    static constexpr int kDeleteConfirmMs = 4000;

    void refresh();
    int rowCount() const { return static_cast<int>(myRows.size()); }
    // The raw version name at a row - not painted text, just this panel's
    // own bookkeeping key, safe to expose since it is the same string
    // FurnitureStore already handed back (a test driving this panel needs
    // to find "the row for version X" without walking button text that a
    // Delete click changes out from under it).
    QString rowNameAt(int index) const;

    QPushButton* compareButtonAt(int index) const;
    QPushButton* restoreButtonAt(int index) const;
    QPushButton* deleteButtonAt(int index) const;

    // The live "second click still armed" countdown for the row named
    // `name`, in ms, or -1 when that row is not armed (or does not exist).
    // ToastHost::remainingMs()'s own shape, for the same reason: a test
    // asserts the ARMED timer rather than waiting kDeleteConfirmMs real
    // milliseconds for it to disarm.
    int deleteArmedMsFor(const QString& name) const;

    // Every FIXED string this panel paints - the title, the empty-state
    // message, and the two Delete-button labels ("Delete" / "Delete — click
    // again"). Deliberately NOT a row's version name or its saved-date text -
    // see the class comment. gui_smoke's banned-word sweep walks this list,
    // the same contract ItemsPanel, WalkthroughPanel, Toast and the rest
    // already keep.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    void applyTheme();
    static int cardWidth();
    // The one place either Delete label is spelled out - paintEvent() draws
    // nothing itself here (the label lives on the QPushButton), but
    // paintedTexts() and the button-building code both call these, so the
    // sweep can never be guarding a different copy than what is on screen.
    static QString deleteLabel();
    static QString deleteArmedLabel();

    struct Row {
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QLabel* saved = nullptr;
        QPushButton* compare = nullptr;
        QPushButton* restore = nullptr;
        QPushButton* remove = nullptr;
        QString versionName;   // this panel's own key - see rowNameAt()
        QTimer* deleteTimer = nullptr;   // single-shot, kDeleteConfirmMs, armed by the first click
        bool deleteArmed = false;
    };

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;
    QLabel* myTitle = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRowsLayout = nullptr;
    std::vector<Row> myRows;
    QString myRowSignature;   // see ItemsPanel::refresh() for why this exists
    bool myRowsBuilt = false;

    // Disarms `row`'s Delete confirmation and repaints its label back to
    // the plain state - called by the timer's own timeout AND by a rebuild
    // that is about to discard the row entirely.
    void disarmDelete(Row& row);
};
