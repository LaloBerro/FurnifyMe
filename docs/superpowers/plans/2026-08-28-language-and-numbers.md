# Language and Numbers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every user-facing string and number in FurnifyMe with copy a furniture maker would recognise: one name per thing, bounding-box dimensions instead of volumes, and errors that name a cause and a fix.

**Architecture:** A Qt-free `Measure` formatter joins `furnify_geometry` so number formatting is headless-testable. `MainWindow` and `ItemsPanel` then have their copy rewritten against a fixed vocabulary, which a `gui_smoke` assertion enforces so it cannot drift.

**Tech Stack:** C++17, Qt 6.11.1 Widgets (qtbase only), OpenCascade 8.0.1, CMake presets + vcpkg, MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-08-28-language-and-numbers-design.md`

## Global Constraints

- **`Measure` must be Qt-free** — it joins `furnify_geometry`, which links no Qt. `std::string`, `gp_*`, `Bnd_Box`, `BRepBndLib` only. Callers wrap with `QString::fromStdString`.
- `ModelingOps`, `DocumentModel`, `SketchController`, `CameraController` stay Qt-free.
- Qt 6 Widgets, qtbase only. No `qtsvg`, no Qt Test module, no new dependency, never install anything.
- No `Debug` configuration; build `RelWithDebInfo` via presets. cmake/ctest at `C:/Program Files/CMake/bin/`.
- **Kill running app processes before every build** (`Get-Process furnifyme,gui_smoke -ErrorAction SilentlyContinue | Stop-Process -Force`) or the link fails with `LNK1168`.
- **No OS-level input simulation** in code or verification. Verify with `gui_smoke` (in-process Qt events) and still screenshots.
- Never `delete` an OCCT `Handle()`.
- The GUI suite currently prints `PASS (0 failures)` with **90 checks**; the headless suite is 3 executables. Trust each run's own printout over any number written here.
- **Banned from every user-visible string:** `OCCT`, `Fuse`, `Solid #`, `mm3`, `(s)`.
- **Vocabulary, exact:** the 2D shape being drawn is an **outline**; closed, it is a **face**; extruded, it is a **body**, formatted `Body 03`. Operations are **Union**, **Subtract**, **Intersect**. The 3D area is the **viewport**.
- **Punctuation:** status-bar left (what happened) and right (where you are) take no trailing period. Dialog body text is full sentences with periods. Tooltips are a fragment with no trailing period, optionally followed by one teaching sentence. Clauses in status lines are separated by an em dash `—`, never a hyphen.
- **Singular and plural are written per-string.** No `(s)` anywhere.
- **Formatting is done by hand, never through `std::locale`** — locale-dependent output would make the tests pass or fail based on the machine's regional settings.

---

### Task 1: The `Measure` formatter

**Files:**
- Create: `src/Measure.h`, `src/Measure.cpp`, `tests/measure.cpp`
- Modify: `CMakeLists.txt` (add to `furnify_geometry`; register a `headless_measure` ctest target)

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
namespace Measure {
struct Extents { double x = 0.0, y = 0.0, z = 0.0; };
Extents extentsOf(const TopoDS_Shape& shape);
std::string formatLength(double millimetres);
std::string formatDimensions(const TopoDS_Shape& shape);
}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/measure.cpp`:

```cpp
//
// Headless tests for number formatting. Formatting is logic, and logic in this
// project is testable without a window - which is why Measure is Qt-free and
// lives in furnify_geometry.
//
#include "Measure.h"
#include "ModelingOps.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void checkEq(const std::string& actual, const std::string& expected, const std::string& what)
{
    const bool ok = actual == expected;
    std::printf("%-6s %s (got \"%s\", expected \"%s\")\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual.c_str(), expected.c_str());
    if (!ok) ++g_failures;
}

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}
}  // namespace

