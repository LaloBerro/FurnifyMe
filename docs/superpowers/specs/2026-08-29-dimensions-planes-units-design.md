# Phase 4: Dimensions, sketch planes, and units — design

Date: 2026-08-29
Status: approved by standing instruction — the user asked for Phase 3 and Phase 4 to run
without further questions, so the decisions below were made rather than asked.

Phase 4 of the UX overhaul. Phases 1–3 (language, learnability, feedback) are merged.
This phase is the first to add **modelling capability**, not only presentation.

## Goal

Three things the user asked for, in their words: *"add segments to show the current length
of an edge, add an XY grid when a face is locked, and add an option to change from mm to
cm."*

## What each one means, decided

The request is one sentence per feature, so each needed a reading. These are the readings
this phase implements, with the reasoning that chose them.

### 1. Length dimensions

The reference image shows a horizontal segment with an extension line at each end, arrow
heads pointing outward, and a boxed `40 cm` label centred above it — a CAD dimension
annotation, drawn while the segment is being made.

**Decision: dimensions appear in two places, drawn by one renderer.**

- **While sketching**, on the segment currently being drawn — from the last committed
  point to the cursor. This is the case the image shows, and it is the one that changes
  what the user can do: today they click blind and find out the size afterwards.
- **On hover and selection of an edge**, in edge-selection mode. This is what "the current
  length of an edge" says literally, and it costs almost nothing once the renderer exists.

One renderer serves both. Two renderers that draw the same annotation will drift, exactly
as two copies of a string do.

**The label reads through `Measure`**, so it obeys the unit setting from part 3 and the
formatting rules Phase 1 settled. It is the same function the status bar and the items
panel use, which is what stops them ever disagreeing.

### 2. The locked face

"An XY grid when a face is locked" is a grid **on the locked face's own plane** — an XY
grid in that face's local coordinates, not the world XY plane, which is what the app
already draws at Z=0 and would be useless overlaid on a vertical face.

**Decision: locking a face makes it the sketch plane.**

`SketchController` already takes an arbitrary `gp_Pln` through `setPlane()`, and
`snapToPlaneGrid()` already rounds in the plane's own coordinates specifically so it stays
correct for non-XY planes and never lifts a point off its plane. The maths this needs is
already written and headless-tested; what is missing is the UI that chooses a plane and
the grid that shows it.

- A planar face selected in face mode can be locked — action **Lock to Face**, and a
  double-click on the face does the same thing.
- Locking sets the sketch plane to that face's plane, orients the grid to it, and says so.
- **Unlock** returns to the ground plane. The ground plane is the default and is never
  itself "locked".
- A **non-planar face cannot be locked** — a cylinder's side has no single plane. The
  attempt reports why, in Phase 1's cause-and-fix form, through a toast.
- The grid is rendered by the existing `GridRenderer`, extended from a hardcoded Z=0 to a
  supplied plane. It keeps its adaptive spacing and distance fade — those are settled and
  this phase must not regress them.

This is the one part of Phase 4 that changes what the app can build: outlines on a locked
face extrude perpendicular to that face, so a user can put a shelf on the side of a
cabinet. `CLAUDE.md` records arbitrary planes as deferred; this phase un-defers them.

### 3. Units

**Decision: a display unit, applied at the formatting boundary only.**

- The model, the kernel, `DocumentModel`, and every stored number stay in **millimetres,
  always**. Only the strings change. A unit that reaches the geometry is a unit that will
  eventually be applied twice.
- `Measure` gains a unit setting and formats to it. Because every user-facing length
  already goes through `Measure` — Phase 1's rule, enforced by tests — this reaches the
  status bar, the items panel, dimension labels and the extrude field for free.
- **Input follows the display unit.** With centimetres selected, typing `4` into the
  extrude height means 40 mm. A field that displays cm and reads mm is a trap.
- Millimetres and centimetres only, as asked. The setting persists through `QSettings`
  beside the learning progress, and lives in `View → Units`.

## Architecture

### `Measure` — extended, still Qt-free

```cpp
namespace Measure {

enum class Unit { Millimetres, Centimetres };

// Process-wide display unit. Millimetres by default, so a caller that never
// sets it behaves exactly as it did before this phase.
void setDisplayUnit(Unit unit);
Unit displayUnit();

// Unchanged signature: still takes millimetres. Now emits "40 cm" when the
// display unit is centimetres. Every existing call site is already correct.
std::string formatLength(double millimetres);

// "34 x 22 x 1.8 cm" - the same numbers, the same order, the display unit.
std::string formatDimensions(const TopoDS_Shape& shape);

// Parses a number the user typed in the current display unit and returns
// millimetres. False when the text is not a number, leaving `out` untouched.
bool parseLength(const std::string& text, double& out);

}  // namespace Measure
```

