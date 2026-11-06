# Scene window, Phase 5: its chrome, its pieces and its add card as HTML

**Goal:** the scene window drawn by the chrome page as the editor is - the
migration report's Phase 5 ("pieces list, add card, tool cluster, render
studio"). The render studio's panel was a page since 3e.

## Rulings

- **R1 One page, two windows.** The scene registers the objects it has on the
  editor's own `EditorChrome` and page; `Editor.svelte` draws only what it is
  given. No second chrome class, no second page.
- **R2 One toast route.** `SceneWindow::showToast()` is the page's while it is
  live and `ToastHost`'s otherwise; a Failure's reason stays on the status line.
- **R3 The models send the Qt cards' own signals**, and the scene wires both
  cards to ONE handler each (`selectPieceFromList`, `setPieceShown`, and the
  existing `removePiece`/`renamePiece`/`addPiece`).
- **R4 Words have one author**: `ScenePiecesPanel::titleText()`/`eyeTipText()`
  and `AddPieceCard::titleText()` were added and the Qt cards read them too.
- **R5 The add card claims the keyboard while it stands**, so its one key,
  Escape, reaches the page; it gives it back however it closes.
- **R6 The menus are the pill's** (`AppBar::menus()`): `menuBar()` after the
  reparent makes a new, empty bar.

## Ledger

- 5a (9f5c7ff): chrome_smoke 452/452 - strip, pill, the scene's 4 menus,
  chips, a Failure on the page, the pill seen in the composited window.
- 5b (db2470f): scene_cards_tests 18/18 (red on stubs first, 15 failing),
  vitest 261/261, chrome_smoke 462/462, ctest 26/26 both presets.
- Not driven: the scene's close question on the page (it needs a dirty scene
  with a saved id); wired as the editor's, and the editor's is driven.
- Phase 5 gate: gui_smoke 6453 checks + 1 skip, twice, floor 6454 unchanged; chrome_smoke 462/462 on the final code.