int main()
{
    // --- formatLength ---------------------------------------------------------
    checkEq(Measure::formatLength(340.0), "340 mm", "a whole value has no decimal part");
    checkEq(Measure::formatLength(18.5), "18.5 mm", "a half millimetre keeps one decimal");
    checkEq(Measure::formatLength(1200.0), "1,200 mm", "four digits are comma separated");
    checkEq(Measure::formatLength(999.0), "999 mm", "three digits are not separated");
    checkEq(Measure::formatLength(1000.0), "1,000 mm", "the separator starts at four digits");
    checkEq(Measure::formatLength(1234567.0), "1,234,567 mm", "separators repeat every three");
    checkEq(Measure::formatLength(1234.5), "1,234.5 mm", "separators and a decimal coexist");

    // Rounding, stated precisely because the panel and the status bar must never
    // disagree about how thick a body is.
    checkEq(Measure::formatLength(18.04), "18 mm", "18.04 rounds down and drops the .0");
    checkEq(Measure::formatLength(18.05), "18.1 mm", "18.05 rounds up to one decimal");
    checkEq(Measure::formatLength(0.04), "0 mm", "a value below half a tenth reads as zero");
    checkEq(Measure::formatLength(-0.01), "0 mm", "a tiny negative never renders as -0");
    checkEq(Measure::formatLength(0.0), "0 mm", "zero is plain");

    // --- extentsOf and formatDimensions --------------------------------------
    {
        const TopoDS_Shape box =
            ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), 340.0, 220.0, 18.0);

        const Measure::Extents e = Measure::extentsOf(box);
        check(std::abs(e.x - 340.0) < 1e-6 && std::abs(e.y - 220.0) < 1e-6 &&
              std::abs(e.z - 18.0) < 1e-6,
              "extentsOf returns the box's own width, depth and height");

        const std::string dims = Measure::formatDimensions(box);
        checkEq(dims, "340 \xC3\x97 220 \xC3\x97 18 mm",
                "dimensions read X by Y by Z with a real multiplication sign");
        check(dims.find('x') == std::string::npos,
              "the separator is U+00D7, not the letter x");

        // A box away from the origin has the same extents: it is a size, not a
        // position.
        const TopoDS_Shape moved =
            ModelingOps::makeBox(gp_Pnt(-500.0, 900.0, 40.0), 340.0, 220.0, 18.0);
        checkEq(Measure::formatDimensions(moved), dims,
                "position does not change the reported dimensions");
    }

    // --- degenerate input -----------------------------------------------------
    {
        const TopoDS_Shape nothing;
        checkEq(Measure::formatDimensions(nothing), "",
                "a null shape formats as an empty string, not 0 by 0 by 0");
        const Measure::Extents e = Measure::extentsOf(nothing);
        check(e.x == 0.0 && e.y == 0.0 && e.z == 0.0, "a null shape has zero extents");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Wire into CMake and verify RED**

In `CMakeLists.txt`, add `src/Measure.cpp` to the `furnify_geometry` source list, and beside the other test targets:

```cmake
add_executable(headless_measure tests/measure.cpp)
target_link_libraries(headless_measure PRIVATE furnify_geometry)
add_test(NAME headless_measure COMMAND headless_measure)
```

Run: `cmake --preset windows-headless` then `cmake --build --preset windows-headless`
Expected: FAIL to compile — cannot open include file `Measure.h`.

- [ ] **Step 3: Write the header**

Create `src/Measure.h`:

```cpp
#pragma once
//
// Formats measurements for display. Qt-free and in furnify_geometry so the
// rounding and separator rules are covered by headless tests - the status bar
// and the items panel must never disagree about how thick a body is.
//
#include <string>

#include <TopoDS_Shape.hxx>

namespace Measure {

// Bounding-box size along each world axis, in millimetres. All zero for a null
// or void shape.
struct Extents {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Extents extentsOf(const TopoDS_Shape& shape);

// "340 mm", "1,200 mm", "18.5 mm". Rounded to one decimal place with a trailing
// ".0" dropped, thousands separated by a plain comma. Formatted by hand rather
// than through std::locale, whose output would depend on the machine's regional
// settings and make these tests pass or fail by geography.
std::string formatLength(double millimetres);

// "340 x 220 x 18 mm" with U+00D7 between the numbers, always in X, Y, Z order
// so the three values always mean the same thing. Empty string for a null or
// void shape.
std::string formatDimensions(const TopoDS_Shape& shape);

}  // namespace Measure
```

- [ ] **Step 4: Write the implementation**

Create `src/Measure.cpp`:

```cpp
#include "Measure.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

namespace Measure {
namespace {

// U+00D7 MULTIPLICATION SIGN, written as UTF-8 bytes so the file's own encoding
// cannot change what ships.
const char* kTimes = "\xC3\x97";

// Groups the integer part in threes: 1234567 -> "1,234,567".
std::string groupThousands(const std::string& digits)
{
    std::string out;
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (i - lead) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

}  // namespace

Extents extentsOf(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return Extents{};

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return Extents{};

    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return Extents{xmax - xmin, ymax - ymin, zmax - zmin};
}

std::string formatLength(double millimetres)
{
    // Round to a tenth first, so every later decision sees the same number the
    // user will read.
    double value = std::round(millimetres * 10.0) / 10.0;
    if (value == 0.0) value = 0.0;   // collapses -0.0, which would print "-0"

    const bool negative = value < 0.0;
    const double magnitude = std::fabs(value);
    const long long whole = static_cast<long long>(magnitude);
    const int tenth = static_cast<int>(std::llround((magnitude - whole) * 10.0));

    char digits[32];
    std::snprintf(digits, sizeof(digits), "%lld", whole);

    std::string out;
    if (negative) out.push_back('-');
    out += groupThousands(digits);
    if (tenth != 0) {
        out.push_back('.');
        out.push_back(static_cast<char>('0' + tenth));
    }
    out += " mm";
    return out;
}

std::string formatDimensions(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return std::string();

    const Extents e = extentsOf(shape);
    if (e.x == 0.0 && e.y == 0.0 && e.z == 0.0) return std::string();

    // Each number is formatted by formatLength so the rounding rule lives in
    // exactly one place, then the trailing unit is stripped from all but the
    // last - "340 x 220 x 18 mm", not "340 mm x 220 mm x 18 mm".
    auto bare = [](const std::string& withUnit) {
        const std::size_t space = withUnit.rfind(' ');
        return space == std::string::npos ? withUnit : withUnit.substr(0, space);
    };

    return bare(formatLength(e.x)) + " " + kTimes + " " + bare(formatLength(e.y)) +
           " " + kTimes + " " + formatLength(e.z);
}

}  // namespace Measure
```

- [ ] **Step 5: Verify GREEN**

Run: `cmake --build --preset windows-headless` then `ctest --preset windows-headless`
Expected: 4/4 tests pass (the three existing plus `headless_measure`).

- [ ] **Step 6: Commit**

```bash
git add src/Measure.h src/Measure.cpp tests/measure.cpp CMakeLists.txt
git commit -m "Add the Measure formatter for lengths and dimensions"
```

---

### Task 2: Rename the operations to Union / Subtract / Intersect

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `MainWindow` members renamed `myUnionAction`, `mySubtractAction`, `myIntersectAction`; slots renamed `onUnion()`, `onSubtract()`, `onIntersect()`. Action texts become `&Union`, `&Subtract`, `&Intersect`. `IconSet::Glyph::Fuse` and `::Cut` keep their enumerator names — they are internal glyph identifiers, not user-visible strings.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, insert just before the final `std::printf`:

```cpp
    // --- vocabulary is enforced, not merely documented ------------------------
    {
        // A documented vocabulary drifts the moment someone is in a hurry. An
        // asserted one cannot.
        const QStringList banned = {QStringLiteral("Fuse"), QStringLiteral("Solid"),
                                    QStringLiteral("OCCT"), QStringLiteral("mm3")};
        QStringList offenders;
        for (QAction* candidate : window.findChildren<QAction*>()) {
            const QString text = candidate->text().remove(QLatin1Char('&'));
            const QString tip = candidate->toolTip();
            for (const QString& word : banned) {
                if (text.contains(word) || tip.contains(word)) {
                    offenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(offenders.isEmpty(),
              QStringLiteral("no action uses a banned word (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("none")
                                           : offenders.join(QStringLiteral(", "))));

        check(action(window, QStringLiteral("Union")) != nullptr, "the Union action exists");
        check(action(window, QStringLiteral("Subtract")) != nullptr, "the Subtract action exists");
        check(action(window, QStringLiteral("Intersect")) != nullptr, "the Intersect action exists");
    }
```

- [ ] **Step 2: Verify RED**

Kill app processes, `cmake --build --preset windows`, run `.\build\RelWithDebInfo\gui_smoke.exe .`
Expected: FAIL — the banned-word check lists `Fuse`, and `the Union action exists` fails.

- [ ] **Step 3: Rename the members and slots**

In `src/MainWindow.h`, rename in the slots section:

```cpp
    void onUnion();
    void onSubtract();
    void onIntersect();
```

and in the members:

```cpp
    QAction* myUnionAction = nullptr;
    QAction* mySubtractAction = nullptr;
    QAction* myIntersectAction = nullptr;
```

In `src/MainWindow.cpp`, replace the three action constructions:

```cpp
    myUnionAction = new QAction(tr("&Union"), this);
    connect(myUnionAction, &QAction::triggered, this, &MainWindow::onUnion);

    mySubtractAction = new QAction(tr("&Subtract"), this);
    connect(mySubtractAction, &QAction::triggered, this, &MainWindow::onSubtract);

    myIntersectAction = new QAction(tr("&Intersect"), this);
    connect(myIntersectAction, &QAction::triggered, this, &MainWindow::onIntersect);
```

and the three slot definitions:

```cpp
void MainWindow::onUnion()     { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Fuse)); }
void MainWindow::onSubtract()  { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Cut)); }
void MainWindow::onIntersect() { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Common)); }
```

`ModelingOps::BooleanKind::Fuse` and `::Cut` keep their names — they are kernel-facing enumerators the user never sees, and renaming them would touch the geometry library and its tests for no user-visible gain.

Update every remaining reference: the menu (`modelMenu->addAction(...)`), the overlay cluster in `buildOverlay()`, and `updateActions()`'s three `setEnabled` calls. Search for `myFuseAction`, `myCutAction`, `myCommonAction` and confirm none remain.

- [ ] **Step 4: Write the teaching tooltips**

Replace the three tooltip lines:

```cpp
    myUnionAction->setToolTip(tr("Merge two bodies into one\n"
                                 "Overlapping material is kept once, not twice."));
    mySubtractAction->setToolTip(tr("Cut the second body out of the first\n"
                                    "Like a chisel removing waste. The body you made "
                                    "first is the one that keeps its shape."));
    myIntersectAction->setToolTip(tr("Keep only where two bodies overlap\n"
                                     "Everything outside the shared volume is discarded."));
```

- [ ] **Step 5: Verify GREEN**

Kill processes, build, run gui_smoke.
Expected: `PASS (0 failures)`; the banned-word check reports `none`.

- [ ] **Step 6: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp tests/gui_smoke.cpp
git commit -m "Rename the boolean operations to Union, Subtract and Intersect"
```

---

### Task 3: Report dimensions instead of volume

**Files:**
- Modify: `src/MainWindow.cpp`, `src/ui/ItemsPanel.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `Measure::formatDimensions` (Task 1); the renamed actions (Task 2).
- Produces: nothing new; existing call sites change what they print.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, insert immediately after the existing `check(window.document().count() == 1, "one solid in the document");` line:

```cpp
    // --- the app reports dimensions, not volume -------------------------------
    {
        const QString status = window.statusBar()->currentMessage();
        check(status.contains(QStringLiteral("Body 0")),
              QStringLiteral("the status line names the body (\"%1\")").arg(status));
        check(status.contains(QString::fromUtf8("\xC3\x97")),
              "the status line reports dimensions with a multiplication sign");
        check(!status.contains(QStringLiteral("volume")) &&
              !status.contains(QStringLiteral("mm3")),
              "the status line no longer mentions volume");
    }
```

Add `#include <QStatusBar>` to the test's includes if it is not already present.

- [ ] **Step 2: Verify RED**

Kill processes, build, run gui_smoke.
Expected: FAIL — the status line still reads `Solid #1 created (volume …)`.

- [ ] **Step 3: Rewrite the result messages**

In `src/MainWindow.cpp`, add `#include "Measure.h"`.

In `extrudePendingFace`, replace the closing status message:

```cpp
    statusBar()->showMessage(tr("%1 created — %2")
                                 .arg(QString::fromStdString(myDocument.nameOf(id)),
                                      QString::fromStdString(Measure::formatDimensions(solid))));
```

In `applyBooleanToSelection`, replace the closing status message. The verb varies with the operation, so build it from the kind:

```cpp
    const QString verb = kind == static_cast<int>(ModelingOps::BooleanKind::Fuse)
                             ? tr("Merged")
                             : kind == static_cast<int>(ModelingOps::BooleanKind::Cut)
                                   ? tr("Subtracted")
                                   : tr("Intersected");
    statusBar()->showMessage(tr("%1 %2 and %3 → %4 — %5")
                                 .arg(verb,
                                      QString::fromStdString(nameA),
                                      QString::fromStdString(nameB),
                                      QString::fromStdString(myDocument.nameOf(id)),
                                      QString::fromStdString(
                                          Measure::formatDimensions(result.shape))));
```

`nameA` and `nameB` must be captured **before** the operands are removed from the document — `nameOf` returns an empty string once they are gone. Immediately after `ids` is sorted and before the removal loop, add:

```cpp
    const std::string nameA = myDocument.nameOf(ids[0]);
    const std::string nameB = myDocument.nameOf(ids[1]);
```

The result body's own name is read *after* `myDocument.addSolid(result.shape)` returns its
id, which is the existing order — `nameOf(id)` on a body that does not exist yet would
return an empty string and the message would read `… → — 340 × 220 × 18 mm`.

- [ ] **Step 4: Show dimensions in the items panel**

In `src/ui/ItemsPanel.cpp`, add `#include "Measure.h"`, and after the existing name label add a second, dimmer label:

```cpp
        auto* size = new QLabel(
            QString::fromStdString(Measure::formatDimensions(solid.shape)), row);
        size->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                .arg(Theme::textMuted().name()));
        layout->addWidget(size);
```

Insert it after `layout->addWidget(name, 1);` and before the eye button, so each row reads `Body 03 · 340 × 220 × 18 mm · [eye]`.

- [ ] **Step 5: Verify GREEN**

Kill processes, build, run gui_smoke and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 4/4.

- [ ] **Step 6: Commit**

```bash
git add src/MainWindow.cpp src/ui/ItemsPanel.cpp tests/gui_smoke.cpp
git commit -m "Report body dimensions instead of volume"
```

---

### Task 4: Rewrite the status and state copy

**Files:**
- Modify: `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: nothing new.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add to the vocabulary block from Task 2, just after the `offenders.isEmpty()` check:

```cpp
        // The state label is the app's most-updated string; it must obey the
        // vocabulary too. It is a permanent widget on the status bar.
        QString stateText;
        for (QLabel* label : window.statusBar()->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) stateText = label->text();
        }
        check(!stateText.contains(QStringLiteral("solid")) &&
              !stateText.contains(QStringLiteral("Solid")),
              QStringLiteral("the state label says body, not solid (\"%1\")").arg(stateText));
        check(!stateText.contains(QStringLiteral("(s)")),
              "the state label writes plurals out rather than using (s)");
