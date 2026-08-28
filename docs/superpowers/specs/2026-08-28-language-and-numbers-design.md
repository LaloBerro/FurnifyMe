# Phase 1: Language and numbers — design

Date: 2026-08-28
Status: approved in chat, awaiting spec review

Phase 1 of three. Phase 2 is help and learnability; Phase 3 is feedback and polish.
This phase changes what the app *says* through the surfaces that already exist. It adds
no new surfaces.

## Goal

Make every word and number in FurnifyMe read as though a furniture maker wrote it for
another furniture maker: one name per thing, dimensions instead of volumes, and errors
that name their cause and their fix.

## Why this is first

The help system in Phase 2 is *made of* language. Building it on the current copy would
mean writing every string twice. Settling the vocabulary, the number formatting and the
error structure first means Phase 2 only has to decide *where* guidance appears, never
*how it is worded*.

## Diagnosis (all quoted from the current build)

The app has 88 user-facing strings. The defects are systemic, not scattered:

- **The kernel leaks.** `"OCCT could not complete the operation:"` names a library the user
  has never heard of. `Fuse` is OpenCascade's word for union; no mainstream CAD app shows
  it to users.
- **Two names for one object.** The Items panel says `Body 03`; the status bar says
  `Solid #3`. Both are on screen simultaneously.
- **Implementation described instead of intent.** `"Subtract the later solid from the
  earlier one"` explains our document ordering, not what the user is trying to do.
- **Unreadable numbers.** `"volume 1349000.00 mm3"` — no separators, no proper glyph, and
  volume is close to the least useful measurement for furniture.
- **Dead-end errors.** `"The extrusion failed."` gives no cause and no next step.

## Decisions (settled with the user)

1. **Object information is dimensions, not volume.** Bounding-box `W × D × H` in
   millimetres. Furniture is specified, cut and bought by dimension; nobody checks that a
   tabletop is 18 litres. Volume is dropped from the UI entirely in this phase.
2. **Operations are Union / Subtract / Intersect.** Standard CAD vocabulary — transferable
   to any other tool — carried by plain-English tooltips that teach the meaning. This suits
   "teach then get out of the way" better than inventing friendlier but non-transferable
   words.
3. **The object noun is Body**, everywhere, formatted `Body 03`.

## Vocabulary

Recorded in CLAUDE.md so it cannot drift. The failure mode is not choosing bad words; it
is choosing good ones and then quietly using others three months later.

| Concept | Word | Never |
|---|---|---|
| The 2D shape being drawn | outline | wire, polygon, polyline, sketch line |
| A closed outline, not yet 3D | face | profile, region |
| A 3D object in the document | body, `Body 03` | solid, `Solid #3`, shape, part |
| Combining two bodies | Union | fuse, merge, join, add |
| Removing one body from another | Subtract | cut, difference, boolean cut |
| Keeping the shared volume | Intersect | common, overlap, boolean common |
| Turning a face into a body | Extrude | pull, push, prism |
| The 3D area | viewport | scene, canvas, view |

`Cut` does not survive anywhere. The action is Subtract in every surface: menu, chip,
tooltip, status line and error message.

## Architecture

### `Measure` — new, Qt-free, in `furnify_geometry`

`src/Measure.{h,cpp}`. Formatting is logic, and logic in this project is headless-testable.
It uses `std::string`, not `QString`, exactly as `DocumentModel::name` does; callers wrap
with `QString::fromStdString`.

```cpp
namespace Measure {

// "340 mm", "1,200 mm", "18.5 mm". One decimal place, dropped when the value is
// whole. Thousands separated with a plain comma - unambiguous in an English UI,
// and unlike a narrow no-break space it survives any font we might ship.
// Formatting is done by hand rather than through std::locale, which would make
// the output depend on the machine's regional settings and break the tests.
std::string formatLength(double millimetres);

// "340 x 220 x 18 mm" using U+00D7 MULTIPLICATION SIGN, always in X, Y, Z order
// so the three numbers always mean the same thing. Empty string for a null or
// void shape.
std::string formatDimensions(const TopoDS_Shape& shape);

// The raw bounding-box extents, for callers that need the numbers rather than
// the words. All zero for a null or void shape.
struct Extents { double x = 0.0, y = 0.0, z = 0.0; };
Extents extentsOf(const TopoDS_Shape& shape);

}  // namespace Measure
```

`formatDimensions` is `extentsOf` plus `formatLength` — it must not duplicate the rounding
rules, or the panel and the status bar will disagree about whether a body is 18 or 18.5 mm
thick.

Rounding: round to one decimal place, then drop a trailing `.0`. A body 18.04 mm thick
rounds to 18.0 and reads `18 mm`; one 18.05 mm thick rounds to 18.1 and reads `18.1 mm`.
A value below 0.05 mm reads `0 mm`, never `0.0 mm` or `-0 mm`.

### Where copy lives

Inline `tr()` at each use site, as now. Qt convention ties translation context to the
call site, and a central string table would break that for no gain. Consistency comes
from the vocabulary table above and from review, not from centralisation.

## The rewrite

Three rules govern every string:

