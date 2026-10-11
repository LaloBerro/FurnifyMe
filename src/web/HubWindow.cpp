#include "HubWindow.h"

#include "FurnitureStore.h"
#include "IconSet.h"
#include "ThemeBridge.h"
#include "WebHost.h"
#include "WindowChrome.h"
#include "session/HubModel.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWebEngineView>

#include <algorithm>

namespace {

// The Qt hub's own measurements (SelectorWindow.cpp), which the page draws
// at: three columns of 260 px cells, 197 px tall with the under-row, 24 px
// apart, inside 32 px margins (16 on top) under a 32 px title strip and a
// 28 px header with 16 px between it and the grid.
constexpr int kColumns = 3;
constexpr int kCellW = 260;
constexpr int kCellH = 197;
constexpr int kGap = 24;
constexpr int kMargin = 32;
constexpr int kTitleH = 32;
constexpr int kHeaderH = 28;
constexpr int kHeaderGap = 16;
constexpr int kScrollLane = 24;

QRect rectFrom(const QVariant& v)
{
    const QVariantMap m = v.toMap();
    return QRect(m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
                 m.value(QStringLiteral("w")).toInt(), m.value(QStringLiteral("h")).toInt());
}

}  // namespace

// --- the page's window controls ------------------------------------------------

HubWindowControls::HubWindowControls(QWidget* window)
    : QObject(window), myWindow(window)
{
}

bool HubWindowControls::maximized() const { return myWindow->isMaximized(); }

void HubWindowControls::minimize() { myWindow->showMinimized(); }

void HubWindowControls::toggleMaximize()
{
    // Reached from the page only where the maximize button's click is client
    // traffic (off Windows, and in tests); on Windows WindowChrome takes it.
    if (myWindow->isMaximized()) myWindow->showNormal();
    else myWindow->showMaximized();
}

void HubWindowControls::close() { myWindow->close(); }

void HubWindowControls::reportChrome(const QVariantMap& chrome)
{
    myTitleBar = rectFrom(chrome.value(QStringLiteral("titleBar")));
    myMaxButton = rectFrom(chrome.value(QStringLiteral("maxButton")));
    myButtons = rectFrom(chrome.value(QStringLiteral("buttons")));
    emit chromeReported();
}

// --- the window ------------------------------------------------------------------

HubWindow::HubWindow(FurnitureStore& store, QWidget* parent)
    : QWidget(parent, Qt::Window), myStore(store)
{
    setWindowTitle(tr("FurnifyMe"));
    setWindowIcon(IconSet::appIcon());

    myModel = new HubModel(myStore, this);
    myTheme = new ThemeBridge(this);
    myControls = new HubWindowControls(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    myHost = new WebHost(this, WebHost::Ground::Opaque);
    layout->addWidget(myHost->view());
    myHost->registerObject(QStringLiteral("hub"), myModel);
    myHost->registerObject(QStringLiteral("win"), myControls);
    myHost->registerObject(QStringLiteral("theme"), myTheme);

    connect(myModel, &HubModel::furnitureChosen, this, &HubWindow::furnitureChosen);
    connect(myModel, &HubModel::sceneChosen, this, &HubWindow::sceneChosen);
    connect(myModel, &HubModel::createRequested, this, &HubWindow::createRequested);
    connect(myModel, &HubModel::createSceneRequested, this, &HubWindow::createSceneRequested);

    sizeToLibrary();

    // The title strip is the page's. Inside it - and outside its three
    // buttons - is caption (drag, double-click maximizes, the system menu);
    // the maximize button answers MaxButton so the snap layouts appear;
    // everything else is the page's own.
    HubWindowControls* controls = myControls;
    WindowChrome::attach(
        this,
        [controls](const QPoint& p) {
            if (controls->maxButton().contains(p)) return WindowChrome::Hit::MaxButton;
            if (controls->titleBar().contains(p) && !controls->buttons().contains(p))
                return WindowChrome::Hit::Caption;
            return WindowChrome::Hit::Client;
        },
        [controls](bool hovered) { emit controls->maxHovered(hovered); },
        [controls](bool pressed) { emit controls->maxPressed(pressed); });

    myHost->load(WebHost::pageUrl(QStringLiteral("hub")));
}

void HubWindow::refresh()
{
    myModel->refresh();
}

int HubWindow::furnitureCount() const
{
    return myModel->furnitureList().size();
}

void HubWindow::sizeToLibrary()
{
    // The Qt hub's own rule: as many rows as the New card plus the furniture
    // fill, capped at three - the scrollbar carries the rest - and never
    // taller than the screen allows.
    const int cells = 1 + furnitureCount();
    const int rows = std::clamp((cells + kColumns - 1) / kColumns, 1, 3);
    const int gridW = kColumns * kCellW + (kColumns - 1) * kGap;
    const int gridH = rows * kCellH + (rows - 1) * kGap;
    const int width = gridW + 2 * kMargin + kScrollLane;
    const int height = kTitleH + kMargin / 2 + kHeaderH + kHeaderGap + gridH + kMargin;
    QSize wanted(width, height);
    if (const QScreen* screen = QGuiApplication::primaryScreen())
        wanted = wanted.boundedTo(screen->availableSize() * 9 / 10);
    resize(wanted);
    setMinimumWidth(std::min(wanted.width(), width));
}

void HubWindow::closeEvent(QCloseEvent* event)
{
    // As SelectorWindow: report the gesture; what closing the hub MEANS is
    // EditorSelectorHandoff::wire()'s call.
    emit closing();
    QWidget::closeEvent(event);
}

void HubWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
        emit myControls->maximizedChanged(isMaximized());
    QWidget::changeEvent(event);
}
