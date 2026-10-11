# Settings Drawer as HTML (Migration Phase 3b) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Settings drawer (`AppearancePanel`) drawn and driven by the editor's chrome page in HTML-chrome mode - four tabs, colour swatches, the type and line fields, every mirrored `QAction` row, Save/Load/Reset - a faithful port, with the Qt drawer kept (and still gated by `gui_smoke`) until Phase 6.

**Architecture:** One table of rows (`SettingsEdits`) feeds both drawers, and every edit goes through one implementation (`SettingsEdits::set*`, carved out of `AppearancePanel`). In HTML mode the Qt card is swapped, in place, for an empty `HtmlCardSlot` that `ViewportOverlay` anchors exactly where the card was, so stacking under the gizmo, `occupiedRects()` and the action-derived visibility keep their laws; the page draws the card at the slot's rectangle. A `SettingsBridge` channel object carries the live spec, the table and the watched actions' states to the page and takes edits back.

**Tech Stack:** C++17, Qt 6.11.1 (installer build, WebEngine + WebChannel), Svelte 5 + TypeScript + Vite, vitest + jsdom.

**Spec:** CLAUDE.md "Settings: one drawer, four tabs, and every row a mirror" (the contract), `src/ui/AppearancePanel.{h,cpp}` (the behaviour being ported - read its header comment in full), the migration report linked from board card T-066, and CLAUDE.md "The HTML UI (migration, branch `editor-session`)" including "What a page over the viewport owes".

## Global Constraints

- Branch `editor-session`, worktree `D:/PizzaData/C++/FurnifyMe-html-spike`; build with `cmake --build --preset windows-web`; never merge, push or delete without asking.
- `gui_smoke` never enables HTML chrome; its gate is **6370 checks + 1 environment skip, 0 failures, twice** on the final tree, with `kCheckFloor` untouched.
- Every row that is not a `Theme::Spec` value mirrors a `QAction`: the page holds no checked state; a click calls the action's trigger; an exclusive group's current member is not re-triggered; a binary row's half triggers only from the state it is NOT.
- Every `Theme::Spec` edit goes through `Theme::setSpec()` (via `SettingsEdits`), and the page re-reads on every Theme broadcast - no page caches a value across it.
- Copy is verbatim from `AppearancePanel.cpp`/`MainWindow.cpp`; every painted string passes `web/test/banned.ts`; no `role=alert`; no focus ring; chips wear `use:chip`; typed fields keep their focus.
- Card: 296 x 380 logical px, radius 10, padding 12 (`Theme::Space::Snug`); tabs 24 tall, 3 px gaps; segment chips 22 tall, radius 6; switch 38 x 20, knob 14; swatch 46 x 18, radius 4, 6 px right gutter; title in the title font, weight 600, `textMuted`.
- Reset: two clicks within `kResetConfirmMs` (4000 ms), labels "Reset colours and sizes" / "Reset — click again", the arm dropped when the drawer hides.
- Colour-file refusals and failures keep MainWindow's existing sentences, reported through `EditorSession::reportFailure`.
- No hex outside `Theme.cpp` (`headless_colour_literals`); pages use tokens only.
- Every new check is watched red before its code exists; a page change is looked at in a composited capture.

## Rulings (decided while planning; each costs what it says if wrong)

- **R1 The card's place is a Qt slot.** `ViewportOverlay::replaceWidget(old, new)` swaps the Qt card for an `HtmlCardSlot` in the same anchor and order, so nothing about TopRight stacking, `occupiedRects()` or the visibility lambda changes. Cost if wrong: one more indirection to delete in Phase 6.
- **R2 The colour picker stays Qt's modeless `QColorDialog`**, opened by C++ with the same live preview and Cancel-restores, because a picker drawn in HTML is a design question for a mockup round, not a port. Cost: one Qt window remains in the Settings flow until that round.
- **R3 Save/Load colours keep the native file dialogs** (the one modal surface the app uses on purpose). Cost: none new.
- **R4 The font list is an in-page listbox, not a `<select>`.** A native select cannot preview on highlight, and live preview with Escape-restores is the Qt behaviour. Cost: a custom control to keep accessible.
- **R5 The page takes the keyboard only while a typed field is being edited** (`claimKeyboard()`/`releaseKeyboard()`), and while it holds it the view claims every `ShortcutOverride`, so Delete or Ctrl+Z in a field never reaches the window's shortcuts. The view is otherwise `Qt::NoFocus` (review I7 of Phase 3a). Cost: a keyboard path to keep in step with WebEngine's focus proxy.
- **R6 One table, two drawers.** `SettingsEdits::rows()` is composed by MainWindow once and read by both the Qt panel and the bridge; the number fields and token names come from `SettingsEdits` tables the Qt panel also reads. Cost: a refactor of the Qt panel's constructor, gated by `gui_smoke`'s unchanged count.

## Review Focus

- A key typed into a Settings field (Delete, Backspace, Ctrl+Z, a digit) must edit the field and never reach the window: no body deleted, no undo, no view snapped by `0`-`3`. Escape or Enter gives focus back to whatever had it before. (Task 5, chrome_smoke.)
- A wheel over the card - over its title, or over the colour list at either end of its scroll - must not zoom the camera. (Task 5, chrome_smoke.)
- An edit made from outside the page (Load colours, Reset from the Qt path, a Theme change at startup) re-dresses the open card: every swatch, field and the font name. (Task 6 vitest, Task 7 chrome_smoke.)
- The drawer hiding mid-edit (Settings toggled off, render mode) releases the keyboard, closes the font list restoring the family it opened over, and drops an armed reset. (Task 6 vitest, Task 7 chrome_smoke.)
- A click on an exclusive option that is already current, or on a control whose action is disabled, triggers nothing. (Task 4 settings_tests, Task 6 vitest.)

---

## File Structure

