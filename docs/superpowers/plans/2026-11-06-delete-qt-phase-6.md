# Phase 6: delete the Qt surfaces the pages replaced

Branch `delete-qt-6`. The migration report (T-066) planned Phase 6 as "delete the Qt
UI"; walking the tree showed that only part of it has a page in its place, and that
several Qt widgets are still the STATE the pages mirror. So this phase deletes what
genuinely has a replacement, and leaves the rest for a Phase 7 that moves the state
first.

## Slices

- **6a: the web build is the only app build.** `FURNIFYME_WEB_UI` defaults ON, and
  configuring the app with it OFF is a CMake error. The `windows` preset IS the web
  build now (`build-web`, the installer Qt with WebEngine); `windows-web` stays as an
  alias of it. The two `*-headless` presets still build the geometry library and the
  headless tests with no Qt app at all.
- **6c: the four Qt drawers.** `AppearancePanel` (Settings), `VersionsPanel`,
  `JointsPanel` and `ItemsPanel` (+ `ItemsRow`, `ItemsFilterField`, `DropIndicator`,
  `DragGhost`) are deleted. Their `HtmlCardSlot`s are built in `buildOverlay()` in the
  drawers' old anchors and stand there for good; `useHtmlChrome()` only registers them.
  `ui/DrawerWidths` keeps the four card widths the drawers used to compute.
  `MainWindow::setItemsVisible()` and `toggleBodyGrain()` are the eye's and the grain
  mark's one route (the Items card's hooks call them; so does the suite).
- **6b: the Qt hub.** `SelectorWindow` and `--qt-hub` are deleted; `main.cpp` always
  builds `HubWindow`. `EditorSelectorHandoff::wire()` is a header template now, so the
  suite instantiates it with `tests/SuiteHub.h` - HubModel in a plain window with
  HubWindow's three signals.
- **6d: the fallbacks.** `--qt-chrome` is deleted: the editor, scene and render pages
  are always used when they are beside the app. **A dead page reloads**:
  `WebHost` loads the page again `kCrashReloadMs` after its render process dies, at
  most `kMaxCrashReloads` times a session; `EditorChrome` re-shows its view when the
  reloaded page says ready, and `RenderPage` forgets the old page's ready.

## Rulings

- **R1: the Qt toast, status bar, pill, rail and cluster STAY.** They are the state the
  page mirrors (the status bar's `currentMessage()`, ToastHost's rules, the real
  `QMenuBar` whose visibility gates Qt's shortcut matcher, the chips' actions) and
  they are what gui_smoke drives. Deleting their painting means moving that state
  first - Phase 7, not this phase. Cost if wrong: a later phase does the deletion this
  one could have.
- **R2: the Qt surfaces are the bridge while a dead page reloads**, not the
  destination: for the few hundred milliseconds of a reload, the editor wears the Qt
  chrome it always fell back to. Three crashes in a session leave it there.
- **R3: gui_smoke loses the coverage of what it deletes, and does not imitate it.**
  The Qt drawers' and Qt hub's own blocks are deleted (about 8,000 lines); their logic
  is the models' and is tested headless (`items_model_tests`, `versions_model_tests`,
  `joints_model_tests`, `hub_model_tests`, `settings_tests`) and on the page
  (`chrome_smoke`, `hub_smoke`, vitest). Checks that only used a drawer as a way IN
  (an eye, a row count, a saved version) now go through the model or the window's own
  route, so what they were really about keeps its pin. `kCheckFloor` is re-measured,
  never computed.
- **R4: the suite's hub is HubModel, not HubWindow.** gui_smoke never prepares Qt
  Quick for WebEngine and must not depend on a page; `SuiteHub` gives the handoff the
  same three signals and the model's own create/choose, so the wiring under test is
  the shipped one.
- **R5: a drawer with no page behind it is an invisible slot.** With a page missing or
  dead, the four drawers draw nothing until the page is back - there is no Qt drawer
  to fall back to any more. The reload is what answers it.
