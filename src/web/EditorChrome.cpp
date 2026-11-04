#include "EditorChrome.h"

#include "HtmlCardSlot.h"
#include "OcctViewWidget.h"
#include "ThemeBridge.h"
#include "WebHost.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QWebEnginePage>
#include <QWebEngineView>

// --- the page's `chrome` object ---------------------------------------------

void ChromeBridge::setUndoEnabledState(bool on)
{
    if (myUndoEnabled == on) return;
    myUndoEnabled = on;
    emit undoEnabledChanged(on);
}

void ChromeBridge::setObstacleList(const QVariantList& list)
{
    if (myObstacles == list) return;
    myObstacles = list;
    emit obstaclesChanged();
}

void ChromeBridge::setStatusMap(const QVariantMap& map)
{
    if (myStatus == map) return;
    myStatus = map;
    emit statusChanged();
}

void ChromeBridge::setQuestionMap(const QVariantMap& map)
{
    if (myQuestion == map) return;
    myQuestion = map;
    emit questionChanged(map);
}

void ChromeBridge::answerQuestion(const QString& which)
{
    if (!myQuestion.value(QStringLiteral("asking")).toBool()) return;
    if (which != QLatin1String("save") && which != QLatin1String("discard") && which != QLatin1String("keep")) return;
    emit questionAnswered(which);
}

void ChromeBridge::setCardMap(const QVariantMap& map)
{
    if (myCards == map) return;
    myCards = map;
    emit cardsChanged();
}

void ChromeBridge::claimKeyboard()
{
    emit keyboardClaimed(QString());
}

void ChromeBridge::releaseKeyboard()
{
    emit keyboardReleased(QString());
}

void ChromeBridge::claimKeyboardFor(const QString& card)
{
    emit keyboardClaimed(card);
}

void ChromeBridge::releaseKeyboardFor(const QString& card)
{
    emit keyboardReleased(card);
}

void ChromeBridge::reportCardHeight(const QString& name, int height)
{
    emit cardHeightReported(name, height);
}

void ChromeBridge::reportHitRects(const QVariantList& rects)
{
    QVector<QRect> parsed;
    for (const QVariant& v : rects) {
        const QVariantMap m = v.toMap();
        const QRect r(m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
                      m.value(QStringLiteral("w")).toInt(), m.value(QStringLiteral("h")).toInt());
        if (r.isValid()) parsed.push_back(r);
    }
    myHitRects = parsed;
    emit hitRectsChanged();
}

void ChromeBridge::undo(int stamp)
{
    emit undoRequested(stamp);
}

void ChromeBridge::ready()
{
    emit pageReady();
}

// --- the chrome --------------------------------------------------------------

namespace {
QVariantMap rectMap(const QRect& r)
{
    return {{QStringLiteral("x"), r.x()},
            {QStringLiteral("y"), r.y()},
            {QStringLiteral("w"), r.width()},
            {QStringLiteral("h"), r.height()}};
}
}  // namespace

EditorChrome::EditorChrome(OcctViewWidget* viewport, QObject* parent)
    : QObject(parent), myViewport(viewport)
{
    myHost = new WebHost(viewport, WebHost::Ground::Transparent, this);
    myHost->view()->setGeometry(viewport->rect());
    myBridge = new ChromeBridge(this);
    myTheme = new ThemeBridge(this);
    myHost->registerObject(QStringLiteral("chrome"), myBridge);
    myHost->registerObject(QStringLiteral("theme"), myTheme);

    // Until the page says otherwise, nothing of it takes the pointer.
    applyMask();
    connect(myBridge, &ChromeBridge::hitRectsChanged, this, &EditorChrome::applyMask);
    connect(myBridge, &ChromeBridge::undoRequested, this, &EditorChrome::undoRequested);
    connect(myBridge, &ChromeBridge::questionAnswered, this, &EditorChrome::questionAnswered);
    connect(myBridge, &ChromeBridge::keyboardClaimed, this, &EditorChrome::claimKeyboard);
    connect(myBridge, &ChromeBridge::keyboardReleased, this, &EditorChrome::releaseKeyboard);
    // A card that sizes itself tells us its height; its slot takes it, and
    // the overlay re-stacks around it (Phase 3c).
    connect(myBridge, &ChromeBridge::cardHeightReported, this, [this](const QString& name, int height) {
        for (const QPointer<HtmlCardSlot>& slot : myCards)
            if (slot && slot->name() == name) slot->setContentHeight(height);
    });
    connect(myBridge, &ChromeBridge::pageReady, this, [this] {
        refuseFocus();
        watchFocusProxy();
        setLive(true);
    });
    connect(myHost, &WebHost::loadFinished, this, [this](bool ok) {
        refuseFocus();
        watchFocusProxy();
        if (!ok) setLive(false);
    });
    connect(myHost->view()->page(), &QWebEnginePage::renderProcessTerminated, this,
            [this] { setLive(false); });
    refuseFocus();

    viewport->installEventFilter(this);
    myHost->view()->show();
    myHost->view()->raise();
}

