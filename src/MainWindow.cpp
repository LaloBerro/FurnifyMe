#include "MainWindow.h"

#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"

#include "AppBar.h"
#include "AppearancePanel.h"
#include "AxisGizmo.h"
#include "BevelArrow.h"
#include "ExtrudePreview.h"
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "PullArrow.h"
#include "ShortcutSheet.h"
#include "Theme.h"
#include "Toast.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "ViewportOverlay.h"
#include "WalkthroughPanel.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <ElSLib.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QCloseEvent>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

MainWindow::MainWindow(QWidget* parent, bool persistProgress)
    : QMainWindow(parent)
    , myPersistProgress(persistProgress)
{
    if (myPersistProgress) {
        const QSettings settings;
        myProgress.deserialize(
            settings.value(QStringLiteral("progress")).toString().toStdString());
        // Read before buildActions() so the Units menu's initial checked
        // state, and the readout label built in buildOverlay(), both agree
        // with what was last chosen - same guard as the learning progress,
        // so the suite (persistProgress=false) can never read the
        // developer's real store.
        if (settings.value(QStringLiteral("displayUnit")).toString() ==
            QStringLiteral("cm"))
            Measure::setDisplayUnit(Measure::Unit::Centimetres);

        // The base projection, on the same terms as the unit above: a
        // preference, read once, under the same guard, so the suite can never
        // see the developer's own choice. It cannot be applied here - the
        // viewport does not exist yet - so it is held until it does.
        myStartOrthographic = settings.value(QStringLiteral("projection")).toString() ==
                              QStringLiteral("ortho");

        // Before a single widget exists, for the same reason as the unit
        // above: every card measures itself with the type scale in its own
        // constructor, so installing the spec afterwards would leave the
        // shell laid out for a size it is no longer wearing. Theme::apply()
        // has already installed defaultSpec() by now (main.cpp calls it
        // before this window is built), so a garbage or absent setting simply
        // leaves the app at its shipped appearance - deserializeSpec()
        // guarantees `stored` is untouched when it refuses.
        Theme::Spec stored;
        if (Theme::deserializeSpec(
                settings.value(QStringLiteral("appearance")).toString(), stored))
            Theme::setSpec(stored);
    }

    myView = new OcctViewWidget(this);
    // Straight onto the camera rather than through setBaseProjection(): that
    // one persists and calls updateActions(), and neither the settings store
    // nor half the shell is ready to be asked anything yet. The viewport has
    // not initialized its OCCT view either - it does that lazily on its first
    // paint - and applyCameraState() reads this state then, so the very first
    // frame is already drawn in the mode the user left.
    if (myStartOrthographic)
        myView->camera().setBaseProjection(CameraController::Projection::Orthographic);
    // Full bleed: the central widget is the viewport and nothing else. The
    // items panel used to take a splitter pane out of the window's width;
    // it is a floating drawer over the viewport now (see buildOverlay()),
    // which is why there is no longer anything to split.
    setCentralWidget(myView);

    // Parented to the viewport from birth - buildOverlay() anchors it, and
    // ViewportOverlay would reparent it anyway, but a card that is a child of
    // the window until then would flash in the wrong place on the first show.
    myItemsPanel = new ItemsPanel(&myDocument, myView, myView);

    connect(myView, &OcctViewWidget::sketchPointPicked, this, &MainWindow::onSketchPointPicked);
    connect(myView, &OcctViewWidget::sketchCursorMoved, this, &MainWindow::onSketchCursorMoved);
    connect(myView, &OcctViewWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
    // The second route to Lock to Face. The viewport reports the gesture; this
    // window decides what it means, and both routes land in the same
    // lockToFace() - including its refusal - rather than one of them growing
    // its own copy of the rule.
    connect(myView, &OcctViewWidget::faceDoubleClicked, this, &MainWindow::lockToFace);
    // The transform gizmo reports the end of a drag; this window decides what
    // it means, exactly as it does for the face-pull arrow above.
    connect(myView, &OcctViewWidget::gizmoReleased, this, &MainWindow::onGizmoReleased);

    buildActions();
    buildAppBar(buildMenus());
    buildOverlay();

    myShortcutSheet = new ShortcutSheet(this);
    connect(myShortcutsAction, &QAction::triggered, myShortcutSheet, &ShortcutSheet::showSheet);

    // appStateChanged alone, not documentChanged too: every document edit
    // already calls updateActions() (and so emits appStateChanged) before it
    // emits documentChanged, so a second connection here only rebuilt the
    // same rows twice per edit. appStateChanged also covers the case
    // documentChanged never fires for - switching the display unit, which
    // touches no document but still has to reread every dimension the panel
    // shows (see setDisplayUnit()).
    connect(this, &MainWindow::appStateChanged, myItemsPanel, &ItemsPanel::refresh);

    // Connected AFTER the refresh above, so it runs after it: a row added or
    // removed changes the drawer's height, and the drawer's rectangle is one
    // of the obstacles the toast, the balloon and the guide place themselves
    // against - re-laying out here is what keeps that set current between
    // resizes.
    //
    // Its visibility is re-DERIVED from the action here rather than only
    // being set when the action is toggled. CLAUDE.md's rule, learned twice
    // already on this viewport (WalkthroughPanel's skip control, Toast's undo
    // pill): a one-shot hide is not a state, and anything that shows a
    // widget's siblings wholesale - QWidget::showChildren() on the window's
    // first show, for one - will happily undo it. Reading it off the action
    // on every state change means the two cannot drift.
    //
    // Only reads state and moves geometry, so it cannot recurse back into
    // updateActions().
    connect(this, &MainWindow::appStateChanged, this, [this] {
        myItemsPanel->setVisible(myItemsPanelAction->isChecked());
        // Derived on every state change from the action, exactly as the
        // drawer above is and for exactly the same reason - a one-shot hide
        // is not a state, and QWidget::showChildren() on the window's first
        // show will happily undo one.
        if (myAppearancePanel)
            myAppearancePanel->setVisible(myAppearanceAction->isChecked());
        if (myOverlay) myOverlay->relayout();
    });

    // A dimension label reads through Measure too, so it has to follow a unit
    // switch the way the items panel and the status bar do. DimensionRenderer
    // is not a QObject - it draws, it does not listen - so the window drives
    // it from the one signal every unit-following surface already refreshes
    // on, rather than setDisplayUnit() growing a private list of everything
    // that shows a length. refreshDimension() redraws only what is already on
    // screen and is a no-op otherwise, so this cannot make an annotation
    // appear; and it only reads and repaints, so it cannot recurse back into
    // updateActions().
    connect(this, &MainWindow::appStateChanged, myView, &OcctViewWidget::refreshDimension);

    // The transform gizmo's visibility, derived on every state change from the
    // one predicate that decides it - never set from the event that happened
    // to make it true. Only reads state and attaches or detaches an AIS
    // object, so it cannot recurse back into updateActions().
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::refreshTransformGizmo);

    // And the edge annotation's, from the bevel arrow's predicate - two
    // annotations on one edge is noise, so the length label stands down for as
    // long as the arrow's own value chip is up. Only reads state and moves AIS
    // objects, so it cannot recurse back into updateActions().
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::refreshEdgeAnnotation);

    // Selection syncs both ways.
    connect(myItemsPanel, &ItemsPanel::solidActivated, this,
            [this](int id) { myView->setSelectedSolids({id}); });
    connect(myView, &OcctViewWidget::selectionChanged, this,
            [this] { myItemsPanel->showSelection(myView->selectedSolidIds()); });

    // The Items rail button, the menu entry and Ctrl+Alt+S all drive the one
    // action, and the drawer's shown state is read off that action rather
    // than stored - exactly as every chip mirrors an action rather than
    // remembering a mode.
    //
    // One other thing does call setVisible() on it: ViewportOverlay::addWidget()
    // show()s whatever it anchors, which is right for every other entry it
    // takes. The derivation wins rather than the initial show, because the
    // appStateChanged slot below re-reads the action on every state change -
    // so an overlay that shows a drawer whose action is unchecked is
    // corrected before the window is ever on screen. That is the point of
    // deriving it repeatedly instead of only on toggle.
    //
    // Opening or closing it also re-lays the overlay out, because the drawer
    // is one of the rectangles ViewportOverlay::occupiedRects() reports and
    // the toast, the balloon and the guide place themselves against that set.
    // Without this, opening the drawer would leave a live toast sitting
    // underneath it until the next resize. relayout() only reads and moves
    // geometry, so it cannot recurse back into updateActions().
    connect(myItemsPanelAction, &QAction::toggled, this, [this](bool shown) {
        myItemsPanel->setVisible(shown);
        if (myOverlay) myOverlay->relayout();
    });

    // The Appearance card on the same terms - see the drawer's toggle above.
    connect(myAppearanceAction, &QAction::toggled, this, [this](bool shown) {
        if (myAppearancePanel) myAppearancePanel->setVisible(shown);
        if (myOverlay) myOverlay->relayout();
    });

    // Theme's broadcast, relayed into this window. Connected to the
    // application-wide notifier rather than to the panel: a spec can also be
    // installed with no panel involved (the persisted one at startup, or a
    // reset), and a relay hung off the panel would miss both.
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &MainWindow::onThemeChanged);

    updateActions();

    // Theme.cpp's stylesheet reaches the status bar's own internal message
    // label through the QStatusBar/QStatusBar QLabel selectors (it is
    // created privately by showMessage() and this code never gets a pointer
    // to it) - this sets the same size directly, so a plain QStatusBar with
    // no matching stylesheet rule would still be correct.
    statusBar()->setFont(Theme::labelFont());

    // Permanent widget so it survives transient showMessage() calls: the left
    // side reports what just happened, the right side always says where you are.
    myStateLabel = new QLabel(this);
    statusBar()->addPermanentWidget(myStateLabel);
    updateStateLabel();
    syncChromeHeights();

    setWindowTitle(tr("FurnifyMe"));
    resize(1280, 800);
    statusBar()->showMessage(tr("Right-drag to orbit, middle-drag to pan, wheel to zoom"));
}

