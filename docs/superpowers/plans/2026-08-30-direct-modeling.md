# Milestone 2: Direct Modeling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Edit geometry by grabbing it — pull faces, transform bodies with a gizmo, drag edges into fillets or chamfers — and open every theme colour and font to the user.

**Architecture:** Kernel operations land first in Qt-free `ModelingOps` with headless tests; three selection-driven gizmos build on them (`PullArrow`, `AIS_Manipulator` wiring, `BevelArrow`), each committing through document-replace + undo + toast; `Theme` becomes spec-backed with a floating Appearance panel.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1 (`BRepFilletAPI`, `AIS_Manipulator`), CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-08-30-direct-modeling-design.md`

## A note on this plan's form

As in Phases 3–5: interfaces, behaviour contracts, exact values and test intent are fixed
here; widget bodies are not transcribed. Each task names its prior art — the repo already
solves most of these shapes once.

## Global Constraints

- **`furnify_geometry` links no Qt.** `ModelingOps` gains every kernel operation this
  milestone; a Qt header there is Critical. Never surface a failed kernel operation as
  success — `ok == false` stops the UI path.
- **Previews are built by the commit's own kernel call** (`ExtrudePreview`'s law), and
  gizmo previews use a **dedicated presentation channel**, never the sketch/extrude
  `setPreview` slot — two features sharing that slot already cost one bug.
- **Every commit that changes the document** replaces the body via the existing
  undo-checkpoint path and reports through a toast offering Undo.
- **Vocabulary:** banned case-insensitively — `Fuse`, `Solid`, `OCCT`, `mm3`, `(s)`,
  `Merge`, `Join`, and now `bevel` in user-visible text. New words: **Fillet** (rounds),
  **Chamfer** (flattens), **Move / Rotate / Scale**, **Appearance**. Bodies are Body;
  lengths go through `Measure`; status text takes no trailing period; em dashes join
  clauses.
- **The shell is action-driven; no new rail buttons.** Gizmos are selection-driven; the
  Appearance action lives in the View menu only.
- **No translucent pixels over the GL surface** (the toast fade stays the one documented
  exception). New overlay widgets use `Theme::paintSurface()` with its ground fill,
  `WA_NoMousePropagation`, derived visibility, `childAt`-verified hit-testing, placement
  off `ViewportOverlay::laidOut()` where anchored.
- **Appearance is verified with measured pixels**, never by eyeballing a crop.
- `MainWindow::appStateChanged()` fires at the end of `updateActions()`; no slot on it
  may call back in. Checks are updated, never deleted; the suite must not lose coverage
  (currently 618 + headless 5/5). Never use OS-level synthetic input — gizmo drags are
  synthesised as in-process Qt mouse events.
- Build: `cmake --build --preset windows`; run `.\build\RelWithDebInfo\gui_smoke.exe <dir>`
  directly (GPU, not in ctest); headless via `--preset windows-headless`. Kill stray
  `furnifyme.exe`/`gui_smoke.exe` before building.
- Every UI task ends with a `PrintWindow` capture (`PW_RENDERFULLCONTENT`, capture only)
  magnified at the gizmo, compared against the corresponding reference image's
  *interaction anatomy* (arrow, field, label placement) — not its exact pixels.

---

### Task 1: The kernel operations

**Files:**
- Modify: `src/ModelingOps.h`, `src/ModelingOps.cpp`
- Test: `tests/headless_geometry.cpp` (or a new registered `tests/direct_modeling.cpp` —
  implementer's choice, registered with ctest either way)

**Interfaces produced (all in `namespace ModelingOps`, all Qt-free):**
```cpp
// Pull a planar face of `body` by `distance` along its OUTWARD normal.
// Positive grows (prism fused on), negative carves (prism cut away).
// Refuses: a null/non-planar face, |distance| < 1e-7, a carve that consumes
// the body entirely (result empty or volume ~0), and any kernel failure.
BooleanResult pullFace(const TopoDS_Shape& body, const TopoDS_Face& face,
                       double distance);

// Round one edge of `body` with radius r / flatten it with distance d.
// Refuses r/d <= 0 and any BRepFilletAPI failure (IsDone false, null or
// empty result) - OCCT fillets legitimately fail on hard geometry and that
// refusal must carry through as ok == false, never as a success.
BooleanResult filletEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                         double radius);
BooleanResult chamferEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                          double distance);