`formatLength` keeping its millimetre parameter is deliberate: every call site in the app
already passes millimetres, and changing the parameter's meaning would silently convert
twice at any site that was missed. The unit lives in the formatter, not in the argument.

Centimetre rounding follows the same rule as millimetres — one decimal place, trailing
`.0` dropped — so 18 mm reads `1.8 cm` and 340 mm reads `34 cm`.

A process-wide setting is the right shape here despite the usual objection: there is one
document, one window, and one user, and threading it through every call site would touch
every string in the app to express something that is genuinely global. It is set once from
`MainWindow` and read everywhere.

### `DimensionRenderer` — new, app layer

`src/ui/DimensionRenderer.{h,cpp}`. Builds the annotation as OCCT presentation objects, so
it lives in 3D and stays correct as the camera moves — a QPainter overlay would need
reprojecting every frame and would sit wrongly on a rotated view.

```cpp
class DimensionRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);

    // Draw the dimension for one segment, replacing whatever was drawn before.
    // `normal` orients the extension lines out of the segment, away from the
    // model. A degenerate segment (endpoints closer than a hair) draws nothing.
    void show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normal);
    void clear();
    bool isShowing() const;

    // The text the label carries, for the suite and the vocabulary sweep.
    std::string labelText() const;
};
```

The label comes from `Measure::formatLength(distance)`, never from a local format call.

### `GridRenderer` — extended to a plane

`update()` gains a `gp_Pln`. Its three concentric bands, adaptive step and distance fade
are unchanged; only the frame they are built in moves. `firstLineAtOrBelow()` and
`minorStepFor()` are untouched and their tests stay as they are.

### `MainWindow` — the lock

- `Lock to Face` (`L`), enabled only when exactly one planar face is selected.
- `Unlock Face` (`Shift+L`), enabled only while locked.
- Double-clicking a face in face mode locks it.
- The locked plane is `SketchController`'s plane; there is no second copy of it. The one
  piece of state `MainWindow` adds is whether a lock is active, which is derivable but
  worth naming for the actions' enabled state.
- Locking records `faceLock.used`, so a hint can teach it and then go quiet, the way
  Phase 2 established.

## Non-goals

No dimension editing — the label reports, it does not accept a typed value that moves
geometry. No angular or radial dimensions. No units beyond millimetres and centimetres, no
imperial. No constraint solver. No change to how booleans, selection or the camera behave.
No history tree: `CLAUDE.md`'s warning about topological naming still stands, and a locked
face's plane is captured **by value** at lock time so a later rebuild cannot silently move
the sketch plane under the user.

## Testing

- **Headless** (`tests/measure.cpp`, extended): `setDisplayUnit` round-trips; `formatLength`
  in centimetres at 0, 4, 18, 340, 1000, 12345 mm; the `.0` drop and comma placement in
  both units; `formatDimensions` in centimetres; `parseLength` returns millimetres for a
  value typed in each unit, and refuses `""`, `"abc"`, `"1.2.3"` without touching `out`;
  the default unit is millimetres, so a caller that never sets it is unaffected.
- **Headless** (`tests/sketch_document.cpp`, extended): a point unprojected onto a
  non-XY plane lands on that plane; `snapToPlaneGrid` on a vertical plane stays on it.
- **`gui_smoke`**:
  - sketching a segment shows a dimension whose label matches `Measure::formatLength` of
    the true distance — computed independently in the test, not read back from the
    renderer;
  - the dimension clears when the sketch is committed or cancelled;
  - hovering an edge in edge mode shows its length; leaving it clears;
  - selecting a planar face enables `Lock to Face`; locking sets the sketch plane to that
    face's plane, asserted against the face's own `gp_Pln`, and points clicked afterwards
    land **on that plane**;
  - a non-planar face cannot be locked and reports why in a toast;
  - `Unlock Face` returns the plane to Z=0;
  - switching to centimetres changes the status bar, the items panel and a dimension label
    together, and switching back restores them exactly;
  - with centimetres selected, typing `4` into the extrude height produces a 40 mm body —
    the trap this phase must not ship;
  - dimension label text joins the banned-word sweep.
- All existing checks continue to pass.

## Acceptance criteria

1. A segment being sketched shows its length live, and an edge shows its length on hover
   and selection, both drawn by one renderer and labelled through `Measure`.
2. A planar face can be locked; the sketch plane and the grid both follow it; a non-planar
   face is refused with a reason.
3. Unlocking returns to the ground plane.
4. Millimetres and centimetres can be chosen; every user-facing length follows, input
   follows the display unit, and the choice persists across a restart.
5. No stored or kernel number changes unit — the conversion happens only in `Measure`.
6. Nothing in this phase changes wording settled in Phase 1, behaviour settled in Phase 2,
   or the feedback surfaces settled in Phase 3.
7. Headless suite and all existing `gui_smoke` checks pass, plus the new ones.