```

Add `#include <QLabel>` to the test includes.

- [ ] **Step 2: Verify RED**

Build and run gui_smoke.
Expected: FAIL — the state label reads `1 solid selected — shift-click a second one for a boolean`.

- [ ] **Step 3: Rewrite `updateStateLabel`**

Replace the body of `MainWindow::updateStateLabel()`'s string construction:

```cpp
    QString state;
    if (mySketching) {
        const int placed = static_cast<int>(mySketch.pointCount());
        if (mySketch.canClose()) {
            state = tr("Sketching — %1 points. Enter or click the first point to close.")
                        .arg(placed);
        } else {
            const int needed = 3 - placed;
            state = needed == 1
                        ? tr("Sketching — %1 points, 1 more to close").arg(placed)
                        : tr("Sketching — %1 points, %2 more to close").arg(placed).arg(needed);
        }
    } else if (!myPendingFace.IsNull()) {
        state = tr("Face ready — press E to extrude");
    } else {
        const std::size_t selected = myView->selectedSolidIds().size();
        const std::size_t bodies = myDocument.count();
        if (selected == 2) {
            state = tr("2 bodies selected — Union, Subtract and Intersect available");
        } else if (selected == 1) {
            state = tr("1 body selected — shift-click another for a boolean");
        } else if (bodies == 0) {
            state = tr("Nothing yet — press Ctrl+K to draw an outline");
        } else if (bodies == 1) {
            state = tr("1 body — click it to select");
        } else {
            state = tr("%1 bodies — click one to select").arg(bodies);
        }
    }
    myStateLabel->setText(state);
```

