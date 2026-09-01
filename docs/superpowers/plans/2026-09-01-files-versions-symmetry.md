# Milestone 3: Files, Versions, Symmetry and Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Furniture persists in a managed library with an init screen, named versions with side-by-side compare, live mirror symmetry via twins, inline rename, a render mode, a restyled manipulator and a bottom-bar toggle.

**Architecture:** Shape serialization joins `furnify_geometry` (Qt-free, `BinTools`); a `FurnitureStore` in the app layer owns the library and the `.furnify` manifest; the init screen and compare viewport are states of the one main window; symmetry is twin replacement, never operation replay.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1, CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-09-01-files-versions-symmetry-design.md` (items numbered as the user numbered them).

## A note on this plan's form

As in every phase since 3: interfaces, contracts, exact values and test intent fixed
here; bodies not transcribed; each task names its prior art.

## Global Constraints

- `furnify_geometry` links no Qt and no visualization toolkits. Kernel refusals never
  surface as success; `ok == false` carries a null shape/empty result.
- Vocabulary per CLAUDE.md's table (banned bare words case-insensitively: `Fuse`,
  `Solid`, `OCCT`, `mm3`, `(s)`, `Merge`, `Join`, `bevel`; word-boundary: `round`,
  `flatten`). **User-typed names are exempt from the sweep** — this milestone creates
  that distinction; the sweep must tell painted user data from painted app copy.
- No modal dialogs, ever. The init screen and compare view are states of the main
  window. Native OS file dialogs remain out of the normal flow entirely (managed
  library).
- The shell is action-driven; `updateActions()` is the single authority; slots on
  `appStateChanged` never call back in; camera sync between two views must not recurse.
- Every document change: one checkpoint, replace, Note toast with Undo; refusals are
  Failure toasts (never silenced). Save/autosave is NOT a checkpoint and never touches
  the undo stack. Version deletion is file data: second-click confirm, final, no Undo.
- Persistence in tests: never the real registry or `Documents/FurnifyMe/` — inject temp
  directories (the `ScopedTestSettings` discipline).
- Millimetres everywhere internally; `Measure` formats. Whole-device-pixel sizes and
  snapped positions for cards; no translucent pixels over the GL surface; measured
  pixels over eyeballed crops; check floor rises with every added check.
- Suite green at native, 1.25× and 1.75× (`QT_SCALE_FACTOR`); never OS input; kill
  stray exes before builds.

---

### Task 1: `.furnify` serialization and the FurnitureStore (item 2's foundation)

**Files:**
- Create: `src/FurnifySerial.{h,cpp}` (furnify_geometry target)
- Create: `src/FurnitureStore.{h,cpp}` (app target)
- Create: `tests/furnify_serial.cpp` (headless, registered in ctest)
- Modify: `CMakeLists.txt`, `src/DocumentModel.{h,cpp}`, `tests/gui_smoke.cpp`

**Interfaces:**
- Produces (Qt-free, `FurnifySerial`):
  ```cpp
  struct SerializedDocument {            // everything the kernel side owns
      std::vector<TopoDS_Shape> bodies;
      std::vector<TopoDS_Shape> outlineFaces;
      std::vector<gp_Pln>       outlinePlanes;
  };
  // Binary blob via BinTools_ShapeSet round-trip; empty vector on ok==false.
  struct SerialResult { bool ok; std::string error; };
  SerialResult writeShapes(const SerializedDocument&, std::ostream&);
  SerialResult readShapes(std::istream&, SerializedDocument&);
  ```
- Produces (app layer, `FurnitureStore` — the directory is INJECTED at
  construction, `FurnitureStore(QString rootDir)`; MainWindow passes
  `QStandardPaths::DocumentsLocation + "/FurnifyMe"`, tests pass a temp dir):
  ```cpp
  struct FurnitureInfo { QString id, name, filePath, thumbPath; QDateTime lastEdited; };
  struct VersionInfo   { QString name; QDateTime saved; };
  QVector<FurnitureInfo> listFurniture() const;    // newest first
  QString createFurniture(const QString& name);    // returns id; writes empty file
  bool saveFurniture(const QString& id, const DocumentModel& doc,
                     const QImage& thumbnail);     // manifest + shapes + thumb
  bool loadFurniture(const QString& id, DocumentModel& doc, QString* error);
  bool renameFurniture(const QString& id, const QString& name);
  // Versions live inside the same file:
  QVector<VersionInfo> versions(const QString& id) const;
  bool saveVersion(const QString& id, const QString& name, const DocumentModel& doc);
  bool loadVersion(const QString& id, const QString& name, DocumentModel& doc);
  bool deleteVersion(const QString& id, const QString& name);
  ```
- File format: a directory-per-furniture under the root
  (`<root>/<id>/manifest.json`, `shapes.bin`, `thumb.png`,
  `versions/<n>.bin` + entries in the manifest). "One `.furnify` file" in
  the spec is satisfied at the library level — the id directory IS the
  furniture; nothing outside the store touches its layout. Manifest keys:
  `format` (int, 1), `name`, `lastEdited` (ISO), per-item `names` and
  `visible` arrays for bodies and outlines, `symmetry` (Task 4 writes it;
  this task reserves the key), `versions` array.
- `DocumentModel` additions consumed by the store and later tasks:
  `itemName(id)` already exists as generated labels — this task makes names
  **stored per item** (`setItemName(id, name)`, still auto-generated on
  creation), snapshotted by the existing undo State, and gives
  `DocumentModel` a `toSerialized()/fromSerialized()` pair mapping to
  `SerializedDocument` + name/visibility vectors.

**Contract:**
- A future `format` (2+) or unreadable file refuses with `ok=false` and a
  human error string; the document is untouched on failed load (load into a
  scratch `DocumentModel`, swap on success — never half-load).
- Headless tests: round-trip a two-body + one-outline document and compare
  re-serialized bytes equal; corrupt `shapes.bin` (truncate) refuses; future
  format refuses; names/visibility survive; a version saves, lists, loads,
  deletes; store enumerates newest-first.
- gui_smoke: nothing user-visible yet — but the sweep exemption mechanism
  lands here: `DocumentModel` item names are user data; add the
  distinction hook the sweep will use (painted strings carry their origin;
  see Task 5's rename checks).

Steps: RED (headless) → implement kernel serial → GREEN → store + tests →
commit.

---

### Task 2: The init screen, save and autosave (item 2)

**Files:**
- Create: `src/ui/InitScreen.{h,cpp}` (gallery over the viewport area)
- Modify: `src/MainWindow.{h,cpp}`, `src/OcctViewWidget.cpp` (thumbnail capture
  reuses `saveSnapshot`), `tests/gui_smoke.cpp`

**Contract:**
- On launch the window shows the **init screen**: a full-bleed panel (Theme
  family card idiom, opaque paint law) with one card per `listFurniture()`
  entry — thumbnail, name, last-edited via a relative date string — plus a
  `New furniture` card. Click opens/creates and reveals the modeling UI.
  The init screen is a state: `MainWindow` gains
  `showInitScreen()/openFurniture(id)`; `updateActions()` disables every
  modeling action while it shows (extends the disjointness proof — all
  gizmo predicates already require selections that cannot exist there).
- `File → Save` (`Ctrl+S`): store save + thumbnail from the snapshot path;
  Note toast `Saved Furniture NN` (no Undo — not a document change). Title
  bar: `<furniture name> — FurnifyMe`, with `*` while dirty (dirty =
  document revision differs from last-saved revision; `revision()` is
  monotonic, record it at save).
- `File → Save automatically` (checkable, persisted in settings): on, a
  debounced (400 ms, the AppearancePanel discipline, flushed on close and
  on returning to the init screen) save runs after every checkpoint.
- `File → Close furniture` returns to the init screen; with autosave off it
  **saves first** and the toast says `Saved and closed <name>` — never a
  modal question. Closing the app mid-furniture: flush pending autosave;
  with autosave off, save likewise (the spec's ruling: never lose work,
  never block).
- New furniture naming: `Furniture NN` (store scans existing). Renaming the
  furniture: F2/double-click on the init card's name — inline QLineEdit,
  Enter commits (store rename + card repaint), Escape cancels; this is the
  same row-edit idiom Task 5 builds for the drawer, so build the shared
  helper here: `ui/InlineRename.{h,cpp}` — `beginRename(QWidget* host,
  QRect cellRect, QString current, std::function<void(QString)> commit)`;
  refuses empty/whitespace commit silently (keeps old name).
- gui_smoke (temp store injected — `MainWindow` grows a constructor store
  override, the `persistProgress=false` pattern): seeded library shows
  cards (childAt-real); opening loads the seeded shapes (volume probe);
  Save writes files and the thumb (file existence + non-empty + PNG
  header); dirty star appears on checkpoint and clears on save; autosave
  timer armed after a checkpoint (assert the armed timer, never wait);
  Close-with-autosave-off saves (file mtime/revision) and lands on init;
  New furniture opens an empty document. No modal after every one of these
  (existing no-QDialog sweep covers it).

---

### Task 3: Versions and the side-by-side compare (item 4)

**Files:**
- Create: `src/ui/VersionsPanel.{h,cpp}` (drawer, ItemsPanel's idiom)
- Create: `src/ui/SaveVersionCard.{h,cpp}` (ExtrudePreview's key-claim contract)
- Modify: `src/MainWindow.{h,cpp}`, `src/OcctViewWidget.{h,cpp}` (viewer-only
  mode), `src/CameraController.h` (nothing new — state get/set exists),
  `tests/gui_smoke.cpp`, `tests/furnify_serial.cpp`

**Contract:**
- `View → Versions` toggles the drawer (checked-state-derived visibility,
  the Items drawer's law). Rows: version name + saved date, and two
  controls per row: `Compare` and `Restore`; plus a per-row `Delete` that
  requires a second click on the same control within 4 s (`Delete — click
  again`), final, no Undo — versions are file data.
- `File → Save version…` opens `SaveVersionCard`: one field, Enter commits
  (store `saveVersion`, Note toast `Version "<name>" saved`, no Undo),
  Escape cancels; app-wide Enter/Escape claim while visible — it must join
  the disjointness proof: it requires the versions feature's own state and
  refuses to open while `ExtrudePreview` or any gizmo claim is live
  (`updateActions()` gates the action on no-pending-face, no-gizmo).
  Duplicate name refuses with a Failure toast naming the clash.
- **Compare**: `MainWindow` swaps the central widget to a `QSplitter`
  holding the live `OcctViewWidget` and a second `OcctViewWidget`
  constructed in **viewer-only mode** (constructor flag: no picking, no
  hover, no gizmos, no grid, no overlay children; displays a
  `SerializedDocument` loaded from the version). A badge (family card)
  names the version in the compare view's corner. **Camera sync both
  ways**: each widget emits `cameraChanged`; MainWindow copies state
  across only when `CameraState` differs (epsilon compare — the no-recursion
  discipline); orbiting either moves both. `Close compare` control on the
  badge returns to full width. Compare is view state, not document state:
  no checkpoint, no dirty star.
- **Restore**: closes any open compare first, then replaces the whole
  document (bodies, outlines, names, visibility) through **one checkpoint**;
  Note toast `Restored version "<name>"` with Undo. The version itself is
  unchanged.
- Version names are user text: painted in rows/badges/toasts with the
  user-data origin from Task 1 so the sweep exempts them.
- gui_smoke: save two versions; rows appear; compare opens the splitter
  (second view childAt-real, click in it selects nothing, camera sync
  measured both directions by driving one view's orbit and reading the
  other's `CameraState`, no oscillation across 3 round trips); restore
  through one checkpoint (undo returns the pre-restore document, volumes
  probed); delete needs two clicks (one click leaves it listed) and is
  final; a version named with a banned word passes the sweep (and the
  mirrored assertion: the same string as app copy fails it).

---

### Task 4: Live symmetry (item 3)

**Files:**
- Modify: `src/ModelingOps.{h,cpp}` (`mirrorShape`), `src/DocumentModel.{h,cpp}`
  (pairings), `src/MainWindow.{h,cpp}` (the twin-replacement hook),
  `src/OcctViewWidget.cpp` (symmetry-plane pick route), `src/FurnitureStore.cpp`
  (persist), `tests/direct_modeling.cpp`, `tests/furnify_serial.cpp`,
  `tests/gui_smoke.cpp`

**Interfaces:**
```cpp
// ModelingOps (Qt-free):
BooleanResult mirrorShape(const TopoDS_Shape&, const gp_Pln& plane);
// gp_Trsf::SetMirror(gp_Ax2) — reflection; result volume must be positive
// (BRepBuilderAPI_Transform handles orientation; the test pins it).

