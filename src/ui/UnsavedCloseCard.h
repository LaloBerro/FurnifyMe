#pragma once
// The question closing asks when there is work on screen that is not on disk
// (improvements item 3, the user's pick: "A - a card in the window").
//
// It is NOT a QDialog. The app has no modal dialogs and gui_smoke asserts the
// window holds none; this is a sibling parented to OcctViewWidget, like every
// other card over the viewport, that dims the viewport behind it and asks.
// Three answers, top to bottom: Save and close (Enter), Close without saving,
// Keep editing (Esc). What each answer DOES is MainWindow's - this widget only
// asks, reports which answer was chosen, and has already hidden itself by the
// time it reports, so a Failure toast raised by the save that follows lands
// on a viewport with nothing standing over it.
//
// The whole widget IS the scrim: it covers the viewport edge to edge, paints
// the dimming wash and the card, and swallows every mouse event that reaches
// it (press, release, double-click, move, wheel, context menu), so nothing
// behind the question can be picked, hovered, orbited or dragged while it
// stands. The three buttons are real child widgets of the scrim - never of a
// WA_TransparentForMouseEvents parent, which would take them out of
// hit-testing along with itself (CLAUDE.md, "Widgets over the viewport") - and
// each carries Qt::WA_NoMousePropagation.
//
// While it is up it owns the KEYBOARD of its own window, whatever holds focus:
// the application-wide filter ExtrudePreview and ShortcutSheet use, installed
// on show and removed on hide, claiming QEvent::ShortcutOverride so
// QShortcutMap cannot take a key first. Enter and Escape answer; every other
// key is swallowed, because a shortcut reaching an action behind the question
// would change the very document the question is about.
#include <QAbstractButton>
#include <QString>
#include <QStringList>
#include <QWidget>

class CloseCardButton;

class UnsavedCloseCard : public QWidget {
    Q_OBJECT

public:
    explicit UnsavedCloseCard(QWidget* viewport);

    // Shows the question for `furnitureName` over the whole viewport, raises
    // it over every other overlay, and installs the key claim. Asking again
    // while already asking only refreshes the name and re-raises.
    void ask(const QString& furnitureName);

    // Whether the question stands. Read off the widget's own hidden flag,
    // never a stored bool, and deliberately isHidden() rather than
    // isVisible(): the answer must not depend on whether the window around
    // it happens to be on screen.
    bool isAsking() const { return !isHidden(); }

    // Re-fits the scrim to the viewport, re-places the card and re-raises -
    // from ViewportOverlay::laidOut(), the one moment every anchored card is
    // at its final rectangle (CLAUDE.md's ordering rule).
    void replace();

    // The three controls, for MainWindow's wiring and for the suite's
    // childAt() reachability checks.
    QAbstractButton* saveButton() const;
    QAbstractButton* discardButton() const;
    QAbstractButton* keepButton() const;

    // The card's painted rectangle, in this widget's (= the viewport's)
    // coordinates.
    QRect cardRect() const { return myCard; }

    QString titleText() const;
    QString bodyText() const;

    // Every string this card paints that is THIS APP's copy - the title's own
    // words with the furniture's name left out, the body, the three labels
    // and the two key names - for gui_smoke's banned-word sweep, sourced from
    // the same functions paintEvent() and the buttons draw with.
    QStringList paintedTexts() const;
    // The one string the user typed: the furniture's name, embedded in the
    // title. Exposed separately so the sweep can mark it as user data - the
    // same split ItemsPanel and SelectorWindow make.
    QStringList paintedUserTexts() const { return {myName}; }

signals:
    void saveChosen();
    void discardChosen();
    void keepChosen();

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
    enum class Answer { Save, Discard, Keep };
    // Hides FIRST, then reports - see the file comment for why the order is
    // the contract.
    void answer(Answer which);
    void layoutCard();
    int titleHeight(int textWidth) const;
    int bodyHeight(int textWidth) const;

    QString myName;
    QRect myCard;
    CloseCardButton* mySave = nullptr;
    CloseCardButton* myDiscard = nullptr;
    CloseCardButton* myKeep = nullptr;
    bool myClaimInstalled = false;
};

// One of the card's three answers. Painted, not a QPushButton, because the
// three looks - accent fill, danger ink, plain - and the key hint on the right
// are this card's own; its state is the button's own press/hover only.
class CloseCardButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Look { Primary, Danger, Plain };
    CloseCardButton(Look look, const QString& label, const QString& keyHint, QWidget* parent);

    QString label() const { return myLabel; }
    QString keyHint() const { return myKeyHint; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Look myLook;
    QString myLabel;
    QString myKeyHint;
    bool myHovered = false;
};

// The furniture's name in the status bar, with a caution dot and "unsaved
// changes" beside it while the live document differs from the last save - so
// the question the card asks is never a surprise. It holds no opinion about
// dirtiness: MainWindow pushes isFurnitureDirty() in on every appStateChanged,
// the same derived-not-stored rule the window title's star follows.
//
// Painted rather than a QLabel on purpose: gui_smoke finds the state label as
// "the status bar's last non-empty QLabel", and a second label would be found
// in its place.
class FurnitureNameMark : public QWidget {
    Q_OBJECT

public:
    explicit FurnitureNameMark(QWidget* parent = nullptr);

    void setState(const QString& name, bool unsaved);
    QString name() const { return myName; }
    bool showsUnsavedDot() const { return myUnsaved; }
    // Where the dot is painted, in this widget's coordinates; null while
    // there is no dot to paint.
    QRect dotRect() const;

    QString unsavedText() const;
    QStringList paintedTexts() const;          // this app's copy
    QStringList paintedUserTexts() const { return {myName}; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString myName;
    bool myUnsaved = false;
};
