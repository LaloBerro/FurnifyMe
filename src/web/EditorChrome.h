#pragma once

// The editor's chrome as an HTML page over the viewport (migration Phase 3a):
// for now the toast and the status strip (web/src/editor). One transparent
// page the viewport's size; it takes the pointer only where it draws, and
// reports those rectangles, which become the view's mask - so every other
// press reaches the 3D view in the same event (the Phase 0 spike's gate 3).
//
// MainWindow stays the author of everything said: in HTML-chrome mode it
// sends Notes and Failures here instead of to ToastHost, and keeps writing
// the (hidden) Qt status bar, which this mirrors to the page.

#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <vector>

class OcctViewWidget;
class ThemeBridge;
class WebHost;
class QWebEngineView;
class HtmlCardSlot;
class ChipFace;

// The page's `chrome` object.
class ChromeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool undoEnabled READ undoEnabled NOTIFY undoEnabledChanged)
    Q_PROPERTY(QVariantList obstacles READ obstacles NOTIFY obstaclesChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(QVariantMap question READ question NOTIFY questionChanged)
    // Whether the page may animate (the viewport's switch: the suites turn it
    // off). The rail and the pill fly in and out on the page's own clock while
    // it does (Phase 3i, ruling R8).
    Q_PROPERTY(bool animate READ animate NOTIFY animateChanged)
public:
    using QObject::QObject;
    bool undoEnabled() const { return myUndoEnabled; }
    QVariantList obstacles() const { return myObstacles; }
    QVariantMap status() const { return myStatus; }
    QVariantMap cards() const { return myCards; }
    // The close question (Phase 3f): {asking, title, body, save, saveKey,
    // discard, keep, keepKey}. Also sent WITH questionChanged, which a page
    // must not wait a batch for.
    QVariantMap question() const { return myQuestion; }
    bool animate() const { return myAnimate; }
    void setAnimate(bool on);
    void setQuestionMap(const QVariantMap& map);
    const QVector<QRect>& hitRects() const { return myHitRects; }

    void setUndoEnabledState(bool on);
    void setObstacleList(const QVariantList& list);
    void setStatusMap(const QVariantMap& map);
    void setCardMap(const QVariantMap& map);

signals:
    // C++ -> page
    // undoOpen rides WITH the message: a property's NOTIFY is batched by
    // QWebChannel on a timer while a plain signal goes at once, so a pill
    // that relied on undoEnabledChanged arrived reading "closed" (measured).
    void toast(const QString& text, const QString& kind, bool offersUndo, int stamp, int pointerX,
               int pointerY, bool undoOpen);
    void documentMoved(int stamp);
    void undoEnabledChanged(bool enabled);
    void obstaclesChanged();
    void statusChanged();
    void cardsChanged();
    void questionChanged(const QVariantMap& question);
    void animateChanged();
    // Observed on the C++ side
    void questionAnswered(const QString& which);
    void nameQuestionAnswered(const QString& which, const QString& name);
    void nameQuestionTyped(const QString& name);
    void undoRequested(int stamp);
    void hitRectsChanged();
    void pageReady();
    // card: the HTML card whose field claims ('' for an anonymous claim).
    void keyboardClaimed(const QString& card);
    void keyboardReleased(const QString& card);
    void listKeysHeld(const QString& card);
    void listKeysReleased(const QString& card);
    void cardHeightReported(const QString& name, int height);
    void cardSizeReported(const QString& name, int width, int height);

public slots:
    // page -> C++
    void reportHitRects(const QVariantList& rects);
    void undo(int stamp);
    void ready();
    void claimKeyboard();
    void releaseKeyboard();
    // The same, owned by one card (Phase 3c review I2): a card going away
    // takes back only its own claims, never another card's.
    void claimKeyboardFor(const QString& card);
    void releaseKeyboardFor(const QString& card);
    // A LIST holds its keys (Phase 3h, ruling R3): the page's view takes Qt
    // focus, but only the list's own keys (session/ItemsKeys) are claimed
    // from the shortcut map - Ctrl+Z, E and every other shortcut still fire,
    // as they do while the Qt drawer's list has focus. The hold ends when
    // focus leaves the page (a click on the viewport), on release, when the
    // card goes, and when the page dies.
    void holdListKeys(const QString& card);
    void releaseListKeys(const QString& card);
    // A card that sizes itself (the Versions drawer, Phase 3c) says how tall
    // it is; its slot takes the height.
    void reportCardHeight(const QString& name, int height);
    // A card that sizes itself both ways (Phase 3i: the rail and the view
    // cluster follow the theme's chip size); its slot takes the size.
    void reportCardSize(const QString& name, int width, int height);
    // The close question's answer: "save", "discard" or "keep". Anything else,
    // or an answer while no question stands, is refused.
    void answerQuestion(const QString& which);
    // The name question's answer (Phase 3g): "create" with the name the
    // page's field holds - it rides WITH the answer - or "cancel". Refused
    // unless the name question stands.
    void answerNameQuestion(const QString& which, const QString& name);
    // Each edit of the page's name field, mirrored into the Qt card's.
    void nameQuestionEdited(const QString& name);

public:
    // Whether the question of this kind ("close", "name") stands.
    bool asks(const QString& kind) const;

private:
    QVariantMap myQuestion;
    bool myAnimate = true;
    bool myUndoEnabled = true;
    QVariantList myObstacles;
    QVariantMap myStatus;
    QVariantMap myCards;
    QVector<QRect> myHitRects;
};

