#pragma once
//
// The furniture picker: FurnifyMe's own top-level window, shown alone at
// boot and again every time the editor (MainWindow) hands control back -
// Milestone 4's split into two windows. Never touches OCCT at all - a
// furniture's thumbnail is a PNG FurnitureStore already wrote, not a live
// render, so this window can exist, refresh and be driven entirely without
// a GL surface, a V3d_View, or any of OcctViewWidget's lazy-init contract.
// That is deliberate, not incidental: the whole point of splitting the
// picker out is that it must be safe to show before the editor's viewer has
// ever been realized.
//
// The picked design (Milestone 5's Gallery round, "A, but the list working
// as a grid max 3x3 and if it bigger get scroll to the bottom", reworking
// Milestone 4's Option B "Shelf"): a header row (title left; a live Search
// field and the Recent/Name sort chips right), then a grid of landscape
// 16:10 preview cards CAPPED AT THREE COLUMNS - a wider window buys bigger
// breathing room, never a fourth column, and everything past three rows
// scrolls vertically. The grid's permanent FIRST cell is the dashed
// "+ New furniture" card (still the ONLY way to create one, and still the
// QPushButton newFurnitureButton() promises); the name and last-edited
// date sit BELOW each furniture card (not painted on it, unlike the
// retired InitScreen). Hovering a card swaps its border to accent() and
// swaps its under-row's date for two small bordered buttons, Rename and
// Delete - reusing VersionsPanel's own hover-reveal and two-click-confirm
// idioms, because both are already this app's established shape for "an
// action that should not clutter a card at rest".
//
// Talks to FurnitureStore ALONE for data - enumerate, create, rename,
// delete, and thumbnails as plain PNG paths a QPixmap loads directly. It
// owns no MainWindow, no DocumentModel and no undo stack: the handoff
// itself (hide this, open a MainWindow on the chosen id, show that instead)
// is main.cpp's wiring - this window only ever reports intent through its
// two signals below, and performs its OWN FurnitureStore calls for create/
// rename/delete, reporting either outcome (never silently) through its own
// inline failure banner - it has no OcctViewWidget to hang a ToastHost off.
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class FurnitureStore;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTimer;

class SelectorWindow : public QWidget {
    Q_OBJECT

public:
    // `store` is owned by the caller (MainWindow, in the real app - see
    // main.cpp) and must outlive this window; the same injection discipline
    // FurnitureStore.h's own header documents for MainWindow itself.
    explicit SelectorWindow(FurnitureStore& store, QWidget* parent = nullptr);

    // Rebuilds every card from store.listFurniture() - called on
    // construction and whenever the library might have changed under it
    // (shown again after the editor hands back, a rename/delete/create
    // commits here).
    void refresh();

    int furnitureCount() const { return static_cast<int>(myCards.size()); }
    // The whole grid cell - the thumbnail card plus the under-row beneath it
    // - for gui_smoke's real childAt()/hover probes, the same discipline
    // InitScreen's own cardAt() already established.
    QWidget* cardAt(int index) const;
    QString cardName(int index) const;
    QString cardFurnitureId(int index) const;
    // The bordered thumbnail itself, in `cardAt(index)`'s local coordinates
    // - a plain click here is what opens the furniture (see the class
    // comment: "plain click on a card opens it"). Its own border colour is
    // what swaps to Theme::accent() on hover.
    QWidget* thumbnailAt(int index) const;
    QPushButton* renameButtonAt(int index) const;
    QPushButton* deleteButtonAt(int index) const;
    // The under-row's date label - hidden, not destroyed, the instant the
    // cell is hovered (see renameButtonAt()/deleteButtonAt()), so a test can
    // assert the swap in both directions.
    QLabel* dateLabelAt(int index) const;

    // Still a QPushButton by contract - but since the Gallery redesign
    // (Milestone 5, "A, but the list working as a grid max 3x3") it is the
    // grid's own FIRST cell, a dashed accent card, rather than a header
    // button. Every caller that clicks it (the handoff wiring, gui_smoke's
    // childAt-real probes) keeps working untouched.
    QPushButton* newFurnitureButton() const { return myNewButton; }

    // The Gallery header's own controls: search filters cards by name as
    // you type (a filtered card is hidden, never rebuilt), and the two sort
    // chips are one exclusive pair - Recent (newest edit first, the
    // default) or Name.
    QLineEdit* searchField() const { return mySearch; }
    QPushButton* sortRecentButton() const { return mySortRecent; }
    QPushButton* sortNameButton() const { return mySortName; }
    bool sortedByName() const { return mySortByName; }
    // How many furniture cards the live search leaves on screen. The New
    // card is not counted - creating is never filtered away.
    int visibleCardCount() const;

