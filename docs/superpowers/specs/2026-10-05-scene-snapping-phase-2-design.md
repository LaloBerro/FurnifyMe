# Scene snapping, phase 2 — a piece against another piece

**Status:** design, awaiting review
**Phase 1:** `docs/superpowers/specs/2026-10-02-scene-editor-design.md` (merged)

Phase 1 shipped two of the four snap kinds the user picked: the grid-and-angle snap and the
floor. This is the other two — **against another piece** and **on top of another piece** —
which phase 1's own spec called "the largest single item in the feature" and deferred to its
own planning round.

## The four decisions

Settled by the user before any of this was written:

1. **Faces: side-to-side and on top.** A piece's outer faces meet another's. Not edges, not
   corners.
2. **Nearest wins, one at a time.** The single closest candidate takes the drag. No combining
   across axes.
3. **A guide line plus the distance.** The existing magnet guide, and a number saying what it
   did — the gap, or `flush`.
4. **Overlap is allowed.** Snapping assists and never refuses. A piece can be pushed into
   another deliberately.

## What already exists, and why this is an extension rather than a new snapper

The **magnet** (`OcctViewWidget::collectMagnetCandidates`, `magnetSnap`, `showMagnetGuide`)
already does most of this for the furniture editor:

- It builds `MagnetCandidate{value, target, movingCentre, targetCentre}` from every other
  displayed body's bounding box — the **low, centre and high** coordinate on the dragged
  axis, against the same three of the moving body.
- `lo_moving → hi_other` **is** flush side-to-side contact. `lo_z → hi_z` **is** sitting on
  top. The two kinds the user picked are already in the candidate set; nothing new has to be
  invented to express them.
- It thresholds in **screen space** (`kMagnetSnapPx = 8.0`, converted through
  `worldPerPixelAt`), takes the nearest candidate, and **overrides the grid snap** when it
  holds — which is decision 2 and decision 4 already implemented.
- It draws a guide line between the two centres.

So phase 2 is not a second snapping system. It is four specific gaps in this one.

## The gaps

### 1. It cannot see a piece

`collectMagnetCandidates` requires `selectedSolidIds().size() == 1` and reads one
`AIS_Shape`'s box. A **piece is many bodies** — the user's own scene has pieces of 50 — so
in a scene the magnet collects nothing at all and the feature is simply absent.

**The change:** in a scene, the moving object is the selected **piece** and each candidate
is another **piece**, both measured as one box over `placedShapesForPiece()`. That accessor
already exists and already applies the placement, which the phase 1 audit made true across
the board.

The box is the piece's **`measuredBox()`**, not a world AABB — the same rule the floor
settle follows, and for the same reason: a turned piece's world box is larger than the
furniture is, so its "faces" would be in the wrong place. A piece turned 30° snaps against
its own sides.

### 2. There is no distance readout

The guide line says *that* it snapped, not *what it did*. Decision 3 asks for the number.

**The change:** the gap between the two faces, through `Measure::formatLength` like every
other length in this app, or the word **flush** when it is zero. Drawn with the guide line,
screen-sized, in the same layer the guide already uses.

`flush` is a deliberate word choice: `0 mm` reads as a measurement that happens to be zero,
and the thing the user wants to know is that the two faces are *touching*.

### 3. "On top" fights the floor rule

Phase 1 settles a piece onto Z = 0 at the end of every gesture. A piece snapped onto another
piece's top face is at Z > 0 and must **stay there**.

**The change:** the floor settle becomes "settle onto whatever is beneath it" — the highest
supporting face under the piece's own footprint, falling back to Z = 0 when nothing is. That
is one rule, not two, and it keeps the phase 1 behaviour exactly when no piece is underneath.

This is the one place phase 2 changes a phase 1 behaviour rather than adding to it, and it is
worth saying plainly: a piece dropped on open floor still lands on the floor; a piece dropped
over a table lands on the table.

### 4. Rotate has no snapping at all

Decision 1 is about faces, and the user's original fork said "the grid and fixed angles",
which the 15° rotate snap already delivers. **Aligning a piece's angle to another piece's
angle is not in scope** — it was not asked for, and it is a different gesture (match an
orientation) from the one this spec is about (meet a face).

Recorded so the absence is a decision rather than an oversight.

## What a user does

1. Select a piece. Drag a Move arm toward another piece.
2. Within 8 screen pixels of an alignment, the drag **latches**: a guide line appears between
   the two pieces and a number says `flush` or the gap.
3. Let go. The piece is where the guide said.
4. Drag it away and nothing holds it — snapping never refuses, and the piece can be left
   overlapping if that is what the user wants.

## Out of scope, explicitly

- **Edges and corners.** Decision 1 is faces. Corner-to-corner alignment is a larger
  candidate set and a harder choice problem, and the user picked against it.
- **Combining axes.** Decision 2. Snapping X from one piece and Y from another at once is
  more powerful and much harder to show; one snap, one guide, one answer.
- **Rotational alignment.** See gap 4.
- **Blocking overlap.** Decision 4.
- **Snapping inside a furniture.** A scene snaps whole pieces. Bodies inside one furniture
  are the editor's business.

## How it is proven

The phase 1 branch's own lesson is that a check whose fixture cannot express the defect is
not a check, so each of these names the fixture that gives it teeth:

- **A piece snaps flush to another piece's side**, measured as the gap between the two
  measured boxes being zero to the micron — and the fixture places the two pieces at a
  deliberately non-round offset, so landing flush cannot be the grid snap's doing.
- **A TURNED piece snaps against its own side, not its world box.** The fixture turns a
  piece 30° and asserts the contact plane is the piece's own face. Without this the world-box
  bug passes, as it did three times in phase 1.
- **A piece dropped over another lands on it; one dropped beside it lands on the floor.**
  Both directions, because a rule that only ever lands things on the floor passes the second
  check alone.
- **The number says `flush` at contact and a real gap otherwise**, read off the drawn
  annotation rather than recomputed by the test.
- **Snapping never refuses**: a drag that pushes one piece well into another leaves it
  overlapping, so decision 4 is a check rather than a comment.
- **The grid is still there**: with no piece in range, a drag still lands on a 10 mm step.

Each gets a mutation that must produce a **named red line**.

## Honest risks

- **The 8 px threshold is the editor's, chosen for bodies a few hundred millimetres across.**
  A scene is metres wide and zoomed further out, so the same screen threshold covers much
  more world distance. It may need its own constant; that is a measurement to take, not a
  number to guess, and the spec does not pretend to know it yet.
- **Candidate count grows with pieces × 9.** The user's scene is five pieces, so this is not
  a concern today, and `collectMagnetCandidates` already runs per drag step for the editor.
  Worth watching rather than optimising blind.
- **"The highest supporting face under its footprint"** is the only genuinely new geometry in
  this spec, and it is where a bug would hide. It needs its own headless test before any UI
  uses it.