- [ ] **Step 4: Rewrite the transient status messages**

Replace each of these in `src/MainWindow.cpp`, matching singular and plural by hand:

```cpp
    // Constructor hint
    statusBar()->showMessage(tr("Right-drag to orbit, middle-drag to pan, wheel to zoom"));

    // onDeleteSelected
    statusBar()->showMessage(
        ids.size() == 1 ? tr("Deleted %1").arg(QString::fromStdString(deletedName))
                        : tr("Deleted %1 bodies").arg(ids.size()));

    // onUndo
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Undone — 1 body in the document")
                                 : tr("Undone — %1 bodies in the document").arg(myDocument.count()));

    // onRedo
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Redone — 1 body in the document")
                                 : tr("Redone — %1 bodies in the document").arg(myDocument.count()));

    // onSnapToggled
    statusBar()->showMessage(enabled
                                 ? tr("Snapping to the 10 mm grid")
                                 : tr("Snapping off — points land exactly where you click"));

    // onSketchCursorMoved
    statusBar()->showMessage(tr("Cursor at %1, %2")
                                 .arg(QString::fromStdString(Measure::formatLength(point.X())),
                                      QString::fromStdString(Measure::formatLength(point.Y()))));

    // onStartSketch
    statusBar()->showMessage(tr("Click points on the ground to draw an outline. "
                                "Enter closes it, Backspace undoes a point, Esc cancels."));

    // onSketchPointPicked
    statusBar()->showMessage(
        mySketch.pointCount() == 1
            ? tr("1 point placed")
            : tr("%1 points placed").arg(mySketch.pointCount()));

    // onCancelSketch
    statusBar()->showMessage(tr("Sketch cancelled"));

    // onFinishSketch
    statusBar()->showMessage(tr("Outline closed — press E to extrude it into a body"));

    // onSelectionModeChanged
    statusBar()->showMessage(myFaceSelectAction->isChecked()
                                 ? tr("Face selection — hovering highlights one face at a time")
                                 : tr("Body selection — click whole bodies for booleans"));

    // onSelectionChanged
    const std::size_t count = myView->selectedSolidIds().size();
    statusBar()->showMessage(count == 0   ? tr("Nothing selected")
                             : count == 1 ? tr("1 body selected")
                                          : tr("%1 bodies selected").arg(count));

    // onExportStep success
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Exported 1 body to %1").arg(path)
                                 : tr("Exported %1 bodies to %2")
                                       .arg(myDocument.count()).arg(path));
```

