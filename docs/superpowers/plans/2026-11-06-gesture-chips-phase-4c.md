# Gesture chips, Phase 4c: the joint card

**Goal:** the last gesture surface drawn by the chrome page - `JointChip`, the
docked card where a joint's kind is switched and its numbers edited - on 4a's
machinery, closing Phase 4.

## Rulings

- **R12 The face is built with relayout()'s own family logic** (`faceRows()`):
  which slots a kind carries stays decided once, in C++.
- **R13 The kind menu is drawn inside the card.** A popup layer of its own
  would need its own mask rectangle and stacking; inside the card it is one
  measured card that grows while the menu is open. The Qt `JointKindMenu`
  never shows while the page draws the card; the open state is the chip's.
- **R14 Outside, drawn elsewhere, is outside the card.** A press on the page's
  menu reaches the chip's application filter as a press on the page's view;
  the menu's geometric rule reads the card's rectangle in that state.
- **R15 The face model grows only what this card needs**: stacked fields with
  their own invalid mark, segment/link/menu buttons with a stacked label, a
  rule, user text, a menu part.
- **R16 The slow suites run once, at the end of Phase 4** (the user's call,
  2026-11-06): per slice only the builds, ctest, vitest and svelte-check.

## Ledger

- 4c fast checks: both presets build, ctest 25/25 in both, vitest 253/253,
  svelte-check clean.
- Phase 4 gate (4a + 4b + 4c): chrome_smoke once, gui_smoke unfiltered twice.
- Phase 4 gate: gui_smoke 6453 checks + 1 skip, twice, floor 6454 unchanged.
  chrome_smoke 440 checks: the first run red on one check, "the slot grows
  with the open row and the card still covers it" - the Joints drawer's
  timing flake already filed as T-084, seen before Phase 4 - and green
  440/440 on the rerun. Every chip check passed on both runs.
