# Phase 4: Dimensions, Sketch Planes and Units Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a length while it is being made, let a planar face become the sketch plane, and let the user read the whole app in centimetres.

**Architecture:** `Measure` gains a display unit and a parser, converting only at the formatting boundary; one `DimensionRenderer` draws the annotation for both the live sketch segment and a hovered edge; `GridRenderer` and `SketchController` move from a hardcoded Z=0 to a supplied plane.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1, CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-08-29-dimensions-planes-units-design.md`

## A note on this plan's form

As with Phase 3, this plan gives **interfaces, behaviour contracts, exact user-facing
strings and exact test code** — the things that must not drift between tasks — and does not
transcribe widget bodies. The repository holds close prior art for every piece of this
phase, named in each task, and reading it beats transcribed pseudo-code. Phase 2's plan got
its verbatim code wrong three times.

## Global Constraints

- **`furnify_geometry` links no Qt.** `Measure`, `ModelingOps`, `DocumentModel`,
  `SketchController` and `CameraController` live there and must stay Qt-free — no `QString`,
  no `QSettings`. This is what keeps the headless suite buildable, and it is enforced
  structurally by the target's link line.
- **Vocabulary is settled.** Banned case-insensitively in any user-visible string: `Fuse`,
  `Solid`, `OCCT`, `mm3`, `(s)`, `Merge`, `Join`. A 3D object is a **body** (`Body 03`);
  operations are **Union / Subtract / Intersect**; the 2D shape is an **outline**; a closed
  outline is a **face**; the 3D area is the **viewport**. Plurals are written out. The suite
  sweeps action texts, tooltips and every `paintedTexts()` source.
- **Every user-facing length goes through `Measure`.** No call site formats a number itself.
  This is what makes the unit switch reach the whole app for free.
- **Millimetres everywhere except the formatting boundary.** The model, the kernel,
  `DocumentModel` and every stored value stay in millimetres. A unit that reaches the
  geometry is a unit that will eventually be applied twice.
- **Widgets over the viewport:** an interactive control is a **sibling** parented to
  `OcctViewWidget` with **derived** visibility and `Qt::WA_NoMousePropagation`, destroyed
  with its owner. Test hit-testing with `view->childAt(point)` against the real pointer.
  Placement is driven by `ViewportOverlay::laidOut()`, not by a widget's own resize filter —
  Phase 3 removed those precisely because filter order left widgets placed against stale
  geometry.
- `MainWindow::appStateChanged()` is emitted at the end of `updateActions()`. Slots may read
  state and repaint; calling back into `updateActions()` recurses.
- C++17. No new dependencies. No qtsvg, no Qt Test module.
- Never drive the GUI with OS-level synthetic input.
- Build: `cmake --build --preset windows`, then `.\build\RelWithDebInfo\gui_smoke.exe <dir>`
  directly (needs a GPU, not in ctest). Headless: `cmake --build --preset windows-headless
  && ctest --preset windows-headless`, 5/5. Kill `furnifyme.exe` and `gui_smoke.exe` before
  building or the link fails on a locked file.

---

### Task 1: `Measure` learns units

**Files:**
- Modify: `src/Measure.h`, `src/Measure.cpp`
- Test: `tests/measure.cpp`

**Interfaces produced:**
```cpp
namespace Measure {
enum class Unit { Millimetres, Centimetres };

void setDisplayUnit(Unit unit);
Unit displayUnit();                       // Millimetres by default

std::string formatLength(double millimetres);            // unchanged signature
std::string formatDimensions(const TopoDS_Shape& shape); // unchanged signature

// Parses a number the user typed **in the current display unit** and returns
// millimetres. False when the text is not a number, leaving `out` untouched.
bool parseLength(const std::string& text, double& out);

std::string unitSuffix();   // "mm" or "cm", for a label that needs it alone
}
```

**Behaviour contract:**
- `formatLength` **keeps taking millimetres**. Every existing call site already passes them,
  and changing the parameter's meaning would silently convert twice at any site missed. The
  unit lives in the formatter, not the argument.
- Centimetre rounding follows the millimetre rule: one decimal place, trailing `.0` dropped.
  18 mm reads `1.8 cm`; 340 mm reads `34 cm`; 4 mm reads `0.4 cm`.
- Thousands separators still apply in the value as displayed: 123,456 mm reads `12,345.6 cm`.
- The default is millimetres, so a caller that never sets the unit behaves exactly as before
  this phase.
- `parseLength` accepts a leading/trailing space and a leading `+` or `-`; it rejects `""`,
  `"abc"`, `"1.2.3"`, `"1,2"` and a bare `"."`, and must not modify `out` on rejection.

- [ ] **Step 1: Write the failing tests** in `tests/measure.cpp`:

```cpp
    // --- the display unit ---------------------------------------------------
    check(Measure::displayUnit() == Measure::Unit::Millimetres,
          "millimetres is the default, so an untouched caller is unaffected");
    check(Measure::formatLength(340.0) == "340 mm", "millimetres unchanged");

    Measure::setDisplayUnit(Measure::Unit::Centimetres);
    check(Measure::displayUnit() == Measure::Unit::Centimetres, "the unit round-trips");
    check(Measure::formatLength(0.0) == "0 cm", "zero has no decimal");
    check(Measure::formatLength(4.0) == "0.4 cm", "4 mm is 0.4 cm");
    check(Measure::formatLength(18.0) == "1.8 cm", "18 mm is 1.8 cm");
    check(Measure::formatLength(340.0) == "34 cm", "a whole value drops the .0");
    check(Measure::formatLength(1000.0) == "100 cm", "1,000 mm is 100 cm");
    check(Measure::formatLength(123456.0) == "12,345.6 cm",
          "thousands are separated in the displayed value");
    check(Measure::unitSuffix() == "cm", "the suffix follows the unit");

    // Input is read in the displayed unit, or a field that shows cm and reads
    // mm is a trap.
    double mm = 0.0;
    check(Measure::parseLength("4", mm) && mm == 40.0, "4 cm parses to 40 mm");
    check(Measure::parseLength(" 1.8 ", mm) && mm == 18.0, "spaces are tolerated");
    check(Measure::parseLength("-2", mm) && mm == -20.0, "a negative parses");

    double untouched = 99.0;
    check(!Measure::parseLength("", untouched) && untouched == 99.0,
          "empty is refused and leaves the value alone");
    check(!Measure::parseLength("abc", untouched) && untouched == 99.0, "letters refused");
    check(!Measure::parseLength("1.2.3", untouched) && untouched == 99.0,
          "two decimal points refused");
    check(!Measure::parseLength(".", untouched) && untouched == 99.0, "a bare point refused");

    Measure::setDisplayUnit(Measure::Unit::Millimetres);
    check(Measure::parseLength("4", mm) && mm == 4.0, "4 mm parses to 4 mm");
    check(Measure::formatLength(340.0) == "340 mm", "switching back restores exactly");
