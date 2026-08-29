# Help and Learnability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach the modelling loop by having the user do it, teach each further capability the moment it first becomes available, and then fall permanently silent once they have used it three times.

**Architecture:** A Qt-free `UserProgress` counter in `furnify_geometry` decides what has been learned; `MainWindow` persists it through `QSettings` and emits one `appStateChanged()` signal from `updateActions()`. A walkthrough panel and a hint manager both derive everything they show from that signal plus live app state, so neither stores a cursor that can drift.

**Tech Stack:** C++17, Qt 6.11.1 Widgets (qtbase only), OpenCascade 8.0.1, CMake presets + vcpkg, MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-08-28-help-and-learnability-design.md`

## Global Constraints

- **`UserProgress` must be Qt-free** — it joins `furnify_geometry`, which links no Qt. `std::string`, `std::map` only. No `QSettings`; persistence is the app layer's job.
- `Measure`, `ModelingOps`, `DocumentModel`, `SketchController`, `CameraController` stay Qt-free.
- Qt 6 Widgets, qtbase only. No `qtsvg`, no Qt Test module, no new dependency, never install anything.
- No `Debug` configuration; build `RelWithDebInfo` via presets. cmake/ctest at `C:/Program Files/CMake/bin/`.
- **Kill running app processes before every build** (`Get-Process furnifyme,gui_smoke -ErrorAction SilentlyContinue | Stop-Process -Force`) or the link fails with `LNK1168`.
- **No OS-level input simulation** in code or verification. Verify with `gui_smoke` and still screenshots.
- Never `delete` an OCCT `Handle()`.
- **`gui_smoke` must never read or write the real `QSettings`.** It constructs `MainWindow(nullptr, /*persistProgress=*/false)`. A suite that touched stored progress would pass or fail based on how often the developer had run the app.
- **Banned from every user-visible string, case-insensitively:** `OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, `Join`. A `gui_smoke` sweep enforces this over action texts and widget tooltips — every string this phase adds must obey it.
- **Vocabulary:** outline → face → body. Operations are Union, Subtract, Intersect. Status text takes no trailing period and uses an em dash `—` as a clause separator; dialog bodies are full sentences; tooltips are a fragment plus optionally one teaching sentence.
- **Phase 2 changes no wording settled in Phase 1** and no modelling, geometry or camera behaviour.
- `UserProgress::kLearnedThreshold` is **3**.
- The GUI suite currently prints `PASS (0 failures)` with **100 checks**; the headless suite is 4 executables. Trust each run's own printout.

**Two amendments to the spec, decided from the code:**

1. **The walkthrough panel anchors `BottomRight`, not `TopRight`.** `TopRight` already holds the axis gizmo and the `mm` readout, so a third widget there would sit far down the right edge. `BottomRight` does not exist yet, so Task 4 adds it to `ViewportOverlay::Anchor` — a small, clean extension that also completes an obviously incomplete enum.
2. **One new signal, `appStateChanged()`, emitted at the end of `updateActions()`.** The spec says the walkthrough and hints derive their state rather than storing a cursor; `updateActions()` already runs after every transition that matters (sketch start, each point, finish, extrude, boolean, delete, undo, redo, selection change), so a single signal there covers all of them. Slots must only read state and repaint themselves — none may call back into `updateActions()`, which would recurse.

---

### Task 1: `UserProgress`

**Files:**
- Create: `src/UserProgress.h`, `src/UserProgress.cpp`, `tests/user_progress.cpp`
- Modify: `CMakeLists.txt` (add to `furnify_geometry`; register a `headless_progress` ctest target)

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
class UserProgress {
public:
    static constexpr int kLearnedThreshold = 3;
    void record(const std::string& event);
    int  count(const std::string& event) const;
    bool hasLearned(const std::string& event) const;
    void reset();
    std::string serialize() const;
    void deserialize(const std::string& text);
};
```

- [ ] **Step 1: Write the failing tests**

Create `tests/user_progress.cpp`:

```cpp
//
// Headless tests for the learning counter. This is the machinery that makes
// "hints recede" a checkable promise rather than a decorative claim, so it is
// Qt-free and tested without a window like the rest of furnify_geometry.
//
#include "UserProgress.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkEq(const std::string& actual, const std::string& expected, const std::string& what)
{
    const bool ok = actual == expected;
    std::printf("%-6s %s (got \"%s\", expected \"%s\")\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual.c_str(), expected.c_str());
    if (!ok) ++g_failures;
}
}  // namespace

