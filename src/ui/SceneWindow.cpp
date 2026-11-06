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
#include "ToolChip.h"
#include "ToolCluster.h"
#include "WindowChrome.h"
#include "Toast.h"
#include "UnsavedCloseCard.h"
#include "ViewportOverlay.h"
#include "CardSlide.h"
#include "HtmlCardSlot.h"
#include "web/SceneCardsBridge.h"
#include "web/HubWindow.h"
#include "session/StatusLine.h"
#ifdef FURNIFYME_WEB_UI
#include "session/ActionRegistry.h"
#include "session/MenuModel.h"
#include "web/EditorChrome.h"
#include "web/ShellBridge.h"
#include "web/WebHost.h"
#include <QShortcut>
#endif

#include <algorithm>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
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
// How much footprint two pieces must share before one counts as standing
// ON the other. A hair, so pieces that merely touch along an edge are not
// treated as supports.
constexpr double kSupportTouchMm = 0.5;

// What a broken piece's row prints where its name goes. FurnitureStore's
// reason is a clause written to follow "Couldn't open this furniture — ", so
// standing alone in a row it needs a subject of its own.
QString brokenPieceReason(const QString& loadError)
{
    if (loadError.isEmpty()) return SceneWindow::tr("This furniture could not be read");
    return SceneWindow::tr("This furniture can't be opened — %1").arg(loadError);
}
}  // namespace

