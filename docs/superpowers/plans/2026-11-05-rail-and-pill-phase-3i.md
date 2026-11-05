# The Rail, the Pill and the Menus as HTML (Migration Phase 3i) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** In HTML-chrome mode the editor's chrome page draws the last of Phase 3's chrome:
- **the rail** (Items · Start Sketch, Extrude, Add shape · Union, Subtract, Intersect · Delete ·
  Undo, Redo; separators between groups), full height where the theme's card says `railFull`;
- **the view controls** under the axis card (Persp/Ortho, Wireframe, Fit All);
- **the pill**: the app mark, the wordmark and the menu titles (File, Sketch, Edit, Model, View,
  Help), dragging the window everywhere but on a title;
- **the menus**: every entry of the Qt menu bar, with its shortcut text, check and radio marks,
  disabled state, separators and the three submenus (Autosave, Units, Theme); mouse and keyboard
  (Alt+mnemonic, arrows, Enter, Escape, letters).

Every chip and entry mirrors its `QAction` (enabled, checked, text, tooltip) and triggers it; nothing
on the page holds state of its own. The Qt `ToolCluster`s, `AppBar` and `QMenuBar` stay built - and
gated by `gui_smoke` - until Phase 6.

**Architecture:** the card pattern (Phases 3b-3h) once more.
- **`session/MenuModel`** (Core + Gui, headless): the menu bar as a tree of `QAction`s, its open
  state (which menu, which submenu path, the current entry at each level), the QMenu keyboard rules
  (skip separators and disabled entries, wrap, Left/Right across menus and into submenus, mnemonic
  letters, Home/End, Escape one level), and the trigger (closes first, then triggers on the next
  turn - an action may open a modal file dialog). Tested by `menu_model_tests`.
- **`web/ShellBridge`** (web preset): the page's `shell` object. It walks the `QMenuBar` into a
  `MenuModel`, lists the rail's and the view cluster's chips (action id + glyph), sends the actions'
  state through an `ActionWatch`, sends each glyph as an ALPHA MASK PNG painted by `IconSet` (one
  glyph source; the page tints it with CSS `mask-image`), and receives what the page measures (each
  chip's rectangle, the pill's titles).
- **`MainWindow`** swaps three `HtmlCardSlot`s in the Qt surfaces' places while the page is live,
  exactly as the drawers do.
- **The page** (`web/src/editor/shell/`) draws the rail, the view cluster, the pill and the menus.

**Tech Stack:** C++17, Qt 6.11.1 (installer build: WebEngine + WebChannel), OCCT 8.0.1, Svelte 5 +
TypeScript + Vite, vitest + jsdom.

**Spec:** read these together.
- `src/ui/ToolChip.{h,cpp}`, `src/ui/ToolCluster.{h,cpp}`, `src/ui/AppBar.{h,cpp}` and
  `MainWindow::buildMenus()` / `buildOverlay()`: the behaviour and look being ported.
