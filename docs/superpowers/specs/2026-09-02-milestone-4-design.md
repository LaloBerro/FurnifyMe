# Milestone 4 — Versions UI, selector window, mirror rework, linked copies, grids, sketch snap, render push

Approved by the user 2026-09-02. Eight user items, grouped into seven phases.

## Process rules (binding for every phase)

- **Phase-gated execution.** After each phase: suite green (gui_smoke + headless), commit,
  rebuild `furnifyme.exe`, then STOP and hand the build to the user for testing. The next
  phase starts only on the user's explicit go-ahead. No exceptions, including "trivial"
  phases.
- **Mockup-gated UI.** Before implementing any phase that adds a new visible surface
  (phases 1, 2, 3 and 7; any other phase if a new surface emerges), present **multiple
  HTML mockup options** as an artifact, iterate with the user, and treat the picked option
  as the contract. No new UI is built from a written description alone.
- All CLAUDE.md laws bind: the vocabulary table (sweep-enforced), no modal dialogs, one
  opaque paint family, device-pixel rules, derive-never-store, `updateActions()` as the
  single availability authority, Notes-can-be-silenced/Failures-cannot, `Measure` for all
  numbers, checkpoint taxonomy (document data = checkpoint+Undo; file data = second click).
- Out of scope, unchanged: custom transform gizmo (parked), FreeCAD STEP round-trip and
  Linux (parked by user decision), history tree, constraints, materials-as-documents.

## Phase 1 — Versions panel redesign (user item 1, mockup image 1)

A right-edge panel replacing the current versions drawer presentation.

- Header row: title **Versions**, and a **+** button at the top right that saves a new
  version. The name is typed inline on the newly created row (the `InlineRename` helper —
  one implementation rule), not in a separate card or dialog. Enter commits, Escape
  cancels the save entirely (no unnamed version is left behind).
- Each row is a card: **preview thumbnail + version name**. Compare, Restore and two-click
  Delete remain, reachable per row (exact affordances decided at the mockup round).
- **Version thumbnails**: captured at save time by the same snapshot machinery the
  furniture card thumbnail uses, stored inside the furniture's directory
  (`versions/<id>.png` beside `versions/<id>.bin`), indexed in the manifest. A version
  with no thumbnail (all pre-phase versions) renders a neutral placeholder — the manifest
  stays forward-compatibly read, never refused for a missing key.
- Restore still closes compare first; restore is one undoable checkpoint; version delete
  stays file data (two-click confirm, no undo). Version names are user data for the
  vocabulary sweep; the panel's own copy is not.

## Phase 2 — Selector window (user item 2, mockup image 2)

The init screen becomes its own top-level window; the editor is a second window.

- On launch the **selector window** alone appears: a grid of large rounded cards — the
  first card is **+** (create new furniture), the rest show a large preview with a name
  bar beneath (mockup image 2's layout; final look decided at the mockup round).
- Inline rename on cards is kept (same `InlineRename` helper), as is every capability the
  current init screen has (open, create, delete if present today).
- Handoff: opening a furniture hides the selector and shows the editor window; closing
  the editor saves first (existing close-saves-first law, autosave rules unchanged) and
  returns to the selector, refreshed (new thumbnails, names, timestamps).
- The in-`MainWindow` init-screen state is retired; `FurnitureStore` remains the only
  file authority. `gui_smoke` keeps driving both windows in-process; no OS input.
- The editor's `File → <back to library>` route (whatever it is named today) now performs
  the same handoff instead of an in-window state switch.

## Phase 3 — Mirror tool rework (user item 3, modes A/B/C)

Symmetry becomes a visible, retroactive, aimable tool. The twins-not-replay engine
(`mirror(edit(A))` replaces the twin; `symmetryOn()` gates every read) is unchanged
underneath; what changes is how pairings are created.

- Flow: select one or more bodies → **Mirror** (Model menu; `S` rebound here; rail stays
  at its current count — the rail-floor rule) → a **visible plane** appears through the
  selection's combined centre (one body = its centre — user mode A; several = combined —
  user mode C) → the user may drag the plane along its normal and switch it between the
  three axis-aligned orientations (user mode B) → **Enter confirms, Escape cancels** (an
  app-wide key claim; its visibility predicate must stay provably disjoint from
  `ExtrudePreview`, the pull arrow, the bevel arrow and the transform gizmo claims).
- On confirm every selected body gets a live twin mirrored through the placed plane,
  **from that moment on** — retroactive pairing, the headline fix. One checkpoint, one
  toast naming the count. A body straddling the placed plane is skipped with the toast
  saying so (existing straddle rule).
- The placed plane becomes the live symmetry plane; later edits maintain twins exactly as
  today. Turning Mirror off unpairs (existing semantics: no checkpoint, revision bump).
