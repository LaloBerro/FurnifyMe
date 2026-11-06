# Phase 7b: move the state out of the Qt shell, then delete it

Branch `phase-7b` (stacked on `t084-chrome-timing`). After Phase 7a every surface the
user sees in the editor and the scene window is drawn by the chrome page, except the
axis gizmo and the compare badge (rulings 7a R4, R5). What is left of the Qt shell -
the toast (`ToastHost` + `Toast`), the status bar (`QStatusBar`, `StatusMessageLine`,
`FurnitureNameMark`, the state label), the pill (`AppBar` holding the real `QMenuBar`)
and the rail and view cluster (`ToolCluster` of `ToolChip`s) - is drawn only while the
page cannot draw (a page missing, or the few hundred milliseconds of a reload) and in
`gui_smoke`, which never loads a page. But several of them still HOLD state the page
mirrors. This phase moves that state into small non-widget models, points the page and
the suite at the models, and deletes the widgets.

## Slices (each: fast checks only; the slow gate once, at the end)

1. **The status line.** A `StatusLine` model (left sentence, right state line and its
   whole sentence, the furniture's name, the unsaved dot): `MainWindow` and
   `SceneWindow` write it where they wrote `statusBar()`; the chrome reads it; the suite
   reads `statusLine()`. Then `QStatusBar` use, `StatusMessageLine`, `FurnitureNameMark`
   and the state label are deleted.
2. **The toast's rules.** `ToastHost`'s state - the message, its kind, the Undo stamp,
   notes on or off, the lifetime and the paused countdown, `documentMovedTo()` - stays
   in a non-widget `ToastHost`; the `Toast` widget and `UndoControl` are deleted.
3. **The menus without a menu bar.** The window keeps its `QMenu`s in a list and adds
   every action to itself, so shortcuts fire with no menu bar on screen; `MenuModel`,
   `ShortcutSheet` and the vocabulary sweep read the list. `AppBar` and the pill's
   `menuActions()` trick are deleted.
4. **The rail and the view cluster.** The page's slots stand for good; the viewport's
   minimum sizes come from the slots. `ToolCluster` and `ToolChip` go from both windows
   (the render studio's exit chip included - its slot is the page's since 7a).
5. **The fallbacks.** Every `htmlChromeLive()` branch that chose a Qt surface goes; a
   page missing or reloading shows no chrome until it is back (ruling R1).

## Rulings

- **R1: with no page there is no chrome.** The Qt shell was the bridge while a page
  reloads (Phase 6 R2); it is deleted, so for those few hundred milliseconds - or for good
  if the page is missing from the install - the viewport stands alone. The reload
  (`WebHost`) is what answers it. Cost: a deploy that lost `web/editor` opens an editor
  with no menus; `main.cpp` already warns.
- **R2: gui_smoke tests the models, chrome_smoke the page.** gui_smoke loses its checks
  on how the shell LOOKS (pixels, geometry, accessible names of Qt chips); what the shell
  SAYS and DOES stays pinned through the models. The floor is re-measured.
- **R3: slices 3-5 land as one change.** The pill, the rail, the view cluster, the window
  buttons and the Back chip all traded places with their page slots in one `liveChanged`
  handler, so deleting one Qt widget meant rewriting the code shared by all of them. The
  slots now stand in `buildOverlay()` for good, at the deleted widgets' own sizes
  (`ui/ShellSizes.h`) until the page measures its own; `AppBar`, `ToolCluster` and
  `WindowButtons` are deleted. `ToolChip` stays: the render card, the Add-shape flyout and
  the scene's Pieces and add cards still build Qt chips.
- **R4: a Qt card that is still the LOGIC keeps painting with no page.** The close and
  name questions, the gesture chips, the loading card, the sheet, the guide, the hint and
  the material card are drawn elsewhere while the page is live and paint themselves
  otherwise. They hold the state and the input rules, so R1 does not reach them in this
  phase; deleting their paint code is its own follow-up. Cost if wrong: dead paint code
  that only gui_smoke exercises.
- **R5: menus are a list the window owns.** `buildMenus()` makes `QMenu`s parented to the
  window and adds every leaf action to the window itself, so a shortcut fires with no menu
  widget anywhere (Qt matches a menu action only through a visible carrier). `ShellBridge`,
  `ShortcutSheet` and the suites read `menus()`.
