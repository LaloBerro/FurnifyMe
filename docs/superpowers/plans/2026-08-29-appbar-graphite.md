# Phase 5: Graphite × App Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the shell into an app bar + icon rail + floating drawer, wearing the Graphite polish — same commands, same words, same viewport hues.

**Architecture:** `AppBar` replaces the menu strip via `QMainWindow::setMenuWidget` while keeping the real `QMenuBar` inside it; the four floating chip clusters collapse into one icon rail; `ItemsPanel` floats as a drawer; `Theme` gains the Graphite chip anatomy and a shared floating-surface paint helper.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1, CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-08-29-appbar-graphite-design.md`
**Visual contract:** the mockup artifact (round two, option H; round one, option A) — final result verified against it by side-by-side capture.

## A note on this plan's form

As in Phases 3 and 4: this plan fixes **interfaces, behaviour contracts, exact token
values and test intent** — the things that must not drift between tasks — and does not
transcribe widget bodies. The repo's own widgets are the prior art; each task names what
to read first.

## Global Constraints

- **Vocabulary is settled.** Banned case-insensitively in any user-visible string:
  `Fuse`, `Solid`, `OCCT`, `mm3`, `(s)`, `Merge`, `Join`. Bodies are **Body**; operations
  **Union / Subtract / Intersect**; the 2D shape an **outline**; a closed outline a
  **face**; the 3D area the **viewport**. Copy does not change in this phase — controls
  move, words stay.
- **The shell is action-driven.** Every control mirrors a `QAction` — enabled, checked,
  text, shortcut — and never stores its own state. `updateActions()` remains the single
  place that decides what is available.
- **Widgets over the viewport:** interactive controls are real hit-test targets —
  verify with `view->childAt(point)` against the actual pointer, never attribute flags.
  Siblings carry `Qt::WA_NoMousePropagation`; visibility is **derived**, never left to a
  hide event Qt may not deliver. Placement is driven by `ViewportOverlay::laidOut()`.
- `MainWindow::appStateChanged()` fires at the end of `updateActions()`; no slot on it
  may call back into `updateActions()`.
- `furnify_geometry` links no Qt and is untouched this phase.
- Checks that assert the old arrangement are **updated, never deleted**; the suite must
  not lose coverage. C++17, no new dependencies, no qtsvg, no Qt Test.
- Never drive the GUI with OS-level synthetic input.
- Build: `cmake --build --preset windows`; run `.\build\RelWithDebInfo\gui_smoke.exe <dir>`
  directly (GPU needed, not in ctest); headless via `--preset windows-headless`, 5/5.
  Kill `furnifyme.exe`/`gui_smoke.exe` before building.
- **Every task ends with a `PrintWindow` capture** (`PW_RENDERFULLCONTENT`, capture only,
  never inject input) compared against the mockup at crop level, because the suite
  asserts behaviour, not appearance, and every visual bug this project has shipped was
  caught in a magnified crop.

---

### Task 1: Graphite tokens, chip anatomy, and the floating-surface family

**Files:**
- Modify: `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/ui/ToolChip.h`, `src/ui/ToolChip.cpp`,
  `src/GridRenderer.cpp` (colour use only, via Theme), plus the paint code of
  `WalkthroughPanel`, `HintBalloon`, `Toast`, `ShortcutSheet` where they draw their card
  background today
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
namespace Theme {
// Exact new values; everything not listed keeps its current value.
QColor chip();        // #2c2c31   (was #2b2b2e)
QColor gridMinor();   // #3e3e44   (was #3a3a40)
QColor gridMajor();   // #4d4d55   (was #4a4a52)

// The one implementation of the floating-surface family: fills `rect` with
// panel(), strokes a 1px border(), rounds to `radius` (default 8), and paints
// the soft shadow ring inside the widget's own bounds. Every floating card
// calls this; none paints its own variant.
void paintSurface(QPainter& p, const QRect& rect, int radius = 8);

// How many pixels of margin a widget must reserve around its content for the
// painted shadow paintSurface() draws. Callers size themselves with it.
int surfaceShadowMargin();   // 3
}
```

**Behaviour contract:**
- `ToolChip` paints: 1px `border()` always; `chipHover()` on hover; checked =
  `chipActive()` fill **plus a 1px inset `accent()` ring**; disabled dims glyph, label
  and shortcut badge to `textDisabled()` together. The focus ring behaviour from Phase 3
  is unchanged and paints on top.
