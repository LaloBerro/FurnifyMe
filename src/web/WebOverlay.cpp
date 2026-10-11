#include "WebOverlay.h"

#include "DocumentModel.h"
#include "OcctViewWidget.h"
#include "WebHost.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWebEngineView>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>

// ---------------------------------------------------------------------------
// SpikeBridge
// ---------------------------------------------------------------------------

void SpikeBridge::reportHitRects(const QVariantList& rects)
{
    QVector<QRect> parsed;
    parsed.reserve(rects.size());
    for (const QVariant& v : rects) {
        const QVariantMap m = v.toMap();
        const QRect r(m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
                      m.value(QStringLiteral("w")).toInt(), m.value(QStringLiteral("h")).toInt());
        if (r.isValid()) parsed.push_back(r);
    }
    myHitRects = parsed;
    emit hitRectsChanged();
}

void SpikeBridge::reportReady()
{
    myReady = true;
    emit pageReady();
}

void SpikeBridge::runCommand(const QString& id)
{
    emit commandReceived(id);
}

void SpikeBridge::reportChipShown(int rev, double x, double y)
{
    emit chipShown(rev, x, y);
}

// ---------------------------------------------------------------------------
// WebOverlay
// ---------------------------------------------------------------------------

void WebOverlay::prepareApplication()
{
    WebHost::prepareApplication();
}

WebOverlay::WebOverlay(OcctViewWidget* viewport, AnchorSource anchor, QObject* parent)
    : QObject(parent), myViewport(viewport), myAnchor(std::move(anchor))
{
    myClock.start();

    // A child of the viewport, like every Qt overlay today. Measured against a
    // sibling laid over it: the same idle cost to the frame (gate 2), because
    // what costs is compositing one more full-window texture, wherever it sits.
    myHost = new WebHost(viewport, WebHost::Ground::Transparent, this);
    myView = myHost->view();
    myView->setGeometry(viewport->rect());

    myBridge = new SpikeBridge(this);
    myHost->registerObject(QStringLiteral("spike"), myBridge);

    // Until the page says otherwise, nothing of it takes the pointer.
    applyMask();
    connect(myBridge, &SpikeBridge::hitRectsChanged, this, &WebOverlay::applyMask);
    connect(myBridge, &SpikeBridge::pageReady, this, [this] {
        applyRenderWidgetAttributes();
        pushAnchor();
    });
    connect(myBridge, &SpikeBridge::chipShown, this, [this](int rev, double, double) {
        ChipSample s;
        s.shownRev = rev;
        s.latestSentRev = myRev;
        if (rev > 0 && rev < int(mySentNs.size()))
            s.latencyMs = (myClock.nsecsElapsed() - mySentNs[size_t(rev)]) / 1.0e6;
        myChipSamples.push_back(s);
    });
    connect(myHost, &WebHost::loadFinished, this, [this] { applyRenderWidgetAttributes(); });

    // A wheel zoom has no release; the gesture ends when the wheel rests.
    myWheelSettle = new QTimer(this);
    myWheelSettle->setSingleShot(true);
    myWheelSettle->setInterval(200);
    connect(myWheelSettle, &QTimer::timeout, this, [this] { setCameraMoving(false); });

    viewport->installEventFilter(this);
    connect(viewport, &OcctViewWidget::cameraChanged, this, &WebOverlay::pushAnchor);

    myView->show();
    myView->raise();
}

WebOverlay::~WebOverlay()
{
    if (myViewport) myViewport->removeEventFilter(this);
}

QUrl WebOverlay::defaultUrl()
{
    return WebHost::pageUrl(QStringLiteral("overlay"));
}

WebOverlay::AnchorSource WebOverlay::topOfFirstBody(const DocumentModel* document)
{
    return [document]() -> std::optional<gp_Pnt> {
        if (!document || document->solids().empty()) return std::nullopt;
        Bnd_Box box;
        BRepBndLib::Add(document->solids().front().shape, box);
        if (box.IsVoid()) return std::nullopt;
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        return gp_Pnt((x0 + x1) / 2.0, (y0 + y1) / 2.0, z1);
    };
}