int main()
{
    // --- counting -------------------------------------------------------------
    {
        UserProgress p;
        check(p.count("boolean.completed") == 0, "an unseen event counts zero");
        check(!p.hasLearned("boolean.completed"), "an unseen event is not learned");

        p.record("boolean.completed");
        p.record("boolean.completed");
        check(p.count("boolean.completed") == 2, "two records count two");
        check(!p.hasLearned("boolean.completed"), "two is not yet learned");

        p.record("boolean.completed");
        check(p.count("boolean.completed") == 3, "three records count three");
        check(p.hasLearned("boolean.completed"), "three crosses the threshold");

        p.record("boolean.completed");
        check(p.hasLearned("boolean.completed"), "past the threshold stays learned");
        check(!p.hasLearned("extrude.completed"), "learning one event teaches nothing else");
    }

    // --- reset ----------------------------------------------------------------
    {
        UserProgress p;
        p.record("a"); p.record("a"); p.record("a"); p.record("b");
        p.reset();
        check(p.count("a") == 0 && p.count("b") == 0, "reset clears every counter");
        check(!p.hasLearned("a"), "nothing is learned after a reset");
        checkEq(p.serialize(), "", "a reset store serializes to nothing");
    }

    // --- serialize round trip -------------------------------------------------
    {
        UserProgress p;
        checkEq(p.serialize(), "", "an empty store serializes to an empty string");

        p.record("extrude.completed");
        p.record("boolean.completed");
        p.record("boolean.completed");
        // Ordering must be stable, or the stored value churns between runs even
        // when nothing was learned.
        checkEq(p.serialize(), "boolean.completed=2;extrude.completed=1",
                "events serialize in sorted order");

        UserProgress restored;
        restored.deserialize(p.serialize());
        check(restored.count("boolean.completed") == 2 &&
              restored.count("extrude.completed") == 1,
              "deserialize restores every counter");
        checkEq(restored.serialize(), p.serialize(), "a round trip is byte-identical");
    }

    // --- malformed input ------------------------------------------------------
    {
        // Stored settings can be corrupted, hand-edited, or written by an older
        // build. None of that may crash the app or leave an unusable object.
        for (const std::string& junk : {std::string("garbage"), std::string("a=b"),
                                        std::string("a="), std::string("=1"),
                                        std::string(";;;"), std::string("a=1;;b=2;"),
                                        std::string("a=-4")}) {
            UserProgress p;
            p.deserialize(junk);
            p.record("sane.event");
            if (p.count("sane.event") != 1) {
                check(false, "a store stayed usable after malformed input: " + junk);
                break;
            }
        }
        check(true, "malformed input never leaves the store unusable");

        UserProgress negative;
        negative.deserialize("a=-4");
        check(negative.count("a") >= 0, "a negative stored count never survives as negative");
    }

    // --- deserialize replaces rather than merges -------------------------------
    {
        UserProgress p;
        p.record("old.event");
        p.deserialize("new.event=1");
        check(p.count("old.event") == 0, "deserialize replaces the previous contents");
        check(p.count("new.event") == 1, "deserialize installs the new contents");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Wire into CMake and verify RED**

In `CMakeLists.txt`, add `src/UserProgress.cpp` to the `furnify_geometry` source list, and beside the other test targets:

```cmake
add_executable(headless_progress tests/user_progress.cpp)
target_link_libraries(headless_progress PRIVATE furnify_geometry)
add_test(NAME headless_progress COMMAND headless_progress)
```

Run: `cmake --preset windows-headless` then `cmake --build --preset windows-headless`
Expected: FAIL to compile — cannot open include file `UserProgress.h`.

- [ ] **Step 3: Write the header**

Create `src/UserProgress.h`:

```cpp
#pragma once
//
// Counts what the user has actually done, so hints can fall silent once they
// have served their purpose. Qt-free and free of any storage concern: the app
// layer persists serialize() and hands deserialize() back at startup, which is
// what lets the test suite run with a store the developer's own usage cannot
// contaminate.
//
#include <map>
#include <string>

class UserProgress {
public:
    // Three completions of an action is the point at which its hint stops.
    static constexpr int kLearnedThreshold = 3;

    void record(const std::string& event);
    int count(const std::string& event) const;
    bool hasLearned(const std::string& event) const;
    void reset();

    // "boolean.completed=2;extrude.completed=1" - sorted, so a round trip is
    // byte-identical and the stored value does not churn between runs.
    std::string serialize() const;

    // Replaces the contents wholesale. Malformed input is discarded pair by
    // pair rather than throwing: stored settings can be corrupted or written by
    // an older build, and neither may stop the app from starting.
    void deserialize(const std::string& text);

private:
    std::map<std::string, int> myCounts;   // std::map iterates sorted, which is why
};                                          // serialize() is stable for free
```

- [ ] **Step 4: Write the implementation**

Create `src/UserProgress.cpp`:

```cpp
#include "UserProgress.h"

#include <cstdlib>

void UserProgress::record(const std::string& event)
{
    if (event.empty()) return;
    ++myCounts[event];
}

int UserProgress::count(const std::string& event) const
{
    const auto it = myCounts.find(event);
    return it == myCounts.end() ? 0 : it->second;
}

bool UserProgress::hasLearned(const std::string& event) const
{
    return count(event) >= kLearnedThreshold;
}

void UserProgress::reset()
{
    myCounts.clear();
}

std::string UserProgress::serialize() const
{
    std::string out;
    for (const auto& entry : myCounts) {
        if (!out.empty()) out.push_back(';');
        out += entry.first;
        out.push_back('=');
        out += std::to_string(entry.second);
    }
    return out;
}

void UserProgress::deserialize(const std::string& text)
{
    myCounts.clear();

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string pair =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);

        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos && equals > 0) {
            const std::string key = pair.substr(0, equals);
            const std::string value = pair.substr(equals + 1);
            // Accept only a well-formed non-negative integer; anything else is a
            // corrupted or foreign entry and is dropped rather than guessed at.
            bool digits = !value.empty();
            for (char c : value) {
                if (c < '0' || c > '9') { digits = false; break; }
            }
            if (digits) myCounts[key] = std::atoi(value.c_str());
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }
}
```

- [ ] **Step 5: Verify GREEN**

Run: `cmake --build --preset windows-headless` then `ctest --preset windows-headless`
Expected: 5/5 tests pass (the four existing plus `headless_progress`).

- [ ] **Step 6: Commit**

```bash
git add src/UserProgress.h src/UserProgress.cpp tests/user_progress.cpp CMakeLists.txt
git commit -m "Add the UserProgress learning counter"
```

---

### Task 2: Persist progress, record events, and add the Help menu

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`, `src/main.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `UserProgress` (Task 1).
- Produces, on `MainWindow`:
  - `explicit MainWindow(QWidget* parent = nullptr, bool persistProgress = true);`
  - `UserProgress& progress();` and `const UserProgress& progress() const;`
  - `void recordProgress(const std::string& event);` — records and persists
  - `signals: void appStateChanged();` — emitted at the end of `updateActions()`
  - a `Help` menu with `Keyboard Shortcuts` (shortcut `?`) and `Show tips again`

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, change the window construction so the suite never touches stored settings:

```cpp
    // Never persist: a suite whose behaviour depends on how often the developer
    // ran the real app is not a suite.
    MainWindow window(nullptr, /*persistProgress=*/false);
```

Then insert before the final `std::printf`:

```cpp
    // --- progress is recorded from real actions -------------------------------
    {
        // The suite has by now completed sketches, extrudes and a boolean, so
        // those events must have been counted.
        check(window.progress().count("extrude.completed") >= 2,
              QStringLiteral("extrudes were recorded (%1)")
                  .arg(window.progress().count("extrude.completed")));
        check(window.progress().count("boolean.completed") >= 1,
              "the boolean was recorded");
        check(window.progress().count("sketch.completed") >= 2,
              "closing an outline was recorded");

        const int before = window.progress().count("undo.used");
        trigger(window, QStringLiteral("Undo"));
        settle(150);
        check(window.progress().count("undo.used") == before + 1,
              "undo records exactly once");
        trigger(window, QStringLiteral("Redo"));
        settle(150);
    }

    // --- the Help menu ---------------------------------------------------------
    {
        check(action(window, QStringLiteral("Keyboard Shortcuts")) != nullptr,
              "a Keyboard Shortcuts action exists");
        QAction* reset = action(window, QStringLiteral("Show tips again"));
        check(reset != nullptr, "a Show tips again action exists");
        if (reset) {
            reset->trigger();
            settle(100);
            check(window.progress().count("extrude.completed") == 0,
                  "Show tips again clears the progress store");
        }
    }
```

- [ ] **Step 2: Verify RED**

Kill app processes, `cmake --build --preset windows`.
Expected: FAIL to compile — `MainWindow` has no constructor taking two arguments, and no `progress()`.

- [ ] **Step 3: Add the member, accessors and signal**

In `src/MainWindow.h`: add `#include "UserProgress.h"`, change the constructor, and add the members.

```cpp
    explicit MainWindow(QWidget* parent = nullptr, bool persistProgress = true);

    UserProgress& progress() { return myProgress; }
    const UserProgress& progress() const { return myProgress; }

    // Records an event and writes the store through immediately, so a crash
    // never costs the user their learning history.
    void recordProgress(const std::string& event);
```

In the signals section, beside `documentChanged()`:

```cpp
    // Emitted after every change that affects what the user can do next.
    // Slots must only read state and update themselves - calling back into
    // updateActions() from here would recurse.
    void appStateChanged();
```

In the private members:

```cpp
    UserProgress myProgress;
    bool myPersistProgress = true;
```

- [ ] **Step 4: Load, save and emit**

In `src/MainWindow.cpp`, add `#include <QSettings>`, and change the constructor's opening:

```cpp
MainWindow::MainWindow(QWidget* parent, bool persistProgress)
    : QMainWindow(parent)
    , myPersistProgress(persistProgress)
{
    if (myPersistProgress) {
        const QSettings settings;
        myProgress.deserialize(
            settings.value(QStringLiteral("progress")).toString().toStdString());
    }
```

Add the recorder:

```cpp
void MainWindow::recordProgress(const std::string& event)
{
    myProgress.record(event);
    if (!myPersistProgress) return;

    QSettings settings;
    settings.setValue(QStringLiteral("progress"),
                      QString::fromStdString(myProgress.serialize()));
}
```

At the very end of `updateActions()`, after `updateStateLabel();`:

```cpp
    emit appStateChanged();
```

In `src/main.cpp`, before `MainWindow window;`, give `QSettings` somewhere to write:

```cpp
    QApplication::setOrganizationName(QStringLiteral("FurnifyMe"));
```

- [ ] **Step 5: Record the seven events**

In `src/MainWindow.cpp`, add `recordProgress` calls at exactly these sites:

```cpp
    // onFinishSketch, immediately after myPendingFace = face;
    recordProgress("sketch.completed");

    // extrudePendingFace, immediately after the successful addSolid
    recordProgress("extrude.completed");

    // applyBooleanToSelection, immediately after the successful addSolid
    recordProgress("boolean.completed");

    // onDeleteSelected, after the removal loop
    recordProgress("delete.used");

    // onUndo and onRedo, each after the successful undo()/redo()
    recordProgress("undo.used");

    // onSelectionModeChanged, only when face mode is the one being turned on
    if (myFaceSelectAction->isChecked()) recordProgress("faceMode.used");
```

For `view.changed`, add the call inside each of the four standard-view slots
(`setViewAxonometric`, `setViewTop`, `setViewFront`, `setViewRight` are on the widget, not
the window) — instead put it in `MainWindow` where those actions are triggered. The View
menu builds them with lambdas calling `myView`; wrap each so it also records:

```cpp
    viewMenu->addAction(tr("&Axonometric"), QKeySequence(Qt::Key_0), this, [this] {
        myView->setViewAxonometric();
        recordProgress("view.changed");
    });
    viewMenu->addAction(tr("&Top"), QKeySequence(Qt::Key_1), this, [this] {
        myView->setViewTop();
        recordProgress("view.changed");
    });
    viewMenu->addAction(tr("F&ront"), QKeySequence(Qt::Key_2), this, [this] {
        myView->setViewFront();
        recordProgress("view.changed");
    });
    viewMenu->addAction(tr("&Right"), QKeySequence(Qt::Key_3), this, [this] {
        myView->setViewRight();
        recordProgress("view.changed");
    });
```

- [ ] **Step 6: Add the Help menu**

In `buildMenus()`, after the View menu:

```cpp
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));

    myShortcutsAction = new QAction(tr("Keyboard Shortcuts"), this);
    myShortcutsAction->setShortcut(QKeySequence(Qt::Key_Question));
    myShortcutsAction->setToolTip(tr("List every keyboard shortcut (?)"));
    helpMenu->addAction(myShortcutsAction);

    helpMenu->addAction(tr("Show tips again"), this, [this] {
        myProgress.reset();
        if (myPersistProgress) {
            QSettings settings;
            settings.setValue(QStringLiteral("progress"), QString());
        }
        statusBar()->showMessage(tr("Tips reset — the guide and hints will appear again"));
        emit appStateChanged();
    });
```

Declare `QAction* myShortcutsAction = nullptr;` among the members. Task 3 connects it; for
now it exists but does nothing, which the test only checks for existence.

- [ ] **Step 7: Verify GREEN**

Kill processes, build, run `.\build\RelWithDebInfo\gui_smoke.exe .` and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 5/5.

- [ ] **Step 8: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp src/main.cpp tests/gui_smoke.cpp
git commit -m "Persist learning progress and add the Help menu"
```

---

### Task 3: The generated shortcut sheet

**Files:**
- Create: `src/ui/ShortcutSheet.h`, `src/ui/ShortcutSheet.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `myShortcutsAction` (Task 2).
- Produces: `class ShortcutSheet : public QWidget` with
  `explicit ShortcutSheet(QWidget* parent)`, `void showSheet()`, and
  `int rowCount() const`. Rows are generated from the window's `QAction`s.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add `#include "ShortcutSheet.h"` and insert before the final `std::printf`:

```cpp
    // --- the shortcut sheet lists every real binding --------------------------
    {
        QAction* open = action(window, QStringLiteral("Keyboard Shortcuts"));
        check(open != nullptr, "the shortcut sheet has an action to open it");
        if (open) {
            open->trigger();
            settle(150);
            ShortcutSheet* sheet = window.findChild<ShortcutSheet*>();
            check(sheet != nullptr && sheet->isVisible(), "triggering it shows the sheet");

            // Generated, not hand written: every action carrying a shortcut must
            // appear, so the sheet cannot go stale when a binding is added.
            int expected = 0;
            for (QAction* candidate : window.findChildren<QAction*>()) {
                if (!candidate->shortcut().isEmpty()) ++expected;
            }
            check(sheet != nullptr && sheet->rowCount() == expected,
                  QStringLiteral("the sheet lists all %1 bound actions (got %2)")
                      .arg(expected)
                      .arg(sheet ? sheet->rowCount() : -1));
            check(expected > 5, "there are enough bound actions for this to mean something");

            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &escape);
            settle(150);
            check(sheet != nullptr && !sheet->isVisible(), "Escape closes the sheet");
        }
    }
```

Add `#include <QKeyEvent>` to the test file if it is not already present.

- [ ] **Step 2: Verify RED**

Kill processes, build.
Expected: FAIL to compile — cannot open include file `ShortcutSheet.h`.

- [ ] **Step 3: Write the header**

Create `src/ui/ShortcutSheet.h`:

```cpp
#pragma once
//
// A centred overlay listing every keyboard shortcut. Its rows are generated
// from the window's own QActions rather than written by hand, so it cannot go
// stale the moment somebody adds a binding - the same principle as the
// vocabulary test: make the documentation executable.
//
#include <QWidget>

#include <vector>

class QAction;

class ShortcutSheet : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutSheet(QWidget* parent);

    // Rebuilds from the parent window's actions, then shows and centres itself.
    void showSheet();

    int rowCount() const { return static_cast<int>(myRows.size()); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Row {
        QString label;
        QString keys;
    };

    void rebuild();

    std::vector<Row> myRows;
};
```

- [ ] **Step 4: Write the implementation**

Create `src/ui/ShortcutSheet.cpp`:

```cpp
#include "ShortcutSheet.h"

#include "Theme.h"

#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>

namespace {
constexpr int kPadding = 24;
constexpr int kRowHeight = 26;
constexpr int kTitleHeight = 44;
constexpr int kColumnGap = 48;
}  // namespace

ShortcutSheet::ShortcutSheet(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void ShortcutSheet::rebuild()
{
    myRows.clear();
    if (!parentWidget()) return;

    for (QAction* candidate : parentWidget()->findChildren<QAction*>()) {
        if (candidate->shortcut().isEmpty()) continue;
        Row row;
        row.label = candidate->text().remove(QLatin1Char('&'));
        row.keys = candidate->shortcut().toString(QKeySequence::NativeText);
        myRows.push_back(row);
    }
    std::sort(myRows.begin(), myRows.end(),
              [](const Row& a, const Row& b) { return a.label < b.label; });
}

void ShortcutSheet::showSheet()
{
    rebuild();

    const QFontMetrics metrics(font());
    int widest = 0;
    for (const Row& row : myRows) {
        widest = std::max(widest, metrics.horizontalAdvance(row.label) +
                                      metrics.horizontalAdvance(row.keys));
    }
    const int width = widest + kColumnGap + kPadding * 2;
    const int height = kTitleHeight + kRowHeight * rowCount() + kPadding;

    if (parentWidget()) {
        move((parentWidget()->width() - width) / 2,
             (parentWidget()->height() - height) / 2);
    }
    resize(width, height);
    show();
    raise();
    setFocus();
}

void ShortcutSheet::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10.0, 10.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.drawPath(panel);

    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPadding, 0, width() - kPadding * 2, kTitleHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Keyboard shortcuts"));

    painter.setFont(font());
    int y = kTitleHeight;
    for (const Row& row : myRows) {
        const QRect line(kPadding, y, width() - kPadding * 2, kRowHeight);
        painter.setPen(Theme::text());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, row.label);
        painter.setPen(Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignRight, row.keys);
        y += kRowHeight;
    }
}

void ShortcutSheet::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ShortcutSheet::mousePressEvent(QMouseEvent* /*event*/)
{
    hide();
}
```

- [ ] **Step 5: Wire it up**

Add `src/ui/ShortcutSheet.cpp` to the `furnify_app` source list in `CMakeLists.txt`.

In `src/MainWindow.cpp`, add `#include "ShortcutSheet.h"`, declare `class ShortcutSheet* myShortcutSheet = nullptr;` among the members in `src/MainWindow.h`, and in the constructor after `buildOverlay()`:

```cpp
    myShortcutSheet = new ShortcutSheet(this);
    connect(myShortcutsAction, &QAction::triggered, myShortcutSheet, &ShortcutSheet::showSheet);
```

- [ ] **Step 6: Verify GREEN**

Kill processes, build, run gui_smoke and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 5/5.

- [ ] **Step 7: Commit**

```bash
git add src/ui/ShortcutSheet.h src/ui/ShortcutSheet.cpp src/MainWindow.h src/MainWindow.cpp CMakeLists.txt tests/gui_smoke.cpp
git commit -m "Add a shortcut sheet generated from the actions themselves"
```

---

### Task 4: The guided first build

**Files:**
- Create: `src/ui/WalkthroughPanel.h`, `src/ui/WalkthroughPanel.cpp`
- Modify: `CMakeLists.txt`, `src/ui/ViewportOverlay.h`, `src/ui/ViewportOverlay.cpp`, `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `MainWindow::appStateChanged()`, `progress()`, `recordProgress()` (Task 2).
- Produces: `ViewportOverlay::Anchor::BottomRight`; `class WalkthroughPanel : public QWidget` with `explicit WalkthroughPanel(MainWindow* window, QWidget* parent)`, `int completedSteps() const`, `bool isFinished() const`.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add `#include "WalkthroughPanel.h"` and insert **immediately after the window is shown and settled**, before any modelling happens:

```cpp
    // --- the walkthrough appears for a newcomer -------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr, "a new user gets the guided first build");
        check(guide != nullptr && guide->isVisible(), "the guide is visible on first run");
        check(guide != nullptr && guide->completedSteps() == 0,
              "no steps are complete before the user does anything");
    }
```

and insert before the final `std::printf`:

```cpp
    // --- the walkthrough completes and stays gone ------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr && guide->isFinished(),
              "building a body completes the guide");
        check(guide != nullptr && !guide->isVisible(),
              "a finished guide hides itself");
        check(window.progress().hasLearned("walkthrough.done"),
              "finishing records walkthrough.done");

        // A returning user does not see it again.
        MainWindow second(nullptr, /*persistProgress=*/false);
        second.progress().record("walkthrough.done");
        second.progress().record("walkthrough.done");
        second.progress().record("walkthrough.done");
        second.setAttribute(Qt::WA_ShowWithoutActivating);
        second.resize(900, 600);
        second.show();
        settle(400);
        WalkthroughPanel* repeat = second.findChild<WalkthroughPanel*>();
        check(repeat == nullptr || !repeat->isVisible(),
              "a returning user never sees the guide again");
        second.close();
    }
```

- [ ] **Step 2: Verify RED**

Kill processes, build.
Expected: FAIL to compile — cannot open include file `WalkthroughPanel.h`.

- [ ] **Step 3: Add the BottomRight anchor**

In `src/ui/ViewportOverlay.h`, extend the enum:

```cpp
    enum class Anchor { TopLeft, LeftCenter, BottomLeft, TopRight, RightCenter, BottomRight };
```

In `src/ui/ViewportOverlay.cpp`'s `relayout()`, add a cursor beside `bottomLeftY`:

```cpp
    int bottomRightY = h - kMargin;
```

and a case in the switch, mirroring `BottomLeft`:

```cpp
            case Anchor::BottomRight:
                bottomRightY -= ch;
                cluster->move(w - cw - kMargin, bottomRightY);
                bottomRightY -= kGap;
                break;
```

- [ ] **Step 4: Write the panel header**

Create `src/ui/WalkthroughPanel.h`:

```cpp
#pragma once
//
// The guided first build: four steps that tick as the user genuinely performs
// them. Step state is DERIVED from live application state on every
// appStateChanged, never stored as a cursor - a stored cursor is a second
// source of truth that drifts from the first.
//
#include <QWidget>

class MainWindow;

class WalkthroughPanel : public QWidget {
    Q_OBJECT

public:
    WalkthroughPanel(MainWindow* window, QWidget* parent);

    int completedSteps() const { return myCompleted; }
    bool isFinished() const { return myFinished; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return QSize(260, 168); }

private:
    void refresh();
    void finish();
    QRect skipRect() const;

    MainWindow* myWindow = nullptr;
    int myCompleted = 0;
    bool myFinished = false;

    // Latches: a step stays ticked once reached, because the state that proves
    // it (a sketch in progress, a pending face) is transient by nature.
    bool myStartedSketch = false;
    bool myPlacedPoints = false;
    bool myClosedOutline = false;
};
```

- [ ] **Step 5: Write the panel implementation**

Create `src/ui/WalkthroughPanel.cpp`:

```cpp
#include "WalkthroughPanel.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "SketchController.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 14;
constexpr int kTitle = 30;
constexpr int kStep = 26;
}  // namespace

WalkthroughPanel::WalkthroughPanel(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedSize(sizeHint());
    connect(myWindow, &MainWindow::appStateChanged, this, &WalkthroughPanel::refresh);
    refresh();
}

void WalkthroughPanel::refresh()
{
    if (myFinished) return;

    // Derive from live state; latch the transient ones.
    if (myWindow->isSketching()) myStartedSketch = true;
    if (myWindow->sketch().pointCount() >= 3) { myStartedSketch = true; myPlacedPoints = true; }
    if (myWindow->hasPendingFace()) {
        myStartedSketch = true; myPlacedPoints = true; myClosedOutline = true;
    }
    const bool madeBody = myWindow->document().count() > 0;
    if (madeBody) { myStartedSketch = true; myPlacedPoints = true; myClosedOutline = true; }

    myCompleted = (myStartedSketch ? 1 : 0) + (myPlacedPoints ? 1 : 0) +
                  (myClosedOutline ? 1 : 0) + (madeBody ? 1 : 0);

    if (madeBody) {
        finish();
        return;
    }
    update();
}

void WalkthroughPanel::finish()
{
    myFinished = true;
    // Record it up to the threshold so hasLearned("walkthrough.done") is true.
    // The guide is a one-shot rather than something you get better at, so it
    // reuses the counter as a flag rather than introducing a second concept.
    for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
        myWindow->recordProgress("walkthrough.done");
    }
    hide();
}

QRect WalkthroughPanel::skipRect() const
{
    return QRect(width() - kPad - 34, 8, 34, 18);
}

void WalkthroughPanel::mousePressEvent(QMouseEvent* event)
{
    if (skipRect().contains(event->position().toPoint())) finish();
}

void WalkthroughPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10.0, 10.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::accent(), 1.0));
    painter.drawPath(panel);

    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, 0, width() - kPad * 2, kTitle),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Make your first body"));

    painter.setFont(font());
    painter.setPen(Theme::textMuted());
    painter.drawText(skipRect(), Qt::AlignCenter, tr("skip"));

    const QString steps[4] = {
        tr("Press Ctrl+K to start an outline"),
        tr("Click at least 3 points on the ground"),
        tr("Press Enter to close the outline"),
        tr("Press E and give it a height"),
    };
    const bool done[4] = {myStartedSketch, myPlacedPoints, myClosedOutline,
                          myWindow->document().count() > 0};

    int y = kTitle;
    for (int i = 0; i < 4; ++i) {
        const QRect line(kPad, y, width() - kPad * 2, kStep);
        painter.setPen(done[i] ? Theme::accent() : Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft,
                         (done[i] ? QStringLiteral("\u2713  ") : QStringLiteral("\u2022  ")) +
                             steps[i]);
        y += kStep;
    }
}
```

- [ ] **Step 6: Mount it**

Add `src/ui/WalkthroughPanel.cpp` to the `furnify_app` sources.

In `src/MainWindow.cpp`, add `#include "WalkthroughPanel.h"`, and at the end of `buildOverlay()`:

```cpp
    // Only a newcomer sees this; it removes itself for good once completed.
    if (!myProgress.hasLearned("walkthrough.done")) {
        myOverlay->addWidget(new WalkthroughPanel(this, myView),
                             ViewportOverlay::Anchor::BottomRight);
    }
```

- [ ] **Step 7: Verify GREEN**

Kill processes, build, run gui_smoke and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 5/5.

- [ ] **Step 8: Commit**

```bash
git add src/ui/WalkthroughPanel.h src/ui/WalkthroughPanel.cpp src/ui/ViewportOverlay.h src/ui/ViewportOverlay.cpp src/MainWindow.cpp CMakeLists.txt tests/gui_smoke.cpp
git commit -m "Add the guided first build"
```

---

### Task 5: Hint balloons

**Files:**
- Create: `src/ui/HintBalloon.h`, `src/ui/HintBalloon.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `MainWindow::appStateChanged()`, `progress()` (Task 2).
- Produces: `class HintBalloon : public QWidget` with `HintBalloon(MainWindow* window, QWidget* parent)`, `QString currentHint() const` (empty when nothing is showing).

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add `#include "HintBalloon.h"` and insert immediately after the check that shift-click selects a second body:

```cpp
    // --- a hint appears the first time two bodies are selected ----------------
    {
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the window has a hint balloon");
        check(hint != nullptr && !hint->currentHint().isEmpty(),
              QStringLiteral("selecting two bodies raises a hint (\"%1\")")
                  .arg(hint ? hint->currentHint() : QString()));
        check(hint != nullptr && hint->currentHint().contains(QStringLiteral("Union")),
              "the hint names the operations now available");
    }
```

and insert before the final `std::printf`:

```cpp
    // --- a learned hint never appears again ------------------------------------
    {
        // The live window already has two bodies and has shown the boolean hint.
        // Teach it, dismiss what is up, then reproduce the exact condition that
        // raised it and assert nothing comes back.
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the hint balloon is still around");
        if (hint) {
            for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
                window.progress().record("boolean.completed");
                window.progress().record("faceMode.used");
                window.progress().record("view.changed");
            }
            // Force a fresh look at the state with everything learned.
            view->clearSelection();
            settle(150);
            const std::vector<int> two = {window.document().solids().front().id,
                                          window.document().solids().back().id};
            if (two.size() == 2 && two[0] != two[1]) {
                view->setSelectedSolids(two);
                settle(200);
                check(hint->currentHint().isEmpty(),
                      "a user who has run three booleans is not told about them again");
            } else {
                check(false, "expected two distinct bodies to re-trigger the hint");
            }
        }
    }
```

That block must run while the document still holds **two** bodies — place it before any
step that reduces the document to one. If the suite has already run its Cut by that point,
create the condition first or move the block earlier.

Add `#include "UserProgress.h"` to the test file.

- [ ] **Step 2: Verify RED**

Kill processes, build.
Expected: FAIL to compile — cannot open include file `HintBalloon.h`.

- [ ] **Step 3: Write the header**

Create `src/ui/HintBalloon.h`:

```cpp
#pragma once
//
// A small balloon that teaches one capability the first time it becomes
// available, and never again once the user has done it three times. It owns
// both the widget and the policy: which hint is due is decided here, from
// UserProgress plus live application state.
//
#include <QString>
#include <QWidget>

#include <set>

class MainWindow;

class HintBalloon : public QWidget {
    Q_OBJECT

public:
    HintBalloon(MainWindow* window, QWidget* parent);

    // The text currently shown, or empty when no hint is up.
    QString currentHint() const { return myText; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void reconsider();
    void showHint(const QString& event, const QString& text);
    void dismiss();

    MainWindow* myWindow = nullptr;
    QString myText;
    QString myEvent;     // the progress event this hint teaches
    // Per-hint, not global: a single flag would mean the first balloon shown
    // silenced the other two for the rest of the session.
    std::set<QString> myShownThisSession;
};
```

- [ ] **Step 4: Write the implementation**

Create `src/ui/HintBalloon.cpp`:

```cpp
#include "HintBalloon.h"

#include "DocumentModel.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "UserProgress.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 250;
}  // namespace

HintBalloon::HintBalloon(MainWindow* window, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
{
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    connect(myWindow, &MainWindow::appStateChanged, this, &HintBalloon::reconsider);
}

void HintBalloon::reconsider()
{
    // One balloon per session per capability: without this it would reappear
    // every time the selection changed. The persisted count is the other half -
    // it stops the hint across sessions once the action has been done three
    // times. Both conditions must hold.
    if (!myText.isEmpty()) return;   // one balloon at a time

    const UserProgress& progress = myWindow->progress();
    const std::size_t selected = myWindow->view()->selectedSolidIds().size();

    if (selected == 2 && !progress.hasLearned("boolean.completed") &&
        !myShownThisSession.count(QStringLiteral("boolean.completed"))) {
        showHint(QStringLiteral("boolean.completed"),
                 tr("Two bodies selected — Union merges them, Subtract cuts the second "
                    "out of the first, Intersect keeps only the overlap."));
        return;
    }

    if (myWindow->document().count() > 0 && !progress.hasLearned("faceMode.used") &&
        !myShownThisSession.count(QStringLiteral("faceMode.used"))) {
        showHint(QStringLiteral("faceMode.used"),
                 tr("Switch to Select Faces to pick one face at a time instead of a "
                    "whole body."));
        return;
    }

    if (myWindow->document().count() > 0 && !progress.hasLearned("view.changed") &&
        !myShownThisSession.count(QStringLiteral("view.changed"))) {
        showHint(QStringLiteral("view.changed"),
                 tr("Click an arm of the gizmo, top right, to look from that direction. "
                    "Keys 0 to 3 do the same."));
    }
}

void HintBalloon::showHint(const QString& event, const QString& text)
{
    myEvent = event;
    myText = text;
    myShownThisSession.insert(event);

    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, kWidth - kPad * 2, 1000),
                                              Qt::TextWordWrap, myText);
    resize(kWidth, bounds.height() + kPad * 2 + 22);
    if (parentWidget()) {
        move((parentWidget()->width() - width()) / 2,
             parentWidget()->height() - height() - 90);
    }
    show();
    raise();
}

void HintBalloon::dismiss()
{
    myText.clear();
    myEvent.clear();
    hide();
}

void HintBalloon::mousePressEvent(QMouseEvent* /*event*/)
{
    dismiss();
}

void HintBalloon::paintEvent(QPaintEvent* /*event*/)
{
    if (myText.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8.0, 8.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::accent(), 1.0));
    painter.drawPath(panel);

    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, kPad, width() - kPad * 2, height() - kPad * 2 - 20),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, myText);

    painter.setPen(Theme::accent());
    painter.drawText(QRect(kPad, height() - 26, width() - kPad * 2, 20),
                     Qt::AlignRight | Qt::AlignVCenter, tr("got it"));
}
```

- [ ] **Step 5: Mount it**

Add `src/ui/HintBalloon.cpp` to the `furnify_app` sources. In `src/MainWindow.cpp`, add
`#include "HintBalloon.h"`, and at the end of `buildOverlay()` — after the walkthrough
block, so the guide is never competing with a hint on first run:

```cpp
    new HintBalloon(this, myView);
```

It parents itself to the viewport and positions itself; it needs no overlay anchor because
it is centred near the bottom rather than pinned to an edge.

- [ ] **Step 6: Verify GREEN**

Kill processes, build, run gui_smoke and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 5/5.

- [ ] **Step 7: Commit**

```bash
git add src/ui/HintBalloon.h src/ui/HintBalloon.cpp src/MainWindow.cpp CMakeLists.txt tests/gui_smoke.cpp
git commit -m "Add hint balloons that fall silent once learned"
```

---

### Task 6: Document and check the result

**Files:**
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-08-28-help-and-learnability-design.md`

- [ ] **Step 1: Add a section to CLAUDE.md**

After the vocabulary section:

```markdown
### Teaching surfaces, and how they go quiet

`UserProgress` (`src/UserProgress.h`, Qt-free) counts what the user has actually
done. Three completions of an action means it is learned, and its hint never
appears again. Storage is injected, not built in: `MainWindow` persists
`serialize()` through `QSettings`, while `gui_smoke` constructs
`MainWindow(nullptr, false)` and never touches the real store — a suite whose
result depended on how often the developer had run the app would not be a suite.

Four surfaces teach, and all four are governed by that counter:

| Surface | Appears | Goes quiet |
|---|---|---|
| `WalkthroughPanel` | first run, bottom right | on completion or skip, forever |
| `HintBalloon` | when a capability first becomes available | after 3 completions |
| `ShortcutSheet` | on demand, `?` or `F1` | never — it is on-demand |
| Tooltips | on hover | never |

`ShortcutSheet` generates its rows from the window's own `QAction`s, so it cannot
list a stale binding. `WalkthroughPanel` derives every step from live application
state on each `appStateChanged()` rather than storing a cursor — a stored cursor
is a second source of truth, and it drifts.

`MainWindow::appStateChanged()` fires at the end of `updateActions()`. Slots may
only read state and repaint themselves; calling back into `updateActions()` from
one would recurse.

`Help → Show tips again` resets the store, which is how you demonstrate the app
to somebody else without reinstalling it.
```

- [ ] **Step 2: Mark the spec implemented**

Change the spec's `Status:` line to `Status: implemented 2026-08-28`.

- [ ] **Step 3: Look at it**

Kill app processes, build, run `.\build\RelWithDebInfo\gui_smoke.exe .` (expect
`PASS (0 failures)`) and `ctest --preset windows-headless` (expect 5/5).

Then launch `.\build\RelWithDebInfo\furnifyme.exe`, wait four seconds, and capture the
window as a still (`GetWindowRect` + `CopyFromScreen` — **capture only, no input
injection**). Note that the real app reads the persisted store, so the walkthrough appears
only if it has not already been completed on this machine; if it is absent, run
`Help → Show tips again` is not possible without input, so instead report that it was
absent and why. Confirm from the still: the panel is bottom right and legible, its four
steps read clearly, and nothing overlaps the axis gizmo or the tool clusters. Report the
screenshot path and what it shows.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-08-28-help-and-learnability-design.md
git commit -m "Document the teaching surfaces"
```
