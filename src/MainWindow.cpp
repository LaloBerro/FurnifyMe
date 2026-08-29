#include "MainWindow.h"

#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"

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

#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
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

    myItemsPanel = new ItemsPanel(&myDocument, myView, this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(myItemsPanel);
    splitter->addWidget(myView);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 1000});
    setCentralWidget(splitter);

    connect(myView, &OcctViewWidget::sketchPointPicked, this, &MainWindow::onSketchPointPicked);
    connect(myView, &OcctViewWidget::sketchCursorMoved, this, &MainWindow::onSketchCursorMoved);
    connect(myView, &OcctViewWidget::selectionChanged, this, &MainWindow::onSelectionChanged);

    buildActions();
    buildMenus();
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

    // Selection syncs both ways.
    connect(myItemsPanel, &ItemsPanel::solidActivated, this,
            [this](int id) { myView->setSelectedSolids({id}); });
    connect(myView, &OcctViewWidget::selectionChanged, this,
            [this] { myItemsPanel->showSelection(myView->selectedSolidIds()); });

    // The Items chip and menu entry collapse the panel.
    connect(myItemsPanelAction, &QAction::toggled, myItemsPanel, &QWidget::setVisible);

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

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(myExportStepAction);
    fileMenu->addAction(myScreenshotAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu* sketchMenu = menuBar()->addMenu(tr("&Sketch"));
    sketchMenu->addAction(myStartSketchAction);
    sketchMenu->addAction(myFinishSketchAction);
    sketchMenu->addAction(myUndoPointAction);
    sketchMenu->addAction(myCancelSketchAction);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(myUndoAction);
    editMenu->addAction(myRedoAction);
    editMenu->addSeparator();
    editMenu->addAction(myDeleteAction);

    QMenu* modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(myExtrudeAction);
    modelMenu->addSeparator();
    modelMenu->addAction(myUnionAction);
    modelMenu->addAction(mySubtractAction);
    modelMenu->addAction(myIntersectAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(myFitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Axonometric"), QKeySequence(Qt::Key_0), this, [this] {
        myView->setViewAxonometric();
        recordViewChanged();
    });
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

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));

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
}

void MainWindow::buildOverlay()
{
    myOverlay = new ViewportOverlay(myView);

    auto cluster = [this](ViewportOverlay::Anchor anchor,
                          std::initializer_list<std::pair<QAction*, IconSet::Glyph>> chips) {
        auto* group = new ToolCluster(myView);
        for (const auto& entry : chips) {
            group->addChip(new ToolChip(entry.first, entry.second));
        }
        myOverlay->addWidget(group, anchor);
    };

    cluster(ViewportOverlay::Anchor::LeftCenter, {
        {myStartSketchAction, IconSet::Glyph::Sketch},
        {myExtrudeAction,     IconSet::Glyph::Extrude},
        {myUnionAction,       IconSet::Glyph::Fuse},
        {mySubtractAction,    IconSet::Glyph::Cut},
        {myIntersectAction,   IconSet::Glyph::Intersect},
        {myDeleteAction,      IconSet::Glyph::Delete},
    });

    cluster(ViewportOverlay::Anchor::BottomLeft, {
        {mySnapAction,         IconSet::Glyph::Snap},
        {mySolidSelectAction,  IconSet::Glyph::SelectSolid},
        {myFaceSelectAction,   IconSet::Glyph::SelectFace},
        {myEdgeSelectAction,   IconSet::Glyph::SelectEdge},
    });

    cluster(ViewportOverlay::Anchor::TopLeft, {
        {myItemsPanelAction, IconSet::Glyph::Items},
        {myUndoAction,       IconSet::Glyph::Undo},
        {myRedoAction,       IconSet::Glyph::Redo},
    });

    cluster(ViewportOverlay::Anchor::RightCenter, {
        {myDisplayModeAction, IconSet::Glyph::DisplayMode},
        {myScreenshotAction,  IconSet::Glyph::Screenshot},
        {myFitAction,         IconSet::Glyph::Fit},
    });

    // The orientation gizmo, then the unit readout beneath it.
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

    // Unit readout under the axis gizmo - follows View -> Units rather than
    // stating a fixed unit. Refreshed from appStateChanged, same as every
    // other surface this setting reaches (see setDisplayUnit()).
    auto* units = new QLabel(QString::fromStdString(Measure::unitSuffix()), myView);
    units->setAlignment(Qt::AlignCenter);
    // A small chip-styled readout - Theme::labelFont(), the same size as a
    // chip label.
    units->setStyleSheet(QStringLiteral(
                             "background-color: %1; color: %2;"
                             "border-radius: 6px; padding: 6px 10px; font-size: %3pt;")
                             .arg(Theme::chip().name(), Theme::textMuted().name())
                             .arg(Theme::labelFont().pointSizeF()));
    units->adjustSize();
    connect(this, &MainWindow::appStateChanged, units, [units] {
        units->setText(QString::fromStdString(Measure::unitSuffix()));
        units->adjustSize();
    });
    myOverlay->addWidget(units, ViewportOverlay::Anchor::TopRight);

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
    statusBar()->showMessage(tr("Cursor at %1, %2")
                                 .arg(QString::fromStdString(Measure::formatLength(point.X())),
                                      QString::fromStdString(Measure::formatLength(point.Y()))));

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

    // Milestone 1 sketches on the fixed XY plane at Z=0.
    myView->setSketchMode(true, mySketch.plane());
    myView->setPreview(TopoDS_Shape());
    updateActions();
    statusBar()->showMessage(tr("Click points on the ground to draw an outline — "
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

void MainWindow::onSelectionChanged()
{
    updateActions();

    const std::size_t count = myView->selectedSolidIds().size();
    statusBar()->showMessage(count == 0   ? tr("Nothing selected")
                             : count == 1 ? tr("1 body selected")
                                          : tr("%1 bodies selected").arg(count));
}