```

Also assert `formatDimensions` on the existing known box in centimetres, reusing whatever
shape that file already builds rather than constructing a second one.

- [ ] **Step 2: Verify RED** — `cmake --build --preset windows-headless`, expect a compile
  failure on `setDisplayUnit`.
- [ ] **Step 3: Implement**, keeping the file Qt-free and continuing to format by hand rather
  than through `std::locale`, whose output depends on the machine's regional settings.
- [ ] **Step 4: Verify GREEN** — `ctest --preset windows-headless`, 5/5.
- [ ] **Step 5: Commit.**

---

### Task 2: The unit setting in the app

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`, `src/ui/ExtrudePreview.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `Measure::setDisplayUnit`, `Measure::parseLength` (Task 1).
- Produces: a `View → Units` submenu with `Millimetres` and `Centimetres` in a
  `QActionGroup`, persisted through `QSettings` beside the learning progress, and
  `MainWindow::setDisplayUnit(Measure::Unit)` which sets the unit, refreshes every visible
  string, and records nothing.

**Behaviour contract:**
- Persistence uses the **same guard as the learning progress**: the constructor's
  `persistProgress` flag governs whether `QSettings` is touched at all, so the suite cannot
  read or write the developer's real store. `gui_smoke` already constructs
  `MainWindow(nullptr, false)`.
- Changing the unit refreshes the status bar, the items panel and any dimension label
  **immediately** — not on the next action. `updateActions()` already ends by emitting
  `appStateChanged()`; use it rather than inventing a second refresh path.
- **`ExtrudePreview` reads its field through `Measure::parseLength`**, so with centimetres
  selected typing `4` produces a 40 mm body. Its unit suffix in the panel follows
  `Measure::unitSuffix()`. This is the trap the phase must not ship.

- [ ] **Step 1: Write the failing test** in `tests/gui_smoke.cpp`, after the extrude blocks:

```cpp
    // --- the whole app reads in one unit --------------------------------------
    {
        QAction* mm = action(window, QStringLiteral("Millimetres"));
        QAction* cm = action(window, QStringLiteral("Centimetres"));
        check(mm != nullptr && cm != nullptr, "both units are offered");
        check(mm != nullptr && mm->isChecked(), "millimetres is the default");

        ItemsPanel* items = window.findChild<ItemsPanel*>();
        check(items != nullptr, "the items panel is present");
        const QString beforeItems = items ? items->rowTextAt(0) : QString();
        check(beforeItems.contains(QStringLiteral("mm")),
              QStringLiteral("the panel reads in millimetres (\"%1\")").arg(beforeItems));

        if (cm) {
            cm->trigger();
            settle(150);
            const QString afterItems = items ? items->rowTextAt(0) : QString();
            check(afterItems.contains(QStringLiteral("cm")),
                  QStringLiteral("the panel follows the unit (\"%1\")").arg(afterItems));
            check(!afterItems.contains(QStringLiteral("mm")),
                  "and no millimetre value is left behind");

            // The trap: a field that displays centimetres and reads millimetres.
            trigger(window, QStringLiteral("Start Sketch"));
            clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
            clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
            trigger(window, QStringLiteral("Finish Sketch"));
            settle(150);
            const int before = static_cast<int>(window.document().solids().size());
            trigger(window, QStringLiteral("Extrude..."));
            settle(150);
            ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
            if (preview && preview->field()) {
                preview->field()->setText(QStringLiteral("4"));
                settle(120);
                QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(preview->field(), &commit);
                settle(250);
            }
            check(static_cast<int>(window.document().solids().size()) == before + 1,
                  "the body was created");
            if (!window.document().solids().empty()) {
                const Measure::Extents e =
                    Measure::extentsOf(window.document().solids().back().shape);
                check(std::fabs(e.z - 40.0) < 1e-6,
                      QStringLiteral("4 typed in centimetres is 40 mm, not 4 (got %1)")
                          .arg(e.z));
            }

            mm->trigger();
            settle(150);
            check(items && items->rowTextAt(0).contains(QStringLiteral("mm")),
                  "switching back restores millimetres");
        }
    }