- `Theme::install()`'s `QMenuBar`/`QMenu` rules (Graphite's menus) and `ThemeShell.h`'s `Card`,
  `Chip` and `Menu` (every other theme's).
- CLAUDE.md "The HTML UI" (every card bullet, the keyboard and mask rules) and "The shell is
  action-driven, and composed as bar + rail + drawer".

## Global Constraints
- Branch `rail-pill` from `main` (`12eaeb3`). Build `windows-web` AND `windows` before calling the
  phase done. Never merge, push or delete without asking.
- `gui_smoke` never enables HTML chrome. Its gate is **6448 + 1 skip, 0 failures, twice**, on the
  final tree, `kCheckFloor` (6449) untouched.
- No new copy: a chip's words are its action's (label; the tooltip composed as
  `ToolChip::syncFromAction()` composes it), an entry's are its action's text (mnemonic shown as an
  underline only while the keyboard opened the menu) and its shortcut's portable text. Every string
  passes `web/test/banned.ts`.
- No control takes focus (`use:chip`); keys reach the menu through a keyboard claim.
- Sizes: Qt's own. Graphite chip 34 px icon-only, radius 6, icon 16; cluster pad 8, gap 3, separator
  band 7 (inset 7), card radius 10; pill pad Y 6, radius = half its height, mark 20, gap
  `Theme::Space::Tight`; menu bar item 6/12 px padding; menu 4 px padding, item 6 24 6 12, separator
  1 px at 4/8 margins. A theme's `shell.chip` / `shell.menu` / `shell.card` numbers override them.

## Rulings (decided while planning; each costs what it says if wrong)
- **R1 Scope.** In: the rail, the view cluster, the pill and its menus. Out, staying Qt this phase:
  the axis gizmo (it follows the camera - Phase 4's shape), the window buttons (their maximize
  traffic is non-client, `WindowChrome`'s), the Add-shape flyout and the render Back chip (render
  layer). *Cost:* four Qt surfaces over the viewport a while longer.
- **R2 One glyph source.** `IconSet::paintGlyph()` paints each glyph once per theme and device
  ratio as a white-on-transparent PNG; the page uses it as a CSS mask filled with the state's ink.
  *Cost:* a data URL per glyph (~13 small PNGs) re-sent on a theme change.
- **R3 Theme numbers travel as data.** `ThemeBridge` adds the theme's `shell.chip`, `shell.menu` and
  `shell.card.radius/railFull` as `--shell-*` custom properties (absent for Graphite, whose CSS
  fallbacks are Qt's Graphite paint). Phase 3h hand-copied `shell.list` into CSS (its R8); this
  one does not repeat that. *Cost:* none.
- **R4 The menu model is C++.** Navigation, mnemonics, which entries are live, and the trigger are
  `MenuModel`'s; the page draws and sends gestures. Entries are addressed by PATH (menu index, then
  entry indices), never by action id - Help's "Show tips again" belongs to no registry. *Cost:* a
  path held across a menu rebuild would point elsewhere; the model rebuilds only on construction.
- **R5 Shortcuts follow the surface that shows.** A Qt menu shortcut fires only while the hidden
  `QMenuBar` is visible (Qt's `correctWidgetContext`), so the pill slot carries the six top-level
  menus' `menuAction()`s: shortcuts fire while the HTML pill shows, exactly as they do while the Qt
  one does (render mode hides both). *Cost:* none - parity, pinned by a check.
- **R6 Alt opens a menu.** One `QShortcut` per title mnemonic (Alt+F, Alt+S, Alt+E, Alt+M, Alt+V,
  Alt+H), enabled only while the HTML pill shows, opens that menu with its first live entry
  current. Plain Alt (focus the bar) and F10 are not ported. *Cost:* a keyboard user loses Alt
  alone; Alt+letter is what the menus advertise.
- **R7 An open menu owns the pointer and the keys.** The page's view is raised over every Qt card,
  its mask takes the whole viewport, a press outside closes the menu and is swallowed (QMenu's
  behaviour), and the menu claims the keyboard under the card name `menu`. Hovering another title
  while one is open switches menus. *Cost:* a press on a window button while a menu is open only
  closes the menu - which is what a QMenu does too.
- **R8 Slides and the entrance are the page's.** A slot moving every frame would cost a mask and a
  repaint per frame (the spike's finding), so the slots' `CardSlide`s are instant and the page
  animates the rail (from the left) and the pill (from the top) when their slot shows or hides, and
  plays `playShellEntrance()` when C++ asks for it. Animations off (the harness) = no animation.
  *Cost:* none.
- **R9 The pill sizes itself.** The page measures its row and reports its width and height; the slot
  takes both (`HtmlCardSlot::setContentSize`), so `minimumWidthBeside()` and the stacked minimum
  height read the page's real size. *Cost:* one relayout after the page first reports.
- **R10 Caption.** In HTML mode the window's hit test answers Caption inside the pill slot except
  over a title rectangle the page reports, and never while a menu is open. *Cost:* none.

## Review Focus
1. **Shortcuts with the Qt bar hidden:** Ctrl+Z, Ctrl+S and E still fire while the HTML pill shows;
   none fire while render mode hides it (parity). (Task 3.)
2. **A menu and the keyboard:** Alt+M opens Model on its first live entry; Down skips separators and
   disabled entries; Right opens a submenu or moves to the next menu; Escape closes one level; a
   field elsewhere gets its keys back when the menu closes. (Tasks 1, 4, 5.)
3. **A disabled action never runs** from a chip, an entry or a letter. (Tasks 1, 4.)
4. **The window still drags** by the pill's ground and not by a title. (Task 5.)
5. **The rail follows the actions live:** Undo enables after an edit, Items shows checked while the
   drawer is open, a theme change re-inks every glyph. (Task 5.)

---

### Task 1: MenuModel
**Files:** create `src/session/MenuModel.{h,cpp}`, `tests/menu_model.cpp` (ctest `menu_model`);
`CMakeLists.txt`.
- Input: `MenuModel::Node {QString title; QPointer<QAction> action; bool separator; std::vector<Node>
  children;}` - the bar is a vector of top-level menus (title + children).
- Properties: `titles` [{title (no '&'), mnemonic}], `open` {menu (-1 none), byKeyboard,
  levels: [{entries: [{text, mnemonic, shortcut, enabled, checkable, checked, radio, submenu,
  separator, tip}], current}]}.
- Slots: `openMenu(index, byKeyboard)`, `hover(level, entry)` (opens a submenu after hover, closes
  deeper levels), `key(name)` (`up`, `down`, `left`, `right`, `enter`, `escape`, `home`, `end`, or a
  single letter), `choose(level, entry)`, `close()`. Signal `changed()`.
- Rules (QMenu's): Up/Down move among enabled, non-separator entries and wrap; Home/End; Right on a
  submenu opens it at its first live entry, otherwise moves to the next top-level menu (wrapping);
  Left closes a submenu, otherwise the previous menu; Enter/Space on a submenu opens it, on an entry
  triggers it; Escape closes one level, the last closes the menu; a letter matching one entry's
  mnemonic triggers it (opens it if a submenu), several step through them; a disabled entry is never
  triggered. A trigger closes the menu FIRST, then `QTimer::singleShot(0)` triggers the action if it
  is still enabled. The entries' state is read fresh from the actions at every `changed()`, and the
  model re-emits on any watched action's `changed`, once per turn.
- [ ] Failing tests first (real QActions, a QCoreApplication-free `QGuiApplication` offscreen):
  every rule above; radio for an exclusive `QActionGroup`; the trigger runs once, after close, never
  for a disabled action; an action disabled between choose and the next turn does not run; text
  mnemonics (`&File` -> F, `E&xport` -> X, none -> first letter is NOT used).
- [ ] Implement, pass, commit.

### Task 2: Glyph masks and the theme's shell numbers
**Files:** `src/ui/IconSet.{h,cpp}` (`QImage glyphMask(Glyph, int px)`), `src/web/ThemeBridge.cpp`,
`tests/settings_tests.cpp` or a new check where ThemeBridge is reachable.
- `glyphMask`: `paintGlyph` with a white 1.5 px-on-24 pen into a transparent ARGB image.
- `ThemeBridge::tokens()` adds, when `shellOf(current).chip.on`: `--shell-chip-size`, `-radius`,
  `-edge-width`, `-idle-fill`, `-idle-edge`, `-idle-ink`, `-hover-fill`, `-hover-edge`, `-hover-ink`,
  `-on-top`, `-on-bottom`, `-on-edge`, `-on-ink`, `-on-inner-top`, `-off-ink`, `-off-edge`; when
  `.menu.on`: `--shell-menu-weight`, `-text`, `-ink`, `-hover`, `-item-radius`, `-item-pad-x`,
  `-wordmark-size`, `-wordmark-weight`; when `.card.on`: `--shell-card-radius`, `--shell-rail-full`
  (1/0). Colours as `rgba()`. Always `--chip-stroke` (`Theme::chipStrokePx()`).
- [ ] Failing check: a glyph mask is not empty and has transparent corners; Violet's tokens carry
  `--shell-chip-on-top: rgba(122, 28, 255, 1)` and Graphite's carry no `--shell-` key.
- [ ] Implement, pass, commit.

### Task 3: ShellBridge and MainWindow's three slots
**Files:** create `src/web/ShellBridge.{h,cpp}`; modify `src/web/EditorChrome.{h,cpp}`
(`ChromeBridge::entrance` signal, menu raise), `src/ui/HtmlCardSlot.{h,cpp}` (`setContentSize`),
`src/ui/CardSlide.{h,cpp}` (`setAnimated(false)`), `src/MainWindow.{h,cpp}`, `CMakeLists.txt`,
`tests/chrome_smoke.cpp`.
- `ShellBridge` (`shell`): CONSTANT `rail` / `view` (lists of `{id, glyph}` or `{separator: true}`),
  `glyphs` (name -> data URL, NOTIFY on theme), `chips` (ActionWatch snapshot + composed tip),
  `wordmark`, `menus` (the MenuModel as a child object `menu` registered beside it);
  slots `trigger(id)` (through the registry, so a disabled action does nothing),
  `reportChipRects(map)`, `reportPill(width, height, titleRects)`.
- MainWindow: `myRailSlot` (LeftEdge spine, the Qt rail's size), `myViewSlot` (TopRight, after the
  gizmo, the view cluster's size), `myPillSlot` (LeftEdge header, the pill's size until the page
  reports). Swapped on `liveChanged` before the drawers (insertion order is the anchor order).
  `railSurface()`, `pillSurface()`, `viewSurface()`; `myRailSlide`/RenderStudio's bar slide move to
  the live surface, instant for a slot; `ThemeDecor`, `syncViewportMinimumWidth()` and the minimum
  height read the surfaces. The pill slot carries the six menus' `menuAction()`s (R5). The caption
  hit test reads the pill slot and the page's title rects (R10). Add shape's flyout stands beside
  the chip rect the page reported. The six Alt shortcuts (R6). An open menu raises the view and
  takes the whole mask (R7).
- [ ] Failing chrome_smoke checks (before the page draws anything): the three slots replace the Qt
  surfaces when the page is live and give them back when it dies; Ctrl+Z fires through the shortcut
  map with the Qt bar hidden (Qt's own active-window record moved, never the OS's); it does not in
  render mode; the caption hit test answers Caption on the pill's ground and Client on a title.
- [ ] Build both presets; implement, pass, commit.

### Task 4: The page
**Files:** create `web/src/editor/shell/{api,mock}.ts`, `Chip.svelte`, `Cluster.svelte`,
`Pill.svelte`, `Menu.svelte`; `web/test/shell.test.ts`; modify `Editor.svelte`, `main.ts`,
`themes.css` (only what CSS variables cannot say).
- Chip: the glyph mask in the state's ink; idle/hover/on/off per `--shell-chip-*` with Graphite's
  fallbacks (chip, chip-hover, chip-active, darker when disabled, border at `--chip-stroke`, the
  accent inset ring when checked); `title` = the composed tip; `aria-label` = the label,
  `aria-description` = the tip, `aria-pressed` when checkable; click -> `shell.trigger(id)`;
  disabled does nothing.
- Cluster: the card (`--card-*`, `--shell-card-radius`), chips and separators; reports each chip's
  rectangle; the rail stretches to its slot's height.
- Pill: mark (`app-mark.png`), wordmark, titles; reports its natural size and the titles' rects;
  a title press opens its menu (a second press closes it); hover switches while one is open.
- Menu: drops under its title, submenus to the right (flipped left at the viewport's edge); entries
  with check/radio marks, text, shortcut, a submenu arrow; separators; hover sets current;
  click chooses; a full-viewport layer closes it on press; keys forwarded to `menu.key()` while it
  is open (keyboard claim `menu`).
- [ ] Failing vitest: names are text; a disabled chip and entry never trigger; check/radio/shortcut
  render; hover opens a submenu; keys forward only while open; the outside layer closes; the claim
  is released on close; the pill reports its size; banned-word sweep over every rendered word.
- [ ] Implement, pass, svelte-check, build; commit.

### Task 5: End to end
- [ ] chrome_smoke block: the rail's chips exist with the actions' names; a click on Items opens the
  drawer (the action checked, the chip on); Undo disabled then enabled after an edit; Delete
  disabled with nothing selected; the view cluster's Ortho toggles the projection; the pill's File
  title opens File; Escape closes; Alt+M opens Model, Down/Enter triggers an entry (one checkpoint
  where it edits); View -> Theme -> Nightshade switches the theme and re-inks the glyphs; render mode
  hides the rail and the pill and Back brings them back; captures per theme with a menu open.
- [ ] Mutations, each to a named red line: the pill slot not carrying the menus (shortcuts die);
  the model triggering a disabled entry; the page dropping the menu's keyboard release.
- [ ] Commit.

### Task 6: Gate, documentation, board, review
- [ ] ctest in both presets; vitest; svelte-check; chrome_smoke; hub_smoke; `gui_smoke` twice,
  unfiltered. Record below.
- [ ] CLAUDE.md: architecture rows; a "The rail, the pill and the menus" bullet (R1-R10).
- [ ] Opus whole-branch review; fix Critical and Important findings test-first; build both presets.
- [ ] Board card done with notes; nothing merged.

## Ledger (rulings made while executing)

- **R7 revised: the menu's keys never reach the page.** An open menu installs an
  application-wide filter for its window (`ShellBridge::eventFilter`, ExtrudePreview's shape):
  every `ShortcutOverride` is claimed and the menu's own keys go straight to `MenuModel::key()`.
  Planned as a page keyboard claim; the filter needs no focus move (the suite's windows are never
  active) and works with focus left in a Qt field. *Cost if wrong:* none found - Escape, arrows,
  Enter, letters and Alt+letter are pinned in chrome_smoke.
- **R8 revised: the page plays the entrance on mount.** When the page goes live the Qt rail and
  pill are hidden at once and the page's fly in (animations on); `playShellEntrance()` still plays
  on the Qt cards before that. *Cost if wrong:* a frame of Qt chrome before the page's at startup.
- **Theme shell numbers grew three keys** (`--shell-chip-off-fill`, `--shell-chip-on-ring`,
  `--shell-chip-glyph`) for what Graphite paints and a theme's chip does not, and a chip colour a
  theme leaves out crosses as transparent - ToolChip's theme branch paints nothing there.
- **The pill follows the theme's card radius** (`Themes::paintCard()` clamps it to half the short
  side: Ultraviolet's is square), not always a full pill.
- **Menus are opaque under the theme's glass.** A nearly-opaque card fill let a card's words show
  through the entries (Violet's first capture).
- **The render-mode checks moved into the render page's block.** Entering render mode in the shell
  block and again later made a theme switch in the second session crash in the GL driver
  (`renderShadowMap` -> `clearMemoryGL`), on this machine's Shadows tier; with one render session
  per run the same switches pass. Not caused by anything this phase draws (A/B on one binary:
  skipping only the early toggle passes 357/357). Filed as T-083 rather than fixed here.

## Gate record

**Commits** (branch `rail-pill` from `main` at `12eaeb3`): `121377a` MenuModel; `2197cda` glyph
masks and the theme's shell numbers; `0a11c4a` ShellBridge, the three slots, the page; `ca3e885`
slot width, a non-vacuous Delete check, no colour literal; `d5a5762` review fixes.

| Gate | Result |
|---|---|
| ctest (both presets) | 24/24 in windows-web and in windows (vcpkg) |
| menu_model_tests | 65/65 |
| settings_tests | 145/145 |
| vitest / svelte-check | 235/235; 0 errors, 0 warnings |
| chrome_smoke | 367/367 (44 new for this phase) |
| hub_smoke | 36/36 |
| gui_smoke run 1 | PASS 6448 + 1 skip, floor 6449 |
| gui_smoke run 2 | PASS 6448 + 1 skip, floor 6449 |

An earlier gui_smoke run, taken while builds and the reviewer ran alongside, failed one wall-clock
check (`reveal: restyling 130 rows ... under 16 ms`, 17.98 ms) with the count at 6448 + 1; both
runs above were taken with nothing else running.

**Mutations, each to a named red line:** the pill slot without the menus' actions (`Ctrl+Z fires
through the shortcut map ... (R5)`); the menu filter not claiming ShortcutOverride (`Delete with a
menu open deletes nothing`, after a vacuous version was found: with both mutations applied the first
killed Delete's shortcut too, so the check gained its control); a disabled entry chosen (two
menu_model checks). The review's C1/I1 checks were watched red against the pre-fix page (File
reported at y -46, drawn at 20; chips not square inside the card).

**Review (Opus, read-only):** 1 Critical, 2 Important, 6 Minor. Fixed, each watched red first: C1
the pill and the rail fly in on the Web Animations API, which fires no `transitionend`, so the mask
and the title rectangles kept the first frame's off-screen positions (a press on File could drag the
window) - the page now measures from layout and `hits.ts` re-reports on `introend`/`outroend`;
I1 the rail and view slots never resized after a theme switch - the clusters report their natural
size (`ChromeBridge::reportCardSize`); I2 chip rectangles went stale on a theme switch - read from
layout and re-sent when a chip resizes. Re-graded and fixed: M2, a menu flashed at the corner for
a frame on every open (not placed until its title's rectangle is known).

**Deferred minors:** M1 a press on the pill's own ground (mark, wordmark) does not close an open
menu; M3 a mouse click on a submenu row switches the menu to keyboard mode (underlines, first entry
current); M4 a level's entries are fixed when it opens (an action hidden meanwhile stays drawn);
M5 before the page's first report the whole pill slot, titles included, answers Caption; M6 a bar
menu can open over an open Items row menu, and Escape closes only the bar's.
