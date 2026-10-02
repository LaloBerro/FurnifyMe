# The Scene editor — several furniture in one picture

**Status:** design agreed 2026-10-02, not yet planned or implemented.

## The problem

FurnifyMe photographs one furniture at a time. Render mode strips the
viewport down to the piece being edited and gives it a studio, a frame and
an export — but the document behind it holds exactly one furniture, so a
dining table and its four chairs cannot be in one picture at all.

Furniture is not used alone and is rarely sold alone. The picture somebody
actually wants is the set: the table with the chairs around it, the cabinet
beside the shelf it was built to match. Today that can only be faked outside
the app.

This feature adds a **Scene**: a saved arrangement of several furniture,
opened from the hub, arranged by moving and turning whole pieces, and
rendered with the same studio render mode already gives one furniture.

## Scope decisions, and why

Five forks were settled in conversation before any design was written.

**A scene REFERENCES its furniture; it does not copy them.** Open a scene
after editing the chair and the scene shows the new chair. One source of
truth, and a scene can never show wood the user has since changed — which is
the failure a snapshot would make silent and permanent. The cost is accepted
and handled rather than designed away: a referenced furniture can be deleted
out from under a scene, and the scene has to say so loudly (see *A piece
whose furniture is gone*).

**Move and rotate, no scale.** Furniture has real dimensions. A scaled chair
is not a chair, and nothing downstream — a cut list, a joint's mark-out, a
re-measured board — would survive one. Scale is excluded deliberately, not
omitted; the existing transform gizmos offer three tools and the scene
raises two.

**Snapping to four things.** The grid and fixed angles (the app's own 10 mm /
15°), the floor, against another piece, and on top of another piece. The last
two are the ones that make a scene look *arranged* rather than *placed*, and
they are also the largest single item in the feature — they are phase 2 for
that reason, not because they are optional.

**Each furniture keeps its own wood.** The table renders in whatever it was
saved with, the chairs in theirs. A scene-wide material would have shipped
sooner and would have made an oak table with walnut chairs inexpressible,
which is an ordinary thing to want. The machinery is closer than it looks:
`OcctViewWidget::refreshWoodOverlays()` already builds one overlay per body
with its own material and simply receives the same one every time.

**A new window, and the render layer comes out of `MainWindow` to serve it.**
Rejected: a second mode inside `MainWindow`, which is 10,219 lines and whose
`updateActions()` is this app's single place availability is decided — every
term in it would gain "…and we are not in a scene", which is how that file
reached its current size. The extraction is the real cost of this feature and
also its main structural win: the render layer stops being a tenant of the
furniture editor, and a second caller is what proves the boundary is honest.
`EditorSelectorHandoff` is this project's own precedent for the same move.

## What a scene is

`SceneModel` lives in `furnify_geometry`, beside `DocumentModel`, so it is
headless-testable and reuses `CameraState` and `DocumentModel::Shot` as they
stand rather than growing near-copies.

```cpp
struct ScenePiece {
    std::string furnitureId;   // into FurnitureStore
    std::string name;          // what the scene calls this piece
    gp_Trsf placement;         // where it stands
};
```

The list may name one furniture **many times** — four chairs around a table
is four pieces naming one id, each with its own placement. That is the normal
case, not an edge case, and nothing in the model may assume ids are unique.

Beside the pieces the scene owns everything render mode owns today for a
furniture: shots, aspect, guides, light angle and strength, background
override, camera FOV, quality, export size and the cut-out switch. A scene is
a photograph's worth of settings with some furniture in it.

**`name` is the scene's, not the furniture's.** Four pieces naming one id
would otherwise be four rows reading "Oak chair", and the user could not say
which one they were about to move. It defaults to the furniture's own name
and is renamed in the scene's list.

**Placement is a `gp_Trsf`, and it holds no scale by construction.** The
gizmos the scene raises are Move and Rotate; nothing writes a scaling
transform, and the loader refuses one rather than rendering furniture at the
wrong size (see *Refusals*).

### Vocabulary

One new row for the table in CLAUDE.md:

| Concept | Word | Never |
|---|---|---|
| One furniture placed in a scene | piece | instance, item, object, copy |

"Piece" is already how this project's own prose refers to a furniture in a
set, it collides with nothing in the existing table, and it is distinct from
**body** (one 3D object inside one furniture), which a scene never addresses.

## Where a scene lives, and how it is reached

### The store

`FurnitureStore` grows scene methods rather than a second store class. It
already owns the library root, id generation, atomic writes and the
refuse-a-future-format rule; a second store would duplicate all four, and a
scene is part of the same managed library.

