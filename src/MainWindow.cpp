#include "MainWindow.h"

#include "ModelingOps.h"
#include "OcctViewWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QInputDialog>
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
    connect(myView, &OcctViewWidget::selectionChanged, this, &MainWindow::onSelectionChanged);

    buildActions();
    buildMenusAndToolbar();
    updateActions();

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

    QMenu* modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(myExtrudeAction);
    modelMenu->addSeparator();
    modelMenu->addAction(myFuseAction);
    modelMenu->addAction(myCutAction);
    modelMenu->addAction(myCommonAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Fit All"), QKeySequence(Qt::Key_F), myView, &OcctViewWidget::fitAll);
    viewMenu->addAction(tr("&Axonometric"), myView, &OcctViewWidget::setViewAxonometric);
    viewMenu->addSeparator();
    viewMenu->addAction(mySolidSelectAction);
    viewMenu->addAction(myFaceSelectAction);

    QToolBar* bar = addToolBar(tr("Main"));
    bar->addAction(myStartSketchAction);
    bar->addAction(myFinishSketchAction);
    bar->addSeparator();
    bar->addAction(myExtrudeAction);
    bar->addSeparator();
    bar->addAction(myFuseAction);
    bar->addAction(myCutAction);
    bar->addAction(myCommonAction);
    bar->addSeparator();
    bar->addAction(mySolidSelectAction);
    bar->addAction(myFaceSelectAction);
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

    const int id = myDocument.addSolid(solid);
    myView->clearPreview();
    myView->displaySolid(id, solid);
    myView->fitAll();

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
