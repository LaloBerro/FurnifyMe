# Render Settings Panel as HTML (Migration Phase 3e) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In a web build, render mode docks an HTML page beside the viewport in the Qt
`RenderSettingsPanel`'s place, in the editor AND the scene window. The port is faithful,
row for row:
- Frame: five aspect chips (3 + 2) and three guide chips;
- Shots: "Save this view", one row per shot (apply, x), the empty line;
- Light: Angle (0-359°) and Strength (20-400%) sliders with their readouts;
- Material: the wood tiles (thumbnail, caption, current ring), click to choose, double-click
  to open the material card; the note under them; the whole section hidden with no tiles;
- Studio: the Background swatch (the modeless colour picker) and the Studio reset chip;
- Camera: FOV (20-120°);
- Quality: Simple / Balanced / Deep, and Cut out;
- pinned below the scroll: the Export heading, five size chips (3 + 2) and the pixel note,
  the tier line, the polish bar (hidden when the tier has none), and the wide shutter.

The page wears the theme. The Qt panel stays built - it is still the state and the one edit
path, and it docks back whenever the page is not live - and stays under `gui_smoke` until
Phase 6.

**Architecture:**

**The Qt panel stays the model.** Every row the page shows is read off `RenderSettingsPanel`
and every edit the page makes goes through a public slot ON the panel that does exactly what
a click on its own chip does (set, then emit). So `RenderStudio`'s wiring and `MainWindow`'s
and `SceneWindow`'s listeners do not move a line, and a value cannot mean two things
depending on which panel set it. The panel's chip lambdas are rewritten to call those same
slots, so there is one implementation of each edit. A new `stateChanged()` signal fires on
every change, silent return paths included.

**The words are the panel's, once.** `ui/RenderSettingsCopy` holds every label, heading,
tooltip and note under the `"RenderSettingsPanel"` tr context; the panel's constructor and
the bridge both read it.

**A page of its own, docked.** The panel is not an overlay card: `RenderStudio::setDockOpen`
puts it in a row beside the viewport, and the scene window has no chrome page. So the HTML
panel is its own opaque page (`web/render.html`) in its own `WebHost`, and `RenderStudio`
docks that view instead of the Qt panel while the page is live. One piece of work serves both
windows.

**The bridge.** `web/RenderBridge` (QtCore + the panel; compiled and tested in both presets)
is the page's `render` object: `layout` (words, chips, tiles with data-URL thumbnails made
once from the panel's own decoded pixmaps), `state` (every current value, readouts formatted
by the panel's own formatter, the shot names, the tier line, the shutter's enabled state and
label), and slots that call the panel's edit slots. `web/RenderPage` (web build only) owns the
`WebHost`, registers `render` and `theme`, tracks whether the page is live, and hands the view
to `RenderStudio`.

**Tech Stack:** C++17, Qt 6.11.1 (installer build, WebEngine + WebChannel), OCCT 8.0.1,
Svelte 5 + TypeScript + Vite, vitest + jsdom.

**Spec:** Read these together:
- `src/ui/RenderSettingsPanel.{h,cpp}`: the behaviour being ported - every row, range,
  tooltip and the footer.
- `src/ui/RenderStudio.{h,cpp}`: `setDockOpen()`, `refresh()`, `syncPanelFromView()`,
  `wireViewportControls()`.
- CLAUDE.md "The HTML UI", including "What a page over the viewport owes" and "What every
  page owes, learned on the hub"; and "Render mode".
- `DESIGN.md`: the locked style and its themes.

## Global Constraints

**Where and how:**
- Branch `render-card` (cut from `main` at `7ca510c`), in `D:/PizzaData/C++/FurnifyMe`.
- Build with `cmake --build --preset windows-web` AND the `windows` preset before calling
  the phase done.
- Never merge, push or delete without asking.

**`gui_smoke`:** it never enables any HTML. Its gate is **6448 checks + 1 environment skip, 0
failures, twice, quiet**, on the final tree, with `kCheckFloor` (6449) untouched. The panel
refactor (copy table, edit slots, stateChanged) must not move that count.

**Copy:**
- Verbatim from `RenderSettingsPanel.cpp`, through `RenderSettingsCopy`; tr context stays
  `"RenderSettingsPanel"`.
- Every painted string of this app's own passes `web/test/banned.ts`. Tile names and shot
  names are the user's and exempt. "Studio", never "Scene" (the vocabulary table).

