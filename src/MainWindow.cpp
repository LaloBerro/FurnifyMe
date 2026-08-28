#include "MainWindow.h"

#include "ModelingOps.h"
#include "OcctViewWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <vector>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    myView = new OcctViewWidget(this);
    setCentralWidget(myView);

    connect(myView, &OcctViewWidget::sketchPointPicked, this, &MainWindow::onSketchPointPicked);
    connect(myView, &OcctViewWidget::sketchCursorMoved, this, &MainWindow::onSketchCursorMoved);
    connect(myView, &OcctViewWidget::selectionChanged, this, &MainWindow::onSelectionChanged);

    buildActions();
    buildMenusAndToolbar();
    updateActions();

    // Permanent widget so it survives transient showMessage() calls: the left
    // side reports what just happened, the right side always says where you are.
    myStateLabel = new QLabel(this);
    statusBar()->addPermanentWidget(myStateLabel);
    updateStateLabel();

    setWindowTitle(tr("FurnifyMe"));
    resize(1280, 800);
    statusBar()->showMessage(tr("RMB drag orbits, MMB drag pans, wheel zooms."));
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

    myFuseAction = new QAction(tr("&Fuse"), this);
    connect(myFuseAction, &QAction::triggered, this, &MainWindow::onFuse);

    myCutAction = new QAction(tr("&Cut"), this);
    connect(myCutAction, &QAction::triggered, this, &MainWindow::onCut);

    myCommonAction = new QAction(tr("&Intersect"), this);
    connect(myCommonAction, &QAction::triggered, this, &MainWindow::onCommon);

    myExportStepAction = new QAction(tr("Export &STEP..."), this);
    myExportStepAction->setShortcut(QKeySequence::Save);
    connect(myExportStepAction, &QAction::triggered, this, &MainWindow::onExportStep);

    mySolidSelectAction = new QAction(tr("Select &Solids"), this);
    mySolidSelectAction->setCheckable(true);
    mySolidSelectAction->setChecked(true);
    myFaceSelectAction = new QAction(tr("Select F&aces"), this);
    myFaceSelectAction->setCheckable(true);

    myDeleteAction = new QAction(tr("&Delete Selected"), this);
    myDeleteAction->setShortcut(QKeySequence::Delete);
    myDeleteAction->setToolTip(tr("Remove the selected solids (Del)"));
    connect(myDeleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelected);

    myUndoAction = new QAction(tr("&Undo"), this);
    myUndoAction->setShortcut(QKeySequence::Undo);
    myUndoAction->setToolTip(tr("Undo the last solid change (Ctrl+Z)"));
    connect(myUndoAction, &QAction::triggered, this, &MainWindow::onUndo);

    myRedoAction = new QAction(tr("&Redo"), this);
    myRedoAction->setShortcut(QKeySequence::Redo);
    myRedoAction->setToolTip(tr("Redo the last undone change (Ctrl+Y)"));
    connect(myRedoAction, &QAction::triggered, this, &MainWindow::onRedo);

    mySnapAction = new QAction(tr("Snap to &Grid"), this);
    mySnapAction->setCheckable(true);
    mySnapAction->setChecked(true);
    mySnapAction->setToolTip(tr("Round sketch points to the 10mm grid"));
    connect(mySnapAction, &QAction::toggled, this, &MainWindow::onSnapToggled);

    myStartSketchAction->setToolTip(tr("Draw a closed outline on the XY plane (Ctrl+K)"));
    myFinishSketchAction->setToolTip(tr("Close the outline into a face - needs 3+ points (Enter)"));
    myExtrudeAction->setToolTip(tr("Turn the closed face into a solid (E)"));
    myFuseAction->setToolTip(tr("Union of two selected solids"));
    myCutAction->setToolTip(tr("Subtract the later solid from the earlier one"));
    myCommonAction->setToolTip(tr("Keep only the overlap of two selected solids"));

    auto* selectionGroup = new QActionGroup(this);
    selectionGroup->addAction(mySolidSelectAction);
    selectionGroup->addAction(myFaceSelectAction);
    selectionGroup->setExclusive(true);
    connect(mySolidSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
    connect(myFaceSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
}

void MainWindow::buildMenusAndToolbar()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(myExportStepAction);
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
    modelMenu->addAction(myFuseAction);
    modelMenu->addAction(myCutAction);
    modelMenu->addAction(myCommonAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Fit All"), QKeySequence(Qt::Key_F), myView, &OcctViewWidget::fitAll);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Axonometric"), QKeySequence(Qt::Key_0),
                        myView, &OcctViewWidget::setViewAxonometric);
    viewMenu->addAction(tr("&Top"), QKeySequence(Qt::Key_1), myView, &OcctViewWidget::setViewTop);
    viewMenu->addAction(tr("F&ront"), QKeySequence(Qt::Key_2), myView, &OcctViewWidget::setViewFront);
    viewMenu->addAction(tr("&Right"), QKeySequence(Qt::Key_3), myView, &OcctViewWidget::setViewRight);
    viewMenu->addSeparator();
    viewMenu->addAction(mySnapAction);
    viewMenu->addSeparator();
    viewMenu->addAction(mySolidSelectAction);
    viewMenu->addAction(myFaceSelectAction);

    QToolBar* bar = addToolBar(tr("Main"));
    bar->addAction(myUndoAction);
    bar->addAction(myRedoAction);
    bar->addSeparator();
    bar->addAction(myStartSketchAction);
    bar->addAction(myFinishSketchAction);
    bar->addSeparator();
    bar->addAction(myExtrudeAction);
    bar->addSeparator();
    bar->addAction(myFuseAction);
    bar->addAction(myCutAction);
    bar->addAction(myCommonAction);
    bar->addSeparator();
    bar->addAction(myDeleteAction);
    bar->addSeparator();
    bar->addAction(mySolidSelectAction);
    bar->addAction(myFaceSelectAction);
    bar->addSeparator();
    bar->addAction(mySnapAction);
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

    myFuseAction->setEnabled(booleanReady);
    myCutAction->setEnabled(booleanReady);
    myCommonAction->setEnabled(booleanReady);

    myExportStepAction->setEnabled(myDocument.count() > 0);
    myDeleteAction->setEnabled(!mySketching && selectedCount > 0);
    myUndoAction->setEnabled(!mySketching && myDocument.canUndo());
    myRedoAction->setEnabled(!mySketching && myDocument.canRedo());

    updateStateLabel();
}

