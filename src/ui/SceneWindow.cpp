#include "SceneWindow.h"

#include "AddPieceCard.h"
#include "AppBar.h"
#include "FurnitureStore.h"
#include "OcctViewWidget.h"
#include "RenderSettingsPanel.h"
#include "RenderStudio.h"
#include "ModelingOps.h"
#include "ScenePiecesPanel.h"
#include "Theme.h"
#include "Toast.h"
#include "UnsavedCloseCard.h"
#include "ViewportOverlay.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
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
// The grid a moved piece lands on, and the editor's own step for the same
// gesture - a scene that snapped to a different number from the furniture
// inside it would make an aligned cabinet unalignable.
constexpr double kSnapStepMm = 10.0;
}  // namespace

SceneWindow::SceneWindow(FurnitureStore* store, QWidget* parent)
    : QMainWindow(parent), myStore(store)
{
    setWindowTitle(tr("FurnifyMe — Scene"));

    myView = new OcctViewWidget(this);
    setCentralWidget(myView);

    buildMenus();
    buildOverlay();

    // WHOLE BODIES, never faces or edges. The editor's auto selection exists
    // so a click can take the face or the edge a modelling gesture needs;
    // there is no gesture in a scene that wants either, and leaving it on
    // would arbitrate between candidates no tool here can use.
    myView->setSelectionMode(OcctViewWidget::SelectionMode::Solid);
    myView->setSnap(true, kSnapStepMm);
    // WOOD ON. Per-piece wood is the spec's own fork, and the per-body
    // material is read in exactly one place - the wood overlays - which exist
    // only while this is true. Without it every piece's saved look was stored
    // and never drawn, and a scene of an oak table and a walnut chair rendered
    // both in the plain body material.
    myView->setRenderWood(true);

    connect(myView, &OcctViewWidget::selectionChanged, this, [this] { refreshSelection(); });

    // The gizmos are the viewport's own, and so is their snapping - a distance
    // arrives already rounded when Snap to Grid is on. What this window adds
    // is which piece the delta belongs to.
    connect(myView, &OcctViewWidget::moveDragged, this, [this](int axis, double distance) {
        if (mySelectedPiece == 0 || axis < 0 || axis > 2) return;
        if (!myDragging) {
            const SceneModel::Piece* piece = myScene.piece(mySelectedPiece);
            if (!piece) return;
            myDragBase = piece->placement;
            myDragging = true;
        }
        gp_Vec along(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0);
        gp_Trsf step;
        step.SetTranslation(along * distance);
        applyPlacement(mySelectedPiece, step.Multiplied(myDragBase));
    });
    connect(myView, &OcctViewWidget::moveReleased, this, [this](bool dragged) {
        commitDrag(dragged);
    });
    connect(myView, &OcctViewWidget::rotateDragged, this, [this](int axis, double degrees) {
        if (mySelectedPiece == 0 || axis < 0 || axis > 2) return;
        if (!myDragging) {
            const SceneModel::Piece* piece = myScene.piece(mySelectedPiece);
            if (!piece) return;
            myDragBase = piece->placement;
            myDragging = true;
        }
        // About the piece's own pivot, never the world origin: a turn about a
        // point the piece is not standing on is a move as well as a turn.
        const gp_Pnt pivot = myView->moveGizmoPivot();
        const gp_Dir dir(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0);
        gp_Trsf turn;
        turn.SetRotation(gp_Ax1(pivot, dir), degrees * M_PI / 180.0);
        applyPlacement(mySelectedPiece, turn.Multiplied(myDragBase));
    });
    connect(myView, &OcctViewWidget::rotateReleased, this, [this](bool dragged) {
        commitDrag(dragged);
    });

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
    connect(myCloseSceneAction, &QAction::triggered, this, [this] {
        if (askBeforeClosing(CloseRoute::Library)) return;
        emit closeRequested();
    });
    myNewPieceAction = new QAction(tr("New piece"), this);
    myNewPieceAction->setToolTip(tr("Put a furniture from the library into this scene"));
    connect(myNewPieceAction, &QAction::triggered, this, [this] {
        if (!myAddPiece || !myStore) return;
        // Re-read on every open: a furniture made since this window opened is
        // one the user expects to find in the list.
        myAddPiece->showFor(myStore->listFurniture());
        // refreshSurfaces(), NOT relayout(). The centring and the
        // after-the-layout raise both live in refreshSurfaces; calling the
        // overlay directly skipped them, so the card came up at its default
        // 0,0 - behind the pill and the pieces list, with its own title
        // clipped off the top-left corner.
        refreshSurfaces();
    });
    file->addAction(myNewPieceAction);

    // AN EXPORT ROUTE. A scene could be rendered and not saved as a picture by
    // any gesture at all - the suite reached saveSnapshot() directly, which is
    // a viewport method and not a door the user has. Taking the picture is the
    // whole point of the window.
    myScreenshotAction = new QAction(tr("Save Screenshot"), this);
    myScreenshotAction->setToolTip(tr("Write the picture to a file"));
    connect(myScreenshotAction, &QAction::triggered, this, [this] {
        const QString suggested =
            (mySceneName.isEmpty() ? tr("Screenshot") : mySceneName) + QStringLiteral(".png");
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save Screenshot"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                .filePath(suggested),
            tr("PNG image (*.png)"));
        if (path.isEmpty()) return;
        if (!myView->saveSnapshot(path) && myToasts) {
            myToasts->show(tr("Screenshot failed — Couldn't save the image to %1 — "
                              "Check that the folder exists and isn't read-only")
                               .arg(path),
                           Toast::Kind::Failure, false);
        }
    });
    file->addAction(myScreenshotAction);

    mySaveAction = new QAction(tr("Save"), this);
    mySaveAction->setShortcut(QKeySequence::Save);
    mySaveAction->setToolTip(tr("Write this scene to the library"));
    connect(mySaveAction, &QAction::triggered, this, [this] { save(); });
    file->addAction(mySaveAction);

    file->addSeparator();
    file->addAction(myCloseSceneAction);

    // THE TWO TOOLS, as real actions. setTool() existed with no caller
    // anywhere in the app - only the suite reached it - so Rotate was
    // unreachable and fifteen checks passed through a door no user has. An
    // action group makes the pair exclusive, and each mirrors myTool rather
    // than holding state of its own, which is this app's law for every
    // control.
    // FIT ALL. The one control whose job is finding things, and this window
    // had none: pieces land clear of each other along X, so a few of them walk
    // straight out of frame and nothing brings them back.
    myFitAllAction = new QAction(tr("Fit All"), this);
    myFitAllAction->setShortcut(QKeySequence(Qt::Key_F));
    myFitAllAction->setToolTip(tr("Frame everything in this scene"));
    connect(myFitAllAction, &QAction::triggered, this, [this] {
        if (myView) myView->fitAll();
    });

    auto* arrange = menuBar()->addMenu(tr("&Arrange"));
    auto* tools = new QActionGroup(this);
    tools->setExclusive(true);

    myMoveAction = new QAction(tr("Move"), this);
    myMoveAction->setCheckable(true);
    myMoveAction->setChecked(true);
    myMoveAction->setShortcut(QKeySequence(Qt::Key_G));
    myMoveAction->setToolTip(tr("Slide a piece along the floor"));
    connect(myMoveAction, &QAction::triggered, this, [this] { setTool(Tool::Move); });
    tools->addAction(myMoveAction);
    arrange->addAction(myMoveAction);

    myRotateAction = new QAction(tr("Rotate"), this);
    myRotateAction->setCheckable(true);
    myRotateAction->setShortcut(QKeySequence(Qt::Key_R));
    myRotateAction->setToolTip(tr("Turn a piece where it stands"));
    connect(myRotateAction, &QAction::triggered, this, [this] { setTool(Tool::Rotate); });
    tools->addAction(myRotateAction);
    arrange->addAction(myRotateAction);

    auto* view = menuBar()->addMenu(tr("&View"));
    myRenderModeAction = new QAction(tr("Render mode"), this);
    myRenderModeAction->setCheckable(true);
    myRenderModeAction->setToolTip(tr("Light the scene and frame a picture of it"));
    connect(myRenderModeAction, &QAction::toggled, this, [this](bool on) {
        if (myStudio) myStudio->setEnabled(on);
        refreshSurfaces();
    });
    view->addAction(myFitAllAction);
    view->addSeparator();
    view->addAction(myRenderModeAction);

    // Snapping is the user's fork too ("full move and rotate, and snapping"),
    // and it is ON by default here where it is off-by-default in the editor:
    // arranging furniture against a grid is the whole gesture, while modelling
    // a profile freehand is ordinary.
    mySnapAction = new QAction(tr("Snap to Grid"), this);
    mySnapAction->setCheckable(true);
    mySnapAction->setChecked(true);
    mySnapAction->setToolTip(tr("Land a moved piece on a whole step"));
    connect(mySnapAction, &QAction::toggled, this, [this](bool on) {
        if (myView) myView->setSnap(on, kSnapStepMm);
    });
    view->addAction(mySnapAction);
}

