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
#include <QPointer>
#include <QUrl>

class QWebChannel;
class QWebEngineProfile;
#include <QWebEngineView>
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
    // Whether `page` is there to load: its built file beside the executable,
    // or - with FURNIFY_WEB_URL set - assumed served. main.cpp asks this
    // before choosing the HTML hub, so a deploy that lost the page still
    // opens the app (on the Qt hub) instead of a blank window.
    static bool pageAvailable(const QString& page);

    // The view is created as a child of `parent`; this object is owned by
    // `owner` (the parent when null).
    WebHost(QWidget* parent, Ground ground, QObject* owner = nullptr);
    ~WebHost() override;

    void registerObject(const QString& name, QObject* object);
    void load(const QUrl& url);

    QWebEngineView* view() const { return myView; }
    bool loaded() const { return myLoaded; }
    bool loadOk() const { return myLoadOk; }

    // A page whose render process dies is loaded again (migration Phase 6:
    // the Qt surfaces are a bridge for the gap, never the destination), at
    // most kMaxCrashReloads times a session - a page that keeps dying keeps
    // the Qt fallback instead of a reload loop. On by default; a test that
    // reads the fallback turns it off first, and turning it back on while the
    // page is dead reloads at once.
    static constexpr int kMaxCrashReloads = 3;
    static constexpr int kCrashReloadMs = 400;
    void setReloadOnCrash(bool on);
    int crashReloads() const { return myCrashReloads; }

signals:
    void loadFinished(bool ok);
    // The render process died (not a normal end); a reload may follow.
    void crashed();

protected:
    // Ctrl+wheel would zoom the page, and a zoomed page draws its title strip
    // somewhere other than where Windows hit-tests it; pages here are laid out
    // in the app's own type scale, which the Settings drawer already sizes.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void injectWebChannelScript();
    void watchRenderWidget();

    QWebEngineProfile* myProfile = nullptr;
    // Guarded: the view is a child of some widget, which may delete it first
    // (EditorChrome's view goes with the viewport), and deleting it again
    // here crashed the app at exit.
    QPointer<QWebEngineView> myView;
    QWebChannel* myChannel = nullptr;
    bool myLoaded = false;
    bool myReloadOnCrash = true;
    bool myDead = false;
    int myCrashReloads = 0;
    QUrl myUrl;
    void reloadAfterCrash();
    bool myLoadOk = false;
};
