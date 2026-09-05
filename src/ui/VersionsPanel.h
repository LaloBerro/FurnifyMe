#pragma once
//
// Lists a furniture's saved versions as a gallery of large cards - the
// picked mockup (Task 1.2, "Option B — large cards"): a right-edge drawer,
// same ItemsPanel drawer idiom as before (a floating, opaquely-painted card
// parented to the viewport, anchored beside the rail through
// ViewportOverlay::Anchor::TopLeft). Its visibility is DERIVED from
// MainWindow's View -> Versions action, both directions, exactly as the
// items drawer's is; nothing else may show or hide it.
//
// Each SAVED version is its own card: a full-width thumbnail on top (a
// flat, neutral placeholder block when the version has none - see
// FurnitureStore::versionThumbPath()), a name bar underneath (the version's
// own name, weight 500, with its saved date/time under it in muted text),
// and three small bordered buttons - Compare, Restore, Delete - at the
// bar's right that are hidden at rest and shown only while the card is
// hovered (see the eventFilter() override, which watches QEvent::Enter/
// Leave on each card).
//
// Task 1.1's SaveVersionCard is retired. The header's accent-bordered +
// button (addButton()) is the one way to make a new version now, and
// MainWindow::onSaveVersion() (File -> Save version..., still a real
// action/menu entry) is wired to the SAME beginNewVersion() the button
// calls - one gesture, reached two ways, never two implementations of it.
// beginNewVersion() inserts a PENDING card - a thumbnail placeholder plus a
// name bar already carrying InlineRename's live edit, pre-filled with a
// fresh default name and selected - directly above the real rows, and
// writes NOTHING to FurnitureStore yet. Enter is what actually persists
// (through MainWindow::saveVersion(), the same call the old menu action
// made); Escape - or this panel's own auto-cancel, mirroring
// SaveVersionCard::onAppStateChanged()'s reasoning, whenever
// MainWindow::canOpenSaveVersion() goes false while the card is open -
// simply tears the pending card down. Nothing was ever saved, so nothing
// has to be deleted: "no unnamed version left behind" is true by
// construction rather than by a cleanup step.
//
// Reads MainWindow/FurnitureStore rather than owning anything: a version
// list is file data, not document state, so there is no "VersionsModel" to
// hold - refresh() re-reads FurnitureStore::versions(currentFurnitureId())
// on every MainWindow::appStateChanged(), the same signal ItemsPanel's own
// refresh() already follows.
//
// Each real row carries the same two-click Delete as before - a second
// click within kDeleteConfirmMs of the first ("Delete — click again") -
// because a version is file data with no Undo once it is gone. The row's
// own QTimer disarms that confirmation on its own if the second click never
// comes; deleteArmedMsFor() exposes the live countdown so a test can assert
// the armed timer rather than waiting kDeleteConfirmMs real milliseconds.
//
// A version's NAME is USER TEXT, exactly as a furniture's own name is (see
// InitScreen's precedent) - it is painted on every card but deliberately
// EXCLUDED from anything the vocabulary sweep walks, so a name spelled with
// one of CLAUDE.md's banned words is not a bug the sweep can even see: the
// card shows the user's own words unmangled, and the sweep has nothing to
// trip on because it never reads them in the first place.
#include <QDateTime>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class MainWindow;
class OcctViewWidget;
class QLabel;
class QLineEdit;
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

    // The row's own card widget - the whole thumbnail+bar card, not just
    // one control inside it. Two uses: real hover events
    // (QEvent::Enter/Leave) can be delivered straight at it, the same
    // pattern CLAUDE.md's hover precedent (ToolChip, AppBar's BarButton)
    // uses, and a test can hit-test it with childAt() rather than merely
    // asserting it exists.
    QWidget* cardAt(int index) const;
    // The thumbnail's own rect, in cardAt(index)'s LOCAL coordinates - what
    // a renderExact(cardAt(index)) capture's pixels line up against.
    // Rendering the card rather than the thumbnail label alone is
    // deliberate: the label paints nothing of its own for the placeholder
    // case (a transparent QLabel with no pixmap), and it is the CARD's own
    // paintSurface() panel fill showing through that IS the flat placeholder
    // block - rendering the label in isolation would show nothing at all,
    // proving neither case.
    QRect thumbnailRectAt(int index) const;
    // The name label's own rect, in cardAt(index)'s LOCAL coordinates - the
    // same mapping thumbnailRectAt() uses, so a test can compare it
    // directly against a button's own mapTo(cardAt(index), ...) rect
    // (fix round 2 - the review's own ask: prove a hover button's geometry
    // never overlaps the name it sits beside).
    QRect nameRectAt(int index) const;

    QPushButton* compareButtonAt(int index) const;
    QPushButton* restoreButtonAt(int index) const;
    QPushButton* deleteButtonAt(int index) const;

    // The header's + control - reachable by childAt(), the same
    // childAt()-vs-sendEvent law CLAUDE.md documents for every other
    // control this shell paints over the viewport. Its own click handler
    // and MainWindow::onSaveVersion() both call beginNewVersion() below -
    // one implementation, two entry points.
    QPushButton* addButton() const { return myAddButton; }

    // Starts the create-a-version gesture (see the class comment for the
    // full contract: nothing is persisted until Enter, so Escape - or this
    // panel's own auto-cancel inside refresh() - genuinely has nothing to
    // undo). A no-op while a create is already open, while no furniture is
    // open, or while MainWindow::canOpenSaveVersion() is false for any of
    // its own reasons (sketching, render mode, a live pull/bevel gizmo) -
    // the same guard the button's own enabled state already shows.
    void beginNewVersion();

    // Derived, not set: this panel's visibility already follows
    // MainWindow's View -> Versions action (see the class comment), and
    // that toggle handler calls setVisible() directly rather than going
    // through updateActions()/appStateChanged - so canOpenSaveVersion()'s
    // own auto-cancel inside refresh() never runs on a plain drawer close.
    // Overridden here so "the drawer stopped being shown" cancels a pending
    // create by itself, the same law every other exit from this gesture
    // already obeys, rather than leaving a half-named card alive-but-hidden
    // until the drawer reopens. hideEvent() below is the same rule's
    // backstop for a hide this widget's own setVisible() is never actually
    // called for (an ANCESTOR hiding, which Qt delivers as a QHideEvent
    // straight to the child rather than by calling the child's setVisible())
    // - CLAUDE.md's own warning that hide() does not always mean a
    // QHideEvent arrives cuts the other way too: a widget CAN be hidden by
    // a route setVisible() never sees, so the cancel cannot live in only
    // one of the two.
    void setVisible(bool visible) override;

    // The live "second click still armed" countdown for the row named
    // `name`, in ms, or -1 when that row is not armed (or does not exist).
    // ToastHost::remainingMs()'s own shape, for the same reason: a test
    // asserts the ARMED timer rather than waiting kDeleteConfirmMs real
    // milliseconds for it to disarm.
    int deleteArmedMsFor(const QString& name) const;

    // Every FIXED string this panel paints - the title, the + button's
    // tooltip, the empty-state message, and the two Delete-button labels
    // ("Delete" / "Delete — click again"). Deliberately NOT a card's
    // version name or its saved-date text, and NOT the pending card's
    // default name either (it is exactly as much the user's own word
    // choice as a committed version's name - the user is free to leave it
    // standing or type over it, and either way it is what gets saved) -
    // see the class comment. gui_smoke's banned-word sweep walks this
    // list, the same contract ItemsPanel, WalkthroughPanel, Toast and the
    // rest already keep.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;
    // Watches every real card for QEvent::Enter/Leave (see buildRealRow())
    // and toggles that card's own action row's visibility - the "revealed
    // on row hover only, hidden at rest" half of the mockup contract.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The backstop half of setVisible()'s own cancel-on-hide rule above -
    // see its comment for why both exist.
    void hideEvent(QHideEvent* event) override;