void MainWindow::buildActions()
{
    myStartSketchAction = new QAction(tr("&Start Sketch"), this);
    myStartSketchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(myStartSketchAction, &QAction::triggered, this, &MainWindow::onStartSketch);

    myFinishSketchAction = new QAction(tr("&Finish Sketch"), this);
    myFinishSketchAction->setShortcut(QKeySequence(Qt::Key_Return));
    connect(myFinishSketchAction, &QAction::triggered, this, &MainWindow::onFinishSketch);

    myUndoPointAction = new QAction(tr("&Undo Last Point"), this);
    myUndoPointAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    connect(myUndoPointAction, &QAction::triggered, this, &MainWindow::onUndoSketchPoint);

    myCancelSketchAction = new QAction(tr("&Cancel Sketch"), this);
    myCancelSketchAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(myCancelSketchAction, &QAction::triggered, this, &MainWindow::onCancelSketch);

    myExtrudeAction = new QAction(tr("&Extrude..."), this);
    myExtrudeAction->setShortcut(QKeySequence(Qt::Key_E));
    connect(myExtrudeAction, &QAction::triggered, this, &MainWindow::onExtrude);

    myUnionAction = new QAction(tr("&Union"), this);
    connect(myUnionAction, &QAction::triggered, this, &MainWindow::onUnion);

    mySubtractAction = new QAction(tr("&Subtract"), this);
    connect(mySubtractAction, &QAction::triggered, this, &MainWindow::onSubtract);

    myIntersectAction = new QAction(tr("&Intersect"), this);
    connect(myIntersectAction, &QAction::triggered, this, &MainWindow::onIntersect);

    myLockFaceAction = new QAction(tr("&Lock to Face"), this);
    myLockFaceAction->setShortcut(QKeySequence(Qt::Key_L));
    myLockFaceAction->setToolTip(lockTooltipText());
    connect(myLockFaceAction, &QAction::triggered, this, &MainWindow::onLockToFace);

    myUnlockFaceAction = new QAction(tr("U&nlock Face"), this);
    myUnlockFaceAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    myUnlockFaceAction->setToolTip(unlockTooltipText());
    connect(myUnlockFaceAction, &QAction::triggered, this, &MainWindow::unlockFace);

    myExportStepAction = new QAction(tr("Export &STEP..."), this);
    myExportStepAction->setShortcut(QKeySequence::Save);
    connect(myExportStepAction, &QAction::triggered, this, &MainWindow::onExportStep);

    mySolidSelectAction = new QAction(tr("Select &Bodies"), this);
    mySolidSelectAction->setCheckable(true);
    mySolidSelectAction->setChecked(true);
    myFaceSelectAction = new QAction(tr("Select F&aces"), this);
    myFaceSelectAction->setCheckable(true);
    myEdgeSelectAction = new QAction(tr("Select &Edges"), this);
    myEdgeSelectAction->setCheckable(true);
    myEdgeSelectAction->setToolTip(tr("Pick one edge at a time\n"
                                      "Hovering shows its length."));

    myDeleteAction = new QAction(tr("&Delete Selected"), this);
    myDeleteAction->setShortcut(QKeySequence::Delete);
    myDeleteAction->setToolTip(tr("Delete the selected bodies (Del)"));
    connect(myDeleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelected);

    myUndoAction = new QAction(tr("&Undo"), this);
    myUndoAction->setShortcut(QKeySequence::Undo);
    myUndoAction->setToolTip(tr("Undo the last change to your bodies (Ctrl+Z)"));
    connect(myUndoAction, &QAction::triggered, this, &MainWindow::onUndo);

    myRedoAction = new QAction(tr("&Redo"), this);
    myRedoAction->setShortcut(QKeySequence::Redo);
    myRedoAction->setToolTip(tr("Redo the change you just undid (Ctrl+Y)"));
    connect(myRedoAction, &QAction::triggered, this, &MainWindow::onRedo);

    mySnapAction = new QAction(tr("Snap to &Grid"), this);
    mySnapAction->setCheckable(true);
    mySnapAction->setChecked(true);
    mySnapAction->setToolTip(snapTooltipText());
    connect(mySnapAction, &QAction::toggled, this, &MainWindow::onSnapToggled);

    myItemsPanelAction = new QAction(tr("Items"), this);
    myItemsPanelAction->setCheckable(true);
    myItemsPanelAction->setChecked(true);
    myItemsPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    myItemsPanelAction->setToolTip(tr("Show or hide the list of bodies (Ctrl+Alt+S)"));

    // Menu only, and deliberately: the rail stays at thirteen tools. Choosing
    // colours is not a modelling tool and does not belong in the spine the
    // user's hand lives on. Checkable, because the panel's visibility is
    // DERIVED from it in both directions - the same contract the items drawer
    // has, and the reason nothing else in this file shows or hides the panel.
    myAppearanceAction = new QAction(tr("Appearance..."), this);
    myAppearanceAction->setCheckable(true);
    myAppearanceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    myAppearanceAction->setToolTip(tr("Choose the app's colours and text size (Ctrl+Alt+A)\n"
                                      "Every change is applied as you make it."));

    // The projection toggle. Checkable, because the mode is state the user
    // chose and comes back next session; a QAction rather than a button that
    // decides for itself, because the bar's readout, the View menu entry and
    // the O shortcut all have to say the same thing - and because the
    // generated shortcut sheet lists it for free the moment it carries a
    // binding.
    //
    // Reads its initial state from the camera, which the constructor has
    // already set from the stored preference, rather than from that
    // preference a second time.
    myOrthographicAction = new QAction(tr("&Orthographic"), this);
    myOrthographicAction->setCheckable(true);
    myOrthographicAction->setChecked(myView->camera().baseProjection() ==
                                     CameraController::Projection::Orthographic);
    myOrthographicAction->setShortcut(QKeySequence(Qt::Key_O));
    myOrthographicAction->setToolTip(tr("Draw without perspective (O)\n"
                                        "Parallel edges stay parallel, so a face seen "
                                        "straight on reads at its true shape."));
    connect(myOrthographicAction, &QAction::toggled, this, &MainWindow::setBaseProjection);

    myDisplayModeAction = new QAction(tr("Wireframe"), this);
    myDisplayModeAction->setCheckable(true);
    myDisplayModeAction->setToolTip(tr("Draw bodies as edges only\n"
                                       "Useful for seeing through to what is behind."));
    connect(myDisplayModeAction, &QAction::toggled, this,
            [this](bool on) { myView->setWireframe(on); });

    myFitAction = new QAction(tr("&Fit All"), this);
    myFitAction->setShortcut(QKeySequence(Qt::Key_F));
    myFitAction->setToolTip(tr("Frame every body in the viewport (F)"));
    connect(myFitAction, &QAction::triggered, myView, &OcctViewWidget::fitAll);

    myScreenshotAction = new QAction(tr("Save S&creenshot..."), this);
    myScreenshotAction->setToolTip(tr("Save the viewport as a PNG image"));
    connect(myScreenshotAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
                                                          QString(), tr("PNG image (*.png)"));
        if (!path.isEmpty() && !myView->saveSnapshot(path)) {
            myToasts->show(tr("Screenshot failed — Couldn't save the image to %1 — "
                              "Check that the folder exists and isn't read-only")
                              .arg(path),
                          Toast::Kind::Failure, false);
        }
    });

    myStartSketchAction->setToolTip(tr("Draw an outline on the ground (Ctrl+K)\n"
                                       "Click to place points; close it to make a face."));
    myFinishSketchAction->setToolTip(tr("Close the outline into a face (Enter)\n"
                                        "Needs at least three points."));
    // NOT "pull the face up": Pull is the face-dragging operation's own name
    // now (see CLAUDE.md's vocabulary table), and one word for two operations
    // is the thing that table exists to stop. Extrude raises a closed outline;
    // Pull moves a face of a body that already exists.
    myExtrudeAction->setToolTip(tr("Raise the face into a body (E)\n"
                                   "The outline's shape becomes the body's footprint."));
    myUnionAction->setToolTip(tr("Combine two bodies into one\n"
                                 "Overlapping material is kept once, not twice."));
    mySubtractAction->setToolTip(tr("Cut the second body out of the first\n"
                                    "Like a chisel removing waste. The body you made "
                                    "first is the one that keeps its shape."));
    myIntersectAction->setToolTip(tr("Keep only where two bodies overlap\n"
                                     "Everything outside the shared volume is discarded."));

    auto* selectionGroup = new QActionGroup(this);
    selectionGroup->addAction(mySolidSelectAction);
    selectionGroup->addAction(myFaceSelectAction);
    selectionGroup->addAction(myEdgeSelectAction);
    selectionGroup->setExclusive(true);
    connect(mySolidSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
    connect(myFaceSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
    connect(myEdgeSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);

    myUnitsMillimetresAction = new QAction(tr("Millimetres"), this);
    myUnitsMillimetresAction->setCheckable(true);
    myUnitsCentimetresAction = new QAction(tr("Centimetres"), this);
    myUnitsCentimetresAction->setCheckable(true);

    auto* unitsGroup = new QActionGroup(this);
    unitsGroup->addAction(myUnitsMillimetresAction);
    unitsGroup->addAction(myUnitsCentimetresAction);
    unitsGroup->setExclusive(true);

    // Reflects whatever setDisplayUnit() the constructor already applied from
    // the persisted setting (or the Millimetres default), before this action
    // group exists at all.
    const bool startsInCentimetres = Measure::displayUnit() == Measure::Unit::Centimetres;
    myUnitsMillimetresAction->setChecked(!startsInCentimetres);
    myUnitsCentimetresAction->setChecked(startsInCentimetres);

    connect(myUnitsMillimetresAction, &QAction::triggered, this,
            [this] { setDisplayUnit(Measure::Unit::Millimetres); });
    connect(myUnitsCentimetresAction, &QAction::triggered, this,
            [this] { setDisplayUnit(Measure::Unit::Centimetres); });
}

QMenuBar* MainWindow::buildMenus()
{
    // Ours from the start, never the window's auto-created one - see the
    // declaration in MainWindow.h and the trap at the top of AppBar.h. It is
    // parented to the window only so it is never briefly a top-level widget;
    // the app bar's layout adopts it a moment later.
    auto* bar = new QMenuBar(this);

    QMenu* fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(myExportStepAction);
    fileMenu->addAction(myScreenshotAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu* sketchMenu = bar->addMenu(tr("&Sketch"));
    sketchMenu->addAction(myStartSketchAction);
    sketchMenu->addAction(myFinishSketchAction);
    sketchMenu->addAction(myUndoPointAction);
    sketchMenu->addAction(myCancelSketchAction);
    sketchMenu->addSeparator();
    sketchMenu->addAction(myLockFaceAction);
    sketchMenu->addAction(myUnlockFaceAction);

    QMenu* editMenu = bar->addMenu(tr("&Edit"));
    editMenu->addAction(myUndoAction);
    editMenu->addAction(myRedoAction);
    editMenu->addSeparator();
    editMenu->addAction(myDeleteAction);

    QMenu* modelMenu = bar->addMenu(tr("&Model"));
    modelMenu->addAction(myExtrudeAction);
    modelMenu->addSeparator();
    modelMenu->addAction(myUnionAction);
    modelMenu->addAction(mySubtractAction);
    modelMenu->addAction(myIntersectAction);

    QMenu* viewMenu = bar->addMenu(tr("&View"));
    viewMenu->addAction(myFitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Axonometric"), QKeySequence(Qt::Key_0), this,
                        &MainWindow::goAxonometric);
    // These three name a DIRECTION and nothing else: unlike a click on a gizmo
    // tip, they deliberately do not borrow an orthographic look, so they leave
    // the projection exactly as the user set it. The asymmetry is the brief's,
    // and it is a real distinction rather than an oversight - the gizmo is a
    // direct-manipulation gesture aimed at a face of a cube, where convergence
    // is the thing being complained about, while these are a menu entry and a
    // number key that mean "look from the top" and make no claim about how the
    // scene should be drawn once you get there.
    viewMenu->addAction(tr("&Top"), QKeySequence(Qt::Key_1), this, [this] {
        myView->setViewTop();
        recordViewChanged();
    });
    viewMenu->addAction(tr("F&ront"), QKeySequence(Qt::Key_2), this, [this] {
        myView->setViewFront();
        recordViewChanged();
    });
    viewMenu->addAction(tr("&Right"), QKeySequence(Qt::Key_3), this, [this] {
        myView->setViewRight();
        recordViewChanged();
    });
    viewMenu->addSeparator();
    // Beside the named views, because it is the other half of "what am I
    // looking at" - but below the separator, because it changes how the scene
    // is drawn rather than where the camera stands.
    viewMenu->addAction(myOrthographicAction);
    viewMenu->addSeparator();
    viewMenu->addAction(mySnapAction);
    viewMenu->addSeparator();
    viewMenu->addAction(mySolidSelectAction);
    viewMenu->addAction(myFaceSelectAction);
    viewMenu->addAction(myEdgeSelectAction);
    viewMenu->addAction(myItemsPanelAction);
    viewMenu->addSeparator();
    QMenu* unitsMenu = viewMenu->addMenu(tr("Units"));
    unitsMenu->addAction(myUnitsMillimetresAction);
    unitsMenu->addAction(myUnitsCentimetresAction);
    viewMenu->addAction(myAppearanceAction);

    QMenu* helpMenu = bar->addMenu(tr("&Help"));

    myShortcutsAction = new QAction(tr("Keyboard Shortcuts"), this);
    // Both bindings the design calls for. F1 is what people reach for without
    // being told; ? is what the sheet itself is worth advertising.
    myShortcutsAction->setShortcuts(
        {QKeySequence(Qt::Key_Question), QKeySequence(Qt::Key_F1)});
    myShortcutsAction->setToolTip(tr("List every keyboard shortcut (? or F1)"));
    helpMenu->addAction(myShortcutsAction);

    helpMenu->addAction(tr("Show tips again"), this, [this] {
        myProgress.reset();
        if (myPersistProgress) {
            QSettings settings;
            settings.setValue(QStringLiteral("progress"), QString());
        }
        statusBar()->showMessage(tr("Tips reset — the guide and hints will appear again"));
        // Before appStateChanged, not after: a surface that remembers what it
        // already showed this session has to forget that first, or the
        // reconsider() this emission drives would find every hint still
        // marked as spent and put none of them back. Emptying the store is
        // only half of what "show tips again" means.
        emit progressReset();
        emit appStateChanged();
        // The guide has just reappeared, somewhere in the middle of that
        // emission. Anything that places itself against it - the toast, the
        // hint balloon - has to be told, and which slot ran first on
        // appStateChanged is not something to rely on. relayout() re-places
        // every anchored widget and then emits laidOut(), which is the one
        // ordering guarantee in this file: the dependents re-place after the
        // guide is at its final rectangle, not before. HintBalloon already
        // handled this case for itself inside reconsider(); ToastHost did
        // not, so a Show tips again under a live toast left the restored
        // guide sitting on top of it.
        if (myOverlay) myOverlay->relayout();
    });

    return bar;
}

void MainWindow::goAxonometric()
{
    // The one way back to the angled view. Its entry points - the View menu
    // and its 0 shortcut - call this, so the pose and the recorded event
    // cannot drift apart the way they would if each site re-derived the
    // camera state for itself. The app bar's button used to be a third; that
    // seat is the projection toggle now.
    //
    // The angled view is the opposite of a face-on one, so it hands back any
    // borrowed orthographic look rather than carrying it into a pose nothing
    // squared up for. A user who CHOSE Ortho keeps it - this clears the loan,
    // not the mode.
    myView->camera().setTemporaryOrtho(false);
    myView->setViewAxonometric();
    recordViewChanged();
}

void MainWindow::buildAppBar(QMenuBar* menus)
{
    myAppBar = new AppBar(menus, myDisplayModeAction, myFitAction);
    // The window takes ownership. Nothing may call menuBar() from here on.
    setMenuWidget(myAppBar);

    myAppBar->setOrthographic(myOrthographicAction->isChecked());
    myAppBar->setUnitLabel(QString::fromStdString(Measure::unitSuffix()));

    // The button triggers the action rather than flipping anything itself -
    // the same contract the unit chip has, and the reason the menu entry, the
    // O shortcut and this button can never disagree. It no longer snaps to
    // the axonometric pose: that is the gizmo's job, and the View menu's, and
    // this seat now belongs to the projection.
    connect(myAppBar, &AppBar::projectionClicked, this,
            [this] { myOrthographicAction->trigger(); });

    // The button triggers the OTHER unit's existing action rather than
    // writing the unit itself: persistence, the items panel, the status bar
    // and the extrude field's own label then all follow the single path
    // setDisplayUnit() already owns, and updateActions() stays the one place
    // that decides anything.
    connect(myAppBar, &AppBar::unitClicked, this, [this] {
        if (Measure::displayUnit() == Measure::Unit::Millimetres)
            myUnitsCentimetresAction->trigger();
        else
            myUnitsMillimetresAction->trigger();
    });

    // The readout follows the one signal every unit-following surface already
    // refreshes on. It only reads and sets a string, so it cannot recurse
    // back into updateActions().
    connect(this, &MainWindow::appStateChanged, myAppBar, [this] {
        myAppBar->setUnitLabel(QString::fromStdString(Measure::unitSuffix()));
        // The BASE mode, off the action that owns it - not the camera's
        // effective one. A gizmo arm or a face lock borrows orthographic for
        // one orbit, and a readout that followed the loan would tell the user
        // they had changed a setting they never touched.
        myAppBar->setOrthographic(myOrthographicAction->isChecked());
    });
}

void MainWindow::buildOverlay()
{
    myOverlay = new ViewportOverlay(myView);

    // ONE rail, pinned to the viewport's left edge, in place of the four
    // chip clusters that used to float in three corners and one edge centre.
    // Every button is an existing QAction rendered icon-only; nothing here
    // creates an action, and nothing here decides whether a button is
    // enabled or checked - updateActions() remains the single place that
    // does. The groups read top to bottom as the order of work: what to look
    // at, what to draw, what to build, what to pick, and - pushed to the
    // bottom by the stretch - what to take back.
    //
    // ViewportOverlay::relayout()'s LeftEdge case deliberately keeps every
    // button its designed size on a too-short viewport and lets the last one
    // run off the bottom edge - Redo first, then Undo - rather than squeezing
    // fixed-size buttons into a space they do not fit, which Qt resolves by
    // overlapping them. See that comment for why the clip is legible rather
    // than fixed there. It is fixed HERE instead, a few lines down, by never
    // letting the viewport get that short in the first place - the minimum
    // height is DERIVED from the rail's own sizeHint() rather than a measured
    // literal, so it cannot go stale the day a fourteenth button is added
    // (see CLAUDE.md's warning that the rail wants a rework well before a
    // screen's own height becomes the real ceiling this derivation cannot
    // push past).
    auto* rail = new ToolCluster(myView);
    auto tool = [rail](QAction* action, IconSet::Glyph glyph) {
        rail->addChip(new ToolChip(action, glyph, ToolChip::ChipMode::IconOnly));
    };

    tool(myItemsPanelAction, IconSet::Glyph::Items);
    rail->addSeparator();
    tool(myStartSketchAction, IconSet::Glyph::Sketch);
    tool(myExtrudeAction,     IconSet::Glyph::Extrude);
    rail->addSeparator();
    tool(myUnionAction,       IconSet::Glyph::Fuse);
    tool(mySubtractAction,    IconSet::Glyph::Cut);
    tool(myIntersectAction,   IconSet::Glyph::Intersect);
    tool(myDeleteAction,      IconSet::Glyph::Delete);
    rail->addSeparator();
    tool(mySnapAction,        IconSet::Glyph::Snap);
    tool(mySolidSelectAction, IconSet::Glyph::SelectSolid);
    tool(myFaceSelectAction,  IconSet::Glyph::SelectFace);
    tool(myEdgeSelectAction,  IconSet::Glyph::SelectEdge);
    rail->addStretch();
    tool(myUndoAction,        IconSet::Glyph::Undo);
    tool(myRedoAction,        IconSet::Glyph::Redo);

    // The viewport must never be able to shrink shorter than the rail needs.
    // rail->sizeHint() is the rail's own natural stack height - every chip,
    // separator and gap, plus the card's own top/bottom padding - with the
    // stretch between Select Edges and Undo contributing nothing, the same
    // number ViewportOverlay::relayout() calls `ch` for a LeftEdge entry.
    // ViewportOverlay pins that entry kEdgeMargin px off BOTH the top and the
    // bottom of the viewport (see relayout()'s LeftEdge case), so the
    // viewport needs at least the rail's height plus twice that margin.
    // Read from ViewportOverlay itself rather than repeated here, so the two
    // cannot silently disagree about what the rail is pinned against.
    myView->setMinimumHeight(rail->sizeHint().height() + 2 * ViewportOverlay::kEdgeMargin);

    myOverlay->addWidget(rail, ViewportOverlay::Anchor::LeftEdge);

    // The items drawer, beside the rail rather than under it - see
    // ViewportOverlay's Anchor comment for why that is the layout's business
    // and not a hard-coded offset here. Anchoring it is also the whole of
    // what puts it in occupiedRects(), so the toast, the balloon and the
    // guide step around it without any of them naming this widget.
    myOverlay->addWidget(myItemsPanel, ViewportOverlay::Anchor::TopLeft);

    // Wireframe and Fit All are buttons in the app bar, and Save Screenshot -
    // the least used of the three, and absent from the design's bar and rail
    // alike - is reachable from the File menu.

    // The orientation gizmo. Its own label chip and the unit readout that sat
    // under it are in the app bar; only the axes stay over the viewport.
    auto* gizmo = new AxisGizmo(myView, myView);
    // Clicking an arm of the gizmo is the other way to look from a named
    // direction, and the hint that teaches the gizmo is retired by
    // view.changed - so a user who only ever used the gizmo used to dismiss
    // that hint every session and never cross the threshold. The gizmo
    // announces the snap and this window decides what it means; giving the
    // gizmo a MainWindow just to record an event would hand a painted
    // overlay a dependency on the whole application.
    connect(gizmo, &AxisGizmo::viewSnapped, this, &MainWindow::recordViewChanged);
    myOverlay->addWidget(gizmo, ViewportOverlay::Anchor::TopRight);

    // The Appearance card, anchored at the same corner so relayout() stacks
    // it one gap under the gizmo - see AppearancePanel.h for why TopRight and
    // not RightCenter. Hidden BEFORE it is added: ViewportOverlay::addWidget()
    // shows whatever it anchors unless the widget has already made an
    // explicit hide decision of its own, and this card's visibility belongs to
    // myAppearanceAction alone.
    myAppearancePanel = new AppearancePanel(myView);
    myAppearancePanel->hide();
    myOverlay->addWidget(myAppearancePanel, ViewportOverlay::Anchor::TopRight);

    // Every outcome the app reports - success or failure - goes through this
    // one host rather than a modal dialog. It parents itself (and its Toast)
    // to the viewport and positions itself, so it needs no overlay anchor of
    // its own; raise()ing on every show() keeps it above whatever cluster
    // happens to be on top. Undo, when a message offers it, replays through
    // the same onUndo() the Undo action itself uses.
    myToasts = new ToastHost(myView, this);
    // Through the ACTION, never straight to onUndo(). The menu entry, the
    // chip and the Ctrl+Z binding all obey myUndoAction's enabled state -
    // "!mySketching && canUndo()", set in updateActions(), which CLAUDE.md
    // makes the single place that decides what is available - and a toast
    // that called the slot directly was a fourth entry point that obeyed
    // none of it: delete a body, start a sketch inside the four-second
    // window, click Undo, and the document was resynced and the selection
    // cleared while the user was still placing points. updateActions() also
    // pushes that same enabled state onto the toast (see setUndoEnabled), so
    // the pill is dimmed and out of hit-testing rather than merely inert.
    connect(myToasts, &ToastHost::undoRequested, this, [this] {
        // Two guards saying two different things. The first is scope: a toast
        // names a change to the DOCUMENT, and since Ctrl+Z gained its
        // mid-sketch meaning the action would otherwise take back a point
        // under a message about a body. The second is availability, which
        // updateActions() owns. The pill is already dimmed and out of
        // hit-testing in both cases; this is the backstop for an event
        // delivered straight at it.
        if (mySketching) return;
        if (myUndoAction->isEnabled()) myUndoAction->trigger();
    });
    // A toast that offers to undo one operation must not survive that
    // operation - see ToastHost::documentMovedTo().
    connect(this, &MainWindow::documentChanged, this,
            [this] { myToasts->documentMovedTo(myDocument.revision()); });
    // A guide can appear UNDERNEATH a toast that is already up (Show tips
    // again does exactly that), and nothing told the toast to step aside
    // when it did - HintBalloon::reconsider() already handled that case for
    // itself. appStateChanged is when it happens - but the connection that
    // used to sit here, straight from appStateChanged to replace(), was
    // REDUNDANT with the one below on ViewportOverlay::laidOut(), not a
    // second necessary route: the lambda a few lines up
    // (myItemsPanel->setVisible(...)) already calls myOverlay->relayout() on
    // every appStateChanged, and relayout() itself emits laidOut() once every
    // anchored widget is at its final rectangle - so replace() was already
    // running once, in the right order, before this line ran it a second
    // time. Removed rather than kept as a belt-and-braces call: two
    // connections that fire from the same event and do the same thing is the
    // sort of drift this file's own rule against a second refresh path warns
    // about, and the survivor is the one ordered correctly - see the comment
    // on the laidOut() connection below.

    // Replaces the old QInputDialog::getDouble() for extrude height. Parents
    // itself to the viewport and positions itself (top-center, clear of the
    // toast/guide/balloon bottom strip - see ExtrudePreview::reposition()),
    // so it needs no overlay anchor of its own either.
    myExtrudePreview = new ExtrudePreview(this, myView);

    // The face-pull gizmo. Like the extrude panel it parents itself to the
    // viewport and places itself - beside the arrow's projected head rather
    // than against a viewport edge, so it needs no overlay anchor. It decides
    // its own visibility from MainWindow::canPullSelectedFace() on every
    // appStateChanged; nothing here shows or hides it.
    myPullArrow = new PullArrow(this, myView);

    // The bevel gizmo, on exactly the same terms: it parents itself to the
    // viewport, places itself beside its arrow's projected head, and decides
    // its own visibility from MainWindow::bevelTarget() on every
    // appStateChanged. Nothing here shows or hides it.
    myBevelArrow = new BevelArrow(this, myView);

    // Always built, even for a user who has already learned this - it
    // decides its own visibility in its constructor (see WalkthroughPanel's
    // refresh()) and hides itself immediately in that case. Gating
    // construction on hasLearned() here instead would mean a returning
    // user's window has no panel to bring back when Show tips again resets
    // their progress, and the guide would stay gone until the app is
    // restarted - exactly the case Show tips again exists for.
    myOverlay->addWidget(new WalkthroughPanel(this, myView),
                         ViewportOverlay::Anchor::BottomRight);

    // Built last, after the walkthrough, so the guide is never competing with
    // a hint on first run. It parents itself to the viewport and positions
    // itself, centred near the bottom rather than pinned to an edge, so it
    // needs no overlay anchor of its own.
    auto* hints = new HintBalloon(this, myView);

    // The three surfaces that place themselves against widgets the overlay
    // owns re-place themselves HERE, from the overlay's own "I have finished
    // laying out" signal - not from their own filters on the viewport's
    // resize event. Qt runs event filters last-installed-first, and the
    // overlay installs its own first, so those filters all ran BEFORE the
    // guide and the chip clusters had moved: on a shrink the toast and the
    // balloon stepped aside from where the guide used to be and the guide
    // then landed on top of them, and the extrude panel was raised before
    // relayout() raised the top-left cluster back over it. Ordering off the
    // signal makes "after the anchored widgets have moved" a property of the
    // code rather than an accident of construction order.
    //
    // This is myToasts's ONE connection to replace() - appStateChanged
    // reaches it too, but only by relaying through relayout()'s own laidOut()
    // emission (see the comment further up, where a second direct connection
    // to appStateChanged used to sit and double-call this).
    connect(myOverlay, &ViewportOverlay::laidOut, myToasts, &ToastHost::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, hints, &HintBalloon::reposition);
    connect(myOverlay, &ViewportOverlay::laidOut, myExtrudePreview, &ExtrudePreview::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, myPullArrow, &PullArrow::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, myBevelArrow, &BevelArrow::replace);
}

void MainWindow::updateActions()
{
    const std::size_t selectedCount = myView->selectedSolidIds().size();
    const bool booleanReady = !mySketching && selectedCount == 2;

    myStartSketchAction->setEnabled(!mySketching);
    myFinishSketchAction->setEnabled(mySketching && mySketch.canClose());
    myUndoPointAction->setEnabled(mySketching && mySketch.pointCount() > 0);
    myCancelSketchAction->setEnabled(mySketching);

    myExtrudeAction->setEnabled(!mySketching && !myPendingFace.IsNull());

    // Exactly one face, and a flat one: an outline needs a single plane to
    // live on, and a cylinder's side has no such plane. Both halves are
    // checked again inside lockToFace(), because the double-click route can
    // reach a curved face this enabled state never sees.
    //
    // Not while sketching: the points already placed live on the plane that is
    // about to be swapped, and an outline with points on two planes is not an
    // outline. Not while a closed outline is waiting either - see
    // canChangeSketchPlane() for what moving the plane out from under it does.
    // That is exactly canPullSelectedFace()'s rule too, so the two read the
    // same function rather than each carrying a copy of it.
    const bool planeCanMove = !mySketching && myPendingFace.IsNull();
    const bool flatFaceSelected = canPullSelectedFace();
    myLockFaceAction->setEnabled(flatFaceSelected);
    myUnlockFaceAction->setEnabled(myFaceLocked && planeCanMove);
    // A disabled control that does not say why is a control the user reads as
    // broken. Same idea as snapTooltipText(): recomputed here rather than
    // frozen at buildActions() time, so the reason is current.
    const QString pendingReason =
        tr("Unavailable while an outline is waiting — press E to extrude it, "
           "or Ctrl+K to start a new one");
    myLockFaceAction->setToolTip(myPendingFace.IsNull() ? lockTooltipText() : pendingReason);
    myUnlockFaceAction->setToolTip(myPendingFace.IsNull() ? unlockTooltipText()
                                                          : pendingReason);

    myUnionAction->setEnabled(booleanReady);
    mySubtractAction->setEnabled(booleanReady);
    myIntersectAction->setEnabled(booleanReady);

    myExportStepAction->setEnabled(myDocument.count() > 0);
    myDeleteAction->setEnabled(!mySketching && selectedCount > 0);
    // Mid-sketch, Undo removes the last placed point (onUndo() reroutes to
    // onUndoSketchPoint); outside a sketch it undoes a document change. The
    // menu text stays "Undo" either way - the user's word for "take that
    // back" does not change with the mode, and a menu entry whose label moved
    // under them would be worse than one whose scope did.
    //
    // Redo has no mid-sketch counterpart - a removed point is gone, not
    // parked on a stack - so it stays disabled while sketching. That
    // asymmetry is deliberate: it is better than a Redo that silently means
    // "redo a document change" while the user is looking at an outline.
    myUndoAction->setEnabled(mySketching ? mySketch.pointCount() > 0
                                         : myDocument.canUndo());
    myUndoAction->setToolTip(mySketching
                                 ? tr("Take back the last point you placed (Ctrl+Z)")
                                 : tr("Undo the last change to your bodies (Ctrl+Z)"));
    // The toast's Undo pill still triggers the action, but its availability
    // is the DOCUMENT half of that predicate, not the action's whole enabled
    // state. Until the reroute above the two were the same expression and
    // this line could just read the action; they are not any more, and the
    // pill has to keep the narrower one. A pill under "Deleted Body 02" that
    // quietly took back a sketch point instead would be the label describing
    // one change while the control performed another - the exact defect the
    // revision guard in ToastHost was added to end. Still decided here, in
    // the one place that decides what is available, and still pushed out
    // rather than re-derived at the toast.
    if (myToasts) myToasts->setUndoEnabled(!mySketching && myDocument.canUndo());
    myRedoAction->setEnabled(!mySketching && myDocument.canRedo());

    // Not a slot on appStateChanged - part of updateActions() itself, same
    // as updateStateLabel(), so it recomputes on every unit switch too
    // rather than freezing whatever unit was active when the tooltip was
    // first built in buildActions().
    mySnapAction->setToolTip(snapTooltipText());

    updateStateLabel();
    emit appStateChanged();
}

void MainWindow::recordViewChanged()
{
    recordProgress("view.changed");
    // Recording alone teaches nothing: the hint that points at the gizmo is
    // retired by reconsider(), which only ever runs off appStateChanged.
    // Without this the third press of 0 left a hint on screen for an action
    // the user had already learned. updateActions() touches nothing this
    // path depends on, so it cannot recurse back in here.
    updateActions();
}

void MainWindow::setBaseProjection(bool orthographic)
{
    myView->setBaseProjection(orthographic ? CameraController::Projection::Orthographic
                                           : CameraController::Projection::Perspective);
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("projection"),
                          orthographic ? QStringLiteral("ortho") : QStringLiteral("persp"));
    }
    // Not recordProgress(), and specifically not recordViewChanged() - see the
    // declaration in MainWindow.h. updateActions() ends by emitting
    // appStateChanged(), which is what the bar's readout follows; no second
    // refresh path.
    updateActions();
}

