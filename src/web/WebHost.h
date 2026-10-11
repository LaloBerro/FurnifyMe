#pragma once

// One HTML page in a QWebEngineView (migration Phase 2): the off-the-record
// profile, the web channel, qwebchannel.js injected before any page script,
// and where the page's file is. Every page in the app is hosted through this
// - the hub filling its window, the editor's overlay over the viewport - so
// the two findings the Phase 0 spike paid for live in one place:
//
// - prepareApplication(), BEFORE QApplication: Qt Quick (which hosts every
//   QWebEngineView's page) defaults to Direct3D 11 on Windows, while a
//   QOpenGLWidget puts its whole window on OpenGL, and texture widgets in one
//   window must share a backend. Left at the default, a page beside the
//   viewport loads, runs scripts, appears in the accessibility tree - and is
//   laid out at 0x0 and drawn nowhere (tests/web_probe.cpp).
// - Ground::Transparent clears the page to transparent, so whatever the page
//   does not draw shows the widget underneath.

#include <QObject>
#include <QUrl>

class QWebChannel;
class QWebEngineProfile;
class QWebEngineView;
class QWidget;

class WebHost : public QObject
{
    Q_OBJECT
public:
    enum class Ground { Opaque, Transparent };

    static void prepareApplication();
    // The built page beside the executable: <exe dir>/web/<page>/index.html.
    // FURNIFY_WEB_URL, when set, names a dev server instead (Vite, for hot
    // reload): <FURNIFY_WEB_URL>/<page>.html.
    static QUrl pageUrl(const QString& page);

    // The view is created as a child of `parent`; this object is owned by
    // `owner` (the parent when null).
    WebHost(QWidget* parent, Ground ground, QObject* owner = nullptr);
    ~WebHost() override;

    void registerObject(const QString& name, QObject* object);
    void load(const QUrl& url);

    QWebEngineView* view() const { return myView; }
    bool loaded() const { return myLoaded; }
    bool loadOk() const { return myLoadOk; }

signals:
    void loadFinished(bool ok);

private:
    void injectWebChannelScript();

    QWebEngineProfile* myProfile = nullptr;
    QWebEngineView* myView = nullptr;
    QWebChannel* myChannel = nullptr;
    bool myLoaded = false;
    bool myLoadOk = false;
};
