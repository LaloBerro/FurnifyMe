#include "SceneWindow.h"

#include "AddPieceCard.h"
#include "AppBar.h"
#include "FurnitureStore.h"
#include "OcctViewWidget.h"
#include "RenderStudio.h"
#include "ScenePiecesPanel.h"
#include "Theme.h"
#include "Toast.h"
#include "ViewportOverlay.h"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>

namespace {
// The gap between a new piece and whatever is already in the scene. A hand's
// width of real furniture - enough that the two read as separate objects at
// the framing a scene opens on.
constexpr double kNewPieceGapMm = 120.0;
}  // namespace

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
    myNewPieceAction = new QAction(tr("New piece"), this);
    myNewPieceAction->setToolTip(tr("Put a furniture from the library into this scene"));
    connect(myNewPieceAction, &QAction::triggered, this, [this] {
        if (!myAddPiece || !myStore) return;
        // Re-read on every open: a furniture made since this window opened is
        // one the user expects to find in the list.
        myAddPiece->showFor(myStore->listFurniture());
        if (myOverlay) myOverlay->relayout();
    });
    file->addAction(myNewPieceAction);
    file->addSeparator();
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

    // A REFUSAL HAS TO REPORT, and in this app a refusal reports through a
    // Failure toast - which cannot be silenced - rather than a status line
    // nobody reads. A status bar carried the first version of openScene()'s
    // refusal and that was the weaker surface.
    myToasts = new ToastHost(myView, this);

    // Not anchored by the overlay: the card is one question in the middle of
    // the viewport, not a rail or a drawer, so it is placed by the window.
    myAddPiece = new AddPieceCard(myView);
    connect(myAddPiece, &AddPieceCard::chosen, this,
            [this](const QString& id) { addPiece(id); });

    connect(myPieces, &ScenePiecesPanel::renameCommitted, this,
            [this](int pieceId, const QString& name) { renamePiece(pieceId, name); });
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

gp_Trsf SceneWindow::placementForNewPiece(const DocumentModel& furniture) const
{
    gp_Trsf placement;
    if (furniture.solids().empty()) return placement;

    // How far right everything already in the scene reaches, and how far LEFT
    // this furniture's own geometry starts - both needed, because a furniture
    // modelled away from the origin would otherwise land with a gap or an
    // overlap depending on where its author happened to draw it.
    double occupiedRight = 0.0;
    bool anything = false;
    for (const auto& entry : myPieceShapes) {
        for (const TopoDS_Shape& shape : entry.second) {
            if (shape.IsNull()) continue;
            Bnd_Box box;
            BRepBndLib::Add(shape, box);
            if (box.IsVoid()) continue;
            double xMin, yMin, zMin, xMax, yMax, zMax;
            box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
            // Measured where the piece actually STANDS, so its own placement
            // counts - the shapes kept here are the untransformed ones.
            const SceneModel::Piece* piece = myScene.piece(entry.first);
            const double shift = piece ? piece->placement.TranslationPart().X() : 0.0;
            occupiedRight = anything ? std::max(occupiedRight, xMax + shift) : xMax + shift;
            anything = true;
        }
    }
    if (!anything) return placement;

    double ownLeft = 0.0;
    bool ownAny = false;
    for (const DocumentModel::Solid& solid : furniture.solids()) {
        if (solid.shape.IsNull()) continue;
        Bnd_Box box;
        BRepBndLib::Add(solid.shape, box);
        if (box.IsVoid()) continue;
        double xMin, yMin, zMin, xMax, yMax, zMax;
        box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        ownLeft = ownAny ? std::min(ownLeft, xMin) : xMin;
        ownAny = true;
    }
    if (!ownAny) return placement;

    placement.SetTranslation(gp_Vec(occupiedRight - ownLeft + kNewPieceGapMm, 0.0, 0.0));
    return placement;
}