void MainWindow::setDisplayUnit(Measure::Unit unit)
{
    Measure::setDisplayUnit(unit);
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("displayUnit"),
                          unit == Measure::Unit::Centimetres ? QStringLiteral("cm")
                                                              : QStringLiteral("mm"));
    }
    // Not recordProgress(): the unit is a display preference, not a learned
    // capability, so it never touches UserProgress. updateActions() ends by
    // emitting appStateChanged(), which is what the items panel, the units
    // readout and the status bar all already refresh from - no second
    // refresh path needed.
    updateActions();
}

void MainWindow::onThemeChanged()
{
    // The status bar's font is SET, not inherited: Theme.cpp's stylesheet
    // reaches QStatusBar's own internal message label through a selector, and
    // this covers a plain QStatusBar with no matching rule. An explicitly set
    // font does not follow QApplication::setFont, so it has to be put back.
    statusBar()->setFont(Theme::labelFont());
    // The font just changed, so both chrome strips just changed height - and
    // that is exactly what moves the viewport's edges onto a fractional device
    // row. See syncChromeHeights().
    syncChromeHeights();

    // The OCCT side of the bridge: a clear colour, two highlight drawers and
    // a grid whose colours are baked into its vertices. None of it is painted
    // by Qt, so none of it is reached by a repaint.
    if (myView) myView->applyTheme();

    // The live sketch markers are AIS objects coloured when they were built.
    // Re-issued from the sketch this window owns rather than from a copy the
    // viewport would have to keep - and only while there is a sketch, so this
    // cannot make a marker appear.
    if (mySketching && myView) {
        if (!mySketch.points().empty()) myView->setSketchPointMarkers(mySketch.points());
    }

    persistAppearance();

    emit themeChanged();

    // Last, and it is what actually repaints the shell: every widget in it
    // reads its colours from Theme inside paintEvent(), and appStateChanged()
    // - which updateActions() ends by emitting - is the signal they already
    // refresh on. No second refresh path.
    updateActions();
    if (myOverlay) myOverlay->relayout();
}