private:
    void applyTheme();
    static int cardWidth();
    // The width a card's own content (its thumbnail, its bar) actually has
    // to work with - cardWidth() minus this panel's own outer padding.
    static int cardInnerWidth();
    // The thumbnail's fixed height for a card of thumbWidth logical pixels
    // wide - a 16:10-ish aspect, rounded up to a whole device-pixel count
    // (Theme::wholeDevicePixels()) so this literal, fixed dimension keeps
    // the same law every other floating card's fixed sizes already follow.
    static int thumbHeightFor(int thumbWidth);
    // The one place either Delete label is spelled out - paintEvent() draws
    // nothing itself here (the label lives on the QPushButton), but
    // paintedTexts() and the row-building code both call these, so the
    // sweep can never be guarding a different copy than what is on screen.
    static QString deleteLabel();
    static QString deleteArmedLabel();

    // "Version 1", "Version 2", ... - scanned from what this furniture's
    // versions() actually holds (the highest existing "Version N" plus
    // one), FurnitureStore::nextFurnitureName()'s own reasoning applied
    // locally rather than added to that class: a deleted-and-recreated
    // version never collides with a name still on screen, and this task's
    // own file list does not touch FurnitureStore.
    QString nextVersionDefaultName() const;

    void buildRealRow(const QString& name, const QDateTime& saved, bool enabled);
    QWidget* buildPendingCard(const QString& defaultName);
    // Opens (or re-opens) InlineRename on the pending card's own name
    // label, seeded with `seedText` and selected. Factored out of
    // beginNewVersion() because commitPendingCreate() needs the exact same
    // gesture a second time on a duplicate-name refusal - InlineRename's
    // own edit always destroys itself once Enter is pressed, success or
    // not, so retrying means opening a fresh one rather than reusing the
    // old.
    void openPendingNameEdit(const QString& seedText);
    // Enter's path: persists through MainWindow::saveVersion() (the exact
    // call the old menu action made). On success, tears the pending card
    // down; on a duplicate-name refusal (MainWindow has already shown the
    // Failure toast naming the clash), the card stays up and
    // openPendingNameEdit() reopens so the user can retype - the exact
    // behaviour SaveVersionCard used to offer on this same refusal.
    void commitPendingCreate(const QString& name);
    // Escape's path, and refresh()'s own auto-cancel: tears the pending
    // card down with no call to FurnitureStore at all - there is nothing
    // to undo, because nothing was ever written.
    void discardPendingCreate();
    void teardownPendingCard();
    // Escape delivery, belt-and-suspenders: InlineRename's own QShortcut
    // (Qt::WidgetWithChildrenShortcut, private to InlineRename.cpp) is the
    // documented mechanism and is what a real keypress goes through. This
    // panel ALSO watches the pending edit's raw QEvent::KeyPress for Escape
    // in eventFilter() below - a plain eventFilter, not a second QShortcut,
    // so the two can never register as ambiguous with each other or with
    // anything else this app binds to plain Escape (Cancel Sketch's own
    // QAction, in particular). The two cannot double-fire either: if the
    // shortcut map consumes the real keypress first, this filter never
    // sees it at all - QApplication::notify() returns before normal event
    // delivery reaches installed filters.

    struct Row {
        QWidget* widget = nullptr;     // the whole card - thumbnail + bar
        QLabel* thumb = nullptr;
        QLabel* name = nullptr;
        QLabel* meta = nullptr;
        QWidget* actions = nullptr;    // Compare/Restore/Delete - hidden at rest
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
    QPushButton* myAddButton = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRowsLayout = nullptr;
    // The in-progress create gesture's own card, or null when none is open.
    // Lives OUTSIDE myRowsLayout (inserted straight into myOuter, above it)
    // so refresh()'s ordinary row-rebuild - which tears down and replaces
    // everything IN myRowsLayout wholesale - can never touch it by
    // accident; the only thing that ever removes it is
    // commitPendingCreate()/discardPendingCreate() themselves.
    QWidget* myPendingWidget = nullptr;
    // The pending gesture's own live edit, watched by eventFilter() for a
    // raw Escape keypress - see teardownPendingCard()'s own comment on the
    // header for why this exists alongside InlineRename's own shortcut. A
    // QPointer, not a bare pointer: it auto-nulls the instant the edit is
    // destroyed by any OTHER path (Enter's commit, in particular), so
    // eventFilter()'s `watched == myPendingEdit` comparison can never match
    // a dangling pointer.
    QPointer<QLineEdit> myPendingEdit;
    std::vector<Row> myRows;
    QString myRowSignature;   // see ItemsPanel::refresh() for why this exists
    bool myRowsBuilt = false;

    // Disarms `row`'s Delete confirmation and repaints its label back to
    // the plain state - called by the timer's own timeout AND by a rebuild
    // that is about to discard the row entirely.
    void disarmDelete(Row& row);
};
