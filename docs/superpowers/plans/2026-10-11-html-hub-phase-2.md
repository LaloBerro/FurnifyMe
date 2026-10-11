# Phase 2 — Bridge, Tokens and the Library Hub in HTML — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (the human partner chose native execution and asked the executor to decide alone). Steps use checkbox (`- [ ]`) syntax.

**Goal:** The library hub — the first window a user sees — is an HTML/CSS page (Svelte 5) in a frameless Qt window, behaving exactly like today's Qt `SelectorWindow`, which stays in the tree (and under `gui_smoke`) until Phase 6 deletes it.

**Architecture:** A reusable `WebHost` (one `QWebEngineView`, an off-the-record profile, a `QWebChannel`, qwebchannel.js injected) carries any page. The hub's logic — listing, sorting, filtering, creating, renaming, deleting, the dates and refusal sentences — lives in a Qt-Widgets-free `HubModel` in `furnify_session`, headless-tested; `HubWindow` is a thin widget that hosts the page and exposes `HubModel` and `ThemeBridge` on the channel. `EditorSelectorHandoff::wire` becomes a template over the hub type, so both hubs share one wiring. The app builds on the installer Qt 6.11.1 (WebEngine) with vcpkg OCCT.

**Tech Stack:** C++17, Qt 6.11.1 installer build (Core, Gui, Widgets, WebEngineWidgets, WebChannel, Quick), OCCT 8.0.1 (vcpkg), Svelte 5 + Vite 8 + TypeScript 5, vitest + jsdom.

**Spec:** migration report "Recommended architecture" and "Migration plan → Phase 2" (https://claude.ai/code/artifact/30802edc-e639-4532-a1f0-19661e952d5a); the behaviour spec is `src/ui/SelectorWindow.cpp` + `src/ui/NameFurnitureCard.cpp` as they stand at `705f388`.

## Global Constraints

- **Faithful port.** Every word, number and behaviour of the Qt hub is kept unless a ruling says otherwise: 3 columns, 260 px cards at 16:10, 24 px grid gap, 32 px margins (16 on top), Recent/Name sort with id tie-break, never-saved last in Recent, 150 ms search debounce, the New card first and never filtered, the Scenes heading, 4000 ms delete arm with the "Used in …" strip fitted (all names → "and N more" → count), the no-match line `Nothing matches "<q>"`, failures as an inline banner for 8000 ms, the delete Note with no Undo, the name question ("Name this furniture" / "You can rename it later from the library." / Cancel Esc / Create Enter, Create disabled on an empty name), scenes created with a numbered name and opened at once.
- **The vocabulary law applies to the page:** every string the page can show is swept for the banned words (CLAUDE.md "The vocabulary — enforced by test"), user data exempt.
- `gui_smoke` keeps its gate: 6370 checks + 1 skip, 0 failures, on the installer-Qt build — the Qt hub is untouched.
- No modal dialogs; no keyboard focus ring; no chip takes focus; Theme tokens are the only colours.
- No OS input in any test; windows stay behind the user's.
- Work stays on branch `editor-session` in `D:/PizzaData/C++/FurnifyMe-html-spike`; nothing is merged.

## Review Focus

1. **A thumbnail that changes on disk after a save** must show the new picture when the hub returns, not the browser's cached one — pinned in Task 6 (URL carries the file's mtime) and by the hub harness re-entering after a save.
2. **A furniture renamed to a name containing quotes, `<`, or a banned word** — rendered as text, never markup, and exempt from the sweep as user data. Pinned in Task 7's page tests.
3. **Rename to an empty or unchanged name** — no store write, no failure. Pinned in Task 4 (HubModel) and Task 7 (page).
4. **A store write that fails** (read-only folder) — the inline banner with the same sentence as today, for 8 s, and the grid unchanged. Pinned in Task 4 with a read-only root.
5. **Escape inside the name card or the rename field** must cancel that and nothing else — never close the window. Pinned in Task 7's page tests.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/session/HubModel.{h,cpp}` | the hub's logic over `FurnitureStore`: entries, order, filter, create/rename/delete with outcomes, dates, sentences; Core+Gui only |
| `src/FurnitureStore.{h,cpp}` | moves into `furnify_session` (it is Core+Gui already) |
| `src/web/WebHost.{h,cpp}` | one page in a `QWebEngineView`: profile, channel, script injection, URL resolution (built file or dev server) |
| `src/web/ThemeBridge.{h,cpp}` | `Theme::Spec` as CSS custom properties, pushed on every `Theme` change |
| `src/web/HubWindow.{h,cpp}` | the frameless hub window: `WebHost` + `HubModel` on the channel + window controls + `WindowChrome` hit test from page-reported rects |
| `src/web/WebOverlay.{h,cpp}` | the spike overlay, rebuilt on `WebHost` |
| `src/ui/WindowChrome.{h,cpp}` | `attach()` overload taking max-hover/pressed callbacks |
| `src/EditorSelectorHandoff.{h,cpp}` | `wire` templated over the hub |
| `src/main.cpp` | `HubWindow` by default in a web build; `--qt-hub` for the old one |
| `web/hub.html`, `web/src/hub/*` | the hub page: `Hub.svelte`, `Card.svelte`, `NameCard.svelte`, `TitleBar.svelte`, `copy.ts`, `usedIn.ts`, `bridge.ts` |
| `web/build.mjs` | builds every page as one self-contained HTML file each |
| `web/test/*.test.ts` | vitest: page behaviour with a mock bridge, the banned-word sweep |
| `tests/hub_model.cpp` | HubModel tests (offscreen, ctest) |
| `tests/hub_smoke.cpp` | the real app with the HTML hub: handoff legs, create, rename, delete, search, sort, captures |
| `CMakeLists.txt`, `CMakePresets.json` | `FURNIFYME_WEB_UI` (renamed from the spike option), the `web_pages` target, the new tests |