void EditorChrome::start()
{
    if (myHost->loaded()) return;
    myHost->load(WebHost::pageUrl(QStringLiteral("editor")));
}

void EditorChrome::registerObject(const QString& name, QObject* object)
{
    myHost->registerObject(name, object);
}

void EditorChrome::addCard(HtmlCardSlot* slot)
{
    if (!slot) return;
    myCards.push_back(slot);
    connect(slot, &HtmlCardSlot::placed, this, &EditorChrome::syncCards);
    syncCards();
}

void EditorChrome::syncCards()
{
    QVariantMap map;
    QStringList gone;
    for (const QPointer<HtmlCardSlot>& slot : myCards) {
        if (!slot) continue;
        QVariantMap card = rectMap(slot->geometry());
        const bool shown = slot->isVisible();
        card.insert(QStringLiteral("shown"), shown);
        // A card that WAS shown and is not now - never merely one that is
        // hidden: with two cards, the closed one would otherwise take the
        // keyboard from a field in the open one on every relayout (Phase 3c).
        if (myCardShown.value(slot->name()) && !shown) gone << slot->name();
        myCardShown.insert(slot->name(), shown);
        map.insert(slot->name(), card);
    }
    // A card going away mid-edit gives ITS claims back from here too, not
    // only from the page (Review Focus 4) - and only its own: a field in a
    // card still open keeps the keyboard (Phase 3c review I2).
    for (const QString& name : gone) myKeyboardClaims.remove(name);
    if (!gone.isEmpty() && myHoldsKeyboard && keyboardClaimCount() == 0) dropKeyboard();
    myBridge->setCardMap(map);
}

int EditorChrome::keyboardClaimCount() const
{
    int total = 0;
    for (int n : myKeyboardClaims) total += n;
    return total;
}

void EditorChrome::claimKeyboard(const QString& card)
{
    if (!myLive) return;
    ++myKeyboardClaims[card];
    if (myHoldsKeyboard) return;
    QWebEngineView* view = myHost->view();
    QWidget* proxy = view->focusProxy() ? view->focusProxy() : view;
    // The WINDOW's focus widget, not QApplication's: that one is null for a
    // window the OS has not activated, which is every window the harness shows.
    myFocusBefore = view->window()->focusWidget();
    // Only a widget OUTSIDE the page is somewhere to go back to: handing focus
    // back to the page's own render widget would keep it claiming keys.
    if (myFocusBefore && (myFocusBefore == view || view->isAncestorOf(myFocusBefore))) myFocusBefore = nullptr;
    myHoldsKeyboard = true;
    watchFocusProxy();
    view->setFocusPolicy(Qt::StrongFocus);
    proxy->setFocusPolicy(Qt::StrongFocus);
    proxy->setFocus(Qt::OtherFocusReason);
}

void EditorChrome::releaseKeyboard(const QString& card)
{
    auto it = myKeyboardClaims.find(card);
    if (it == myKeyboardClaims.end()) return;
    if (--it.value() <= 0) myKeyboardClaims.erase(it);
    if (keyboardClaimCount() > 0) return;
    dropKeyboard();
}

void EditorChrome::dropKeyboard()
{
    myKeyboardClaims.clear();
    if (!myHoldsKeyboard) return;
    myHoldsKeyboard = false;
    QWebEngineView* view = myHost->view();
    QWidget* now = view->window()->focusWidget();
    // Focus goes back only if it is still the page's. A release that follows
    // the user clicking a Qt field (the Items filter, the Re-measure field)
    // must leave it there, or the next letters reach window shortcuts instead
    // of the field they were typed into (review I2).
    const bool stillOurs = !now || now == view || view->isAncestorOf(now);
    refuseFocus();
    if (stillOurs) {
        if (myFocusBefore) myFocusBefore->setFocus(Qt::OtherFocusReason);
        else view->clearFocus();
    }
    myFocusBefore = nullptr;
}

void EditorChrome::watchFocusProxy()
{
    QWidget* proxy = myHost->view()->focusProxy();
    if (!proxy || proxy == myWatchedProxy) return;
    if (myWatchedProxy) myWatchedProxy->removeEventFilter(this);
    myWatchedProxy = proxy;
    proxy->installEventFilter(this);
}