- The painted shadow lives inside an enlarged rect: chips and cards grow by
  `surfaceShadowMargin()` per side and paint body inset. Hit-testing the margin is
  acceptable; a click there activates the chip.
- `WalkthroughPanel`, `HintBalloon`, `Toast`, `ShortcutSheet` replace their hand-rolled
  card backgrounds with `paintSurface()`. Their content painting is untouched.
- Grid: only the two grey tokens move; `GridRenderer` logic untouched.

- [ ] **Step 1 — failing test.** Add to `gui_smoke`: grab-compare a chip normal vs
  hovered (deliver a real `QEvent::Enter`/`HoverEnter`), enabled vs disabled, and
  checked vs unchecked — three distinct-image assertions using the focus-ring pattern;
  plus `Theme::surfaceShadowMargin() == 3` and exact new token values, so a drive-by
  "cleanup" of the palette fails loudly.
- [ ] **Step 2 — RED.**
- [ ] **Step 3 — implement** per contract. Read `ToolChip.cpp` and `Toast.cpp` first.
- [ ] **Step 4 — GREEN**, full suite + headless 5/5.
- [ ] **Step 5 — capture** and compare chips at crop level against mockup A's chips.
- [ ] **Step 6 — commit.**

---

### Task 2: The app bar

**Files:**
- Create: `src/ui/AppBar.h`, `src/ui/AppBar.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.h`, `src/MainWindow.cpp`,
  `src/ui/AxisGizmo.h`, `src/ui/AxisGizmo.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
class AppBar : public QWidget {
public:
    // Owns nothing it shows: the menu bar is the window's real QMenuBar,
    // reparented in; every button mirrors a QAction from MainWindow.
    AppBar(QMenuBar* menuBar,
           QAction* wireframe, QAction* fitAll,
           QWidget* parent = nullptr);

    void setViewLabel(const QString& text);   // "Persp", "Top", ...
    void setUnitLabel(const QString& text);   // "mm" / "cm"
    QWidget* viewLabelButton() const;         // for the suite's hit tests
    QWidget* unitButton() const;
    QStringList paintedTexts() const;         // wordmark + button copy, for the sweep

signals:
    void viewLabelClicked();                  // -> MainWindow snaps to Axonometric
    void unitClicked();                       // -> MainWindow cycles the unit
};
```

**Behaviour contract:**
- Installed with `setMenuWidget(appBar)`. The `QMenuBar` inside stays the window's
  `menuBar()` — menus, shortcuts, `ShortcutSheet`'s enumeration and the vocabulary sweep
  keep working with **zero changes** to any of them. Verify by asserting the sheet's row
  count is unchanged.
- **One source for the view label.** The label-text logic currently in
  `AxisGizmo::labelText()` moves to a shared home (`OcctViewWidget::viewLabelText()` is
  the natural one, next to the camera it reads); the bar button and any other consumer
  read it from there. `AxisGizmo` stops painting its label chip and shrinks to the axes;
  its `viewSnapped()` wiring and tip-click behaviour are untouched.
- Clicking the view label button = the exact behaviour the gizmo's label chip had:
  animate to Axonometric and `recordViewChanged()`. The button's text updates on
  `cameraChanged` — throttle-free is fine, it is one string compare.
- Clicking the unit chip triggers the *other* unit's existing `QAction` (the pair from
  Phase 4's `View → Units`), so persistence, refresh and the `(cm)` field label all
  happen through the one existing path.
- The old viewport unit chip and the gizmo label chip are removed; the right-center
  cluster loses Wireframe and Fit All to the bar, and `Save Screenshot` becomes
  menu-only. The right-center cluster is then empty and is removed entirely (the rail
  arrives in Task 3; between the two tasks the commands stay reachable via menus, which
  is acceptable mid-branch but must be called out in the task report).
- Wordmark: `▰ FurnifyMe`, glyph in `accent()`. It is painted copy — expose it through
  `paintedTexts()` and sweep it.

- [ ] **Step 1 — failing test:** the window's menu widget is an `AppBar`; the real
  `QMenuBar` is inside it and `ShortcutSheet` still lists the same row count; the view
  label reads `Persp` after start, changes to `Top` after the Top action, and clicking
  it (real `childAt` + click) snaps back to a pose the label calls `Persp` **and**
  increments `view.changed`; the unit button cycles mm→cm→mm with the items panel
  following; the gizmo no longer has a label chip (its `labelCenter()` accessor and the
  checks that clicked it are updated to the bar button).
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture**, compare the bar against mockup H's bar.
- [ ] **Step 6 — commit.**

