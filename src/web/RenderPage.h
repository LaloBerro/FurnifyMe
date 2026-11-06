#pragma once

// THE RENDER SETTINGS PANEL AS A PAGE, docked (migration Phase 3e). One opaque
// page in its own WebHost, carrying the `render` object (RenderBridge, over the
// studio's Qt panel) and `theme`. RenderStudio docks this page's view beside the
// viewport in the Qt panel's place while the page is live - from the moment it
// says it is listening until it fails to load or its render process dies, when
// the Qt panel docks back at once, every value intact, since it never stopped
// being the state.
//
// attach() is the one way in, and both windows that have a RenderStudio call
// it: MainWindow::useHtmlChrome() for the editor and main.cpp for the scene
// window, under the same rule as the chrome (the page present).
// gui_smoke calls neither, so the suite's render mode docks the Qt panel.

#include <QObject>
#include <QPointer>

class QWidget;
class RenderBridge;
class RenderStudio;
class ThemeBridge;
class WebHost;

class RenderPage : public QObject
{
    Q_OBJECT
public:
    // Builds the page for `studio`'s panel as a child of `window` (hidden until
    // docked), hands its view to the studio and starts loading. Owned by
    // `window`. Null when the studio has no panel.
    static RenderPage* attach(RenderStudio* studio, QWidget* window);

    bool isLive() const { return myLive; }
    QWidget* view() const;
    WebHost* host() const { return myHost; }
    RenderBridge* bridge() const { return myBridge; }

signals:
    void liveChanged(bool live);

private:
    RenderPage(RenderStudio* studio, QWidget* window);
    void setLive(bool live);
    void refuseFocus();

    // Guarded: the studio is a sibling child of the window, and the page's
    // view can report a failed load or a dead render process after it.
    QPointer<RenderStudio> myStudio;
    WebHost* myHost = nullptr;
    RenderBridge* myBridge = nullptr;
    ThemeBridge* myTheme = nullptr;
    bool myLive = false;
};