void MainWindow::persistAppearance()
{
    if (!myPersistProgress) return;

    // Built on first use rather than in the constructor: a window that never
    // sees a theme edit never creates one, and this is the only place that
    // can say whether the guard above let us get this far.
    if (!myAppearanceWrite) {
        myAppearanceWrite = new QTimer(this);
        myAppearanceWrite->setSingleShot(true);
        myAppearanceWrite->setInterval(kAppearanceWriteMs);
        connect(myAppearanceWrite, &QTimer::timeout, this,
                &MainWindow::writeAppearanceNow);
    }
    // start() on a running single-shot timer RESTARTS it, which is the whole
    // debounce: a drag through the colour wheel keeps pushing the deadline
    // out and lands exactly one write once the user stops.
    myAppearanceWrite->start();
}

void MainWindow::syncChromeHeights()
{
    // The viewport's top and bottom edges ARE the app bar's bottom edge and
    // the status bar's top edge, and both have to land on a whole device row.
    //
    // Widget geometry is logical; the surface OCCT paints into is sized in
    // device pixels. A chrome strip whose logical height does not multiply up
    // to a whole number of device rows leaves the seam between it and the
    // viewport on a fraction - Qt flushes the row, neither side's painter
    // reaches it, and over the GL surface an unpainted row is not transparent
    // but whatever the driver left, which measures as an exact 0,0,0 line.
    // Measured at 175% with an edited type scale: a 2068-device-pixel black
    // line the full width of the window, exactly where the status bar meets
    // the viewport. It is the floating-card rule (Theme::wholeDevicePixels,
    // see Theme.h) applied to the two cards that span the window, and neither
    // paintSurface() nor anything else either widget paints can reach a row
    // that is inside NEITHER widget's logical rect.
    //
    // It only appeared once the Appearance panel shipped because the default
    // type scale happens to give both strips a whole height. The base size is
    // a number the user edits now, so "happens to" stopped being a rule.
    //
    // The constraints are lifted before the hint is read, so this is
    // idempotent whatever a strip's sizeHint() does with its own fixed size:
    // re-running it can never ratchet a strip taller.
    auto whole = [](QWidget* strip) {
        if (!strip) return;
        strip->setMinimumHeight(0);
        strip->setMaximumHeight(QWIDGETSIZE_MAX);
        strip->setFixedHeight(Theme::wholeDevicePixels(strip->sizeHint().height()));
    };
    whole(menuWidget());
    whole(statusBar());
}