void MainWindow::updateStateLabel()
{
    if (!myStateLabel) return;

    QString state;
    if (mySketching) {
        state = mySketch.canClose()
                    ? tr("Sketching - %1 points - Enter or click the start point to close")
                          .arg(mySketch.pointCount())
                    : tr("Sketching - %1 of 3 points needed").arg(mySketch.pointCount());
    } else if (!myPendingFace.IsNull()) {
        state = tr("Face ready - press E to extrude");
    } else {
        const std::size_t selected = myView->selectedSolidIds().size();
        const std::size_t solids = myDocument.count();
        if (selected == 2) {
            state = tr("2 solids selected - Fuse / Cut / Intersect available");
        } else if (selected == 1) {
            state = tr("1 solid selected - shift-click a second one for a boolean");
        } else if (solids == 0) {
            state = tr("Empty - start a sketch (Ctrl+K)");
        } else {
            state = tr("%1 solid(s) - click one to select").arg(solids);
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

    myDocument.checkpoint();
    myView->clearSelection();
    for (int id : ids) {
        myDocument.removeSolid(id);
        myView->removeSolid(id);
    }

    updateActions();
    statusBar()->showMessage(tr("Deleted %1 solid(s).").arg(ids.size()));
}

void MainWindow::onUndo()
{
    if (!myDocument.undo()) return;

    myView->clearSelection();
    resyncView();
    updateActions();
    statusBar()->showMessage(tr("Undone. %1 solid(s) in the document.").arg(myDocument.count()));
}

void MainWindow::onRedo()
{
    if (!myDocument.redo()) return;

    myView->clearSelection();
    resyncView();
    updateActions();
    statusBar()->showMessage(tr("Redone. %1 solid(s) in the document.").arg(myDocument.count()));
}

void MainWindow::onSnapToggled(bool enabled)
{
    myView->setSnap(enabled, 10.0);
    statusBar()->showMessage(enabled ? tr("Snapping to the 10mm grid.")
                                     : tr("Snapping off - points land exactly where you click."));
}

void MainWindow::onSketchCursorMoved(const gp_Pnt& point)
{
    if (!mySketching) return;

    myView->setPreview(mySketch.previewShapeWithCursor(point));
    statusBar()->showMessage(tr("Cursor: (%1, %2, %3)")
                                 .arg(point.X(), 0, 'f', 1)
                                 .arg(point.Y(), 0, 'f', 1)
                                 .arg(point.Z(), 0, 'f', 1));
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
    statusBar()->showMessage(tr("Sketch mode: click points on the XY plane. "
                                "Enter closes the wire, Backspace undoes, Esc cancels."));
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
    statusBar()->showMessage(tr("%1 point(s). Last: (%2, %3, %4)")
                                 .arg(mySketch.pointCount())
                                 .arg(point.X(), 0, 'f', 2)
                                 .arg(point.Y(), 0, 'f', 2)
                                 .arg(point.Z(), 0, 'f', 2));
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
    statusBar()->showMessage(tr("Sketch cancelled."));
}

void MainWindow::onFinishSketch()
{
    const TopoDS_Face face = mySketch.closedFace();
    if (face.IsNull()) {
        QMessageBox::warning(this, tr("Sketch"),
                             tr("Could not build a planar face from these points. "
                                "A closed, non-self-intersecting outline of at least "
                                "3 points is required."));
        return;
    }

    myPendingFace = face;
    mySketching = false;
    myView->setSketchMode(false, mySketch.plane());
    myView->setPreview(face, /*shaded=*/true);
    updateActions();
    statusBar()->showMessage(tr("Face closed. Extrude (E) to make it a solid."));
}

void MainWindow::onExtrude()
{
    if (myPendingFace.IsNull()) return;

    bool accepted = false;
    const double height = QInputDialog::getDouble(this, tr("Extrude"), tr("Height (mm):"),
                                                  10.0, -10000.0, 10000.0, 3, &accepted);
    if (!accepted || height == 0.0) return;

    const TopoDS_Shape solid =
        ModelingOps::extrude(myPendingFace, mySketch.plane().Axis().Direction(), height);
    if (solid.IsNull()) {
        QMessageBox::warning(this, tr("Extrude"), tr("The extrusion failed."));
        return;
    }

    // Frame the very first solid; after that leave the camera where the user
    // put it rather than yanking the view on every extrude.
    const bool wasEmpty = myDocument.count() == 0;
    myDocument.checkpoint();
    const int id = myDocument.addSolid(solid);
    myView->clearPreview();
    myView->displaySolid(id, solid);
    if (wasEmpty) myView->fitAll();

    myPendingFace.Nullify();
    mySketch.reset();
    updateActions();
    statusBar()->showMessage(tr("Solid #%1 created (volume %2 mm3).")
                                 .arg(id)
                                 .arg(ModelingOps::volume(solid), 0, 'f', 2));
}

void MainWindow::onFuse()   { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Fuse)); }
void MainWindow::onCut()    { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Cut)); }
void MainWindow::onCommon() { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Common)); }

