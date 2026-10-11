#include "EditorChrome.h"

#include "OcctViewWidget.h"
#include "ThemeBridge.h"
#include "WebHost.h"

#include <QEvent>
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
    connect(myBridge, &ChromeBridge::pageReady, this, [this] {
        refuseFocus();
        setLive(true);
    });
    connect(myHost, &WebHost::loadFinished, this, [this](bool ok) {
        refuseFocus();
        if (!ok) setLive(false);
    });
    connect(myHost->view()->page(), &QWebEnginePage::renderProcessTerminated, this,
            [this] { setLive(false); });
    refuseFocus();

    viewport->installEventFilter(this);
    myHost->view()->show();
    myHost->view()->raise();
    myHost->load(WebHost::pageUrl(QStringLiteral("editor")));
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

void EditorChrome::setLive(bool live)
{
    if (myLive == live) return;
    myLive = live;
    // A dead page leaves the viewport outright: its mask would otherwise
    // keep swallowing presses on the strip's band over nothing at all.
    if (!live) myHost->view()->hide();
    emit liveChanged(live);
}

void EditorChrome::refuseFocus()
{
    // No chip takes keyboard focus (F17), and that has to hold at the Qt
    // level too: a press on the pill or the strip moved focus out of the
    // Re-measure field or the Items list into Chromium (review I7). The
    // render widget behind the view is its focus proxy, created at load.
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
