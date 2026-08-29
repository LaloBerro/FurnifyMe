# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`FurnifyMe` — a cross-platform (Windows + Linux, both first-class) desktop CAD app with
direct-modeling interaction, in the spirit of Shapr3D. C++17 / CMake / Qt 6 Widgets /
OpenCascade (OCCT) 7.6+. CMake project `FurnifyMe`; app target `furnifyme`; geometry
library target `furnify_geometry`.

This file distills the project brief (`CAD_APP_BRIEF.md`, supplied at init; ask the user
for it if you need the verbatim original). Milestone 1 scope is exactly: **sketch → extrude → boolean**,
plus STEP export. Fillets, chamfers, push/pull on faces, history/parametric tree,
constraint solver, 2D drawings, assemblies, materials, and any file format beyond STEP are
out of scope until Milestone 1 runs clean on both platforms.

### Stack decisions — settled, do not re-litigate

- Geometry kernel is OCCT. Viewport is **OCCT's own** `V3d_View` + `AIS_InteractiveContext`.
  Do **not** write a custom renderer — OCCT gives you B-rep tessellation, an OpenGL driver,
  camera control, hover highlighting, and topological picking (hit a NURBS face, get back the
  actual `TopoDS_Face`).
- Qt 6 **Widgets**, not QML.

## Build & run

### Windows

Already provisioned on this machine: CMake 4.4.2 (winget `Kitware.CMake`), vcpkg at
`C:cpkg`, MSVC 2022 Community + Windows SDK 10.0.22621. Dependencies were installed with:

```powershell
C:cpkgcpkg.exe install opencascade `
  "qtbase[core,thread,gui,widgets,opengl,freetype,harfbuzz,png,jpeg,zstd,doubleconversion,pcre2]" `
  --triplet x64-windows-release --host-triplet x64-windows-release --clean-after-build
```

**Both triplet flags matter.** `x64-windows-release` skips every debug build (~half the
work), and `--host-triplet` must match it — otherwise vcpkg treats the build as
cross-compiling and builds a *second*, full-featured qtbase (ICU, OpenSSL, PostgreSQL) for
the host just to get moc/rcc/uic. Always check the plan with `--dry-run` before a long
install.

The `qtbase[core,...]` list is deliberate: `core` is vcpkg's "no default features" marker,
so it drops ICU, OpenSSL, the SQL drivers, dbus, brotli, dnslookup and testlib — none of
which a Widgets + OCCT app needs.

**Consequence of release-only deps:** no debug OCCT/Qt binaries exist, so the app cannot be
built in the `Debug` configuration — MSVC's debug CRT against release dependencies is a
runtime mismatch. Use `RelWithDebInfo`, which still gives full symbols for our own code. If
stepping into OCCT internals ever becomes necessary, install `opencascade:x64-windows`
alongside and configure a separate build dir against that triplet.

Configure and build through the presets — they pin the toolchain file and both triplets:

```powershell
cmake --preset windows
cmake --build --preset windows
```

Fallback if the vcpkg route is ever abandoned: the official prebuilt OCCT installer from
dev.opencascade.org plus the Qt online installer, then point `OpenCASCADE_DIR` and
`Qt6_DIR` at them manually.

### Dependency versions actually installed

vcpkg resolved **OCCT 8.0.1** and **Qt 6.11.1** — both well past the brief's 7.6+ baseline.
OCCT 7.8 renamed the data-exchange toolkits (`CMakeLists.txt` branches on that), but 8.0 may
have merged or renamed others. **If a `TK*` target fails to resolve on first configure,
check the real names** in `C:/vcpkg/installed/x64-windows-release/lib/TK*.lib` rather than
trusting the brief's list.

### Linux

```bash
sudo apt install -y build-essential cmake \
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
  libocct-data-exchange-dev libocct-visualization-dev qt6-base-dev libgl1-mesa-dev
cmake --preset linux && cmake --build --preset linux
```

### Current state

**Kernel integration and the core UI loop are verified on Windows** (MSVC 19.38, OCCT
8.0.1, Qt 6.11.1).