void SceneWindow::buildOverlay()
{
    myOverlay = new ViewportOverlay(myView);

    // TopLeft, NOT LeftEdge - and that distinction is the whole of a bug worth
    // recording. ViewportOverlay treats the LAST LeftEdge entry as the SPINE
    // and stretches it to the viewport's bottom edge; in the editor that is the
    // rail, with the pill sitting above it as a header. THIS WINDOW HAS NO
    // RAIL, so a LeftEdge pill was both the first entry and the last, became
    // the spine, and was stretched to the full height of the viewport - and
    // because AppBar's corner radius is half its own height, it painted as an
    // enormous ellipse down the left of the screen with the menus floating in
    // the middle of it. A header is only a header when something follows it.
    myAppBar = new AppBar(menuBar(), myView);
    myOverlay->addWidget(myAppBar, ViewportOverlay::Anchor::TopLeft);

    // Added after the pill, so it stacks below it in the same TopLeft column.
    myPieces = new ScenePiecesPanel(myView);
    myOverlay->addWidget(myPieces, ViewportOverlay::Anchor::TopLeft);

    // THE RENDER LAYER, the same class the furniture editor drives. Built
    // after the pieces list so its Back chip is the LAST TopLeft entry and
    // takes the corner only when the list above it is hidden - which is
    // exactly and only render mode.
    myStudio = new RenderStudio(myView, this, myOverlay, myAppBar, myRenderModeAction, this);
    connect(myStudio, &RenderStudio::enabledChanged, this, [this](bool) { refreshSurfaces(); });
    // Every viewport control on the render card raises this one signal; the
    // wiring itself is RenderStudio's, shared with the editor.
    connect(myStudio, &RenderStudio::settingsChanged, this, [this] { refreshSurfaces(); });
    if (myStudio->panel() && myScreenshotAction)
        myStudio->panel()->setShutterAction(myScreenshotAction);

    // THE SHOT ROWS ARE LIVE HERE TOO. RenderStudio re-emits the panel's three
    // shot signals, so leaving them unconnected would put a Save-shot control
    // and a list of rows on screen that did nothing - which is the inert
    // control this app refuses everywhere else.
    connect(myStudio, &RenderStudio::shotSaveRequested, this, [this] {
        const QString name = nextShotName();
        addShotToScene(currentShot(name));
        // A Note with no Undo: a shot is presentation, there is no checkpoint
        // for an undo to pop, and the row's own x is how one is taken back.
        if (myToasts) myToasts->show(tr("%1 saved").arg(name), Toast::Kind::Note, false);
    });
    connect(myStudio, &RenderStudio::shotApplied, this, [this](int index) {
        const std::vector<DocumentModel::Shot>& shots = myScene.shots();
        if (index < 0 || index >= static_cast<int>(shots.size())) return;
        const DocumentModel::Shot shot = shots[static_cast<std::size_t>(index)];
        applyShot(shot);
        statusBar()->showMessage(
            tr("%1 — camera, frame, lens and light").arg(QString::fromStdString(shot.name)));
    });
    connect(myStudio, &RenderStudio::shotRemoved, this, [this](int index) {
        const std::vector<DocumentModel::Shot>& shots = myScene.shots();
        if (index < 0 || index >= static_cast<int>(shots.size())) return;
        const QString name = QString::fromStdString(shots[static_cast<std::size_t>(index)].name);
        if (removeShotFromScene(static_cast<std::size_t>(index)))
            statusBar()->showMessage(tr("%1 forgotten").arg(name));
    });

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

    // The EDITOR'S OWN question and the editor's own dot. Nothing about either
    // is about furniture rather than scenes, and a second pair that looked
    // like them is exactly the drift this project keeps paying to avoid.
    myCloseCard = new UnsavedCloseCard(myView);
    connect(myCloseCard, &UnsavedCloseCard::saveChosen, this, [this] {
        const CloseRoute route = myCloseRoute;
        myCloseCard->hide();
        // Hidden BEFORE the save is attempted, so a Failure toast lands on a
        // clear viewport - and a refused save ABORTS the exit rather than
        // leaving by the back door.
        if (!save()) return;
        finishClose(route);
    });
    connect(myCloseCard, &UnsavedCloseCard::discardChosen, this, [this] {
        const CloseRoute route = myCloseRoute;
        myCloseCard->hide();
        // Nothing is written on the way out: mySavedRevision is moved to the
        // live one so no later save path can resurrect the discarded state.
        mySavedRevision = myScene.revision();
        finishClose(route);
    });
    connect(myCloseCard, &UnsavedCloseCard::keepChosen, this, [this] {
        myCloseCard->hide();
        myCloseRoute = CloseRoute::None;
        refreshSurfaces();
    });

    myNameMark = new FurnitureNameMark(statusBar());
    statusBar()->addPermanentWidget(myNameMark);

    connect(myPieces, &ScenePiecesPanel::removeRequested, this,
            [this](int pieceId) { removePiece(pieceId); });
    // The eye was inert too: every row had one, and toggling it changed
    // nothing on screen.
    connect(myPieces, &ScenePiecesPanel::visibilityToggled, this,
            [this](int pieceId, bool shown) {
                for (int bodyId : bodyIdsForPiece(pieceId))
                    myView->setSolidVisible(bodyId, shown);
                refreshSurfaces();
            });
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
        // A FAILURE TOAST, not a status line. A refusal reports loudly or not
        // at all, and the status bar was the weaker surface - the editor's own
        // openFurniture() has always used a toast here.
        if (myToasts) {
            myToasts->show(tr("Couldn't open this scene — %1")
                               .arg(error.isEmpty() ? tr("it could not be read") : error),
                           Toast::Kind::Failure, false);
        }
        return false;
    }

    myScene = loaded;
    mySceneId = id;
    // Freshly loaded is freshly clean, and everything this window had shown
    // belongs to the scene it is no longer showing.
    myView->clearSolids();
    myView->clearBodyWood();
    myView->clearBodyWoodTextures();
    myPieceBodies.clear();
    myPieceShapes.clear();
    myBodyPiece.clear();
    myBrokenPieces.clear();
    myGrainAcrossBodies.clear();
    myNextBodyId = 1;
    mySelectedPiece = 0;
    mySceneName.clear();
    for (const FurnitureStore::SceneInfo& info : myStore->listScenes()) {
        if (info.id == id) mySceneName = info.name;
    }
    // The window NAMES what it is showing: a second window with no name on it
    // is one the user cannot tell from the editor at a glance.
    setWindowTitle(mySceneName.isEmpty() ? tr("FurnifyMe — Scene")
                                         : tr("FurnifyMe — %1").arg(mySceneName));
    // EVERY PIECE THE FILE NAMES, drawn where the file says - including the
    // ones whose furniture has gone, which keep their row and their reason
    // rather than being dropped. A scene that silently forgot a piece on open
    // would delete the reference on the next save.
    for (const SceneModel::Piece& piece : myScene.pieces()) {
        QString loadError;
        DocumentModel furniture;
        if (myStore->loadFurniture(QString::fromStdString(piece.furnitureId), furniture,
                                   &loadError)) {
            displayPiece(piece.id, furniture, piece.placement);
        } else {
            myPieceBodies[piece.id] = {};
            myPieceShapes[piece.id] = {};
            myBrokenPieces[piece.id] = loadError.isEmpty()
                                           ? tr("This furniture could not be read")
                                           : loadError;
        }
    }
    mySavedRevision = myScene.revision();

    // THE SCENE'S OWN SETTINGS AND CAMERA. They round-tripped to disk from the
    // first commit and were then thrown away on every reload: frame a picture,
    // set 16:9, a light angle and a quality, save, reopen - and every one of
    // them was back at its default with the camera at the origin. The file
    // carried them the whole time; nothing read them.
    applySceneSettings();

    // AND IT IS FRAMED. A scene whose pieces stand metres from the origin - a
    // table at x = 3430 in the user's own scene - opened on a camera looking at
    // the origin from 700 mm, so render mode produced an empty backdrop and
    // this window has no Fit All to recover with. Framed only when the saved
    // camera is the untouched default, so a framing the user DID set is never
    // overridden by one this window invented.
    const CameraState fresh;
    const bool cameraUntouched =
        myScene.camera.target.Distance(fresh.target) < 1.0e-6 &&
        std::fabs(myScene.camera.distance - fresh.distance) < 1.0e-6 &&
        std::fabs(myScene.camera.azimuthDeg - fresh.azimuthDeg) < 1.0e-6 &&
        std::fabs(myScene.camera.elevationDeg - fresh.elevationDeg) < 1.0e-6;
    if (cameraUntouched && !myScene.pieces().empty()) myView->fitAll();

    refreshSurfaces();
    return true;
}

