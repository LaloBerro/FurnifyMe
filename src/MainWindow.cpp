#include "MainWindow.h"

#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"

#include "AppBar.h"
#include "AxisGizmo.h"
#include "ExtrudePreview.h"
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "ShortcutSheet.h"
#include "Theme.h"
#include "Toast.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "ViewportOverlay.h"
#include "WalkthroughPanel.h"

#include <BRepAdaptor_Surface.hxx>
#include <ElSLib.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_Orientation.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QtGlobal>

#include <algorithm>
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
    }

    myView = new OcctViewWidget(this);
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
    myExtrudeAction->setToolTip(tr("Pull the face up into a body (E)\n"
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
    // The one way back to the angled view. Both entry points - the View menu
    // (and its 0 shortcut) and the app bar's view label button - call this,
    // so the pose and the recorded event cannot drift apart the way they
    // would if each site re-derived the camera state for itself.
    myView->setViewAxonometric();
    recordViewChanged();
}

void MainWindow::buildAppBar(QMenuBar* menus)
{
    myAppBar = new AppBar(menus, myDisplayModeAction, myFitAction);
    // The window takes ownership. Nothing may call menuBar() from here on.
    setMenuWidget(myAppBar);

    myAppBar->setViewLabel(myView->viewLabelText());
    myAppBar->setUnitLabel(QString::fromStdString(Measure::unitSuffix()));

    connect(myView, &OcctViewWidget::cameraChanged, myAppBar,
            [this] { myAppBar->setViewLabel(myView->viewLabelText()); });

    // Exactly what the gizmo's label chip did - and it is the View menu's
    // Axonometric entry, not a second copy of the pose it applies. The button
    // and the menu entry are the same route, so a user who only ever presses
    // this button still retires the hint that teaches named views.
    connect(myAppBar, &AppBar::viewLabelClicked, this, &MainWindow::goAxonometric);

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
    // Thirteen buttons give the rail a measured natural height of 524px
    // (13 x 34 of card, three 7px separators, 3px between each, 8px of card
    // padding top and bottom). Anchored 14px down, its bottom sits at 538, so
    // a viewport shorter than that starts clipping - Redo is the first
    // casualty, then Undo - and 552px is what keeps the bottom margin too.
    // Below the threshold the LeftEdge anchor deliberately keeps every button
    // its designed size and lets the last one run off the edge rather than
    // squeezing thirteen fixed-size buttons into twelve buttons' worth of
    // space, which Qt resolves by overlapping them. Every screen this app is
    // used on clears 552 comfortably - at 1200x800 the viewport is 743 and
    // the rail has ~190px of slack - but a fourteenth tool needs a real
    // answer, not another 37px.
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
        if (myUndoAction->isEnabled()) myUndoAction->trigger();
    });
    // A toast that offers to undo one operation must not survive that
    // operation - see ToastHost::documentMovedTo().
    connect(this, &MainWindow::documentChanged, this,
            [this] { myToasts->documentMovedTo(myDocument.revision()); });
    // A guide can appear UNDERNEATH a toast that is already up (Show tips
    // again does exactly that), and nothing told the toast to step aside
    // when it did - HintBalloon::reconsider() already handled that case for
    // itself. appStateChanged is when it happens; replace() only reads
    // geometry, so it cannot recurse back into updateActions().
    connect(this, &MainWindow::appStateChanged, myToasts, &ToastHost::replace);

    // Replaces the old QInputDialog::getDouble() for extrude height. Parents
    // itself to the viewport and positions itself (top-center, clear of the
    // toast/guide/balloon bottom strip - see ExtrudePreview::reposition()),
    // so it needs no overlay anchor of its own either.
    myExtrudePreview = new ExtrudePreview(this, myView);

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
    connect(myOverlay, &ViewportOverlay::laidOut, myToasts, &ToastHost::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, hints, &HintBalloon::reposition);
    connect(myOverlay, &ViewportOverlay::laidOut, myExtrudePreview, &ExtrudePreview::replace);
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
    const TopoDS_Face selectedFace = myView->selectedFace();
    // Not while sketching: the points already placed live on the plane that is
    // about to be swapped, and an outline with points on two planes is not an
    // outline. Not while a closed outline is waiting either - see
    // canChangeSketchPlane() for what moving the plane out from under it does.
    const bool planeCanMove = !mySketching && myPendingFace.IsNull();
    const bool flatFaceSelected =
        planeCanMove && !selectedFace.IsNull() &&
        BRepAdaptor_Surface(selectedFace).GetType() == GeomAbs_Plane;
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
    myUndoAction->setEnabled(!mySketching && myDocument.canUndo());
    // The toast's Undo pill is the same route as the action, so it obeys the
    // same enabled state - decided here, in the one place that decides what
    // is available, and pushed out rather than re-derived at the toast.
    if (myToasts) myToasts->setUndoEnabled(myUndoAction->isEnabled());
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
    } else {
        const std::size_t selected = myView->selectedSolidIds().size();
        const std::size_t bodies = myDocument.count();
        if (selected == 2) {
            state = tr("2 bodies selected — Union, Subtract and Intersect available");
        } else if (selected == 1) {
            state = tr("1 body selected — Shift-click another to combine them");
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
    // Clicking the first point again closes the sketch, the way every CAD tool
    // behaves. Half a grid step is a forgiving but unambiguous target.
    const double closeTolerance = myView->snapEnabled() ? myView->snapStep() * 0.5 : 5.0;
    if (mySketch.isNearFirstPoint(point, closeTolerance)) {
        onFinishSketch();
        return;
    }

    mySketch.addPoint(point);
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    updateActions();
    statusBar()->showMessage(
        mySketch.pointCount() == 1
            ? tr("1 point placed")
            : tr("%1 points placed").arg(mySketch.pointCount()));
}

void MainWindow::onUndoSketchPoint()
{
    mySketch.removeLastPoint();
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    updateActions();
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
    recordProgress("faceLock.used");

    updateActions();
    statusBar()->showMessage(tr("Locked to this face — outlines you draw now sit on it, "
                                "and extrude square to it"));
    return true;
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
