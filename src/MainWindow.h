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

    // Operations, split from the dialogs that ask for their parameters. The GUI
    // smoke test drives these directly; a modal QInputDialog cannot be answered
    // from inside the same event loop that raised it.
    bool extrudePendingFace(double height);
    bool applyBooleanToSelection(int kind);   // ModelingOps::BooleanKind

    // Read-only state, for assertions.
    const DocumentModel& document() const { return myDocument; }
    const SketchController& sketch() const { return mySketch; }
    bool hasPendingFace() const { return !myPendingFace.IsNull(); }
    bool isSketching() const { return mySketching; }
    OcctViewWidget* view() const { return myView; }
    class ItemsPanel* itemsPanel() const { return myItemsPanel; }

signals:
    // DocumentModel is Qt-free by design, so the window announces its changes.
    void documentChanged();

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

    void onDeleteSelected();
    void onUndo();
    void onRedo();

    void onExportStep();
    void onSelectionModeChanged();
    void onSelectionChanged();

private:
    void buildActions();
    void buildMenus();
    void buildOverlay();
    void updateActions();
    // Persistent right-hand readout: what mode we are in and what is possible.
    void updateStateLabel();
    // Rebuilds the viewport from the document. Cheaper than tracking individual
    // differences, and the only way to be sure the two agree after undo/redo.
    void resyncView();
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
    QAction* myDeleteAction = nullptr;
    QAction* myUndoAction = nullptr;
    QAction* myRedoAction = nullptr;
    QAction* myItemsPanelAction = nullptr;
    QAction* myDisplayModeAction = nullptr;
    QAction* myFitAction = nullptr;
    QAction* myScreenshotAction = nullptr;

    class ViewportOverlay* myOverlay = nullptr;
    class QLabel* myStateLabel = nullptr;
    class ItemsPanel* myItemsPanel = nullptr;
};