SceneWindow::SceneWindow(FurnitureStore* store, QWidget* parent)
    : QMainWindow(parent), myStore(store)
{
    setWindowTitle(tr("FurnifyMe — Scene"));
    // The bottom strip's state (Phase 7b), before anything can write to it.
    myStatusLine = new StatusLine(this);

    myView = new OcctViewWidget(this);
    setCentralWidget(myView);

    buildMenus();
    buildOverlay();

    // The custom title bar, attached AFTER the overlay so the pill and the
    // buttons it hit-tests against both exist. MainWindow's variant 2: the
    // native caption is gone and we add no strip of our own either.
    // In HTML-chrome mode the pill is the page's (Phase 5a, the editor's
    // ruling R10): its ground drags the window, its titles do not, and
    // nothing does while a menu is open.
    {
        auto qtHit = WindowChrome::captionHitTest(this, myAppBar, myWindowButtons);
        WindowChrome::attach(
            this,
            [this, qtHit](const QPoint& p) {
#ifdef FURNIFYME_WEB_UI
                // The page's window buttons (Phase 7a): the editor's rule.
                if (myButtonsSlot && myWindowControls && htmlChromeLive() && myButtonsSlot->isVisible() &&
                    !myWindowControls->maxButton().isEmpty()) {
                    const QPoint at = myView->mapFrom(this, p);
                    if (myWindowControls->maxButton().contains(at)) return WindowChrome::Hit::MaxButton;
                    if (myWindowControls->buttons().contains(at)) return WindowChrome::Hit::Client;
                }
#endif
                const WindowChrome::Hit hit = qtHit(p);
#ifdef FURNIFYME_WEB_UI
                if (hit != WindowChrome::Hit::MaxButton && myPillSlot && myShellBridge && htmlChromeLive() &&
                    myPillSlot->isVisible()) {
                    const QPoint at = myView->mapFrom(this, p);
                    if (!myPillSlot->geometry().contains(at)) return hit;
                    if (myShellBridge->menu()->isOpen()) return WindowChrome::Hit::Client;
                    for (const QRect& title : myShellBridge->titleRects())
                        if (title.contains(at)) return WindowChrome::Hit::Client;
                    return WindowChrome::Hit::Caption;
                }
#endif
                return hit;
            },
            [this](bool hovered) {
                if (myWindowButtons) myWindowButtons->setMaxHovered(hovered);
#ifdef FURNIFYME_WEB_UI
                if (myWindowControls) emit myWindowControls->maxHovered(hovered);
#endif
            },
            [this](bool pressed) {
                if (myWindowButtons) myWindowButtons->setMaxPressed(pressed);
#ifdef FURNIFYME_WEB_UI
                if (myWindowControls) emit myWindowControls->maxPressed(pressed);
#endif
            });
    }

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

    // A LEFT CLICK IN THE RENDER VIEWPORT LEAVES RENDER MODE, exactly as it
    // does in the editor. The viewport emits this for both windows and only
    // MainWindow answered it, so the identical gesture exited there and did
    // nothing here - the scene's own back-arrow chip was the only way out.
    // Orbit and pan are not gated: framing a shot does not exit.
    connect(myView, &OcctViewWidget::renderModeExitRequested, this, [this] {
        if (myStudio && myStudio->isEnabled()) myStudio->setEnabled(false);
    });

    // THE CONTEXT CAN DIE, and when it does every presentation goes with it.
    // Without this the scene window came back blank with no recovery but
    // closing and reopening the scene - the editor has answered this signal
    // since the QOpenGLWidget migration and this window never did.
    connect(myView, &OcctViewWidget::glResourcesReleased, this, [this] {
        resyncPieces();
        // Render mode is cleared inside the viewport by the release, so a
        // menu entry left checked would be the single-source-of-truth rule
        // broken across the one event nobody drives. The editor does exactly
        // this beside its own resync.
        if (myStudio && myStudio->isEnabled()) myStudio->setEnabled(false);
    });

    // The gizmos are the viewport's own, and so is their snapping - a distance
    // arrives already rounded when Snap to Grid is on. What this window adds
    // is which piece the delta belongs to.
    connect(myView, &OcctViewWidget::moveDragged, this, [this](int axis, double distance) {
        if (mySelectedPiece == 0 || axis < 0 || axis > 2) return;
        if (!myDragging) {
            const SceneModel::Piece* piece = myScene.piece(mySelectedPiece);
            if (!piece) return;
            myDragBase = piece->placement;
            // ONE CHECKPOINT PER GESTURE, taken as the drag STARTS. Every step
            // applies a placement, so a checkpoint at commit time would
            // capture the state after the move and undo nothing - the editor
            // takes its checkpoint before the change for the same reason.
            myScene.checkpoint();
            myDragging = true;
        }
        myDragAxis = axis;
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
            // ONE CHECKPOINT PER GESTURE, taken as the drag STARTS. Every step
            // applies a placement, so a checkpoint at commit time would
            // capture the state after the move and undo nothing - the editor
            // takes its checkpoint before the change for the same reason.
            myScene.checkpoint();
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
    // ONE VERB (the 2026-10 review's F2). This was "New piece" under File
    // while the empty card said "Add a furniture" - two verbs for one gesture,
    // and the entry filed with saving and closing rather than with arranging.
    // It is "Add piece" in the Arrange menu, on the Pieces card's own row and
    // in the card's empty line.
    myAddPieceAction = new QAction(tr("Add piece"), this);
    myAddPieceAction->setToolTip(tr("Put a furniture from the library into this scene"));
    connect(myAddPieceAction, &QAction::triggered, this, [this] {
        if (!myAddPiece || !myStore) return;
        // NOT WHILE THE STUDIO IS LIVE. Adding a piece displays bodies into a
        // viewport whose floor was built under the old contents and whose
        // picking is set up for a shot, so the new piece arrives into a scene
        // that is not ready for it. Leaving render mode first is what every
        // document-changing route in the editor already does, and it is also
        // the honest reading of the gesture: putting furniture in is
        // arranging, not shooting. QAction::trigger() does not consult
        // isEnabled(), so the gate lives HERE rather than only on the action.
        if (myStudio && myStudio->isEnabled()) myStudio->setEnabled(false);
        // Re-read on every open: a furniture made since this window opened is
        // one the user expects to find in the list.
        // The page's card while the page is live (Phase 5b), the Qt one otherwise.
        if (htmlChromeLive() && myAddPieceModel) myAddPieceModel->showFor(myStore->listFurniture());
        else myAddPiece->showFor(myStore->listFurniture());
        // refreshSurfaces(), NOT relayout(). The centring and the
        // after-the-layout raise both live in refreshSurfaces; calling the
        // overlay directly skipped them, so the card came up at its default
        // 0,0 - behind the pill and the pieces list, with its own title
        // clipped off the top-left corner.
        refreshSurfaces();
    });

    // AN EXPORT ROUTE. A scene could be rendered and not saved as a picture by
    // any gesture at all - the suite reached saveSnapshot() directly, which is
    // a viewport method and not a door the user has. Taking the picture is the
    // whole point of the window.
    myScreenshotAction = new QAction(tr("Save Screenshot"), this);
    myScreenshotAction->setToolTip(tr("Write the picture to a file"));
    connect(myScreenshotAction, &QAction::triggered, this, [this] {
        // REFUSED BEFORE THE FILE IS ASKED FOR. Asking where to put a picture
        // and then declining to take it wastes the one question this gesture
        // asks - see screenshotRefusal().
        const QString refusal = screenshotRefusal();
        if (!refusal.isEmpty()) {
            showToast(refusal, Toast::Kind::Failure, false);
            return;
        }
        const QString suggested =
            (mySceneName.isEmpty() ? tr("Screenshot") : mySceneName) + QStringLiteral(".png");
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save Screenshot"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                .filePath(suggested),
            tr("PNG image (*.png)"));
        if (path.isEmpty()) return;
        saveScreenshotTo(path);
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

    // EDIT, before Arrange - a scene has an undo stack now (SceneModel's own,
    // the editor's shape). Before this, taking a piece out was final, which is
    // why removal needed a two-click confirm to be safe at all.
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    myUndoAction = new QAction(tr("Undo"), this);
    myUndoAction->setShortcut(QKeySequence::Undo);
    myUndoAction->setToolTip(tr("Take back the last change to this scene"));
    connect(myUndoAction, &QAction::triggered, this, [this] {
        if (!myScene.undo()) return;
        // The pieces and their placements have changed under the viewport, so
        // it is re-displayed from the live scene - the same call a lost
        // graphics context uses, for the same reason.
        resyncPieces();
        refreshSurfaces();
    });
    edit->addAction(myUndoAction);

    myRedoAction = new QAction(tr("Redo"), this);
    myRedoAction->setShortcut(QKeySequence::Redo);
    myRedoAction->setToolTip(tr("Put back the change you just took away"));
    connect(myRedoAction, &QAction::triggered, this, [this] {
        if (!myScene.redo()) return;
        resyncPieces();
        refreshSurfaces();
    });
    edit->addAction(myRedoAction);

    auto* arrange = menuBar()->addMenu(tr("&Arrange"));
    // Putting furniture in is arranging, so it leads the menu that arranges.
    arrange->addAction(myAddPieceAction);
    arrange->addSeparator();
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

void SceneWindow::syncViewportMinimumWidth()
{
    if (!myView || !myAppBar || !myWindowButtons) return;
    // MainWindow::syncViewportMinimumWidth()'s rule, for this window's two
    // rows: the pill beside the window buttons, and under them the Pieces
    // card beside the Move/Rotate/Render cluster - both pairs stand in the
    // same TopLeft/TopRight columns, so either could be the one that meets.
    int wanted = ViewportOverlay::minimumWidthBeside(
        myAppBar, ViewportOverlay::Anchor::TopLeft, myWindowButtons);
    if (myPieces && myArrangeTools) {
        wanted = std::max(wanted, ViewportOverlay::minimumWidthBeside(
                                      myPieces, ViewportOverlay::Anchor::TopLeft,
                                      myArrangeTools));
    }
    myView->setMinimumWidth(wanted);
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

    // FRAMELESS, LIKE THE EDITOR. This window wore the native Windows caption
    // while the editor hid its own, so the same app had two window treatments
    // side by side - and the scene's name was printed twice, once by the OS
    // bar and once by the pill. The machinery is WindowChrome's and is used
    // exactly as MainWindow uses it: the buttons float over the viewport, and
    // dragging lives on the pill (any point inside it whose deepest child is
    // nothing answers Caption, which buys native move, double-click-to-
    // maximize and the system menu in one word).
    myWindowButtons = new WindowButtons(WindowButtons::Look::Card, myView);
    myOverlay->addWidget(myWindowButtons, ViewportOverlay::Anchor::TopRight);

    // THE WINDOW'S TWO OTHER JOBS, ON SCREEN (the 2026-10 review's F16).
    // Arranging and rendering were menu-only. Three icon-only chips mirroring
    // the actions the Arrange and View menus already own - built exactly as
    // the editor builds its view controls, and anchored where that cluster is:
    // TopRight, so relayout() stacks it one gap under the window buttons. Not
    // in the left column: that is the pill and the Pieces card, and a cluster
    // stacked under a list that grows would move every time a piece arrived.
    // TopRight also keeps the "a header is only a header when something
    // follows it" rule out of play - nothing here is a LeftEdge entry.
    // HIDDEN IN RENDER MODE (refreshSurfaces()), unlike the editor's view
    // controls: those three still mean something in a shot - projection,
    // wireframe, framing - while Move and Rotate do nothing with the gizmos
    // gone, and the studio's own Back chip is already the way out. A shot is
    // the furniture alone.
    myArrangeTools = new ToolCluster(myView);
    myArrangeTools->addChip(
        new ToolChip(myMoveAction, IconSet::Glyph::Move, ToolChip::ChipMode::IconOnly));
    myArrangeTools->addChip(
        new ToolChip(myRotateAction, IconSet::Glyph::Rotate, ToolChip::ChipMode::IconOnly));
    myArrangeTools->addChip(
        new ToolChip(myRenderModeAction, IconSet::Glyph::Render, ToolChip::ChipMode::IconOnly));
    myOverlay->addWidget(myArrangeTools, ViewportOverlay::Anchor::TopRight);

    // Added after the pill, so it stacks below it in the same TopLeft column.
    myPieces = new ScenePiecesPanel(myView);
    // The card's own Add piece row - the action's mirror, see setAddAction().
    myPieces->setAddAction(myAddPieceAction);
    myOverlay->addWidget(myPieces, ViewportOverlay::Anchor::TopLeft);
    syncViewportMinimumWidth();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &SceneWindow::syncViewportMinimumWidth);

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
        reportNote(tr("%1 saved").arg(name), false);
    });
    connect(myStudio, &RenderStudio::shotApplied, this, [this](int index) {
        const std::vector<DocumentModel::Shot>& shots = myScene.shots();
        if (index < 0 || index >= static_cast<int>(shots.size())) return;
        const DocumentModel::Shot shot = shots[static_cast<std::size_t>(index)];
        applyShot(shot);
        myStatusLine->setMessage(
            tr("%1 — camera, frame, lens and light").arg(QString::fromStdString(shot.name)));
    });
    connect(myStudio, &RenderStudio::shotRemoved, this, [this](int index) {
        const std::vector<DocumentModel::Shot>& shots = myScene.shots();
        if (index < 0 || index >= static_cast<int>(shots.size())) return;
        const QString name = QString::fromStdString(shots[static_cast<std::size_t>(index)].name);
        if (removeShotFromScene(static_cast<std::size_t>(index)))
            myStatusLine->setMessage(tr("%1 forgotten").arg(name));
    });

    // A REFUSAL HAS TO REPORT, and in this app a refusal reports through a
    // Failure toast - which cannot be silenced - rather than a status line
    // nobody reads. A status bar carried the first version of openScene()'s
    // refusal and that was the weaker surface.
    myToasts = new ToastHost(myView, this);
    // THE EDITOR'S UNDO PILL, on the scene's own stack (the 2026-10 review's
    // F10). A piece added, taken out, moved or renamed reports a Note with
    // Undo now, and the pill triggers the Edit menu's own Undo - gated on its
    // enabled state here as well as dimmed by refreshSurfaces(), because an
    // event delivered straight at the pill does not consult either.
    connect(myToasts, &ToastHost::undoRequested, this, [this] {
        if (myUndoAction && myUndoAction->isEnabled()) myUndoAction->trigger();
    });
    // A refusal's reason stays on the left of the status bar after its toast
    // goes, the editor's own rule from the same review.
    connect(myToasts, &ToastHost::failureShown, this,
            [this](const QString& reason) { myStatusLine->setMessage(reason); });

    // Not anchored by the overlay: the card is one question in the middle of
    // the viewport, not a rail or a drawer, so it is placed by the window.
    myAddPiece = new AddPieceCard(myView);
    connect(myAddPiece, &AddPieceCard::chosen, this,
            [this](const QString& id) { addPiece(id); });
    // An empty library's way back to the hub is THE way back to the hub -
    // File -> Close scene, with its close question - not a second route.
    myAddPiece->setLibraryAction(myCloseSceneAction);

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

    // THE EDITOR'S TWO CHANNELS. The left - elided, so a long piece name never
    // runs into the scene's name - says what is selected and what just
    // happened; the right says what to do next (the review's F16: the window
    // had no hint at all). Same widgets, same order as MainWindow's bar.
    // Held as state since Phase 7b (session/StatusLine); the page's strip
    // draws it.

    // Clicking a row selects that piece. The selection itself still lives in
    // the viewport and mySelectedPiece is still DERIVED from it through
    // refreshSelection() - this sets the one source of truth rather than
    // keeping a second copy here, so a row click and a viewport click land in
    // exactly the same state.
    connect(myPieces, &ScenePiecesPanel::selectionRequested, this, &SceneWindow::selectPieceFromList);
    connect(myPieces, &ScenePiecesPanel::removeRequested, this,
            [this](int pieceId) { removePiece(pieceId); });
    // The eye was inert too: every row had one, and toggling it changed
    // nothing on screen.
    connect(myPieces, &ScenePiecesPanel::visibilityToggled, this, &SceneWindow::setPieceShown);
    connect(myPieces, &ScenePiecesPanel::renameCommitted, this,
            [this](int pieceId, const QString& name) { renamePiece(pieceId, name); });
}

void SceneWindow::resyncPieces()
{
    if (!myView || !myStore) return;

    // Everything the viewport held is already gone; these clears are about
    // THIS window's own bookkeeping, which still names bodies that no longer
    // exist. The ids are scene-local and handed out here, so they can be
    // handed out again from 1.
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

    // PLACEMENTS COME FROM THE LIVE SCENE, shapes from the store. That split
    // is the whole point: re-opening the scene from disk would be simpler and
    // would throw away every unsaved move the user had made, which is a
    // strange thing to do to somebody whose driver just reset.
    for (const SceneModel::Piece& piece : myScene.pieces()) {
        QString loadError;
        DocumentModel furniture;
        if (myStore->loadFurniture(QString::fromStdString(piece.furnitureId), furniture,
                                   &loadError)) {
            displayPiece(piece.id, furniture, piece.placement);
        } else {
            myPieceBodies[piece.id] = {};
            myPieceShapes[piece.id] = {};
            myBrokenPieces[piece.id] = brokenPieceReason(loadError);
        }
    }

    // mySavedRevision and mySceneId are deliberately untouched: a rebuild is
    // not a load, and a scene with unsaved changes still has them afterwards.
    showToolGizmo();
    refreshSurfaces();
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
            showToast(tr("Couldn't open this scene — %1")
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
            myBrokenPieces[piece.id] = brokenPieceReason(loadError);
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
    // CLAMPED, because these arrive from a FILE. They were cast straight from
    // the stored int, so a corrupt scene - or one written by a later build
    // that added a ratio - produced an enum with no enumerator and a viewport
    // asked to frame a shape that does not exist. The manifest reader's own
    // law (validate before you mutate) applied to the three ordinals that
    // were skipping it; quality already had explicit bounds below.
    auto clampOrdinal = [](int value, int count) {
        return value >= 0 && value < count ? value : 0;
    };
    myView->setRenderAspect(
        static_cast<OcctViewWidget::RenderAspect>(clampOrdinal(myScene.aspect, 5)));
    myView->setRenderGuides(
        static_cast<OcctViewWidget::RenderGuides>(clampOrdinal(myScene.guides, 3)));
    myView->setRenderLightAngleDeg(myScene.lightAngleDeg);
    myView->setRenderLightStrength(myScene.lightStrength);
    myView->setRenderFov(myScene.fovDeg);
    myView->setRenderExportSize(
        static_cast<OcctViewWidget::ExportSize>(clampOrdinal(myScene.exportSize, 5)));
    myView->setRenderQuality(myScene.quality == 0 ? OcctViewWidget::RenderQuality::Simple
                                                  : (myScene.quality == 1
                                                         ? OcctViewWidget::RenderQuality::Balanced
                                                         : OcctViewWidget::RenderQuality::Deep));
    myView->setRenderCutout(myScene.cutout);
    myView->setBaseProjection(myScene.orthographic ? CameraController::Projection::Orthographic
                                                   : CameraController::Projection::Perspective);
    myView->setCameraStateNow(myScene.camera);
    // AND THE CARD IS TOLD. Until this existed a reopened scene RENDERED with
    // its saved aspect, guides, light, FOV, export size, quality and cutout
    // while every one of the card's own rows showed its default until
    // touched - the settings reached the view and nothing reached the
    // controls. RenderStudio owns the one entry point, so this window and the
    // editor cannot sync different subsets of the same card.
    if (myStudio) myStudio->syncPanelFromView();
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
    // THROUGH placedShapesForPiece(), which applies the placement properly.
    // Adding the placement's X TRANSLATION to an untransformed box is only the
    // right answer while nothing has been turned: a rotation about a pivot
    // rewrites that translation into a number that no longer means "how far
    // right it stands", so a new piece landed inside a turned neighbour or far
    // out in empty space.
    for (const auto& entry : myPieceShapes) {
        for (const TopoDS_Shape& shape : placedShapesForPiece(entry.first)) {
            if (shape.IsNull()) continue;
            Bnd_Box box;
            BRepBndLib::Add(shape, box);
            if (box.IsVoid()) continue;
            double xMin, yMin, zMin, xMax, yMax, zMax;
            box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
            occupiedRight = anything ? std::max(occupiedRight, xMax) : xMax;
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

    // ONE CHECKPOINT, BEFORE the scene is touched - the editor's rule. It is
    // taken even when the furniture turns out to be unreadable, because the
    // piece is added either way (a reference the scene drops is one the user
    // never learns was broken) and that addition has to be undoable too.
    myScene.checkpoint();
    const bool wasEmpty = myScene.pieces().empty();
    QString error;
    DocumentModel furniture;
    const bool loaded = myStore->loadFurniture(furnitureId, furniture, &error);

    // THE PIECE IS ADDED EITHER WAY. A reference the scene drops on a failed
    // read is a reference the user never learns was broken, and saving the
    // scene would then quietly delete it from the file.
    // DISTINGUISHABLE ON ARRIVAL. Six chairs round a table all name one
    // furniture, so without this the list reads Chair / Chair / Chair and the
    // only way to find the one you mean is to click every row. The first of a
    // kind keeps the plain name - a scene with one table should say "Table" -
    // and each later one takes the next free number.
    // "Piece" is only ever STORED, for a furniture id the library cannot
    // name; such a piece is broken, and its row prints its reason where the
    // name goes, so this word never reaches the screen.
    const QString baseName = name.isEmpty() ? tr("Piece") : name;
    QString pieceName = baseName;
    auto nameTaken = [this](const QString& candidate) {
        const std::string want = candidate.toStdString();
        for (const SceneModel::Piece& existing : myScene.pieces()) {
            if (existing.name == want) return true;
        }
        return false;
    };
    for (int suffix = 2; nameTaken(pieceName); ++suffix)
        pieceName = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    const int pieceId = myScene.addPiece(furnitureId.toStdString(), pieceName.toStdString());
    if (pieceId == 0) return false;

    if (!loaded) {
        myPieceBodies[pieceId] = {};
        myPieceShapes[pieceId] = {};
        myBrokenPieces[pieceId] = brokenPieceReason(error);
        if (myToasts) {
            // THE FURNITURE'S OWN NAME, or none at all (the review's F25e).
            // This used to name the piece - which for a furniture the library
            // could not name was the placeholder "Piece" - so the user read
            // "Couldn't put Piece in this scene". Named where the library
            // knows the furniture; otherwise the broken row's own sentence,
            // so the toast and the row say the same thing.
            const QString reason = error.isEmpty() ? tr("it could not be read") : error;
            showToast(name.isEmpty()
                               ? brokenPieceReason(error)
                               : tr("Couldn't add %1 to this scene — %2").arg(name, reason),
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
    // REPORTED, with Undo - the editor's taxonomy: every change to the
    // document is a Note that offers to take it back.
    reportNote(tr("Added %1").arg(pieceName), true);
    return true;
}

bool SceneWindow::removePiece(int pieceId)
{
    // Taking a piece out is the change this window's undo stack was built
    // for: before it existed, removal was final and needed a two-click
    // confirm to be safe at all.
    const SceneModel::Piece* going = myScene.piece(pieceId);
    if (!going) return false;
    const QString goingName = QString::fromStdString(going->name);
    myScene.checkpoint();
    // The viewport first, the model second: the bodies are this window's own
    // ids and nothing else knows them, so they have to go here or they stay on
    // screen belonging to a piece that no longer exists.
    for (int bodyId : bodyIdsForPiece(pieceId)) myView->removeSolid(bodyId);
    // THE GRAIN LIST TOO. It is keyed by body id and was the one map
    // removePiece() did not clean, so a piece taken out left its ids behind -
    // inert, because ids are never reused within a session, but handed back
    // to setBodyGrainAcross() on every later piece as a list naming bodies
    // that no longer exist.
    {
        const std::vector<int> going = bodyIdsForPiece(pieceId);
        for (int bodyId : going) {
            myGrainAcrossBodies.erase(
                std::remove(myGrainAcrossBodies.begin(), myGrainAcrossBodies.end(), bodyId),
                myGrainAcrossBodies.end());
        }
        if (myView) myView->setBodyGrainAcross(myGrainAcrossBodies);
    }
    myPieceBodies.erase(pieceId);
    myPieceShapes.erase(pieceId);
    myBrokenPieces.erase(pieceId);
    for (auto it = myBodyPiece.begin(); it != myBodyPiece.end();)
        it = (it->second == pieceId) ? myBodyPiece.erase(it) : std::next(it);
    if (mySelectedPiece == pieceId) mySelectedPiece = 0;
    if (!myScene.removePiece(pieceId)) return false;
    showToolGizmo();
    refreshSurfaces();
    reportNote(tr("Removed %1").arg(goingName), true);
    return true;
}

bool SceneWindow::setPiecePlacement(int pieceId, const gp_Trsf& placement)
{
    if (SceneModel::checkPlacement(placement) != SceneCheck::Ok) return false;
    if (!myScene.piece(pieceId)) return false;
    applyPlacement(pieceId, placement);
    showToolGizmo();
    refreshSurfaces();
    return true;
}

bool SceneWindow::renamePiece(int pieceId, const QString& name)
{
    const SceneModel::Piece* piece = myScene.piece(pieceId);
    if (!piece) return false;
    // The same name again is not a change: no checkpoint, nothing to report.
    if (piece->name == name.toStdString()) return true;
    // A CHECKPOINT, which renaming never took - so the one change in this
    // window that could not be undone was the one most likely to be a typo.
    // Taken before the change, the editor's rule, and only once the name is
    // known to differ.
    myScene.checkpoint();
    if (!myScene.setPieceName(pieceId, name.toStdString())) return false;
    refreshSurfaces();
    // The NEW name in the Note, as the editor's own rename does; the old one
    // is the row the user just typed over.
    reportNote(tr("Renamed to \"%1\"").arg(name), true);
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
    // WHAT IS BENEATH IT, not the ground. Phase 1 settled every piece onto
    // Z = 0, which is right until a piece is put ON another one: a lamp
    // dropped on a table belongs on the table. One rule rather than two -
    // with nothing underneath, `support` stays 0 and this is exactly the
    // phase 1 behaviour.
    const double support = supportHeightUnder(pieceId, placed);
    const double drop = lowest - support;
    if (std::fabs(drop) < 1.0e-9) return placement;

    gp_Trsf down;
    down.SetTranslation(gp_Vec(0.0, 0.0, -drop));
    return down.Multiplied(placement);
}

double SceneWindow::supportHeightUnder(int pieceId,
                                       const std::vector<TopoDS_Shape>& placed) const
{
    // The piece's own footprint on the floor plane.
    Bnd_Box mine;
    for (const TopoDS_Shape& shape : placed) {
        if (!shape.IsNull()) BRepBndLib::Add(shape, mine);
    }
    if (mine.IsVoid()) return 0.0;
    double myX0, myY0, myZ0, myX1, myY1, myZ1;
    mine.Get(myX0, myY0, myZ0, myX1, myY1, myZ1);

    double support = 0.0;
    for (const SceneModel::Piece& other : myScene.pieces()) {
        if (other.id == pieceId) continue;
        // A piece the user cannot see holds nothing up.
        bool visible = false;
        for (int bodyId : bodyIdsForPiece(other.id)) {
            if (myView && myView->isSolidVisible(bodyId)) visible = true;
        }
        if (!visible) continue;

        Bnd_Box theirs;
        for (const TopoDS_Shape& shape : placedShapesForPiece(other.id)) {
            if (!shape.IsNull()) BRepBndLib::Add(shape, theirs);
        }
        if (theirs.IsVoid()) continue;
        double x0, y0, z0, x1, y1, z1;
        theirs.Get(x0, y0, z0, x1, y1, z1);

        // OVERLAPPING FOOTPRINTS ONLY. A piece standing beside this one is
        // not under it, however tall it is - which is the whole difference
        // between "it sits on the table" and "it floats at table height".
        const bool overlaps = myX0 < x1 - kSupportTouchMm && myX1 > x0 + kSupportTouchMm &&
                              myY0 < y1 - kSupportTouchMm && myY1 > y0 + kSupportTouchMm;
        if (!overlaps) continue;
        // And only what is BELOW the piece's own top, so a tall neighbour it
        // is already inside does not teleport it to the ceiling.
        if (z1 > myZ1 + kSupportTouchMm) continue;
        support = std::max(support, z1);
    }
    return support;
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
    //
    // EXCEPT WHEN THE USER DRAGGED IT UP. The settle exists so a piece that
    // ARRIVES, or is slid across the floor, lands on something solid - not to
    // forbid putting one at a height. Running it after a Z drag made vertical
    // movement impossible by construction: the piece rose with the hand and
    // was pulled straight back down on release, which is exactly what the user
    // reported ("i cannot move the furniture in the z axis").
    const bool liftedByHand = myDragAxis == 2;
    myDragAxis = -1;
    if (!liftedByHand)
        applyPlacement(mySelectedPiece, settledOnFloor(mySelectedPiece, piece->placement));
    showToolGizmo();
    refreshSurfaces();

    // REPORTED, with Undo - but only when the piece really stands somewhere
    // else. A drag that snapped back to where it began took a checkpoint and
    // changed nothing, and "Moved Table" over an unmoved table is a Note that
    // lies.
    const SceneModel::Piece* landed = myScene.piece(mySelectedPiece);
    if (!landed) return;
    double moved = 0.0;
    for (int r = 1; r <= 3; ++r) {
        for (int c = 1; c <= 4; ++c)
            moved = std::max(moved, std::fabs(landed->placement.Value(r, c) -
                                              myDragBase.Value(r, c)));
    }
    if (moved < 1.0e-9) return;
    const QString name = QString::fromStdString(landed->name);
    reportNote(myTool == Tool::Rotate ? tr("Rotated %1").arg(name) : tr("Moved %1").arg(name),
               true);
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
    if (myPiecesModel) myPiecesModel->setSelected(piece);
    // The left channel's first job: what is selected now.
    myStatusLine->setMessage(selectionStatusText());
    refreshSurfaces();
}

QString SceneWindow::selectionStatusText() const
{
    const SceneModel::Piece* piece = mySelectedPiece ? myScene.piece(mySelectedPiece) : nullptr;
    return piece ? tr("%1 selected").arg(QString::fromStdString(piece->name)) : QString();
}

QString SceneWindow::statusHintText() const
{
    // THE NEXT STEP, from live state - the right channel's one job. The keys
    // are read off the actions, so a rebound shortcut cannot leave this line
    // naming the old one.
    const auto key = [](const QAction* action) {
        return action ? action->shortcut().toString(QKeySequence::NativeText) : QString();
    };
    if (myStudio && myStudio->isEnabled())
        return tr("Frame the picture — Save Screenshot writes it to a file");
    if (myScene.pieces().empty())
        return tr("Add piece puts a furniture from the library in this scene");
    if (mySelectedPiece == 0)
        return tr("Select a piece — then Move it (%1) or Rotate it (%2), or take a picture "
                  "in Render mode")
            .arg(key(myMoveAction), key(myRotateAction));
    if (myTool == Tool::Rotate)
        return tr("Drag a ring to turn the piece — %1 moves it instead").arg(key(myMoveAction));
    return tr("Drag an arrow to move the piece — %1 rotates it instead").arg(key(myRotateAction));
}

void SceneWindow::reportNote(const QString& text, bool undo)
{
    if (!myToasts) return;
    // Stamped with the scene's revision when it offers Undo, so the toast
    // goes the moment the scene moves on and its pill can never pop a
    // different change from the one it names (ToastHost::documentMovedTo()).
    showToast(text, Toast::Kind::Note, undo, undo ? static_cast<int>(myScene.revision()) : -1);
    // NOT ALSO ON THE LEFT - the editor's rule (the review's F9). While the
    // toast carries the report the left goes back to the selection; a toast
    // that was not shown leaves the bar as the only record of it.
    if (htmlChromeLive() || (myToasts->isShowing() && myToasts->currentText() == text))
        myStatusLine->setMessage(selectionStatusText());
    else
        myStatusLine->setMessage(text);
}

QString SceneWindow::screenshotRefusal() const
{
    // AN EMPTY PICTURE IS REFUSED (the review's F25c). It used to export a
    // featureless backdrop and say nothing, which is a file that looks like a
    // failure and is reported as nothing at all. Refused with the reason and
    // the step that fixes it - and the same for a scene whose every piece is
    // hidden or unreadable, which would write exactly the same empty frame.
    if (myScene.pieces().empty()) return tr("The scene is empty — add a piece first");
    bool anyShown = false;
    for (const auto& entry : myBodyPiece) {
        if (myView && myView->isSolidVisible(entry.first)) {
            anyShown = true;
            break;
        }
    }
    if (!anyShown) return tr("Nothing in this scene is showing — show a piece or add one first");
    return QString();
}

bool SceneWindow::saveScreenshotTo(const QString& path)
{
    const QString refusal = screenshotRefusal();
    if (!refusal.isEmpty()) {
        showToast(refusal, Toast::Kind::Failure, false);
        return false;
    }
    if (!myView || !myView->saveSnapshot(path)) {
        if (myToasts) {
            showToast(tr("Screenshot failed — Couldn't save the image to %1 — "
                              "Check that the folder exists and isn't read-only")
                               .arg(path),
                           Toast::Kind::Failure, false);
        }
        return false;
    }
    // A SUCCESS SAYS WHERE IT WENT. Nothing was reported before, so the only
    // way to know the picture existed was to go and look for it. No Undo: a
    // file on disk is not a change to the scene.
    reportNote(tr("Picture saved as %1").arg(QFileInfo(path).fileName()), false);
    return true;
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
            showToast(tr("Couldn't save %1 — Check that its folder still exists and "
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
        if (QWidget* pieces = piecesSurface()) pieces->setVisible(!(myStudio && myStudio->isEnabled()));
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
        if (myPiecesModel) {
            // The page's card also says whether each piece shows - the
            // viewport's own answer, never a copy kept beside it.
            for (ScenePiecesPanel::Row& row : rows) {
                const std::vector<int> bodies = bodyIdsForPiece(row.pieceId);
                row.visible = bodies.empty() || myView->isSolidVisible(bodies.front());
            }
            myPiecesModel->setRows(rows);
        }
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
    // WHICH BODIES ARE ONE PIECE. Without this the magnet measures single
    // bodies and the selection is a whole piece, so it collected nothing and
    // snapping simply did not exist in a scene.
    if (myView) myView->setMagnetGroups(myBodyPiece);
    if (myMoveAction) myMoveAction->setChecked(myTool == Tool::Move);
    if (myRotateAction) myRotateAction->setChecked(myTool == Tool::Rotate);
    const bool renderMode = myStudio && myStudio->isEnabled();
    // The arranging chips step out of a shot - see buildOverlay(). Whichever
    // of the Qt cluster and its slot is on screen (Phase 5a).
    if (QWidget* arrange = arrangeSurface()) arrange->setVisible(!renderMode);
    // WHAT UNDO CAN DO, DECIDED HERE: the menu entries, Ctrl+Z and the toast's
    // pill all read the same answer, so the pill cannot offer a step the
    // stack does not have.
    if (myUndoAction) myUndoAction->setEnabled(myScene.canUndo());
    if (myRedoAction) myRedoAction->setEnabled(myScene.canRedo());
    if (myToasts) {
        myToasts->setUndoEnabled(myUndoAction && myUndoAction->isEnabled());
        // A toast naming one change must not outlive it - an undo, another
        // change, anything that moves the scene takes it away.
        myToasts->documentMovedTo(static_cast<int>(myScene.revision()));
    }
    if (myStatusLine) {
        myStatusLine->setState(statusHintText());
        myStatusLine->setName(mySceneName, isSceneDirty());
    }
    syncHtmlChrome();
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

// --- the chrome as a page (migration Phase 5a) ------------------------------

bool SceneWindow::htmlChromeLive() const
{
#ifdef FURNIFYME_WEB_UI
    return myHtmlChrome && myHtmlChrome->isLive();
#else
    return false;
#endif
}

QWidget* SceneWindow::pillSurface() const
{
    if (htmlChromeLive() && myPillSlot) return myPillSlot;
    return myAppBar;
}

QWidget* SceneWindow::piecesSurface() const
{
    if (htmlChromeLive() && myPiecesSlot) return myPiecesSlot;
    return myPieces;
}

void SceneWindow::selectPieceFromList(int pieceId)
{
    if (!myView) return;
    const std::vector<int> bodies = bodyIdsForPiece(pieceId);
    // A broken piece has no bodies at all. Clearing is the honest answer:
    // there is nothing on screen to select, and leaving the previous piece
    // selected would make the row look like it had done something.
    myView->setSelectedSolids(bodies);
}

void SceneWindow::setPieceShown(int pieceId, bool shown)
{
    if (!myView) return;
    for (int bodyId : bodyIdsForPiece(pieceId)) myView->setSolidVisible(bodyId, shown);
    refreshSurfaces();
}

QWidget* SceneWindow::arrangeSurface() const
{
    if (htmlChromeLive() && myViewSlot) return myViewSlot;
    return myArrangeTools;
}

void SceneWindow::showToast(const QString& text, Toast::Kind kind, bool undo, int stamp)
{
#ifdef FURNIFYME_WEB_UI
    if (htmlChromeLive()) {
        // Undo's state rides WITH the message (the editor's measured rule).
        myHtmlChrome->setUndoEnabled(myUndoAction && myUndoAction->isEnabled());
        myHtmlChrome->showToast(text, kind == Toast::Kind::Failure ? EditorChrome::Kind::Failure : EditorChrome::Kind::Note,
                                undo, stamp);
        if (kind == Toast::Kind::Failure) {
            QString line = text;
            if (line.endsWith(QLatin1Char('.'))) line.chop(1);
            myStatusLine->setMessage(line);
        }
        return;
    }
#endif
    if (myToasts) myToasts->show(text, kind, undo, stamp);
}

void SceneWindow::syncHtmlChrome()
{
#ifdef FURNIFYME_WEB_UI
    if (!myHtmlChrome) return;
    myHtmlChrome->bridge()->setAnimate(myView->animationsEnabled());
    const bool live = htmlChromeLive();
    // The page's strip stands in the Qt bar's place; the overlay's cards
    // anchored at the bottom stand above it.
    if (myOverlay) myOverlay->setBottomInset(live ? 24 : 0);
    myHtmlChrome->setStatus(myStatusLine->message(), myStatusLine->state(), myStatusLine->name(),
                            myStatusLine->unsaved(), live);
    myHtmlChrome->setObstacles(myOverlay ? myOverlay->occupiedRects() : std::vector<QRect>());
    myHtmlChrome->setUndoEnabled(myUndoAction && myUndoAction->isEnabled());
    myHtmlChrome->documentMovedTo(static_cast<int>(myScene.revision()));
    // Alt+letter answers only while the page's pill stands (the editor's R6).
    const bool pillShown = live && myPillSlot && myPillSlot->isVisible();
    for (QShortcut* shortcut : myMenuShortcuts) shortcut->setEnabled(pillShown);
    // The close question, drawn by the page while the Qt card asks it.
    QVariantMap words;
    if (myCloseCard && myCloseCard->isAsking() && myCloseCard->drawnElsewhere()) {
        words = myCloseCard->words();
        QWidget* shownButtons = myButtonsSlot && myButtonsSlot->isVisible() ? static_cast<QWidget*>(myButtonsSlot)
                                                                            : static_cast<QWidget*>(myWindowButtons);
        if (shownButtons && shownButtons->isVisible()) {
            const QRect r(shownButtons->mapTo(myView, QPoint(0, 0)), shownButtons->size());
            words.insert(QStringLiteral("keepClear"), QVariantMap{{QStringLiteral("x"), r.x()},
                                                                  {QStringLiteral("y"), r.y()},
                                                                  {QStringLiteral("w"), r.width()},
                                                                  {QStringLiteral("h"), r.height()}});
        }
    }
    if (live) myHtmlChrome->setQuestion(!words.isEmpty(), words);
    if (!words.isEmpty()) {
        myHtmlChrome->view()->raise();
        if (myWindowButtons) myWindowButtons->raise();
    }
#endif
}

void SceneWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
#ifdef FURNIFYME_WEB_UI
    if (event->type() == QEvent::WindowStateChange && myWindowControls)
        emit myWindowControls->maximizedChanged(isMaximized());
#endif
}

void SceneWindow::useHtmlChrome()
{
#ifdef FURNIFYME_WEB_UI
    if (myHtmlChrome || !myView) return;
    myActionRegistry = new ActionRegistry(this);
    myActionRegistry->adoptChildren(this);
    myHtmlChrome = new EditorChrome(myView, this);

    // The page's Undo pill: through the action, and only for the change it
    // names (the editor's review I1).
    connect(myHtmlChrome, &EditorChrome::undoRequested, this, [this](int stamp) {
        if (stamp >= 0 && stamp != static_cast<int>(myScene.revision())) return;
        if (myUndoAction && myUndoAction->isEnabled()) myUndoAction->trigger();
    });
    // The close question's answers, through the Qt card's own answer().
    connect(myHtmlChrome, &EditorChrome::questionAnswered, this, [this](const QString& which) {
        if (!myCloseCard || !myCloseCard->isAsking()) return;
        if (which == QLatin1String("save")) myCloseCard->answer(UnsavedCloseCard::Answer::Save);
        else if (which == QLatin1String("discard")) myCloseCard->answer(UnsavedCloseCard::Answer::Discard);
        else if (which == QLatin1String("keep")) myCloseCard->answer(UnsavedCloseCard::Answer::Keep);
    });
    if (myCloseCard) connect(myCloseCard, &UnsavedCloseCard::questionChanged, this, &SceneWindow::syncHtmlChrome);

    // THE PILL AND THE ARRANGING CHIPS (the editor's Phase 3i): slots take
    // the Qt surfaces' places while the page is live.
    myPillSlot = new HtmlCardSlot(QStringLiteral("pill"), myAppBar->sizeHint(), myView);
    myPillSlot->hide();
    myHtmlChrome->addCard(myPillSlot);
    myViewSlot = new HtmlCardSlot(QStringLiteral("view"), myArrangeTools->sizeHint(), myView);
    myViewSlot->hide();
    myHtmlChrome->addCard(myViewSlot);
    std::vector<ShellBridge::Chip> view = {
        ShellBridge::Chip{myMoveAction, IconSet::Glyph::Move, QStringLiteral("move")},
        ShellBridge::Chip{myRotateAction, IconSet::Glyph::Rotate, QStringLiteral("rotate")},
        ShellBridge::Chip{myRenderModeAction, IconSet::Glyph::Render, QStringLiteral("render")}};
    // The PILL's menu bar: the window's own was reparented into the AppBar, and
    // asking QMainWindow::menuBar() again quietly makes a new, empty one.
    if (myWindowButtons) {
        myButtonsSlot = new HtmlCardSlot(QStringLiteral("buttons"), myWindowButtons->size(), myView);
        myButtonsSlot->hide();
        myHtmlChrome->addCard(myButtonsSlot);
        myWindowControls = new HubWindowControls(this);
        myHtmlChrome->registerObject(QStringLiteral("window"), myWindowControls);
    }
    std::vector<ShellBridge::Chip> back;
    if (myStudio && myStudio->exitChip() && myRenderModeAction) {
        myBackSlot = new HtmlCardSlot(QStringLiteral("back"), myStudio->exitChip()->sizeHint(), myView);
        myBackSlot->hide();
        myHtmlChrome->addCard(myBackSlot);
        back.push_back(ShellBridge::Chip{myRenderModeAction, IconSet::Glyph::Back, QStringLiteral("back")});
    }
    myShellBridge = new ShellBridge(myActionRegistry, {}, std::move(view), myAppBar->menus(), this, myHtmlChrome,
                                    std::move(back));
    if (myStudio && myBackSlot) myStudio->setExitSlot(myBackSlot, [this] { return htmlChromeLive(); });
    myHtmlChrome->registerObject(QStringLiteral("shell"), myShellBridge);
    myHtmlChrome->registerObject(QStringLiteral("menu"), myShellBridge->menu());
    // Every menu shortcut fires while the HTML pill shows (the editor's R5).
    myPillSlot->addActions(myShellBridge->menuActions());
    connect(myShellBridge, &ShellBridge::pillReported, this, [this](int w, int h) {
        if (myPillSlot) myPillSlot->setContentSize(QSize(w, h));
    });
    for (HtmlCardSlot* slot : {myPillSlot, myViewSlot}) {
        connect(slot, &HtmlCardSlot::contentHeightChanged, this, [this] {
            if (myOverlay) myOverlay->relayout();
            syncViewportMinimumWidth();
        });
    }
    connect(myPillSlot, &HtmlCardSlot::placed, this, &SceneWindow::syncHtmlChrome);
    // An open menu owns the pointer and the keys (the editor's R7).
    connect(myShellBridge->menu(), &MenuModel::openChanged, this, [this](bool open) {
        if (myHtmlChrome) myHtmlChrome->setMenuOpen(open);
    });
    // Alt+letter opens that menu, as the Qt menu bar's mnemonics do (R6).
    const QVariantList titles = myShellBridge->menu()->titles();
    for (int i = 0; i < titles.size(); ++i) {
        const QString title = titles.at(i).toMap().value(QStringLiteral("title")).toString();
        const int at = titles.at(i).toMap().value(QStringLiteral("mnemonicAt")).toInt();
        if (at < 0 || at >= title.size()) continue;
        const QChar letter = title.at(at).toUpper();
        if (letter < QLatin1Char('A') || letter > QLatin1Char('Z')) continue;
        const int key = Qt::Key_A + (letter.unicode() - u'A');
        auto* shortcut = new QShortcut(QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key(key))), this);
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setEnabled(false);
        connect(shortcut, &QShortcut::activated, this, [this, i] {
            if (myShellBridge && htmlChromeLive()) myShellBridge->menu()->openMenu(i, true);
        });
        myMenuShortcuts << shortcut;
    }

    // THE TWO CARDS (Phase 5b): the Pieces card in the Qt card's TopLeft
    // place, its height the page's; the add card drawn centred by the page.
    // Both send the Qt cards' own signals into the same handlers.
    myPiecesSlot = new HtmlCardSlot(QStringLiteral("pieces"), myPieces->sizeHint(), myView);
    myPiecesSlot->hide();
    myHtmlChrome->addCard(myPiecesSlot);
    connect(myPiecesSlot, &HtmlCardSlot::contentHeightChanged, this, [this] {
        if (myOverlay) myOverlay->relayout();
    });
    myPiecesModel = new ScenePiecesModel(myHtmlChrome);
    myPiecesModel->setAddAction(myAddPieceAction);
    connect(myPiecesModel, &ScenePiecesModel::selectionRequested, this, &SceneWindow::selectPieceFromList);
    connect(myPiecesModel, &ScenePiecesModel::visibilityToggled, this, &SceneWindow::setPieceShown);
    connect(myPiecesModel, &ScenePiecesModel::removeRequested, this, [this](int pieceId) { removePiece(pieceId); });
    connect(myPiecesModel, &ScenePiecesModel::renameCommitted, this,
            [this](int pieceId, const QString& name) { renamePiece(pieceId, name); });
    myHtmlChrome->registerObject(QStringLiteral("pieces"), myPiecesModel);
    myAddPieceModel = new AddPieceModel(myHtmlChrome);
    myAddPieceModel->setLibraryAction(myCloseSceneAction);
    connect(myAddPieceModel, &AddPieceModel::chosen, this, [this](const QString& id) { addPiece(id); });
    myHtmlChrome->registerObject(QStringLiteral("addPiece"), myAddPieceModel);

    // Until the page says it is listening, and for good once it dies, the Qt
    // surfaces carry everything.
    connect(myHtmlChrome, &EditorChrome::liveChanged, this, [this](bool live) {
        if (myOverlay && myPiecesSlot && myPieces) {
            if (live) myOverlay->replaceWidget(myPieces, myPiecesSlot);
            else myOverlay->replaceWidget(myPiecesSlot, myPieces);
        }
        // An add card the page was showing goes with the page.
        if (!live && myAddPieceModel) myAddPieceModel->close();
        if (myOverlay && myPillSlot && myAppBar) {
            if (live) myOverlay->replaceWidget(myAppBar, myPillSlot);
            else myOverlay->replaceWidget(myPillSlot, myAppBar);
            if (myStudio && myStudio->barSlide()) {
                if (live) myStudio->barSlide()->retarget(myPillSlot, false);
                else myStudio->barSlide()->retarget(myAppBar, true);
            } else {
                (live ? static_cast<QWidget*>(myPillSlot) : myAppBar)->show();
            }
        }
        if (myOverlay && myViewSlot && myArrangeTools) {
            if (live) myOverlay->replaceWidget(myArrangeTools, myViewSlot);
            else myOverlay->replaceWidget(myViewSlot, myArrangeTools);
        }
        if (myOverlay && myButtonsSlot && myWindowButtons) {
            if (live) myOverlay->replaceWidget(myWindowButtons, myButtonsSlot);
            else myOverlay->replaceWidget(myButtonsSlot, myWindowButtons);
            (live ? static_cast<QWidget*>(myButtonsSlot) : myWindowButtons)->show();
        }
        if (myOverlay && myBackSlot && myStudio && myStudio->exitChip()) {
            if (live) myOverlay->replaceWidget(myStudio->exitChip(), myBackSlot);
            else myOverlay->replaceWidget(myBackSlot, myStudio->exitChip());
            myStudio->refresh();
        }
        if (!live && myShellBridge) myShellBridge->menu()->close();
        if (myCloseCard) myCloseCard->setDrawnElsewhere(live);
        refreshSurfaces();
        if (myOverlay) myOverlay->relayout();
    });
    connect(myStatusLine, &StatusLine::changed, this, &SceneWindow::syncHtmlChrome);
    myHtmlChrome->start();
    refreshSurfaces();
#endif
}
