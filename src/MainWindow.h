#pragma once
// OCCT first (Handle() macro vs. Windows headers pulled in by Qt).
#include <TopoDS_Face.hxx>

#include <QMainWindow>

#include "DocumentModel.h"
#include "SketchController.h"

class OcctViewWidget;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onStartSketch();
    void onFinishSketch();
    void onUndoSketchPoint();
    void onCancelSketch();
    void onSketchPointPicked(const gp_Pnt& point);
    void onSketchCursorMoved(const gp_Pnt& point);
    void onSnapToggled(bool enabled);

    void onExtrude();
    void onFuse();
    void onCut();
    void onCommon();

    void onExportStep();
    void onSelectionModeChanged();
    void onSelectionChanged();

private:
    void buildActions();
    void buildMenusAndToolbar();
    void updateActions();
    // Persistent right-hand readout: what mode we are in and what is possible.
    void updateStateLabel();
    void runBoolean(int kind);   // ModelingOps::BooleanKind as int, to keep it out of the header

    OcctViewWidget* myView = nullptr;
    DocumentModel myDocument;
    SketchController mySketch;

    // Face produced by the last committed sketch, waiting to be extruded.
    TopoDS_Face myPendingFace;
    bool mySketching = false;

    QAction* myStartSketchAction = nullptr;
    QAction* myFinishSketchAction = nullptr;
    QAction* myUndoPointAction = nullptr;
    QAction* myCancelSketchAction = nullptr;
    QAction* myExtrudeAction = nullptr;
    QAction* myFuseAction = nullptr;
    QAction* myCutAction = nullptr;
    QAction* myCommonAction = nullptr;
    QAction* myExportStepAction = nullptr;
    QAction* mySolidSelectAction = nullptr;
    QAction* myFaceSelectAction = nullptr;
    QAction* mySnapAction = nullptr;

    class QLabel* myStateLabel = nullptr;
};