`onDeleteSelected` needs `deletedName` captured before removal, since `nameOf` is empty afterwards. Immediately after the `ids.empty()` guard add:

```cpp
    const std::string deletedName = ids.size() == 1 ? myDocument.nameOf(ids.front())
                                                    : std::string();
```

Note the cursor readout drops the Z coordinate: the sketch plane is always Z = 0 in this milestone, so printing `0 mm` every frame is noise.

- [ ] **Step 5: Verify GREEN**

Kill processes, build, run gui_smoke.
Expected: `PASS (0 failures)`.

- [ ] **Step 6: Commit**

```bash
git add src/MainWindow.cpp tests/gui_smoke.cpp
git commit -m "Rewrite the status bar and state label copy"
```

---

### Task 5: Rewrite the errors and tooltips

**Files:**
- Modify: `src/MainWindow.cpp`, `src/ui/ItemsPanel.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: nothing new.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, extend the banned-word block from Task 2 to cover every widget's tooltip, not just actions. Insert after the action loop:

```cpp
        QStringList tipOffenders;
        for (QWidget* widget : window.findChildren<QWidget*>()) {
            const QString tip = widget->toolTip();
            if (tip.isEmpty()) continue;
            for (const QString& word : banned) {
                if (tip.contains(word)) tipOffenders << (tip.left(30) + QStringLiteral("…"));
            }
        }
        check(tipOffenders.isEmpty(),
              QStringLiteral("no widget tooltip uses a banned word (%1)")
                  .arg(tipOffenders.isEmpty() ? QStringLiteral("none")
                                              : tipOffenders.join(QStringLiteral(", "))));
