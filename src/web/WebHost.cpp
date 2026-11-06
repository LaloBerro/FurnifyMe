#include "WebHost.h"

#include "Theme.h"

#include <QCoreApplication>
#include <QWheelEvent>
#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>

void WebHost::prepareApplication()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#ifdef Q_OS_WIN
    // THE PAGES ARE COMPOSITED IN SOFTWARE (T-083). GPU-composited, a page's
    // frames reach Qt's OpenGL from Chromium's Direct3D, and OCCT drawing in
    // the same process alongside them died inside the NVIDIA driver: a theme
    // switch or a quality change in a SECOND render-mode session, every time,
    // in chrome_smoke's sequence, at the same driver addresses whichever OCCT
    // call was on the stack (a synchronous GL debug context hid it, and
    // reported no GL error). Composited in software the same sequence passes,
    // the pages still draw, and the viewport's frame cost measured the same
    // (web_spike gate 2, 17.8 against 17.7 ms per move). NOT --use-angle=gl:
    // its shared images fail and the pages are not drawn at all. Read by
    // QtWebEngine when it starts, so it is set here, before QApplication; a
    // user's own flags are kept.
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!flags.contains("--disable-gpu-compositing")) {
        if (!flags.isEmpty()) flags += ' ';
        flags += "--disable-gpu-compositing";
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    }
#endif
}

QUrl WebHost::pageUrl(const QString& page)
{
    const QString dev = qEnvironmentVariable("FURNIFY_WEB_URL");
    if (!dev.isEmpty()) {
        QString base = dev;
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        return QUrl(base + QLatin1Char('/') + page + QStringLiteral(".html"));
    }
    return QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("web/%1/index.html").arg(page)));
}

bool WebHost::pageAvailable(const QString& page)
{
    const QUrl url = pageUrl(page);
    return !url.isLocalFile() || QFile::exists(url.toLocalFile());
}

namespace {

// A page that is part of the app goes nowhere else. A dropped file, a link,
// `location.href = ...` - each would replace the page, and the web channel is
// the PAGE's, not the URL's, so whatever replaced it would inherit every
// object registered on it. Only the URL the host loaded is accepted.
class AppPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;
    QUrl allowed;

protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override
    {
        if (!isMainFrame) return false;
        return url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment) ==
               allowed.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment);
    }
};

}  // namespace

WebHost::WebHost(QWidget* parent, Ground ground, QObject* owner)
    : QObject(owner ? owner : parent)
{
    // Off the record: no cache or cookies written anywhere for a page that is
    // part of the application.
    myProfile = new QWebEngineProfile(this);
    myView = new QWebEngineView(parent);
    auto* page = new AppPage(myProfile, myView);
    myView->setPage(page);
    myView->setAcceptDrops(false);
    if (ground == Ground::Transparent) {
        page->setBackgroundColor(Qt::transparent);
        myView->setAttribute(Qt::WA_TranslucentBackground);
        // Scrollbars stay ON: a page over the viewport has lists that scroll
        // and must show it (the Settings card's colour list, Phase 3b). Its
        // body is overflow: hidden, so the page itself never grows one.
    } else {
        // The window's own ground until the page paints - not a white flash.
        page->setBackgroundColor(Theme::chrome());
    }
    // The page is a file beside the exe and reads the library's thumbnails,
    // which are files too.
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    myView->setContextMenuPolicy(Qt::NoContextMenu);

    myChannel = new QWebChannel(this);
    page->setWebChannel(myChannel);
    injectWebChannelScript();

    connect(page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        myLoaded = true;
        myLoadOk = ok;
        watchRenderWidget();
        emit loadFinished(ok);
    });
    connect(page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int) {
                if (status == QWebEnginePage::NormalTerminationStatus) return;
                myDead = true;
                emit crashed();
                if (myReloadOnCrash) QTimer::singleShot(kCrashReloadMs, this, &WebHost::reloadAfterCrash);
            });
    myView->installEventFilter(this);
    watchRenderWidget();
}

void WebHost::watchRenderWidget()
{
    // The page draws into a child render widget (the view's focus proxy),
    // which is the one that receives the wheel; it can be replaced, so it is
    // (re)watched on every load.
    if (QWidget* render = myView->focusProxy()) {
        render->removeEventFilter(this);
        render->installEventFilter(this);
    }
}

bool WebHost::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Wheel &&
        (static_cast<QWheelEvent*>(event)->modifiers() & Qt::ControlModifier))
        return true;   // no page zoom - see the header
    return QObject::eventFilter(watched, event);
}

WebHost::~WebHost()
{
    // The view must go before the profile its page was built on, which Qt
    // WebEngine requires. It is a child of some widget, not of this, and that
    // widget may already have deleted it - the QPointer says so.
    delete myView.data();
}

void WebHost::registerObject(const QString& name, QObject* object)
{
    myChannel->registerObject(name, object);
}

void WebHost::setReloadOnCrash(bool on)
{
    myReloadOnCrash = on;
    if (on && myDead) reloadAfterCrash();
}

void WebHost::reloadAfterCrash()
{
    if (!myDead || !myReloadOnCrash || myCrashReloads >= kMaxCrashReloads || myUrl.isEmpty() || !myView) return;
    ++myCrashReloads;
    load(myUrl);
}

void WebHost::load(const QUrl& url)
{
    myUrl = url;
    myDead = false;
    myLoaded = false;
    myLoadOk = false;
    static_cast<AppPage*>(myView->page())->allowed = url;
    myView->load(url);
}

void WebHost::injectWebChannelScript()
{
    // qwebchannel.js ships inside Qt6WebChannel's own resources. Injected
    // rather than referenced by URL, so the page needs no qrc: access and the
    // same page runs from a file, from the Vite dev server, or anywhere else.
    QFile file(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("WebHost: qwebchannel.js is not in the resources");
        return;
    }
    QWebEngineScript script;
    script.setName(QStringLiteral("qwebchannel"));
    script.setSourceCode(QString::fromUtf8(file.readAll()));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    myView->page()->scripts().insert(script);
}
