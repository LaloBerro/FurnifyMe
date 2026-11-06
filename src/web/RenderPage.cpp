#include "RenderPage.h"

#include "RenderBridge.h"
#include "RenderSettingsPanel.h"
#include "RenderStudio.h"
#include "Theme.h"
#include "ThemeBridge.h"
#include "WebHost.h"

#include <QWebEnginePage>
#include <QWebEngineView>

RenderPage* RenderPage::attach(RenderStudio* studio, QWidget* window)
{
    if (!studio || !studio->panel() || !window) return nullptr;
    return new RenderPage(studio, window);
}

RenderPage::RenderPage(RenderStudio* studio, QWidget* window) : QObject(window), myStudio(studio)
{
    RenderSettingsPanel* panel = studio->panel();
    myHost = new WebHost(window, WebHost::Ground::Opaque, this);
    QWebEngineView* view = myHost->view();
    // The Qt panel's own width, so the viewport beside it is the same size
    // whichever of the two is docked.
    view->setFixedWidth(panel->minimumWidth());   // the panel's own setFixedWidth()
    view->setMinimumHeight(1);

    myBridge = new RenderBridge(panel, this);
    myTheme = new ThemeBridge(this);
    myHost->registerObject(QStringLiteral("render"), myBridge);
    myHost->registerObject(QStringLiteral("theme"), myTheme);

    connect(myBridge, &RenderBridge::readyChanged, this, [this](bool ready) {
        refuseFocus();
        if (ready) setLive(true);
    });
    connect(myHost, &WebHost::loadFinished, this, [this](bool ok) {
        refuseFocus();
        if (!ok) setLive(false);
    });
    connect(view->page(), &QWebEnginePage::renderProcessTerminated, this, [this] {
        myBridge->forgetReady();
        setLive(false);
    });
    refuseFocus();

    studio->setHtmlPanel(view, [this] { return myLive; });
    myHost->load(WebHost::pageUrl(QStringLiteral("render")));
}

QWidget* RenderPage::view() const { return myHost ? myHost->view() : nullptr; }

void RenderPage::setLive(bool live)
{
    if (myLive == live) return;
    myLive = live;
    // The dock follows at once - the Qt panel back the moment the page cannot
    // draw, the page in its place the moment it can.
    if (myStudio) myStudio->htmlPanelLiveChanged();
    emit liveChanged(live);
}

void RenderPage::refuseFocus()
{
    // No chip takes keyboard focus (F17), at the Qt level too: a press on the
    // panel must not move focus out of whatever field held it.
    QWebEngineView* view = myHost->view();
    view->setFocusPolicy(Qt::NoFocus);
    if (QWidget* proxy = view->focusProxy()) proxy->setFocusPolicy(Qt::NoFocus);
}
