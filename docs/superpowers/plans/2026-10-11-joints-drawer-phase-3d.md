# Joints Drawer as HTML (Migration Phase 3d) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In HTML-chrome mode the editor's chrome page draws and drives the Joints drawer. The port is faithful:
- every planned joint, broken ones first;
- a row opens in place, and a click also selects the joint;
- an open row shows a to-scale ruler per cut piece: ticks for fasteners, a band for housings and interlocks, tick labels kept apart on two lines, and the edge word at zero where it fits;
- the written fallback, used when no single edge applies or the labels still collide;
- the caveat with its caution mark;
- delete on an open row (one checkpoint with Undo);
- the empty state.

The card wears the theme. The Qt `JointsPanel` stays built, and gated by `gui_smoke`, until Phase 6.

**Architecture:** Settings and Versions set the pattern; this card follows it with one new split.

**Content in C++, once.** What a row SAYS and MEASURES moves out of `JointsPanel.cpp` into `session/JointRows`:
- names, the kind line, the reason, the caveat, depth texts, marks in mm;
- each mark's position along the run as a fraction, and its label;
- the written lines and the shared line.

The Qt drawer and the new `session/JointsModel` both read `JointRows`, so the two drawers cannot word a joint differently. This is the `SettingsEdits` precedent.

**Pixel layout in the page.** Where a label goes is the page's job: `web/src/editor/joints/ruler.ts` is a pure port of `layoutStrip()`, which places labels greedily on two lines, decides the edge word, and falls back when labels collide. It is tested with a fake measurer. Pixels never cross the channel.

**Card and model.** The card's height is the page's, through the Versions card's `reportCardHeight`. `JointsModel` owns which rows are open, by joint id, as the Qt drawer does. It reaches `setSelectedJoint`/`deleteJoint` and the action through hooks, and a hidden drawer reads nothing.

**Tech Stack:** C++17, Qt 6.11.1 (installer build, WebEngine + WebChannel), OCCT 8.0.1, Svelte 5 + TypeScript + Vite, vitest + jsdom.

**Spec:** Read these four together:
- `src/ui/JointsPanel.{h,cpp}`: the behaviour being ported. Read the header comment in full, and `contentFor()`, `layoutStrip()`, `layoutRow()` and the row's `paintEvent()`.
- CLAUDE.md "Joinery: a joint is a relationship, never a world position": a joint breaks loudly and with no numbers, and the reason ends with a next step.
- CLAUDE.md (worktree) "The HTML UI", including "What a page over the viewport owes", the Versions card bullets and "Keyboard claims belong to a CARD".
- `D:/PizzaData/C++/FurnifyMe/DESIGN.md`: the locked style and its themes.

## Global Constraints

