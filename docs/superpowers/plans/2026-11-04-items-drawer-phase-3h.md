# The Items Drawer as HTML (Migration Phase 3h) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** In HTML-chrome mode the editor's chrome page draws and drives the Items drawer, a
faithful port of `ItemsPanel`:
- the tree of bodies, outlines and folders, folded by default, indented by depth;
- the eye on every row (a folder's speaks for everything under it) and the grain mark on body rows;
- the selection mark (fill + bar), a partly selected folder's `2/5`, the pending outline's words;
- click / Ctrl / Shift selection, a folder click selecting its bodies, an outline click choosing
  what Extrude consumes;
- double-click and F2 rename in place, the right-click row menu with Move into;
- the filter with its highlight, held-open folders, the no-match line and the hidden-pick foot;
- reveal of a viewport pick;
- the list's keys (Up/Down, Shift, Home/End, Left/Right, F2, Alt+arrows, and Down/Enter from the
  filter);
- drag and drop with the line, the ring and the lit title, spring-loading, auto-scroll, a ghost,
  Escape, and refusals with their reasons.

The card wears the theme. The Qt `ItemsPanel` stays built, and gated by `gui_smoke`, until Phase 6.

**Architecture:** the Joints card's split, at a larger scale.
- **The logic is C++, once, headless:** `session/ItemsModel` owns the tree as the page sees it
  (visible rows only), folds, the filter, the keys' row and the Shift anchor, the selection marks,
  reveal and the hidden-pick foot, the row menu's entries, the key commands, and the drag's dry run
  and words. It writes nothing itself; every write goes through a hook to the code the Qt drawer's
  signals reach (`MainWindow` checkpoints and reports).
- **The page draws and gestures:** rows, the inline rename field, the menu, pointer gestures (the
  click-vs-drag threshold, drop zones with hysteresis, spring and auto-scroll timers, the ghost).
  It never decides a refusal or writes a sentence.
- **One key table:** `session/ItemsKeys` is `ItemsPanel::listKeys()` moved; the Qt drawer, the
  shortcut sheet, the model (which sends it to the page) and the chrome's list hold all read it.

**Tech Stack:** C++17, Qt 6.11.1 (installer build: WebEngine + WebChannel), OCCT 8.0.1, Svelte 5 +
TypeScript + Vite, vitest + jsdom.