Verified by the two headless tests (49 checks, all passing):
wire -> face -> prism -> cut -> exact face counts and volumes -> 494-entity `out.step`;
ray/plane unprojection including the parallel-ray case; sketch accumulation; document id
lifecycle; compound building.

Verified by driving the running app and reading the screenshots:
viewport renders with grid, triedron and lighting; sketch mode draws a live yellow polyline
and reports clicked points at exactly `Z = 0.00`; closing produces a filled face; extrude
produces a shaded solid with a plausible volume; clicking a solid selects it (status bar
reports `1 solid(s) selected`).

Also verified through the UI: hover highlight (cyan), selection (orange), and a **two-solid
Cut**, checked by arithmetic rather than by eye - a 1,113,000 mm3 slab minus a 748,000 mm3
block left 926,000 mm3, and the 187,000 mm3 removed is exactly the tool's 18,700 mm2
footprint times the slab's 10mm thickness. Both operands were replaced by the single result.

Face-selection mode is confirmed too: hovering outlines a single face, and the outline
follows the hole through the cut solid, so it is the real `TopoDS_Face` and not the whole
shape.

STEP export via the UI produces a well-formed file - AP214 (`AUTOMOTIVE_DESIGN`), 658
entities, one `MANIFOLD_SOLID_BREP`, and exactly 10 `ADVANCED_FACE` entries, which is what a
slab with a rectangular through-hole should have (top, bottom, 4 outer sides, 4 hole sides).
The exported topology therefore matches what is on screen.

**Not yet verified:**
- **Opening a STEP file in FreeCAD.** The file is structurally correct, but "opens correctly
  in FreeCAD" is the actual acceptance criterion and FreeCAD is not installed here
  (`winget install FreeCAD.FreeCAD`).
- **Anything at all on Linux** - never configured, built or run. This is the largest
  remaining gap in Milestone 1, since both platforms are first-class.

**Both are parked by the user's decision (2026-08-28): Windows-only focus for now.** Neither
FreeCAD nor WSL is installed and neither should be installed without being asked for. Do not
re-raise these as blockers; keep them listed as unverified, and treat "Milestone 1 complete"
as a claim that cannot honestly be made until they are done.

### Driving the GUI: use `gui_smoke`, not synthetic OS input

`tests/gui_smoke.cpp` drives the real `MainWindow` by delivering Qt events
**straight to the widgets** with `QCoreApplication::sendEvent`, and by calling
`QAction::trigger()`. Nothing goes through the OS input queue, so it never moves the cursor
or steals focus - the machine stays usable while it runs. It finishes in seconds and covers
what the headless tests cannot: that clicks become sketch points, that picking returns the
right solids, that the document and viewport stay in agreement across delete/undo/redo, and
that a two-solid Cut works end to end.

Screenshots come from `V3d_View::Dump` via `OcctViewWidget::saveSnapshot()`, which renders
the viewport to a file. It captures only the 3D view, regardless of what is on top of the
window. **View shortcuts:** keys `0`–`3` trigger Axonometric, Top, Front, Right views with
smooth animation; `gui_smoke` disables animations at startup for deterministic camera state
in the test suite.

```powershell
cmake --build --preset windows
.\build\RelWithDebInfo\gui_smoke.exe <output-dir-for-snapshots>
```

A window still appears - OCCT's `V3d_View` needs a real native window and a GL surface, so
`-platform offscreen` cannot work - but it is shown with `WA_ShowWithoutActivating` and
never takes focus. `gui_smoke` is deliberately **not** registered with ctest: it needs a GPU
and a window server, while the headless tests must stay runnable anywhere, including CI.

**Do not go back to `SetCursorPos`/`mouse_event` PowerShell scripts.** That approach cost far
more time than it was worth: `GetWindowRect` reported a 1500x2900 window on a 1920x1080
screen (DPI virtualization between the driving process and the app), clicks landed outside
unmaximized windows, and toolbar pixel positions silently went stale the moment a button was
added - which produced a false bug report. It also holds the machine hostage while it runs.

