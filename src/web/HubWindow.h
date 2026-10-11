#pragma once

// The library hub as an HTML page (migration Phase 2) - a frameless window
// whose whole content is web/hub, talking to HubModel over the web channel.
// It stands in for SelectorWindow (the Qt hub, kept until Phase 6) with the
// same signals and refresh(), so EditorSelectorHandoff::wire() drives either.
//
// The title strip is the page's; WindowChrome answers Windows' hit tests from
// the rectangles the page reports (reportChrome), so dragging the strip moves
// the window and the maximize button raises Windows 11's snap layouts.

#include "WindowChrome.h"

#include <QRect>
#include <QWidget>

class FurnitureStore;
class HubModel;
class ThemeBridge;
class WebHost;
class HubWindowControls;

class HubWindow : public QWidget
{
    Q_OBJECT
public:
    explicit HubWindow(FurnitureStore& store, QWidget* parent = nullptr);

    void refresh();
    int furnitureCount() const;
    // What a point of the window is, for Windows' hit test: caption, the
    // maximize button, or the page's own. From the rects the page reported;
    // until it has (or if it never loads) the top 32 px are caption with the
    // maximize slot where the page would draw it, so the window can always be
    // moved, maximized and closed from its system menu.
    WindowChrome::Hit hitTest(const QPoint& p) const;

    HubModel* model() const { return myModel; }
    WebHost* host() const { return myHost; }
    HubWindowControls* controls() const { return myControls; }

signals:
    void furnitureChosen(const QString& id);
    void createRequested();
    void sceneChosen(const QString& id);
    void createSceneRequested();
    void closing();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void sizeToLibrary();

    FurnitureStore& myStore;
    HubModel* myModel = nullptr;
    ThemeBridge* myTheme = nullptr;
    HubWindowControls* myControls = nullptr;
    WebHost* myHost = nullptr;
};

// The page's `win` object: the window controls, and where the page drew them.
class HubWindowControls : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)
public:
    explicit HubWindowControls(QWidget* window);
    bool maximized() const;

    QRect titleBar() const { return myTitleBar; }
    QRect maxButton() const { return myMaxButton; }
    QRect buttons() const { return myButtons; }

public slots:
    void minimize();
    void toggleMaximize();
    void close();
    void reportChrome(const QVariantMap& chrome);

signals:
    void maximizedChanged(bool maximized);
    void maxHovered(bool hovered);
    void maxPressed(bool pressed);
    void chromeReported();

private:
    QWidget* myWindow;
    QRect myTitleBar;
    QRect myMaxButton;
    QRect myButtons;
};
