# Themes, Part 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Five whole-style themes - Violet Glass Float (default for a new or migrating user), Aurora / Mesh Gradient, Nightshade Glass, Ultraviolet Grid, Graphite - switchable from View → Theme and the Settings drawer, each remembering the user's own colour edits; the HTML pages wear the whole style, Qt surfaces and the viewport wear its colours and typeface.

**Architecture:** A theme is DATA - `assets/themes/<id>.json` (name, typeface, the `Theme::Spec` colours it sets, its page tokens) compiled into the resources, so no hex enters `src/`. `Themes` (src/ui) loads them, answers each theme's base spec (Graphite's is `Theme::defaultSpec()`, so a window built with persistence off - `gui_smoke` - stays exactly as today), and switches: it stores the leaving theme's spec through an injected store and installs the arriving one's stored edits or its base. One checkable QAction per theme mirrors into both Settings drawers through `MainWindow::settingsRows()`. The pages take the theme's id and tokens through `ThemeBridge`; `web/src/themes.css` carries each theme's whole style under `:root[data-theme=…]`.

**Tech Stack:** C++17, Qt 6.11.1, Svelte 5 + Vite, bundled Google Fonts (OFL).

**Spec:** `D:/PizzaData/C++/FurnifyMe/DESIGN.md` (locked 2026-10-11 - the default, the four themes' full tokens and rules, "What a theme is", "Rules every theme keeps", "Theme decisions"); captures to match: `D:/PizzaData/C++/FurnifyMe/.dipsynator/_style/screens/B.html` and `_style/ten/screens/{C,G,H}.html`.

## Global Constraints

- Branch `themes`, cut from `editor-session` in `D:/PizzaData/C++/FurnifyMe-html-spike`; never merge, push or delete without asking; never `git stash` (memory: no-stash-pop-for-red-runs).
- `gui_smoke` (persistence off) keeps the Graphite world: 6370 + 1, 0 failures, twice, quiet machine; `Theme::defaultSpec()` is unchanged.
- No hex in `src/` outside `Theme.cpp` (`headless_colour_literals`); theme colours live in `assets/themes/*.json`.
- Every theme keeps the product rules in DESIGN.md "Rules every theme keeps": no focus ring; no modal; accent-text for words at ≥ 4.5:1; grey bodies; hover only under the cursor; selection, hover, sizes and axes mutually distinct; the vocabulary law; motion stops under reduced motion.
- Type scale: four sizes, plus a theme's named extras only (Aurora's 26px card title; Ultraviolet Grid's 32px heading and measured numerals).
- Viewport inks are the theme's own (DESIGN.md "Theme decisions").
- Fonts ship in the build (DM Sans, Sora, Outfit, Plus Jakarta Sans, Inter Tight, IBM Plex Mono) with their OFL licence files; no page fetches a font.
- The theme rows mirror QActions (the Settings mirror law); the user's colour edits are kept per theme.

## Rulings

- **R1 Graphite is `Theme::defaultSpec()`.** A theme's base spec is defaultSpec() with that theme's colours and typeface over it; Graphite sets none. So the suite's world, every pinned hex and every colour file saved before this, means what it meant. Cost: none.
- **R2 The default theme is a STARTUP choice, not a defaultSpec change.** A persisted window with no `theme` key selects Violet Glass Float; a window with persistence off stays on Graphite. An existing user's stored `appearance` becomes their Graphite edits, then Violet Glass Float is selected (DESIGN.md: B is the default). Cost: an existing user sees the new look on first launch, their old colours one click away under Graphite.
- **R3 Qt surfaces take colours and typeface only.** Shapes, glass and type extras reach a surface when it moves to HTML (DESIGN.md "What a theme is"). Cost: Ultraviolet Grid looks half-applied on Qt cards until then.
- **R4 Glass on a page is a tinted translucent fill with a specular rim, NOT a backdrop blur.** The page is composited over the GL viewport by Qt; `backdrop-filter` there samples the page's own (transparent) backing, not the 3D scene, so a blur would blur nothing. Cost: glass reads flatter than the mockup; checked in captures.
- **R5 Reset puts back the CURRENT theme's base**, not Graphite. Cost: none.

## Review Focus

- Switching theme while a colour picker is open, or mid-edit in a Settings field: the picker's Cancel must not write a colour into the NEW theme (Task 3, chrome_smoke).
- Switching and switching back keeps the user's edits to each theme (Task 3, settings_tests and chrome_smoke).
- A stored `theme` id that is not one of the five (an older or newer build) falls back to the default, not to an empty spec (Task 3, settings_tests).
- Every theme's words are readable: text on panel and accent-text on chip-active at ≥ 4.5:1 (Task 1, settings_tests).
- A page loaded while a theme is already active comes up in it, not in Graphite first (Task 4, vitest + chrome_smoke).

---

### Task 1: Themes as data

**Files:**
- Create: `assets/themes/{violet-glass-float,aurora-mesh-gradient,nightshade-glass,ultraviolet-grid,graphite}.json`, `src/ui/Themes.{h,cpp}`
- Modify: `resources.qrc`, `CMakeLists.txt`, `tests/settings_tests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Themes {
  struct Info { QString id; QString name; QString fontFamily; };
  const QVector<Info>& all();                 // in menu order, the default first
  QString defaultId();                        // "violet-glass-float"
  QString graphiteId();                       // "graphite"
  bool exists(const QString& id);
  Theme::Spec baseSpec(const QString& id);    // defaultSpec() + the theme's colours and typeface
  QVariantMap pageTokens(const QString& id);  // the theme's page-only tokens (radii, surfaces, extra sizes)
  }
  ```
- JSON shape: `{ "name": "...", "fontFamily": "...", "spec": { "<Theme token id>": "#rrggbb", ... }, "page": { "radius-card": "20px", ... } }`.

The colours (from DESIGN.md's theme tokens; a token a theme does not name keeps Graphite's):

| token | violet-glass-float | aurora-mesh-gradient | nightshade-glass | ultraviolet-grid |
|---|---|---|---|---|
| chrome | #0B0912 | #0B0B1A | #08060E | #030305 |
| panel | #14101C | #15152B | #110D1B | #0B0B10 |
| chip | #1A122C | #1C1C36 | #1A132C | #0B0B10 |
| chipHover | #241A3A | #24244A | #231A3B | #14131C |
| chipActive | #2E2148 | #2E2E5A | #2A1F48 | #1C1B24 |
| border | #2E2540 | #2A2A44 | #2B2542 | #2A2933 |
| text | #F4F1FB | #F5F5FA | #F3F0FA | #F5F4FA |
| textMuted | #B4ABC8 | #A8A8C0 | #B1A9C6 | #A9A7B8 |
| textDisabled | #5E5773 | #7C7C98 | #5E5773 | #5A5866 |
| accent | #6A00FF | #A78BFA | #A57BFF | #9466FF |
| accentText | #BB9CFF | #C4B5FD | #C9B1FF | #B79BFF |
| danger | #E0564A | #FB7185 | #FF5C8A | #FF6B81 |
| dangerText | #FF8F82 | #FDA4AF | #FF9DB3 | #FF9AA8 |
| caution | #FFCA4A | #FBBF24 | #F4C783 | #FFCA4A |
| viewport | #1C1C1E | #07070F | #0A0812 | #030305 |
| gridMinor | #2D2D31 | #14142A | #1C1828 | #17161E |
| gridMajor | #35353B | #1E1E3A | #2B2542 | #26252F |
| body | #B3B3B3 | #B3B3B8 | #B3B3B3 | #B3B3B3 |
| highlightHover | #06D1FF | #F472B6 | #5EE4F0 | #06D1FF |
| highlightSelected | #FFA500 | #B9A4FB | #FFA500 | #FFFFFF |
| sizesOneBody | #6FB6FF | #67E8F9 | #8EC5FF | #9466FF |
| dimensionLine | #6A00FF | #A78BFA | #A57BFF | #9466FF |
| axisX / gizmoAxisX | #E0564A | #F87171 | #E0564A | #E0564A |
| axisY / gizmoAxisY | #7FC84E | #86EFAC | #7FC84E | #7FC84E |
| gizmoAxisZ | #4A80E0 | #60A5FA | #4A80E0 | #4A80E0 |

Typefaces: violet-glass-float DM Sans; aurora-mesh-gradient Outfit; nightshade-glass Plus Jakarta Sans; ultraviolet-grid Inter Tight; graphite DM Sans (its spec block is empty).

- [ ] **Step 1: Write the failing checks** in `tests/settings_tests.cpp`:

```cpp
    // --- themes as data --------------------------------------------------------
    check(Themes::all().size() == 5 && Themes::all().front().id == Themes::defaultId() &&
              Themes::defaultId() == QStringLiteral("violet-glass-float"),
          "five themes, the default first");
    check(Themes::baseSpec(Themes::graphiteId()) == Theme::defaultSpec(), "Graphite is the shipped spec itself");
    for (const Themes::Info& info : Themes::all()) {
        const Theme::Spec s = Themes::baseSpec(info.id);
        const QByteArray id = info.id.toUtf8();
        check(!info.name.isEmpty() && !info.fontFamily.isEmpty(), "every theme has a name and a typeface: " + id);
        check(contrast(s.text, s.panel) >= 4.5, "text reads on a panel: " + id);
        check(contrast(s.accentText, s.chipActive) >= 4.5, "accent words read on a pressed chip: " + id);
        check(distinct({s.highlightHover, s.highlightSelected, s.sizesOneBody, s.axisX, s.axisY, s.gizmoAxisZ}, 60),
              "selection, hover, sizes and axes are told apart: " + id);
        check(s.highlightHover != s.highlightSelected && s.body.saturationF() < 0.1, "bodies stay grey: " + id);
    }
    check(Themes::baseSpec(QStringLiteral("aurora-mesh-gradient")).highlightSelected !=
              Themes::baseSpec(Themes::graphiteId()).highlightSelected,
          "a theme may recolour the viewport's state inks");
    check(!Themes::exists(QStringLiteral("nope")) && Themes::baseSpec(QStringLiteral("nope")) ==
                                                         Themes::baseSpec(Themes::defaultId()),
          "an unknown id answers the default");
```

with two helpers above `main()` — `contrast(a, b)` (WCAG relative luminance ratio) and `distinct(colours, minimum)` (every pair's RGB distance ≥ minimum).

- [ ] **Step 2: Run red** - `cmake --build --preset windows-web --target settings_tests` → `Themes.h` not found.
- [ ] **Step 3: Write the five JSON files** from the table above (graphite's `spec` is `{}`), each with a `page` block (Task 4 fills it; `{}` now), list them in `resources.qrc` under prefix `/themes`, and implement `Themes` reading `:/themes/<id>.json` once (cached), `baseSpec()` = `defaultSpec()` + each `spec` entry through `Theme::colourTokens()` + `fontFamily`.
- [ ] **Step 4: Run green** - `ctest --test-dir build-web -C RelWithDebInfo -R settings --output-on-failure`. A theme failing a contrast or distinctness check is a DATA bug: fix the JSON value nearest to the token's DESIGN.md hue, and record the change in the ledger.
- [ ] **Step 5: Commit** - `Themes: five whole styles as data (assets/themes)`.

---

### Task 2: The typefaces ship

**Files:**
- Create: `assets/fonts/{Sora,Outfit,PlusJakartaSans,InterTight}[wght].ttf`, `assets/fonts/IBMPlexMono-{Regular,Medium}.ttf`, `assets/fonts/OFL/<family>.txt`, `web/public/fonts/` (the same files)
- Modify: `resources.qrc`, `src/ui/Theme.cpp:728` (register every bundled face), `web/src/tokens.css` (@font-face per family), `tests/settings_tests.cpp`

- [ ] **Step 1: Failing check:** for each `Themes::Info`, `QFontDatabase::families().contains(info.fontFamily)` after `Theme::apply()` → "every theme's typeface is installed: <id>".
- [ ] **Step 2: Run red.**
- [ ] **Step 3:** Download from `https://github.com/google/fonts/raw/main/ofl/<dir>/` (sora, outfit, plusjakartasans, intertight, ibmplexmono) with each `OFL.txt`; add to `resources.qrc` under `/fonts`; register each in `Theme::apply()` beside DM Sans. Pages: copy to `web/public/fonts/`, `@font-face` in `tokens.css` with `url('../fonts/<file>')`, so the page build references them beside `dist/<page>/` rather than inlining megabytes into every page; `build.mjs` copies `public/fonts` to `dist/fonts`.
- [ ] **Step 4: Run green** (settings_tests; `npm run build` then check `dist/fonts` holds six families and a built page's CSS names them).
- [ ] **Step 5: Commit** - `Fonts: the five themes' typefaces ship (OFL)`.

---

### Task 3: Switching themes

**Files:**
- Modify: `src/ui/Themes.{h,cpp}`, `src/ui/SettingsEdits.cpp` (reset → current theme's base), `src/MainWindow.{h,cpp}` (actions, View → Theme, settingsRows, store, startup), `src/web/ThemeBridge.{h,cpp}` (`themeId`), `tests/settings_tests.cpp`, `tests/chrome_smoke.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Themes {
  // Where each theme's edited spec lives: QSettings in the app, a map in tests.
  struct Store { std::function<QString(const QString& id)> load; std::function<void(const QString& id, const QString& spec)> save; };
  void setStore(Store store);
  QString currentId();               // graphite until select() runs
  bool select(const QString& id);    // saves the leaving theme's live spec, installs the arriving one's stored edits or its base; false for an unknown id
  QObject* notifier();               // emits currentChanged(QString id)
  }
  ```
  `SettingsEdits::reset()` → `Theme::setSpec(Themes::baseSpec(Themes::currentId()))`. MainWindow: one checkable QAction per theme (text = the theme's name) in a `QActionGroup`, in **View → Theme**; `settingsRows()` gains a stacked Choice row "Theme" on the Colours tab, placed FIRST on that tab in both drawers; startup (persistence on) reads `theme` (unknown or absent → `defaultId()`; a legacy `appearance` value and no `appearance/graphite` → stored as Graphite's edits first) then `select()`s it; the existing `appearance` debounce writes `appearance/<currentId>`.

- [ ] **Step 1: Failing checks** (settings_tests, with a map-backed store):

```cpp
    {
        QHash<QString, QString> saved;
        Themes::setStore({[&](const QString& id) { return saved.value(id); },
                          [&](const QString& id, const QString& spec) { saved.insert(id, spec); }});
        check(Themes::currentId() == Themes::graphiteId(), "a window starts on Graphite until a theme is chosen");
        check(Themes::select(QStringLiteral("nightshade-glass")) &&
                  Theme::spec() == Themes::baseSpec(QStringLiteral("nightshade-glass")),
              "choosing a theme installs its base");
        SettingsEdits::setTokenColour(QStringLiteral("accent"), QColor(1, 2, 3));
        Themes::select(Themes::graphiteId());
        check(Theme::spec().accent != QColor(1, 2, 3), "another theme does not wear that edit");
        Themes::select(QStringLiteral("nightshade-glass"));
        check(Theme::spec().accent == QColor(1, 2, 3), "coming back brings the edit back (Review Focus 2)");
        SettingsEdits::reset();
        check(Theme::spec() == Themes::baseSpec(QStringLiteral("nightshade-glass")), "Reset puts back the theme's own base");
        check(!Themes::select(QStringLiteral("from-a-newer-build")) && Themes::currentId() == QStringLiteral("nightshade-glass"),
              "an unknown theme changes nothing (Review Focus 3)");
        Themes::select(Themes::graphiteId());
    }
```

and in chrome_smoke: the Settings card's Colours tab shows a Theme row with five options, the current one checked; clicking Aurora triggers the action, `Themes::currentId()` follows, the page's `document.documentElement.dataset.theme` becomes `aurora-mesh-gradient`, and a colour picker opened before the switch is closed by it without writing (Review Focus 1: `SettingsEdits::activeColourPicker()` is null after `select()`, and the new theme's accent is its base).

- [ ] **Step 2: Run red.**
- [ ] **Step 3: Implement.** `select()` closes any open colour picker FIRST (`activeColourPicker()->close()` after disconnecting its `rejected` restore), then saves `Theme::serializeSpec()` under the leaving id, then installs `deserializeSpec(load(id))` or `baseSpec(id)`, then emits `currentChanged`. `ThemeBridge` gains `Q_PROPERTY(QString themeId ...)` and re-emits `changed` on `currentChanged`; `tokens()` also carries `pageTokens(currentId())`.
- [ ] **Step 4: Run green** - settings_tests, chrome_smoke, and gui_smoke filtered `settings` and `appearance` (the Colours tab's new first row - a check that counts the tab's rows is updated to name the Theme row, never deleted).
- [ ] **Step 5: Commit** - `Themes: switch from View → Theme and Settings, each theme keeps its edits`.

---

### Task 4: The pages wear the whole style

**Files:**
- Create: `web/src/themes.css`
- Modify: `web/src/theme.ts` (set `data-theme`), `web/src/tokens.css` (shape/surface/type variables), the page components that hard-code a radius, a border, a font family or a shadow (`hub/*`, `editor/Toast.svelte`, `editor/StatusStrip.svelte`, `editor/settings/*`), `assets/themes/*.json` (`page` blocks), `web/test/theme.test.ts` (new)

The variables every component reads instead of a literal: `--radius-card`, `--radius-chip`, `--radius-control`, `--radius-pill`, `--surface-fill`, `--surface-border`, `--surface-rim` (specular top-left highlight, `none` for flat themes), `--surface-shadow`, `--font-family`, `--font-display` (titles; Aurora: Sora), `--font-numeric` (Aurora: Plus Jakarta Sans; UV Grid: Inter Tight), `--font-mono` (UV Grid labels: IBM Plex Mono), `--font-display-size` (Aurora 26px card title; others = `--font-title`), `--grain` (Aurora/Violet: on), `--glow` (the theme's edge glow behind the viewport-free chrome of opaque pages: the hub). Graphite's values equal today's literals, so Graphite renders unchanged.

- [ ] **Step 1: Failing tests** (`web/test/theme.test.ts`): `applyTokens({...tokens, themeId: 'ultraviolet-grid'})` sets `document.documentElement.dataset.theme`; under it a rendered Settings card computes `border-radius: 0px` and a hub card `0px`; under `graphite` the Settings card is `10px`; a page loaded with a theme already set has `data-theme` before its first paint (main.ts applies tokens before `mount()` - Review Focus 5).
- [ ] **Step 2: Run red.**
- [ ] **Step 3: Implement** - the variable refactor with Graphite = today's values (vitest stays 89 green throughout), then `themes.css` per DESIGN.md (B: capsules, 26px panels, rim, violet edge glow + grain on the hub; Aurora: 20px cards, Sora 300 titles at 26px, grain, aurora glow on the hub; Nightshade: 14px panels, frosted fill with the 1px light edge; UV Grid: 0px everywhere, hairline borders, mono uppercase labels, no shadow).
- [ ] **Step 4: Run green** - vitest, svelte-check; then hub_smoke and chrome_smoke capture each theme (`hub-<id>.png`, `chrome-<id>.png`, `settings-<id>.png`); look at every capture beside its DESIGN.md mockup (`_style/screens/B.html`, `_style/ten/screens/{C,G,H}.html`) and fix what does not match. Every capture goes on the board card.
- [ ] **Step 5: Commit** - `Pages: each theme's whole style (themes.css)`.

---

### Task 5: Gate, review, docs

- [ ] **Step 1:** full build; ctest; vitest; svelte-check; hub_smoke; chrome_smoke; then two quiet unfiltered `gui_smoke` runs - Expected `6370 + 1, 0 failures` twice (Graphite world unchanged; a check updated in Task 3 keeps the count).
- [ ] **Step 2:** whole-branch review (opus) with this plan's Review Focus; fix Critical/Important test-first.
- [ ] **Step 3:** CLAUDE.md - a "Themes" section (data in assets, Graphite = defaultSpec, the startup choice, per-theme edits, what a Qt surface takes), architecture rows; board T-072 notes with the captures; gate table.

## Gate record

| When | Checks | Skips | Failures | Run |
| --- | --- | --- | --- | --- |
| dab7161 (Tasks 1-4), quiet, run 1 | 6448 | 1 | 1 (a refusal's "wrote nothing" - a save moved revision(); root-fixed in 3c35779) | dab7161 |
| dab7161, quiet, run 2 | 6448 | 1 | 0 | dab7161 |
| 3c35779 (review fixes), quiet, run 1 | 6448 | 1 | 0 | 3c35779 |
| 3c35779, quiet, run 2 | 6448 | 1 | 0 | 3c35779 |

The count rose 6370 -> 6448: six new strings (five theme names and "Theme") through the
banned-word sweep, 13 times each, found by diffing the check names of two runs.
kCheckFloor re-ratcheted to the measured 6449.