// DocumentModel:
void setSymmetry(bool on, const gp_Pln& plane);   // plane captured by value
bool symmetryOn() const;  gp_Pln symmetryPlane() const;
void pairBodies(int idA, int idB);                 // twin map, both directions
int  twinOf(int id) const;                         // -1 when unpaired
// unpairing on setSymmetry(false); pair map snapshotted by undo State
// and persisted in the manifest ("symmetry": {on, plane origin+normal, pairs}).
```

**Contract:**
- `Symmetry` is a checkable action (Modify menu + rail if a slot is free —
  the rail is at its floor-derived height; if adding a fourteenth chip
  clips on the user's screen, menu-only and ledger it). Checking it with
  the default plane (world YZ through origin) or after picking a flat face
  (`Set symmetry plane` action, the Lock to Face pick idiom, plane captured
  by value, outward-normal lesson applies) turns the mode on; the status
  label leads with `Symmetry on — …`. A faint plane indicator draws in the
  sketch-work layer while on (GridRenderer's aspect idiom, screen-sized via
  `worldPerPixel`).
- **Creation pairs**: extruding while on adds body + `mirrorShape` twin in
  the one existing checkpoint; toast names both (`Body 03 and Body 04
  created`). A created body whose bounding box straddles the plane
  (min/max signed distance spans zero with tolerance 1e-7) stays unpaired.
- **Every edit propagates**: one hook in the commit path —
  `MainWindow::commitReplaceBody(id, newShape, …)` (the single place every
  pull/bevel/transform/boolean lands; if today they land in several
  places, this task's first refactor is to route them through one) — after
  computing the edited result, if `twinOf(id) != -1`, also replace the twin
  with `mirrorShape(result)`. One checkpoint covers both; the toast names
  the operation once (`Pulled Body 03 — twin followed`  shape: the
  existing sentence plus ` — twin followed`).
- **Booleans**: operand pair (A, twinOf(A)) collapses to the single result,
  unpaired (the symmetric whole — spec ruling). A boolean between a paired
  body and an unrelated body: result keeps A's id, twin replaced by
  mirrored result. Delete on either deletes both in one checkpoint
  (`Deleted Body 03 and Body 04`).
- Turning symmetry off unpairs all (no checkpoint — pairing is not
  geometry; but it IS persisted state: mark dirty). Re-enabling does not
  re-pair.
- Headless: `mirrorShape` involution (mirror twice ≈ identity, volume
  positive and preserved, centre of mass reflected exactly);
  edit-then-mirror equals mirror-then-edit (pull a box face 20 mm both
  routes, volumes equal to 1e-6); straddle detection; pairing map
  round-trips the file including the plane.
- gui_smoke: extrude with symmetry on → two bodies, centres of mass
  reflected (probe); pull one → twin volume follows; delete one → both
  gone, one undo restores both; boolean with own twin → one unpaired body;
  toggle off → edits stop propagating; state survives save/load.

---

### Task 5: Rename, bottom bar, gizmo restyle (items 1, 7, 6)

**Files:**
- Modify: `src/ui/ItemsPanel.{h,cpp}` (row rename via `ui/InlineRename` from
  Task 2), `src/MainWindow.cpp`, `src/OcctViewWidget.cpp` (manipulator
  styling), `src/ui/Theme.{h,cpp}` (axis hue tokens), `src/ui/AxisGizmo.cpp`
  (reads tokens), `tests/gui_smoke.cpp`

**Contract:**
- **Rename (1):** double-click or F2 on an Items-drawer row (body or
  outline) opens the inline edit over the row's text cell. Enter:
  `DocumentModel::setItemName` through **one checkpoint** + Note toast
  `Renamed to "<name>"` with Undo. Escape/empty/whitespace: keep old name,
  no checkpoint, no toast. Names flow to every surface that names items
  (toasts, status label, walkthrough step text reads live names, chips) —
  these all already read `DocumentModel`, verify rather than rewire. The
  sweep: rename a body to a banned word → sweep passes (user data);
  the same string as app copy fails (both directions asserted).
- **Bottom bar (7):** `View → Show bottom bar` (checkable, default on,
  persisted with the settings family). Off hides `statusBar()`. The
  viewport minimum-height derivation must not break (it reads rail +
  margins, not the status bar — verify). A refusal while hidden still
  reaches the user: Failure toasts are already unsilenceable; assert one
  fires with the bar hidden.
- **Gizmo restyle (6):** promote the axis hues to `Theme::Spec` tokens
  (`axisX #e0564a`, `axisY #7fc84e`, `axisZ #4a80e0` — three new spec
  entries, pinned to hex in the suite like the other 21, editable in the
  Appearance panel for free since it enumerates the spec). `AxisGizmo`
  reads them. The manipulator: after `Attach`, set part colours via
  `AIS_Manipulator::SetPart`/`Attributes()` to the same tokens (translation
  arrows and rotation rings per axis; the uniform-scale handle takes
  `accent`), and slimmer proportions where the API allows
  (`SetGap`/axis radius via `Attributes()->…`— probe what OCCT 8.0
  actually exposes; whatever it does not expose stays stock and the
  ledger records the boundary). Re-style on `themeChanged` (no widget may
  cache a colour across it — the manipulator counts). Suite: Dump-pixel
  probe finds each axis token's colour on the attached manipulator
  (find-the-mark scan, never single-point), and repeats after an
  Appearance edit of `axisX` — measured pixels, never setter data (the
  zoom-persistence lesson).

