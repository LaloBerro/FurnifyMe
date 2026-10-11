#include "WebHost.h"

#include <QCoreApplication>
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

WebHost::WebHost(QWidget* parent, Ground ground, QObject* owner)
    : QObject(owner ? owner : parent)
{
    // Off the record: no cache or cookies written anywhere for a page that is
    // part of the application.
    myProfile = new QWebEngineProfile(this);
    myView = new QWebEngineView(parent);
    auto* page = new QWebEnginePage(myProfile, myView);
    myView->setPage(page);
    if (ground == Ground::Transparent) {
        page->setBackgroundColor(Qt::transparent);
        myView->setAttribute(Qt::WA_TranslucentBackground);
    }
    page->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
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
        emit loadFinished(ok);
    });
}

WebHost::~WebHost()
{
    // The view must go before the profile its page was built on, which Qt
    // WebEngine requires; the view is a child of some widget, not of this.
    delete myView;
}

void WebHost::registerObject(const QString& name, QObject* object)
{
    myChannel->registerObject(name, object);
}

void WebHost::load(const QUrl& url)
{
    myLoaded = false;
    myLoadOk = false;
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