void MainWindow::writeAppearanceNow()
{
    // The ONE place the spec reaches QSettings. Two routes want it - the
    // debounce timer's timeout and the flush in closeEvent() - and they used
    // to carry a copy of the write each, which is two places to keep in step
    // with the key name and with whatever else a stored appearance ever needs
    // to include.
    QSettings settings;
    settings.setValue(QStringLiteral("appearance"), Theme::serializeSpec());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // A window closed inside the debounce window still has to store what the
    // user chose. Fired by hand rather than left to the timer, which is about
    // to be destroyed with this window.
    if (myAppearanceWrite && myAppearanceWrite->isActive()) {
        myAppearanceWrite->stop();
        writeAppearanceNow();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::recordProgress(const std::string& event)
{
    myProgress.record(event);
    if (!myPersistProgress) return;

    QSettings settings;
    settings.setValue(QStringLiteral("progress"),
                      QString::fromStdString(myProgress.serialize()));
}

QString MainWindow::lockTooltipText() const
{
    return tr("Draw on the selected face instead of the ground (L)\n"
              "Double-clicking a face does the same. Outlines drawn "
              "there extrude square to it.");
}

QString MainWindow::unlockTooltipText() const
{
    return tr("Go back to drawing on the ground (Shift+L)");
}

QString MainWindow::snapTooltipText() const
{
    return tr("Snap outline points to the %1 grid\n"
              "Turn this off for freehand placement.")
        .arg(QString::fromStdString(Measure::formatLength(10.0)));
}

void MainWindow::updateStateLabel()
{
    if (!myStateLabel) return;

    QString state;
    if (mySketching) {
        const int placed = static_cast<int>(mySketch.pointCount());
        if (mySketch.canClose()) {
            state = tr("Sketching — %1 points — Enter or click the first point to close")
                        .arg(placed);
        } else if (placed == 0) {
            state = tr("Sketching — click to place your first point");
        } else if (placed == 1) {
            state = tr("Sketching — 1 point, 2 more to close");
        } else {
            state = tr("Sketching — 2 points, 1 more to close");
        }
    } else if (!myPendingFace.IsNull()) {
        state = tr("Face ready — press E to extrude");
    } else if (canPullSelectedFace()) {
        // The gizmo is on screen and it is not obvious what to do with it -
        // an arrow with no words is a guess. Reads the same predicate the
        // arrow itself does, so the label cannot describe a gizmo that is not
        // there (or stay quiet about one that is).
        state = tr("Face selected — drag the arrow to pull, or type a distance");
    } else if (canBevelSelectedEdge()) {
        // Same rule as the line above: the gizmo is on screen, one axis does
        // two different things, and an arrow cannot say that by itself. Reads
        // the same predicate the arrow does.
        // The table's words, the same two the chip, the tooltips and the
        // refusals use. Saying "round or flatten" here and "Fillet"/"Chamfer"
        // everywhere else is two vocabularies for one pair of operations.
        state = tr("Edge selected — drag in for a Fillet, out for a Chamfer, "
                   "or type a size");
    } else {
        const std::size_t selected = myView->selectedSolidIds().size();
        const std::size_t bodies = myDocument.count();
        if (selected == 2) {
            state = tr("2 bodies selected — Union, Subtract and Intersect available");
        } else if (selected == 1) {
            // The transform gizmo is on screen whenever this holds, and a
            // handful of arrows and rings with no words is a guess. Reads the
            // same predicate the gizmo itself does, so the label cannot
            // describe a gizmo that is not there - or stay quiet about one
            // that is.
            state = canTransformSelectedBody()
                        ? tr("1 body selected — drag a handle to Move, Rotate or Scale — "
                             "Shift-click another to combine them")
                        : tr("1 body selected — Shift-click another to combine them");
        } else if (bodies == 0) {
            state = tr("Nothing yet — press Ctrl+K to draw an outline");
        } else if (bodies == 1) {
            state = tr("1 body — click it to select");
        } else {
            state = tr("%1 bodies — click one to select").arg(bodies);
        }
    }
    // A lock is a mode, and a mode with no persistent cue is a trap: the
    // message that announced it is transient, and the grid's orientation is
    // easy to misread once the camera has moved. It leads the label, because
    // where the next outline will land governs how to read everything after
    // it.
    if (myFaceLocked) state = tr("On a locked face — %1").arg(state);

    myStateLabel->setText(state);
}

void MainWindow::resyncView()
{
    myView->clearSolids();
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        myView->displaySolid(solid.id, solid.shape);
    }
}

void MainWindow::onDeleteSelected()
{
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.empty()) return;

    const std::string deletedName = ids.size() == 1 ? myDocument.nameOf(ids.front())
                                                    : std::string();

    myDocument.checkpoint();
    myView->clearSelection();
    for (int id : ids) {
        myDocument.removeSolid(id);
        myView->removeSolid(id);
    }
    recordProgress("delete.used");

    updateActions();
    emit documentChanged();
    const QString message =
        ids.size() == 1 ? tr("Deleted %1").arg(QString::fromStdString(deletedName))
                        : tr("Deleted %1 bodies").arg(ids.size());
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
}