**Behaviour that must survive the port:**
- A chip that is already current does nothing when clicked (no signal).
- Cut out toggles.
- A tile click on the tile already worn does nothing; any other tile chooses that wood.
- A double-click on a tile opens that material's card; its first click already chose it.
- The shot x forgets that shot with one click (file data, no Undo).
- The Background swatch opens ONE modeless `QColorDialog`; Cancel puts the colour back.
- The Material section (heading, tiles, note, its rule) is absent when there are no tiles -
  the scene window's case.
- The polish bar is hidden while the tier reports a negative progress.
- The shutter mirrors Save Screenshot: disabled with it, and a click triggers it only while
  enabled; its label is the action's text without `&` and `...`.
- The Export heading, chips, note, tier line, bar and shutter are pinned; the sections above
  scroll, with the app's scrollbar only when needed.
- No control takes keyboard focus (`use:chip`); a slider drag moves the value live.

**Evidence:** every new check is watched red before its code exists. The page is looked at in
composited captures in at least two themes.

## Rulings (decided while planning; each costs what it says if wrong)

- **R1. The Qt panel stays the state and the edit path.** The page never talks to
  `RenderStudio` or a window; it calls the panel's slots, which emit the signals the existing
  wiring already handles. Cost if wrong: a hidden widget kept alive per window, and Phase 6
  must lift the state out of it.
- **R2. A page of its own, docked by `RenderStudio`.** Not a card on the editor's chrome page:
  the panel sits beside the viewport, not over it, and the scene window has no chrome page.
  Cost if wrong: a second WebEngine view per window (memory), loaded at startup so it is live
  before render mode is first entered.
- **R3. Thumbnails cross as PNG data URLs made from the panel's own decoded pixmaps.** The page
  never decodes a 4K texture. Cost if wrong: about 10-30 KB per tile in `layout`, once.
- **R4. The background colour is the Qt modeless picker**, opened by the panel's own function,
  as the Settings card's swatches do. Cost if wrong: a native dialog over a web page.
