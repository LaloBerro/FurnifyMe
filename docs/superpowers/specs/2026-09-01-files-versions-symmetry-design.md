# Milestone 3: Files, versions, symmetry and render — design

Date: 2026-09-01
Status: implemented 2026-09-02. The user listed seven items, answered four scoping questions
(live symmetry over one-shot mirror; side-by-side over ghost overlay; restyle the
stock manipulator; managed library), and approved the sectioned design. Numbers
below are the user's own numbering.

Two deviations from this design, both API/measurement boundaries rather than scope cuts,
documented at their read site in `src/OcctViewWidget.cpp`:

- **Item 6, the manipulator restyle, ships with stock hues AND stock proportions**, not
  slimmer proportions on the spec's own palette. `AIS_Manipulator.hxx` (OCCT 8.0.1) has no
  colour setter reachable at any access level, which the spec anticipated ("where the API
  does not reach, the stock look stays"); proportions looked reachable via a subclass
  (`protected Axis myAxes[3]` + public `SetAxisRadius()`), but a measured `Dump`-pixel probe
  of that subclass showed the rendered cross-section growing as the radius shrank rather than
  shrinking, so it was reverted rather than shipped. The 2D `AxisGizmo` card carries the
  palette instead.
- **The three axis-hue tokens are named `gizmoAxisX`/`gizmoAxisY`/`gizmoAxisZ`**, not the
  design's `axisX`/`axisY`/`axisZ` — those names were already taken by the ground grid's own
  axis tint tokens.

## The seven items, as decided

### 2 — Furniture files, the init screen, autosave

A **`.furnify` file is one furniture.** It contains a manifest — format version,
per-item names and visibility, the symmetry state (on/off, plane, pairings), and
the versions index — plus the shapes serialized with OCCT's own `BinTools`
(exact B-rep, no tessellation loss). One file, self-contained; versions (item 4)
live inside it.

- **Managed library:** the app owns `Documents/FurnifyMe/` (created on demand;
  `QStandardPaths::DocumentsLocation`). No file dialogs in the normal flow.
- **The init screen** replaces the empty viewport at launch: one gallery card per
  furniture — thumbnail (captured at save via the existing snapshot path), name,
  last-edited date — plus **New furniture**. Clicking a card opens it; the window
  title carries the furniture's name. The init screen is a **state of the main
  window**, not a dialog: the no-modal law stands.
- **Save** (`Ctrl+S`, File menu) writes the file and refreshes the thumbnail.
  **Autosave** is a checkable, persisted option: on, a save runs after every
  checkpoint, debounced (the theme panel's 400 ms discipline — a drag fires per
  move). Save is not a checkpoint itself and never touches the undo stack.
- **Unsaved work on close or on returning to the init screen** gets a
  toast-based flow, never a modal: with autosave on there is nothing to ask;
  with it off, returning to the init screen saves first (a library app's files
  are not precious enough to lose work over a missed dialog), and the toast
  names what happened.
- A furniture that fails to load (corrupt, future format version) is refused
  with a Failure toast naming the file; the init screen stays. Never a crash,
  never a half-loaded document.
- **Naming:** a new furniture is `Furniture NN`; renaming it uses item 1's
  rename-in-drawer idiom applied to the title / init card (F2 or double-click
  on the init card's name).

### 4 — Versions with side-by-side compare

A **version is a named snapshot stored inside the furniture's file**: shapes,
item names, visibility — captured on demand, never automatically.

- **Save version…** opens a modeless card (the `ExtrudePreview` family: field,
  Enter commits, Escape cancels, app-wide key claim while visible) to name it.
- A **Versions drawer** (toggled like the Items drawer) lists versions with name
  and date. Each row: **Compare** and **Restore**.
- **Compare** opens a second, **read-only** viewport beside the live one in a
  splitter: the version's shapes, a badge naming the version, **cameras synced
  both ways** — orbiting either turns both. No gizmos, no selection, no hover,
  no grid toggles in the second view; it is a viewer. Closing the compare
  returns the full-width viewport. Camera sync must not recurse: each side
  applies the other's state only when it differs (the `appStateChanged`
  no-reentry discipline).
- **Restore** replaces the current document with the version through **one undo
  checkpoint** — an accidental restore is undoable. Restore does not delete or
  alter the version; versions are only removed by an explicit Delete on the row
  (toast with Undo? No — versions are file data, not document data: deleting a
  version is confirmed by a second click on the same control, "Delete —
  click again to confirm", and is final. The undo stack governs the document
  only.)
- Version names are user text: exempt from the banned-word sweep, like item 1's
  names.

### 3 — Live symmetry

**Twins, not replayed operations.** The mechanism that makes live symmetry
tractable without a history tree:

- `Symmetry` is a mode (toolbar/menu action, checkable) with a **symmetry
  plane** — default: the world YZ plane through the origin (down a furniture's
  middle); settable the way Lock to Face sets the sketch plane (pick a flat
  face; its plane, captured by value, becomes the symmetry plane).
- While on, **every body created gets a twin**: its exact mirror through the
  plane (`gp_Trsf` mirror — a reflection, which `BRepBuilderAPI_Transform`
  applies exactly). An outline drawn while symmetry is on extrudes into a
  paired body (one gesture, both bodies).
- **Any edit to either paired body — pull, bevel, transform, boolean, rename
  aside — finishes by replacing the twin with the mirror of the result.** No
  operation replay, no face correspondence: mirror(edit(A)) is definitionally
  the correct twin of edit(A). One gesture, **one checkpoint covering both
  bodies, one toast** naming both.
- **Deleting one deletes both** (one checkpoint). **Turning symmetry off
  unpairs** — bodies remain as ordinary bodies; turning it back on does not
  re-pair existing bodies (pairing happens at creation only).
- A body that **straddles the plane** at creation is left unpaired — the user
  is modeling on the centreline. A boolean between a paired and an unpaired
  body produces an unpaired result on the edited side and its twin is replaced
  by the mirrored result as usual — unless the *other* operand was the twin
  itself, in which case the pair collapses to the single result, unpaired (a
  body fused with its own mirror is the symmetric whole).
- Pairings, the plane, and the mode persist in the `.furnify` file.
- **Kernel:** `ModelingOps::mirrorShape(shape, plane)` — Qt-free,
  headless-tested (mirror twice = identity within tolerance; volume preserved;
  centre of mass reflected exactly). Reflection reverses orientation;
  `BRepBuilderAPI_Transform` handles this — the test asserts the mirrored
  solid's volume is *positive*.

### 1 — Rename, 7 — bottom bar, 6 — gizmo restyle

- **Rename (1):** double-click or F2 on an Items-drawer row opens an inline
  `QLineEdit` in the row; Enter commits, Escape cancels. Names persist in the
  file; every surface that names an item (toasts, walkthrough, status label,
  chips) shows the user's name. Renaming is a document change: checkpoint +
  Note toast with Undo. Empty or whitespace-only input is refused (keeps the
  old name, no toast). **User-typed names are exempt from the banned-word
  sweep** — the vocabulary law governs our copy, not theirs; the sweep must
  distinguish painted user data from painted app copy.
- **Bottom bar (7):** `View → Show bottom bar`, checkable, persisted with the
  settings family. Hiding it hides the status bar; refusals still reach the
  user via Failure toasts (which the notifications toggle already cannot
  silence), so nothing goes quiet.
- **Gizmo restyle (6):** restyle `AIS_Manipulator` to the axis widget's
  palette — X `#e0564a`, Y `#7fc84e`, Z `#4a80e0` (today's untokenised axis
  hues; tokenising them is in scope here since two surfaces now share them) —
  and slimmer part proportions, as far as the manipulator's API allows
  (`SetPart`/aspect colours and geometry sizing exist; where the API does not
  reach, the stock look stays and the spec says so rather than promising
  pixels). The axis-gizmo card and the manipulator must visibly agree on hue.

### 5 — Render mode

`View → Render mode`, checkable. On:

- **Hides** the grid, axis gizmo card, rail, drawers, dimension, markers,
  overlays and any live gizmo; suppresses hover/selection highlights. The
  viewport is the furniture alone.
- **Switches the scene to OCCT ray-traced rendering with real shadows**
  (OCCT 8.0 `Graphic3d_RenderingParams`); ambient occlusion arrives with the
  path-tracing tier if this GPU sustains it interactively — the implementation
  probes at first activation and picks the best tier, with shadow-mapped
  rasterization as the honest fallback. The chosen tier is reported once in a
  Note toast ("Render mode — ray tracing" / "— shadows"), so the user knows
  what they are looking at.
- **A neutral studio backdrop** — soft vertical gradient — replaces the flat
  viewport colour while active.
- **Save Screenshot exports at 2× resolution** while render mode is on
  (`V3d_View::Dump` at doubled dimensions).
- Any modeling gesture (starting a sketch, picking) exits render mode back to
  the normal scene; orbit/pan/zoom do not — you frame your shot.
- The mode is session-only, never persisted: the app always starts in
  modeling.

## Cross-cutting decisions

- **`FurnitureStore` (Qt-free where possible, storage injected)** owns the
  library: enumerate, load, save, thumbnails paths. Serialization of shapes
  via `BinTools` lives beside `ModelingOps` in `furnify_geometry` (no Qt);
  manifest read/write may use Qt JSON in the app layer. The suite never
  touches `Documents/FurnifyMe/` — tests inject a temp directory (the
  `ScopedTestSettings` discipline).
- **`DocumentModel` gains identity**: stable item ids already exist; names
  become mutable; symmetry pairings are document state (persisted, versioned,
  snapshotted by undo checkpoints like everything else).
- The init screen, versions drawer, rename edit, and save-version card are all
  action-driven and modeless; `updateActions()` remains the single authority
  (Save enabled when a furniture is open; Compare closes before Restore runs;
  gizmos hidden on the init screen and in render mode — the disjointness
  proof extends with two new "off" states rather than new claims).
- Every new document-changing operation follows the standing contract: one
  checkpoint, replace, Note toast with Undo, Failure toast on refusal, no
  modals anywhere.

## Non-goals

No cloud sync, no multi-window, no version diffing beyond side-by-side viewing,
no per-axis symmetry groups (one plane), no re-pairing of existing bodies when
symmetry re-enables, no materials/textures in render mode (lighting and shadows
only), no STEP import on the init screen, no auto-versioning.

## Testing

- **Headless:** `.furnify` round-trip (shapes bit-comparable via `BinTools`
  re-serialization, names/visibility/pairings/versions preserved; corrupt and
  future-version files refused with `ok == false`); `mirrorShape` (involution,
  positive volume, reflected centre of mass); twin-replacement maths
  (edit-then-mirror equals mirror-then-edit on a box pull); version
  snapshot/restore on `DocumentModel`.
- **`gui_smoke`:** init screen shows cards for a seeded temp library and opens
  one; save writes a file and a thumbnail into the temp library; autosave
  fires after a checkpoint (debounce asserted by timer, not real waiting);
  rename via the drawer row propagates to a toast naming the new name; the
  banned-word sweep exempts user-typed names (a name containing a banned word
  passes the sweep exactly where app copy would fail it — asserted both ways);
  versions: save, list, compare opens the second viewport (camera sync both
  directions, no recursion, read-only — a click in the compare view selects
  nothing), restore through one checkpoint and undo returns; symmetry: create
  → twin exists mirrored (centre-of-mass probe), pull one → other follows,
  delete one → both gone, one checkpoint per gesture throughout; render mode
  hides grid/overlays (pixel probes), exits on a modeling gesture, screenshot
  doubles its dimensions; bottom-bar toggle hides the status bar and
  persists; manipulator hue matches the axis card's tokens (Dump pixel probe,
  the zoom-persistence lesson: measure pixels, never setter data).
- Both suites green at native, 1.25× and 1.75× throughout; the check floor
  rises with every added check.

## Acceptance criteria

1. Launch shows the init screen; furniture opens, saves (manual and autosave),
   and round-trips losslessly through `.furnify` in a managed library with
   thumbnails.
2. Named versions save inside the file, list, compare side-by-side with synced
   cameras, and restore through one undoable checkpoint.
3. Live symmetry pairs new bodies with mirror twins; every edit to either is
   mirrored to the other in the same gesture, checkpoint and toast; straddling
   bodies stay unpaired; state persists in the file.
4. Items rename inline, names persist and appear on every surface, and the
   vocabulary sweep exempts user text.
5. Render mode strips the scene to the furniture, adds real shadows (best tier
   the GPU sustains), studio backdrop, and 2× screenshots; any modeling
   gesture exits it.
6. The manipulator wears the axis palette; the bottom bar toggles and
   persists.
7. Both suites green at three scales throughout; every kernel addition is
   Qt-free and headless-tested.
