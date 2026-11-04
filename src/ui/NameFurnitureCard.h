#pragma once
// The name a furniture is given as it is created (improvements items 3 and 4,
// the user's pick: "a name card, both places").
//
// A furniture can be born in two places - the library's + card and the
// editor's File -> New furniture - and both ask here, so there is one question
// with one wording and one set of keys rather than two flows that drift. It is
// NOT a QDialog: the app has no modal dialogs and gui_smoke asserts as much.
// This is UnsavedCloseCard's shape, clause for clause - the whole widget is
// the scrim, it covers its host edge to edge, paints the wash and the card,
// swallows every mouse event that reaches it, and owns its window's keyboard
// through an application-wide filter installed on show and removed on hide
// that claims QEvent::ShortcutOverride so QShortcutMap cannot take a key
// first. Enter creates, Escape cancels, and it borrows that card's own
// CloseCardButton for the two controls so the two questions look like one
// family.
//
// Two things are this card's own. It holds a REAL QLineEdit (the close
// question has none), which is a child of the scrim so it stays hit-testable,
// takes focus when the card opens and carries its text selected so typing
// replaces the suggested name outright. And the name is validated as it is
// typed: an empty name cannot be created, so Create dims and Enter reports
// rather than doing nothing silently.
//
// WHAT the name is then used for is the caller's - this widget only asks. It
// hides itself before reporting, exactly as the close question does, so a
// failure raised by the creation that follows lands on a clear window.
//
// IN HTML-CHROME MODE THE PAGE DRAWS IT (migration Phase 3g, UnsavedCloseCard's
// Phase 3f shape). setDrawnElsewhere() makes this widget paint nothing, hide
// its field and buttons and pass the pointer through, while it stays SHOWN -
// so isAsking(), the key claim and "hide first, then report" all hold. One
// difference from the close question: the field is the page's, so every key
// aimed at the page's view goes THROUGH the claim, Enter and Escape included,
// and the page answers with the name its field holds (create()) - never one
// mirrored a channel message earlier, which a fast "type, Enter" would beat.
// The page mirrors each edit too (setName()), so a key that reaches this
// filter from elsewhere, or a page that dies mid-question, has the typed name.
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

class CloseCardButton;
class QAbstractButton;
class QHideEvent;
class QLineEdit;
class QShowEvent;

class NameFurnitureCard : public QWidget {
    Q_OBJECT

public:
    explicit NameFurnitureCard(QWidget* host);

    // Opens the question with `suggestion` in the field, selected. Asking
    // again while it already stands re-seeds and re-raises.
    void ask(const QString& suggestion);

    // Whether the question stands - the widget's own hidden flag, never a
    // stored bool, and isHidden() rather than isVisible() so the answer does
    // not depend on whether the window happens to be on screen.
    bool isAsking() const { return !isHidden(); }

    // Re-fits the scrim to the host, re-places the card and re-raises.
    void replace();

    // The controls, for wiring and for the suite's childAt() reachability
    // checks.
    QLineEdit* field() const { return myField; }
    QAbstractButton* createButton() const;
    QAbstractButton* cancelButton() const;

    // The name in the field, trimmed - what created() carries.
    QString name() const;

    QRect cardRect() const { return myCard; }
    QString titleText() const;
    QString bodyText() const;
    // Every string this card paints that is THIS APP's copy, for gui_smoke's
    // banned-word sweep. The field's own text is the user's and is not here.
    QStringList paintedTexts() const;

    // --- HTML-chrome mode (Phase 3g) -----------------------------------------
    // `keyTarget` is the page's view: keys aimed at it, or at anything inside
    // it, go through the claim untouched (ShortcutOverride still claimed, so no
    // window shortcut fires). Back to false, mid-question too, the card paints
    // again and its field takes focus holding what was typed on the page.
    void setDrawnElsewhere(bool drawnElsewhere, QWidget* keyTarget = nullptr);
    bool drawnElsewhere() const { return myDrawnElsewhere; }
    // {kind: "name", title, body, name, seed, maxLength, create, createKey,
    // cancel, cancelKey}. `name` is the field's text; `seed` moves on every
    // ask and every hand-over to the page, and the page re-seeds its field
    // only when it does - so an edit mirrored back never retypes the field.
    QVariantMap words() const;
    // The page's field, mirrored into this card's own.
    void setName(const QString& name);
    // The page's answers. create() takes the name the page holds and refuses
    // an empty one exactly as Enter does; both are no-ops unless asking.
    void create(const QString& name);
    void cancel();

signals:
    void created(const QString& name);
    void cancelled();
    // The question stood up (or was asked again) or went away - when the key
    // claim is installed or removed, and on a re-ask while it stands.
    void questionChanged(bool asking);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Hides FIRST, then reports - the close question's contract.
    void accept();
    void reject();
    void layoutCard();
    void applyTheme();
    int titleHeight(int textWidth) const;

    QRect myCard;
    QLineEdit* myField = nullptr;
    CloseCardButton* myCreate = nullptr;
    CloseCardButton* myCancel = nullptr;
    bool myClaimInstalled = false;
    bool myDrawnElsewhere = false;
    QPointer<QWidget> myKeyTarget;
    int mySeed = 0;
};