```

If `ItemsPanel` has no `rowTextAt`, add one — a read accessor for the suite is preferable to
walking its internals, and the panel already exposes its rows to itself.

- [ ] **Step 2: Verify RED.**
- [ ] **Step 3: Add the menu, the persistence and the refresh.**
- [ ] **Step 4: Route `ExtrudePreview` through `Measure::parseLength` and `unitSuffix`.**
- [ ] **Step 5: Verify GREEN**, full suite plus headless 5/5.
- [ ] **Step 6: Commit.**

---

### Task 3: The dimension renderer

**Files:**
- Create: `src/ui/DimensionRenderer.h`, `src/ui/DimensionRenderer.cpp`
- Modify: `CMakeLists.txt`, `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
class DimensionRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);

    // Draw the dimension for one segment, replacing whatever was drawn before.
    // `normal` orients the extension lines out of the segment. A segment
    // shorter than a hair draws nothing and leaves isShowing() false.
    void show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normal);
    void clear();
    bool isShowing() const;

    // The label's text, for the suite and the banned-word sweep. Empty when
    // nothing is shown.
    std::string labelText() const;
};
```

**Behaviour contract:**
- Built as OCCT presentation objects, **not** a `QPainter` overlay: the annotation lives in
  3D and stays correct as the camera moves. A painter overlay would need reprojecting every
  frame and would sit wrongly on a rotated view. `GridRenderer` is the prior art for
  building presentation objects from this side of the codebase.
- **The label text is `Measure::formatLength(distance)`** — never a local format call. That
  is what makes it follow Task 2's unit setting for free.
- Two call sites, one renderer:
  - **while sketching**, the segment from the last placed point to the cursor, updated on
    mouse move and cleared when the sketch commits or cancels;
  - **on hover and selection of an edge** in edge-selection mode, cleared when the hover
    leaves.
- The renderer holds no opinion about which case it is drawing. If it ever needs one, that
  is the signal the two cases have diverged and want separate renderers.

- [ ] **Step 1: Write the failing test** in `tests/gui_smoke.cpp`:

```cpp
    // --- a length you can see while you make it -------------------------------
    {
        trigger(window, QStringLiteral("Start Sketch"));
        settle(100);
        clickAt(view, QPointF(300, 300));
        moveTo(view, QPointF(420, 300));     // add this helper if absent
        settle(120);

        check(view->dimension().isShowing(),
              "dragging out a segment shows its length");

        // Computed independently: the renderer must not be its own oracle.
        const gp_Pnt a = window.sketch().points().front();
        gp_Pnt b;
        check(view->lastHoverPoint(b), "the cursor's ground point is known");
        const std::string expected = Measure::formatLength(a.Distance(b));
        check(view->dimension().labelText() == expected,
              QStringLiteral("the label reads the true distance (\"%1\" vs \"%2\")")
                  .arg(QString::fromStdString(view->dimension().labelText()))
                  .arg(QString::fromStdString(expected)));

        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(120);
        check(!view->dimension().isShowing(),
              "cancelling the outline clears the dimension");
    }