---

### Task 6: Render mode (item 5)

**Files:**
- Modify: `src/OcctViewWidget.{h,cpp}` (rendering params, backdrop, 2× dump),
  `src/MainWindow.{h,cpp}` (action, exits), `src/GridRenderer.cpp` (hide),
  `tests/gui_smoke.cpp`

**Contract:**
- `View → Render mode` (checkable, session-only — never persisted; the app
  always starts modeling). On:
  - Hide: grid, axis gizmo card, rail, drawers, dimension, markers, every
    overlay child and live gizmo (detach manipulator, hide arrows);
    suppress hover and selection highlight (`ClearSelected`, deactivate
    selection modes). The viewport is the furniture alone.
  - **Tier probe at first activation**: try
    `Graphic3d_RenderingParams::Method = Graphic3d_RM_RAYTRACING` with
    shadows on; measure one redraw's wall time; if over ~100 ms, fall back
    to rasterization with shadow-mapped directional light (OCCT 8.0
    `V3d_DirectionalLight` shadow maps) — and if that fails to produce a
    shadow (Dump probe at first use), plain rasterization stands. Cache
    the tier for the session. One Note toast on entry names the tier:
    `Render mode — ray tracing` / `Render mode — shadows` /
    `Render mode`. (Copy uses none of the banned words.)
  - **Studio backdrop**: `V3d_View::SetBgGradientColors` vertical, derived
    from Theme viewport colour (lighter top, darker bottom); restored to
    flat on exit. This is OCCT-side, not a Qt translucency — the opaque
    law is untouched.
  - **Save Screenshot doubles**: while in render mode, `saveSnapshot`
    dumps at 2× the view's device-pixel size (`V3d_View::Dump` to a
    buffer of doubled dimensions).