```

- [ ] **Step 2: Verify RED**

Build and run gui_smoke.
Expected: FAIL — the items panel eye button's tooltip reads `Show or hide this solid`.

- [ ] **Step 3: Rewrite the error dialogs**

In `src/MainWindow.cpp`:

```cpp
    // onFinishSketch, when the face cannot be built
    QMessageBox::warning(this, tr("Can't close this outline"),
                         tr("This outline can't close into a flat face. It probably "
                            "crosses itself.\n\n"
                            "Press Backspace to undo the last point and redraw it, or "
                            "Esc to start over."));

    // extrudePendingFace, when the prism fails
    QMessageBox::warning(this, tr("Extrude failed"),
                         tr("This face couldn't be extruded into a body.\n\n"
                            "The outline may cross itself or be too small to have an "
                            "inside. Try redrawing it with Ctrl+K."));

    // applyBooleanToSelection, wrong number of bodies
    QMessageBox::information(this, operationName,
                             tr("%1 needs exactly two bodies.\n\n"
                                "Click one body, then Shift-click another.")
                                 .arg(operationName));

    // applyBooleanToSelection, the operation failed
    QMessageBox::critical(this, tr("%1 failed").arg(operationName),
                          tr("The two bodies couldn't be combined.\n\n"
                             "This usually means they only touch at a single edge or "
                             "corner, which the geometry engine can't resolve. Move one "
                             "body so they overlap properly, then try again."));

    // onExportStep failure
    QMessageBox::critical(this, tr("Export failed"),
                          tr("Couldn't write the STEP file.\n\n"
                             "Check that the folder exists and isn't read-only, then "
                             "try a different location."));

    // the screenshot failure
    QMessageBox::warning(this, tr("Screenshot failed"),
                         tr("Couldn't save the image to %1.\n\n"
                            "Check that the folder exists and isn't read-only.").arg(path));