void WebOverlay::load(const QUrl& url)
{
    myHost->load(url);
}

bool WebOverlay::loaded() const { return myHost->loaded(); }
bool WebOverlay::loadOk() const { return myHost->loadOk(); }

void WebOverlay::setStackOnTop(bool on)
{
    myStackOnTop = on;
    applyRenderWidgetAttributes();
}

void WebOverlay::applyRenderWidgetAttributes()
{
    // The view hosts its page in a child render widget (its focus proxy), a
    // texture widget like the viewport itself. Qt composites texture widgets
    // in stacking order and blends one over another only when it is marked
    // stack-on-top - which is what lets the transparent page show the
    // viewport underneath rather than a cleared rectangle.
    // Off (the default) leaves the render widget exactly as Qt WebEngine made
    // it - measured: translucency set on it WITHOUT stack-on-top composites
    // black wherever the page is empty.
    if (!myStackOnTop) return;
    QWidget* render = myView->focusProxy();
    for (QWidget* w : {static_cast<QWidget*>(myView), render}) {
        if (!w) continue;
        w->setAttribute(Qt::WA_AlwaysStackOnTop, myStackOnTop);
        w->setAttribute(Qt::WA_TranslucentBackground, true);
    }
}

void WebOverlay::applyMask()
{
    QRegion region;
    for (const QRect& r : myBridge->hitRects()) region += r;
    myHitRegion = region;
    // An empty region would CLEAR the mask - the whole view taking the
    // pointer. One pixel outside the widget is a mask that contains nothing.
    myView->setMask(region.isEmpty() ? QRegion(QRect(-2, -2, 1, 1)) : region);
}

void WebOverlay::pushAnchor()
{
    if (!myViewport || myOrbiting) return;
    bool visible = false;
    QPointF px;
    if (myAnchor) {
        if (const std::optional<gp_Pnt> world = myAnchor()) {
            QPoint screen;
            if (myViewport->projectToScreen(*world, screen)) {
                px = screen;
                visible = myViewport->rect().contains(screen);
            }
        }
    }
    ++myRev;
    if (int(mySentNs.size()) <= myRev) mySentNs.resize(size_t(myRev) + 1024, 0);
    mySentNs[size_t(myRev)] = myClock.nsecsElapsed();
    myLastAnchorPx = px;
    emit myBridge->anchorMoved(myRev, px.x(), px.y(), visible);
}

void WebOverlay::setCameraMoving(bool moving)
{
    if (moving == myCameraMoving) return;
    myCameraMoving = moving;
    emit myBridge->cameraMoving(moving);
}

bool WebOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myViewport) {
        switch (event->type()) {
        case QEvent::Resize:
            myView->setGeometry(myViewport->rect());
            pushAnchor();
            break;
        case QEvent::MouseButtonPress: {
            const Qt::MouseButton b = static_cast<QMouseEvent*>(event)->button();
            if (b == Qt::RightButton) {
                myOrbiting = true;
                emit myBridge->orbitActive(true);
            }
            if (b == Qt::RightButton || b == Qt::MiddleButton) setCameraMoving(true);
            break;
        }
        case QEvent::MouseButtonRelease: {
            const auto* m = static_cast<QMouseEvent*>(event);
            if (m->button() == Qt::RightButton) {
                myOrbiting = false;
                pushAnchor();   // where the chip comes back
                emit myBridge->orbitActive(false);
            }
            if ((m->button() == Qt::RightButton || m->button() == Qt::MiddleButton) &&
                !(m->buttons() & (Qt::RightButton | Qt::MiddleButton)))
                setCameraMoving(false);
            break;
        }
        case QEvent::Wheel:
            setCameraMoving(true);
            myWheelSettle->start();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