```

Add whatever small read accessors this needs (`dimension()`, `lastHoverPoint`,
`SketchController::points()` if absent). A read accessor for the suite is fine; a second
copy of the geometry is not.

- [ ] **Step 2: Verify RED.**
- [ ] **Step 3: Write `DimensionRenderer`.**
- [ ] **Step 4: Add to `CMakeLists.txt`** on `furnify_app`.
- [ ] **Step 5: Wire the sketch case**, updating on mouse move while sketching.
- [ ] **Step 6: Wire the edge case** — hover and selection in edge-selection mode. Note that
  `AIS_Shape` selection mode `2` is edges; the app currently activates `0` (whole shape) and
  `4` (faces). Adding edge mode is part of this step.
- [ ] **Step 7: Extend the banned-word sweep** to `labelText()`.
- [ ] **Step 8: Verify GREEN**, full suite plus headless 5/5.
- [ ] **Step 9: Commit.**

---

### Task 4: Locking a face as the sketch plane

**Files:**
- Modify: `src/GridRenderer.h`, `src/GridRenderer.cpp`, `src/MainWindow.h`,
  `src/MainWindow.cpp`, `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`
- Test: `tests/gui_smoke.cpp`, `tests/sketch_document.cpp`

**Interfaces:**
- Consumes: `SketchController::setPlane`, `SketchController::snapToPlaneGrid` — both already
  written and headless-tested for arbitrary planes.
- Produces: `GridRenderer::update(double cameraDistance, const gp_Pnt& target, const gp_Pln& plane)`;
  `MainWindow::lockToFace(const TopoDS_Face&)`, `MainWindow::unlockFace()`,
  `MainWindow::isFaceLocked()`.

**Behaviour contract:**
- `Lock to Face` (`L`) is enabled only when **exactly one planar face** is selected.
  `Unlock Face` (`Shift+L`) only while locked.
- A double-click on a face in face mode locks it.
- Locking sets `SketchController`'s plane to the face's plane, orients the grid to it, and
  reports it. **The plane is captured by value at lock time** — `CLAUDE.md` warns that face
  indices are not stable across a rebuild, so holding a reference to the face would let a
  later change move the sketch plane under the user.
- A **non-planar face cannot be locked**. `BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane`
  is the check. The refusal is a `Failure` toast naming the cause and the fix, in Phase 1's
  form: `"This face isn't flat, so it can't hold an outline. Pick a flat face and try again."`
- `Unlock Face` returns the plane to the ground plane (XY at Z=0). The ground plane is the
  default and is never itself "locked".
- Locking records `faceLock.used` through `recordProgress`, so a hint can teach it and then
  go quiet the way Phase 2 established.
- `GridRenderer`'s adaptive step, three concentric bands and distance fade are **unchanged** —
  only the frame they are built in moves. `minorStepFor()` and `firstLineAtOrBelow()` keep
  their signatures and their existing tests.

- [ ] **Step 1: Write the failing headless test** in `tests/sketch_document.cpp`:

```cpp
    // A point unprojected onto a non-XY plane must land on that plane, and
    // snapping must not lift it off.
    {
        const gp_Pln vertical(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));   // the XZ plane
        const gp_Lin ray(gp_Pnt(50, -100, 30), gp_Dir(0, 1, 0));
        gp_Pnt hit;
        check(SketchController::intersectRayWithPlane(ray, vertical, hit),
              "a ray meeting a vertical plane intersects it");
        check(std::fabs(vertical.Distance(hit)) < 1e-9,
              "the hit lies on the plane it was cast at");

        const gp_Pnt snapped = SketchController::snapToPlaneGrid(hit, vertical, 10.0);
        check(std::fabs(vertical.Distance(snapped)) < 1e-9,
              "snapping keeps the point on its own plane");
    }