```

`operationName` is a local built at the top of `applyBooleanToSelection`, so the dialogs name the operation the user actually chose:

```cpp
    const QString operationName =
        kind == static_cast<int>(ModelingOps::BooleanKind::Fuse)   ? tr("Union")
        : kind == static_cast<int>(ModelingOps::BooleanKind::Cut)  ? tr("Subtract")
                                                                   : tr("Intersect");
```

The OCCT error text is no longer shown to the user. It is genuinely useful for debugging, so keep it out of sight but not lost — replace the previous `result.error` usage with:

```cpp
    qWarning("%s failed: %s", qPrintable(operationName), result.error.c_str());
```

Add `#include <QtGlobal>` for `qWarning` if it is not already available.

Also update the transient status line after a failure:

```cpp
    statusBar()->showMessage(tr("%1 failed — nothing was changed").arg(operationName));
```

- [ ] **Step 4: Rewrite the remaining tooltips**

In `src/MainWindow.cpp`:

```cpp
    myDeleteAction->setToolTip(tr("Delete the selected bodies (Del)"));
    myUndoAction->setToolTip(tr("Undo the last change to your bodies (Ctrl+Z)"));
    myRedoAction->setToolTip(tr("Redo the change you just undid (Ctrl+Y)"));
    mySnapAction->setToolTip(tr("Snap outline points to the 10 mm grid\n"
                                "Turn this off for freehand placement."));
    myItemsPanelAction->setToolTip(tr("Show or hide the list of bodies (Ctrl+Alt+S)"));
    myDisplayModeAction->setToolTip(tr("Draw bodies as edges only\n"
                                       "Useful for seeing through to what is behind."));
    myFitAction->setToolTip(tr("Frame every body in the viewport (F)"));
    myScreenshotAction->setToolTip(tr("Save the viewport as a PNG image"));
    myStartSketchAction->setToolTip(tr("Draw an outline on the ground (Ctrl+K)\n"
                                       "Click to place points; close it to make a face."));
    myFinishSketchAction->setToolTip(tr("Close the outline into a face (Enter)\n"
                                        "Needs at least three points."));
    myExtrudeAction->setToolTip(tr("Pull the face up into a solid body (E)"));
```

