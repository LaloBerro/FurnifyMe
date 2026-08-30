#pragma once
// OCCT first (Handle() macro vs. Windows headers pulled in by Qt).
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>

#include <QMainWindow>

#include "DocumentModel.h"
#include "Measure.h"
#include "SketchController.h"
#include "UserProgress.h"

class AppBar;
class ExtrudePreview;
class OcctViewWidget;
class PullArrow;
class QAction;
class QMenuBar;
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

    // Makes `face`'s own plane the sketch plane, so the next outline is drawn
    // on the face and extrudes perpendicular to it. False - with a toast
    // naming the cause and the fix - when the face is not flat, or when a
    // closed outline is still waiting to be extruded on the plane this would
    // replace (see canChangeSketchPlane). Both refusals are checked here as
    // well as in the actions' enabled state, because the double-click route
    // never consults that.
    //
    // The plane is captured BY VALUE here and the face itself is not kept.
    // CLAUDE.md's topological-naming warning is the reason: face indices are
    // not stable across a rebuild, so a stored face (or a plane re-derived
    // from one later) would let a boolean or an undo move the sketch plane
    // under the user without a single visible event.
    // Pulls `face` by `distance` along its own outward normal - positive
    // grows, negative carves - through ModelingOps::pullFace, replacing the
    // body the face belongs to. The one commit path for the face-pull gizmo:
    // it takes the undo checkpoint, resyncs the viewport, records progress
    // and reports the outcome, so PullArrow never touches DocumentModel.
    //
    // False, with a Failure toast in cause-and-fix form and the body left
    // exactly as it was, whenever the kernel refuses - which it legitimately
    // does for a carve deeper than the body. The kernel's own error string is
    // logged, never shown: it is written for this file, not for the user.
    bool pullFaceBy(const TopoDS_Face& face, double distance);

    // Exactly one flat face selected, no sketch in progress, no outline
    // waiting. THE predicate behind the pull arrow, and the same one
    // updateActions() uses for Lock to Face and updateStateLabel() uses for
    // its teaching text - one function, so the gizmo, the enabled state and
    // the label can never disagree about whether a pull is possible.
    //
    // It refuses while a face is pending, which is exactly when
    // ExtrudePreview can be open: the two panels' application-wide
    // Enter/Escape claims are therefore mutually exclusive by construction
    // rather than by luck.
    bool canPullSelectedFace() const;

    // THE predicate behind the transform gizmo: the document id of the one
    // body it should be standing on, or 0. Exactly one body selected, in body
    // selection mode, with no sketch in progress and no outline waiting.
    //
    // The mode check is what keeps the three gizmos mutually exclusive BY
    // CONSTRUCTION rather than by three predicates that have to be kept in
    // step: face pull needs face mode, bevels need edge mode, and this needs
    // body mode, so no two of them can ever be true at once. The sketch and
    // pending-face halves are canPullSelectedFace()'s, for the same reasons
    // spelled out there.
    int transformableBodyId() const;
    bool canTransformSelectedBody() const { return transformableBodyId() > 0; }

    // Bakes `delta` into body `id` through ModelingOps::transformShape and
    // replaces it, with an undo checkpoint and a Note toast offering Undo -
    // the one commit path for the transform gizmo, so nothing else touches
    // DocumentModel on its behalf.
    //
    // False, with a Failure toast and the body untouched, when the kernel
    // refuses or when the scale factor falls outside kMinScale..kMaxScale.
    // That clamp is this layer's, deliberately: the kernel only refuses a
    // factor <= 0, and it will happily build a body 1e-9 of its size or a
    // thousand times it - both of which are a lost body rather than an edit.
    bool transformBody(int id, const gp_Trsf& delta);

    // The band a single scale gesture may land in. Below the first, a body is
    // gone from the viewport without looking deleted; above the second, it
    // swallows the scene. Both are recoverable by scaling again, which is why
    // this refuses the gesture rather than clamping the number - a clamp would
    // silently do something other than what the user dragged.
    static constexpr double kMinScale = 0.05;
    static constexpr double kMaxScale = 20.0;

    // The document id of the body `face` belongs to, or 0. Derived by walking
    // the document rather than remembered: face indices are not stable across
    // a rebuild (CLAUDE.md's topological-naming warning), so a cached
    // face-to-body mapping is a bug waiting for the user's next boolean.
    int bodyIdForFace(const TopoDS_Face& face) const;

    bool lockToFace(const TopoDS_Face& face);
    // Back to the ground plane. The ground plane is the default and is never
    // itself "locked", so this is not a toggle of the same state. Refused,
    // with the same toast, while an outline is pending - unlocking re-aims a
    // pending extrude exactly as locking does, only the other way.
    void unlockFace();
    bool isFaceLocked() const { return myFaceLocked; }

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
    void onLockToFace();
    // The end of a transform-gizmo drag. An identity delta is a cancel - the
    // user released where they started, or the snap rounded the whole gesture
    // away - and a cancel takes no checkpoint and says nothing. The viewport
    // has already put its presentation back by the time this runs (see
    // OcctViewWidget::endGizmoDrag), so there is nothing to undo here either.
    void onGizmoReleased(int solidId, const gp_Trsf& delta);