- The plane gizmo's look goes through the mockup round.
- The ledgered `bevelAxis`/`outwardNormalNear` mirrored-body risk (CLAUDE.md) is
  re-checked in this phase's tests, since mirrored bodies now become common.

## Phase 4 — Linked copies (user item 3, mode D; user chose BOTH creation routes)

N bodies declared "the same shape, placed differently"; editing any member re-derives all.

- **Duplicate linked**: clones the selected body as a linked copy, slightly offset,
  transform gizmo attached for placement. Placement is rigid (translate + rotate);
  uniform scale bakes into the shared shape and so propagates to the whole group.
- **Link selected**: two or more existing bodies; the **first-selected is the anchor**
  and shape source; the others' shapes are replaced by the anchor's shape placed at each
  member's position (v1 placement is translation-only, centre to centre — stated in the
  confirm toast, one undoable checkpoint). Rotated variants want Duplicate linked.
- Edit propagation: `edit(member)` → the anchor shape is re-derived through the member's
  inverse placement → every member re-derived through its own. One gesture, one
  checkpoint, one toast naming the group size. **Unlink** dissolves a group (checkpoint).
- Group membership and placements live in `DocumentModel::State` (checkpointed, like the
  twin map). A body cannot be both in a link group and a mirror pair in v1 — the second
  request is refused with a toast that says why (taxonomy kept clean; revisit later).
- Geometry mechanism is Qt-free in `ModelingOps`/`DocumentModel` and headless-tested:
  place/unplace round-trip, propagation correctness, undo restoring group and placements.

## Phase 5 — Grids (user items 4 + 5)

- **Face-on ortho grids**: when the effective projection is orthographic and the camera
  is square onto Front/Back/Left/Right, the grid is built on the matching vertical plane
  through the origin (user image 3). Top/Bottom and free orbits keep the ground grid. A
  locked face's plane still outranks everything (existing rule). `GridRenderer` is
  already plane-generic; this phase only changes which plane the caller supplies.
- **Grid density setting**: the zoom level where grid cells merge becomes tunable — a
  multiplier (0.5×–2×, default 1×) applied in `GridRenderer::minorStepFor`'s
  distance→step mapping. Lives in the Appearance panel beside Text size, persisted with
  the spec, serialized with the same refuse-out-of-range rule every token gets.

## Phase 6 — Sketch snap + top view (user items 6 + 7)

- **Shift in sketch = 8-direction snap**: the in-progress segment snaps to the nearest of
  the 8 directions at 45° steps (starting at 0° = the sketch plane's +u axis), measured
  from the segment's start toward the cursor. This replaces the previous-segment-direction
  rule. Snap to Grid then snaps the point along the chosen direction (parameter snap, as
  today); the close-hit on the first point still outranks the constraint. Headless-tested
  in `CameraController`/sketch math where the geometry lives.
- **Top view alignment (bug)**: from the axis gizmo's top face, the camera is visibly not
  square onto -Z (side faces show — user image 4). Root-cause first (prime suspect: the
  turntable elevation clamp stopping short of ±90°, which would tilt every "Top" by the
  clamp margin; second suspect: the gizmo's snap pose), fix at the cause, and pin with a
  check that the Top view's direction is exactly (0,0,-1) — and same for the other five
  named views while at it.

## Phase 7 — Render mode pushed (user item 8; user accepted progressive)

- **Path-tracing tier** above the current three: `Graphic3d_RM_RAYTRACING` with global
  illumination and adaptive screen sampling — progressive refinement, grainy while
  orbiting, clean within ~1–2 s at rest. The tier probe grows a fourth step
  (PathTracing → RayTracing → Shadows → Plain), still measured, still cached per
  session, still named in the Note toast.
- **PBR look**: PBR shading model and tone mapping while render mode is on; bodies get a
  render material with roughness/metallic. All render-params changes are entry/exit
  scoped and fully restored, like the lights today.
- **Render settings panel** (mockup round): modeless, render-mode-only. Controls, v1:
  material roughness + metallic (applied to all bodies), background colour, light
  azimuth/elevation + intensity, camera FOV (render-mode override of the fixed 45°,
  restored on exit). Values persist via `QSettings`; render mode itself remains
  session-only and never auto-starts.
- **Camera button** (mockup round): a floating corner control, visible only in render
  mode, triggering the existing Save Screenshot action (2× export). It obeys the
  overlay-sibling laws (opaque paint family, `WA_NoMousePropagation`, device-pixel size
  and position).
- The exit-on-left-click gesture must not swallow clicks on the panel or the button —
  both are Qt siblings above the viewport, so they take their clicks natively; only a
  press reaching the viewport exits.

## Acceptance

Per phase: gui_smoke green (with new pins for that phase's behaviour), headless suites
green, vocabulary sweep clean, CLAUDE.md updated, one commit series, exe rebuilt, user
test passed. The milestone is done when the user signs off phase 7.