    // How long a first Delete click on a card stays armed before it disarms
    // itself - VersionsPanel::kDeleteConfirmMs's own shape and value, so a
    // test asserts the real armed timer rather than waiting it out.
    static constexpr int kDeleteConfirmMs = 4000;
    int deleteArmedMsFor(int index) const;

    // Drives the same InlineRename gesture the Rename button's own click
    // handler does - exposed so a test can reach it without simulating the
    // hover first, InitScreen::beginRenameAt()'s own shape.
    void beginRenameAt(int index);

    // The live inline-failure banner's text, or empty when nothing is
    // showing - ToastHost::currentText()'s own shape, for the same reason:
    // this window has no OcctViewWidget to hang a real ToastHost off, so a
    // create/rename/delete refusal is reported through its own small,
    // opaque banner instead (see the .cpp).
    QString currentFailureText() const;
    bool failureVisible() const;

    // Every string this window paints itself - furniture NAMES are
    // deliberately excluded, the same isUserData exemption InitScreen's own
    // paintedTexts() already established (a furniture literally named
    // "Fuse My Table" is the owner's own word choice, not this app's copy).
    // Every failure banner message actually shown this run is included,
    // Toast::paintedTexts()'s own honest-limit shape.
    QStringList paintedTexts() const;

signals:
    // An existing card was opened by a plain click.
    void furnitureChosen(const QString& id);
    // "+ New furniture" was clicked. This window creates the furniture
    // itself (see the class comment - it owns every FurnitureStore call)
    // and, on success, emits furnitureChosen() for the id it just made -
    // opening a freshly created furniture is not a different gesture from
    // opening an existing one, from main.cpp's point of view, so one
    // connection handles both. This signal fires first, purely as an
    // observable "the user asked for one", and carries no id of its own.
    void createRequested();

    // This window is closing - the native X, or a real close() call. This
    // class knows nothing about quitting the application; it only reports
    // the gesture, exactly as it reports everything else through a signal
    // rather than acting on another object directly. EditorSelectorHandoff::
    // wire() (src/EditorSelectorHandoff.h) is what turns this into the
    // app's one honest quit gesture - see its own header for why that
    // matters (Milestone 4's fix-round-1 CRITICAL finding: two unparented
    // top-level windows and Qt's default quitOnLastWindowClosed() do not
    // mix safely with a handoff that hides one before showing the other).
    void closing();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    struct Card {
        QString id;
        QWidget* widget = nullptr;   // the whole cell - see cardAt()
        // Carried here for the sort chips and the search filter, so a
        // reorder or a filter pass never rebuilds a widget - relayoutCards()
        // sorts a VIEW over these, and myCards itself keeps the store's own
        // order, which is what every *At(index) accessor answers in.
        QString name;
        QDateTime lastEdited;
    };

    void rebuildCards();
    void relayoutCards();
    QWidget* buildCard(const QString& id, const QString& name, const QString& thumbPath,
                       const QDateTime& lastEdited);
    void applyTheme();
    // Both refusals FurnitureStore can hand back from here - a library that
    // will not take a new directory, a rename or delete landing on a
    // furniture whose files are gone. Shown as this window's own inline
    // failure banner rather than a Toast: this window never touches
    // OcctViewWidget, which is what ToastHost is built around. Never
    // silent, per CLAUDE.md's never-lie-about-saving/never-silent-failure
    // law - the same law InitScreen's own two refusal signals existed to
    // satisfy, moved in-window since there is no MainWindow toast to route
    // them through here.
    void showFailure(const QString& text);

    FurnitureStore& myStore;
    QLabel* myTitle = nullptr;
    QPushButton* myNewButton = nullptr;
    QLineEdit* mySearch = nullptr;
    QPushButton* mySortRecent = nullptr;
    QPushButton* mySortName = nullptr;
    bool mySortByName = false;
    QLabel* myFailureBanner = nullptr;   // built lazily, see showFailure()
    QTimer* myFailureTimer = nullptr;
    QStringList myShownFailures;         // every message shown this run - see paintedTexts()
    QScrollArea* myScroll = nullptr;
    QWidget* myGrid = nullptr;
    std::vector<Card> myCards;
};
