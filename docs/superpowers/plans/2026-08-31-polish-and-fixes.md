# Phase 7: Polish and Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sixteen user-reported items — an orthographic mode, sketch polish, outlines as document items, bevel fixes, and a batch of small corrections.

**Architecture:** Projection state joins `CameraController` (Qt-free); outlines join `DocumentModel` as first-class items; the bevel kernel ops take edge lists; everything else refines existing surfaces.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1, CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-08-31-polish-and-fixes-design.md` (items numbered as the user numbered them).

## A note on this plan's form

As in every phase since 3: interfaces, contracts, exact values and test intent fixed
here; bodies not transcribed; each task names its prior art.

## Global Constraints

- `furnify_geometry` links no Qt. Kernel refusals never surface as success; `ok == false`
  carries a null shape.
- Vocabulary (banned case-insensitively): `Fuse`, `Solid`, `OCCT`, `mm3`, `(s)`, `Merge`,
  `Join`, `bevel` in user-visible text. A closed outline item is an **Outline**
  (`Outline 01`); everything else per CLAUDE.md's table. Lengths via `Measure`.
- The shell is action-driven; `updateActions()` is the single authority; slots on
  `appStateChanged` never call back in. Gizmo previews use `setModelingPreview` only.
- Commits that change the document go through checkpoint + replace + Note toast with
  Undo (now gated by item 12's toggle — **Failure toasts bypass the toggle always**).
- No translucent pixels over GL (toast fade excepted); whole-device-pixel sizes and
  positions for cards; measured pixels over crop eyeballs; `childAt`-verified controls
  with pinned guards; the check-counter floor rises with every added check.
- Suite must stay green at native, 1.25× and 1.75× (`QT_SCALE_FACTOR`); never OS input.
- Build/run: presets as ever; kill stray exes first.

---

### Task 1: Projection modes (items 1, 15)

**Files:** `src/CameraController.{h,cpp}`, `src/OcctViewWidget.{h,cpp}`,
`src/ui/AppBar.{h,cpp}`, `src/MainWindow.{h,cpp}`, `src/ui/AxisGizmo.cpp`,
`tests/camera_controller.cpp` (or the camera headless file as named), `tests/gui_smoke.cpp`.

**Interfaces:**
```cpp
// CameraController (Qt-free)
enum class Projection { Perspective, Orthographic };
void setBaseProjection(Projection p);      // the user's chosen mode (the toggle)
Projection baseProjection() const;
void setTemporaryOrtho(bool on);           // gizmo/lock fly-to engages it
bool effectiveOrtho() const;               // temporary || base == Orthographic
// Any orbit (azimuth/elevation change) clears the temporary flag.
```

**Contract:**
- The bar's view label button becomes the **Persp / Ortho toggle**: clicking flips the
  base projection and the label text (`Persp` / `Ortho`); it no longer snaps to
  Axonometric (that behaviour moves fully to the gizmo and the View menu, which keep
  it). Tooltip updated; the shortcut sheet follows automatically.
- Gizmo arm click and `lockToFace` fly-to set `temporaryOrtho`; the first orbit clears
  it and the OCCT camera returns to the base projection. Pan and zoom do NOT clear it —
  panning across a face-on drawing is normal drafting.
- `lockToFace` now also flies the camera: eye along the face's outward normal, target at
  the face centre, distance framing the face (reuse `animateTo`), orthographic.
- OCCT side: `Graphic3d_Camera::SetProjectionType`; `worldPerPixel()` and every
  screen-space helper must be verified against ortho (they read the camera — check the
  maths holds for parallel projection; `axisParameterForRay` too).
- Headless: base/temporary state machine (orbit clears temporary, pan/zoom do not,
  effective resolves correctly); gui_smoke: toggle flips the OCCT camera's projection
  type; gizmo-arm click lands ortho with the label unchanged (`Ortho` only when base);
  orbit restores; lock-face flies square-on (view direction ∥ face normal, ortho).

Steps: RED → implement → GREEN at three scales → capture (a face-on ortho view like
image 1) → commit.

---

### Task 2: Sketch layer, markers, Shift-straight, mid-sketch undo (items 2, 3, 5, 6a)

**Files:** `src/OcctViewWidget.{h,cpp}`, `src/SketchController.{h,cpp}` (Qt-free helper),
`src/MainWindow.cpp`, `src/GridRenderer.cpp` (layer only), `tests/sketch_document.cpp`,
`tests/gui_smoke.cpp`.

**Contract:**
- **Layering (2):** grid renders in a Z-layer beneath default scene objects; the
  in-progress outline, markers, pending face and dimension render in a layer above the
  grid (`Graphic3d_ZLayerId` — pick layers so bodies still occlude each other normally
  but the grid never draws over sketch work; note the locked-face grid nudge must keep
  working). Probe: with a sketch over a grid line, sample pixels along the segment —
  outline colour, never grid colour.
- **Markers (3):** first point = filled accent square (~7 px), placed points = small
  dots, cursor = violet ring — image 3's anatomy; update the marker checks' expected
  styles, keep the childAt/count checks.
- **Shift-straight (5):** `SketchController::snapToDirection(prev, dir, candidate)`
  (Qt-free): project candidate onto the line through `prev` along `dir`; headless tests
  incl. degenerate zero-length dir. Wired when Shift is held and ≥2 points exist; the
  dimension and cursor marker follow the snapped point. Status label gains no new copy.
- **Mid-sketch undo (6a):** while sketching, `Ctrl+Z` triggers the existing
  remove-last-point path (Backspace's), via the Undo action being enabled during
  sketch with rerouted behaviour — keep `updateActions()` the single authority; the
  menu text stays `Undo`. Redo stays disabled mid-sketch.

---

### Task 3: Outlines as document items + lifecycle undo (items 4, 6b)

**Files:** `src/DocumentModel.{h,cpp}` (Qt-free), `src/MainWindow.cpp`,
`src/ui/ItemsPanel.cpp`, `src/OcctViewWidget.{h,cpp}`, `tests/sketch_document.cpp`,
`tests/gui_smoke.cpp`.

**Contract:**
- `DocumentModel` gains outline items: `addOutline(TopoDS_Face, gp_Pln)` →
  `Outline NN`; `outlines()`; `removeOutline(id)`; extrude consumes an outline id and
  adds a body in **one checkpoint** so undo restores the outline and removes the body.
  Closing a sketch creates the outline item (checkpoint + Note toast with Undo);
  the pending-face concept becomes "the selected/most recent outline" — **keep
  `hasPendingFace()` semantics as a derived view over outlines** so every gizmo
  predicate and `ExtrudePreview` keeps working unchanged (they gate on it).
- Items drawer lists outlines above bodies with the eye toggle; row text
  `Outline 01 — 340 × 220 mm` (plane-local extents via `Measure`).
- Viewport: an outline item displays as its face (the existing pending-face preview
  becomes a real, selectable-in-body-mode? **No** — outlines are NOT selectable
  geometry this phase; they display non-pickable like previews, eye-toggleable).
- Undo/redo walk the whole lifecycle: place points (not in document) → close (item) →
  extrude (converts) → Ctrl+Z (outline back, body gone) → Ctrl+Z (outline gone) → redo
  ×2 restores. Headless tests on `DocumentModel`; gui_smoke drives the UI path.

---

### Task 4: Bevel correctness (items 8, 9)

**Files:** `src/ModelingOps.{h,cpp}`, `src/OcctViewWidget.{h,cpp}`, `src/ui/BevelArrow.cpp`,
`src/MainWindow.cpp`, `tests/direct_modeling.cpp`, `tests/gui_smoke.cpp`.

**Contract:**
- **Diagnose 8 before fixing.** Build the failing case headless first: box, fillet edge
  A (commit), then fillet edge B adjacent to A's strip; assert only B's neighbourhood
  changed (face count +1 strip; A's strip face unchanged). If the spread reproduces,
  identify whether `BRepFilletAPI_MakeFillet` chains tangent edges or the picked edge
  after `UnifySameDomain` IS a merged chain; fix accordingly (`ChFi3d` continuity
  setting, or re-pick the exact sub-edge). The regression test is the deliverable as
  much as the fix.
- **Multi-edge (9):** `filletEdge`/`chamferEdge` gain list overloads
  (`std::vector<TopoDS_Edge>`), one `BRepFilletAPI` build with all edges added, all
  refusal rules intact (any foreign/curved edge refuses the whole call). Edge mode
  allows Shift-click accumulation; the bevel arrow shows on the LAST selected edge and
  applies to all (`n edges` named in the chip when n>1: `Fillet — 3 edges`); one
  checkpoint, one toast (`Fillet added to Body 01 — 3 edges — …` shape, vocabulary
  clean). Headless: 2-edge fillet volume delta = 2× single (far-apart edges).

---

### Task 5: The small batch (items 7, 10, 11, 12, 13, 14, 16)

One dispatch, one commit; each item is a few files.

- **7 export/import:** `Save colours…` / `Load colours…` buttons in the Appearance
  panel; `QFileDialog::getSaveFileName`/`getOpenFileName` (native dialogs are
  acceptable here — they are OS surfaces, not app modals; note this in a comment),
  writing/reading `Theme::serializeSpec()` to `*.furnifytheme`; a bad file → Failure
  toast, spec untouched.
- **10 live font preview:** apply on the combo's `highlighted` signal; Escape while the
  popup is open restores the family from before the popup opened.
- **11 gizmo size clamp:** derive `AIS_Manipulator` size from the view so its screen
  footprint stays within ~15% of the viewport's smaller dimension at any zoom; re-derive
  on camera change (cheap guard like the label's); probe at two zooms.
- **12 notifications toggle:** `View → Show notifications` (checkable, default on,
  persisted with the settings family). Off: `ToastHost::show` drops `Note` kind only;
  `Failure` always shows. Checks: a delete with notes off shows no toast but undo still
  works via menu; a refusal with notes off still toasts.
- **13 double-click routes:** in face/edge mode, double-click on a body → select that
  whole body (switch to body selection mode with it selected — one gesture, mode
  follows); **Ctrl+double-click on a face → lock** (the old plain-double-click route
  moves; `L`/menu unchanged; `canChangeSketchPlane` guard still applies). Update the
  affected checks; the walkthrough/hints copy does not mention double-click, verify.
- **14 stripe:** toast kind stripe 3 px → 6 px; update the pixel probes.
- **16 icon:** a 256px painted icon (accent `▰` glyph on Graphite tile, painted by a
  small tool or committed asset `assets/icon.ico`), wired via `.rc` resource +
  `w.setWindowIcon`. Check: `windowIcon().isNull()` false.

---

### Task 6: Document and verify

`CLAUDE.md` (projection state, outline items, the bevel-chain lesson, the toggle's
Failure exception, double-click table), spec status, both suites at three scales,
captures: ortho face-on, sketch-above-grid with new markers, multi-edge bevel, the
icon in the title bar. Present the side-by-sides.
