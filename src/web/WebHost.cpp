#include "WebHost.h"

#include "Theme.h"

#include <QCoreApplication>
#include <QWheelEvent>
#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QSGRendererInterface>
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

void WebHost::load(const QUrl& url)
{
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