// Bake a rigid transform (+ uniform scale) into the shape's geometry.
// gp_Trsf carries rotation/translation/uniform scale; refuses a transform
// whose scale factor is <= 0.
BooleanResult transformShape(const TopoDS_Shape& body, const gp_Trsf& trsf);
```

**Behaviour contract:**
- `pullFace` derives the **outward** normal itself (reverse the plane when the face is
  `TopAbs_REVERSED` — the Phase 4 lesson, already proven in `lockToFace`); callers pass
  a signed distance in that outward frame. Result goes through
  `ShapeUpgrade_UnifySameDomain` like the booleans, so pulled faces do not accumulate
  junk edges.
- All four return the existing `BooleanResult{ok, shape, error}`; `error` strings are
  kernel-facing (the UI rewrites them - they never reach the user raw).
- `transformShape` uses `BRepBuilderAPI_Transform` with `copy = true`.

- [ ] **Step 1 — failing tests.** Exact numeric assertions on a known box
  (100×80×10 at origin):
  - pull the top face +5 → one solid, volume grows by exactly 100·80·5;
    centre of mass rises by the right amount; still 6 faces after unify.
  - pull the top face −5 → volume shrinks by 100·80·5.
  - pull the top face −10 (the full thickness) → refused, `ok == false`.
  - pull a null face / zero distance → refused.
  - fillet one long edge r=3 → volume shrinks by exactly (1 − π/4)·9·100
    (tolerance 1e-3 relative); result valid (`BRepCheck_Analyzer`).
  - chamfer the same edge d=3 → volume shrinks by exactly 0.5·9·100.
  - fillet r=20 on the 10-thick box → refused, `ok == false`, body untouched.
  - transform: translate (10,20,30) moves the centre of mass exactly, volume
    unchanged; rotate 90° about Z maps extents (x,y)→(y,x); scale 2.0 gives
    volume ×8.
- [ ] **Step 2 — RED** (headless preset only; no GPU needed).
- [ ] **Step 3 — implement.** Read `ModelingOps::applyBoolean` and `extrude` first —
  the fuzzy-value, unify and refusal idioms are established there.
- [ ] **Step 4 — GREEN**, headless all-pass; also build the app preset to prove no
  call-site breakage.
- [ ] **Step 5 — commit.**

---

### Task 2: Face pull

**Files:**
- Create: `src/ui/PullArrow.h`, `src/ui/PullArrow.cpp`
- Modify: `CMakeLists.txt`, `src/OcctViewWidget.{h,cpp}`, `src/MainWindow.{h,cpp}`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `ModelingOps::pullFace` (Task 1), the outward-normal derivation already in
  `MainWindow::lockToFace`, `OcctViewWidget::worldPerPixel()`, the toast, the undo path.
- Produces: `PullArrow` — a screen-space overlay widget (family card is wrong here; it
  is an arrow + field, prior art `ExtrudePreview` for the field contract and
  `DimensionRenderer` for screen-space sizing) positioned at the projected face centre;
  `OcctViewWidget::setModelingPreview(const TopoDS_Shape&)` — the **dedicated preview
  channel**, selection-mode −1, never the sketch slot.

**Behaviour contract:**
- Appears when face mode has **exactly one planar face** selected and no sketch is
  active and no face is pending; disappears the moment that stops holding
  (`appStateChanged` drives it; the predicate is one function used to show and to hide).
- Drag maps the cursor to a signed distance along the outward normal: closest-point
  parameter of the mouse ray against the normal line through the face centre — the
  maths lives in a small Qt-free helper if it is more than a few lines.
- Live preview on every drag/edit through `pullFace` itself; the preview shape goes to
  `setModelingPreview`, never `setPreview`. Invalid or refused values keep the last good
  preview and mark the field (`ExtrudePreview`'s exact idiom, including the
  `ShortcutOverride` Enter/Escape claim while visible).
- Release after a real drag commits; Enter commits a typed value; Escape cancels and
  clears the preview channel. Commit path: `pullFace` → refuse via Failure toast
  (cause-and-fix wording) or replace body through the existing undo-checkpoint route →
  Note toast with Undo → `updateActions()`.
- The status label teaches while the arrow is up: `Face selected — drag the arrow to
  pull, or type a distance`.
- Snapping: when Snap to Grid is on, the drag distance snaps to the 10 mm step.

- [x] **Step 1 — failing test:** select a face on a known body (derive the click from
  projected geometry, assert the pick); arrow appears (`childAt` identity) with its
  field; synthesise an in-process drag on the arrow — press, move, release; assert the
  document body's extents grew by the dragged amount (within the snap step), the toast
  offers Undo, Undo restores; repeat inward and assert carve; type `-999` (a full
  carve) and assert refusal with body untouched; Escape leaves no modeling preview
  (`hasModelingPreview()` accessor).
- [x] **Step 2 — RED.** — [x] **Step 3 — implement.** — [x] **Step 4 — GREEN** (full
  suite + headless). — [x] **Step 5 — capture**, magnified at the arrow, against image
  1's anatomy. — [x] **Step 6 — commit.**

---

### Task 3: The transform gizmo

**Files:**
- Modify: `CMakeLists.txt` (if a new file), `src/OcctViewWidget.{h,cpp}`,
  `src/MainWindow.{h,cpp}`; create `src/ui/TransformGizmo.{h,cpp}` if the wiring wants
  its own home
- Test: `tests/gui_smoke.cpp`

**Behaviour contract:**
- `AIS_Manipulator` attached when **exactly one body** is selected in body mode, no
  sketch active, no face pending; detached otherwise. One derived predicate.
- Modes: translation (axes + planes as the API provides), rotation, uniform scaling.
- During drag the manipulator transforms the presentation (its `StartTransform` /
  `Transform` flow); on release the accumulated `gp_Trsf` is snapped (if Snap on:
  translation to 10 mm, rotation to 15°, scale to 5%) and baked through
  `ModelingOps::transformShape`, replacing the body with undo checkpoint and a Note
  toast with Undo. On refusal the presentation resets to the document shape.
- The manipulator must not fight the camera: RMB orbit and MMB pan pass through
  untouched; only LMB on manipulator parts drags it. Verify the existing camera checks
  still pass.
- Styling: accent-tint its parts where `AIS_Manipulator`'s aspects allow; stock
  appearance is acceptable where not. It is an AIS object, exempt from the Qt opacity
  sweeps.
- Snap reads the existing Snap action's checked state — no second toggle.

- [ ] **Step 1 — failing test:** one body selected → manipulator displayed
  (`OcctViewWidget` exposes `hasManipulator()`); two bodies → gone; sketch → gone.
  Synthesise a drag on the Z translation arrow's projected screen position; assert on
  release the body's centre of mass moved by a multiple of the snap step with volume
  unchanged, toast offers Undo, Undo restores. Toggle Snap off, drag again, assert a
  non-multiple move is possible (assert the exact dragged delta rather than a grid
  multiple). Scale: drag the scale handle, assert volume scaled by the cube of a 5%
  multiple with Snap on.
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture** at the gizmo, against image 2's anatomy. — [ ] **Step 6 —
  commit.**

---

### Task 4: Bevels

**Files:**
- Create: `src/ui/BevelArrow.h`, `src/ui/BevelArrow.cpp`
- Modify: `CMakeLists.txt`, `src/OcctViewWidget.{h,cpp}`, `src/MainWindow.{h,cpp}`
- Test: `tests/gui_smoke.cpp`

**Behaviour contract:**
- Appears when edge mode has exactly one straight edge selected on one body, no sketch,
  no pending face. `PullArrow` is the prior art — same field contract, same key claims,
  same dedicated preview channel, same commit shape.
- The drag axis is perpendicular to the edge, along the bisector of the two adjacent
  faces' outward normals. **Dragging inward (against the bisector) fillets with radius
  r; dragging outward chamfers with distance d.** The label reads `R 2 cm` or `C 2 cm`
  through `Measure::formatLength`, and the field names the kind: `Fillet` / `Chamfer`.
- Live preview through `filletEdge`/`chamferEdge` themselves. A kernel refusal mid-drag
  keeps the last good preview and marks the field; a refusal on commit is a Failure
  toast in cause-and-fix form:
  `"This edge can't take a fillet that big — the curve would eat a neighbouring face. Try a smaller size."`
  (same sentence with Chamfer for the chamfer case). The body is untouched on refusal.
- The dimension annotation from Phase 4 (edge length) clears while the bevel arrow is
  up — two annotations on one edge is noise; it returns when the arrow goes.
- Commit on release or Enter; undo checkpoint + Note toast with Undo.

- [ ] **Step 1 — failing test:** select a long edge of a box body (projected-geometry
  click, pick asserted); arrow + field appear (`childAt`); drag inward and assert on
  release the volume shrank by the fillet formula for the dragged radius (snap-stepped),
  the label carried `R`, the result body has more faces than before (the fillet strip);
  Undo restores; drag outward and assert the chamfer volume delta and `C` label;
  type a radius larger than the body thickness and assert the Failure toast fired and
  the body is byte-identical (same volume, same face count).
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture** against images 3–5's anatomy. — [ ] **Step 6 — commit.**

---

### Task 5: The Appearance panel

**Files:**
- Create: `src/ui/AppearancePanel.h`, `src/ui/AppearancePanel.cpp`
- Modify: `CMakeLists.txt`, `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/MainWindow.{h,cpp}`,
  `src/OcctViewWidget.cpp` (viewport background + grid rebuild on theme change)
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
namespace Theme {
// The editable state behind every token. Defaults are today's Graphite
// constants; colour accessors read it. Fonts derive from base:
// badge = base-2, label = base-1, body = base, title = base+3 (pt).
struct Spec { /* one QColor per token; QString fontFamily; double basePt; */ };
const Spec& spec();
void setSpec(const Spec& next);      // applies stylesheet + broadcasts
Spec defaultSpec();                  // Graphite
QString serializeSpec();             // for QSettings, one string
bool deserializeSpec(const QString&, Spec& out);   // tolerant, false on garbage
}
// MainWindow: signal void themeChanged(); Appearance action in the View menu.
```