void SceneWindow::applySceneSettings()
{
    if (!myView) return;
    myView->setRenderAspect(static_cast<OcctViewWidget::RenderAspect>(myScene.aspect));
    myView->setRenderGuides(static_cast<OcctViewWidget::RenderGuides>(myScene.guides));
    myView->setRenderLightAngleDeg(myScene.lightAngleDeg);
    myView->setRenderLightStrength(myScene.lightStrength);
    myView->setRenderFov(myScene.fovDeg);
    myView->setRenderExportSize(static_cast<OcctViewWidget::ExportSize>(myScene.exportSize));
    myView->setRenderQuality(myScene.quality == 0 ? OcctViewWidget::RenderQuality::Simple
                                                  : (myScene.quality == 1
                                                         ? OcctViewWidget::RenderQuality::Balanced
                                                         : OcctViewWidget::RenderQuality::Deep));
    myView->setRenderCutout(myScene.cutout);
    myView->setBaseProjection(myScene.orthographic ? CameraController::Projection::Orthographic
                                                   : CameraController::Projection::Perspective);
    myView->setCameraStateNow(myScene.camera);
    // NOTE: the card's own controls are not pushed back to these values yet -
    // the panel has no single sync entry point, and MainWindow sets its rows
    // one setter at a time. So a reopened scene RENDERS with its saved
    // settings while the card's sliders read their defaults until touched.
    // Recorded rather than papered over.
}

