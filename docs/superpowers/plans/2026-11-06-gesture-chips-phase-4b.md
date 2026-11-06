# Gesture chips, Phase 4b: Extrude and the glued chips

**Goal:** the rest of the field chips drawn by the editor's chrome page, on 4a's
machinery (`ChipFace`, `ChipsBridge`, `Chip.svelte`): Extrude (top centre), and
the five chips that stand beside a point on the model - Pull, Fillet/Chamfer,
Move, Mitre and Re-measure. The joint card is 4c.

**Spec:** the migration report (T-066), "Phase 4 - gesture chips", and the
spike's gate 4: a glued chip fades while the camera moves and leaves the mask.
Plan 4a's rulings R1-R5 hold unchanged.

## Rulings

- **R6 Glued chips leave the page while the camera moves.** `ChipFace::
  setGlued(true)`; `EditorChrome` marks every glued chip `moving` from the
  first `cameraChanged` until 150 ms after the last, and the page neither
  draws it nor lets it take a press meanwhile. A drag that moves the chip
  (the Move gizmo travels with its body) is NOT camera motion - the chip is
  up exactly to show that drag's value, so it stays drawn.
- **R7 One list.** `MainWindow::chipFaces()` names every chip the page draws;
  the bridge, the chrome and the live hand-over all read it.
- **R8 The member is `myChipFace`.** Extrude, Pull and Mitre already had a
  `myFace` (the picked `TopoDS_Face`).
- **R9 A gesture card is `[data-gesture]`, never `[data-chip]`.** The rail's
  buttons already wear `data-chip="<action id>"`, and the rail's Extrude
  button answered a query meant for the Extrude panel.
- **R10 A field with no label of its own is named** (`ChipFace::field(id,
  label, name)`): Extrude's and Pull's sit under the card's label row.
- **R11 The page's view rises only when something stands over it**: a chip
  moving every frame of a drag would otherwise restack the overlay every frame.

## Review focus

1. An orbit with a field focused: the field is hidden, so it blurs and gives
   the keyboard back; Enter afterwards is the Qt claim's (mirrored value).
2. Fillet/Chamfer's kind and value change mid-drag; the face follows.
3. Mitre's reason appears and goes as the angle changes; the card re-measures.
4. Move's chip shows only during a gizmo drag and stays drawn through it.
5. A theme switch re-measures each card.

## Ledger

- Ruling: Move and Fillet/Chamfer are pinned in chrome_smoke by their wiring
  only (registered, drawn elsewhere, glued) - starting them needs a gizmo
  drag and an edge pick through the harness, which no block here does yet.
  Cost if wrong: a layout bug in those two cards reaches the user unseen;
  their faces are the same parts the four driven chips use.