- **Exits**: any modeling gesture — Start Sketch, a pick press in the
  viewport, any document-changing action — leaves render mode first
  (uncheck the action through `updateActions()`'s single authority);
  orbit/pan/zoom and Fit All do NOT exit (framing the shot). Toggling off
  restores everything hidden (rail, drawers per their actions' checked
  state — visibility stays derived, the sibling-visibility law).
- gui_smoke: enter render mode → grid pixels absent along a known grid
  line, rail/drawer hidden, manipulator detached with a body selected;
  screenshot file dimensions exactly 2× the normal dump's; orbit does not
  exit; a viewport press exits and restores the rail (childAt-real again);
  the tier toast fired (paintedTexts). The tier probe itself is
  environment-dependent: assert the toast names ONE of the three tiers,
  and `g_skippedByEnvironment` is NOT used here — all three tiers are
  passes, the assertion is that a tier was chosen and reported.

---

### Task 7: Document and verify

`CLAUDE.md` (the library and file layout, versions, the twin mechanism and its
boolean collapse rule, render-mode tiers, the sweep's user-data exemption, new
pitfalls found), spec status line, both suites at three scales, captures: init
screen gallery, side-by-side compare, a symmetric pair mid-pull, render-mode
shot vs modeling shot, a renamed drawer row. Present the side-by-sides.

## Self-review

Spec coverage: item 1 → T5; 2 → T1+T2; 3 → T4; 4 → T3; 5 → T6; 6 → T5; 7 → T5;
cross-cutting (store injection, sweep exemption, no-modal) → T1/T2/T3; testing
section's cases distributed to their tasks; acceptance 1–7 each land in a task.
Type consistency: `SerializedDocument`/`FurnitureInfo`/`VersionInfo` defined in
T1 and consumed by name in T2–T4; `InlineRename` built in T2, reused in T5;
`mirrorShape` signature identical in T4's two mentions. No placeholders; the
two deliberately-open probes (manipulator API surface, render tier) are scoped
with their fallback behaviour and ledger instructions rather than left vague.
