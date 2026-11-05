# Gesture chips, Phase 4a: the chip face, Boolean and Slats

**Goal:** the first gesture chips drawn by the editor's chrome page - the
Boolean chip and the Slats chip, both at the viewport's bottom centre - over
machinery every later chip (4b: Extrude, Pull, Fillet/Chamfer, Move, Mitre,
Re-measure; 4c: the joint card) reuses.

**Architecture:** the Qt chip stays the state. It keeps its place, its
Enter/Escape claim, its preview and its commit; drawn elsewhere it paints
nothing, hides its controls and lets the pointer through. What the chip SAYS
is described as data - rows of parts (words, fields, buttons, toggles) read
off its real Qt controls - by a `ChipFace`, sent to the page by `ChipsBridge`
(`chips`), and drawn by one Svelte component. An edit on the page lands in
the Qt control itself (a field's `setText`, a button's `click()`), so the
chip's own signals run exactly as for a Qt click.

**Spec:** the migration report (T-066), "Phase 4 - gesture chips", and the
spike's gate 4 (glued chips fade while the camera moves - 4b's concern).

## Global constraints

- Both presets build; `gui_smoke` (Qt path) does not move: floor 6454.
- No modal anything; no OS input in any suite; no `QAccessible::Alert`.
- Words are C++'s (the chips' own `tr()`), swept by gui_smoke's paintedTexts.
- Every control on the page wears `use:chip`; typed fields keep focus.

## Rulings

- **R1 Content in C++, layout in the page.** A face is `{rows, invalid, seed,
  focus}`; a row is a list of parts `{kind: text|field|button|toggle, id,
  ...}`. One component draws any chip.
- **R2 The Qt chip is the chip.** Its geometry is the slot: `EditorChrome::
  addChip()` reports its rectangle and visibility in the `cards` map, and
  its size is the page's measurement (`ChipFace::setFaceSize`), so its own
  placement code (bottom centre now, beside a 3D point in 4b) runs
  unchanged.
- **R3 Keys.** A page field holds the keyboard in the chip's name; while it
  does, keys aimed at the page's view pass the chip's claim untouched and
  the page answers Enter WITH its field values (`chips.commit(name,
  values)`) - the name question's rule: a mirrored edit can lag one message
  behind Enter. Enter with the keyboard anywhere else is the Qt claim's,
  committing what the mirrored fields hold.
- **R4 No jump.** The page draws a chip only once the rectangle it was told
  carries its own measured size (within the whole-device-pixel rounding).
- **R5 Seeding.** A field is (re)seeded when the face's `seed` moves - a new
  gesture - and claims the keyboard outright as it is focused; a value
  coming back from C++ never retypes the field under the user's hands.
- **R6 Order.** The two fixed chips first; the glued chips (4b) fade while
  the camera moves and leave the mask, the spike's gate 4.

## Tasks

1. `ChipFace` + `ChipsBridge`, offscreen ctest `chip_face_tests` first.
2. `EditorChrome::addChip()`; MainWindow wires faces while the page is live.
3. Boolean chip face; Slats chip face (drawn elsewhere, sized by the page).
4. Page: `chips/api.ts`, `chips/Chip.svelte`, mock, vitest.
5. chrome_smoke: each chip drawn where the Qt chip stands, the toggle, a
   typed width mirrored and previewed, Enter from the page commits its
   values, Escape cancels, composited captures.
6. Gate: both presets, ctest, gui_smoke twice, chrome_smoke, vitest;
   CLAUDE.md; board.

## Review focus

1. A theme switch while a chip is up re-measures it (the page's font moves).
2. A gesture ending while a page field holds the keyboard gives it back.
3. A page that dies mid-gesture: the Qt chip paints again with the typed
   values (they were mirrored).
4. Enter typed straight after a digit commits the digit (R3).
5. Two gestures in a row re-seed the field (R5).

## Ledger