In `src/ui/ItemsPanel.cpp`, the eye button:

```cpp
        eye->setToolTip(tr("Show or hide this body"));
```

and the panel's empty state, added at the end of `refresh()` when there are no rows:

```cpp
    if (myRowCount == 0) {
        auto* empty = new QLabel(tr("No bodies yet.\n\nPress Ctrl+K and click points on "
                                    "the ground to draw your first outline."),
                                 this);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignTop);
        empty->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                 .arg(Theme::textMuted().name()));
        myRows->addWidget(empty);
    }
```

The empty label is added to `myRows`, so the existing wholesale rebuild disposes of it and `rowCount()` — which counts `myRowWidgets`, not layout children — is unaffected.

- [ ] **Step 5: Verify GREEN**

Kill processes, build, run gui_smoke and `ctest --preset windows-headless`.
Expected: gui_smoke `PASS (0 failures)`; headless 4/4.

- [ ] **Step 6: Commit**

```bash
git add src/MainWindow.cpp src/ui/ItemsPanel.cpp tests/gui_smoke.cpp
git commit -m "Rewrite the error dialogs and tooltips to name a cause and a fix"
```

---

### Task 6: Record the vocabulary and check the result

**Files:**
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-08-28-language-and-numbers-design.md`

- [ ] **Step 1: Add the vocabulary to CLAUDE.md**

Add a new section after the architecture table:

```markdown
### The vocabulary — enforced by test

One word per concept, everywhere. `gui_smoke` fails if any action text or widget
tooltip contains a banned word, so this table is executable, not aspirational.

| Concept | Word | Never |
|---|---|---|
| The 2D shape being drawn | outline | wire, polygon, polyline, sketch line |
| A closed outline, not yet 3D | face | profile, region |
| A 3D object in the document | body, `Body 03` | solid, `Solid #3`, shape, part |
| Combining two bodies | Union | fuse, merge, join, add |
| Removing one body from another | Subtract | cut, difference, boolean cut |
| Keeping the shared volume | Intersect | common, overlap, boolean common |
| Turning a face into a body | Extrude | pull, push, prism |
| The 3D area | viewport | scene, canvas, view |

`ModelingOps::BooleanKind::Fuse` and `::Cut` keep their kernel-facing names — the
user never sees them, and renaming them would churn the geometry library and its
tests for no visible gain.

Numbers are formatted by `Measure` (`src/Measure.h`), never by hand at a call
site: lengths as `340 mm` / `1,200 mm` / `18.5 mm`, sizes as `340 × 220 × 18 mm`.
Volume is not shown anywhere — furniture is specified by dimension.

Punctuation: status-bar text takes no trailing period; dialog bodies are full
sentences; tooltips lead with a fragment and may add one teaching sentence.
Clauses are separated by an em dash. Singular and plural are written out — no
`(s)` anywhere.
```

- [ ] **Step 2: Mark the spec implemented**

Change the spec's `Status:` line to `Status: implemented 2026-08-28`.

- [ ] **Step 3: Look at the result**

Kill app processes, build, run `.\build\RelWithDebInfo\gui_smoke.exe .` (expect `PASS (0 failures)`) and `ctest --preset windows-headless` (expect 4/4).

Then launch `.\build\RelWithDebInfo\furnifyme.exe`, wait four seconds, and capture the window as a still (`GetWindowRect` + `CopyFromScreen` — capture only; **no input injection**). Confirm by eye: the state label reads `Nothing yet — press Ctrl+K to draw an outline`, the items panel shows its empty-state text, and the Model menu is not visible in a still so its names are covered by the test instead. Report the screenshot path and what it shows.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-08-28-language-and-numbers-design.md
git commit -m "Record the enforced vocabulary"
```