| File | Responsibility |
|---|---|
| `src/ui/SettingsEdits.{h,cpp}` (new) | The Settings drawer's model: the tab table, the row table type, the number-field table, token names and group headings, and every edit (`setNumber`, `setTokenColour`, `setFontFamily`, `reset`, `saveColoursTo`, `loadColoursFrom`). No widgets. |
| `src/ui/AppearancePanel.{h,cpp}` | Delegates to `SettingsEdits`; builds its rows from `SettingsEdits::Row` and its number fields from `SettingsEdits::numberField()`. |
| `src/session/ActionRegistry.{h,cpp}` | Adds `snapshotOf(ids)`. |
| `src/session/ActionWatch.{h,cpp}` (new) | Watches a set of action ids and emits one coalesced `changed()` per event-loop turn. |
| `src/ui/ViewportOverlay.{h,cpp}` | Adds `replaceWidget(old, replacement)`. |
| `src/web/HtmlCardSlot.{h,cpp}` (new) | An empty, mouse-transparent widget of a fixed size that holds an HTML card's place in the overlay. |
| `src/web/SettingsBridge.{h,cpp}` (new) | The page's `settings` object: layout, live spec, watched actions; slots for every edit, the picker, save/load, trigger. |
| `src/web/EditorChrome.{h,cpp}` | Registers `settings`; `cards` property; `claimKeyboard()`/`releaseKeyboard()`; the shortcut and wheel shields. |
| `src/MainWindow.{h,cpp}` | Composes `settingsRows()` once; in HTML mode swaps the Qt card for a slot and wires the bridge. |
| `web/src/editor/settings/*.svelte` (new) | `Settings.svelte` (card, title, close x, tabs), `ColourList.svelte`, `NumberField.svelte`, `Switch.svelte`, `Segments.svelte`, `FontPicker.svelte`, `ResetButton.svelte`. |
| `web/src/editor/settings/api.ts`, `mock.ts`, `copy.ts` (new) | The `settings` object's shape, a mock, the page's own words. |
| `web/src/editor/Editor.svelte`, `api.ts`, `mock.ts` | Mount the card at the slot's rect; `cards`, `claimKeyboard`, `releaseKeyboard`. |
| `tests/settings_tests.cpp` (new, ctest, offscreen) | SettingsEdits, ActionWatch-free bridge checks, `replaceWidget`. |
| `tests/session.cpp` | `snapshotOf`, `ActionWatch`. |
| `tests/chrome_smoke.cpp` | The drawer end to end, the keyboard and wheel shields, captures. |
| `web/test/settings.test.ts` (new) | The page against the mock. |

---

### Task 1: SettingsEdits - one model and one edit path for both drawers