void SceneWindow::displayPiece(int pieceId, const DocumentModel& furniture,
                               const gp_Trsf& placement)
{
    std::vector<int> bodies;
    std::vector<TopoDS_Shape> shapes;
    std::vector<int> grainAcross;

    // WHICH WOOD: the furniture's own saved look. A furniture records a look
    // per MATERIAL NAME and does not record which of them was last on screen,
    // so with several the first recorded one is taken - see the header note.
    DocumentModel::MaterialLook look;
    const auto& looks = furniture.materialLooks();
    if (!looks.empty()) look = looks.front();

    for (const DocumentModel::Solid& solid : furniture.solids()) {
        if (solid.shape.IsNull()) continue;
        const int bodyId = myNextBodyId++;
        TopoDS_Shape placed = solid.shape;
        if (placement.Form() != gp_Identity) {
            BRepBuilderAPI_Transform move(solid.shape, placement, true);
            if (move.IsDone()) placed = move.Shape();
        }
        myView->displaySolid(bodyId, placed);

        OcctViewWidget::BodyWood wood;
        wood.red = look.red;
        wood.green = look.green;
        wood.blue = look.blue;
        wood.brightness = look.brightness;
        wood.surface = look.surface;
        wood.metal = look.metal;
        wood.grainSize = look.grainSize;
        wood.grainAngle = look.grainAngle;
        myView->setBodyWood(bodyId, wood);

        // The one exception a real piece of furniture needs, carried across
        // with everything else: a rail cut the other way out of the same
        // board was saved that way and must render that way.
        if (furniture.bodyGrainAcross(solid.id)) grainAcross.push_back(bodyId);

        bodies.push_back(bodyId);
        shapes.push_back(solid.shape);
        myBodyPiece[bodyId] = pieceId;
    }

    myPieceBodies[pieceId] = bodies;
    myPieceShapes[pieceId] = shapes;

    if (!grainAcross.empty()) {
        // Accumulated HERE rather than read back off the viewport: this
        // window hands out every scene-local id, so it is the one place that
        // can answer for the whole set, and the viewport needs no accessor it
        // would otherwise have only one caller for.
        myGrainAcrossBodies.insert(myGrainAcrossBodies.end(), grainAcross.begin(),
                                   grainAcross.end());
        myView->setBodyGrainAcross(myGrainAcrossBodies);
    }
}

bool SceneWindow::addPiece(const QString& furnitureId)
{
    if (!myStore) return false;

    QString name;
    for (const FurnitureStore::FurnitureInfo& info : myStore->listFurniture()) {
        if (info.id == furnitureId) name = info.name;
    }

    QString error;
    DocumentModel furniture;
    const bool loaded = myStore->loadFurniture(furnitureId, furniture, &error);

    // THE PIECE IS ADDED EITHER WAY. A reference the scene drops on a failed
    // read is a reference the user never learns was broken, and saving the
    // scene would then quietly delete it from the file.
    const QString pieceName = name.isEmpty() ? tr("Piece") : name;
    const int pieceId = myScene.addPiece(furnitureId.toStdString(), pieceName.toStdString());
    if (pieceId == 0) return false;

    if (!loaded) {
        myPieceBodies[pieceId] = {};
        myPieceShapes[pieceId] = {};
        myBrokenPieces[pieceId] =
            error.isEmpty() ? tr("This furniture could not be read") : error;
        if (myToasts) {
            myToasts->show(tr("Couldn't put %1 in this scene — %2")
                               .arg(pieceName, myBrokenPieces[pieceId]),
                           Toast::Kind::Failure, false);
        }
        refreshSurfaces();
        return false;
    }

    const gp_Trsf placement = placementForNewPiece(furniture);
    myScene.setPlacement(pieceId, placement);
    displayPiece(pieceId, furniture, placement);
    refreshSurfaces();
    return true;
}

bool SceneWindow::renamePiece(int pieceId, const QString& name)
{
    if (!myScene.setPieceName(pieceId, name.toStdString())) return false;
    refreshSurfaces();
    return true;
}

std::vector<int> SceneWindow::bodyIdsForPiece(int pieceId) const
{
    const auto at = myPieceBodies.find(pieceId);
    return at == myPieceBodies.end() ? std::vector<int>() : at->second;
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
            const auto broken = myBrokenPieces.find(piece.id);
            if (broken != myBrokenPieces.end()) row.reason = broken->second;
            rows.push_back(row);
        }
        myPieces->setRows(rows);
    }
    if (myAddPiece && myAddPiece->isVisible() && myView) {
        // Centred by the window, not anchored by the overlay - a question is
        // not a rail. Snapped, so its near edge lands on whole device pixels.
        const int cardX = (myView->width() - myAddPiece->width()) / 2;
        const int cardY = (myView->height() - myAddPiece->height()) / 2;
        const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
        const double dpr = myView->devicePixelRatioF();
        myAddPiece->move(Theme::snapToDevicePixels(std::max(0, cardX), origin.x(), dpr),
                         Theme::snapToDevicePixels(std::max(0, cardY), origin.y(), dpr));
        myAddPiece->raise();
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
