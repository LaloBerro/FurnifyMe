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

#include <QObject>
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

// The page's `chrome` object.
class ChromeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool undoEnabled READ undoEnabled NOTIFY undoEnabledChanged)
    Q_PROPERTY(QVariantList obstacles READ obstacles NOTIFY obstaclesChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap cards READ cards NOTIFY cardsChanged)
public:
    using QObject::QObject;
    bool undoEnabled() const { return myUndoEnabled; }
    QVariantList obstacles() const { return myObstacles; }
    QVariantMap status() const { return myStatus; }
    QVariantMap cards() const { return myCards; }
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
    // Observed on the C++ side
    void undoRequested(int stamp);
    void hitRectsChanged();
    void pageReady();
    void keyboardClaimed();
    void keyboardReleased();

public slots:
    // page -> C++
    void reportHitRects(const QVariantList& rects);
    void undo(int stamp);
    void ready();
    void claimKeyboard();
    void releaseKeyboard();

private:
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
    // A typed field on the page holds the keyboard (ruling R5). While it
    // does, the view claims every ShortcutOverride, so Delete, Ctrl+Z or a
    // digit in a field never reaches a window shortcut.
    void claimKeyboard();
    void releaseKeyboard();
    bool holdsKeyboard() const { return myHoldsKeyboard; }
    ~EditorChrome() override;

    enum class Kind { Note, Failure };
    void showToast(const QString& text, Kind kind, bool offersUndo, int stamp);
    void documentMovedTo(int stamp);
    void setUndoEnabled(bool enabled);
    void setObstacles(const std::vector<QRect>& rects);
    void setStatus(const QString& left, const QString& right, const QString& name, bool unsaved,
                   bool shown);

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
    bool myHoldsKeyboard = false;
    // How many of the page's fields hold the keyboard. Moving from field A
    // to field B arrives as B's claim THEN A's release (the browser's
    // order), so a flag would give the keyboard up in the middle (review
    // I1); a count only lets go when the last holder does.
    int myKeyboardDepth = 0;
    void dropKeyboard();
    QPointer<QWidget> myFocusBefore;
    QPointer<QWidget> myWatchedProxy;
    std::vector<QPointer<HtmlCardSlot>> myCards;
};
