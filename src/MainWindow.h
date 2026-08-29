#pragma once
// OCCT first (Handle() macro vs. Windows headers pulled in by Qt).
#include <TopoDS_Face.hxx>

#include <QMainWindow>

#include "DocumentModel.h"
#include "Measure.h"
#include "SketchController.h"
#include "UserProgress.h"

class ExtrudePreview;
class OcctViewWidget;
class QAction;
class ToastHost;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool persistProgress = true);

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

    UserProgress& progress() { return myProgress; }
    const UserProgress& progress() const { return myProgress; }

    // Records an event and writes the store through immediately, so a crash
    // never costs the user their learning history.
    void recordProgress(const std::string& event);

    // Sets the unit the whole app reads and types in, persists it through the
    // same QSettings guard as the learning progress, and refreshes every
    // visible string via updateActions()/appStateChanged() - no separate
    // refresh path. Records nothing in UserProgress; this is a display
    // preference, not a learned capability.
    void setDisplayUnit(Measure::Unit unit);

signals:
    // DocumentModel is Qt-free by design, so the window announces its changes.
    void documentChanged();

    // Emitted after every change that affects what the user can do next.
    // Slots must only read state and update themselves - calling back into
    // updateActions() from here would recurse.
    void appStateChanged();

    // Emitted by Help -> Show tips again, immediately before the
    // appStateChanged() that follows it. Clearing the store is not enough on
    // its own to bring every teaching surface back: a surface that also
    // remembers what it has already shown *this session* would stay quiet
    // until a restart, which is precisely what Show tips again exists to
    // avoid. This lets each surface drop that session memory itself, without
    // MainWindow having to know any of them has one.
    void progressReset();

private slots:
    void onStartSketch();
    void onFinishSketch();
    void onUndoSketchPoint();
    void onCancelSketch();
    void onSketchPointPicked(const gp_Pnt& point);
    void onSketchCursorMoved(const gp_Pnt& point);
    void onSnapToggled(bool enabled);

    void onExtrude();
    void onUnion();
    void onSubtract();
    void onIntersect();

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
    // The grid-step length, through Measure, so the snap tooltip never goes
    // stale after a unit switch - refreshed from updateActions(), same as
    // updateStateLabel().
    QString snapTooltipText() const;
    // Rebuilds the viewport from the document. Cheaper than tracking individual
    // differences, and the only way to be sure the two agree after undo/redo.
    void resyncView();
    void runBoolean(int kind);   // ModelingOps::BooleanKind as int, to keep it out of the header
    // The one place "the camera was moved to a named direction" is recorded.
    // Every route to that - the four View menu entries and a click on the
    // axis gizmo - goes through here, so no route can record the event
    // without also emitting appStateChanged, which is what actually retires
    // the hint that teaches it.
    void recordViewChanged();

    OcctViewWidget* myView = nullptr;
    DocumentModel myDocument;
    SketchController mySketch;

    // Face produced by the last committed sketch, waiting to be extruded.
    TopoDS_Face myPendingFace;
    bool mySketching = false;

    UserProgress myProgress;
    bool myPersistProgress = true;

    QAction* myStartSketchAction = nullptr;
    QAction* myFinishSketchAction = nullptr;
    QAction* myUndoPointAction = nullptr;
    QAction* myCancelSketchAction = nullptr;
    QAction* myExtrudeAction = nullptr;
    QAction* myUnionAction = nullptr;
    QAction* mySubtractAction = nullptr;
    QAction* myIntersectAction = nullptr;
    QAction* myExportStepAction = nullptr;
    QAction* mySolidSelectAction = nullptr;
    QAction* myFaceSelectAction = nullptr;
    QAction* myEdgeSelectAction = nullptr;
    QAction* mySnapAction = nullptr;
    QAction* myDeleteAction = nullptr;
    QAction* myUndoAction = nullptr;
    QAction* myRedoAction = nullptr;
    QAction* myItemsPanelAction = nullptr;
    QAction* myDisplayModeAction = nullptr;
    QAction* myFitAction = nullptr;
    QAction* myScreenshotAction = nullptr;
    QAction* myShortcutsAction = nullptr;
    QAction* myUnitsMillimetresAction = nullptr;
    QAction* myUnitsCentimetresAction = nullptr;

    class ViewportOverlay* myOverlay = nullptr;
    class QLabel* myStateLabel = nullptr;
    class ItemsPanel* myItemsPanel = nullptr;
    class ShortcutSheet* myShortcutSheet = nullptr;
    ToastHost* myToasts = nullptr;
    ExtrudePreview* myExtrudePreview = nullptr;
};
