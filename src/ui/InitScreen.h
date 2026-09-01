#pragma once
//
// The init screen: a full-bleed gallery over the viewport, one card per
// furniture the library holds plus a New furniture card. It is not a modal
// dialog and not a separate window - CLAUDE.md's no-modal law applies here
// exactly as everywhere else - it is a STATE MainWindow puts the viewport
// into. Parented to OcctViewWidget like every other overlay widget, it is
// raised above the rail/drawer/gizmo the instant it is shown, and it paints
// its ENTIRE rect opaquely (the same law every card in this shell already
// obeys, applied here to the whole viewport rather than to one floating
// card), so nothing under it is visible while it is up.
//
// Visibility alone does not make the modeling actions unreachable, though -
// a QAction's shortcut fires whether or not anything is painted on top of
// the widget it would otherwise click through to. MainWindow::updateActions()
// is what actually closes that door (see its atInit gate); this widget only
// owns what is drawn and what a click on it means.
#include <QString>
#include <QWidget>

#include <vector>

class FurnitureStore;
class QDateTime;
class QLabel;
class QScrollArea;
class QWidget;

class InitScreen : public QWidget {
    Q_OBJECT

public:
    InitScreen(FurnitureStore* store, QWidget* parent);

    // Rebuilds every card from store->listFurniture(). Called whenever the
    // library might have changed under it - shown, a rename commits, a new
    // furniture is created - so the gallery is never stale.
    void refresh();

    int furnitureCount() const { return static_cast<int>(myCards.size()); }
    // The card widgets themselves, for gui_smoke's childAt-real hit tests -
    // a probe that clicks a coordinate and asserts identity against the
    // pointer this returns, not merely "something was there".
    QWidget* cardAt(int index) const;
    QWidget* newCard() const { return myNewCard; }
    QString cardName(int index) const;
    QString cardFurnitureId(int index) const;

    // Drives the rename gesture the way a real F2 or a double-click on the
    // card's name would - see ui/InlineRename.h. Exposed so the suite can
    // reach it without synthesizing the exact key/click sequence.
    void beginRenameAt(int index);

    // Every string THIS app wrote, for the banned-word sweep -
    // WalkthroughPanel::paintedTexts()'s own reasoning: painted copy is
    // invisible to that sweep unless a widget hands it over itself. Per-
    // furniture card NAMES are deliberately excluded - they are the user's
    // own word choice (a furniture literally named "Fuse My Table" is not a
    // lapse in this app's copy), the same exemption
    // AppearancePanel/gui_smoke's usesBannedWord(..., isUserData) already
    // carries for user-typed text elsewhere.
    QStringList paintedTexts() const;

signals:
    // An existing card was opened.
    void furnitureChosen(const QString& id);
    // New furniture was created and should be opened immediately - the
    // FurnitureStore contract that a freshly created furniture already
    // round-trips through loadFurniture() is what makes that safe.
    void furnitureCreated(const QString& id);

    // The two refusals FurnitureStore can hand back from this screen's own
    // gestures - a disk that will not take a new directory, or a rename
    // landing on a furniture whose files have gone missing underneath it.
    // Neither is swallowed: this card owns the gallery, not the way this
    // app reports outcomes, so the copy - in cause-and-fix form, like every
    // other refusal - lives in MainWindow with everything else this app
    // says no to, on the same terms AppearancePanel's colourSaveFailed()/
    // colourLoadRefused() already established.
    void furnitureCreateFailed(const QString& attemptedName);
    void furnitureRenameFailed(const QString& id, const QString& attemptedName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Card {
        QString id;
        QWidget* widget = nullptr;
    };

    void rebuildCards();
    void relayoutCards();
    QWidget* buildFurnitureCard(const QString& id, const QString& name,
                               const QString& thumbPath, const QDateTime& lastEdited);
    QWidget* buildNewCard();
    void applyTheme();

    FurnitureStore* myStore = nullptr;
    QLabel* myTitle = nullptr;
    QScrollArea* myScroll = nullptr;
    QWidget* myGrid = nullptr;
    std::vector<Card> myCards;
    QWidget* myNewCard = nullptr;
};