```
<root>/<id>/          a furniture, unchanged
<root>/scenes/<id>/   a scene: scene.json, thumb.png
```

Scenes sit in their own subdirectory so `listFurniture()` cannot return one
by accident. The layout stays **store-private**, exactly as the class header
already promises — nothing outside `FurnitureStore` touches a path inside it.

`scene.json` carries its own format version and **refuses a future one
outright**, as the furniture manifest does. Every number is clamped to the
range its own control allows and read back with its own default, so a file
written by an older build opens as a usable picture rather than a camera at
the origin looking at nothing.

### The hub

`SelectorWindow` gains a **second section below the furniture, titled
Scenes**, with its own `+` card.

One grid holding both was rejected: a card's click would mean two different
things and the user would have to know which kind they were looking at before
they knew what a click would do. Two sections make the kind obvious before
the click, and give the window two unambiguous signals:

```cpp
void sceneChosen(const QString& id);
void createSceneRequested();
```

### The handoff

`EditorSelectorHandoff::wire()` gains a third leg, following the existing
rule exactly — **show the target first, hide the source second**, so at every
observable instant at least one top-level window is visible. That rule is not
stylistic: Qt fires its last-window-closed check on a mere `hide()`, and a
posted `QEvent::Quit` cannot be taken back later in the same call stack.

Closing the scene window **quits**, the way closing the editor does since
Milestone 5 — the X is not a route back to the hub. `File → Close scene` is
the route back, mirroring `File → Close furniture`.

A scene window and a furniture editor are never open at once in this version.
Opening a furniture from within a scene is out of scope (see *Out of scope*).

## The render layer comes out of `MainWindow`

A new `RenderStudio` owns what is about *taking a picture* rather than about
*furniture*:

- the settings panel (`RenderSettingsPanel`) and its dock
- the frame and guides overlay (`RenderFrameGuides`)
- the Back chip and the app bar's slide-away
- the render tier ticker
- `setRenderModeEnabled()` / `setRenderDockOpen()` and the studio's
  entry/exit state

It is constructed over an `OcctViewWidget` and a host `QMainWindow`, and it
owns **the widgets and the viewport-facing state only**. The host still owns
its own document.

**Shots stay a host concern.** `RenderStudio` re-emits "save this view",
"apply shot N" and "forget shot N"; `MainWindow` writes them to its
`DocumentModel` and `SceneWindow` to its `SceneModel`. Pushing a shot list
down into `RenderStudio` would mean giving it an interface both document
types implement, which would drag a UI concept into `furnify_geometry` for no
gain. The boundary is already where it works today; this preserves it.

The extraction is **a move, not a redesign**. Behaviour in the furniture
editor must be identical afterwards, and that is a testable claim: the whole
existing render-mode suite has to pass unchanged across it.

## Each piece keeps its own wood

`OcctViewWidget::refreshWoodOverlays()` builds one `WoodBodyObject` per body
already, each carrying its own material, tile size and grain angle — it is
simply handed the same material for every body.

It gains a per-body material lookup that **falls back to the single live
material when a body has no entry**. The furniture editor sets no entries and
behaves exactly as it does today; the scene sets one per piece, derived from
that furniture's own saved `MaterialLook`.

This is the smallest honest version. It is deliberately not generalised into
per-body materials *inside* one furniture — that is a real feature with its
own assignment gesture and its own design round, and is out of scope here.
Nothing in this design forecloses it.

## The scene window

A third top-level window: `SceneWindow`, holding its own `OcctViewWidget`,
its own `SceneModel`, and a `RenderStudio`.

What it has:

- **A pieces list** — the Items drawer's shape, one row per piece: the
  piece's name, an eye, and a rename. No folders, no outlines, no bodies.
- **Add a piece** — a picker listing the library's furniture; choosing one
  places it at the scene's origin, on the floor.
- **Move and Rotate** — the existing `TransformGizmo` renderers, two tools of
  the three, acting on the selected piece.
- **Render** — `RenderStudio`, identical to the furniture editor's.
- **Save / Close scene** — `Ctrl+S` and the hub route, with the same unsaved
  dot and the same close question the editor uses.

What it does not have: sketching, extrude, booleans, joints, mirror, linked
copies, versions, compare, or any gesture that edits geometry. A scene
arranges furniture; it does not make it.

**Selection is per piece, never per body.** Clicking any body of a piece
selects the whole piece. The furniture editor's auto-selection — edges and
faces competing under the cursor — is deliberately absent: there is nothing
in a scene a face or an edge could be used for, and leaving it on would raise
gizmos for operations the window does not offer.