- **R5. Readouts are formatted in C++** (`syncValueLabels()`'s own rules, through the panel),
  so the page never writes a degree or a percent itself.
- **R6. The opt-in follows the chrome's.** `RenderPage::attach()` is called for the editor by
  `MainWindow::useHtmlChrome()` and for the scene window by `main.cpp` under the same
  condition (`--qt-chrome` off, the `render` page present). `gui_smoke` calls neither.
- **R7. The page's view is a child of the host window, hidden until docked.** `RenderStudio`
  swaps it with the Qt panel in the dock row when the page's live state changes while render
  mode is on.

## Review Focus

1. **The page dies mid-render-mode** (render process killed): the Qt panel must dock back at
   once with every value it holds, and render mode stays on.
2. **The page is not live yet when render mode is entered** (a very fast first entry): the
   Qt panel docks, and the page takes its place the moment it reports ready.
3. **A theme switch while render mode is on**: the page re-styles; the chips' current marks
   and the readouts stay right.
4. **User-named tiles and shots** with markup in them ("<b>Oak</b>") render as text.
5. **The scene window**: no tiles, so no Material section; shots save to the scene.

## File Structure

| File | Change |
|---|---|
| `src/ui/RenderSettingsCopy.{h,cpp}` | new - every word the panel paints, one tr context |
| `src/ui/RenderSettingsPanel.{h,cpp}` | read the copy; public edit slots; `stateChanged()`; thumbnail, readout and footer getters |
| `src/web/RenderBridge.{h,cpp}` | new - the page's `render` object (both presets) |
| `tests/render_bridge.cpp` | new ctest `render_bridge_tests` (offscreen) |
| `src/ui/RenderStudio.{h,cpp}` | `setHtmlPanel(view, live)`; dock the view while live |
| `src/web/RenderPage.{h,cpp}` | new (web build) - WebHost, bridge, theme, live |
| `src/MainWindow.cpp`, `src/main.cpp` | attach the page (R6) |
| `web/render.html`, `web/src/render/*` | new page: `main.ts`, `api.ts`, `mock.ts`, `Render.svelte`, `Slider.svelte`, `Tile.svelte` |
| `web/build.mjs` | build `render` |
| `web/test/render.test.ts` | new page tests |
| `tests/chrome_smoke.cpp` | a Render block: the real editor, the page docked, DOM drives |
| `CLAUDE.md` | a Phase 3e bullet |

---

### Task 1: The copy table and the panel's edit slots

**Files:** create `src/ui/RenderSettingsCopy.{h,cpp}`; modify `src/ui/RenderSettingsPanel.{h,cpp}`,
`CMakeLists.txt`; test `tests/render_bridge.cpp` (the panel half).

**Produces:**
- `RenderSettingsCopy`: `title()`, `section(Section)`, `rowLabel(Row)`, chip label/tip arrays for
  aspect, guides, quality, export, cutout, shots, studio, tooltips, the material note, the shot
  empty line, the colour-dialog title.
- `RenderSettingsPanel` public slots: `chooseAspect(Aspect)`, `chooseGuides(Guides)`,
  `chooseExportSize(ExportSize)`, `chooseQuality(Quality)`, `toggleCutout()`,
  `chooseWood(QString name)`, `editMaterial(QString name)`, `saveShot()`, `applyShot(int)`,
  `removeShot(int)`, `pickBackground()`, `resetBackground()`.
- Getters: `QImage materialThumbnail(QString name)`, `QString readout(Row)`,
  `QString exportNote()`, `QString tierName()`, `double tierProgress()`.
- Signal `stateChanged()`.

- [ ] Write `render_bridge_tests`' panel half red: each slot emits its signal exactly as the chip
  click does (a current choice emits nothing; cut out toggles; a worn tile emits nothing);
  `stateChanged()` fires on every setter, silent ones too; readouts read `0°`, `200%`, `45°`.
- [ ] Extract the copy, add the slots, route every chip lambda through them.
- [ ] `render_bridge_tests` green; `gui_smoke` filtered to the render blocks green with their
  counts unchanged.

### Task 2: RenderBridge

**Files:** create `src/web/RenderBridge.{h,cpp}`; test `tests/render_bridge.cpp` (bridge half).

**Produces:** `RenderBridge(RenderSettingsPanel*)` with `Q_PROPERTY(QVariantMap layout CONSTANT)`,
`Q_PROPERTY(QVariantMap state NOTIFY stateChanged)`, slots `aspect(int)`, `guides(int)`,
`exportSize(int)`, `quality(int)`, `cutout()`, `lightAngle(double)`, `lightStrength(double)`,
`fov(double)`, `wood(QString)`, `editMaterial(QString)`, `saveShot()`, `applyShot(int)`,
`removeShot(int)`, `pickBackground()`, `resetBackground()`, `shoot()`, `ready()`; signal
`readyChanged()`. `layout.tiles` is `[{name, thumb}]` with `thumb` a `data:image/png;base64,`
URL; the layout is re-read (`layoutChanged`) if tiles are added after construction.

- [ ] Red: layout carries every word and chip; tiles carry data URLs; state follows every panel
  setter; each slot reaches the panel; `shoot()` does nothing while Save Screenshot is disabled;
  an out-of-range index is refused.
- [ ] Implement; green.

### Task 3: RenderStudio docks the page while it is live

**Files:** modify `src/ui/RenderStudio.{h,cpp}`.

**Produces:** `void setHtmlPanel(QWidget* view, std::function<bool()> live)` and
`void htmlPanelLiveChanged()`; `QWidget* dockedPanel() const` for the suite.

- [ ] Docking picks `live() ? view : myPanel`; a live change while open swaps the two in the row;
  the one not docked is hidden; closing the dock re-parents both to the host.
- [ ] Checked end to end in Task 5's chrome_smoke block.

### Task 4: The page

**Files:** create `web/render.html`, `web/src/render/{main.ts,api.ts,mock.ts,Render.svelte,Slider.svelte,Tile.svelte}`,
`web/test/render.test.ts`; modify `web/build.mjs`.

- [ ] Red tests (mock bridge): every section in order and its words; a current chip is marked and
  clicking it sends nothing; a slider input sends the value; the empty shot line vs rows; the
  shot x sends its index; tiles show their thumbnails, the worn one ringed, a click chooses, a
  double-click edits once; no Material section with no tiles; the bar hidden at -1; the shutter
  disabled and refusing; user names render as text; the banned-word sweep over the page's copy
  and its rendered text.
- [ ] Implement with the themes' card variables; green; `svelte-check` clean.

### Task 5: Attach it, and drive it in the real app

**Files:** create `src/web/RenderPage.{h,cpp}`; modify `src/MainWindow.cpp`, `src/main.cpp`,
`CMakeLists.txt`, `tests/chrome_smoke.cpp`.

- [ ] chrome_smoke Render block red: render mode on docks the page's view (not the Qt panel); a DOM
  click on Simple sets the panel's quality and the viewport's; a slider input moves the light;
  Save this view adds a shot row on the page; a tile click chooses the wood; the shutter's
  disabled state follows the action; killing the render process docks the Qt panel back with
  render mode still on; composited captures in Violet and Ultraviolet.
- [ ] Implement `RenderPage::attach()`; wire both windows; green.

### Task 6: Gate, documentation, board, review

- [ ] ctest (both presets), vitest, svelte-check, chrome_smoke, hub_smoke; gui_smoke 6448 + 1 twice.
- [ ] CLAUDE.md bullet; board card; whole-branch review on the most capable model; fixes test-first.

## Gate record

**Commits** (branch `render-card`, cut from `main` at `7ca510c`): `2d37399` copy table and edit
slots; `04568a2` RenderBridge; `2a2b73c` RenderStudio docks the page; `fc8e7bf` the page;
`cadb0e3` attached in both windows, plus the background-colour crash; `e79009d` the scene
window check; `775b5b2` review fixes.

**Tests:** render_bridge_tests 76 (new, offscreen); ctest 22/22 in `build-web` and 22/22 in
`build`; vitest 160 (17 new); svelte-check clean; chrome_smoke 201 (25 new, twice clean in a row
after one run straight off a build failed two unrelated timing checks - the toast's hover fade
and the Joints card's height - both green on the next two runs of the same binary); hub_smoke 36.

**gui_smoke:** 6448 checks + 1 environment skip, 0 failures, twice, on `e79009d` (the tree before
the review fixes, which touch only the page, the bridge and RenderPage - none of it reached by
the suite) - and again on the final tree `775b5b2`: **6448 + 1, 0 failures, twice**. Both presets build
(`windows` linked once the user's running copy was closed).

**Found while driving it, not planned:**
- `MainWindow` set the Qt panel's visibility itself after `RenderStudio::refresh()`, so with the
  page docked the Qt panel also stood loose in the window's corner. The line is gone;
  `RenderStudio` alone decides. Caught by the block's first dock check.
- The theme-switch crash's real mechanism: `SetBackgroundColor()` redraws on the spot, with no GL
  context of the viewport's own current. Reproduced deterministically by leaving render mode
  after the render page's process died; the colour change now runs in a `GlScope`. The earlier
  fix (the backdrop image's swap) covered only the half after it.

**Review (Opus, read-only):** 0 Critical, 1 Important, 6 Minor.
- Fixed, each by a check watched red first: I1 (a shot's index alone, stale under a double-click
  or a click after a removal - the name now travels and a mismatch is refused, and the second
  click of a double-click sends nothing); M2 (a dial stuck on a lost release - re-graded up and
  fixed); M3 (RenderPage holds its studio by `QPointer` - re-graded up and fixed).

**Rulings:**
- R6 as built differs from the plan's words: the editor's page is attached inside
  `useHtmlChrome()`, so a missing EDITOR chrome page also means the Qt render panel; the scene
  window needs only the render page. Kept - the editor's HTML layer is one switch - cost if wrong:
  an editor whose chrome page is missing shows the Qt render panel.
- The shutter's `isEnabled()` guard in `RenderBridge::shoot()` is redundant - Qt's own
  `QAction::trigger()` refuses a disabled action, which is why its mutation stayed green. Kept as
  the panel's stated rule; the check is true of the behaviour, not proof of the guard.
- Review Focus 2 (render mode entered before the page is live) is not driven by a check: the
  page loads at window construction, long before render mode can be entered. The code path is
  the same swap Review Focus 1 drives (the Qt panel docked, the page taking its place on
  `liveChanged`).

**Deferred minors:** every Quality click hides, reparents and re-shows the web view through the
existing render-mode round trip (a possible one-frame flash); `RenderSettingsCopy`'s strings are
not extractable by lupdate (no translations ship); thumbnails are PNG at twice tile size, ~30-45
KB each in the channel's first message and on each layoutChanged (JPEG would be smaller for a
big materials folder); `layout` is NOTIFY rather than the plan's CONSTANT (the right choice, the
tiles can change).
