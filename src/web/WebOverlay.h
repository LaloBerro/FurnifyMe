#pragma once

// PHASE 0 SPIKE - the HTML/CSS chrome layer laid over the OCCT viewport.
//
// One QWebEngineView, transparent, the size of the viewport and a child of it,
// carrying a Svelte page (web/). The page and C++ talk through one QWebChannel
// object, SpikeBridge. Nothing here knows about MainWindow: the overlay is
// attached from outside (main.cpp's --web-spike, and tests/web_spike.cpp), so
// the spike changes no behaviour of the shipped app.
//
// INPUT IS DECIDED IN C++, SYNCHRONOUSLY. The page's root takes no pointer; it
// reports the rectangles of everything that does (reportHitRects), and that
// list becomes this view's QWidget mask. Qt's own hit test (childAt) honours a
// mask, so a press outside every rectangle goes straight to OcctViewWidget in
// the same event, with no hop to the browser process - and an orbit that
// starts on the viewport keeps its implicit grab while it crosses a card.

#include <QElapsedTimer>
#include <QTimer>
#include <QObject>
#include <QPointer>
#include <QRegion>
#include <QUrl>
#include <QVariantList>
#include <QVector>

#include <gp_Pnt.hxx>

#include <functional>
#include <optional>
#include <vector>

class DocumentModel;
class OcctViewWidget;
class QWebEngineView;

class SpikeBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    // C++ -> page
    void anchorMoved(int rev, double x, double y, bool visible);
    void orbitActive(bool active);
    // Any camera gesture (orbit, pan, zoom): the page pauses its animations,
    // which otherwise cost the viewport about a millisecond per frame.
    void cameraMoving(bool moving);
    void toastRequested(const QString& text, const QString& kind);
    void animatingChanged(bool on);

    // Observed on the C++ side (the page never connects to these)
    void hitRectsChanged();
    void pageReady();
    void commandReceived(const QString& id);
    void chipShown(int rev, double x, double y);

public slots:
    // page -> C++. Public slots, so QWebChannel exposes them to the page.
    void reportHitRects(const QVariantList& rects);
    void reportReady();
    void runCommand(const QString& id);
    void reportChipShown(int rev, double x, double y);

public:
    const QVector<QRect>& hitRects() const { return myHitRects; }
    bool ready() const { return myReady; }

private:
    QVector<QRect> myHitRects;
    bool myReady = false;
};

class WebOverlay : public QObject
{
    Q_OBJECT
public:
    // Where the glued chip stands, asked again on every camera change. Empty
    // means "nothing to anchor to" and hides the chip.
    using AnchorSource = std::function<std::optional<gp_Pnt>()>;

    // BEFORE QApplication, beside OcctViewWidget::surfaceFormat(). Qt Quick -
    // which hosts every QWebEngineView's page - defaults to Direct3D 11 on
    // Windows, while a QOpenGLWidget puts its whole top-level window on
    // OpenGL, and texture widgets in one window must share one backend. Left
    // at the default, the page's QQuickWidget never initialises and Chromium
    // lays the page out at 0x0: loaded, scripted, in the accessibility tree,
    // and drawn nowhere (measured: tests/web_probe.cpp, page "0x0" with a
    // QOpenGLWidget in the window, "800x600" with this call).
    static void prepareApplication();

    WebOverlay(OcctViewWidget* viewport, AnchorSource anchor, QObject* parent = nullptr);
    ~WebOverlay() override;

    // The built page beside the executable (web/index.html), unless
    // FURNIFY_WEB_URL names another - the Vite dev server, for hot reload.
    static QUrl defaultUrl();
    // The spike's anchor: the top centre of the document's first body.
    static AnchorSource topOfFirstBody(const DocumentModel* document);
    void load(const QUrl& url);

    QWebEngineView* view() const { return myView; }
    SpikeBridge* bridge() const { return myBridge; }
    bool loaded() const;
    bool loadOk() const;
    QRegion hitRegion() const { return myHitRegion; }

    // Re-projects the anchor and tells the page. Called on every camera
    // change; callers also call it after the document changes.
    void pushAnchor();

    // --- measurements the spike harness reads --------------------------------
    struct ChipSample {
        int shownRev = 0;      // the revision the page painted
        int latestSentRev = 0; // the newest revision C++ had sent by then
        double latencyMs = 0;  // send of shownRev -> the page's report
    };
    const std::vector<ChipSample>& chipSamples() const { return myChipSamples; }
    void clearChipSamples() { myChipSamples.clear(); }
    int anchorRevision() const { return myRev; }
    QPointF lastAnchorPx() const { return myLastAnchorPx; }

    // A/B switch for the compositing gate: the view's render widget drawn
    // as a stack-on-top texture or not. Default OFF: once Qt Quick runs on
    // OpenGL the page composites correctly without it, at the same cost.
    void setStackOnTop(bool on);
    bool stackOnTop() const { return myStackOnTop; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyMask();
    void applyRenderWidgetAttributes();

    void setCameraMoving(bool moving);

    QPointer<OcctViewWidget> myViewport;
    bool myCameraMoving = false;
    // True from a right press to its release. The chip is faded then, so
    // its position is not sent at all: a faded chip that still moves
    // makes Chromium render a frame per orbit step for nothing.
    bool myOrbiting = false;
    QTimer* myWheelSettle = nullptr;
    AnchorSource myAnchor;
    class WebHost* myHost = nullptr;
    QWebEngineView* myView = nullptr;
    SpikeBridge* myBridge = nullptr;
    QRegion myHitRegion;
    bool myStackOnTop = false;

    int myRev = 0;
    QPointF myLastAnchorPx;
    QElapsedTimer myClock;
    std::vector<qint64> mySentNs;   // index = revision
    std::vector<ChipSample> myChipSamples;
};