## Arranging

### Phase 1

**Move and Rotate**, the existing gizmos. Deltas apply to the piece's
`placement` transform, about the piece's own pivot.

**Grid and angle snap** follows the app's existing `Snap to Grid` setting and
its existing steps — 10 mm and 15°. One setting, one meaning, everywhere.

**Floor snap**: a piece's lowest point sits on Z = 0 unless deliberately
lifted. Derived from the piece's own measured box, so it is correct for a
turned piece, which a world bounding box would not be — this project's most
repeated bug class is an oriented quantity measured in world terms.

### Phase 2

**Against another piece** and **on top of another piece**. While a Move drag
is live, the dragged piece's measured box is tested against every other
piece's; within a grab distance, the drag result is corrected so the two
faces meet exactly rather than overlapping or stopping short.

Phase 2 is specified here only to the extent that phase 1 must not foreclose
it: placement stays a transform the snapper can correct before it is
committed, and the measured box is the one source of a piece's extents.
Phase 2 gets its own planning round.

## A piece whose furniture is gone

A referenced furniture can be deleted, renamed or fail to load. The scene
**opens anyway**:

- the piece keeps its row, with the reason printed where its name goes
- it renders nothing
- saving the scene preserves the dangling reference rather than dropping it,
  so restoring the furniture restores the piece

It is never silently removed and never silently skipped at render time. This
is the joinery rule applied one level up: a plan that quietly drops a piece
is worse than one that visibly cannot draw it, because the first produces a
picture the user believes.

A rename is not a break — the store's id is what a scene holds, and a
furniture's name is not its identity.

## Refusals

Every refusal returns a **value**, never a sentence, following
`checkMitre()`'s contract: the UI maps the value to copy, and nothing
re-derives a reason by matching substrings.

- `SceneCheck::FurnitureMissing` — the id names nothing in the store
- `SceneCheck::FurnitureUnreadable` — it exists and failed to load
- `SceneCheck::PlacementNotRigid` — a loaded placement carries scale or shear

A non-rigid placement is refused rather than normalised: a file that asks for
a 0.75× chair is either corrupt or from a build that allowed something this
one does not, and silently rendering it at the wrong size would be this app
telling a lie about a dimension.

## Testing

**Headless** (`furnify_geometry`, no window, no GPU):

- `SceneModel` round trip: pieces, repeats of one id, placements to the
  micron, names, and every render setting
- a placement carrying scale refused, with the named code
- the store's scene CRUD: create, list, load, save, rename, delete, and a
  future format version refused

**`gui_smoke`** — one independent block, its own window:

- the hub lists scenes in their own section, and `+` makes one
- opening a scene shows the scene window and hides the hub, in that order
- adding a furniture places a piece on the floor
- Move and Rotate under snap land on the step, and Scale is not offered
- a piece's row renames independently of the furniture's own name
- two pieces naming one furniture move independently
- save, close, reopen: placements identical
- a deleted furniture leaves its row with a reason and renders nothing
- the render panel in a scene is **the same class** as the editor's, with a
  shot saved in a scene restoring the scene's own camera
- each piece renders in its own wood — two furniture with different woods,
  measured as two different pixel colours in one `Dump`

**The extraction's own gate:** the entire existing render-mode suite passes
unchanged after `RenderStudio` exists. That is what makes "a move, not a
redesign" a claim rather than a hope.

## Out of scope

Named so a later reader knows these were decided rather than forgotten:

- **Opening a furniture for editing from inside a scene.** A scene window and
  an editor window are never both open in this version.
- **Per-body materials inside one furniture.** A separate feature with its
  own assignment gesture; this design does not foreclose it.
- **Rooms** — walls, floors, ceilings, lighting fixtures. A scene renders in
  the same studio one furniture does.
- **Scene-level geometry.** No sketching, no bodies of a scene's own.
- **Joints between furniture.** Joinery is within one furniture.
- **Exporting a scene to STEP.** Render and export images only.

## Phasing

**Phase 1** — a scene is a real thing: `SceneModel`, the store's scene
methods, the hub section, the third handoff leg, `RenderStudio` extracted,
`SceneWindow` with its pieces list and picker, Move and Rotate with grid and
floor snap, per-piece wood, save/load, and render.

**Phase 2** — snapping against and on top of another piece.

Phase 1 is itself large, and `RenderStudio` is its spine: nothing else in the
scene window can be built until the render layer has a second caller. It
should be the first task, and the existing suite passing unchanged is its
acceptance.