**Where and how:**
- Branch `joints-drawer` (cut from `versions-drawer`), worktree `D:/PizzaData/C++/FurnifyMe-html-spike`.
- Build with `cmake --build --preset windows-web`, AND build the `windows` preset before calling the phase done (the Versions review's C1).
- Never merge, push or delete without asking.

**`gui_smoke`:** it never enables HTML chrome. Its gate is **6448 checks + 1 environment skip, 0 failures, twice, quiet**, on the final tree, with `kCheckFloor` (6449) untouched. The `JointRows` refactor of the Qt drawer must not move that count.

**Copy:**
- Verbatim from `JointsPanel.cpp`, through `JointRows`.
- The `tr()` context stays `"JointsPanel"`, so an existing translation still applies.
- Every painted string of this app's own passes `web/test/banned.ts`. Piece names are the user's and are exempt; the vocabulary law's stem exception keeps "Joint"/"Joints" legal.

**A broken joint:**
- carries NO number: no ruler, no readout, no caveat;
- its names are painted in `dangerText`;
- its row has a danger ground and a 3 px danger bar at its left edge;
- it sorts to the top.

**A selected joint:**
- has an accent ground at 0.22 alpha and a lighter accent bar.

**Ruler geometry:**
- `kBarInset` 6, `kBarHeight` 8, `kTickOverhang` 2, `kZeroOverhang` 3, `kLabelGap` 3, `kStripGap` 6;
- row padding L 13 / R 11 / T 6 / B 7;
- tick labels in the badge font, names in the label font at weight 600;
- the depth text in the label font, muted, right-aligned.

**Card:** width `JointsPanel::cardWidth()`, at least 60 tall; the list scrolls past 420 logical px with the app's scrollbar.

**Interaction:**
- A row's click toggles it AND selects the joint.
- A double-click toggles once. The Qt drawer swallowed the trailing release; the page ignores a click with `detail >= 2`.
- Delete is offered only on an open row, is disabled while sketching or in render mode, and is ONE checkpoint with a Note offering Undo.

**Evidence:** every new check is watched red before its code exists. A page change is looked at in composited captures.

## Rulings (decided while planning; each costs what it says if wrong)

- **R1. Content is extracted, layout is ported.**
  - `JointRows` takes over every string and number the Qt drawer computes in `contentFor()` and in `layoutRow()`'s written and shared lines.
  - `JointsPanel` keeps its pixel layout and paint, now reading `JointRows`.
  - The page re-implements only the layout, in `ruler.ts`, with the same constants.
  - *Cost:* two layout implementations until Phase 6 deletes the Qt one, which is the plan for every card.
- **R2. Tick and band positions cross as FRACTIONS of the run** (0..1, clamped, as `xAt()` clamps). The page multiplies by its own bar span, so a row lays out the same at any type scale.
  - *Cost:* none.
- **R3. The model owns `expanded`, by joint id**, intersected with the live joints on every read. That is the Qt drawer's own rule, so a row stays open across the rebuild a document change causes.
  - *Cost:* the page cannot open a row on its own. That is intended: a click asks C++.
- **R4. A hidden drawer reads nothing** (`shown` term, the Versions review's I1). Showing it reads the document and the cached derivations once.
  - *Cost:* none.
- **R5. One ruler font measurement per string, by canvas.** The page measures with a 2D canvas context set to the painted font, through `measurerFor()` (`web/src/hub/usedIn.ts`, already shipped for the hub). It does not use a DOM span, so no layout thrash.
  - *Cost:* a canvas font must match the CSS one, which the theme's `--font-family` provides.

## Review Focus

1. **A theme or type-scale change re-lays the rulers.** An open row drawn at one font must not keep label positions measured at another. The ruler lays out from `$derived` state that reads the theme's font sizes, and every theme is captured.
2. **A broken joint shows no number anywhere.** No digit may appear in the row's own copy (the names are the user's), even when the row was open when it broke. (Task 1 and Task 4 tests, Task 5 chrome_smoke.)
3. **A row stays open across a document change, and the right one does.** Breaking a joint moves it to the top; the row that was open is still the open one, keyed by id, not by index. (Task 2 model test, Task 4 vitest.)
4. **A double-click toggles once, and a click on the delete control never toggles the row.** (Task 4 vitest.)
5. **The card leaves cleanly.** Hidden mid-anything, the drawer reads nothing more. Its slot shrinks with its list, so no tall slot keeps swallowing presses over the viewport. (Task 2 model test, Task 5 chrome_smoke.)

---

## File Structure

| File | Responsibility |
|---|---|
| `src/session/JointRows.{h,cpp}` (new) | What a joint's row says and measures. No widgets, no fonts. Also `reasonText()`. |
| `src/ui/JointsPanel.cpp` | Reads `JointRows` for content and for the written and shared lines; `reasonText()` delegates. Layout and paint unchanged. |
| `src/session/JointsModel.{h,cpp}` (new) | The drawer's model for the page: rows, words, `live`/`enabled`, expanded-by-id, hooks. |
| `tests/joints_model.cpp` (new, ctest `joints_model`) | `JointRows` and `JointsModel`, headless, on real boards and `Joinery::derive`. |
| `src/MainWindow.{h,cpp}` | The joints slot and model in `useHtmlChrome()`; `jointsSurface()`; `syncJointsModel()`. |
| `web/src/editor/joints/{api,mock,ruler}.ts` (new) | The `joints` object's shape, a mock, the ruler layout. |
| `web/src/editor/joints/{Joints,JointRow}.svelte` (new) | The card and one row. |
| `web/src/editor/{Editor.svelte,main.ts}` | Mount the card. |
| `web/test/{ruler,joints}.test.ts` (new) | The layout, and the card against the mock. |
| `tests/chrome_smoke.cpp` | The drawer end to end; captures per theme. |

---

### Task 1: JointRows - what a joint's row says, once

**Files:**
- Create: `src/session/JointRows.h`, `src/session/JointRows.cpp`, `tests/joints_model.cpp`
- Modify: `src/ui/JointsPanel.cpp` (content from `JointRows`); `CMakeLists.txt` (furnify_session sources; the `joints_model_tests` target and ctest `joints_model`)

**Interfaces:**
- Produces, in `namespace JointRows`:
  - `struct Mark { double mm; double at; QString text; }`: `at` is the 0..1 position along the run; `text` is `numberOnly(mm)`.
  - `struct Piece { QString name, depthText; bool band; std::vector<Mark> marks; Mark bandStart, bandEnd; QString numbers, writtenLine; }`
  - `struct Row { int jointId; bool broken, selected, expanded, fastener, edgeNamed; QString nameA, nameB, kindLine, caveat, edge, unit, readoutText, sharedDrawn, sharedWritten; double run, inset, depthA, depthB, width; std::vector<Piece> pieces; }`
  - `Row content(const DocumentModel&, const DocumentModel::Joint&, const Joinery::Derivation&, int selectedJoint, bool expanded)`
  - `QString reasonText(const std::string&)`, `QString lengthText(double)`, `QString numberOnly(double)`
  - `QString title()` ("Joints"), `QString emptyText()`, `QString deleteTooltip()`

- [ ] **Step 1: Write the failing test** `tests/joints_model.cpp`. Its body:
  - two 600 × 300 × 18 boards meeting at a T, with a dowel joint between them (the `tests/joinery.cpp` pattern: `Joinery::defaultsFor(Dowel, 18.0)`, `doc.addJoint`);
  - `Joinery::derive(...)` on their shapes.

  It checks:
  - `content()` gives `kindLine` = `"Dowel, <n> × <size>"`, with `n` from `items.size()` and size from `Measure`;
  - two pieces with `depthText` starting `"drill "`;
  - every mark's `at` in [0, 1], equal to `mm / run` within 1e-9;
  - `edge` non-empty;
  - `sharedDrawn` = `"mm from the <edge> edge · inset <n> mm from the face"`;
  - `readoutText` carries the depth texts;
  - for the same joint after moving board B 50 mm away, then re-deriving: `broken`, `kindLine` = `"Broken — these two pieces don't meet — move the pieces so they meet"`, no pieces, an empty caveat, and no digit in `kindLine`;
  - for a housing (Dado, `Joinery::defaultsForContact`): one or two band pieces, `bandStart.at < bandEnd.at`, and `depthText` = `"<w> × <d> mm"`;
  - `reasonText("contact: anything")` is the measured-squarely sentence; `reasonText("the pieces aren't crossing")` ends with `"move one so it crosses the other"`.

  Register `joints_model_tests` (linking `furnify_session`) and ctest `joints_model`.

- [ ] **Step 2: Run it and watch it fail**: compile errors, because `JointRows` does not exist.

- [ ] **Step 3: Implement** `JointRows`. Move the bodies, sentence for sentence:
  - from `JointsPanel.cpp`: `reasonText`, the `contentFor()` content, `lengthText`, `numberOnly`, `emptyText`, `deleteTooltip`;
  - from `layoutRow()`: the written lines ("from the %1 edge: %2" or the bare numbers, and "%1 to %2" for a band) and the shared lines.

  Every `tr()` becomes `QCoreApplication::translate("JointsPanel", ...)`. Add `Mark::at` (`run > 0 ? clamp(mm, 0, run) / run : 0`).

  In `JointsPanel.cpp`:
  - `contentFor()` copies from `JointRows::content()`;
  - `layoutRow()` reads `piece.writtenLine`, `m.sharedDrawn` and `m.sharedWritten` instead of composing them;
  - `JointsPanel::reasonText` returns `JointRows::reasonText`;
  - `emptyText()` and `deleteTooltip()` call `JointRows`.

  Painting and layout stay untouched.

- [ ] **Step 4: Run and pass**:
  - `joints_model_tests`;
  - ctest;
  - `gui_smoke` filtered to the joint drawer's blocks (`joints-drawer`, `joints`).

  Expected: PASS, with the joint blocks' counts unchanged.

- [ ] **Step 5: Commit**: `"JointRows: what a joint's row says, once (Phase 3d)"`.

### Task 2: JointsModel - the drawer's model for the page

**Files:**
- Create: `src/session/JointsModel.{h,cpp}`
- Modify: `tests/joints_model.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces `JointsModel(Hooks, QObject*)`, with:
  - `Hooks{ std::function<void(int)> select; std::function<bool(int)> remove; std::function<void()> close; }`;
  - properties:
    - `rows`: QVariantList of maps with `id, broken, selected, expanded, fastener, edgeNamed, nameA, nameB, kindLine, caveat, edge, sharedDrawn, sharedWritten, pieces`; each piece is a map with `name, depthText, band, marks [{at, text}], bandStart {at, text}, bandEnd {at, text}, writtenLine`;
    - `live` and `enabled` (bool);
    - `words` (CONSTANT: `title, empty, deleteTip, close`);
  - `setState(const DocumentModel*, std::vector<Joinery::Derivation>, int selectedJoint, bool live, bool enabled, bool shown)`;
  - slots `activate(int jointId)`, `remove(int jointId)`, `close()`, `isExpanded(int) const`;
  - signal `changed()`.

- [ ] **Step 1: Failing tests** (appended to `tests/joints_model.cpp`):
  - `setState` with two joints, one broken: the broken one is row 0;
  - `activate(id)` marks it expanded and calls `select(id)` once; a second `activate` closes it and selects again;
  - after the document changes so the open joint becomes broken and moves to row 0, the OPEN row is still that joint's (Review Focus 3);
  - a removed joint's id leaves the expanded set;
  - `remove(id)` reaches the hook only while `enabled`;
  - `shown = false` reads nothing: a changed document, then setState(hidden), emits nothing and the rows stay as they were;
  - `live = false` empties the rows;
  - `changed()` fires once per real change and never for an identical state;
  - the words are verbatim.

- [ ] **Step 2: Watch them fail.**
- [ ] **Step 3: Implement** with `JointRows::content()` per joint, broken first (`std::stable_partition`), and expanded intersected with the live ids.
- [ ] **Step 4: Pass.**
- [ ] **Step 5: Commit**: `"JointsModel: the Joints drawer's model, headless"`.

### Task 3: MainWindow - the joints slot and model

**Files:** Modify `src/MainWindow.{h,cpp}`, `tests/chrome_smoke.cpp`.

**Interfaces:**
- Produces:
  - `JointsModel* MainWindow::jointsModel() const`;
  - a slot named `htmlCardSlot_joints`;
  - `jointsSurface()`;
  - `syncJointsModel()`.

- [ ] **Step 1: Failing check** in chrome_smoke: the Joints action shows `htmlCardSlot_joints`, the Qt `JointsPanel` stays hidden, and `window.jointsModel()` exists.
- [ ] **Step 2: Watch it fail** (compile).
- [ ] **Step 3: Implement**, in the Versions card's shape:
  - The slot is `QSize(JointsPanel::cardWidth(), 60)`, starts hidden, and goes through `addCard`. `contentHeightChanged` triggers `relayout`. On a theme change its width follows `JointsPanel::cardWidth()`. `cardWidth()` becomes public.
  - Hooks: `select` → `setSelectedJoint`, `remove` → `deleteJoint`, `close` → uncheck `myJointsPanelAction`.
  - Register the model as `joints`.
  - `syncJointsModel()` runs on `appStateChanged` and in the action's toggled lambda. It passes:
    - `&myDocument`;
    - `shown ? jointDerivations() : {}`;
    - `selectedJointId()`;
    - `live = !myShowingInitScreen`;
    - `enabled = live && !mySketching && !renderModeEnabled()`;
    - `shown = jointsSurface()->isVisible()`.
  - Add the swap to the `liveChanged` lambda.
  - Replace the two `myJointsPanel->setVisible(...)` calls with `jointsSurface()`.
  - The model include sits OUTSIDE the web guard.
- [ ] **Step 4: Pass**: chrome_smoke and the `windows` preset build.
- [ ] **Step 5: Commit.**

### Task 4: The page - ruler and card

**Files:**
- Create: `web/src/editor/joints/{api,mock,ruler}.ts`, `web/src/editor/joints/{Joints,JointRow}.svelte`, `web/test/{ruler,joints}.test.ts`
- Modify: `web/src/editor/{Editor.svelte,main.ts}`

**Interfaces:**
- `ruler.ts`: `layoutStrip(piece, edge, x0, x1, measure): StripLayout | null`.
  - `measure(text): number` is the badge font's width.
  - `StripLayout = { barLeft, barRight, ticks: number[], band: [number, number] | null, labels: {text, left, line: 0 | 1}[], edgeLine: -1 | 0 | 1, staggered: boolean }`.
  - `null` means the labels collide even on two lines, so the row writes its numbers.
  - It is a line-for-line port of `layoutStrip()`'s placement, with `xAt = barLeft + at * (barRight - barLeft)`.
- DOM for the end-to-end test:
  - `.joints-card`, `.joints-card .empty`;
  - `.joint[data-joint="<id>"]`, with the classes `.broken`, `.selected` and `.open`;
  - `.joint .names`, `.joint .kind`, `.joint .caveat`;
  - `.joint .strip`, holding `.bar`, `.tick`, `.band`, `.tick-label` and `.edge-word`;
  - `.joint .written`, `.joint .shared`, `.joint .delete`.

- [ ] **Step 1: Failing tests.**

  `ruler.test.ts`, with `measure = (t) => t.length * 6`:
  - three marks well apart → all on line 0 and `staggered` false;
  - two marks 4 px apart → the second on line 1;
  - three marks within 4 px → `null`;
  - a band → two ticks and the band span;
  - the edge word fits before the first label → line 0; it doesn't fit there but does before line 1's first → line 1; it fits on neither → -1;
  - labels clamp inside [x0, x1].

  `joints.test.ts`:
  - rows render broken-first in the order given;
  - a broken row has the `.broken` class, no `.strip` and no digit in `.kind`;
  - a click calls `activate(id)` once;
  - a `detail: 2` click calls nothing;
  - `.delete` appears only on an `.open` row, and its click calls `remove(id)` and not `activate`;
  - `enabled` false disables `.delete`;
  - an open fastener row draws one `.strip` per piece with `.tick`s;
  - an open row whose ruler is `null` (forced by a mock with crowded marks) shows `.written` lines instead;
  - `edgeNamed` false writes the edge sentence first;
  - the empty state shows when live with no rows;
  - the card reports its height;
  - the close × calls `close()`;
  - no banned word appears in the words or in a row's own copy.
- [ ] **Step 2: Watch them fail.**
- [ ] **Step 3: Implement.**

  **The card:** `Joints.svelte`, with the Versions card's frame. The header holds the title and the count (`textMuted`, label font) with the × at the right. The list is `max-height: 420px` with the app's scrollbar, `rows` is re-read on `changed`, and the height is measured and reported as Versions does (border included).

  **`JointRow.svelte`:**
  - Names are elided with CSS (`text-overflow: ellipsis`) to share a line, with " ↔ " between them.
  - The kind line wraps.
  - The caveat has an inline SVG caution triangle in `--caution`.
  - When open and not broken, it lays out each piece's strip with `layoutStrip()` at the row's content width. The width is measured once on mount and on resize; fonts come from computed style. If any strip comes back `null`, every piece is written as `.written` lines, with the edge sentence first when `!edgeNamed`.
  - It draws, with absolutely positioned elements inside a relatively positioned strip box: the bar (`--chip-hover` fill, `--border` edge, 1.5 px radius), the band (accent at 0.55 alpha), the zero tick (muted, 1.4 px), item ticks (accent lightened), labels (badge font) and the edge word (muted).
  - The shared line goes under the strips.
  - The ground and the left bar mark broken or selected state.
  - The chevron and `.delete` (`use:chip`, the Delete glyph as inline SVG) show on an open row.
  - Click handlers: `onclick={(e) => e.detail < 2 && activate()}`; the delete button calls `stopPropagation()`.
  - Wire into `Editor.svelte` like Versions (`keyboardFor` is not needed: the card has no typed field), and add the dev mock in `main.ts`.
- [ ] **Step 4: Pass**: vitest, svelte-check and `npm run build`.
- [ ] **Step 5: Commit.**

### Task 5: The drawer end to end in the real app

- [ ] **The chrome_smoke block.** Seed a second furniture with two boards in a T and a dowel joint through the window (the joint chip's commit path, or `DocumentModel::addJoint` plus a checkpoint through a public window call the suite already uses; read `gui_smoke`'s joints blocks for the seam). Then:
  - open the drawer: the card covers its slot and shows one row with the kind line;
  - click the row: it opens, `window.selectedJointId()` is that joint, a ruler draws with as many ticks as dowels, and the edge word or the shared line names the edge;
  - move board B away (`transformBody`) so the joint breaks: the row turns `.broken`, still open by id, with no `.strip` and no digit in its kind line;
  - Undo: it reads whole again;
  - delete from the open row: the joint is gone, a Note offers Undo, and Undo brings it back;
  - Joints off: the slot is hidden and the slot's former rect leaves the hit region;
  - captures per theme with the row open.
- [ ] **Mutations, each to a named red line:**
  - the page ignores `detail` (a double-click toggles twice);
  - `JointsModel` loses the broken-first sort;
  - `ruler.ts` never staggers.
- [ ] **Commit.**

### Task 6: Gate, documentation, board, review

- [ ] ctest in both presets; vitest; svelte-check; chrome_smoke; hub_smoke; `gui_smoke` twice, unfiltered and quiet. Record each below.
- [ ] CLAUDE.md (worktree): architecture rows; a "The Joints card" bullet (R1, R2, the ruler port).
- [ ] Opus whole-branch review; fix Critical and Important findings test-first; build both presets again.
- [ ] Board card done with notes; nothing merged.

## Gate record

| Gate | Result |
|---|---|
| ctest (both presets) | |
| vitest / svelte-check | |
| chrome_smoke | |
| hub_smoke | |
| gui_smoke run 1 | |
| gui_smoke run 2 | |