void MainWindow::onUndo()
{
    // Mid-sketch, Undo means the last POINT. One implementation with two
    // triggers, not a second remove-last-point path: Backspace and Ctrl+Z
    // both land in onUndoSketchPoint(), so the two can never drift.
    //
    // Rerouting rather than adding a branch to updateActions() alone: the
    // enabled state (mySketching ? points > 0 : canUndo()) and the behaviour
    // have to agree, and updateActions() stays the single place that decides
    // availability. Everything that goes through myUndoAction follows for
    // free - the menu entry, the rail chip, Ctrl+Z, and the toast's Undo
    // pill, which triggers the action rather than calling this.
    if (mySketching) {
        onUndoSketchPoint();
        return;
    }

    if (!myDocument.undo()) return;
    recordProgress("undo.used");

    myView->clearSelection();
    resyncView();
    updateActions();
    emit documentChanged();
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Undone — 1 body in the document")
                                 : tr("Undone — %1 bodies in the document").arg(myDocument.count()));
}

void MainWindow::onRedo()
{
    if (!myDocument.redo()) return;
    recordProgress("undo.used");

    myView->clearSelection();
    resyncView();
    updateActions();
    emit documentChanged();
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Redone — 1 body in the document")
                                 : tr("Redone — %1 bodies in the document").arg(myDocument.count()));
}

void MainWindow::onSnapToggled(bool enabled)
{
    myView->setSnap(enabled, 10.0);
    statusBar()->showMessage(
        enabled ? tr("Snapping to the %1 grid")
                      .arg(QString::fromStdString(Measure::formatLength(10.0)))
                : tr("Snapping off — points land exactly where you click"));
}

void MainWindow::onSketchCursorMoved(const gp_Pnt& point)
{
    if (!mySketching) return;

    myView->setPreview(mySketch.previewShapeWithCursor(point));
    // The live snapped cursor dot - see setSketchCursorMarker()'s comment
    // on why Snap to Grid is exactly when this matters most.
    myView->setSketchCursorMarker(point);
    // The plane's OWN coordinates, not the world's. On a face locked at
    // y = 220 the world Y never changes as the cursor runs up the face, so a
    // world X/Y readout froze one number and made the other meaningless in
    // the plane the user is actually drawing in. ElSLib::Parameters is the
    // same conversion SketchController::snapToPlaneGrid uses, so the readout
    // and the snap grid agree by construction rather than by coincidence -
    // and on the ground plane (u, v) is (X, Y), so nothing changes there.
    Standard_Real u = 0.0, v = 0.0;
    ElSLib::Parameters(mySketch.plane(), point, u, v);
    statusBar()->showMessage(tr("Cursor at %1, %2")
                                 .arg(QString::fromStdString(Measure::formatLength(u)),
                                      QString::fromStdString(Measure::formatLength(v))));

    // The live length of the segment being dragged out - the last placed
    // point to the cursor. Only one call site touches this in
    // OcctViewWidget's own hover branch too (a selected edge); this is the
    // other of the two, per DimensionRenderer's contract.
    if (mySketch.points().empty()) {
        myView->dimension().clear();
        return;
    }
    const gp_Pnt& last = mySketch.points().back();
    const gp_Vec segment(last, point);
    if (segment.Magnitude() < 1.0e-4) {
        myView->dimension().clear();
        return;
    }
    // Sideways within the sketch plane - perpendicular to both the segment
    // and the plane's own normal - so the extension lines lie flat on the
    // plane the user is actually drawing on.
    gp_Vec sideways = gp_Vec(mySketch.plane().Axis().Direction()).Crossed(segment);
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(1.0, 0.0, 0.0);
    myView->dimension().show(last, point, gp_Dir(sideways), myView->worldPerPixel());
}

void MainWindow::onStartSketch()
{
    mySketch.reset();
    myPendingFace.Nullify();
    mySketching = true;

    // The ground plane by default, a locked face's own plane while one is
    // locked - SketchController holds the single copy of it either way.
    myView->setSketchMode(true, mySketch.plane());
    myView->setPreview(TopoDS_Shape());
    updateActions();
    statusBar()->showMessage(
        myFaceLocked
            ? tr("Click points on the locked face to draw an outline — "
                 "Enter closes it, Backspace undoes a point, Esc cancels")
            : tr("Click points on the ground to draw an outline — "
                 "Enter closes it, Backspace undoes a point, Esc cancels"));
}

void MainWindow::onSketchPointPicked(const gp_Pnt& point)
{
    // Clicking the first point again closes the sketch, the way every CAD
    // tool behaves. The radius comes from the viewport rather than being
    // recomputed here: Shift's straight constraint has to stand down inside
    // exactly this distance (see OcctViewWidget::sketchCloseTolerance), and
    // two copies of the formula would be two answers to the same question.
    const double closeTolerance = myView->sketchCloseTolerance();
    if (mySketch.isNearFirstPoint(point, closeTolerance)) {
        onFinishSketch();
        return;
    }

    mySketch.addPoint(point);
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    syncSketchConstraints();
    updateActions();
    statusBar()->showMessage(
        mySketch.pointCount() == 1
            ? tr("1 point placed")
            : tr("%1 points placed").arg(mySketch.pointCount()));
}

void MainWindow::onUndoSketchPoint()
{
    if (mySketch.pointCount() == 0) return;

    // The SAME counter the document path records, and recorded here rather
    // than in onUndo()'s reroute so Backspace earns it too. "Undo" is one
    // thing the user learns, not two: somebody who has taken back three
    // points by whichever key has learned to take things back, and a hint
    // still teaching them that would be teaching a lesson already taken.
    recordProgress("undo.used");
    mySketch.removeLastPoint();
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    syncSketchConstraints();
    updateActions();
}

void MainWindow::syncSketchConstraints()
{
    gp_Dir dir;
    if (mySketching && mySketch.lastSegmentDirection(dir))
        myView->setSketchStraightAnchor(mySketch.points().back(), dir);
    else
        myView->clearSketchStraightAnchor();

    // Only while clicking the first point would actually close the outline -
    // the same canClose() rule isNearFirstPoint() carries, read from the same
    // sketch, so the exemption cannot outlive the thing it exempts.
    if (mySketching && mySketch.canClose())
        myView->setSketchCloseTarget(mySketch.points().front());
    else
        myView->clearSketchCloseTarget();
}

void MainWindow::onCancelSketch()
{
    mySketching = false;
    mySketch.reset();
    myPendingFace.Nullify();
    myView->setSketchMode(false, mySketch.plane());
    myView->clearPreview();
    updateActions();
    statusBar()->showMessage(tr("Sketch cancelled"));
}

void MainWindow::onFinishSketch()
{
    const TopoDS_Face face = mySketch.closedFace();
    if (face.IsNull()) {
        myToasts->show(tr("This outline can't close into a flat face. It probably "
                          "crosses itself — Press Backspace to undo the last point and "
                          "redraw it, or Esc to start over"),
                      Toast::Kind::Failure, false);
        return;
    }

    myPendingFace = face;
    recordProgress("sketch.completed");
    mySketching = false;
    myView->setSketchMode(false, mySketch.plane());
    myView->setPreview(face, /*shaded=*/true);
    updateActions();
    statusBar()->showMessage(tr("Outline closed — press E to extrude it into a body"));
}

void MainWindow::onExtrude()
{
    if (myPendingFace.IsNull()) return;

    // Opens a live preview over the viewport instead of a modal dialog - see
    // ExtrudePreview. It calls extrudePendingFace() itself once the user
    // commits (Enter) or leaves the pending face alone if they back out
    // (Escape).
    myExtrudePreview->begin(myPendingFace);
}