**Files:**
- Create: `src/ui/SettingsEdits.h`, `src/ui/SettingsEdits.cpp`, `tests/settings_tests.cpp`
- Modify: `src/ui/AppearancePanel.h`, `src/ui/AppearancePanel.cpp`, `src/MainWindow.cpp:2516-2563`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace SettingsEdits {
  enum class Tab { Colours = 0, TextLines = 1, Viewport = 2, UnitsSaving = 3 };
  struct TabInfo { Tab tab; QString name; QString tip; };
  const QVector<TabInfo>& tabs();
  struct NumberField { QString id; QString label; double min, max, step; int decimals; QString suffix; QString tip; };
  // ids: basePt, chipStrokePx, gridDensity, edgeWidthPx, sketchLineWidthPx, gizmoScale
  const QVector<NumberField>& numberFields();
  const NumberField* numberField(const QString& id);
  double numberValue(const Theme::Spec& spec, const QString& id);   // NaN for an unknown id
  bool setNumber(const QString& id, double value);                 // clamps; false for an unknown id
  struct Row { enum class Kind { Toggle, Choice, Binary, Number }; Kind kind; Tab tab; QString label;
               QVector<QAction*> actions; QStringList optionLabels; bool stacked = false; QString numberId; };
  QString tokenName(const QString& id);          // AppearancePanel::nameForToken's table
  QString groupHeadingFor(const QString& id);    // "" unless this token opens a group
  bool isRetiredToken(const QString& id);
  void setTokenColour(const QString& id, const QColor& colour);
  void setFontFamily(const QString& family);
  QString familyDisplayName(const QString& family);
  QVector<QPair<QString, QString>> fontFamilies(); // (shown, family), bundled first, each shown name once
  void reset();
  bool saveColoursTo(const QString& path);
  bool loadColoursFrom(const QString& path);
  QString colourFileSuffix();   // ".furnifytheme"
  QString colourFileFilter();
  QString resetLabel();         // "Reset colours and sizes"
  QString resetArmedLabel();    // "Reset — click again"
  constexpr int kResetConfirmMs = 4000;
  }
  ```
  `AppearancePanel::Tab` becomes `using Tab = SettingsEdits::Tab;`; `AppearancePanel::addRows(const std::vector<SettingsEdits::Row>&)`; `MainWindow::settingsRows()` returns the vector.

- [ ] **Step 1: Write the failing test** - `tests/settings_tests.cpp`:

```cpp
// The Settings drawer's model (migration Phase 3b), with no window: the
// tables both drawers read and the one edit path both drive.
#include "SettingsEdits.h"
#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++gFailures;
}
}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::apply(app);

    // --- the tables ---------------------------------------------------------
    check(SettingsEdits::tabs().size() == 4 && SettingsEdits::tabs()[3].name == "Working",
          "four tabs, the last named Working");
    const auto* size = SettingsEdits::numberField("basePt");
    check(size && size->min == Theme::kMinBasePt && size->max == Theme::kMaxBasePt &&
              size->suffix == " pt" && size->decimals == 0,
          "Text size is 8-14 pt, whole points");
    const auto* edge = SettingsEdits::numberField("edgeWidthPx");
    check(edge && edge->step == 0.5 && edge->decimals == 1 && edge->suffix == " px",
          "Edge lines step by 0.5 px");
    check(SettingsEdits::numberField("gridDensity") &&
              SettingsEdits::numberField("gridDensity")->suffix == "x",
          "Grid detail is a bare multiplier");
    check(SettingsEdits::tokenName("gridMajor") == QString::fromUtf8("Grid — every tenth line"),
          "token names are the user's words");
    check(SettingsEdits::groupHeadingFor("axisX") == "Axes and gizmo" &&
              SettingsEdits::groupHeadingFor("axisY").isEmpty(),
          "a heading opens a group, and only there");
    check(SettingsEdits::isRetiredToken("focusRing"), "the focus-ring tokens are retired");

    // --- the edits ----------------------------------------------------------
    SettingsEdits::setNumber("basePt", 99);
    check(Theme::spec().basePt == Theme::kMaxBasePt, "a number is clamped to its range");
    check(!SettingsEdits::setNumber("nonsense", 1), "an unknown field is refused");
    SettingsEdits::setTokenColour("accent", QColor(10, 20, 30));
    check(Theme::spec().accent == QColor(10, 20, 30), "a colour edit lands in the live spec");
    const auto families = SettingsEdits::fontFamilies();
    check(!families.isEmpty() && families.front().second == Theme::defaultSpec().fontFamily,
          "the bundled family is listed first");

    QTemporaryDir dir;
    const QString path = dir.filePath("mine.furnifytheme");
    check(SettingsEdits::saveColoursTo(path), "colours save to a file");
    SettingsEdits::reset();
    check(Theme::spec() == Theme::defaultSpec(), "reset puts the shipped spec back");
    check(SettingsEdits::loadColoursFrom(path) && Theme::spec().accent == QColor(10, 20, 30),
          "and loading the file brings the colours back");
    const QString junk = dir.filePath("junk.furnifytheme");
    QFile f(junk);
    f.open(QIODevice::WriteOnly);
    f.write("not a colour file");
    f.close();
    const Theme::Spec before = Theme::spec();
    check(!SettingsEdits::loadColoursFrom(junk) && Theme::spec() == before,
          "a file that is not a colour file changes nothing");

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
```

Add to `CMakeLists.txt` inside the app block, beside `session_tests`:

```cmake
add_executable(settings_tests tests/settings_tests.cpp)
target_link_libraries(settings_tests PRIVATE furnify_app)
add_test(NAME settings_tests COMMAND settings_tests)
set_tests_properties(settings_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
```

and append `settings_tests` to the POST_BUILD deploy loop that already lists `chrome_smoke`.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build --preset windows-web --target settings_tests`
Expected: FAIL to compile - `SettingsEdits.h` not found.

- [ ] **Step 3: Carve `SettingsEdits` out of `AppearancePanel`.** Move, unchanged in behaviour: `nameForToken`'s table to `tokenName`, the `groupOpeners` list to `groupHeadingFor`, `isRetiredToken`, `familyDisplayName`, the families loop (bundled first, `QFontDatabase::families()`, each shown name once) to `fontFamilies()`, each `set*` body, `reset`, `saveColoursTo`, `loadColoursFrom` (with `kMaxColourFileBytes`), the suffix/filter and the two reset labels. The number table, verbatim from the constructor and `addGridDetailRow`/`addGizmoSizeRow`:

```cpp
const QVector<NumberField>& numberFields()
{
    static const QVector<NumberField> fields = {
        {"basePt", QObject::tr("Text size"), Theme::kMinBasePt, Theme::kMaxBasePt, 1, 0, QObject::tr(" pt"),
         QObject::tr("How big the app's text is — everything else in the type scale moves with it")},
        {"edgeWidthPx", QObject::tr("Edge lines"), Theme::kMinEdgeWidthPx, Theme::kMaxEdgeWidthPx, 0.5, 1,
         QObject::tr(" px"),
         QObject::tr("How thick the lines along a body's own edges are — 0 leaves the shading with none at all")},
        {"sketchLineWidthPx", QObject::tr("Outline lines"), Theme::kMinSketchLineWidthPx,
         Theme::kMaxSketchLineWidthPx, 0.5, 1, QObject::tr(" px"),
         QObject::tr("How thick the line an outline draws is — while it is being drawn and once it is closed")},
        {"chipStrokePx", QObject::tr("Button border"), Theme::kMinChipStrokePx, Theme::kMaxChipStrokePx, 1, 0,
         QObject::tr(" px"), QObject::tr("How thick the line around the tool buttons is — 0 leaves only the fill")},
        {"gridDensity", QObject::tr("Grid detail"), Theme::kMinGridDensity, Theme::kMaxGridDensity, 0.1, 1,
         QStringLiteral("x"),
         QObject::tr("How many lines the work-plane grid draws — higher packs more in, lower thins it out")},
        {"gizmoScale", QObject::tr("Gizmo size"), Theme::kMinGizmoScale, Theme::kMaxGizmoScale, 0.1, 1,
         QStringLiteral("x"), QObject::tr("How large the Move, Rotate and Scale handles draw over a selected body")},
    };
    return fields;
}
```

`setNumber(id, value)` clamps to the field's range and writes the matching `Spec` member through `Theme::setSpec()`; `numberValue` reads it. `AppearancePanel`'s public `set*`, `reset`, `saveColoursTo`, `loadColoursFrom`, `nameForToken`, `isRetiredToken`, `familyDisplayName`, `resetLabel`, `resetArmedLabel`, `colourFileSuffix`, `colourFileFilter` stay as one-line delegations (gui_smoke calls them). The spin rows read their range, step, decimals, suffix and tooltip from `numberField(id)`.

- [ ] **Step 4: One table of rows.** Add `std::vector<SettingsEdits::Row> MainWindow::settingsRows() const` returning the rows `buildOverlay()` adds today, in today's order (Show the grid; Number gridDensity; Show sizes around a selection; Binary Projection Perspective/Orthographic; Number gizmoScale; Show notifications; Choice Sizes in; Snap to Grid; Magnet; Choice Autosave stacked). Add `AppearancePanel::addRows(rows)` that calls the existing `addToggleRow`/`addChoiceRow`/`addBinaryChoiceRow`/`addGridDetailRow`/`addGizmoSizeRow` per row, and replace the ten calls in `buildOverlay()` with `myAppearancePanel->addRows(settingsRows());`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset windows-web && ctest --test-dir build-web -C RelWithDebInfo -R settings_tests --output-on-failure`
Expected: `PASS (0 failures, 13 checks)`.
Run: `build-web/RelWithDebInfo/gui_smoke.exe build-web/smoke-t1 settings` and `... appearance`
Expected: every block green, the same check counts the Phase 3a gate printed for those blocks.

- [ ] **Step 6: Commit** - `git commit -m "Settings: one model and one edit path for both drawers (SettingsEdits)"`

---

### Task 2: Watching a few actions without re-sending all of them

**Files:**
- Create: `src/session/ActionWatch.h`, `src/session/ActionWatch.cpp`
- Modify: `src/session/ActionRegistry.{h,cpp}`, `tests/session.cpp`, `CMakeLists.txt` (furnify_session sources)

**Interfaces:**
- Produces: `QJsonArray ActionRegistry::snapshotOf(const QStringList& ids) const` (same fields as `snapshot()`, in the order asked, unknown ids skipped); `class ActionWatch : QObject { ActionWatch(ActionRegistry*, QObject* parent); void setIds(const QStringList&); QJsonArray snapshot() const; signals: void changed(); }` - one `changed()` per event-loop turn however many watched actions changed in it.

- [ ] **Step 1: Write the failing checks** in `tests/session.cpp`, after the registry block:

```cpp
    {
        ActionRegistry registry;
        QObject owner;
        auto* grid = new QAction(QStringLiteral("Show the &grid"), &owner);
        grid->setCheckable(true);
        auto* snap = new QAction(QStringLiteral("Snap to Grid"), &owner);
        snap->setCheckable(true);
        registry.adoptChildren(&owner);
        const QJsonArray two = registry.snapshotOf({"snap-to-grid", "show-the-grid", "nope"});
        check(two.size() == 2 && two[0].toObject()["id"].toString() == "snap-to-grid",
              "snapshotOf answers the ids asked, in that order, skipping unknown ones");

        ActionWatch watch(&registry);
        watch.setIds({"show-the-grid", "snap-to-grid"});
        int fired = 0;
        QObject::connect(&watch, &ActionWatch::changed, [&] { ++fired; });
        grid->setChecked(true);
        snap->setChecked(true);
        grid->setEnabled(false);
        check(fired == 0, "a watch waits for the event loop");
        QCoreApplication::processEvents();
        check(fired == 1, "and then says so once for three changes");
        check(watch.snapshot()[0].toObject()["checked"].toBool() &&
                  !watch.snapshot()[0].toObject()["enabled"].toBool(),
              "its snapshot reads the actions as they are now");
        auto* other = new QAction(QStringLiteral("Not watched"), &owner);
        registry.add(other);
        other->setEnabled(false);
        QCoreApplication::processEvents();
        check(fired == 1, "an action nobody watches changes nothing");
    }
```

- [ ] **Step 2: Run it to see it fail** - `cmake --build --preset windows-web --target session_tests` - Expected: compile error, `ActionWatch.h` not found.

- [ ] **Step 3: Implement.** `snapshotOf` reuses the per-action object `snapshot()` builds (factor it into a private `static QJsonObject describe(const QString& id, const QAction*)`). `ActionWatch::setIds` disconnects what it watched, connects each named action's `QAction::changed` to a private `schedule()` that starts a zero-interval single-shot `QTimer` (member, `setSingleShot(true)`), whose `timeout` emits `changed()`.

- [ ] **Step 4: Run** - `ctest --test-dir build-web -C RelWithDebInfo -R session --output-on-failure` - Expected: both session tests pass, including `session_no_widgets` (ActionWatch includes no Widgets header).

- [ ] **Step 5: Commit** - `git commit -m "Session: ActionWatch - a few actions' state, coalesced per turn"`

---

### Task 3: A place in the overlay for an HTML card

**Files:**
- Create: `src/web/HtmlCardSlot.h`, `src/web/HtmlCardSlot.cpp`
- Modify: `src/ui/ViewportOverlay.{h,cpp}`, `tests/settings_tests.cpp`, `CMakeLists.txt` (HtmlCardSlot in the web sources)

**Interfaces:**
- Produces: `bool ViewportOverlay::replaceWidget(QWidget* old, QWidget* replacement)` - the replacement takes the old entry's anchor and its position in the order; `old` is hidden and no longer anchored; false (nothing changed) when `old` is not an entry. `class HtmlCardSlot : public QWidget { HtmlCardSlot(const QString& name, QSize size, QWidget* viewport); QString name() const; signals: void placed(); }` - fixed at `Theme::wholeDevicePixels(size)`, `WA_TransparentForMouseEvents`, paints nothing, emits `placed()` on move, resize, show and hide.

- [ ] **Step 1: Write the failing checks** in `tests/settings_tests.cpp` (it links `furnify_app`, which has the web sources in this build):

```cpp
    {
        QWidget viewport;
        viewport.resize(1000, 700);
        ViewportOverlay overlay(&viewport);
        QWidget top(&viewport);
        top.setFixedSize(100, 50);
        QWidget card(&viewport);
        card.setFixedSize(296, 380);
        overlay.addWidget(&top, ViewportOverlay::Anchor::TopRight);
        overlay.addWidget(&card, ViewportOverlay::Anchor::TopRight);
        viewport.show();
        overlay.relayout();
        const QRect was = card.geometry();
        HtmlCardSlot slot(QStringLiteral("settings"), QSize(296, 380), &viewport);
        check(overlay.replaceWidget(&card, &slot), "a card can be swapped for a slot");
        slot.show();
        overlay.relayout();
        check(slot.geometry() == was, "the slot stands exactly where the card stood");
        check(!card.isVisible(), "and the card it replaced is hidden");
        bool listed = false;
        for (const QRect& r : overlay.occupiedRects()) listed = listed || r == slot.geometry();
        check(listed, "the slot is an obstacle the toast steps around");
        check(slot.testAttribute(Qt::WA_TransparentForMouseEvents), "and takes no click itself");
        check(!overlay.replaceWidget(&card, &slot), "a widget that is not an entry is refused");
    }
```

- [ ] **Step 2: Run to see it fail** - Expected: compile error, `HtmlCardSlot.h` not found / `replaceWidget` not a member.

- [ ] **Step 3: Implement** both. `replaceWidget` finds the entry whose `widget == old`, sets `entry.widget = replacement`, calls `old->hide()`, installs the overlay's event filter on `replacement` exactly as `addWidget` does, and calls `relayout()`.

- [ ] **Step 4: Run** - `ctest ... -R settings_tests` - Expected: PASS, 18 checks.

- [ ] **Step 5: Commit** - `git commit -m "Overlay: replaceWidget, and HtmlCardSlot to hold an HTML card's place"`

---

### Task 4: SettingsBridge - the page's `settings` object

**Files:**
- Create: `src/web/SettingsBridge.h`, `src/web/SettingsBridge.cpp`
- Modify: `tests/settings_tests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `SettingsEdits` (Task 1), `ActionRegistry` + `ActionWatch` (Task 2).
- Produces:
  ```cpp
  class SettingsBridge : public QObject {
      Q_OBJECT
      Q_PROPERTY(QVariantMap layout READ layout CONSTANT)   // tabs, tokens, numbers, rows, families, words
      Q_PROPERTY(QVariantMap spec READ spec NOTIFY specChanged)  // colours {id: "#rrggbb"}, numbers {id: value}, fontFamily
      Q_PROPERTY(QJsonArray actions READ actions NOTIFY actionsChanged)
  public:
      SettingsBridge(ActionRegistry* registry, const std::vector<SettingsEdits::Row>& rows, QObject* parent);
  public slots:
      void setNumber(const QString& id, double value);
      void setFontFamily(const QString& family);
      void pickColour(const QString& id);      // R2: the modeless QColorDialog
      void reset();
      void saveColours();                      // R3: native dialog, then SettingsEdits::saveColoursTo
      void loadColours();
      bool trigger(const QString& actionId);   // ActionRegistry::trigger - refuses a disabled action
  signals:
      void specChanged(); void actionsChanged();
      void colourSaveFailed(const QString& path); void colourLoadRefused(const QString& path);
  };
  ```
  `layout.rows` is a list of `{kind: "toggle"|"choice"|"binary"|"number", tab: 0-3, label, actions: [ids], options: [labels], stacked, number}`; `layout.numbers` the number table; `layout.tokens` `[{id, name, group}]` with retired tokens left out; `layout.families` `[{shown, family}]`; `layout.words` `{title, save, load, saveTip, loadTip, reset, resetArmed, resetTip, close, pickTip}` (`pickTip` is `"Pick a colour for %1"`).

- [ ] **Step 1: Write the failing checks** in `tests/settings_tests.cpp`:

```cpp
    {
        ActionRegistry registry;
        QObject owner;
        auto* grid = new QAction(QStringLiteral("Show the &grid"), &owner);
        grid->setCheckable(true);
        auto* mm = new QAction(QStringLiteral("Millimetres"), &owner);
        auto* cm = new QAction(QStringLiteral("Centimetres"), &owner);
        for (QAction* a : {mm, cm}) a->setCheckable(true);
        auto* group = new QActionGroup(&owner);
        group->addAction(mm);
        group->addAction(cm);
        mm->setChecked(true);
        int cmTriggered = 0, mmTriggered = 0;
        QObject::connect(cm, &QAction::triggered, [&] { ++cmTriggered; });
        QObject::connect(mm, &QAction::triggered, [&] { ++mmTriggered; });
        registry.adoptChildren(&owner);
        using Row = SettingsEdits::Row;
        std::vector<Row> rows;
        rows.push_back({Row::Kind::Toggle, SettingsEdits::Tab::Viewport, "Show the grid", {grid}, {}, false, {}});
        rows.push_back({Row::Kind::Choice, SettingsEdits::Tab::UnitsSaving, "Sizes in", {mm, cm},
                        {"Millimetres", "Centimetres"}, false, {}});
        SettingsBridge bridge(&registry, rows, nullptr);

        const QVariantList layoutRows = bridge.layout().value("rows").toList();
        check(layoutRows.size() == 2 &&
                  layoutRows[1].toMap().value("actions").toStringList() ==
                      QStringList({"millimetres", "centimetres"}),
              "the layout names each row's actions by id");
        check(bridge.layout().value("tokens").toList().size() ==
                  Theme::colourTokens().size() - 2,
              "every token but the two retired ones has a row");
        check(bridge.spec().value("colours").toMap().value("accent").toString() ==
                  Theme::spec().accent.name(),
              "the spec carries the live colours");
        bridge.setNumber("gizmoScale", 1.5);
        check(Theme::spec().gizmoScale == 1.5, "a number from the page lands through SettingsEdits");
        check(bridge.trigger("centimetres") && cmTriggered == 1, "a trigger from the page runs the action");
        grid->setEnabled(false);
        check(!bridge.trigger("show-the-grid"), "a disabled action refuses the page's trigger");
        int specs = 0;
        QObject::connect(&bridge, &SettingsBridge::specChanged, [&] { ++specs; });
        SettingsEdits::setTokenColour("accent", QColor(1, 2, 3));
        check(specs == 1, "an edit made anywhere else reaches the page");
    }
```

(The "current option is not re-triggered" rule lives on the page, Task 6, because the page is what knows a chip is current; this bridge only refuses disabled actions, as the registry does.)

- [ ] **Step 2: Run to see it fail** - Expected: compile error, `SettingsBridge.h` not found.

- [ ] **Step 3: Implement.** `layout()` is built once in the constructor from `SettingsEdits::tabs()`, `numberFields()`, `colourTokens()` (skipping `isRetiredToken`), `fontFamilies()`, the rows (each action's id is `ActionRegistry::idFor(action->text())`) and the words. `spec()` reads `Theme::spec()` fresh each call. The constructor connects `Theme::notifier()`'s `changed` to `specChanged`, makes an `ActionWatch` over every row's ids, and forwards its `changed` to `actionsChanged`. `pickColour` moves `AppearancePanel::openColourDialog`'s body into `SettingsEdits::openColourPicker(const QString& id, QWidget* parent)` (one picker at a time, live `currentColorChanged`, `rejected` restores, `show()` never `open()`), and both the Qt panel and the bridge call it; the bridge's parent widget is set with `setPickerParent(QWidget*)` by `EditorChrome`.

- [ ] **Step 4: Run** - `ctest ... -R settings_tests` - Expected: PASS, 25 checks.

- [ ] **Step 5: Commit** - `git commit -m "SettingsBridge: the Settings drawer's state and edits for the page"`

---

### Task 5: EditorChrome - the card's rectangle, the keyboard and the wheel

**Files:**
- Modify: `src/web/EditorChrome.{h,cpp}`, `src/MainWindow.{h,cpp}`, `web/src/editor/api.ts`, `web/src/editor/mock.ts`, `tests/chrome_smoke.cpp`

**Interfaces:**
- Consumes: `HtmlCardSlot`, `ViewportOverlay::replaceWidget` (Task 3), `SettingsBridge` (Task 4).
- Produces: `EditorChrome::addCard(HtmlCardSlot*)`; `ChromeBridge` property `cards` (`{name: {x, y, w, h, shown}}`, NOTIFY `cardsChanged`); slots `claimKeyboard()` / `releaseKeyboard()`; `EditorChrome::registerObject(name, QObject*)`; `bool EditorChrome::holdsKeyboard() const`. MainWindow: in `useHtmlChrome()`, `mySettingsSlot = new HtmlCardSlot("settings", AppearancePanel's fixed size, myView)`, `myOverlay->replaceWidget(myAppearancePanel, mySettingsSlot)`, the visibility lambda shows `settingsSurface()` (the slot in HTML mode, the Qt panel otherwise), and a `SettingsBridge` over `mySession.actions()` and `settingsRows()` is registered as `settings`, its two failure signals wired to the existing two sentences.

- [ ] **Step 1: Write the failing checks** in `tests/chrome_smoke.cpp`, before "the page's words":

```cpp
    // --- the Settings card's place (Phase 3b) ------------------------------------------
    QAction* settingsAction = actionNamed(&window, QStringLiteral("Settings..."));
    check(settingsAction != nullptr, "the editor has Settings...");
    if (settingsAction && !settingsAction->isChecked()) settingsAction->trigger();
    pump(300);
    {
        QWidget* slot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_settings"));
        const QJsonObject card = QJsonDocument::fromJson(
            jsString(page, "JSON.stringify(window.__chromeCards?.settings || {})").toUtf8()).object();
        check(slot && slot->isVisible(), "Settings shows the card's slot");
        check(slot && card.value("x").toInt() == slot->x() && card.value("y").toInt() == slot->y() &&
                  card.value("shown").toBool(),
              "the page is told where the slot stands");
        check(!window.findChild<AppearancePanel*>()->isVisible(), "and the Qt drawer stays hidden");
    }

    // --- typing in a field keeps every key (Review Focus 1) ----------------------------
    {
        const int before = static_cast<int>(window.document().solids().size());
        window.view()->setSelectedSolids({window.document().solids().front().id});
        pump(100);
        js(page, "document.querySelector('[data-tab=\"1\"]').click()");
        pump(100);
        js(page, "document.querySelector('[data-number=\"basePt\"] input').dispatchEvent(new MouseEvent('mousedown', {bubbles: true}))");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        check(chrome->holdsKeyboard(), "pressing a typed field gives the page the keyboard");
        QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
        for (int key : {int(Qt::Key_Delete), int(Qt::Key_Backspace), int(Qt::Key_1)}) {
            QKeyEvent over(QEvent::ShortcutOverride, key, Qt::NoModifier);
            QCoreApplication::sendEvent(proxy, &over);
            check(over.isAccepted(), QStringLiteral("the page claims key %1 before any shortcut").arg(key));
        }
        QKeyEvent undo(QEvent::ShortcutOverride, Qt::Key_Z, Qt::ControlModifier);
        QCoreApplication::sendEvent(proxy, &undo);
        check(undo.isAccepted(), "and Ctrl+Z");
        check(static_cast<int>(window.document().solids().size()) == before, "so nothing was deleted");
        js(page, "document.querySelector('[data-number=\"basePt\"] input').dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return !chrome->holdsKeyboard(); }, 2000);
        check(!chrome->holdsKeyboard() && page->focusPolicy() == Qt::NoFocus,
              "Escape gives the keyboard back");
    }

    // --- a wheel over the card never zooms (Review Focus 2) -----------------------------
    {
        QWidget* slot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_settings"));
        const double distance = window.view()->camera().state().distance;
        const QPoint at = slot->geometry().topLeft() + QPoint(40, 14);   // the title
        QWheelEvent wheel(QPointF(at), QPointF(window.view()->mapToGlobal(at)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window.view()->childAt(at), &wheel);
        pump(150);
        check(std::abs(window.view()->camera().state().distance - distance) < 1e-6, "a wheel over the card leaves the camera");
    }
```

The page sets `window.__chromeCards = cards` whenever it receives them - a test hook, one line in `Editor.svelte`.

- [ ] **Step 2: Run to see it fail** - `cmake --build --preset windows-web --target chrome_smoke && build-web/RelWithDebInfo/chrome_smoke.exe build-web/smoke-t5` - Expected: compile errors on `holdsKeyboard`, then red on the Settings checks.

- [ ] **Step 3: Implement.**
  - `addCard(slot)`: connects `placed()` to a private `syncCards()` that writes `{x, y, w, h, shown: slot->isVisible()}` per slot into `cards` and emits `cardsChanged()` only when it differs.
  - `claimKeyboard()`: records `QApplication::focusWidget()` in a `QPointer<QWidget> myFocusBefore`, sets the view and its focus proxy to `Qt::StrongFocus`, calls `proxy->setFocus(Qt::OtherFocusReason)`, sets `myHoldsKeyboard`. `releaseKeyboard()`: back to `Qt::NoFocus`, `myFocusBefore->setFocus()` when it still exists, clears the flag. Both are idempotent.
  - The keyboard shield: an event filter on the view's focus proxy (re-installed on `loadFinished`, as `refuseFocus()` already is) that, while `myHoldsKeyboard`, accepts every `QEvent::ShortcutOverride` and returns false - the key then goes to the page and never to a window shortcut.
  - The wheel shield: an event filter on the VIEW itself. A wheel the page did not consume is ignored by the proxy and propagates to its parent, the view, on its way to the viewport; the filter returns true for any `QEvent::Wheel` there whose position is inside `myHitRegion`, so it stops at the card. A wheel outside the card never reaches the view at all (the mask sends it to the viewport directly).
  - `releaseKeyboard()` is also called from `setLive(false)` and from MainWindow's visibility lambda whenever the Settings slot hides (Review Focus 4).
- [ ] **Step 4: Run** - Expected: chrome_smoke `PASS`, every check including the 10 new ones.
- [ ] **Step 5: Commit** - `git commit -m "Editor chrome: the Settings card's place, the keyboard and the wheel"`

---

### Task 6: The Settings card in the page

**Files:**
- Create: `web/src/editor/settings/{Settings,ColourList,NumberField,Switch,Segments,FontPicker,ResetButton}.svelte`, `web/src/editor/settings/{api,mock,copy}.ts`, `web/test/settings.test.ts`
- Modify: `web/src/editor/Editor.svelte`, `web/src/editor/main.ts`, `web/src/editor/copy.ts` (`allCopy()` includes the settings words)

**Interfaces:**
- Consumes: `settings` (Task 4), `chrome.cards`, `chrome.claimKeyboard()`, `chrome.releaseKeyboard()`, `chrome.trigger` is NOT used - row clicks call `settings.trigger(id)`.
- Produces: `<Settings settings rect onclose>`; data hooks the harness reads: `[data-tab="0..3"]`, `[data-row="<label>"]`, `[data-number="<id>"]`, `[data-swatch="<token id>"]`, `[data-action="<action id>"]`, `.reset`, `.font-picker`.

- [ ] **Step 1: Write the failing tests** - `web/test/settings.test.ts`, against `mockSettings()` (a mock with the Task 4 shape whose `layout` holds two tabs' worth of rows: the grid toggle, Grid detail, Projection binary, the Sizes in choice, the Autosave stacked choice, three tokens across two groups, two families):

```ts
it('opens on Colours, and a tab swaps the rows', ...)            // [data-tab="0"] current; click 2 -> Grid detail row shown, swatches gone
it('a swatch shows the live colour and asks C++ for the picker', ...) // style background = spec colour; click -> calls pickColour('accent')
it('a group heading opens its run', ...)                          // heading text precedes the first token of the group
it('a toggle mirrors its action and triggers it', ...)            // aria-checked follows actions[].checked; click -> trigger(id)
it('a current choice is not re-triggered', ...)                   // click Millimetres while checked -> no trigger call  (Review Focus 5)
it('a disabled action\'s control triggers nothing', ...)          // enabled:false -> aria-disabled, click -> no call  (Review Focus 5)
it('a binary row triggers only from the side it is not', ...)     // Perspective while unchecked -> no call; Orthographic -> trigger
it('a number field steps, clamps and formats like the spin box', ...) // ▲ on 1.9x gridDensity -> setNumber(2.0); value text "2.0x"; typed 9 -> setNumber clamps to 2
it('typing claims the keyboard and Escape gives it back', ...)    // focus input -> chrome.claimKeyboard; Escape -> releaseKeyboard, value restored
it('the font list previews on highlight and Escape restores', ...)  // open, ArrowDown -> setFontFamily(second); Escape -> setFontFamily(first)
it('reset takes two clicks within 4 s', ...)                       // click -> label armed, no reset; advance 4001 -> label back; click, click -> reset() once
it('hiding the card drops the arm, the list and the keyboard', ...)  // cards.settings.shown=false -> releaseKeyboard, list closed (family restored), arm dropped (Review Focus 4)
it('an edit made elsewhere re-dresses the open card', ...)        // spec changed + specChanged.emit -> swatch and field show the new values (Review Focus 3)
it('the close x triggers Settings', ...)                            // hover card -> x visible; click -> trigger('settings')
it('takes the pointer only on the card', ...)                       // card has data-hit; nothing else in Settings does
it('no word it paints is banned', ...)                              // bannedWordsIn(allCopy() + layout words + row labels) == []
```

Write each `it` in full in the file, following `web/test/editor.test.ts`'s shape (`render`, `vi.useFakeTimers()`, `settle()`), with the exact assertions named in the comment on its line.

- [ ] **Step 2: Run** - `cd web && npx vitest run test/settings.test.ts` - Expected: FAIL, `Settings.svelte` not found.

- [ ] **Step 3: Implement the components** to the Global Constraints' measurements and the Qt painting they port:
  - `Settings.svelte`: absolutely positioned at `rect`, 296 x 380, `background: var(--panel)`, 1 px `var(--border)`, radius 10, padding 12, `data-hit`; title "Settings" (`--font-title`, 600, `--text-muted`); a hover-only 20 x 20 close x (radius 5, 8 px glyph) in the top-right corner calling `settings.trigger('settings')`; the tab bar packs greedily into rows by measured width (`getBoundingClientRect` after mount; one row at the shipped scale) with 3 px gaps, each tab `use:chip`, the current one on `--chip-active` with a 2 px `--accent` rule along its bottom edge, the rest `--text-muted`, hovered `--chip-hover`; the current tab is component state, starting at 0, never persisted; pages are the rows whose `tab` matches, in table order, 6 px apart, top-aligned.
  - `ColourList.svelte`: a scrolling column (scrollbar per the app sheet: `--chip-active` handle, transparent track, no arrows) of group headings (`--font-label`, bold, `--text-muted`) and rows (name left, swatch right, 4 px apart, 6 px right gutter), each swatch a 46 x 18 `use:chip` button, radius 4, `var(--border)` 1 px, background the token's colour from `spec.colours`, title `pickTip` with the name; a click calls `settings.pickColour(id)`. Below the list: Save colours... / Load colours... side by side, then Reset at the right at its own width.
  - `NumberField.svelte`: label left; a field showing `value.toFixed(decimals) + suffix`; two 16 px step buttons (`use:chip`) beside it; a press in the text calls `chrome.claimKeyboard()`, Escape restores the shown value and releases, Enter and blur commit and release; typing commits live whenever the text parses (suffix stripped) as a number, clamped to [min, max]; a wheel over the field steps it (QAbstractSpinBox's own behaviour) and is stopped there.
  - `Switch.svelte` (38 x 20, knob 14, `--accent` when current, `--chip` otherwise, `--chip-hover` hovered, disabled knob `--text-disabled`), `Segments.svelte` (22 tall, radius 6, current `--chip-active` with a 1.6 px `--accent` border, stacked = one per line with the label top-aligned), `ResetButton.svelte` (the two labels, the 4 s arm, its width the wider label's), `FontPicker.svelte` (a button showing the shown family; opening it lists `layout.families` in a popup inside the card, ArrowUp/Down highlight AND preview through `settings.setFontFamily`, Enter or a click keeps it, Escape or the card hiding restores the family it opened over; it claims the keyboard while open).
  - `Editor.svelte`: renders `<Settings>` when `chrome.cards.settings?.shown`, and sets `window.__chromeCards`.
- [ ] **Step 4: Run** - `npx vitest run && npx svelte-check` - Expected: every test passes; 0 errors, 0 warnings.
- [ ] **Step 5: Commit** - `git commit -m "Settings page: the drawer's four tabs in HTML"`

---

### Task 7: The drawer end to end in the real app

**Files:**
- Modify: `tests/chrome_smoke.cpp`

- [ ] **Step 1: Write the checks** after Task 5's, each driving the page through DOM events and reading the app's own state:

```cpp
    // --- the drawer, end to end ---------------------------------------------------------
    js(page, "document.querySelector('[data-tab=\"2\"]').click()");
    pump(150);
    QAction* gridAction = actionNamed(&window, QStringLiteral("Show the grid"));
    const bool gridWas = gridAction && gridAction->isChecked();
    js(page, "document.querySelector('[data-action=\"show-the-grid\"]').click()");
    pump(200);
    check(gridAction && gridAction->isChecked() != gridWas, "the grid switch triggers the View menu's action");
    check(js(page, "document.querySelector('[data-action=\"show-the-grid\"]').getAttribute('aria-checked')").toString() ==
              (gridAction->isChecked() ? "true" : "false"),
          "and the switch reads the action back");
    gridAction->trigger();   // and the menu moves the switch
    pump(200);
    check(js(page, "document.querySelector('[data-action=\"show-the-grid\"]').getAttribute('aria-checked')").toString() ==
              (gridAction->isChecked() ? "true" : "false"),
          "a change made from the menu reaches the switch");
    js(page, "document.querySelector('[data-number=\"gizmoScale\"] .up').click()");
    pump(200);
    check(std::abs(Theme::spec().gizmoScale - 1.1) < 1e-9, "Gizmo size steps by 0.1 into the live spec");
    SettingsEdits::setTokenColour("accent", QColor(200, 40, 90));   // an edit from elsewhere (Review Focus 3)
    pump(200);
    js(page, "document.querySelector('[data-tab=\"0\"]').click()");
    pump(150);
    check(jsString(page, "getComputedStyle(document.querySelector('[data-swatch=\"accent\"]')).backgroundColor") ==
              "rgb(200, 40, 90)",
          "an edit made elsewhere re-dresses the open card");
    js(page, "document.querySelector('.reset').click()");
    pump(100);
    check(Theme::spec().accent == QColor(200, 40, 90), "one click on Reset changes nothing");
    js(page, "document.querySelector('.reset').click()");
    pump(200);
    check(Theme::spec() == Theme::defaultSpec(), "the second click resets");
    settingsAction->trigger();   // hidden mid-arm (Review Focus 4)
    pump(200);
    check(!window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_settings"))->isVisible() &&
              !js(page, "!!document.querySelector('.settings-card')").toBool(),
          "Settings off takes the card away");
#ifdef _WIN32
    for (int tab = 0; tab < 4; ++tab) {
        settingsAction->trigger();
        pump(200);
        js(page, QStringLiteral("document.querySelector('[data-tab=\"%1\"]').click()").arg(tab));
        pump(300);
        capture(&window, outDir + QStringLiteral("/settings-tab-%1.png").arg(tab));
        settingsAction->trigger();
        pump(150);
    }
#endif
```

- [ ] **Step 2: Run them red** against Task 5's tree with Task 6's page reverted (`git stash` the `web/` changes, rebuild `web_pages`) - Expected: the drawer checks FAIL, naming each. Restore with `git stash pop`.
- [ ] **Step 3: Run green** - `cmake --build --preset windows-web --target chrome_smoke web_pages && build-web/RelWithDebInfo/chrome_smoke.exe build-web/smoke-t7` - Expected: PASS. Then look at the four `settings-tab-*.png` captures beside a `gui_smoke` capture of the Qt drawer (`appearance` block) and fix what does not match: a wrong size, a clipped label, a row out of order, a colour that is not the token.
- [ ] **Step 4: Commit** - `git commit -m "chrome_smoke: the Settings drawer end to end, with captures"`

---

### Task 8: Gate, documentation, board

- [ ] **Step 1:** `cmake --build --preset windows-web`, `ctest --test-dir build-web -C RelWithDebInfo` (Expected: 19/19, `settings_tests` new), `cd web && npx vitest run && npx svelte-check`, `chrome_smoke`, `hub_smoke` - every one green.
- [ ] **Step 2:** Two unfiltered `gui_smoke` runs. Expected: `PASS (0 failures, 6370 checks, 1 skipped by the environment, floor 6324)` both times. A difference is a failure of this phase - find it.
- [ ] **Step 3:** CLAUDE.md: the architecture rows (`SettingsEdits`, `ActionWatch`, `HtmlCardSlot`, `SettingsBridge`), and in "The HTML UI" - the slot pattern (R1) as the way every later card is placed, and the keyboard claim (R5).
- [ ] **Step 4:** Fill this plan's gate table; board card for Phase 3b to done with notes; commit `"Phase 3b: gate measured; documented"`.

## Gate record

| When | Checks | Skips | Failures | Run |
| --- | --- | --- | --- | --- |
| d5e3399 (Tasks 1-7), quiet, run 1 | 6370 | 1 | 0 | d5e3399 |
| d5e3399, quiet, run 2 | 6370 | 1 | 0 | d5e3399 |
| 861e604 (review fixes), quiet, run 1 | 6370 | 1 | 0 | 861e604 |
| 861e604, run 2 | 6370 | 1 | 1 (Items drawer selection-restyle wall clock 16.01 ms against 16; 11.1-12.9 in every other run) | 861e604 |
| 861e604, quiet, run 3 | 6370 | 1 | 0 (that check 11.14 ms) | 861e604 |

Also on 861e604: ctest 19/19, vitest 89, svelte-check clean, hub_smoke 29, chrome_smoke 78.