class EditorChrome : public QObject
{
    Q_OBJECT
public:
    // The page is not loaded until start(), so objects registered between
    // the two (the Settings card's `settings`) are there when it connects.
    EditorChrome(OcctViewWidget* viewport, QObject* parent = nullptr);
    void start();
    void registerObject(const QString& name, QObject* object);
    // An HTML card's place (Phase 3b): the page is told where the slot
    // stands and whether it shows, every time it moves, shows or hides.
    void addCard(HtmlCardSlot* slot);
    // A gesture chip (Phase 4a, ruling R2): the Qt chip IS its own slot. Its
    // rectangle and visibility go in the same `cards` map, under its face's
    // name, every time it moves, resizes, shows or hides.
    void addChip(ChipFace* face);
    // Whether a field of this card holds the keyboard (ruling R3: a chip's
    // claim lets keys aimed at the page through only while one does).
    bool claimsKeyboardFor(const QString& card) const { return myKeyboardClaims.value(card) > 0; }
    // A typed field on the page holds the keyboard (ruling R5). While it
    // does, the view claims every ShortcutOverride, so Delete, Ctrl+Z or a
    // digit in a field never reaches a window shortcut.
    void claimKeyboard(const QString& card = QString());
    void releaseKeyboard(const QString& card = QString());
    bool holdsKeyboard() const { return myHoldsKeyboard; }
    // See ChromeBridge::holdListKeys().
    void holdListKeys(const QString& card);
    void releaseListKeys(const QString& card);
    bool holdsListKeys() const { return !myListHolds.isEmpty(); }
    // Which keys a list hold claims: the table's list-scope keys by default.
    // MainWindow narrows it (F2 only over a live row, as the Qt drawer does).
    void setListKeyClaim(std::function<bool(QKeyCombination)> claim) { myListKeyClaim = std::move(claim); }
    ~EditorChrome() override;

    enum class Kind { Note, Failure };
    void showToast(const QString& text, Kind kind, bool offersUndo, int stamp);
    void documentMovedTo(int stamp);
    void setUndoEnabled(bool enabled);
    void setObstacles(const std::vector<QRect>& rects);
    void setStatus(const QString& left, const QString& right, const QString& name, bool unsaved,
                   bool shown);
    // A question stands (words from UnsavedCloseCard::words() or
    // NameFurnitureCard::words(), told apart by `kind`) or none does (an
    // empty map). While one stands the page's view is raised over every Qt
    // card, so its wash takes every press (Phase 3f, ruling R2). One at a
    // time: MainWindow::syncHtmlQuestion() picks which (Phase 3g, R2).
    void setQuestion(bool asking, const QVariantMap& words);
    // A menu stands open (Phase 3i, ruling R7): the mask takes the whole
    // viewport at once and the view is raised, so a press anywhere reaches
    // the page's outside layer, which closes the menu and swallows it.
    void setMenuOpen(bool open);
    bool menuOpen() const { return myMenuOpen; }

    ChromeBridge* bridge() const { return myBridge; }
    WebHost* host() const { return myHost; }
    QWebEngineView* view() const;
    QRegion hitRegion() const { return myHitRegion; }

    // The page is LISTENING: it loaded, its script ran, and it said so
    // (ChromeBridge::ready()). False until then, false for good once its
    // render process dies or the load fails - and while false, MainWindow
    // reports through the Qt toast and status bar instead (review I3: a
    // Failure sent to a page that is not there is shown nowhere).
    bool isLive() const { return myLive; }

signals:
    void undoRequested(int stamp);
    void liveChanged(bool live);
    void questionAnswered(const QString& which);
    void nameQuestionAnswered(const QString& which, const QString& name);
    void nameQuestionTyped(const QString& name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyMask();
    void setLive(bool live);
    void refuseFocus();
    void syncCards();
    void watchFocusProxy();

    QPointer<OcctViewWidget> myViewport;
    WebHost* myHost = nullptr;
    ChromeBridge* myBridge = nullptr;
    ThemeBridge* myTheme = nullptr;
    QRegion myHitRegion;
    QPoint myPointer{-1, -1};
    bool myLive = false;
    // The close question stands (Phase 3f review I3): the mask takes the whole
    // viewport at once, without waiting for the page to draw and report.
    bool myAsking = false;
    bool myMenuOpen = false;
    // The camera moved within the last kCameraSettleMs (Phase 4b): a glued
    // chip is not drawn until it holds still.
    bool myCameraMoving = false;
    class QTimer* myCameraSettle = nullptr;
    bool myHoldsKeyboard = false;
    // How many of the page's fields hold the keyboard. Moving from field A
    // to field B arrives as B's claim THEN A's release (the browser's
    // order), so a flag would give the keyboard up in the middle (review
    // I1); a count only lets go when the last holder does.
    // Counted PER CARD (Phase 3c review I2): a card going away gives back its
    // own claims, and the keyboard goes only when no card holds it.
    QHash<QString, int> myKeyboardClaims;
    int keyboardClaimCount() const;
    void dropKeyboard();
    void dropListHolds();
    // Puts Qt focus in the page (a claim or a hold arriving with neither
    // standing), and gives it back when the last of them goes.
    void takeFocus();
    void giveFocusBack();
    QSet<QString> myListHolds;
    std::function<bool(QKeyCombination)> myListKeyClaim;
    QPointer<QWidget> myFocusBefore;
    QPointer<QWidget> myWatchedProxy;
    std::vector<QPointer<HtmlCardSlot>> myCards;
    std::vector<QPointer<ChipFace>> myChips;
    // Whether each card was shown at the last sync, by name.
    QHash<QString, bool> myCardShown;
};