---

### Task 3: The rail

**Files:**
- Modify: `src/ui/ToolChip.h`, `src/ui/ToolChip.cpp`, `src/ui/ToolCluster.h`,
  `src/ui/ToolCluster.cpp`, `src/ui/ViewportOverlay.h`, `src/ui/ViewportOverlay.cpp`,
  `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
// ToolChip gains a display mode; everything else about it is unchanged.
enum class ChipMode { Labelled, IconOnly };   // IconOnly: 34x34, glyph only,
                                              // label+shortcut in the tooltip
// ToolCluster gains separators:
void addSeparator();
// ViewportOverlay gains the anchor the rail hangs on:
enum class Anchor { ..., LeftEdge };          // pinned top-to-bottom, 14px margins
```

**Behaviour contract:**
- One rail, one `ToolCluster` in `IconOnly` mode at `LeftEdge`, containing in order:
  Items toggle · sep · Start Sketch · Extrude · sep · Union · Subtract · Intersect ·
  Delete Selected · sep · Snap to Grid · Select Bodies · Select Faces · Select Edges ·
  stretch · Undo · Redo. All existing `QAction`s; no new actions.
- Tooltips must show **label + shortcut** — they already do via the actions; assert one.
- The four old clusters are removed. `occupiedRects()` reporting the rail comes free
  from it being an overlay entry; assert the toast steps around it at a narrow width.
- Every rail button reachable via `view->childAt()`; checked/disabled render per Task 1.
- The `IconOnly` glyphs come from `IconSet` — no emoji, no text glyphs.

- [ ] **Step 1 — failing test:** rail exists with the exact button order (asserted by
  walking its chips' actions); every button `childAt`-reachable; a disabled action's
  button paints differently (grab-compare); the old clusters are gone (no labelled chip
  floats over the viewport any more); toast avoidance at 800px width still holds.
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture** vs mockup H's rail. — [ ] **Step 6 — commit.**

---

### Task 4: The items drawer

**Files:**
- Modify: `src/ui/ItemsPanel.h`, `src/ui/ItemsPanel.cpp`, `src/MainWindow.cpp`,
  `src/ui/ViewportOverlay.cpp` (only if a new anchor offset is needed)
- Test: `tests/gui_smoke.cpp`

**Behaviour contract:**
- `ItemsPanel` is no longer a dock: it becomes a floating card (Task 1's
  `paintSurface()` family), anchored top-left beside the rail through `ViewportOverlay`,
  toggled by the **existing** Items action and `Ctrl+Alt+S` — no new action, no new
  state; visibility mirrors the action's checked state, derived, both directions.
- Content unchanged: rows, selection accent bar, dims line, visibility eyes,
  `rowTextAt()`. Every existing ItemsPanel check keeps passing with only geometry
  updates.
- The viewport is full-bleed: the central widget is the viewport alone.
- The drawer is in `occupiedRects()`; guide/balloon/toast step around it for free —
  assert the toast does at a narrow width with the drawer open.

- [ ] **Step 1 — failing test:** no dock widget remains; the drawer floats over the
  viewport (`childAt` on a row hits the panel); Items action toggles it both ways and
  the chip/menu stay in sync; `rowTextAt(0)` still carries the dims; toast steps around
  an open drawer.
- [ ] **Step 2 — RED.** — [ ] **Step 3 — implement.** — [ ] **Step 4 — GREEN.**
- [ ] **Step 5 — capture** vs mockup H's drawer. — [ ] **Step 6 — commit.**

---

### Task 5: Document and verify against the mockup

**Files:** `CLAUDE.md`, the spec, plus the final side-by-side capture.

- [ ] **Step 1:** Update `CLAUDE.md`: the shell composition (app bar via
  `setMenuWidget` with the real `QMenuBar` inside; rail; drawer), the
  one-paint-helper rule for floating surfaces, the shadow-inside-bounds constraint, and
  anything a task report surfaced that the next phase must know. Update the
  architecture table.
- [ ] **Step 2:** Mark the spec implemented.
- [ ] **Step 3:** Build, both suites green, then `PrintWindow` the running app and
  produce the side-by-side against mockup H (bar crop, rail crop, drawer crop, full
  shell). This is the phase's acceptance gate — present it, do not just file it.
- [ ] **Step 4: Commit.**