bool MainWindow::extrudePendingFace(double height)
{
    if (myPendingFace.IsNull() || height == 0.0) return false;

    const TopoDS_Shape solid =
        ModelingOps::extrude(myPendingFace, mySketch.plane().Axis().Direction(), height);
    if (solid.IsNull()) {
        myToasts->show(tr("This face couldn't be extruded into a body — "
                          "The outline may cross itself or be too small to have an "
                          "inside. Try redrawing it with Ctrl+K"),
                      Toast::Kind::Failure, false);
        return false;
    }

    // Frame the very first solid; after that leave the camera where the user
    // put it rather than yanking the view on every extrude.
    const bool wasEmpty = myDocument.count() == 0;
    myDocument.checkpoint();
    const int id = myDocument.addSolid(solid);
    recordProgress("extrude.completed");
    myView->clearPreview();
    myView->displaySolid(id, solid);
    if (wasEmpty) myView->fitAll();

    myPendingFace.Nullify();
    mySketch.reset();
    updateActions();
    emit documentChanged();
    const QString message = tr("%1 created — %2")
                                .arg(QString::fromStdString(myDocument.nameOf(id)),
                                     QString::fromStdString(Measure::formatDimensions(solid)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

bool MainWindow::canPullSelectedFace() const
{
    // No sketch in progress, and no closed outline waiting - see
    // canChangeSketchPlane() and the header for both halves. The pending-face
    // half is what keeps this and ExtrudePreview mutually exclusive.
    if (mySketching || !myPendingFace.IsNull()) return false;

    // selectedFace() is deliberately "the ONE selected face", never the first
    // of several, so this cannot be a coin toss between two highlighted
    // faces. It is null outside face-selection mode, which is what makes the
    // mode check implicit rather than a second condition to keep in step.
    const TopoDS_Face face = myView->selectedFace();
    if (face.IsNull()) return false;
    return BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane;
}

int MainWindow::bodyIdForFace(const TopoDS_Face& face) const
{
    if (face.IsNull()) return 0;
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        for (TopExp_Explorer it(solid.shape, TopAbs_FACE); it.More(); it.Next()) {
            if (it.Current().IsSame(face)) return solid.id;
        }
    }
    return 0;
}

bool MainWindow::pullFaceBy(const TopoDS_Face& face, double distance)
{
    if (face.IsNull() || distance == 0.0) return false;

    const int id = bodyIdForFace(face);
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    const ModelingOps::BooleanResult result = ModelingOps::pullFace(body, face, distance);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never
        // show its error text: it is written for this file, not for the user.
        qWarning("Pull failed: %s", result.error.c_str());
        myToasts->show(tr("This face can't be pulled that far — a carve deeper than the "
                          "body removes the whole thing, and the geometry engine has "
                          "nothing left to build. Try a smaller distance, or drag the "
                          "arrow the other way"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("Pull refused — nothing was changed"));
        return false;
    }

    myDocument.checkpoint();
    myDocument.replaceSolid(id, result.shape);
    // The preview and the arrow both describe the face that is about to stop
    // existing; the selection holds that face too. All three go before the
    // body is redisplayed, in that order, so nothing is left pointing at
    // topology from before the rebuild.
    myView->clearModelingPreview();
    myView->clearPullArrow();
    myView->clearSelection();
    myView->displaySolid(id, result.shape);
    recordProgress("pull.completed");

    updateActions();
    emit documentChanged();
    const QString message =
        tr("%1 pulled — %2")
            .arg(QString::fromStdString(myDocument.nameOf(id)),
                 QString::fromStdString(Measure::formatDimensions(result.shape)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

int MainWindow::bodyIdForEdge(const TopoDS_Edge& edge) const
{
    if (edge.IsNull()) return 0;
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        for (TopExp_Explorer it(solid.shape, TopAbs_EDGE); it.More(); it.Next()) {
            if (it.Current().IsSame(edge)) return solid.id;
        }
    }
    return 0;
}

bool MainWindow::bevelTarget(TopoDS_Edge& edge, int& bodyId, gp_Pnt& centre,
                             gp_Dir& outward) const
{
    // The same two halves canPullSelectedFace() opens with, for the same
    // reasons - see its comment and the header.
    if (mySketching || !myPendingFace.IsNull()) return false;

    // Edge mode explicitly, so this cannot be true at the same time as the
    // face pull's predicate or the transform gizmo's.
    if (myView->selectionMode() != OcctViewWidget::SelectionMode::Edge) return false;

    // "The ONE selected edge", never the first of several - selectedEdge()'s
    // own rule, so a bevel can never be a coin toss between two highlighted
    // edges.
    const TopoDS_Edge selected = myView->selectedEdge();
    if (selected.IsNull()) return false;

    const int id = bodyIdForEdge(selected);
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    // Straightness, the two adjacent faces and the outward bisector are all
    // ModelingOps::bevelAxis()'s to decide, and it decides them once for the
    // predicate and the gizmo both.
    gp_Pnt at;
    gp_Dir axis;
    if (!ModelingOps::bevelAxis(body, selected, at, axis)) return false;

    edge = selected;
    bodyId = id;
    centre = at;
    outward = axis;
    return true;
}

bool MainWindow::canBevelSelectedEdge() const
{
    TopoDS_Edge edge;
    int bodyId = 0;
    gp_Pnt centre;
    gp_Dir outward;
    return bevelTarget(edge, bodyId, centre, outward);
}

QString MainWindow::bevelRefusalText(bool fillet)
{
    // No trailing period: the app's failure sentences end without one (see the
    // pull's and the transform's), and this pair was the exception.
    return fillet ? tr("This edge can't take a fillet that big — the curve "
                       "would eat a neighbouring face. Try a smaller size")
                  : tr("This edge can't take a chamfer that big — the flat "
                       "would eat a neighbouring face. Try a smaller size");
}

QString MainWindow::transformOperationName(const gp_Trsf& delta)
{
    return transformIsScale(delta)      ? tr("Scale")
           : transformIsRotation(delta) ? tr("Rotate")
                                        : tr("Move");
}

QString MainWindow::transformPastVerb(const gp_Trsf& delta)
{
    return transformIsScale(delta)      ? tr("scaled")
           : transformIsRotation(delta) ? tr("rotated")
                                        : tr("moved");
}

QString MainWindow::transformRefusalText(const gp_Trsf& delta)
{
    // "refused" would carry `fuse` as a substring, and the banned-word sweep
    // matches bare substrings case-insensitively (CLAUDE.md says so). The
    // sentence this replaced said "the geometry engine refused the change" and
    // sailed through every run only because nothing ever triggered it - which
    // is exactly why the suite now shows this copy through a probe.
    return tr("This body couldn't be %1 — the geometry engine turned that "
              "change down. Try a smaller drag, or a different handle")
        .arg(transformPastVerb(delta));
}

bool MainWindow::transformIsScale(const gp_Trsf& delta)
{
    return std::fabs(delta.ScaleFactor() - 1.0) > 1.0e-9;
}

bool MainWindow::transformIsRotation(const gp_Trsf& delta)
{
    gp_Vec axis;
    Standard_Real angle = 0.0;
    delta.GetRotation().GetVectorAndAngle(axis, angle);
    return std::fabs(angle) > 1.0e-9;
}

bool MainWindow::bevelEdgeBy(const TopoDS_Edge& edge, double size, bool fillet)
{
    if (edge.IsNull() || size <= 0.0) return false;

    const int id = bodyIdForEdge(edge);
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    const ModelingOps::BooleanResult result =
        fillet ? ModelingOps::filletEdge(body, edge, size)
               : ModelingOps::chamferEdge(body, edge, size);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never show
        // its error text: it is written for this file, not for the user. A
        // fillet failing on hard geometry is normal, not exceptional - see
        // ModelingOps::filletEdge - so the sentence names the cause and the fix
        // rather than apologising.
        qWarning("Bevel failed: %s", result.error.c_str());
        myToasts->show(bevelRefusalText(fillet), Toast::Kind::Failure, false);
        statusBar()->showMessage(fillet ? tr("Fillet refused — nothing was changed")
                                        : tr("Chamfer refused — nothing was changed"));
        return false;
    }

    myDocument.checkpoint();
    myDocument.replaceSolid(id, result.shape);
    // The preview, the arrow and the selection all describe the edge that is
    // about to stop existing. All three go before the body is redisplayed, in
    // that order, so nothing is left pointing at topology from before the
    // rebuild - the face pull's rule, one gizmo over.
    myView->clearModelingPreview();
    myView->clearBevelArrow();
    myView->clearSelection();
    myView->displaySolid(id, result.shape);
    recordProgress("bevel.completed");

    updateActions();
    emit documentChanged();
    // Led by the operation's own name. "Body 03 rounded" describes the result
    // in a word that appears nowhere else in the app - the chip, the tooltips,
    // the state label and the refusal all say Fillet or Chamfer.
    const QString message =
        (fillet ? tr("Fillet added to %1 — %2") : tr("Chamfer added to %1 — %2"))
            .arg(QString::fromStdString(myDocument.nameOf(id)),
                 QString::fromStdString(Measure::formatDimensions(result.shape)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

int MainWindow::transformableBodyId() const
{
    // The same two halves canPullSelectedFace() opens with, for the same
    // reasons: an outline in progress lives on a plane, and a body that moved
    // under it would take the plane's meaning with it.
    if (mySketching || !myPendingFace.IsNull()) return 0;

    // Body mode explicitly. selectedSolidIds() reports the owning body of a
    // selected FACE too, so without this the gizmo would appear over a face
    // selection and fight the pull arrow for the same drag.
    if (myView->selectionMode() != OcctViewWidget::SelectionMode::Solid) return 0;

    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 1) return 0;
    return ids.front();
}

void MainWindow::refreshTransformGizmo()
{
    const int id = transformableBodyId();
    if (id > 0) myView->attachManipulator(id);
    else        myView->detachManipulator();
}

void MainWindow::refreshEdgeAnnotation()
{
    // Derived from the arrow's own predicate, not from the arrow's visibility
    // and not from the event that happened to raise it - the rule this file
    // keeps for every other surface over the viewport.
    myView->setEdgeDimensionSuppressed(canBevelSelectedEdge());
}

void MainWindow::onGizmoReleased(int solidId, const gp_Trsf& delta)
{
    // A drag that nets nothing is a cancel, not an edit: no checkpoint, no
    // toast, no revision. It reaches here for two reasons that look identical
    // from the document's side - a press and release at the same point, and a
    // real drag the snap rounded back to where it started - and both deserve
    // the same silence. The viewport already restored its own presentation
    // before emitting, so there is nothing to put back.
    if (ModelingOps::isIdentityTransform(delta)) return;
    transformBody(solidId, delta);
}

bool MainWindow::transformBody(int id, const gp_Trsf& delta)
{
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    // This layer's clamp, not the kernel's: transformShape refuses only a
    // factor <= 0, and a body scaled to 1e-9 is not an error the kernel can
    // see - it is a body the user has lost. See kMinScale/kMaxScale.
    // INCLUSIVE on both ends, and that is the whole point: a shrink dragged to
    // the floor snaps to exactly kMinScale with Snap on and lands fractionally
    // below it with Snap off, so an exclusive test (`< kMinScale`) let the
    // SAME gesture commit or be refused depending on a toggle that is supposed
    // to change where a drag lands, not whether it is allowed at all.
    const double scale = delta.ScaleFactor();

    // Which of the three this gesture is, read off the transform itself and
    // derived ONCE, ABOVE the two refusal branches. It used to be derived only
    // on the success path, so a rotate or a scale the kernel turned down was
    // announced as a failed Move - a refusal that names the wrong operation is
    // worse than one that names none, because the user goes looking for a move
    // they never made.
    const QString operation = transformOperationName(delta);

    if (scale <= kMinScale || scale >= kMaxScale) {
        myToasts->show(tr("That's too big a change of size to make at once — anything "
                          "under a twentieth or over twenty times leaves a body you "
                          "can't see or can't fit on screen. Drag the handle back "
                          "toward the body and scale it in smaller steps"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 refused — nothing was changed").arg(operation));
        return false;
    }

    const ModelingOps::BooleanResult result = ModelingOps::transformShape(body, delta);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never show
        // its error text - it is written for this file, not for the user.
        qWarning("Transform failed: %s", result.error.c_str());
        myToasts->show(transformRefusalText(delta), Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 refused — nothing was changed").arg(operation));
        return false;
    }

    myDocument.checkpoint();
    myDocument.replaceSolid(id, result.shape);
    myView->displaySolid(id, result.shape);
    // Selected again on purpose, unlike the face pull's clearSelection(): the
    // body is still the same body, and keeping it selected is what leaves the
    // gizmo standing on it for a second drag. displaySolid() detached the
    // gizmo along with the presentation it was holding; the updateActions()
    // below re-attaches it at the body's new position.
    myView->setSelectedSolids({id});
    recordProgress("transform.completed");

    updateActions();
    emit documentChanged();

    // The same derivation the refusals above use - one source for all three
    // outcomes, and it stays right if a gesture ever combines two of them.
    const QString message =
        tr("%1 %2 — %3")
            .arg(QString::fromStdString(myDocument.nameOf(id)),
                 transformPastVerb(delta),
                 QString::fromStdString(Measure::formatDimensions(result.shape)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onUnion()     { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Fuse)); }
void MainWindow::onSubtract()  { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Cut)); }
void MainWindow::onIntersect() { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Common)); }

void MainWindow::runBoolean(int kind)
{
    applyBooleanToSelection(kind);
}

bool MainWindow::applyBooleanToSelection(int kind)
{
    const QString operationName =
        kind == static_cast<int>(ModelingOps::BooleanKind::Fuse)   ? tr("Union")
        : kind == static_cast<int>(ModelingOps::BooleanKind::Cut)  ? tr("Subtract")
                                                                   : tr("Intersect");

    std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 2) {
        myToasts->show(tr("%1 needs exactly two bodies — "
                          "Click one body, then Shift-click another")
                          .arg(operationName),
                      Toast::Kind::Note, false);
        return false;
    }

    // Cut is not commutative. The lower document id is the base, so the result is
    // predictable rather than dependent on pick order, which AIS does not preserve.
    std::sort(ids.begin(), ids.end());
    const std::string nameA = myDocument.nameOf(ids[0]);
    const std::string nameB = myDocument.nameOf(ids[1]);
    const TopoDS_Shape a = myDocument.shapeOf(ids[0]);
    const TopoDS_Shape b = myDocument.shapeOf(ids[1]);
    if (a.IsNull() || b.IsNull()) return false;

    const ModelingOps::BooleanResult result =
        ModelingOps::applyBoolean(static_cast<ModelingOps::BooleanKind>(kind), a, b);

    if (!result.ok) {
        // Never present a failed boolean as a success. The engine's error text is
        // genuinely useful for debugging, so keep it in the log, not the toast.
        qWarning("%s failed: %s", qPrintable(operationName), result.error.c_str());
        myToasts->show(tr("%1 failed — The two bodies couldn't be combined — "
                          "This usually means they only touch at a single edge or "
                          "corner, which the geometry engine can't resolve. Move one "
                          "body so they overlap properly, then try again")
                          .arg(operationName),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 failed — nothing was changed").arg(operationName));
        return false;
    }

    myDocument.checkpoint();
    myView->clearSelection();
    for (int id : ids) {
        myDocument.removeSolid(id);
        myView->removeSolid(id);
    }

    const int id = myDocument.addSolid(result.shape);
    recordProgress("boolean.completed");
    myView->displaySolid(id, result.shape);

    updateActions();
    emit documentChanged();
    const QString message = tr("%1 — %2 and %3 → %4 — %5")
                                .arg(operationName,
                                     QString::fromStdString(nameA),
                                     QString::fromStdString(nameB),
                                     QString::fromStdString(myDocument.nameOf(id)),
                                     QString::fromStdString(
                                         Measure::formatDimensions(result.shape)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onExportStep()
{
    if (myDocument.count() == 0) return;

    const QString path = QFileDialog::getSaveFileName(this, tr("Export STEP"), QString(),
                                                      tr("STEP files (*.step *.stp)"));
    if (path.isEmpty()) return;

    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(myDocument.count());
    for (const DocumentModel::Solid& solid : myDocument.solids()) shapes.push_back(solid.shape);

    const ModelingOps::StepResult result =
        ModelingOps::exportStep(ModelingOps::makeCompound(shapes), path.toStdString());

    if (!result.ok) {
        qWarning("STEP export failed: %s", result.error.c_str());
        myToasts->show(tr("Export failed — Couldn't write the STEP file — "
                          "Check that the folder exists and isn't read-only, then "
                          "try a different location"),
                      Toast::Kind::Failure, false);
        return;
    }
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Exported 1 body to %1").arg(path)
                                 : tr("Exported %1 bodies to %2")
                                       .arg(myDocument.count()).arg(path));
}

void MainWindow::onSelectionModeChanged()
{
    const OcctViewWidget::SelectionMode mode =
        myFaceSelectAction->isChecked() ? OcctViewWidget::SelectionMode::Face
        : myEdgeSelectAction->isChecked() ? OcctViewWidget::SelectionMode::Edge
                                          : OcctViewWidget::SelectionMode::Solid;
    myView->setSelectionMode(mode);
    if (myFaceSelectAction->isChecked()) recordProgress("faceMode.used");
    statusBar()->showMessage(
        myFaceSelectAction->isChecked()
            ? tr("Face selection — hovering highlights one face at a time")
        : myEdgeSelectAction->isChecked()
            ? tr("Edge selection — hovering shows one edge's length at a time")
            : tr("Body selection — click whole bodies to combine them"));
    // Neither mySolidSelectAction nor myFaceSelectAction is touched by
    // updateActions() itself (their checked state is handled entirely by the
    // QActionGroup they belong to), so this cannot recurse back in here -
    // but without this call, HintBalloon::reconsider() only ever finds out
    // face selection was used the next time something unrelated happens to
    // fire appStateChanged, which left its hint lingering.
    updateActions();
}

bool MainWindow::canChangeSketchPlane()
{
    // A closed outline that has not been extruded yet still belongs to the
    // plane it was drawn on, and both the commit (extrudePendingFace) and the
    // live preview sweep it along whatever the sketch plane's normal happens
    // to be AT THAT MOMENT. Move the plane in between and the outline is swept
    // in a direction lying in its own plane: a body with no volume at all,
    // which BRepPrimAPI_MakePrism reports as done. ModelingOps::extrude now
    // refuses that sweep outright, so nothing degenerate can reach the
    // document either way - but a refusal the user meets only after pressing E
    // is not an explanation, so the plane simply does not move while an
    // outline is waiting.
    //
    // Discarding the pending outline instead was the alternative, and it is
    // worse: it throws away work the user did without being asked.
    if (myPendingFace.IsNull()) return true;

    myToasts->show(tr("There's an outline waiting to be extruded, and it belongs to the "
                      "plane it was drawn on. Press E to turn it into a body, or Ctrl+K "
                      "to start a new outline, before you change the face you draw on."),
                  Toast::Kind::Note, false);
    return false;
}

void MainWindow::onLockToFace()
{
    const TopoDS_Face face = myView->selectedFace();
    if (face.IsNull()) return;
    lockToFace(face);
}

bool MainWindow::lockToFace(const TopoDS_Face& face)
{
    if (face.IsNull()) return false;
    if (!canChangeSketchPlane()) return false;

    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        myToasts->show(tr("This face isn't flat, so it can't hold an outline. "
                          "Pick a flat face and try again."),
                      Toast::Kind::Failure, false);
        return false;
    }

    // By value, and the face is dropped here - see lockToFace()'s comment in
    // the header for why holding on to it would be a bug waiting for the
    // user's next boolean.
    //
    // DIRECTION CONVENTION: the stored plane's normal is the face's OUTWARD
    // normal, because extrude sweeps along it (see extrudePendingFace and
    // ExtrudePreview) and a shelf has to come out of the cabinet rather than
    // into it.
    //
    // BRepAdaptor_Surface carries the underlying geometry and its location
    // and nothing else - it never applies TopAbs_Orientation. On a plain
    // BRepPrimAPI_MakeBox three of the six faces are TopAbs_REVERSED, and
    // for those the surface normal points INTO the body. Locking one of them
    // without this flip sweeps the prism straight through the body it is
    // standing on. Reverse it here, once, so no consumer of the sketch plane
    // has to know any of this.
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        // Origin and in-plane X direction preserved, normal flipped: gp_Ax3's
        // (P, N, Vx) constructor keeps Vx as the X direction when it is
        // already perpendicular to N, which it is, so only the normal (and
        // with it the derived Y direction) changes. The grid is symmetric
        // about both, so nothing visible moves.
        plane = gp_Pln(gp_Ax3(plane.Location(), plane.Axis().Direction().Reversed(),
                              plane.Position().XDirection()));
    }
    mySketch.setPlane(plane);
    myFaceLocked = true;
    // One call sets both where clicks land and where the grid is drawn; they
    // are the same value inside the viewport, so they cannot disagree.
    myView->setWorkPlane(plane);
    // Only now, past every refusal above: a lock that was declined must leave
    // the camera exactly where it was, or the user is looking at a face they
    // are not going to be drawing on.
    flyOntoFace(face, plane);
    recordProgress("faceLock.used");

    updateActions();
    statusBar()->showMessage(tr("Locked to this face — outlines you draw now sit on it, "
                                "and extrude square to it"));
    return true;
}

void MainWindow::flyOntoFace(const TopoDS_Face& face, const gp_Pln& plane)
{
    // `plane` is the OUTWARD-oriented plane lockToFace() has already derived -
    // reversed where the face is TopAbs_REVERSED, which is three faces in six
    // on a plain box. Re-deriving it from the face here would be a second copy
    // of that rule, and the wrong half of it is what sends a prism through the
    // body it stands on (see lockToFace).
    Bnd_Box box;
    BRepBndLib::Add(face, box);
    if (box.IsVoid()) return;

    // frame() gives the target and a distance that fits it - the same framing
    // a double-click on a body uses, so a face-on look is no closer or further
    // than the app's one idea of "framed".
    //
    // The target is the BOUNDING BOX's centre, not the face's centre of mass.
    // The two coincide on anything symmetric and separate on an L-shaped or
    // tapered face, and the box centre is the right one here: the job is to
    // put the whole face on screen, which is a question about its extent.
    // Anything asserting where this lands must derive the box centre too -
    // comparing against a centre of mass would be measuring a different point
    // and calling the gap an error.
    CameraController scratch = myView->camera();
    scratch.frame(box, OcctViewWidget::kFovyDeg);
    // ...and then the direction, which frame() leaves alone.
    scratch.lookFrom(plane.Axis().Direction());

    // Ortho before the flight, for the same reason the gizmo sets it before
    // its own: applyCameraState() runs on the first animation frame, and a
    // look that only squares up once it lands would flash. The user's first
    // orbit hands it back.
    myView->camera().setTemporaryOrtho(true);
    myView->animateTo(scratch.state());
}

void MainWindow::unlockFace()
{
    if (!myFaceLocked) return;
    if (!canChangeSketchPlane()) return;

    const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
    mySketch.setPlane(ground);
    myFaceLocked = false;
    myView->setWorkPlane(ground);

    updateActions();
    statusBar()->showMessage(tr("Back to the ground — outlines are drawn flat again"));
}

void MainWindow::onSelectionChanged()
{
    updateActions();

    const std::size_t count = myView->selectedSolidIds().size();
    statusBar()->showMessage(count == 0   ? tr("Nothing selected")
                             : count == 1 ? tr("1 body selected")
                                          : tr("%1 bodies selected").arg(count));
}