1. **Say what happened, and what is now possible.** `"Body 03 created — 340 × 220 × 18 mm"`
   over `"Solid #3 created (volume 1349000.00 mm3)."`
2. **Never show an internal id or a library name.** No `#3`, no `OCCT`, no `TopoDS`.
3. **Errors name the cause and the fix**, in that order, in one or two sentences.

### Representative rewrites

| Current | Replacement |
|---|---|
| `Solid #%1 created (volume %2 mm3).` | `Body %1 created — %2` (dimensions) |
| `Solid #%1 created from #%2 and #%3 (volume %4 mm3).` | `Subtracted Body %2 from Body %1 → Body %3` (verb varies by operation) |
| `OCCT could not complete the operation:\n\n%1` | `Union failed — the two bodies don't overlap anywhere, so there's nothing to join. Move one so they touch, then try again.` |
| `The extrusion failed.` | `Extrude failed — this outline can't form a flat face. It probably crosses itself; press Backspace to undo points and redraw.` |
| `Could not build a planar face from these points.` | `This outline can't close into a face. It probably crosses itself — press Backspace to undo the last point and redraw.` |
| `Select exactly two solids (Shift-click to add).` | `%1 needs two bodies. Click one, then Shift-click another.` (operation name substituted) |
| `Sketching - %1 of 3 points needed` | `Sketching — %1 points, %2 more to close` |
| `Sketching - %1 points - Enter or click the start point to close` | `Sketching — %1 points. Enter or click the first point to close.` |
| `%1 solid(s) - click one to select` | `%1 bodies — click one to select` |
| `Empty - start a sketch (Ctrl+K)` | `Nothing yet — press Ctrl+K to draw an outline` |
| `Subtract the later solid from the earlier one` | `Cuts the second body out of the first, like a chisel removing waste` |
| `Deleted %1 solid(s).` | `Deleted %1 bodies` / `Deleted Body 03` when it is exactly one |
| `Nothing selected.` | `Nothing selected` |
| `Boolean failed - model unchanged.` | `%1 failed — nothing was changed` |

Singular and plural are handled per-string, not with `(s)`. `"Deleted 1 bodies"` is the
kind of detail that makes software feel unfinished.

### Punctuation convention

- **Status bar, left side** — what just happened. A sentence; ends with no period, because
  it is a log line rather than prose, and periods accumulate visually in a strip that
  updates constantly.
- **Status bar, right side** — where you are. A fragment; no period.
- **Dialog body text** — prose; full sentences with periods.
- **Tooltips** — a fragment naming the effect, then optionally one sentence of teaching.
  No trailing period on the fragment.
- Em dashes (`—`) separate clauses in status lines; hyphens are never used for this.

### The Subtract asymmetry

`Subtract` is the one operation where argument order matters, and the current tooltip
explains it in terms of document order ("the later solid from the earlier one"), which is
an implementation fact the user cannot see. The replacement states the rule in terms the
user *can* verify — the body created first is the one that survives:

> **Subtract** — Cuts the second body out of the first, like a chisel removing waste.
> The body you made first is the one that keeps its shape.

The underlying behaviour (sort by document id, lower id is the base) does not change in
this phase.

## Non-goals

No onboarding, no shortcut sheet, no toasts, no help panel — Phase 2 and 3. No change to
any modelling behaviour, geometry, or camera. No change to `Subtract`'s operand ordering.
No translation infrastructure beyond the `tr()` calls that already exist. Volume is
removed from the UI but `ModelingOps::volume` stays; the tests use it.

## Testing

- **Headless** (`tests/measure.cpp`, new, registered with ctest): `formatLength` at
  0, 0.04, 0.05, 18, 18.5, 999, 1000, 1200, 1234567; the `.0` drop; comma placement;
  `formatDimensions` on a known box giving exactly `"340 × 220 × 18 mm"`; the `×` is
  U+00D7 and not the letter x; a null shape gives an empty string, not `"0 × 0 × 0 mm"`;
  `extentsOf` matches `formatDimensions` so the two cannot disagree.
- **gui_smoke**: after an extrude the status text matches `Body 0` and contains `×`;
  the status text never contains `Solid #` or `OCCT` anywhere in a full session; the
  Items panel row for a body contains its dimensions; the Model menu actions are named
  Union, Subtract and Intersect. The existing 90 checks continue to pass.
- **A grep gate, as a test**: `gui_smoke` asserts that the set of `QAction` texts contains
  no banned word (`Fuse`, `Solid`, `OCCT`). A vocabulary that is only documented drifts;
  one that is asserted does not.

## Acceptance criteria

1. No user-visible string contains `OCCT`, `Fuse`, `Solid #`, or `mm3`.
2. Every body is named `Body NN` in every surface that mentions it.
3. Extrude, Union, Subtract, Intersect and Delete all report dimensions, not volume.
4. Every error message names a cause and a next action.
5. Singular and plural are correct everywhere; no `(s)` remains.
6. `Measure` is covered by headless tests and used by both the status bar and the panel.
7. Headless suite and all existing gui_smoke checks pass, plus the new ones.
