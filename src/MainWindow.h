#pragma once
// OCCT first (Handle() macro vs. Windows headers pulled in by Qt).
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <QMainWindow>

#include "DocumentModel.h"
#include "FurnitureStore.h"
#include "Measure.h"
#include "SketchController.h"
#include "UserProgress.h"

class AppBar;
class AppearancePanel;
class AxisGizmo;
class BevelArrow;
class ExtrudePreview;
class OcctViewWidget;
class PullArrow;
class QAction;
class QMenuBar;
class QSplitter;
class RenderSettingsPanel;
class RenderShutterButton;
class ToastHost;
class ToolCluster;
class VersionsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // The sentinel `libraryRoot` means "use the real library location" -
    // QStandardPaths::DocumentsLocation + "/FurnifyMe" - and is the
    // constructor's own default, so `MainWindow window;` (main.cpp's own
    // call) still gets it for free. Deliberately NOT the empty string:
    // QTemporaryDir::path() returns exactly "" when a temp directory could
    // not be created at all, and treating that the same as "the caller
    // wants the default" would let a broken test silently read and write a
    // real user's Documents folder instead of failing where the mistake
    // happened. An empty (or otherwise blank) `libraryRoot` reaching the
    // constructor is refused outright - see its definition - rather than
    // quietly resolved to the real path.
    static QString defaultLibraryRoot();

    // `libraryRoot` is FurnitureStore's INJECTED directory - see
    // FurnitureStore.h and defaultLibraryRoot() above. Every test passes a
    // real QTemporaryDir path here, the same discipline persistProgress=false
    // already established for QSettings.
    explicit MainWindow(QWidget* parent = nullptr, bool persistProgress = true,
                        const QString& libraryRoot = defaultLibraryRoot());

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

    // The same debounce, for the six render-settings values (Task 7.2) - a
    // slider drag fires per mouse-move exactly as the colour wheel does, so
    // this follows persistAppearance()'s own reasoning and its own number
    // rather than inventing a second one.
    static constexpr int kRenderSettingsWriteMs = 400;

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

    // The success-message suffix commitReplaceBody()'s `linkedOthersUpdated`
    // feeds into pullFaceBy()/bevelEdgesBy()/transformBody()'s own messages,
    // on the identical " — twin followed" contract those three already read
    // off `twinFollowed` - one implementation rather than four copies of the
    // same plural rule (plurals written out, per the vocabulary rules).
    // Empty when `othersUpdated` is 0, which is every unlinked edit.
    static QString linkedGroupSuffix(int othersUpdated);

    // --- symmetry (Milestone 3: live mirror twins) --------------------------
    //
    // The Symmetry action's own handler: checking it turns the mode on at
    // WHATEVER plane document() already holds (the constructed default,
    // world YZ through the origin, the first time this ever fires); the
    // face-pick route below is the only thing that changes the plane.
    // Unchecking it unpairs everything - see DocumentModel::setSymmetry()
    // for why that carries no checkpoint but still marks the furniture
    // dirty.
    void setSymmetryEnabled(bool on);
    bool symmetryEnabled() const { return myDocument.symmetryOn(); }

    // "Set symmetry plane": the Lock to Face pick idiom, aimed at
    // DocumentModel's plane instead of the sketch plane. Turns symmetry ON
    // at `face`'s own outward-oriented plane - captured BY VALUE, the same
    // rule lockToFace() follows and for the same reason (face indices are
    // not stable across a rebuild). Refuses a non-flat face with a Failure
    // toast; unlike lockToFace() this has no pending-outline conflict to
    // refuse, since it does not touch the sketch plane at all.
    bool setSymmetryPlaneFromFace(const TopoDS_Face& face);

    // --- the mirror plane placement gesture (Milestone 4, Phase 3) ---------
    //
    // The RETROACTIVE half of live symmetry: pairing bodies that already
    // exist, as opposed to setSymmetryEnabled()'s creation-time toggle above
    // (which this gesture now feeds - a successful confirm turns
    // symmetryOn() on exactly as that toggle used to, so every later extrude
    // keeps mirroring the way it always did). `S` and the Model menu's
    // Symmetry entry are both this now when symmetryOn() reads false; when it
    // reads true the SAME action instead calls setSymmetryEnabled(false) -
    // the old toggle-off semantics, kept reachable exactly as CLAUDE.md's
    // ruling requires, one Model-menu entry serving both directions of one
    // idea ("S always means toggle symmetry off, or place a plane to turn it
    // on"). See onSymmetryActionTriggered() in the .cpp.
    //
    // ONE OR MORE bodies selected, in body selection mode, no sketch in
    // progress, no outline waiting, render mode off, and no gesture already
    // active - the same three terms canPullSelectedFace() opens with, plus
    // the mode check transformableBodyId() carries for the same reason:
    // mutual exclusivity by construction with the pull arrow (face mode),
    // the bevel arrow (edge mode) and ExtrudePreview (pending face), all
    // BEFORE either widget has to ask about the other. The transform gizmo
    // is the one exception that needs an explicit cross-check, since exactly
    // one body in body mode satisfies both this and
    // transformableBodyId() - see that function's own added term.
    bool canBeginMirrorPlacement() const;
    // Enter's own handler, called by the gesture's own value chip - reads
    // OcctViewWidget::mirrorPlacementIds()/mirrorPlacementPlane(), commits
    // through DocumentModel::pairWithMirror() (which checkpoints itself - see
    // its own header comment on why render mode's exit has to run BEFORE
    // this call rather than through checkpointDocument()'s usual choke
    // point), and reports the outcome: a Note toast with Undo naming the
    // paired count when at least one body was actually paired, a Failure
    // when pairWithMirror() found nothing it could do (every id straddling,
    // already paired, or refused by the kernel) - never-silent-failure,
    // applied to a document mutation that can genuinely net zero. Ends the
    // gesture either way. False on that Failure path; true on success.
    bool confirmMirrorPlacement();
    // Escape's own handler: ends the gesture with nothing changed. Safe to
    // call when no gesture is active.
    void cancelMirrorPlacement();

    // --- linked copies (Milestone 4, Task 4.2) ------------------------------
    //
    // Where symmetry keeps two bodies in step across a plane, a link group
    // keeps N bodies in step across an arbitrary placement each - see
    // DocumentModel.h's own "link groups" section for the whole engine this
    // wires up. Unlike the mirror-placement gesture above, none of the three
    // actions below is a multi-step gesture with its own Enter/Escape claim:
    // each is one click that either commits immediately or refuses with a
    // reason, the same shape Union/Subtract/Delete already have.
    //
    // Model -> Duplicate linked (Ctrl+D). Exactly one body selected, in body
    // selection mode, no sketch in progress, no outline waiting, and not
    // already paired with a mirror twin - see duplicateLinkedSourceId(). An
    // already-linked source is fine: the copy simply becomes another member
    // of its existing group (DocumentModel::createLinkedCopy()'s own rule).
    // Offsets the copy by one visible grid step so it never lands exactly on
    // its source, selects the copy and leaves body-selection mode as it is -
    // refreshTransformGizmo() (an appStateChanged slot) is what attaches the
    // transform gizmo to it, the same machinery an ordinary click already
    // drives, nothing new. One checkpoint (createLinkedCopy() takes it
    // itself - see its own header comment), one Note toast with Undo naming
    // the group's new size.
    bool duplicateLinkedCopy();
    // The body createLinkedCopy() would copy, or 0 when the gesture is
    // unavailable - see the predicate's own definition for the exact terms.
    int duplicateLinkedSourceId() const;
    bool canDuplicateLinked() const { return duplicateLinkedSourceId() > 0; }

    // Model -> Link selected. Two or more selected bodies, in body selection
    // mode, no sketch in progress, no outline waiting, none already linked
    // and none already mirror-paired - see canLinkSelected(). Snaps every
    // selected body but the first onto the first's own shape, centre to
    // centre, through DocumentModel::linkExisting() - the visible shape
    // change IS the point, not a side effect. One checkpoint
    // (linkExisting() takes it itself), one Note toast with Undo naming how
    // many bodies now match.
    bool linkSelectedBodies();
    bool canLinkSelected() const;

    // Model -> Unlink. Exactly one selected body that is a linked member -
    // see unlinkTargetId(). Removes just that body from its group through
    // DocumentModel::unlink(); every other member keeps its own current
    // shape exactly as it stands and simply stops following this one's
    // future edits. Neither body's shape changes, so there is nothing for
    // the viewport to resync. One checkpoint (unlink() takes it itself), one
    // Note toast with Undo naming how many bodies are still linked to each
    // other afterward.
    bool unlinkSelectedBody();
    // The linked member Unlink would act on, or 0 when the gesture is
    // unavailable.
    int unlinkTargetId() const;
    bool canUnlink() const { return unlinkTargetId() > 0; }

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

    // Discards the waiting outline - one checkpoint, one Note toast carrying
    // Undo, nothing else in the document touched. False when none is waiting.
    //
    // This is the outline's EXIT, and it exists because it had none. Extrude
    // is the only other way one leaves the document, and every
    // direct-modeling gate (the pull arrow, the bevel arrow, the transform
    // gizmo, Lock to Face) refuses while one waits - while booleans and
    // Delete, which are not gated, push onto the undo stack and take
    // "Ctrl+Z to take it back" with them. Reached from Delete Selected when
    // NO BODIES are selected; see onDeleteSelected() for why that state is
    // the right one to give the second meaning to.
    bool deletePendingOutline();

    bool isSketching() const { return mySketching; }
    OcctViewWidget* view() const { return myView; }
    class ItemsPanel* itemsPanel() const { return myItemsPanel; }
    AppearancePanel* appearancePanel() const { return myAppearancePanel; }
    RenderSettingsPanel* renderSettingsPanel() const { return myRenderSettingsPanel; }
    RenderShutterButton* renderShutter() const { return myRenderShutter; }
    // The floating pill (Milestone 5, item 3) - the window's own menu strip
    // before this task, a ViewportOverlay::Anchor::TopLeft card now. Exposed
    // the same way every other overlay card is, rather than making a caller
    // find it by class through findChild<>().
    AppBar* appBar() const { return myAppBar; }
    // The four view controls (Persp/Ortho, the unit chip, Wireframe, Fit
    // All) that used to live as bar buttons - an icon-only ToolCluster
    // anchored TopRight, stacked under the axis gizmo card. Distinct from
    // the rail (also a ToolCluster, anchored LeftEdge instead) - a caller
    // that wants "the rail" still gets it as the first match of
    // findChild<ToolCluster*>() (added first, in buildOverlay()), and a
    // caller that wants this one asks here instead of guessing which
    // ToolCluster findChildren() returned.
    ToolCluster* viewControls() const { return myViewControls; }

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

    // --- the selector handoff, save and autosave (Milestones 3 and 4) -------
    //
    // Milestone 4 split the gallery out of this window into its own
    // top-level SelectorWindow (src/ui/SelectorWindow.h) - this window no
    // longer hosts an init-screen STATE at all, only the "nothing open" data
    // state (myShowingInitScreen, an empty document) that state used to
    // dress. showInitScreen() still owns that reset - live document replaced
    // with a fresh, empty one (never merely cleared - clear() leaves the
    // undo stack behind it, and the next furniture opened must not inherit
    // checkpoints that were never its own) - and emits returnedToSelector()
    // once it is done. Fix round 1 (the CRITICAL quit-trap finding): it does
    // NOT hide this window itself any more - EditorSelectorHandoff::wire()
    // (src/EditorSelectorHandoff.h) is the ONE place that ever does, and it
    // always shows the selector FIRST. Hiding here unconditionally, before
    // anything could show the selector, is exactly the ordering that let two
    // unparented top-level windows both be hidden at once and race Qt's
    // quitOnLastWindowClosed() - see that header for the full story.
    // openFurniture() is the one path both an existing card and a freshly
    // created one go through, so a new furniture and a reopened one
    // round-trip identically; it does NOT show this window itself either -
    // the caller (EditorSelectorHandoff::wire(), in production and in every
    // test that wants the real behaviour) shows it first, preserving the
    // CLAUDE.md lazy-`initializeViewer()` contract exactly as it already
    // held before this task: every route that ever called openFurniture()
    // already did so on an already-shown window.
    void showInitScreen();
    bool openFurniture(const QString& id);
    bool isShowingInitScreen() const { return myShowingInitScreen; }
    QString currentFurnitureId() const { return myFurnitureId; }
    QString currentFurnitureName() const { return myFurnitureName; }
    // Dirty = the live document's revision differs from the revision as of
    // the last save - revision() is monotonic (DocumentModel.h), so this can
    // never be fooled by an undo landing back on a number it already used.
    bool isFurnitureDirty() const;

    // File -> Save (Ctrl+S). The one save path this window has - autosave's
    // debounce timer and "close with autosave off" both call this rather
    // than carrying a copy of the write each, so a save always means the
    // same thing: capture the document, capture a thumbnail through
    // OcctViewWidget::captureThumbnail(), advance the saved-revision mark.
    // Read-only while no furniture is open or the init screen is showing.
    bool saveCurrentFurniture();
    // File -> Close furniture, and the native X's own route to it
    // (MainWindow::closeEvent(), which always calls this when a furniture
    // is open). SAVES FIRST whenever the furniture is dirty - never a modal
    // question - then returns to the init screen. Cancels any autosave
    // debounce still pending first, so a furniture closed a moment after
    // its last edit never loses that edit to a timer that had not fired
    // yet, but performs at most ONE fresh save attempt itself rather than
    // also flushing that timer separately - CLAUDE.md's fix-round-2 ruling
    // that an earlier failed autosave must not be double-reported.
    //
    // Fix round 2 (never-silent-failure law): if that save FAILS, this
    // ABORTS the whole close - it returns without calling showInitScreen(),
    // so the editor stays open, the Failure toast performSave() already
    // raised stays genuinely readable (it would not if this window hid a
    // moment later, per EditorSelectorHandoff.h), and the furniture stays
    // open and dirty. A caller cannot tell success from failure by return
    // value (this is still void, matching every other route into it) -
    // isFurnitureDirty() and isShowingInitScreen() are what to read instead.
    void closeCurrentFurniture();
    // File -> Save automatically (checkable, persisted). On, a debounced
    // (400 ms) save runs after every document change; see
    // kAutosaveWriteMs.
    void setAutosaveEnabled(bool enabled);
    bool autosaveEnabled() const { return myAutosaveOn; }
    static constexpr int kAutosaveWriteMs = 400;
    // The live autosave countdown in ms, or -1 when nothing is pending -
    // ToastHost::remainingMs()'s own shape, for the same reason: the suite
    // asserts the ARMED timer rather than waiting kAutosaveWriteMs real
    // milliseconds for it to fire.
    int autosavePendingMs() const;

    FurnitureStore& furnitureStore() { return myStore; }

    // --- versions and the side-by-side compare (Milestone 3) ---------------
    //
    // The commit path both VersionsPanel's + button and File -> Save
    // version... reach through (see VersionsPanel::beginNewVersion() /
    // MainWindow::onSaveVersion()) - the one place this window reaches the
    // store to persist a version. A duplicate name is
    // FurnitureStore::saveVersion()'s one real refusal here (an unknown
    // furniture id cannot happen - this guards on a real, open one first),
    // reported with a Failure toast naming the clash; VersionsPanel's own
    // pending card stays open on that refusal so the user can retype.
    // Versions are file data, not document state - no checkpoint, no Undo
    // on the Note toast that reports success.
    bool saveVersion(const QString& name);

    // VersionsPanel's Restore button. Closes any open compare FIRST (a
    // restore replaces the very document a stale compare pane would still
    // be showing half of), then replaces the WHOLE live document - bodies,
    // outlines, names, visibility - through ONE checkpoint
    // (DocumentModel::checkpoint() then restoreFrom(), never
    // fromSerialized(), which clears undo history outright - see
    // DocumentModel.h) so a single Ctrl+Z brings back everything this
    // replaced. Note toast `Restored version "<name>"` with Undo. The
    // version itself is unchanged - this only ever READS it.
    bool restoreVersion(const QString& name);

    // VersionsPanel's Delete, called once its own two-click confirmation has
    // fired. Final and carries no Undo - a version is file data, and
    // "Ctrl+Z brings back a deleted file" is not a promise this app makes
    // anywhere else either. Closes an open compare of exactly this version
    // first, so a stale read-only pane can never outlive the file it reads.
    bool deleteVersionByName(const QString& name);

    // Opens the side-by-side compare: swaps the central widget to a
    // QSplitter holding the live view and a second, VIEWER-ONLY
    // OcctViewWidget showing `name`'s own saved shapes, read-only. Camera
    // orbit/pan/zoom on EITHER view is mirrored onto the other - see
    // syncCamera() - until closeCompare() (the badge's own control, or a
    // Restore) ends it. Compare is VIEW state, not document state: no
    // checkpoint, no dirty star, and the live document is untouched by it.
    // Opening a different version while one is already open replaces the
    // pane rather than stacking a second one.
    bool openCompare(const QString& name);
    // Returns to the full-bleed single viewport. A no-op when compare is
    // not open.
    void closeCompare();
    bool isCompareOpen() const { return myCompareView != nullptr; }
    OcctViewWidget* compareView() const { return myCompareView; }
    QString compareVersionName() const { return myCompareVersionName; }

    // THE predicate behind both File -> Save version... and VersionsPanel's
    // + button: a furniture is open, no sketch is in progress, render mode
    // is off, and none of the three application-wide Enter/Escape claims is
    // live (ExtrudePreview, the pull arrow, the bevel arrow). Milestone 4
    // retired SaveVersionCard - the fourth application-wide claim this
    // predicate used to keep disjoint from - so the "mutually exclusive
    // claims" reasoning is now about those three alone; kept as its own
    // named predicate rather than folded away because updateActions() still
    // gates the action on it, and VersionsPanel still reads it a second
    // time on appStateChanged, to cancel a pending create if it goes false
    // while one is open (VersionsPanel::refresh()'s own auto-cancel, the
    // same reasoning SaveVersionCard::onAppStateChanged() used to apply).
    bool canOpenSaveVersion() const;

    // Fixed copy the compare badge paints, exposed statically - like
    // bevelRefusalText() and friends above - so the vocabulary sweep can
    // check it without a live compare pane, and so it has exactly one
    // implementation the badge's own construction reads too.
    static QString compareBadgeCloseLabel();

    // The mirror-placement chip's own label and hint text, on
    // compareBadgeCloseLabel()'s exact terms: MirrorPlacementChip has no
    // header of its own to declare these in (it lives entirely inside
    // MainWindow.cpp - see myMirrorChip's field comment), so gui_smoke's
    // banned-word sweep cannot reach a live instance's paintedTexts() the
    // way it does for PullArrow/BevelArrow/ExtrudePreview. These two are
    // the SAME strings the chip paints, never a second copy only the sweep
    // sees - its own labelText()/hintText() call straight through to these.
    static QString mirrorPlacementLabelText();
    static QString mirrorPlacementHintText();
    // The live chip WIDGET, for gui_smoke's application-wide-claim count.
    // The chip installs its filter on show and removes it on hide (its own
    // showEvent()/hideEvent()), so this widget's isVisible() IS the
    // installed-filter state - which is the whole reason the suite must
    // read it rather than OcctViewWidget::mirrorPlacementActive(). A
    // visible chip over an inactive gesture is a live Enter/Escape/X/Y/Z
    // claim, and counting the state flag instead reported ZERO claims for
    // exactly that case: CLAUDE.md's "assert isVisible(), or a stub that
    // never calls show() sails through", applied backwards.
    QWidget* mirrorPlacementChip() const { return myMirrorChip; }

    // --- Render mode (Milestone 3, item 5) ----------------------------------
    //
    // View -> Render mode: strips the viewport to the furniture alone -
    // grid, drawers, rail, the axis gizmo card, every live gizmo and the
    // dimension all hidden or suppressed - and switches to the best
    // rendering tier this GPU sustains interactively (see
    // OcctViewWidget::setRenderMode() for the three-tier probe). Checkable,
    // and unlike every OTHER View toggle in this file, deliberately NEVER
    // persisted: CLAUDE.md's own words are "the app always starts in
    // modeling", so this never touches QSettings the way
    // setShowBottomBar()/setAutosaveEnabled() and friends do.
    //
    // Also the one place that flips myRenderModeAction's checked state, in
    // BOTH directions - the user unchecking the box calls this through the
    // action's own toggled(bool), and every exit gesture (a viewport pick,
    // Start Sketch, any document-changing commit - see checkpointDocument())
    // calls it directly, which is what un-checks the box FOR them. Ends by
    // calling updateActions(), the single authority every other toggle in
    // this file already answers to.
    void setRenderModeEnabled(bool on);
    bool renderModeEnabled() const { return myRenderModeOn; }

    // THE predicate behind View -> Render mode's own enabled state: a
    // furniture open, no compare open, not sketching, no outline waiting -
    // the four conditions named in this task's own ruling. Render mode
    // raises no application-wide Enter/Escape claim of its own (it is a
    // toggle, not a text field), so unlike canOpenSaveVersion() it does not
    // need to exclude the three gizmo predicates - it hides them itself the
    // moment it turns on.
    bool canOpenRenderMode() const;

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

    // Milestone 4: this window is done editing and wants the selector shown
    // again - emitted by showInitScreen() (so every route that already went
    // through it - Close furniture, opening a different card, a failed
    // openFurniture() - carries this for free) once its own state reset is
    // done. Fix round 1: this window does NOT hide itself before or after
    // emitting this - EditorSelectorHandoff::wire() (src/
    // EditorSelectorHandoff.h) is what shows SelectorWindow and THEN hides
    // this window, in that order, which is what closes the quit-trap the
    // opposite order opened. This window knows nothing of that class,
    // exactly as it knows nothing of MainWindow.
    void returnedToSelector();

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
    void onRenameSelected();
    void onUndo();
    void onRedo();

    // The Items drawer's own rename gesture (double-click or F2) landed on a
    // row - see ItemsPanel::renameCommitted(). One checkpoint, one setItemName
    // call, one Note toast with Undo - the shape every other checkpointed
    // commit in this file follows.
    void onItemRenameCommitted(int id, bool isOutline, QString newName);

    void onExportStep();
    void onSelectionModeChanged();
    void onSelectionChanged();
    void onLockToFace();
    // "Set symmetry plane": reads the current face selection and calls
    // setSymmetryPlaneFromFace() - the Lock to Face idiom, one gizmo over.
    // The checkable Symmetry action itself needs no slot of its own: its
    // toggled(bool) connects straight to setSymmetryEnabled(), exactly as
    // myAutosaveAction connects to setAutosaveEnabled().
    void onSetSymmetryPlane();
    // mySymmetryAction's own triggered() handler (NOT toggled() any more -
    // see the action's own comment in the .cpp for why the split matters):
    // turns mirroring off when it is already on, otherwise attempts to begin
    // the plane-placement gesture and reverts the action's own optimistic
    // checked-flash via updateActions() when that refuses.
    void onSymmetryActionTriggered();
    // A plain double-click on a body in face or edge selection mode: switch to
    // body selection and select that body, in one gesture. Routed through
    // mySolidSelectAction rather than straight at the viewport, so the rail
    // chip, the menu entry and the status label all follow - the mode is that
    // action's checked state, and nothing else may write it.
    void onBodyDoubleClicked(int solidId);
    // The end of a transform-gizmo drag. An identity delta is a cancel - the
    // user released where they started, or the snap rounded the whole gesture
    // away - and a cancel takes no checkpoint and says nothing. The viewport
    // has already put its presentation back by the time this runs (see
    // OcctViewWidget::endGizmoDrag), so there is nothing to undo here either.
    void onGizmoReleased(int solidId, const gp_Trsf& delta);

    // File -> Save version...: starts VersionsPanel's own create-a-version
    // gesture (VersionsPanel::beginNewVersion()), opening the drawer first
    // if it is not already showing. Split from the panel itself on the same
    // terms onExtrude()/ExtrudePreview are - the action's enabled state is
    // canOpenSaveVersion(), and this is only ever reachable once that
    // already holds.
    void onSaveVersion();

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
    // points: the compass anchor (the last placed point, from which Shift
    // dials the 8 directions - one point is enough, no prior segment needed)
    // and the closing target (the first point, or nothing until the outline
    // can close). One function, because the two are derived from the same
    // list and must never describe different sketches. Called from every
    // route that changes that list, for the same reason
    // updateEdgeDimension() is: state only some of them refresh is state that
    // is sometimes a lie.
    void syncSketchConstraints();
    // Persistent right-hand readout: what mode we are in and what is possible.
    void updateStateLabel();
    // The label's own word for a vertical face-on plane's normal -
    // "Front" for a Y-normal (world XZ - Front and Back share this plane,
    // so a plane already pinned to a sketch has no way to tell the two
    // apart, and this names it Front either way) and "Right" for an
    // X-normal (world YZ - Right and Left, same reasoning). Empty for
    // anything else: the ground plane's Z-normal, or a locked face at an
    // arbitrary angle - updateStateLabel() already has its own cue for
    // that case and never calls this for it.
    QString faceOnDirectionLabel(const gp_Dir& normal) const;
    // The grid-step length, through Measure, so the snap tooltip never goes
    // stale after a unit switch - refreshed from updateActions(), same as
    // updateStateLabel().
    QString snapTooltipText() const;
    // The two plane actions' ordinary tooltips, in one place, because
    // updateActions() swaps them for a reason-it-is-unavailable message while
    // an outline is pending and has to be able to put them back.
    QString lockTooltipText() const;
    QString unlockTooltipText() const;
    // The three linked-copy actions' ordinary tooltips (Milestone 4, Task
    // 4.2), on the identical contract: buildActions() sets these once and
    // updateActions() swaps in a reason-specific message while disabled,
    // restoring these exact strings the moment the action is enabled again.
    QString duplicateLinkedTooltipText() const;
    QString linkSelectedTooltipText() const;
    QString unlinkBodyTooltipText() const;
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
    // Fix round 1's own finding: a mirror-placement gesture's disjointness
    // from the other three gizmo claims held only at the PRESS that began
    // it, not for the gesture's whole life - switching selection mode
    // mid-gesture (still enabled; nothing had ever re-checked it) let
    // PullArrow or BevelArrow rise while the mirror chip was still up, two
    // application-wide key filters live at once. ExtrudePreview's own
    // self-cancel discipline, one gizmo over: a slot on appStateChanged
    // that recomputes mirrorPlacementEnvironmentOk() against an ALREADY
    // active gesture and ends it the instant that predicate fails - mode
    // switch, sketch start, render mode, all covered by the one recompute
    // rather than three separate reminders. Reads state and moves AIS
    // objects only, so it cannot recurse back into updateActions() - see
    // its own definition for why that matters here specifically.
    void refreshMirrorPlacement();
    // The environment half of canBeginMirrorPlacement() - no sketch, no
    // pending outline, no render mode, body selection mode - WITHOUT that
    // function's other two terms ("not already active", "something is
    // selected"), which only make sense at the moment of a BEGIN and would
    // be wrong to ask of a gesture already running. Shared by
    // canBeginMirrorPlacement() and refreshMirrorPlacement() so the two
    // cannot drift into different ideas of what makes the gesture's
    // surroundings valid.
    bool mirrorPlacementEnvironmentOk() const;
    // The reason-specific refusal text for onSymmetryActionTriggered()'s own
    // "cannot begin" branch - fix round 1 (Task 3.2 review, Finding 3). Asks
    // the same terms canBeginMirrorPlacement() refuses on, in the same
    // order, so this can never name an obstacle that predicate did not
    // actually refuse on. The "gesture already active" case does not appear
    // here - it never reaches this function, since onSymmetryActionTriggered()
    // now treats S-while-active as a cancel, not a refusal to explain.
    QString mirrorPlacementRefusalText() const;
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
    // persistAppearance()'s own shape, for the six render-settings values
    // (Task 7.2) - a no-op under the same myPersistProgress guard, debounced
    // on the same kRenderSettingsWriteMs, restarted by every one of the six
    // RenderSettingsPanel signals rather than by any one of them alone,
    // since a burst that touches several controls in one drag should still
    // land one write.
    void persistRenderSettings();
    // The ONE place the six values reach QSettings - the debounce timer and
    // closeEvent()'s flush both call this rather than carrying a copy of
    // the write each, persistAppearance()/writeAppearanceNow()'s own split.
    void writeRenderSettingsNow();
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

    // THE one place every pull/bevel/transform commit lands - see the task-4
    // brief's own words: "if today they land in several places, this task's
    // first refactor is to route them through one". Before this, all three
    // hand-rolled their own `checkpoint(); replaceSolid(); displaySolid();`
    // sequence, which is exactly three places the twin-follows rule could be
    // forgotten as a fourth gizmo arrived.
    //
    // Takes the undo checkpoint, replaces `id`'s shape and redisplays it,
    // then - if `id` has a mirror twin - replaces the twin too, with
    // `ModelingOps::mirrorShape(newShape, myDocument.symmetryPlane())`, in
    // the SAME checkpoint, so a single Ctrl+Z reverts both. `twinFollowed`
    // reports whether that actually happened - false, and the twin left
    // completely untouched, both when `id` is unpaired and on the (expected
    // to be unreachable in practice) case the mirror itself fails, since a
    // failed twin-mirror must never turn a successful primary edit into a
    // reported failure.
    //
    // Callers still do their OWN gizmo cleanup (clearModelingPreview,
    // clearPullArrow/clearBevelArrow, clearSelection) - that has to happen
    // before the body they describe is replaced, and it differs per gizmo -
    // so this owns only the part that is genuinely identical three times
    // over: the checkpoint, the replace, and the twin.
    //
    // Milestone 4 (Task 4.2) extends the same choke point for linked copies:
    // `id`'s mirror twin and its link-group membership are mutually
    // exclusive by construction (DocumentModel's own v1 exclusion, enforced
    // both directions), so at most one of the two follow-up branches ever
    // runs. When `id` is linked, DocumentModel::propagateLinkedEdit() re-
    // derives every OTHER member from `newShape` inside the SAME checkpoint
    // taken two lines up (it takes none of its own - see its header), and
    // this redisplays each one; `linkedOthersUpdated` reports how many, 0
    // when `id` has no group, so a caller's toast can name the group size
    // exactly as `twinFollowed` already lets it say "twin followed". It is
    // -1 - never a count - when propagateLinkedEdit() REFUSED, having
    // written nothing; linkedGroupSuffix() turns that into a sentence
    // saying the copies did not follow, because a group's membership count
    // is not evidence that anything was written to it (M1).
    void commitReplaceBody(int id, const TopoDS_Shape& newShape, bool& twinFollowed,
                           int& linkedOthersUpdated);

    // Walks `editedId`'s link group (if it has one) and redisplays every
    // OTHER member from the document's own now-current shape -
    // propagateLinkedEdit() already wrote them; this is only the viewport's
    // own resync, the same split commitReplaceBody() already draws between
    // "document mutation" and "AIS redisplay" for a mirror twin. Returns the
    // number of other members touched (0 when `editedId` has no group).
    int resyncLinkGroupView(int editedId);

    // The environment shared by all three linked-copy actions above: no
    // sketch in progress, no outline waiting, a furniture actually open, and
    // body selection mode explicitly - selectedSolidIds() reports the owning
    // body of a selected FACE or EDGE too, so without this a face or edge
    // selection could satisfy a count check that means something different
    // in body mode. The same mode check transformableBodyId() and
    // mirrorPlacementEnvironmentOk() each carry, for the same reason.
    bool linkGestureEnvironmentOk() const;

    // THE single choke point every document-changing commit's checkpoint()
    // call now goes through, in place of calling myDocument.checkpoint()
    // directly (eight call sites, before this) - which is what makes render
    // mode's own exit rule ("any document-changing action leaves render mode
    // FIRST") structural rather than eight separate reminders to add one.
    // Exits through setRenderModeEnabled(false), the single authority that
    // un-checks the action, before the checkpoint it guards ever lands.
    void checkpointDocument();
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

    // View -> Show notifications. Stores the preference under the same guard as
    // every other one and calls updateActions(), which is what pushes it onto
    // the toast host. Silences Kind::Note only - see ToastHost::show() for why
    // a Failure is not this preference's to suppress.
    void setShowNotifications(bool show);

    // View -> Show bottom bar. Same shape as setShowNotifications() - stores
    // the preference under the same guard, calls updateActions(), which is
    // what derives statusBar()'s visibility from it (the same
    // appStateChanged-driven block that derives the items/versions/appearance
    // drawers' own visibility from their actions, so a QWidget::show() this
    // file did not intend to survive cannot leave the bar stuck on). It hides
    // only the BAR - a Failure toast is unrelated chrome, parented to
    // OcctViewWidget rather than to the status bar, and stays reachable
    // exactly as CLAUDE.md's never-silent-failure law requires.
    void setShowBottomBar(bool show);

    // The one save implementation - Ctrl+S, autosave's debounce timer and
    // "close with autosave off" all call this rather than each carrying its
    // own copy. `announce` is what tells Ctrl+S's success apart from
    // autosave's: a Note ("Saved Furniture NN") only when the user asked for
    // it directly, never once per debounced background write, while a
    // FAILURE is never conditional on it - CLAUDE.md's law that a refusal
    // must report somewhere applies to a silent autosave exactly as it does
    // to everything else.
    bool performSave(bool announce);
    // The debounce timer's own timeout. closeCurrentFurniture() does NOT
    // route through this any more - it cancels the debounce and makes one
    // fresh save decision of its own, so a failed close-time save can abort
    // the handoff (fix round 2). A no-op when nothing is actually dirty, so
    // an autosave firing the instant after a manual save does not write twice.
    void flushAutosave();
    // Builds myAutosaveTimer on first use (same lazy-build reasoning as
    // persistAppearance()'s myAppearanceWrite) and (re)starts it - the ONE
    // place either happens, so the two call sites that arm it (a checkpoint,
    // and the toggle turning on over an already-dirty document) cannot drift
    // out of step with each other's interval or wiring.
    void armAutosaveTimer();
    // The window title from the furniture name and the dirty star - the
    // ONE place either is written, called from updateActions() the way
    // updateStateLabel() is, so a save, an undo/redo, or opening a different
    // furniture can never leave it stale.
    void updateWindowTitle();

    // Flies the camera square onto a face: the eye moves onto the face's
    // OUTWARD normal, the target to the face's centre, the distance out far
    // enough to frame it, orthographic for as long as the user does not orbit.
    // Called by lockToFace() only, and only once the lock has been ACCEPTED -
    // a refused lock must fly nowhere, or the camera would move to a face the
    // user is not going to be drawing on.
    void flyOntoFace(const TopoDS_Face& face, const gp_Pln& plane);

    // The no-recursion camera sync: copies `from`'s CameraState onto `to`
    // ONLY when the two differ by more than a tight epsilon, then pushes it
    // straight onto `to`'s OCCT camera through setCameraStateNow() - which
    // itself unconditionally emits cameraChanged() again. That second
    // emission is what closes the loop rather than opening an infinite one:
    // by the time it reaches the OTHER direction's own equality check, the
    // two states already agree (this call just made them), so that check
    // returns without copying anything further. Wired both ways - myView's
    // cameraChanged to sync into myCompareView, and (only once one exists)
    // myCompareView's cameraChanged to sync into myView - so orbiting
    // either view moves both.
    void syncCamera(OcctViewWidget* from, OcctViewWidget* to);

    OcctViewWidget* myView = nullptr;
    DocumentModel myDocument;
    SketchController mySketch;

    // The managed library - see FurnitureStore.h. A plain value member, not
    // a pointer: the class holds nothing but its own root directory string,
    // so there is no ownership question to resolve between "the real one"
    // and "the one a test injected" - the constructor just picks which
    // string to build it from.
    FurnitureStore myStore;
    // The init screen is the window's state whenever no furniture is open -
    // true from construction (nothing is open yet) until openFurniture()
    // succeeds, and true again the moment showInitScreen() runs.
    bool myShowingInitScreen = true;
    // Empty exactly when myShowingInitScreen is true - the two are kept in
    // step by showInitScreen()/openFurniture() rather than derived from one
    // another, because "derived from an empty id" reads backwards from what
    // actually causes what.
    QString myFurnitureId;
    QString myFurnitureName;
    // DocumentModel::revision() as of the last successful save - see
    // isFurnitureDirty(). 0 while no furniture is open, which is harmless:
    // isFurnitureDirty() refuses to answer true for that state regardless.
    int mySavedRevision = 0;
    // File -> Save automatically, persisted under the same guard as every
    // other preference. Defaults to on: CLAUDE.md's ruling for this branch
    // is "never lose work, never block", and a new user who has not found
    // the toggle yet should get the safer default.
    bool myAutosaveOn = true;
    // The debounce behind autosave - built on first use, exactly as
    // myAppearanceWrite is, and for the same reason: a window that never
    // sees a checkpoint while autosave is on never creates one.
    class QTimer* myAutosaveTimer = nullptr;

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
    // persistRenderSettings()'s own debounce, myAppearanceWrite's exact shape
    // for the six render-settings values (Task 7.2).
    class QTimer* myRenderSettingsWrite = nullptr;

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
    // F2, and (like Delete) two meanings decided in ONE place - updateActions().
    // Unlike Delete, the two meanings never fall back on each other: renaming
    // is a single-item gesture (InlineRename edits one name), so this is
    // enabled for exactly one selected body, or for the waiting outline when
    // no body is selected - never for a multi-body selection, where Delete
    // stays available but this does not.
    QAction* myRenameAction = nullptr;
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
    // Symmetry (Milestone 3) - menu-only, per the ledger note: the rail is
    // at its height floor and a fourteenth chip is the rework CLAUDE.md
    // already says it wants before it gets there.
    QAction* mySymmetryAction = nullptr;
    // "Turn Mirroring Off" - the destructive unpair-everything half, split
    // out of mySymmetryAction when S was re-scoped to always begin a
    // placement (see buildActions()). Menu-only, no shortcut.
    QAction* mySymmetryOffAction = nullptr;
    QAction* mySetSymmetryPlaneAction = nullptr;
    // Linked copies (Milestone 4, Task 4.2) - menu-only for the same reason:
    // the rail stays at thirteen tools.
    QAction* myDuplicateLinkedAction = nullptr;
    QAction* myLinkSelectedAction = nullptr;
    QAction* myUnlinkAction = nullptr;
    QAction* myAppearanceAction = nullptr;
    // File -> Save / Save automatically / Close furniture - see the public
    // methods above, which every one of these three triggers into.
    QAction* myFileSaveAction = nullptr;
    QAction* myAutosaveAction = nullptr;
    QAction* myCloseFurnitureAction = nullptr;
    // Checkable, and the single source of the base projection's truth: the
    // View menu entry, the O shortcut and the bar's readout button are all
    // this one action, exactly as the unit chip is the Units entries.
    QAction* myOrthographicAction = nullptr;
    // Checkable, and the single source of the notification preference's truth,
    // exactly as myOrthographicAction is for the projection.
    QAction* myNotificationsAction = nullptr;
    // What the stored setting said, read in the constructor before any action
    // exists so the View entry is built already ticked correctly. Default true.
    bool myShowNotifications = true;
    // Checkable, and the single source of the bottom bar's own visibility,
    // exactly as myItemsPanelAction is for the drawer - statusBar()'s shown
    // state is DERIVED from this action's checked state, both directions, in
    // the same appStateChanged-driven block that derives the drawers' own.
    QAction* myBottomBarAction = nullptr;
    // What the stored setting said, read in the constructor before any action
    // exists, on the same terms as myShowNotifications above. Default true -
    // an app that started with no status bar would look broken to a
    // first-time user, the same reasoning myShowNotifications's own comment
    // gives.
    bool myShowBottomBar = true;
    // What the stored setting said, read in the constructor before the
    // viewport exists and applied the moment it does. A plain bool rather
    // than a second read, because QSettings is touched once per preference
    // and only under myPersistProgress.
    bool myStartOrthographic = false;

    // The six render-settings values (Task 7.2), read from QSettings in the
    // constructor on myStartOrthographic's own terms - before myView
    // exists, applied to it the moment it does - and pushed silently into
    // RenderSettingsPanel once buildOverlay() constructs it. Defaults match
    // OcctViewWidget's own (see its header), so a first-ever run applies
    // nothing different from what the viewport already defaults to.
    double myStartRenderRoughness = 0.55;
    double myStartRenderMetallic = 0.0;
    double myStartRenderLightAngleDeg = -1.0;   // sentinel: "use the viewport's own default"
    double myStartRenderLightStrength = 2.0;
    QColor myStartRenderBackground;             // invalid = no stored override
    double myStartRenderFov = 45.0;

    AppBar* myAppBar = nullptr;
    class ViewportOverlay* myOverlay = nullptr;
    class QLabel* myStateLabel = nullptr;
    class ItemsPanel* myItemsPanel = nullptr;
    AppearancePanel* myAppearancePanel = nullptr;
    // The render settings card and the camera shutter (Task 7.2) - both
    // constructed in buildOverlay(), both derived-visible off myRenderModeOn
    // alone (neither has a QAction of its own to check; they exist exactly
    // when render mode does), kept here on myRail/myAxisGizmo's own terms so
    // the appStateChanged-driven visibility lambda can reach them.
    RenderSettingsPanel* myRenderSettingsPanel = nullptr;
    RenderShutterButton* myRenderShutter = nullptr;
    class ShortcutSheet* myShortcutSheet = nullptr;
    ToastHost* myToasts = nullptr;
    ExtrudePreview* myExtrudePreview = nullptr;
    PullArrow* myPullArrow = nullptr;
    BevelArrow* myBevelArrow = nullptr;

    // --- versions and the side-by-side compare (Milestone 3) ---------------
    QAction* myVersionsPanelAction = nullptr;   // View -> Versions - the drawer's law
    QAction* mySaveVersionAction = nullptr;     // File -> Save version...
    VersionsPanel* myVersionsPanel = nullptr;

    // Non-null only while compare is open. mySplitter owns myView and
    // myCompareView as its two panes for that interval; myView is
    // reparented BACK to being the plain central widget the moment compare
    // closes (see closeCompare()) - it is never left inside a torn-down
    // splitter, and setCentralWidget(myView) is what performs that move.
    QSplitter* mySplitter = nullptr;
    OcctViewWidget* myCompareView = nullptr;
    // --- Render mode (Milestone 3, item 5) ----------------------------------
    // The one flag every predicate and every appStateChanged-driven
    // visibility block below reads - never persisted, never read back from
    // OcctViewWidget::renderModeActive() at a second call site, so the
    // viewport's own state and this window's idea of it cannot
    // independently drift. setRenderModeEnabled() is the only writer.
    bool myRenderModeOn = false;
    QAction* myRenderModeAction = nullptr;
    // The rail and the axis gizmo card, kept here rather than found with
    // findChild<>() on demand - both are constructed as locals inside
    // buildOverlay() otherwise, and both need to be reached from the
    // appStateChanged-driven visibility lambda that already hides the three
    // drawers and the status bar the same way.
    ToolCluster* myRail = nullptr;
    AxisGizmo* myAxisGizmo = nullptr;
    // The view-controls cluster (Milestone 5, item 3) and the unit chip
    // inside it - kept on myRail/myAxisGizmo's own terms: both are built as
    // locals inside buildOverlay() otherwise, and the unit chip specifically
    // needs a stored pointer because, unlike the other three, it owns no
    // QAction of its own to mirror - its text is pushed in on every
    // appStateChanged, exactly as AppBar::setUnitLabel() used to be called.
    ToolCluster* myViewControls = nullptr;
    class ToolChip* myUnitChip = nullptr;
    QString myCompareVersionName;   // user text - see the badge's own rule
    // The badge and its Close-compare control, parented to myCompareView -
    // owned by Qt's parent-child cascade (destroyed with myCompareView),
    // kept only so closeCompare() need not search for them and the badge's
    // name can be updated without a second lookup if that is ever wanted.
    class QWidget* myCompareBadge = nullptr;
    // The mirror-placement gesture's floating value chip - constructed and
    // wired entirely inside buildOverlay(), like the compare badge above:
    // a plain QWidget subclass local to MainWindow.cpp, PullArrow's shape
    // but with no typed field of its own to expose (X/Y/Z, Enter and Escape
    // are its whole vocabulary), so it needs no header of its own. Its own
    // visibility is DERIVED from OcctViewWidget::mirrorPlacementActive() on
    // every appStateChanged - nothing here shows or hides it - and it is a
    // proper CHILD of the viewport (not a sibling needing manual teardown
    // the way Toast's UndoControl does), so Qt's own parent-child cascade is
    // what destroys it.
    class QWidget* myMirrorChip = nullptr;
    // Bumped once per openCompare() call - the token a deferred
    // QTimer::singleShot(0, ...) close (the compare badge's own Close
    // button; see closeCompare()'s comment) checks against before acting,
    // so a stale deferred close from a session already replaced by a newer
    // one cannot close the WRONG pane.
    int myCompareGeneration = 0;
};