void SceneWindow::captureSceneSettings()
{
    if (!myView) return;
    myScene.aspect = static_cast<int>(myView->renderAspect());
    myScene.guides = static_cast<int>(myView->renderGuides());
    myScene.lightAngleDeg = myView->renderLightAngleDeg();
    myScene.lightStrength = myView->renderLightStrength();
    myScene.fovDeg = myView->renderFov();
    myScene.exportSize = static_cast<int>(myView->renderExportSize());
    myScene.quality = static_cast<int>(myView->renderQuality());
    myScene.cutout = myView->renderCutout();
    myScene.orthographic =
        myView->camera().baseProjection() == CameraController::Projection::Orthographic;
    myScene.camera = myView->camera().state();
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
    // WHICH WOOD: the one this furniture RECORDED, now that a furniture
    // carries one. Before that existed the only answer was "the first look it
    // happened to save", which for real furniture was none at all - every
    // piece fell back to one default and the scene showed a single material
    // whatever its furniture were made of.
    DocumentModel::MaterialLook look;
    const std::string& named = furniture.activeMaterial();
    if (!named.empty() && furniture.materialLook(named, look)) {
        // its own, by name
    } else if (!furniture.materialLooks().empty()) {
        // A furniture that edited a look but never recorded which wood it
        // wears - the first it saved is the only honest guess.
        look = furniture.materialLooks().front();
    }
    const QString texture = QString::fromStdString(furniture.woodTextureFile());

    for (const DocumentModel::Solid& solid : furniture.solids()) {
        if (solid.shape.IsNull()) continue;
        const int bodyId = myNextBodyId++;
        // Displayed AS SAVED and then PLACED, never rebuilt somewhere else:
        // the placement is a presentation transform here (see
        // OcctViewWidget::setSolidPlacement), so the shape this window holds
        // stays the furniture's own and a drag costs no tessellation.
        myView->displaySolid(bodyId, solid.shape);
        myView->setSolidPlacement(bodyId, placement);

        OcctViewWidget::BodyWood wood;
        wood.red = look.red;
        wood.green = look.green;
        wood.blue = look.blue;
        wood.brightness = look.brightness;
        wood.surface = look.surface;
        wood.metal = look.metal;
        wood.grainSize = look.grainSize;
        wood.grainAngle = look.grainAngle;
        wood.texturePath = texture;
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

    const bool wasEmpty = myScene.pieces().empty();
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

    gp_Trsf placement = placementForNewPiece(furniture);
    myScene.setPlacement(pieceId, placement);
    displayPiece(pieceId, furniture, placement);
    // ON THE FLOOR from the moment it arrives, not only after it is dragged. A
    // furniture modelled 500 mm above its own origin would otherwise come into
    // the scene hovering, and the user would have to discover that the fix is
    // to drag it and let go.
    placement = settledOnFloor(pieceId, placement);
    applyPlacement(pieceId, placement);
    // The FIRST piece frames itself - MainWindow's own "if (wasEmpty) fitAll()"
    // rule. Later ones do not, because by then the user has framed a shot and
    // re-framing under them would be the window overriding a choice.
    if (wasEmpty && myView) myView->fitAll();
    refreshSurfaces();
    return true;
}

bool SceneWindow::removePiece(int pieceId)
{
    // The viewport first, the model second: the bodies are this window's own
    // ids and nothing else knows them, so they have to go here or they stay on
    // screen belonging to a piece that no longer exists.
    for (int bodyId : bodyIdsForPiece(pieceId)) myView->removeSolid(bodyId);
    myPieceBodies.erase(pieceId);
    myPieceShapes.erase(pieceId);
    myBrokenPieces.erase(pieceId);
    for (auto it = myBodyPiece.begin(); it != myBodyPiece.end();)
        it = (it->second == pieceId) ? myBodyPiece.erase(it) : std::next(it);
    if (mySelectedPiece == pieceId) mySelectedPiece = 0;
    if (!myScene.removePiece(pieceId)) return false;
    showToolGizmo();
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

void SceneWindow::applyPlacement(int pieceId, const gp_Trsf& placement)
{
    myScene.setPlacement(pieceId, placement);
    if (!myView) return;
    for (int bodyId : bodyIdsForPiece(pieceId)) myView->setSolidPlacement(bodyId, placement);
}

std::vector<TopoDS_Shape> SceneWindow::placedShapesForPiece(int pieceId) const
{
    std::vector<TopoDS_Shape> out;
    const auto at = myPieceShapes.find(pieceId);
    if (at == myPieceShapes.end()) return out;
    const SceneModel::Piece* piece = myScene.piece(pieceId);
    const gp_Trsf placement = piece ? piece->placement : gp_Trsf();
    for (const TopoDS_Shape& shape : at->second) {
        if (shape.IsNull()) continue;
        if (placement.Form() == gp_Identity) {
            out.push_back(shape);
            continue;
        }
        BRepBuilderAPI_Transform move(shape, placement, true);
        out.push_back(move.IsDone() ? move.Shape() : shape);
    }
    return out;
}

gp_Trsf SceneWindow::settledOnFloor(int pieceId, const gp_Trsf& placement) const
{
    const auto at = myPieceShapes.find(pieceId);
    if (at == myPieceShapes.end() || at->second.empty()) return placement;

    std::vector<TopoDS_Shape> placed;
    for (const TopoDS_Shape& shape : at->second) {
        if (shape.IsNull()) continue;
        if (placement.Form() == gp_Identity) {
            placed.push_back(shape);
            continue;
        }
        BRepBuilderAPI_Transform move(shape, placement, true);
        placed.push_back(move.IsDone() ? move.Shape() : shape);
    }
    if (placed.empty()) return placement;

    // ITS OWN MEASURED BOX, not a world bounding box - the oriented box is
    // what "along its own sides" means everywhere else in this app, and a
    // piece turned on the floor has a world box taller than it is.
    const ModelingOps::MeasuredBox box = ModelingOps::measuredBox(placed);
    if (!box.ok) return placement;
    double lowest = 0.0;
    bool first = true;
    for (int w = -1; w <= 1; w += 2) {
        for (int d = -1; d <= 1; d += 2) {
            for (int h = -1; h <= 1; h += 2) {
                const double z = box.corner(w, d, h).Z();
                lowest = first ? z : std::min(lowest, z);
                first = false;
            }
        }
    }
    if (std::fabs(lowest) < 1.0e-9) return placement;

    gp_Trsf drop;
    drop.SetTranslation(gp_Vec(0.0, 0.0, -lowest));
    return drop.Multiplied(placement);
}

void SceneWindow::commitDrag(bool dragged)
{
    myDragging = false;
    if (!dragged || mySelectedPiece == 0) {
        refreshSurfaces();
        return;
    }
    const SceneModel::Piece* piece = myScene.piece(mySelectedPiece);
    if (!piece) return;
    // Settled on the way OUT of the gesture, not on every step: a piece that
    // climbed back to the floor mid-drag would fight the hand moving it.
    applyPlacement(mySelectedPiece, settledOnFloor(mySelectedPiece, piece->placement));
    showToolGizmo();
    refreshSurfaces();
}

void SceneWindow::refreshSelection()
{
    if (myApplyingSelection || !myView) return;

    int piece = 0;
    for (int bodyId : myView->selectedSolidIds()) {
        const auto at = myBodyPiece.find(bodyId);
        if (at == myBodyPiece.end()) continue;
        // A selection spanning two pieces is not one piece, and there is no
        // gesture here that acts on two.
        if (piece != 0 && piece != at->second) { piece = 0; break; }
        piece = at->second;
    }

    mySelectedPiece = piece;
    if (piece != 0) {
        // CLICKING ANY BODY SELECTS THE WHOLE PIECE. Guarded, because this
        // very assignment re-emits selectionChanged().
        const std::vector<int> bodies = bodyIdsForPiece(piece);
        if (bodies != myView->selectedSolidIds()) {
            myApplyingSelection = true;
            myView->setSelectedSolids(bodies);
            myApplyingSelection = false;
        }
    }

    showToolGizmo();
    if (myPieces) myPieces->setSelected(piece);
    refreshSurfaces();
}

void SceneWindow::showToolGizmo()
{
    if (!myView) return;
    const bool renderMode = myStudio && myStudio->isEnabled();
    if (mySelectedPiece == 0 || renderMode) {
        myView->clearBodyGizmos();
        return;
    }
    const std::vector<TopoDS_Shape> placed = placedShapesForPiece(mySelectedPiece);
    if (placed.empty()) {
        myView->clearBodyGizmos();
        return;
    }
    const ModelingOps::MeasuredBox box = ModelingOps::measuredBox(placed);
    const gp_Pnt pivot = box.ok ? box.centre : gp_Pnt(0.0, 0.0, 0.0);
    // Move or Rotate, and NEVER showScaleGizmo() - there is no Tool that
    // reaches it, which is the point rather than an omission.
    if (myTool == Tool::Rotate) myView->showRotateGizmo(pivot);
    else myView->showMoveGizmo(pivot);
}

void SceneWindow::setTool(Tool tool)
{
    if (myTool == tool) return;
    myTool = tool;
    showToolGizmo();
    refreshSurfaces();
}

bool SceneWindow::save()
{
    if (!myStore || mySceneId.isEmpty()) return false;
    // Read out of the viewport at the moment of writing, so what comes back on
    // the next open is the picture that was actually framed.
    captureSceneSettings();
    // NOT while render mode is on, the editor's own rule: a render-mode
    // viewport is a studio shot, not a picture of the scene's arrangement, and
    // a save must not silently replace the library card with it. saveScene()
    // treats a null image as "leave the old one" rather than as a failure.
    QImage thumb;
    if (myView && !(myStudio && myStudio->isEnabled())) thumb = myView->captureThumbnail();
    if (!myStore->saveScene(mySceneId, myScene, thumb)) {
        if (myToasts) {
            myToasts->show(tr("Couldn't save %1 — Check that its folder still exists and "
                              "isn't read-only")
                               .arg(mySceneName.isEmpty() ? tr("this scene") : mySceneName),
                           Toast::Kind::Failure, false);
        }
        return false;
    }
    mySavedRevision = myScene.revision();
    refreshSurfaces();
    return true;
}

DocumentModel::Shot SceneWindow::currentShot(const QString& name) const
{
    return RenderStudio::shotFrom(myView, name);
}

void SceneWindow::applyShot(const DocumentModel::Shot& shot)
{
    RenderStudio::applyShotTo(myView, shot);
    refreshSurfaces();
}

QString SceneWindow::nextShotName() const
{
    // The first free number rather than count + 1, MainWindow's own rule:
    // deleting the middle shot and saving again would otherwise make a second
    // row with the same name, and a name is how a shot is picked.
    for (int n = 1; n < 1000; ++n) {
        const QString candidate = tr("Shot %1").arg(n);
        bool taken = false;
        for (const DocumentModel::Shot& shot : myScene.shots()) {
            if (QString::fromStdString(shot.name) == candidate) taken = true;
        }
        if (!taken) return candidate;
    }
    return tr("Shot");
}

void SceneWindow::addShotToScene(const DocumentModel::Shot& shot)
{
    myScene.addShot(shot);
    refreshSurfaces();
}

bool SceneWindow::removeShotFromScene(std::size_t index)
{
    if (!myScene.removeShot(index)) return false;
    refreshSurfaces();
    return true;
}

bool SceneWindow::isAskingBeforeClose() const
{
    return myCloseCard && myCloseCard->isAsking();
}

bool SceneWindow::askBeforeClosing(CloseRoute route)
{
    if (!isSceneDirty() || !myCloseCard) return false;
    // Render mode first: asking is leaving, and the question belongs over the
    // viewport the user was arranging in.
    if (myStudio && myStudio->isEnabled()) myStudio->setEnabled(false);
    myCloseRoute = route;
    myCloseCard->ask(mySceneName.isEmpty() ? tr("this scene") : mySceneName);
    refreshSurfaces();
    return true;
}

void SceneWindow::finishClose(CloseRoute route)
{
    myCloseRoute = CloseRoute::None;
    if (route == CloseRoute::Library) emit closeRequested();
    else if (route == CloseRoute::Quit) emit quitRequested();
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
    }
    if (myMoveAction) myMoveAction->setChecked(myTool == Tool::Move);
    if (myRotateAction) myRotateAction->setChecked(myTool == Tool::Rotate);
    if (myNameMark) {
        myNameMark->setState(mySceneName, isSceneDirty());
    }
    if (myStudio && myStudio->panel()) {
        // Rebuilt from the scene on every state change, like every other
        // surface here - setShots() has its own equal-guard.
        QStringList shotNames;
        for (const DocumentModel::Shot& shot : myScene.shots())
            shotNames << QString::fromStdString(shot.name);
        myStudio->panel()->setShots(shotNames);
    }
    if (myOverlay) myOverlay->relayout();
    // RAISED AFTER relayout, never before. relayout() raises every anchored
    // card as it places it, so a raise beforehand was undone a line later and
    // the question card came up UNDERNEATH the pieces list and the pill. The
    // same ordering law CLAUDE.md records for the toast and the balloon:
    // anything that must sit on top goes after the layout pass, not before.
    if (myAddPiece && myAddPiece->isVisible()) myAddPiece->raise();
}

void SceneWindow::closeEvent(QCloseEvent* event)
{
    // The X QUITS, the way closing the editor does since Milestone 5 - it is
    // not a second route back to the hub. File -> Close scene is that route,
    // and it says so in its own tooltip.
    if (askBeforeClosing(CloseRoute::Quit)) {
        // Asking is not leaving. The window stays put until the question is
        // answered, exactly as the editor's X does.
        event->ignore();
        return;
    }
    emit quitRequested();
    QMainWindow::closeEvent(event);
}