**Behaviour contract:**
- The panel is a scrollable floating family card (prior art: the items drawer for the
  card + anchoring; it may be wider). One row per colour token — name, swatch, and a
  click opening a **modeless** `QColorDialog` (`open()`, not `exec()`); a family combo;
  a base-size control (spin or slider, 8–14 pt). Every edit applies **live**.
- Applying: `setSpec` re-installs the stylesheet, updates the app font, emits the
  broadcast; `MainWindow` relays: viewport background, grid rebuild, `updateActions()`
  so every painted widget repaints. No widget caches a colour across the broadcast.
- **The type-scale sweep adapts**: it already reads the four fonts from `Theme`; confirm
  no check hardcodes 8/9/10/13 pt literals — any that does is updated to read the
  derived values.
- Persistence under the `persistProgress` guard, beside progress and the unit. Reset
  button restores `defaultSpec()` and persists.
- The panel's own copy joins the vocabulary sweep via `paintedTexts()`; token names in
  the panel are user-words (`Viewport`, `Panels`, `Accent`, `Text`…), not code names,
  and contain no banned word.

- [ ] **Step 1 — failing test:** `View → Appearance…` opens the panel (visible,
  `childAt`); programmatically set the accent token via the panel's API and assert a
  rail chip's checked ring pixel now samples the new colour; set the viewport token and
  assert `OcctViewWidget`'s background cleared to it (snapshot pixel); change base size
  and assert `Theme::bodyFont().pointSizeF()` follows with all four derived sizes
  distinct; reset restores Graphite exactly (`spec() == defaultSpec()`); construct with
  `persistProgress=false` and assert nothing was written (ScopedTestSettings idiom).
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture** the panel over the viewport. — [ ] **Step 6 — commit.**

---

### Task 6: Document and verify

**Files:** `CLAUDE.md`, the spec, final captures.

- [ ] **Step 1:** CLAUDE.md: the direct-modeling section (kernel ops and their refusal
  contracts, the dedicated preview channel rule, the gizmo predicates), vocabulary
  table gains Fillet/Chamfer and bans `bevel`, the Appearance/spec-backed Theme note,
  architecture table additions.
- [ ] **Step 2:** Spec marked implemented.
- [ ] **Step 3:** Both suites green; captures of all three gizmos in action and the
  Appearance panel; magnified crops verified against the reference images' anatomy.
- [ ] **Step 4:** Commit.