```

- [ ] **Step 2: Write the failing `gui_smoke` test:**

```cpp
    // --- a flat face can become the sketch plane ------------------------------
    {
        trigger(window, QStringLiteral("Select Faces"));
        settle(120);
        // Pick a face on an existing body, then lock it.
        clickAt(view, QPointF(360, 340));
        settle(150);

        QAction* lock = action(window, QStringLiteral("Lock to Face"));
        check(lock != nullptr, "there is an action to lock a face");
        check(lock != nullptr && lock->isEnabled(),
              "selecting one flat face enables it");
        if (lock && lock->isEnabled()) {
            lock->trigger();
            settle(200);
            check(window.isFaceLocked(), "the face is locked");

            // The sketch plane must BE the face's plane, not merely something.
            const TopoDS_Face face = view->selectedFace();
            check(!face.IsNull(), "the locked face is retrievable");
            if (!face.IsNull()) {
                const gp_Pln facePlane = BRepAdaptor_Surface(face).Plane();
                const gp_Pln sketchPlane = window.sketch().plane();
                check(sketchPlane.Axis().Direction().IsParallel(
                          facePlane.Axis().Direction(), 1.0e-7),
                      "the sketch plane is oriented like the face");
                check(std::fabs(facePlane.Distance(sketchPlane.Location())) < 1.0e-6,
                      "and sits on it");
            }

            QAction* unlock = action(window, QStringLiteral("Unlock Face"));
            check(unlock != nullptr && unlock->isEnabled(), "it can be unlocked");
            if (unlock) {
                unlock->trigger();
                settle(150);
                check(!window.isFaceLocked(), "unlocking releases it");
                check(std::fabs(window.sketch().plane().Location().Z()) < 1.0e-9,
                      "and returns to the ground plane");
            }
        }
    }
```

- [ ] **Step 3: Verify RED** on both.
- [ ] **Step 4: Extend `GridRenderer::update` to take a plane.** Keep the band maths; change
  only the frame.
- [ ] **Step 5: Add the actions, the lock state and the double-click route.**
- [ ] **Step 6: Refuse a non-planar face** with the exact string above, through the toast.
- [ ] **Step 7: Verify GREEN**, full suite plus headless 5/5.
- [ ] **Step 8: Commit.**

---

### Task 5: Document the result

**Files:** Modify `CLAUDE.md`, `docs/superpowers/specs/2026-08-29-dimensions-planes-units-design.md`

- [ ] **Step 1:** Add a `### Dimensions, planes and units` section to `CLAUDE.md`: that
  millimetres are the only unit anything stores and the conversion happens in `Measure`
  alone; that input is read in the displayed unit; that one renderer serves both dimension
  cases and why; that a locked plane is captured by value because face indices are not
  stable across a rebuild; and that arbitrary sketch planes are **no longer deferred** —
  update the sentence in the sketch section that says they come later.
- [ ] **Step 2:** Add the new files to the architecture table and add `Lock to Face` /
  `Unlock Face` to any shortcut listing.
- [ ] **Step 3:** Mark the spec `Status: implemented 2026-08-29`.
- [ ] **Step 4:** Build, run both suites, and capture the app with `PrintWindow`
  (`PW_RENDERFULLCONTENT`) — **capture only, never inject input**; a screen-region grab
  catches whatever window is on top. Confirm a dimension label is legible and the grid sits
  on a locked face. Report the path and what it shows.
- [ ] **Step 5: Commit.**