void MainWindow::runBoolean(int kind)
{
    std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 2) {
        QMessageBox::information(this, tr("Boolean"),
                                 tr("Select exactly two solids (Shift-click to add)."));
        return;
    }

    // Cut is not commutative. The lower document id is the base, so the result is
    // predictable rather than dependent on pick order, which AIS does not preserve.
    std::sort(ids.begin(), ids.end());
    const TopoDS_Shape a = myDocument.shapeOf(ids[0]);
    const TopoDS_Shape b = myDocument.shapeOf(ids[1]);
    if (a.IsNull() || b.IsNull()) return;

    const ModelingOps::BooleanResult result =
        ModelingOps::applyBoolean(static_cast<ModelingOps::BooleanKind>(kind), a, b);

    if (!result.ok) {
        // Never present a failed boolean as a success.
        QMessageBox::critical(this, tr("Boolean failed"),
                              tr("OCCT could not complete the operation:\n\n%1\n\n"
                                 "Near-tangent geometry is the usual cause; adjusting the "
                                 "fuzzy value sometimes helps.")
                                  .arg(QString::fromStdString(result.error)));
        statusBar()->showMessage(tr("Boolean failed - model unchanged."));
        return;
    }

    myDocument.checkpoint();
    myView->clearSelection();
    for (int id : ids) {
        myDocument.removeSolid(id);
        myView->removeSolid(id);
    }

    const int id = myDocument.addSolid(result.shape);
    myView->displaySolid(id, result.shape);

    updateActions();
    statusBar()->showMessage(tr("Solid #%1 created from #%2 and #%3 (volume %4 mm3).")
                                 .arg(id).arg(ids[0]).arg(ids[1])
                                 .arg(ModelingOps::volume(result.shape), 0, 'f', 2));
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
        QMessageBox::critical(this, tr("Export failed"),
                              QString::fromStdString(result.error));
        return;
    }
    statusBar()->showMessage(tr("Exported %1 solid(s) to %2")
                                 .arg(myDocument.count()).arg(path));
}

void MainWindow::onSelectionModeChanged()
{
    myView->setSelectionMode(myFaceSelectAction->isChecked() ? OcctViewWidget::SelectionMode::Face
                                                             : OcctViewWidget::SelectionMode::Solid);
    statusBar()->showMessage(myFaceSelectAction->isChecked()
                                 ? tr("Face selection: hover highlights faces.")
                                 : tr("Solid selection: pick whole solids for booleans."));
}

void MainWindow::onSelectionChanged()
{
    updateActions();

    const std::size_t count = myView->selectedSolidIds().size();
    statusBar()->showMessage(count == 0
                                 ? tr("Nothing selected.")
                                 : tr("%1 solid(s) selected.").arg(count));
}
