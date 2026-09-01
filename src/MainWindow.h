#pragma once
// OCCT first (Handle() macro vs. Windows headers pulled in by Qt).
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <QMainWindow>

#include "DocumentModel.h"
#include "Measure.h"
#include "SketchController.h"
#include "UserProgress.h"

class AppBar;
class AppearancePanel;
class BevelArrow;
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

    // The OPEN band a single scale gesture may land in - both ends are
    // refused, not merely everything beyond them. Exclusive on purpose: a
    // shrink dragged all the way down snaps to exactly kMinScale with Snap on
    // and lands a hair below it with Snap off, so a half-open band would let
    // Snap to Grid decide whether the same gesture was legal.
    //
    // Below the low end a body is gone from the viewport without looking
    // deleted; above the high end it swallows the scene. Both are recoverable
    // by scaling again, which is why this refuses the gesture rather than
    // clamping the number - a clamp would silently do something other than
    // what the user dragged.
    static constexpr double kMinScale = 0.05;
    static constexpr double kMaxScale = 20.0;

    // How long after the last appearance edit the spec is written to
    // QSettings - see persistAppearance() for why that write is debounced at
    // all. Public so the suite waits on the real number rather than a second
    // copy of it that could drift out of step with this one.
    static constexpr int kAppearanceWriteMs = 400;

    // The document id of the body `face` belongs to, or 0. Derived by walking
    // the document rather than remembered: face indices are not stable across
    // a rebuild (CLAUDE.md's topological-naming warning), so a cached
    // face-to-body mapping is a bug waiting for the user's next boolean.
    int bodyIdForFace(const TopoDS_Face& face) const;
    // The same, for an edge, and derived the same way for the same reason.
    int bodyIdForEdge(const TopoDS_Edge& edge) const;

    // THE predicate behind the bevel arrow, and everything the gizmo needs to
    // stand itself up: ONE OR MORE straight edges selected, in edge selection
    // mode, ALL ON ONE document body, each with two adjacent faces that define
    // an outward bisector - and no sketch in progress and no outline waiting.
    //
    // The widening from "exactly one" to "one or more on one body" is
    // multi-edge bevels. A selection spanning two bodies raises no arrow: one
    // gesture is one kernel build on one body, and there is no honest way to
    // draw one arrow for two. `edges` comes back in selection order and
    // `edge` is the one the arrow stands on - the last one picked.
    //
    // One function, used to show the arrow, to hide it, and to write the
    // status label, so the three can never disagree. The mode check is what
    // keeps this exclusive with the face pull (face mode) and the transform
    // gizmo (body mode); the sketch and pending-face halves are
    // canPullSelectedFace()'s, and they are what keep it exclusive with
    // ExtrudePreview - and so keep the two application-wide Enter/Escape
    // claims from ever being installed at once.
    //
    // Outputs are left untouched when it returns false.
    bool bevelTarget(std::vector<TopoDS_Edge>& edges, TopoDS_Edge& edge, int& bodyId,
                     gp_Pnt& centre, gp_Dir& outward) const;
    bool canBevelSelectedEdge() const;

    // Rounds `edges` with radius `size` (fillet == true) or flattens them with
    // distance `size` (fillet == false), through ModelingOps, replacing the
    // body they belong to. The one commit path for the bevel gizmo: it takes
    // the undo checkpoint, resyncs the viewport, records progress and reports
    // the outcome, so BevelArrow never touches DocumentModel.
    //
    // ONE checkpoint and ONE toast however many edges are named, because it is
    // one gesture - and one kernel build, so the refusal is all-or-nothing
    // (ModelingOps::filletEdges' contract). Every edge must belong to the same
    // body; a list spanning two is refused before the kernel is asked.
    //
    // False, with a Failure toast in cause-and-fix form and the body left
    // exactly as it was, whenever the kernel refuses - which it legitimately
    // does whenever the radius or the flat would eat a neighbouring face. The
    // kernel's own error string is logged, never shown.
    bool bevelEdgesBy(const std::vector<TopoDS_Edge>& edges, double size, bool fillet);

    // The refusal copy, one source each, so the production path and the
    // banned-word sweep read the same sentence rather than a second copy only
    // the sweep sees.
    //
    // Toast::paintedTexts() records every message shown this run - which means
    // a Failure a run never triggers is not swept at all, and two of this
    // branch's are exactly that: a chamfer the kernel refuses (only the fillet
    // half is reachable from a probe) and a transform it refuses (the kernel
    // accepts every gp_Trsf a gesture can build). The suite shows both once
    // through these, which is the only honest way to cover them.
    static QString bevelRefusalText(bool fillet);
    // The refusal that is NOT about the size - a set of edges the kernel will
    // only bevel some of. Separate copy because "try a smaller size" is false
    // advice there: no size works. See ModelingOps' combinationRefused.
    static QString bevelCombinationRefusalText(bool fillet);

    // Which of Move / Rotate / Scale a delta is, in the two forms the copy
    // needs - "Rotate" for a sentence that leads with the operation, "rotated"
    // for one that reports it. Read off the transform itself rather than
    // remembered from the handle that was grabbed, and used by the success
    // path AND both refusal paths, so a refused rotate cannot report a Move.
    static QString transformOperationName(const gp_Trsf& delta);
    static QString transformPastVerb(const gp_Trsf& delta);
    static QString transformRefusalText(const gp_Trsf& delta);

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

    // THE outline Extrude would consume: the one selected in the Items drawer
    // if that selection still names a live outline, otherwise the most recent
    // one. 0 when there is none.
    //
    // Since Phase 7 the "pending face" is not a member any more - it is this
    // derived view over DocumentModel's outline items, and hasPendingFace()
    // below is exactly `pendingOutlineId() != 0`. Every gizmo predicate,
    // ExtrudePreview, canChangeSketchPlane() and the Enter/Escape exclusivity
    // ruling gate on that function, and its truth table is unchanged for the
    // flow they were all written against: close an outline and it is true,
    // extrude and it is false.
    //
    // What DID change is that starting or cancelling another sketch no longer
    // makes it false. An outline is a document item now: discarding one as a
    // side effect of picking up the pencil again would delete something the
    // drawer lists and the undo stack owns. Undo is how an outline goes away
    // without becoming a body.
    int pendingOutlineId() const;
    TopoDS_Face pendingFace() const;
    // The direction extrudePendingFace() would sweep the pending outline
    // along: that outline's OWN stored plane normal, falling back to the live
    // sketch plane when nothing is pending. Exposed so ExtrudePreview builds
    // its preview along the direction the commit will actually use rather
    // than re-deriving one from state that may have moved since.
    gp_Dir pendingSweepDirection() const;
    bool hasPendingFace() const { return pendingOutlineId() != 0; }
    // Makes `id` the outline Extrude will consume - the Items drawer's row
    // click, and the only route to it. A no-op for an id that is not a live
    // outline.
    void selectOutline(int id);
    bool isSketching() const { return mySketching; }
    OcctViewWidget* view() const { return myView; }
    class ItemsPanel* itemsPanel() const { return myItemsPanel; }
    AppearancePanel* appearancePanel() const { return myAppearancePanel; }

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

    // Emitted after Theme::setSpec() has installed a new appearance and this
    // window has re-dressed everything that cannot re-derive its own colours
    // at paint time. Purely an announcement for anything outside this window
    // that wants to follow the look; the window's own relay - the viewport's
    // background and grid, the status bar's font, updateActions() - has
    // already run by the time this fires.
    void themeChanged();

    // Emitted by Help -> Show tips again, immediately before the
    // appStateChanged() that follows it. Clearing the store is not enough on
    // its own to bring every teaching surface back: a surface that also
    // remembers what it has already shown *this session* would stay quiet
    // until a restart, which is precisely what Show tips again exists to
    // avoid. This lets each surface drop that session memory itself, without
    // MainWindow having to know any of them has one.
    void progressReset();