### Tests

`tests/headless_geometry.cpp` needs no window and no GPU — it links only the modeling
toolkits and runs on any box, including CI.

```bash
cmake --preset windows-headless      # or linux-headless
cmake --build --preset windows-headless
ctest --preset windows-headless
```

The `*-headless` presets set `FURNIFYME_BUILD_APP=OFF`, building the geometry library and
the test with no Qt involved at all — the fastest loop for kernel work, and what CI should
run. They use a separate `build-headless/` dir so they never fight with the app build.

**Pass this test before writing a single line of Qt code.** It asserts:
square wire → face → 10mm prism → second offset box → cut → expected face/solid counts via
`TopExp_Explorer` → positive, sane volume via `BRepGProp::VolumeProperties` → `out.step`
written and non-empty. Once it passes, kernel integration is proven correct and **every
later bug is a UI bug**. That separation is the point.

## Architecture

Source files under `src/`, plus `tests/`:

| File | Role |
|---|---|
| `ModelingOps.{h,cpp}` | pure geometry, **zero Qt includes** |
| `CameraController.{h,cpp}` | turntable camera maths, Qt-free, headless-tested |
| `Measure.{h,cpp}` | every user-facing length and size string, Qt-free, headless-tested |
| `UserProgress.{h,cpp}` | counts what the user has done; Qt-free, storage injected |
| `GridRenderer.{h,cpp}` | adaptive fading ground grid (app layer) |
| `main.cpp` | `QApplication` + `MainWindow` |
| `MainWindow.{h,cpp}` | menus, toolbar, mode switching |
| `OcctViewWidget.{h,cpp}` | the Qt↔OCCT bridge — the only genuinely tricky file |
| `DocumentModel.{h,cpp}` | owns the list of solids |
| `SketchController.{h,cpp}` | 2D input → wire on a plane |
| `ui/Theme.{h,cpp}` | colour tokens + the app stylesheet; the only place hex lives |
| `ui/IconSet.{h,cpp}` | glyphs painted with QPainter (no qtsvg installed) |
| `ui/ToolChip.{h,cpp}` | a button built from a `QAction`, never storing its own state |
| `ui/ToolCluster.{h,cpp}` | a vertical stack of chips |
| `ui/ViewportOverlay.{h,cpp}` | anchors clusters to viewport edges; not a widget |
| `ui/ItemsPanel.{h,cpp}` | solid list with visibility toggles |
| `ui/AxisGizmo.{h,cpp}` | Unity-style orientation gizmo; each axis tip snaps the view |
| `ui/WalkthroughPanel.{h,cpp}` | the guided first build; steps derived from live state |
| `ui/HintBalloon.{h,cpp}` | one hint at a time, retired when its trigger stops holding |
| `ui/ShortcutSheet.{h,cpp}` | shortcut list generated from the window's own `QAction`s |
| `ui/Toast.{h,cpp}` | one non-blocking message at a time, with Undo where it applies |
| `ui/ExtrudePreview.{h,cpp}` | height entry with a live preview built by the commit's own path |

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
tests for no visible gain. The enforced bans match the bare word (case-insensitive):
`OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, and `Join` are forbidden everywhere
in action text and widget tooltips, regardless of capitalization.

Numbers are formatted by `Measure` (`src/Measure.h`), never by hand at a call
site: lengths as `340 mm` / `1,200 mm` / `18.5 mm`, sizes as `340 × 220 × 18 mm`.
Volume is not shown anywhere — furniture is specified by dimension.

Punctuation: status-bar text takes no trailing period; dialog bodies are full
sentences; tooltips lead with a fragment and may add one teaching sentence.
Clauses are separated by an em dash. Singular and plural are written out — no
`(s)` anywhere.

**Hard rule: `ModelingOps` must not include a single Qt header.** That invariant is what
makes the headless test possible; breaking it collapses the whole testability story. It is
enforced structurally: `ModelingOps` lives in the `furnify_geometry` target, which does not
link Qt and does not link the visualization toolkits (`TKV3d`, `TKOpenGl`, `TKService`) —
those are on the `furnifyme` app target only.

`applyBoolean()` returns a `BooleanResult{ok, shape, error}` rather than a bare shape,
specifically so a failed boolean cannot be mistaken for a success. Surface `error` in the
UI; never continue past `ok == false`.

### Teaching surfaces, and how they go quiet

`UserProgress` (`src/UserProgress.h`, Qt-free) counts what the user has actually done.
Three completions of an action means it is learned, and that action's hint never appears
again. **Storage is injected, not built in:** `MainWindow` persists `serialize()` through
`QSettings`, while `gui_smoke` constructs `MainWindow(nullptr, false)` and never touches
the real store. A suite whose result depended on how often the developer had run the app
would not be a suite. The one place the suite does exercise persistence — the
returning-user path, which is decided inside the constructor before `progress()` is
reachable — goes through `ScopedTestSettings`, an RAII guard that swaps in an ini file
under a temp directory so nothing reaches the registry.

Four surfaces teach, and the counter governs all of them:

| Surface | Appears | Goes quiet |
|---|---|---|
| `WalkthroughPanel` | first run, bottom right of the viewport | on completion or skip |
| `HintBalloon` | when a capability first becomes available this session | after 3 completions |
| `ShortcutSheet` | on demand, `?` or `F1` | never — it is on-demand |
| Tooltips | on hover | never |

`Help → Show tips again` resets the store and brings the first two back, which is how you
demonstrate the app to somebody without reinstalling it. It genuinely restores, and that
takes two things, not one. The walkthrough panel is **always constructed** and decides its
own visibility, because a panel built only for un-learned users cannot be restored without
a restart. And the store is not the only state involved: `HintBalloon` also remembers which
hints it has already shown *this session*, which clearing the store cannot reach — so the
handler emits `progressReset()` before `appStateChanged()`, and the balloon drops that
memory itself. `MainWindow` announces the reset; it never touches another surface's
internals.

Three rules hold this together, and each was learned by getting it wrong first:

- **Derive state; never store a cursor.** `WalkthroughPanel` recomputes which steps are
  satisfied from live application state on each `appStateChanged()`. A stored cursor is a
  second source of truth and it drifts. The one piece of remembered state is
  `myBodyBaseline`, a per-session body count captured whenever the panel transitions into
  showing, so step 4 means "a body was made since the guide appeared" rather than "a body
  exists" — without it, `Show tips again` re-completes the walkthrough the instant it
  restores it.
- **A hint retires when its own trigger stops holding.** One predicate, `conditionHolds()`,
  both raises a hint and retires it, so the two can never drift apart. That single rule
  covers the spec's "dismissed by performing the action" and "dismissed by the capability
  going away" at once. Note that it only runs when something emits `appStateChanged()` —
  wiring a new hint means checking that the state it watches actually emits. Each predicate
  reads the **recorded event**, never a piece of state that merely tends to accompany it:
  the view hint used to retire on `AxisGizmo::labelText() != "Persp"`, and pressing `0`
  records `view.changed` while leaving a camera pose the gizmo still calls `Persp`, so the
  balloon sat there after the user had done exactly what it taught. Every route that changes
  the camera to a named direction — the four View entries and a click on the gizmo — now
  goes through `MainWindow::recordViewChanged()`, which records *and* calls
  `updateActions()`. `AxisGizmo` emits `viewSnapped()` and knows nothing about `MainWindow`.
- **Generate documentation, never write it twice.** `ShortcutSheet` builds its rows from
  the window's own `QAction`s, so it cannot list a stale binding, and `gui_smoke` asserts
  the row count against the same enumeration. The grouping is generated too — rows sit under
  the menu that owns them, read off the menu bar, with a catch-all group so an action bound
  outside the menus can never be silently dropped. Painted copy is invisible to the
  banned-word sweep, so `WalkthroughPanel::paintedTexts()`, `HintBalloon::paintedTexts()`
  and `ShortcutSheet::paintedTexts()` expose it — and all three are sourced from the same
  strings the widget paints, never a second copy that only the sweep sees.

`MainWindow::appStateChanged()` fires at the end of `updateActions()`. Slots on it may read
state and repaint themselves; calling back into `updateActions()` from one would recurse.

#### Widgets over the viewport: two traps

Both cost a fix round, and both produced a green test suite while the app was broken.

- **`Qt::WA_TransparentForMouseEvents` excludes the widget *and its entire subtree*** from
  hit-testing — `QWidgetPrivate::childAtRecursiveHelper` skips past it. An interactive
  control that is a *child* of a transparent overlay is unreachable by any real click. The
  walkthrough's skip control is therefore a **sibling** parented to `OcctViewWidget`,
  positioned from the panel's `moveEvent`/`showEvent` and destroyed with it.
- **Test hit-testing with `view->childAt(point)`, compared against the actual control
  pointer.** Asserting an attribute flag, or `sendEvent`-ing straight at the widget you hope
  is reachable, passes against a control no user can click. The same applies to visibility:
  assert `isVisible()`, or a stub that sets the text and never calls `show()` sails through.

A widget that accepts a press must accept the release too. `HintBalloon` left the release to
propagate, and the viewport underneath re-picked and re-emitted `selectionChanged()` on
every dismissal. `Qt::WA_NoMousePropagation` closes that whole class of event rather than
enumerating handlers one at a time. **Every sibling control over the viewport carries it** —
`UndoControl`, `SkipControl`, `ExtrudePreview`'s field. `SkipControl` was the one that did
not, so clicking "skip" on first run also selected whatever body sat behind the guide; the
check is a count of `selectionChanged()` emissions across a press *and its release*, since
a press-only probe never reproduces it. `ShortcutSheet` dismisses on a click *outside* itself,
which it can only see through an application-wide event filter — the press lands on the
viewport, never on the sheet or its parent. Swallowing that press is not enough either: the
viewport picks on the **release**, so the filter stays installed one event longer than the
sheet is visible, specifically to swallow it.

Three widgets share the viewport's bottom edge — the guide bottom-right, the balloon and the
toast centred — plus the Snap/Select chip cluster bottom-left, and they collide below about
900 px of viewport width, which `Show tips again` makes reachable. `HintBalloon::reposition()`
steps aside when the guide is visible and would overlap; `reconsider()` calls it for a hint
that is already up, because that is exactly when the guide can appear underneath one.

`ToastHost::reposition()` steps around **everything the overlay has anchored**, asked for as
`ViewportOverlay::occupiedRects()` rather than by naming the widget classes this file happens
to know — a cluster added later is then stepped around for free. It solves one horizontal
band at a time: an obstacle to the left raises the floor, one to the right lowers the
ceiling, and when the two meet the search moves to the band above and asks again, because the
row above can be just as occupied as the row below. A per-obstacle "step left" rule cannot
express that, and stepping left is exactly the wrong move for the bottom-**left** cluster.

**Order the re-placement off `ViewportOverlay::laidOut()`, never off filter registration.**
Qt runs event filters last-installed-first and the overlay installs its own first, so every
widget that watched the viewport's resize event ran *before* the guide and the clusters had
moved: on a shrink the toast and the balloon stepped aside from where the guide used to be
and the guide then landed on top of them, and `ExtrudePreview` was raised before `relayout()`
re-raised the top-left cluster over its field. `relayout()` now emits `laidOut()` when every
anchored entry is at its final rectangle, and `ToastHost::replace()`,
`HintBalloon::reposition()` and `ExtrudePreview::replace()` hang off that — re-placing **and**
re-raising. `Show tips again` calls `relayout()` itself after its `appStateChanged()`, so a
guide that reappears underneath a live toast moves it aside without depending on which slot
happened to run first.

A sibling's visibility must be **derived, not inherited from an event**. The walkthrough's
skip control is synced from the panel's `showEvent`/`hideEvent`/`moveEvent`, and on the
returning-user path `refresh()` calls `hide()` on a panel that was never shown — a case Qt
delivers no `QHideEvent` for. `syncSkipGeometry()` therefore sets `mySkip->setVisible(isVisible())`
itself, so the control's hidden state never depends on an event arriving.

### Reporting outcomes

**The app has no modal dialogs.** Nothing it has to say requires an answer, so nothing
blocks. Seven `QMessageBox` calls and one `QInputDialog` were removed and none should come
back; `gui_smoke` asserts the window holds no `QDialog` after an outcome, and the honest
backstop is that a modal would *hang* the suite rather than fail it politely.

`ToastHost` owns exactly one `Toast`. A second message replaces the first and restarts the
timer — a stack of toasts is a dialog with extra steps. A `Note` lives 4 seconds and a
`Failure` 8, because a failure carries a sentence the user has to read and act on; the
suite asserts both against the armed timer rather than waiting real seconds.

**A toast that reports a change to the document offers Undo**, and that control is a
sibling parented to `OcctViewWidget` for the reason recorded above. Two things about it
were each found by a review after passing a green suite:

- **The fade made Undo double-clickable.** With animations on — the shipping default —
  `dismiss()` fades rather than hides, so the control stayed live for 160 ms, inside the
  system double-click interval, and a second click popped the undo stack again. It is now
  hidden at the top of `dismiss()`, before the fade starts.
- **A hide is not a state.** `syncUndoGeometry()` re-derives visibility from the toast's
  own flags, and `ToastHost::reposition()` runs on any viewport resize — so a resize
  landing mid-fade re-showed the control `dismiss()` had just hidden. `myDismissing` is now
  part of the predicate. The rule from the sibling-visibility paragraph applies one level
  down: derive it, or something later will undo your one-shot.

Two more, found only once the whole branch was assembled:

- **The pill is not a fourth entry point.** It triggers `myUndoAction`, so it obeys the same
  `!mySketching && canUndo()` that `updateActions()` — CLAUDE.md's single place that decides
  what is available — sets for the menu entry, the chip and `Ctrl+Z`. `updateActions()` also
  pushes that state onto the toast (`setUndoEnabled`), which folds it into the same derived
  predicate as `myDismissing`: the control leaves hit-testing and the pill paints dimmed,
  rather than looking live and doing nothing.
- **A toast must not outlive the change it names.** `show()` records
  `DocumentModel::revision()` and `documentMovedTo()` dismisses the toast when it moves, so
  "Deleted Body 02 — Undo" cannot survive a `Ctrl+Z` and then pop the *previous* checkpoint.
  `revision()` is monotonic and never rolled back; an undo is a move, not a return.
- **`Toast::paintedTexts()` records every message shown this run**, not just the live one —
  the banned-word sweep was otherwise a coin toss over whichever string happened to be up.
  A message never triggered during a run is still not covered, and the comment says so.

**`ExtrudePreview` must build its preview through the same `ModelingOps::extrude` the
commit uses.** A preview built by a different path is a lie, and this is the one place a
user judges a number by what it looks like. The preview shape goes through
`OcctViewWidget::setPreview` with selection mode `-1` and is **never** added to
`DocumentModel` — a shape the user can see that exists in no document is the worst thing
this widget could produce. It cancels itself off `appStateChanged()` when the pending face
goes away, because `Start Sketch` stays enabled while it is open. Invalid input keeps the
last good preview rather than clearing it: no flicker, and `0` is refused while a negative
extrudes downward, matching what the old dialog's range allowed.

**It owns Enter and Escape while it is visible, regardless of what holds focus** — an
application-wide filter installed on `show` and removed on `hide`, plus a
`QEvent::ShortcutOverride` claim so `QShortcutMap` cannot take the key first. This is
`ShortcutSheet`'s shape, and it is not optional: the field-only filter it replaced stopped
working the moment anything else took focus, and an RMB orbit — the entire reason a *live*
preview exists — does exactly that. The panel also names both keys in painted text; a
modeless panel with invisible verbs is how that went unnoticed for a whole branch.

**Cancelling restores the face; it does not clear the slot.** `OcctViewWidget` has one
preview channel and two features write it — `onFinishSketch()` puts the closed face there,
`updatePreview()` overwrites it with the body. Clearing on cancel left an intact pending
face, an enabled `Extrude` and a status bar saying "Outline closed" above an empty viewport,
against Milestone 1's own criterion. `OcctViewWidget::previewShape()` exists so a test can
tell *which* of the two is on screen, since `hasPreview()` cannot.

**Type, motion and focus are `Theme` tokens** — four font sizes and no more, `motionMs()`
at 160 with `motionCurve()`. `gui_smoke` walks every visible widget and fails on a size
outside the scale, so the sizes are set through the stylesheet `Theme::apply` installs and
inherit into Qt's own children. Note that Qt 6's `QStatusBar` has **no** internal `QLabel`:
`showMessage()` stores a string and `paintEvent` draws it with the status bar's own font,
so `statusBar()->setFont(...)` is what covers it.

`OcctViewWidget`'s camera animation deliberately keeps its own 250 ms and does not read
`motionMs()`. A camera move is not a UI transition; reading well at the same speed as a
chip hover would be a coincidence, not a rule.

**Measure text with the font you paint it with.** Bold is wider than regular, and a title
measured non-bold and painted bold clips. Widgets that size themselves — `WalkthroughPanel`
most of all — derive their width from the same strings `paintedTexts()` exposes, so a new
line cannot silently exceed the box.

**Two Qt facts this phase paid for.** `QGraphicsOpacityEffect` is incompatible with a
widget painted over `OcctViewWidget`'s on-screen GL surface — it crashes; fade with
`QPainter::setOpacity()` instead. And `QAbstractAnimation::DeleteWhenStopped` deletes the
animation on *natural completion* too, so a retained raw pointer dangles; one long-lived
animation at `KeepWhenStopped` removes the question rather than detecting it.

### Qt plugin deployment - do not remove

Qt will not start without a platform plugin, and it looks for one in a `platforms/`
directory next to the executable, not alongside the Qt DLLs. vcpkg's applocal deployment
copies DLLs but **not** plugins, and the `windeployqt` feature is deliberately not installed
(it would have dragged the full default feature set back in). `CMakeLists.txt` therefore
copies `QWindowsIntegrationPlugin` and `QModernWindowsStylePlugin` itself in a POST_BUILD
step. Delete that and the app dies at startup with
`could not find the Qt platform plugin "windows"`.

### The shell is action-driven

Every tool chip is constructed from a `QAction` and mirrors it - enabled state,
checked state, label, shortcut. Never give a chip its own state: menus, chips and
shortcuts would drift, and `gui_smoke` finds actions by text, so the chips are covered
for free. `updateActions()` remains the single place that decides what is available.

Cluster widgets are **direct children of `OcctViewWidget`**. A probe confirmed Qt
composites plain children over OCCT's OpenGL surface correctly on Windows; a translucent
container was deliberately avoided as the least reliable variant of that.

### CMake note

OCCT 7.8 renamed the data-exchange toolkits — `CMakeLists.txt` branches on
`OpenCASCADE_VERSION` (`TKDESTEP`/`TKDESTL` for ≥7.8, `TKSTEP`/`TKSTL` below). Modeling
toolkit names are unchanged across those versions.

### `OcctViewWidget` — the bridge

Construction order: `Aspect_DisplayConnection` → `OpenGl_GraphicDriver` → `V3d_Viewer`
(`SetDefaultLights()` + `SetLightOn()`) → `viewer->CreateView()` → `AIS_InteractiveContext`.

Native window attach is platform-specific: `WNT_Window(winId())` on Win32, `Xw_Window(disp,
winId())` elsewhere; then `SetWindow(wind)` and `Map()` if not mapped.

Required `QWidget` setup — omitting any of these gives flicker or a black viewport:
`WA_PaintOnScreen`, `WA_NoSystemBackground`, `WA_OpaquePaintEvent`,
`setAutoFillBackground(false)`, `setMouseTracking(true)` (needed for hover highlight), and
`paintEngine()` overridden to return `nullptr`.

Event wiring: `paintEvent`→`Redraw()`, `resizeEvent`→`MustBeResized()`, RMB drag→turntable orbit around the current view target (Unity-style, the user's explicit preference — no cursor-anchored pivoting), MMB drag→pan, wheel→zoomToward cursor; camera state lives in CameraController and is pushed via SetEye/SetCenter/SetUp; the projection is perspective (FOVy 45°).

### Selection

`AIS_Shape` selection modes are integers: `0` whole shape, `1` vertex, `2` edge, `3` wire,
`4` face, `5` shell, `6` solid. `Deactivate(shape)` then `Activate(shape, 4)` for faces.
Iterate with `InitSelected()`/`MoreSelected()`/`NextSelected()`, pull topology via
`SelectedShape()`.

### The modeling loop

- **Sketch:** unproject the click with `view->ConvertWithProj(...)` into a `gp_Lin`, then
  intersect with the sketch plane via `IntAna_IntConicQuad`. Start with a fixed XY plane at
  Z=0; arbitrary planes come later. Accumulate points into `BRepBuilderAPI_MakePolygon`,
  `Close()`, then `BRepBuilderAPI_MakeFace(wire, true)`. Show the in-progress polyline as a
  temporary `AIS_Shape` and remove it on commit.
- **Extrude:** `BRepPrimAPI_MakePrism(face, gp_Vec(plane.Axis().Direction()) * height)`.
- **Boolean:** `BRepAlgoAPI_Cut`/`_Fuse`/`_Common` with `SetRunParallel(true)` and
  `SetFuzzyValue(1.0e-5)`. Always follow with `ShapeUpgrade_UnifySameDomain(result, true,
  true, true)` — it merges the coplanar faces the boolean leaves behind. Skip it and the
  model accumulates junk edges that make later selection miserable.
- **STEP export:** `STEPControl_Writer` with
  `Interface_Static::SetCVal("write.step.schema", "AP214IS")`.

## Pitfalls (read before debugging)

- **Wayland breaks the native window handle.** `winId()` under Wayland gives OCCT something
  it cannot use. Force XCB: `qputenv("QT_QPA_PLATFORM", "xcb")` before constructing
  `QApplication`, or run with `QT_QPA_PLATFORM=xcb`. This costs an afternoon if unknown.
- **`paintEngine()` must return `nullptr`** or Qt and OpenGL fight over the surface.
- **Never `delete` an OCCT handle.** `Handle(Foo)` is refcounted; let it go out of scope.
- **`Handle()` is a macro** that collides with some Windows headers. Include OCCT headers
  before `<windows.h>` where possible.
- **Tessellate before display or STL export:** `BRepMesh_IncrementalMesh(shape, 0.1)`.
  Without it, curved faces render faceted or not at all.
- **Booleans fail on near-tangent geometry** — OCCT's known weak spot vs. Parasolid. Always
  check `IsDone()`; tune `SetFuzzyValue` when it fails. **Never surface a failed boolean as a
  success**, and do not silently continue past one.
- **Topological naming:** face indices are not stable across a rebuild. Milestone 1 dodges
  this by having no history tree — do not design in an assumption of stable IDs, because a
  real naming scheme will be needed when history lands.

## Milestone 1 acceptance — all of these, on **both** platforms

Orbitable/pannable/zoomable viewport with a visible grid · clicking points on the XY plane
draws a live polyline · closing the sketch produces a filled face · extrude with a
user-entered height produces a solid · two solids fuse/cut/intersect · hover highlights faces
and clicking selects them · STEP export opens correctly in FreeCAD · headless geometry test
passes · clean CMake configure + build from scratch.

## Reality check

OCCT is a real B-rep kernel but it is not Parasolid — roughly 90% of the app is reachable on
it, and the last 10% (multi-edge variable-radius fillets, near-tangent booleans) is genuinely
hard. An early fillet failure is not evidence of a mistake. **FreeCAD is the closest prior
art on this exact stack**; when OCCT behavior is unclear, reading FreeCAD's source is usually
faster than the OCCT docs.