---

### Task 1: The build on the installer Qt keeps the gate

- [ ] Build every target with `windows-web`; `ctest` all pass.
- [ ] Two unfiltered `gui_smoke` runs from `build-web`. Expected: 6370 + 1 skip, 0 failures, both.
- [ ] Record in the gate table below. A difference stops Phase 2 until explained.

### Task 2: `WebHost`, and the spike rebuilt on it

**Interfaces — Produces:**
```cpp
class WebHost : public QObject {
public:
    enum class Ground { Opaque, Transparent };
    WebHost(QWidget* parent, Ground ground, QObject* owner = nullptr);
    static void prepareApplication();                       // Qt Quick on OpenGL (the spike's finding)
    static QUrl pageUrl(const QString& page);               // <exe>/web/<page>/index.html, or FURNIFY_WEB_URL/<page>.html
    void registerObject(const QString& name, QObject* object);
    void load(const QUrl& url);
    QWebEngineView* view() const;
    bool loaded() const; bool loadOk() const;
signals:
    void loadFinished(bool ok);
};
```
- [ ] Move the profile, page, channel and qwebchannel.js injection out of `WebOverlay` into `WebHost`; `WebOverlay` keeps the mask, the anchor and the camera signals and owns a `WebHost(viewport, Transparent)`.
- [ ] `WebOverlay::prepareApplication()` forwards to `WebHost::prepareApplication()`.
- [ ] Verify: `web_spike.exe` — every gate line it printed at `7c37028` still prints PASS (gate 2's 100% lines are the known borderline; compare numbers, not colours).

### Task 3: The web build makes every page

- [ ] `web/build.mjs`: for each page in `["overlay", "hub"]`, `vite build` with `root` = `web`, input `<page>.html`, `outDir` = `dist/<page>`, singlefile. Rename `index.html` → `overlay.html`.
- [ ] `package.json`: `"build": "node build.mjs"`, `"test": "vitest run"`; add `vitest`, `jsdom`, `@testing-library/svelte`.
- [ ] CMake: `add_custom_target(web_pages ALL COMMAND npm run build WORKING_DIRECTORY web BYPRODUCTS web/dist/hub/index.html web/dist/overlay/index.html)` when `FURNIFYME_WEB_UI`; every web exe depends on it and copies `web/dist` to `$<TARGET_FILE_DIR>/web`.
- [ ] Verify: a clean `cmake --build --preset windows-web` produces `build-web/RelWithDebInfo/web/hub/index.html` and `web/overlay/index.html`.

### Task 4: `HubModel` (TDD, `tests/hub_model.cpp`)

**Interfaces — Produces:**
```cpp
struct HubEntry { QString id; QString name; QString thumbPath; qint64 thumbStamp; QDateTime lastEdited; bool scene; };
class HubModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList furniture READ furnitureList NOTIFY changed)
    Q_PROPERTY(QVariantList scenes READ sceneList NOTIFY changed)
public:
    explicit HubModel(FurnitureStore& store, QObject* parent = nullptr);
    // pure, for the page and the tests
    static std::vector<HubEntry> ordered(std::vector<HubEntry> entries, bool byName);
    static bool matches(const QString& name, const QString& query);
    static QString editedText(const QDateTime& when, const QDateTime& now);
    static QString deletedNoteText(const QString& name, int scenes);
    QVariantList furnitureList() const; QVariantList sceneList() const;   // id, name, thumbUrl, edited, editedText
public slots:   // the page's commands
    void refresh();
    QString suggestedName() const;
    QStringList usedIn(const QString& furnitureId) const;
    void createFurniture(const QString& name);   // -> furnitureChosen(id) | failure(text)
    void createScene();                          // -> sceneChosen(id)     | failure(text)
    void rename(const QString& id, bool scene, const QString& name);
    void remove(const QString& id, bool scene);  // -> note(text)          | failure(text)
    void choose(const QString& id, bool scene);  // -> furnitureChosen / sceneChosen
signals:
    void changed();
    void furnitureChosen(const QString& id);
    void sceneChosen(const QString& id);
    void createRequested();
    void createSceneRequested();
    void failure(const QString& text);
    void note(const QString& text);
};
```
- [ ] Tests first, each watched failing, then the code moved from `SelectorWindow.cpp` (sentences verbatim): order by recent (never-saved last, id tie-break) and by name (case-insensitive, id tie-break); filter (trimmed, case-insensitive, empty matches all); `editedText` for today / yesterday / this week / this year / another year / never-saved ("Never saved"); create → chosen id and a listed entry; create on a read-only root → the exact failure sentence; rename to empty or unchanged → no write, no signal; rename failure sentence; delete → note text with the scene count taken BEFORE the delete; delete failure sentence; scenes using a furniture; `thumbStamp` changes when `thumb.png` is rewritten.
- [ ] `FurnitureStore.cpp` moves to `furnify_session`; `session_no_widgets` stays green.

### Task 5: `ThemeBridge`

- [ ] `tokens()` returns a `QVariantMap` of CSS custom properties from `Theme::*` accessors: `--chrome --panel --chip --chip-hover --chip-active --accent --accent-text --danger --danger-text --text --text-muted --text-disabled --border --viewport --caution`, the four font sizes `--font-badge/label/body/title` in px from the `Theme` fonts, and `--font-family`; `changed()` on `Theme::notifier()->changed`.
- [ ] Page helper `applyTokens(map)` writes them to `:root`. Verified by the hub harness reading `getComputedStyle(document.documentElement)` against `Theme::panel().name()`.

### Task 6: `HubWindow` and the handoff

- [ ] `HubWindow : QWidget` (frameless, `Qt::Window`): a `WebHost(Opaque)` filling it; `HubModel` registered as `hub`, `ThemeBridge` as `theme`, and a `window` object with `minimize()`, `toggleMaximize()`, `close()`, `reportChrome(QVariantMap)` (title-bar rect and the maximize rect, in page pixels) and signals `maxHovered(bool)`, `maxPressed(bool)`, `maximizedChanged(bool)`.
- [ ] `WindowChrome::attach(topLevel, hitTest, onMaxHover, onMaxPressed)` overload; the hit test answers Caption inside the reported title rect minus the button rects, MaxButton inside the maximize rect.
- [ ] Same signals and `refresh()` as `SelectorWindow`; `furnitureCount()`.
- [ ] Window size: the Qt hub's own arithmetic (rows = clamp(ceil((1 + cards)/3), 1, 3)) on the page's reported cell size, bounded to 90% of the screen; minimum width holds three columns.
- [ ] `EditorSelectorHandoff::wire` becomes `template <class Hub>` with explicit instantiations for `SelectorWindow` and (web builds) `HubWindow`; `gui_smoke` compiles unchanged.
- [ ] `main.cpp`: `HubWindow` unless `--qt-hub`.

### Task 7: The hub page (vitest first)

- [ ] `web/test/hub.test.ts` with a mock bridge, each written and watched failing before the component: renders the New card first and never filters it; sort chips reorder (and are a single exclusive pair); typing filters after 150 ms and shows the no-match line; hover swaps the date for Rename/Delete; Delete arms ("Delete — click again"), shows the Used-in strip from `usedIn()`, disarms after 4000 ms, and the second click calls `remove`; rename opens a field over the name, Enter commits, Escape cancels, an unchanged name sends nothing; the name card opens with `suggestedName()`, Create is disabled on an empty name, Enter creates, Escape cancels and nothing else; a name with `<b>` renders as text; the banned-word sweep over every string in `copy.ts` and over the rendered DOM text minus user data.
- [ ] Components: `TitleBar.svelte` (mark, wordmark, three window buttons with the max button's hover/pressed driven from the bridge), `Hub.svelte` (header, banner, grid, sections), `Card.svelte` (thumbnail with the monogram placeholder, hover border, under-row, arm, strip), `NameCard.svelte` (scrim, card, field, buttons with key hints), a Note toast.
- [ ] `usedIn.ts`: the fitting rule measured with a canvas `measureText` in the strip's own font.
- [ ] `npm test` green; `svelte-check` 0 errors.

### Task 8: The hub harness (`tests/hub_smoke.cpp`) and the gate

- [ ] Seeds a library (three furniture, one with a thumbnail, one scene using one of them) in a temp dir; builds `MainWindow`, `HubWindow`, `SceneWindow`, wires them; drives the page with `runJavaScript` (DOM `.click()`, input events — in-page, never OS input).
- [ ] Checks: the page lists 3 + 1 cards with the right names; tokens reach `:root`; choosing a card shows the editor first and hides the hub second and opens that furniture; Close furniture brings the hub back, refreshed; + New furniture → name card → Create opens a new furniture of that name; rename persists to the store; delete arms, names the scene on the strip, deletes on the second click and shows the Note; the read-only failure shows the banner; search and sort reorder; the maximize rect answers MaxButton and the title rect Caption; captures `hub-rest.png`, `hub-hover.png`, `hub-armed.png`, `hub-name-card.png` (composited, `PrintWindow`) for looking at.
- [ ] Gate: `gui_smoke` 6370 + 1 twice on the final tree; `ctest` all; `npm test`; `hub_smoke` 0 failures; look at the four captures.

## Gate record

| When | gui_smoke checks | skips | failures | build |
| --- | --- | --- | --- | --- |
| Installer Qt, run 1 | 6370 | 1 | 0 | 705f388 |
| Installer Qt, run 2 | 6370 | 1 | 0 | 705f388 |
| Phase 2 before review, run 1 | 6370 | 1 | 0 | 2cc483f |
| Phase 2 before review, run 2 | 6370 | 1 | 0 | 2cc483f |
| End of Phase 2, run 1 | 6370 | 1 | 0 | bc9bec6 |
| End of Phase 2, run 2 | 6370 | 1 | 0 | bc9bec6 |