private:
    void buildActions();
    // Builds the menus on a QMenuBar this window owns from the start, and
    // hands it back for the app bar to adopt. Deliberately NOT
    // QMainWindow::menuBar(): once the app bar is the menu widget, that
    // accessor cannot find a QMenuBar in the slot and creates a fresh empty
    // one, whose setMenuBar() then deletes the bar. See AppBar.h.
    QMenuBar* buildMenus();
    // Installs the app bar as the window's menu strip, with `menus` inside
    // it. Must run after buildActions(), whose actions the bar mirrors.
    void buildAppBar(QMenuBar* menus);
    // Back to the angled view, and record it. The View menu's Axonometric
    // entry and the app bar's view label button are both this, so neither
    // carries its own copy of the pose.
    void goAxonometric();
    void buildOverlay();
    void updateActions();
    // Persistent right-hand readout: what mode we are in and what is possible.
    void updateStateLabel();
    // The grid-step length, through Measure, so the snap tooltip never goes
    // stale after a unit switch - refreshed from updateActions(), same as
    // updateStateLabel().
    QString snapTooltipText() const;
    // The two plane actions' ordinary tooltips, in one place, because
    // updateActions() swaps them for a reason-it-is-unavailable message while
    // an outline is pending and has to be able to put them back.
    QString lockTooltipText() const;
    QString unlockTooltipText() const;
    // False - with a toast naming the cause and the fix - while a closed
    // outline is waiting to be extruded. Both plane changes ask this, because
    // both would silently re-aim that outline's extrude. See its definition.
    bool canChangeSketchPlane();
    // Rebuilds the viewport from the document. Cheaper than tracking individual
    // differences, and the only way to be sure the two agree after undo/redo.
    void resyncView();
    // Shows or hides the transform gizmo from transformableBodyId(). A slot on
    // appStateChanged, and the ONE thing that attaches or detaches it - a
    // gizmo raised on a click and dismissed on some other click would be two
    // rules that drift, which is PullArrow's rule one gizmo over. Reads state
    // and moves AIS objects only, so it cannot recurse back into
    // updateActions().
    void refreshTransformGizmo();
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
    // Derivable from the sketch plane, but named because two actions' enabled
    // state reads it and "is this plane the ground one" is a floating-point
    // comparison nobody should repeat at four call sites.
    bool myFaceLocked = false;

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
    QAction* myLockFaceAction = nullptr;
    QAction* myUnlockFaceAction = nullptr;

    AppBar* myAppBar = nullptr;
    class ViewportOverlay* myOverlay = nullptr;
    class QLabel* myStateLabel = nullptr;
    class ItemsPanel* myItemsPanel = nullptr;
    class ShortcutSheet* myShortcutSheet = nullptr;
    ToastHost* myToasts = nullptr;
    ExtrudePreview* myExtrudePreview = nullptr;
    PullArrow* myPullArrow = nullptr;
};
