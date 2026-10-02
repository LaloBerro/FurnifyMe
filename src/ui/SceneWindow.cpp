#include "SceneWindow.h"

#include "AppBar.h"
#include "FurnitureStore.h"
#include "OcctViewWidget.h"
#include "RenderStudio.h"
#include "ScenePiecesPanel.h"
#include "Theme.h"
#include "ViewportOverlay.h"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

SceneWindow::SceneWindow(FurnitureStore* store, QWidget* parent)
    : QMainWindow(parent), myStore(store)
{
    setWindowTitle(tr("FurnifyMe — Scene"));

    myView = new OcctViewWidget(this);
    setCentralWidget(myView);

    buildMenus();
    buildOverlay();
    refreshSurfaces();
}

void SceneWindow::buildMenus()
{
    // ONLY what a scene can actually do. There is no Model menu at all, and no
    // sketch, extrude, boolean, joint, mirror, version or compare entry
    // anywhere - absent rather than disabled, because a control that exists
    // and refuses reads as broken.
    auto* file = menuBar()->addMenu(tr("&File"));

    myCloseSceneAction = new QAction(tr("Close scene"), this);
    myCloseSceneAction->setToolTip(tr("Go back to the library"));
    connect(myCloseSceneAction, &QAction::triggered, this, [this] { emit closeRequested(); });
    file->addAction(myCloseSceneAction);

    auto* view = menuBar()->addMenu(tr("&View"));
    myRenderModeAction = new QAction(tr("Render mode"), this);
    myRenderModeAction->setCheckable(true);
    myRenderModeAction->setToolTip(tr("Light the scene and frame a picture of it"));
    connect(myRenderModeAction, &QAction::toggled, this, [this](bool on) {
        if (myStudio) myStudio->setEnabled(on);
        refreshSurfaces();
    });
    view->addAction(myRenderModeAction);
}

void SceneWindow::buildOverlay()
{
    myOverlay = new ViewportOverlay(myView);

    // The pill leads the left column, as it does in the editor - same margin,
    // so the two read as one column rather than two floating cards.
    myAppBar = new AppBar(menuBar(), myView);
    myOverlay->addWidget(myAppBar, ViewportOverlay::Anchor::LeftEdge);

    myPieces = new ScenePiecesPanel(myView);
    myOverlay->addWidget(myPieces, ViewportOverlay::Anchor::TopLeft);

    // THE RENDER LAYER, the same class the furniture editor drives. Built
    // after the pieces list so its Back chip is the LAST TopLeft entry and
    // takes the corner only when the list above it is hidden - which is
    // exactly and only render mode.
    myStudio = new RenderStudio(myView, this, myOverlay, myAppBar, myRenderModeAction, this);
    connect(myStudio, &RenderStudio::enabledChanged, this, [this](bool) { refreshSurfaces(); });
}

bool SceneWindow::openScene(const QString& id)
{
    if (!myStore) return false;

    // SCRATCH-THEN-SWAP at this level too: loadScene() leaves `loaded`
    // untouched on a refusal, and nothing of this window's own state moves
    // until it has succeeded.
    SceneModel loaded;
    QString error;
    if (!myStore->loadScene(id, loaded, &error)) {
        statusBar()->showMessage(error.isEmpty() ? tr("That scene could not be opened") : error);
        return false;
    }

    myScene = loaded;
    mySceneId = id;
    mySceneName.clear();
    for (const FurnitureStore::SceneInfo& info : myStore->listScenes()) {
        if (info.id == id) mySceneName = info.name;
    }
    // The window NAMES what it is showing: a second window with no name on it
    // is one the user cannot tell from the editor at a glance.
    setWindowTitle(mySceneName.isEmpty() ? tr("FurnifyMe — Scene")
                                         : tr("FurnifyMe — %1").arg(mySceneName));
    refreshSurfaces();
    return true;
}

void SceneWindow::refreshSurfaces()
{
    // DERIVED on every state change, never set once at the control that moved
    // - the sibling-visibility law every surface in this app follows.
    if (myStudio) myStudio->refresh();
    if (myPieces) {
        myPieces->setVisible(!(myStudio && myStudio->isEnabled()));
        QVector<ScenePiecesPanel::Row> rows;
        rows.reserve(static_cast<int>(myScene.pieces().size()));
        for (const SceneModel::Piece& piece : myScene.pieces()) {
            ScenePiecesPanel::Row row;
            row.pieceId = piece.id;
            row.name = QString::fromStdString(piece.name);
            rows.push_back(row);
        }
        myPieces->setRows(rows);
    }
    if (myOverlay) myOverlay->relayout();
}

void SceneWindow::closeEvent(QCloseEvent* event)
{
    // The X QUITS, the way closing the editor does since Milestone 5 - it is
    // not a second route back to the hub. File -> Close scene is that route,
    // and it says so in its own tooltip.
    emit quitRequested();
    QMainWindow::closeEvent(event);
}