EditorChrome::~EditorChrome()
{
    if (myViewport) myViewport->removeEventFilter(this);
}

QWebEngineView* EditorChrome::view() const { return myHost->view(); }

void EditorChrome::showToast(const QString& text, Kind kind, bool offersUndo, int stamp)
{
    // Kept above every other overlay child, so a card raised by a relayout
    // never covers the message.
    myHost->view()->raise();
    emit myBridge->toast(text, kind == Kind::Failure ? QStringLiteral("failure") : QStringLiteral("note"),
                         offersUndo, stamp, myPointer.x(), myPointer.y(), myBridge->undoEnabled());
}

void EditorChrome::documentMovedTo(int stamp)
{
    emit myBridge->documentMoved(stamp);
}

void EditorChrome::setUndoEnabled(bool enabled)
{
    myBridge->setUndoEnabledState(enabled);
}

void EditorChrome::setObstacles(const std::vector<QRect>& rects)
{
    QVariantList list;
    for (const QRect& r : rects) list.push_back(rectMap(r));
    myBridge->setObstacleList(list);
}

void EditorChrome::setStatus(const QString& left, const QString& right, const QString& name,
                             bool unsaved, bool shown)
{
    myBridge->setStatusMap({{QStringLiteral("left"), left},
                            {QStringLiteral("right"), right},
                            {QStringLiteral("name"), name},
                            {QStringLiteral("unsaved"), unsaved},
                            {QStringLiteral("shown"), shown}});
}

void EditorChrome::setQuestion(bool asking, const QVariantMap& words)
{
    QVariantMap map = asking ? words : QVariantMap();
    map.insert(QStringLiteral("asking"), asking);
    myBridge->setQuestionMap(map);
    if (asking) myHost->view()->raise();
}

void EditorChrome::setLive(bool live)
{
    if (myLive == live) return;
    myLive = live;
    // A dead page leaves the viewport outright: its mask would otherwise
    // keep swallowing presses on the strip's band over nothing at all.
    if (!live) {
        dropKeyboard();
        myHost->view()->hide();
        // Nothing of the page takes the pointer any more - the wheel shield
        // and the mask read this region (review I3).
        myBridge->reportHitRects({});
    }
    emit liveChanged(live);
}

void EditorChrome::refuseFocus()
{
    // No chip takes keyboard focus (F17), and that has to hold at the Qt
    // level too: a press on the pill or the strip moved focus out of the
    // Re-measure field or the Items list into Chromium (review I7). The
    // render widget behind the view is its focus proxy, created at load.
    // Not while a typed field holds the keyboard - releaseKeyboard() comes
    // back here when it ends.
    if (myHoldsKeyboard) return;
    QWebEngineView* view = myHost->view();
    view->setFocusPolicy(Qt::NoFocus);
    if (QWidget* proxy = view->focusProxy()) proxy->setFocusPolicy(Qt::NoFocus);
}

void EditorChrome::applyMask()
{
    QRegion region;
    for (const QRect& r : myBridge->hitRects()) region += r;
    myHitRegion = region;
    // An empty region would CLEAR the mask - the whole page taking the
    // pointer. One pixel outside the widget is a mask that contains nothing.
    myHost->view()->setMask(region.isEmpty() ? QRegion(QRect(-2, -2, 1, 1)) : region);
}

bool EditorChrome::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myWatchedProxy && event->type() == QEvent::ShortcutOverride && myHoldsKeyboard) {
        // Every key is the field's while it holds the keyboard: claimed
        // here, before QShortcutMap can hand it to a window shortcut.
        event->accept();
        return true;
    }
    if (watched == myViewport && event->type() == QEvent::Wheel && myLive) {
        // THE WHEEL SHIELD (Review Focus 2). A wheel the page did not consume
        // - over a card's title, or a list already at its end - is handed by
        // WebEngine to the view's PARENT, the viewport, which would zoom the
        // camera. The mask sends every wheel outside the page's rectangles to
        // the viewport directly, so one inside them can only have come back
        // from the page: it stops here.
        const QPoint at = static_cast<QWheelEvent*>(event)->position().toPoint();
        if (myHitRegion.contains(at)) {
            event->accept();
            return true;
        }
    }
    if (watched == myViewport) {
        switch (event->type()) {
        case QEvent::Resize:
            myHost->view()->setGeometry(myViewport->rect());
            break;
        case QEvent::MouseMove:
            // Where the pointer rests, for a toast arriving under it.
            myPointer = static_cast<QMouseEvent*>(event)->position().toPoint();
            break;
        case QEvent::Leave:
            myPointer = QPoint(-1, -1);
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