**Spec:** read these together.
- `src/ui/ItemsPanel.{h,cpp}`: the behaviour being ported. Read it in full.
- CLAUDE.md "The Items drawer is a map you can trust (T-063)", and "The HTML UI" (the Versions
  and Joints card bullets, "Keyboard claims belong to a CARD", "A typed field takes the keyboard
  and gives it back", "What a page over the viewport owes").
- `DESIGN.md`: the locked style and its themes; `assets/themes/*.json` `shell.list` blocks.

## Global Constraints
- Branch `items-card` from `main` (`3687008`). Build `windows-web` AND `windows` before calling
  the phase done. Never merge, push or delete without asking.
- `gui_smoke` never enables HTML chrome. Its gate is **6448 + 1 skip, 0 failures, twice**, on the
  final tree, `kCheckFloor` (6449) untouched. Task 1's moves must not change a single count.
- Copy is `ItemsPanel`'s, verbatim, through `QCoreApplication::translate("ItemsPanel", ...)`, and
  `ItemMoveText` for every move sentence. Every string of this app's own passes
  `web/test/banned.ts`; item names are the user's and are exempt.
- A name is never markup: the highlight crosses as a match range and the page builds text nodes.
- No control takes focus (`use:chip`); the list keeps DOM focus for its keys and paints no ring
  (the selection mark is the focus cue).
- Card: width `ItemsPanel::cardWidth()`, at least 176 tall, at most to the viewport's bottom less
  16 (`rowsHeightCap()`); the rows scroll inside it with NO scrollbar drawn (improvements item 12).
- Geometry: indent 12 per level, row inset 6, row buttons 24, drag threshold 6, hysteresis 4,
  spring 700 ms, auto-scroll band 20 / step 8 / tick 30 ms, carried rows at 45% ink.

## Rulings (decided while planning; each costs what it says if wrong)
- **R1 Logic in `ItemsModel`, drawing in the page; the Qt drawer keeps its own copy until Phase 6**
  (the HubModel/VersionsModel precedent). *Cost:* two copies of the selection, key and menu rules
  for a while; the headless tests and chrome_smoke hold the HTML copy to the Qt one's behaviour.
- **R2 One key table** (`session/ItemsKeys`), moved out of `ItemsPanel` with its statics left as
  forwards, so gui_smoke and the shortcut sheet compile unchanged. *Cost:* none.
- **R3 The list HOLDS its keys; a field CLAIMS the keyboard.** A row press gives the page Qt focus
  and the chrome accepts `ShortcutOverride` only for the table's list-scope keys, so Ctrl+Z, E and
  every other shortcut still work while the list is focused - as they do in the Qt drawer. The hold
  ends when focus leaves the page (a click on the viewport, which is StrongFocus) or the card goes.
  *Cost:* a key the table gains later must be added once, in the table.
- **R4 Rows cross VISIBLE only, each carrying its drop targets** (above, below, into) worked out in
  C++, so the page's zone geometry never asks which sibling follows; the refusal and the words are
  the model's dry run (`dragOver`). *Cost:* a few more fields per row.
- **R5 The row menu is the model's description, the page's drawing**: entries with enabled
  states, tooltips and key text, chosen by id. A press outside closes it and is swallowed
  (QMenu's behaviour). *Cost:* a page menu is not a native one (no OS animation).
- **R6 Rename is the page's field under R5 of Phase 3b**: Enter or a click away commits, Escape
  cancels; the model trims and refuses empty (InlineRename's rule). `MainWindow`'s calls that open
  a rename (F2, a new folder) go to the model while the card is the surface. *Cost:* none.
- **R7 A hidden drawer builds nothing**, but a pick still opens its row's folders (the Qt drawer
  does, through `ensureFresh()`), so the row is there when the drawer opens. *Cost:* none.
- **R8 The ledger (Ultraviolet) is CSS**: the model always sends its words; `themes.css` shows the
  count, headings and foot only where the theme's list asks for them, with the theme's list
  numbers ported from `shell.list`. *Cost:* the numbers live twice (JSON for Qt, CSS for the page).

## Review Focus
1. **Keys while the list is focused:** arrows walk the list, and Ctrl+Z, E or Delete still reach the
   window; a click on the viewport gives every key back. (Task 4 chrome_smoke.)
2. **A name is data:** `<b>Top</b>` and `a & b` render as typed, highlighted or not. (Task 6 vitest.)
3. **A drag that never sees its release** (a window blur, the card hidden, the furniture closed)
   leaves no ghost, no hold and no status line. (Task 6 vitest, Task 7 chrome_smoke.)
4. **A filtered list never reorders:** Alt+Up/Down and Move up/down refuse with the RF3 sentence; only
   into-folder drops are offered. (Task 3 model tests, Task 6 vitest.)
5. **The card follows the document without losing your place:** a rename, a move, an undo keep the
   folds, the filter and the keys' row. (Task 2 model tests, Task 7.)

---

### Task 1: The key table and the move words in the session library
**Files:** create `src/session/ItemsKeys.{h,cpp}`; move `src/ui/ItemMoveText.{h,cpp}` to
`src/session/`; modify `src/ui/ItemsPanel.{h,cpp}` (forwards), includes, `CMakeLists.txt`.
- `ItemsKeys::Command` (the `ListCommand` values), `ItemsKeys::Key {command, keys, label,
  fromFilterField}`, `table()`, `forKey(QKeyCombination, bool fromFilterField)`, `label()`,
  `text()`, `sequence()` - the bodies of `ItemsPanel::listKeys()`/`listKeyFor()`/`listKeyLabel()`/
  `listKeyText()`/`listKeySequence()`, `tr` context kept `"ItemsPanel"`.
- `ItemsPanel` keeps `using ListCommand = ItemsKeys::Command; using ListKey = ItemsKeys::Key;` and
  its five statics as one-line forwards.
- [ ] Build both presets; `gui_smoke` filtered to the items blocks: counts unchanged.
- [ ] Commit.

### Task 2: ItemsModel - the tree, the marks, the filter, the menu
**Files:** create `src/session/ItemsModel.{h,cpp}`, `tests/items_model.cpp` (ctest `items_model`).
- `ItemsModel(Hooks, QObject*)`; `setState(const DocumentModel*, onScreen(id), selectedIds,
  pendingOutline, folderEditsAllowed, newFolderEnabled, newFolderTip, live, shown)`;
  `showSelection(ids)` (reveal, the hidden-pick foot); `forgetFurniture()`, `resetPick()`.
- Properties (re-read on `changed()`, which fires only when something moved): `rows` (visible, in
  order: id, kind, name, depth, parent, eyeOn, onScreen, grainAcross, marked, current, pending,
  partial, muted, match [start, length], open, held, the accessible names and tips, carried, and
  the three drop targets); `filter`, `noMatch`, `foot`, `empty`, `menu`, `rename`, `drag`,
  `ledger`, `newFolder`; CONSTANT `words` and `keys`.
- Slots: `activate(id, kind, ctrl, shift)`, `toggleFold(id)`, `toggleEye(id)`, `toggleGrain(id)`,
  `setFilter(text)`, `clearFilter()`, `beginRename(id)`, `commitRename(id, text)`,
  `cancelRename()`, `openMenu(id)`, `chooseMenu(entry)`, `closeMenu()`, `newFolder()`.
- [ ] Failing tests first, on a real `DocumentModel`: folders fold by default and open by id; the
  order is `orderedChildren()`; marks (a body, a whole folder, a partial `2/5` with its words, the
  pending outline unmarked); Ctrl toggles, Shift takes visible body rows between the anchor and the
  click, a folder click selects its bodies (Ctrl adds); the filter matches literally and
  case-blind, holds ancestors open without writing folds, mutes them, and an emptied field puts the
  folds back; no-match; reveal opens ancestors and keeps them open, a pick the filter hides raises
  the foot; the eye on a folder writes every body under it; rename trims and refuses empty; the menu
  entries and their enabled states for body, outline and folder rows, Move into lists only
  `checkMove() == Moved` folders, and the filtered refusals; a hidden model builds nothing but a pick
  still opens folders; `changed()` once per real change; every word verbatim.
- [ ] Implement, pass, commit.

### Task 3: ItemsModel - the keys and the drag
- Slots: `key(command, shift)` (the table's commands); `beginDrag(id)`, `dragOver(kind, parent,
  before)` (kind `none`/`line`/`into`/`top`/`outside`), `endDrag(commit)`; property `drag`
  `{active, ids, refused, status, ghost {kind, name, more}}`.
- [ ] Failing tests: Up/Down/Home/End select as a click does; Shift extends; Left folds or goes to
  the parent, Right opens or goes to the first child; held folders do not fold; F2 asks to rename
  only over a live row; Alt+Up/Down step through `siblingStepTarget()`, refused while filtered with
  the RF3 sentence; Alt+Left `outOfFolderTarget()`; Alt+Right into the folder above, or the "no
  folder above" refusal; Enter from the filter lands on the first MATCHING row; a drag carries the
  selection when pressed on a selected body of several; `dragOver` reads `checkMove()` for refusal
  and `ItemMoveText` for the words; outside says release-to-cancel; `endDrag(true)` asks the move
  hook once and `endDrag(false)` never; the drag line reaches the status hook.
- [ ] Implement, pass, commit.

### Task 4: The chrome's list hold
**Files:** `src/web/EditorChrome.{h,cpp}`; `tests/chrome_smoke.cpp`.
- `ChromeBridge::holdListKeys(card)` / `releaseListKeys(card)`: Qt focus to the view's proxy; while
  held (and no field claims the keyboard) a `ShortcutOverride` is accepted only for
  `ItemsKeys::forKey(..., false)`; the hold ends on the proxy's `FocusOut`, on release, on the card
  going, and on a dead page.
- [ ] Failing chrome_smoke checks (with a stub hold, before the card exists): while held, Up is
  claimed and Ctrl+Z is not; a press on the viewport ends the hold.
- [ ] Implement, pass, commit.

### Task 5: MainWindow - the items slot and model
- `myItemsSlot` (`htmlCardSlot_items`, TopLeft in the Qt drawer's place, its height the page's),
  `myItemsModel` registered as `items`, `itemsSurface()`, `syncItemsModel()` on `appStateChanged`.
- Hooks onto exactly what the Qt drawer's signals reach today (see the constructor's connections):
  selection, outline, eye (+ `visibilityToggled`'s Isolate step), grain, the two renames, moves,
  new folder, wrap, remove-keep, delete-folder, the quiet refusal and its withdrawal, the drag line.
- Every `myItemsPanel->` call that shows state goes to both; the gestures (`beginRename*`,
  `setGroupExpanded`, `dragStatusText`, `cancelDrag`) to the live surface.
- [ ] chrome_smoke: the Items action shows the slot and hides the Qt drawer; `itemsModel()` exists.
- [ ] Build both presets; commit.

### Task 6: The page
**Files:** `web/src/editor/items/{api,mock,drop}.ts`, `Items.svelte`, `ItemRow.svelte`,
`RowMenu.svelte`; `web/test/{drop,items}.test.ts`; `Editor.svelte`, `main.ts`, `themes.css`.
- `drop.ts`: `zoneAt(rowKind, open, y, h, previous)` - 25/50/25 for folders, halves otherwise, 4 px
  hysteresis toward the zone in hand - and `targetFor(row, zone)`; pure and tested.
- The card: title row (title, ledger count, the New folder chip mirroring the action), the filter
  field (typed-field claim), the scrolling rows, the no-match line, the foot, the ledger foot; the
  height measured and reported, capped at `innerHeight - top - 16`.
- [ ] Failing vitest: names render as text (Review Focus 2); the highlight wraps exactly the match;
  click / Ctrl / Shift reach `activate`; the twisty reaches `toggleFold` and is inert while held; the
  eye and grain reach their slots and never select; a double-click opens the field over the row and
  Enter commits, Escape cancels, blur commits; the menu draws the model's entries, disabled ones do
  nothing, a press outside closes it; the keys send the table's commands only while the list holds
  them; a press that travels 6 px is a drag, not a click; zones and hysteresis; outside cancels; a
  window blur cancels; Escape cancels; the ghost follows the pointer; carried rows dim; the empty
  state; the banned-word sweep over words and rendered text.
- [ ] Implement, pass, svelte-check, build; commit.

### Task 7: End to end
- [ ] chrome_smoke block: a furniture with a folder of three boards and two loose ones; open the
  drawer; rows and folds; a click selects (window selection); Ctrl/Shift; a folder click; the eye
  hides a body in the viewport; rename by double-click; F2; the menu's Move into; the filter, its
  highlight and held folders; a viewport pick reveals; the list keys (Up claimed, Ctrl+Z not, a
  viewport click gives them back); Alt+Down moves with one checkpoint; a drag into a folder moves
  with one checkpoint and a Note; Escape mid-drag moves nothing; Items off hides the slot. Captures
  per theme.
- [ ] Mutations, each to a named red line: the hold claims every key; the model's Shift range uses
  hidden rows; the page drops the drag threshold.
- [ ] Commit.

### Task 8: Gate, documentation, board, review
- [ ] ctest in both presets; vitest; svelte-check; chrome_smoke; hub_smoke; `gui_smoke` twice,
  unfiltered. Record below.
- [ ] CLAUDE.md: architecture rows; a "The Items card" bullet (R1-R4).
- [ ] Opus whole-branch review; fix Critical and Important findings test-first; build both presets.
- [ ] Board card done with notes; nothing merged.

## Gate record

**Commits** (branch `items-card` from `main` at `3687008`): `1e07473` key table and move words into
the session library; `07e70a2` ItemsModel; `0df460b` the list hold; `6cb6b3c` MainWindow wiring;
`d6b8d4a` the page; `4294281` end to end, with three fixes; `3f4f26a` review fixes.

| Gate | Result |
|---|---|
| ctest (both presets) | 23/23 in windows-web and in windows (vcpkg) |
| items_model_tests | 95/95 |
| vitest / svelte-check | 215/215; 0 errors, 0 warnings |
| chrome_smoke | 313/313 (42 new for the card, 6 for the list hold) |
| hub_smoke | 36/36 |
| gui_smoke run 1 | PASS 6448 + 1 skip, floor 6449 |
| gui_smoke run 2 | PASS 6448 + 1 skip, floor 6449 |

**Mutations, each to a named red line:** the Shift range over hidden rows (after a vacuous fixture
was found and fixed - the folded folder sat before the anchor); the filter writing folds; the hold
claiming every key; the drag threshold dropped; a name rendered as HTML; a refusal shown for any
target. The page was written before its tests (a ledgered departure, as in 3g); the mutations are
what prove the tests have teeth.

**Found by looking at captures and by the end-to-end checks, fixed in `4294281`:** the card could
grow but never shrink (it measured the list's `scrollHeight`); the drag ghost was never drawn (the
view's mask clips painting - a full-viewport layer stands in it for the drag); focus stayed in the
page after a hold with nowhere recorded to return to (`clearFocus()` does nothing in an inactive
window - the keys now go to the 3D view).

**Review (Opus, read-only):** 1 Critical, 3 Important, 4 Minor. Fixed, each watched red first: C1
the row menu could stay open for good after a document swap or a drag start, its layer and claim
taking every press and key; I1 a rename committed by a click away took focus back from where the
user clicked; I2 a double-click on a twisty, eye or grain mark opened a rename; I3 a rename C++
closed kept its keyboard claim, and one the card was hidden under blocked the next; M1 F2 claimed
only while the keys' row is on screen; M2 a double-click's second press selected again.

**Rulings:** R1-R8 as planned. I1's second suggestion (a hold that only records while focus is
outside the page) was declined: a row press does not move Qt focus into the NoFocus view, so the
hold has to take it, or the list's keys would never reach the page.

**Deferred minors:** M3 - a click on a Qt chip above the page runs while the row menu is open (QMenu
would have eaten it); the menu closes on the next document change. M4 - an Undo resets the keys' row
(`resyncView()` -> `resetPick()`), as the Qt drawer does: parity, though Review Focus 5 promised
more.