protected:
    // Flushes a pending appearance write - see myAppearanceWrite.
    void closeEvent(QCloseEvent* event) override;

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
    // entry and its 0 shortcut are both this, so neither carries its own copy
    // of the pose. Also drops any borrowed orthographic look - see the
    // definition.
    void goAxonometric();
    void buildOverlay();
    void updateActions();

    // Pushes both sketch constraints onto the viewport from the sketch's own
    // points: the straight-continuation anchor (the last placed point and the
    // direction of the segment that led into it, or nothing below two points)
    // and the closing target (the first point, or nothing until the outline
    // can close). One function, because the two are derived from the same
    // list and must never describe different sketches. Called from every
    // route that changes that list, for the same reason
    // updateEdgeDimension() is: state only some of them refresh is state that
    // is sometimes a lie.
    void syncSketchConstraints();
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
    // Holds the edge-length annotation back for as long as the bevel arrow is
    // up, and lets it come back when the arrow goes. A slot on
    // appStateChanged, derived from the SAME predicate that raises the arrow -
    // BevelArrow does not reach into DimensionRenderer, and DimensionRenderer
    // knows nothing about bevels. Reads state and moves AIS objects only, so
    // it cannot recurse back into updateActions().
    void refreshEdgeAnnotation();
    // The relay from Theme's broadcast into this window. Re-dresses the three
    // things a repaint cannot reach - the viewport (a driver clear colour,
    // two Prs3d drawers and a grid built out of coloured vertices), the
    // status bar's explicitly set font, and the live sketch markers, whose
    // colours are baked into AIS objects built when the point was placed -
    // then persists the spec and calls updateActions(), whose
    // appStateChanged() is what repaints every painted widget in the shell.
    //
    // The sketch markers are re-issued from mySketch rather than from a copy
    // OcctViewWidget would otherwise have to keep, which is why this lives
    // here and not there: this window owns the gesture's state.
    void onThemeChanged();
    // Writes the live spec to QSettings under the same guard as the learning
    // progress and the display unit. A no-op for the suite's windows.
    //
    // DEBOUNCED, unlike recordProgress()'s write-through. A learning event
    // happens once per user action; a theme edit happens once per mouse MOVE
    // inside the colour picker's wheel, and each of those already costs a
    // full stylesheet re-polish and a grid rebuild. Adding a registry write
    // and a file sync to every frame of a drag is the one part of that cost
    // that buys nothing: nobody needs the value from halfway through a
    // gesture to survive a crash. kAppearanceWriteMs after the last edit,
    // so one write per editing burst however long the drag was.
    void persistAppearance();
    // The ONE place a spec reaches QSettings. Both routes that store one - the
    // debounce timer and closeEvent()'s flush - call this rather than carrying
    // a copy of the write each.
    void writeAppearanceNow();
    // Rounds the two chrome strips' heights up to whole device pixels, so the
    // viewport's top and bottom edges cannot land on a fractional device row
    // and leave an unpainted black line across the window. Called from the
    // constructor and from every theme change, because the type scale is what
    // moves those heights. See its definition for the measurement.
    void syncChromeHeights();
    // The two halves transformOperationName()/transformPastVerb() agree on.
    // Scale is asked first: AIS_Manipulator leaves the rotation part identity
    // during a scale, and a gesture that somehow carried both is a scale the
    // user is watching happen.
    static bool transformIsScale(const gp_Trsf& delta);
    static bool transformIsRotation(const gp_Trsf& delta);
    void runBoolean(int kind);   // ModelingOps::BooleanKind as int, to keep it out of the header
    // The one place "the camera was moved to a named direction" is recorded.
    // Every route to that - the four View menu entries and a click on the
    // axis gizmo - goes through here, so no route can record the event
    // without also emitting appStateChanged, which is what actually retires
    // the hint that teaches it.
    void recordViewChanged();

    // The document ids of every outline right now, in list order. Taken
    // before an undo or a redo so adoptRestoredOutline() can tell which one
    // the move brought back.
    std::vector<int> outlineIds() const;
    // Makes an outline that has APPEARED since `before` the pending
    // selection - the thing the user just took back, or put back. See its
    // definition for why pendingOutlineId()'s "last in the list" fallback
    // cannot answer this on its own.
    void adoptRestoredOutline(const std::vector<int>& before);

    // The Persp/Ortho toggle's one implementation. Sets the camera's BASE
    // projection, persists it under the same guard as every other preference,
    // and refreshes the bar's readout through updateActions().
    //
    // It deliberately does NOT recordViewChanged(): a projection flip is not a
    // look in a named direction, and the hint that teaches the axis gizmo
    // retires on that event. Letting this record it would retire the hint for
    // something the user has not done - the precise defect CLAUDE.md's
    // "a hint retires when its own trigger stops holding" rule exists to stop.
    void setBaseProjection(bool orthographic);

    // Flies the camera square onto a face: the eye moves onto the face's
    // OUTWARD normal, the target to the face's centre, the distance out far
    // enough to frame it, orthographic for as long as the user does not orbit.
    // Called by lockToFace() only, and only once the lock has been ACCEPTED -
    // a refused lock must fly nowhere, or the camera would move to a face the
    // user is not going to be drawing on.
    void flyOntoFace(const TopoDS_Face& face, const gp_Pln& plane);

    OcctViewWidget* myView = nullptr;
    DocumentModel myDocument;
    SketchController mySketch;

    // Which outline item Extrude would consume, when the user has chosen one
    // from the drawer. Not the pending face itself and not a cursor into the
    // outline list: it is a document id, checked against the live list on
    // every read (see pendingOutlineId()), so an undo that removes the outline
    // it names silently falls back to the newest rather than resolving to
    // something else. 0 means "whichever is newest", which is what the
    // single-outline flow always wants.
    int mySelectedOutlineId = 0;
    bool mySketching = false;
    // Derivable from the sketch plane, but named because two actions' enabled
    // state reads it and "is this plane the ground one" is a floating-point
    // comparison nobody should repeat at four call sites.
    bool myFaceLocked = false;

    UserProgress myProgress;
    bool myPersistProgress = true;
    // The debounce behind persistAppearance(). Single-shot and restarted by
    // every edit, so it fires once the user stops moving. closeEvent() flushes
    // it, because a window shut inside the debounce window must not lose the
    // colour the user just chose - a debounce that can drop the last write is
    // not a debounce, it is a bug with a timer.
    class QTimer* myAppearanceWrite = nullptr;

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
    QAction* myAppearanceAction = nullptr;
    // Checkable, and the single source of the base projection's truth: the
    // View menu entry, the O shortcut and the bar's readout button are all
    // this one action, exactly as the unit chip is the Units entries.
    QAction* myOrthographicAction = nullptr;
    // What the stored setting said, read in the constructor before the
    // viewport exists and applied the moment it does. A plain bool rather
    // than a second read, because QSettings is touched once per preference
    // and only under myPersistProgress.
    bool myStartOrthographic = false;

    AppBar* myAppBar = nullptr;
    class ViewportOverlay* myOverlay = nullptr;
    class QLabel* myStateLabel = nullptr;
    class ItemsPanel* myItemsPanel = nullptr;
    AppearancePanel* myAppearancePanel = nullptr;
    class ShortcutSheet* myShortcutSheet = nullptr;
    ToastHost* myToasts = nullptr;
    ExtrudePreview* myExtrudePreview = nullptr;
    PullArrow* myPullArrow = nullptr;
    BevelArrow* myBevelArrow = nullptr;
};
