# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`FurnifyMe` — a cross-platform (Windows + Linux, both first-class) desktop CAD app with
direct-modeling interaction, in the spirit of Shapr3D. C++17 / CMake / Qt 6 Widgets /
OpenCascade (OCCT) 7.6+. CMake project `FurnifyMe`; app target `furnifyme`; geometry
library target `furnify_geometry`.

This file distills the project brief (`CAD_APP_BRIEF.md`, supplied at init; ask the user
for it if you need the verbatim original). Milestone 1 scope is exactly: **sketch → extrude → boolean**,
plus STEP export. **Milestone 2 (direct modeling) is merged**: push/pull on faces,
fillets, chamfers, and a transform gizmo now exist — see "Direct modeling" below. Still out
of scope: history/parametric tree, constraint solver, 2D drawings, assemblies, materials,
and any file format beyond STEP.

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

**The no-input law runs both ways.** `gui_smoke` installs an application-wide filter that
drops every *spontaneous* mouse, wheel and key event, so the machine's own user cannot drive
the app under test either. That is not belt-and-braces: Windows' "scroll inactive windows on
hover" delivers real wheel events to whatever the resting cursor happens to sit over, with
no focus and no click, and a single one of them moved the camera between `show()` and the
`startup distance is 700mm` check — the 1.75× flake that cascaded to 43 failures, and
scale-dependent because the window covers more screen at 1.75×.

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
| `ui/DimensionRenderer.{h,cpp}` | CAD length annotation; one renderer for the sketch and edges |
| `ui/PullArrow.{h,cpp}` | face pull: 3D arrow + value chip, preview by the commit's own call |
| `ui/BevelArrow.{h,cpp}` | edge drag: inward fillets, outward chamfers, same contract |
| `ui/AppearancePanel.{h,cpp}` | every Theme token editable live; debounced persistence |
| `ui/AppBar.{h,cpp}` | the menu strip: wordmark, real `QMenuBar`, view controls |

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
| Turning a closed outline into a body | Extrude | prism, raise-up |
| Moving a face of an existing body | Pull, `Pull distance` | push/pull, offset, drag-face, extrude |
| The 3D area | viewport | scene, canvas, view |
| Rounding an edge | Fillet, `R 20 mm` | bevel, round-over, round |
| Flattening an edge | Chamfer, `C 20 mm` | bevel, break, flatten |
| Repositioning a body | Move / Rotate / Scale | transform, translate |
| The colours-and-fonts panel | Appearance | theme, settings, preferences |

Extrude and Pull are two rows, not one, and the Extrude row no longer bans "pull":
Milestone 2 made face pull an operation in its own right, so "pull" became a word
this app owns rather than one it avoids. The two must not borrow each other's verb
— Extrude raises a **closed outline** that is not yet a body, Pull moves a **face of
a body that already exists** — which is why `Extrude`'s tooltip says "Raise the face
into a body" and only the pull arrow and its refusals say Pull. Fillet and Chamfer
own "round" and "flatten" the same way: the two operations may be *described* as
rounding and flattening in prose, but no painted string names them that way, because
a user who reads "Body 03 rounded" has no word to look for in the interface.

`ModelingOps::BooleanKind::Fuse` and `::Cut` keep their kernel-facing names — the
user never sees them, and renaming them would churn the geometry library and its
tests for no visible gain. The enforced bans match the bare word (case-insensitive):
`OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, `Join` and `bevel` are forbidden
everywhere in action text and widget tooltips, regardless of capitalization.

**`round` and `flatten` are banned too, but matched at a word boundary.** They are the
Never column for Fillet and Chamfer and they shipped for a whole branch inside two Failure
sentences ("will only round some of them") because the sweep could not see them — while
substring matching would red-flag "background", "ground" and "surround", which this app is
entitled to say. `gui_smoke`'s `usesBannedWord()` is the one matcher every sweep goes
through, and its boundary rule is pinned in both directions: a rule living at one call site
is not a rule.

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
toast centred — and since Phase 5 the rail spans the full left edge, which every avoider must
treat as an obstacle. They collide below about 900 px of viewport width, which `Show tips
again` makes reachable. `HintBalloon`'s left floor is **band-aware**, as `ToastHost`'s solver
is: only an obstacle whose vertical span intersects the balloon's own band constrains it —
the top-left drawer must not shove a bottom-strip balloon. `HintBalloon::reposition()`
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

### Direct modeling

Milestone 2's rule: geometry is edited by grabbing it, and the kernel refuses before the
UI can lie. Four operations in `ModelingOps`, all Qt-free, all headless-tested, all
returning `BooleanResult` where **`ok == false` always carries a null shape** (documented
on the type, pinned by tests): `pullFace` (outward-normal prism, fused or cut; refuses a
face that is not on the body — without that guard a mis-wired pick returned two
disconnected solids as success), `filletEdge` / `chamferEdge` (OCCT throws are caught at
the operation boundary; fillets legitimately fail on hard geometry and that refusal is a
Failure toast, never a success), and `transformShape` (uniform scale only — `gp_Trsf`
cannot express per-axis, and `GTransform` would convert faces to NURBS).

Three selection-driven gizmos, no new rail buttons. Their visibility predicates are one
derived function each, driven from `appStateChanged`, and **provably disjoint**:
`ExtrudePreview` requires a pending face; the pull arrow, transform manipulator and bevel
arrow all require none, plus three different selection modes. At most one app-wide
Enter/Escape claim can therefore exist at a time. Gizmo previews go through the
**dedicated `setModelingPreview` channel** (selection mode −1), never the sketch/extrude
`setPreview` slot — two features sharing that slot already cost one bug.

- **Face pull**: drag or type; outward grows, inward carves; snap follows Snap to Grid.
  The distance is the closest-point parameter of the mouse ray against the outward-normal
  line (`CameraController::axisParameterForRay`, headless-tested; a near-parallel ray
  keeps the last value, and a press whose angle refuses still claims the gesture).
- **Transform**: `AIS_Manipulator`, translation/rotation/uniform scaling, snapped deltas
  (10 mm / 15° / 5%) rebuilt about the body's own pivot — naive `TranslationPart()`
  snapping displaces the pivot. Scale bakes are clamped to [0.05, 20] at the UI; the
  kernel accepts more. During an additive (Shift) pick the manipulator is `Deactivate`d
  around the `MoveTo`/`SelectDetected` pair, or `AIS_ManipulatorOwner` outranks the
  shape's owner and the second body cannot be picked.
- **Bevels**: the drag axis is the bisector of the adjacent faces' outward normals
  (`ModelingOps::bevelAxis`, 12-edge headless oracle); against the bisector = Fillet,
  along it = Chamfer. On a concave edge the mapping is unchanged but the fillet bulges
  toward the notch — correct CAD behaviour with an inverted-looking gesture, documented
  rather than special-cased. Curved edges raise no arrow.

**A bevel is clipped to the edges you picked.** `BRepFilletAPI`'s `Add()` is *documented*
to build a **contour by propagation**: "edges of the shape which are tangential to one
another and which delimit two series of tangential faces". A fillet strip made by an
earlier operation is exactly such a series, so rounding an edge that *ends on one* pulls
the strip's far neighbour into the same contour and bevels an edge nobody picked —
measured on a 100×80×10 box, one `Add` produced a contour of **three** edges and removed
13.8% more than the single-edge formula. Nothing in OCCT turns it off: `SetContinuity` at
every continuity and tolerance, `ChFi3d_FilletShape`, and `ShapeUpgrade_UnifySameDomain`
first were each measured and none changed the contour; `ChFiDS_Spine` has no way to remove
an edge and `BRepFilletAPI_MakeFillet` keeps its builder private. So `filletEdges` detects
the spread (walk `NbContours`/`NbEdges`/`Edge` and compare against what was asked for) and
**clips** it: restore the material outside the picked edges' own extents, each extent being
the slab between the two planes perpendicular to that edge at its endpoints. Propagation
*enters and leaves through those endpoints*, which is why that is containment and not an
approximation — the volume removed comes back to the single-edge formula to six figures.
The clip runs **only when a spread is detected**, so the ordinary case takes the plain
kernel path.

Two limits, both on the header where callers read them, both found by review rather than by
the suite. **When the picked extents already cover the body there is nothing to put back**,
and the raw kernel result stands rather than being refused — one picked edge spanning the
body in its own direction is enough, which a Shift-selection on a box reaches in two clicks,
and refusing there told the user to "try a smaller size" when *no* size could work, because
the geometry and not the radius decides whether the slabs cover the body. And the residual
at a non-right-angled corner is **thin but not short**: on a skew prism at 80°, r=4, it is
~6% of the operation's volume running the **full length** of the unpicked edge, so a 700 mm
post gets a 700 mm sliver. Cutting it would cut the picked edge's own bevel short exactly
where it should meet its neighbour. What the clip *does* remove is the case users report —
a neighbouring edge rounded at the full radius along its whole length — and `gui_smoke`
pins the difference by counting only strips longer than four radii.

**Multi-edge bevels are one gesture, one build, one checkpoint, one toast.** Shift-click
accumulates edges (`AIS_SelectionScheme_XOR`, the additive body pick's own path);
`filletEdges`/`chamferEdges` take a `std::vector<TopoDS_Edge>` and the one-edge spellings
delegate to them, so there is one implementation of every refusal. The refusal is
**all-or-nothing** — one foreign, null or unbuildable edge refuses the whole call, because
a partial bevel leaves the user working out which edges took. That is *enforced*, and
`NbContours() > 0` is not the enforcement: `Add()` takes or drops each edge on its own (a
cylinder's seam edge is an ordinary straight edge that yields no contour), so a three-edge
list with one dropped leaves two contours and would build a body with two of the three
bevelled. Every requested edge must appear in some contour before the build runs. Contours
are **not** one per edge — two edges of one tangent chain share one, two far apart get one
each — so counting them cannot answer it. A refusal of the *combination* rather than the
size carries `BooleanResult::combinationRefused`, and the UI has a second sentence for it:
"try them one at a time", because "try a smaller size" is false advice there — no size
works, and the user shrinks the number until they give up. The arrow stands on the edge
picked **last**, which `OcctViewWidget` has to *remember* (`myLastPickedEdge`, validated
against the live selection on every read): OCCT's `InitSelected` order is the context's,
not the user's. The chip names the count only when there is one — `Fillet — 3 edges`, with
the value still `R 20 mm`, because a radius does not multiply — and the state label and the
toast do the same, with the plural written out. The visibility predicate widened from
"exactly one straight edge" to "one or more straight edges, **all on one body**": a
selection spanning two bodies raises no arrow, since one gesture is one build on one shape.
It still requires edge mode, so it stays disjoint from the face pull, the transform gizmo
and `ExtrudePreview`, and the app-wide Enter/Escape claims still cannot collide.

**A screen-space arrow hit test swallows a press before the picker sees it.** `arrowHit()`
is a 14 px Qt-side test, not an AIS owner, so it does not *compete* for a pick — it takes
the press outright. The bevel arrow stands on the last edge picked and the next edge is
usually right beside it, so the press handler excludes Shift from the arrow branch. Same
hazard the transform gizmo's `Deactivate` closes, one layer up and by a different mechanism.

**`Theme` is spec-backed** since the Appearance panel: every colour accessor and the four
derived fonts (badge = base−2, label = base−1, body = base, title = base+3 pt) read
`Theme::Spec`; `defaultSpec()` is Graphite byte-for-byte and all 21 defaults are pinned to
hex in the suite. Edits apply live through one `themeChanged` broadcast — no widget may
cache a colour across it — and persist **debounced** (400 ms, flushed on close), because a
colour-wheel drag fires per mouse-move. The picker opens with `show()`, never `open()`:
`QDialog::open()` forces window-modality regardless of `setModal(false)`, and nothing in
this app blocks. The gizmo's axis hues and the OCCT body/preview materials are the
remaining untokenised colours, by scope ruling.

### Dimensions, planes and units

**Millimetres are the only unit anything stores.** The model, the kernel, `DocumentModel`,
every persisted value and every number handed to `ModelingOps` are millimetres. The
conversion lives in `Measure` alone, at the formatting boundary. `formatLength` still
*takes* millimetres — changing the parameter's meaning would silently convert twice at any
call site that was missed, so the unit lives in the formatter, not the argument.

**Input is read in the displayed unit.** `Measure::parseLength` returns millimetres from
whatever the user typed, so with centimetres selected typing `4` builds a 40 mm body. A
field that displays one unit and reads another is a trap, and it is the specific thing this
work existed to avoid. `parseLength` validates the grammar itself before calling `strtod` —
an optional sign, digits, at most one point — because `strtod` alone accepts `0x10` and
`1e3` and is locale-dependent for the decimal separator.

Anything that changes the unit must re-read, not just repaint: `ExtrudePreview` rebuilds its
preview shape on `appStateChanged()`, or the label would say `(cm)` over a shape still built
from the old unit's reading of the same field, and Enter would commit ten times what the
viewport showed.

**One `DimensionRenderer` serves both cases** — the live sketch segment and a hovered edge.
It holds no opinion about which it is drawing; if it ever needs one, the two cases have
diverged and want separate renderers. Its label is `Measure::formatLength`, never a local
format, which is why it follows the unit for free. Everything except the measured span
itself — gap, offset, extension overrun, arrow size, label gap — is sized in screen pixels
through `OcctViewWidget::worldPerPixel()`; furniture scaled in model units vanishes when you
zoom out. Two OCCT notes: `AIS_TextLabel` needs a family `Font_FontMgr` can resolve, so the
app's DM Sans is spilled from its Qt resource to a temp file and registered once; and
`Graphic3d_ArrayOfTriangles` drew nothing at all here, so arrowheads are two strokes on the
same `Graphic3d_AspectLine3d` path the lines use.

#### Locking a face

A flat face can become the sketch plane, which is what makes it possible to put a shelf on
the side of a cabinet. `SketchController` already took an arbitrary `gp_Pln`, and
`snapToPlaneGrid` already rounded in the plane's own coordinates — what this added is the UI
that chooses one and the grid that shows it.

- **The plane is captured by value.** Face indices are not stable across a rebuild (see the
  topological-naming pitfall below), so re-deriving the plane from a stored face later would
  let a boolean or an undo move the sketch plane under the user. The lock therefore survives
  the deletion of the body it came from, as a plane floating where the face was. That is
  deliberate and safe, not an oversight.
- **`BRepAdaptor_Surface` never applies `TopAbs_Orientation`.** It carries geometry and
  location only. On a plain box, **three of six faces are `TopAbs_REVERSED` and their plane
  normals point into the body** — lock one without flipping and Extrude sweeps the prism
  through the body it is standing on. `lockToFace` reverses the plane when the face is
  `REVERSED`. Verified against a `BRepClass3d_SolidClassifier` ground truth on a box, a Cut,
  a Fuse and a translated body: every outward normal correct, FORWARD faces untouched.
- **This hid behind the face picker**, which skipped faces by `Dot(surfaceNormal, viewDirection) >= 0`
  — on the same wrong assumption, which filtered out precisely the `REVERSED` faces, so
  testing could only ever land on one where the two normals already agreed. Both places
  derive the outward normal now.
- **A volume check cannot catch it.** An inward prism is a valid prism of the right volume.
  The suite asserts the new body's centre of mass on the outward side, and carries a
  deterministic six-face box probe; stubbing the flip to `if (false)` fails the probe while
  the end-to-end path stays green, which is the argument for having both.
- The grid is coplanar with a shaded face and would z-fight, so **only the drawn plane** is
  nudged toward the eye by `octave * 1e-4`, quantized by octave because `GridRenderer` caches
  on the plane it built. `mySketchPlane` is untouched, so clicks land on the true face plane.
  `Graphic3d_ZLayerId_Topmost` was rejected — it clears depth, so the ground grid would paint
  over every body standing on it — and a depth-offset ZLayer does nothing to line primitives.
- A locked plane is a mode, and a mode with no persistent cue is a trap: `updateStateLabel`
  leads with `On a locked face — …` so the label says where the next outline will land.
- **A closed outline pins the plane it was drawn on.** Both the commit
  (`extrudePendingFace`) and the preview (`ExtrudePreview`) sweep the pending face along
  the sketch plane's normal *as it is at that moment*, so locking or unlocking in between
  silently re-aims the extrude — a ground-plane outline swept along a direction lying in
  its own plane, which `BRepPrimAPI_MakePrism` reports as `IsDone()`. Both plane changes
  therefore ask `canChangeSketchPlane()`, which refuses while `hasPendingFace()` and says
  why; `updateActions()` disables both actions and swaps their tooltips for the reason,
  because a disabled control that will not say why reads as broken. The refusal lives in
  `lockToFace`/`unlockFace` too, not only in the enabled state, because the
  `faceDoubleClicked` route never consults an action. And `ModelingOps::extrude` refuses an
  in-plane sweep direction outright — a rule enforced only where a UI path remembered it is
  not a rule, and it is `furnify_geometry`, so it is headless-tested.
- **The cursor readout is the plane's own (u, v)**, through `ElSLib::Parameters` — the same
  call `snapToPlaneGrid` uses, so the two agree by construction. World X/Y froze one number
  and made the other meaningless the moment the plane stopped being the ground.
- **The dimension follows selection as well as hover, and follows the unit.**
  `updateEdgeDimension()` prefers the detected edge and falls back to the single selected
  one, so leaving a selected edge does not drop its label; every route that can change
  either input calls it, including `clearSolids`/`removeSolid`, because an annotation must
  not outlive the body it measures. `DimensionRenderer` is not a `QObject`, so it keeps the
  span it last drew and `MainWindow` drives `refresh()` from `appStateChanged` — the same
  signal the items panel and the extrude preview already follow, rather than
  `setDisplayUnit()` growing a private list of everything that shows a length.

### Qt plugin deployment - do not remove

Qt will not start without a platform plugin, and it looks for one in a `platforms/`
directory next to the executable, not alongside the Qt DLLs. vcpkg's applocal deployment
copies DLLs but **not** plugins, and the `windeployqt` feature is deliberately not installed
(it would have dragged the full default feature set back in). `CMakeLists.txt` therefore
copies `QWindowsIntegrationPlugin` and `QModernWindowsStylePlugin` itself in a POST_BUILD
step. Delete that and the app dies at startup with
`could not find the Qt platform plugin "windows"`.

### The shell is action-driven, and composed as bar + rail + drawer

Every control is constructed from a `QAction` and mirrors it - enabled state, checked
state, label, shortcut. Never give a control its own state: menus, rail buttons and
shortcuts would drift, and `gui_smoke` finds actions by text, so the controls are covered
for free. `updateActions()` remains the single place that decides what is available.

The shell's composition, settled in Phase 5 against HTML mockups the user chose from:

- **The app bar** replaces the menu strip via `QMainWindow::setMenuWidget`. It holds the
  wordmark, the window's **real `QMenuBar`** (reparented in - menus, shortcuts, the
  generated sheet and the vocabulary sweep all keep working untouched), and the view
  controls: the **Persp/Ortho toggle** (Phase 7 - it triggers the checkable
  `Orthographic` action and holds no state, exactly as the unit chip does; it does **not**
  snap to Axonometric, which belongs to the gizmo, keys 0-3 and the View menu, and it
  records no `view.changed`, because a projection flip is not a look in a named direction
  and would otherwise retire the hint teaching the gizmo), the unit chip (triggers the
  *other* unit's existing action - it holds no state), Wireframe and Fit All.
  `Save Screenshot` is menu-only. `OcctViewWidget::viewDirectionName()` (once
  `viewLabelText()`) still answers "which world axis is the camera square onto", and is
  what the suite asserts snap flights against, but nothing paints it any more.
- **The rail** is one `ToolCluster` in `ChipMode::IconOnly` at `Anchor::LeftEdge` -
  every tool as an icon button, labels and shortcuts in tooltips that auto-update from
  the actions. `MainWindow::buildOverlay()` sets the viewport's own minimum height from
  the rail's `sizeHint()` plus both `ViewportOverlay` edge margins - derived, not a
  literal, so it cannot go stale the day a button is added - which is what keeps the
  viewport from ever shrinking short enough to clip the rail (Redo was the first
  casualty, then Undo). A fourteenth tool raises that floor rather than reintroducing
  the clip, but the user's actual screen height is a real ceiling the floor cannot push
  past, so the rail still wants a rework - scrolling, grouping, something - well before
  it gets there.
- **The items drawer** floats beside the rail, toggled by the existing Items action -
  visibility is derived from the action's checked state, both directions, and nothing
  else may show or hide it. The viewport is full-bleed; there is no dock.

Overlay widgets are **direct children of `OcctViewWidget`**. A probe confirmed Qt
composites plain children over OCCT's OpenGL surface correctly on Windows - but see the
opacity rule below: translucency over that surface is the unreliable variant, and this
project no longer paints any.

### One opaque paint family, and the rule that made it

**No widget paints a translucent pixel over the GL surface.** Phase 5 paid twice to learn
this was already the law: the chip shadows introduced early in the phase "worked" only by
blending alpha over garbage, and the rail's unpainted slack rendered as a solid black band
down the app. `Theme::paintSurface()` is the one implementation of the floating-surface
family - an **opaque ground fill across the full widget rect** (`viewport()` by default,
`chrome()` for any future caller painted on a non-viewport ground - the bar's own buttons
paint their own body directly and never call this), then the rounded panel card and a crisp
1px border on top, so a rounded card's corners are flat viewport-grey instead of black.
There are no shadows;
borders carry the separation. `surfaceShadowMargin()` returns 0 and stays only so caller
arithmetic keeps working. `Theme::drawCrispBorder` is the one half-pixel-alignment idiom -
an antialiased 1px pen at an integer coordinate smears across two rows at half intensity.

**One ruled exception:** `Toast::paintEvent()`'s `painter.setOpacity(myOpacity)` blends the
whole toast during its 160 ms dismiss fade - permitted because it is transient and
motion-token-driven rather than a resting translucent surface, and because gui_smoke runs
with animations off, so the opacity/colour sweeps never actually see a blended pixel.

**A floating card's logical size must cover whole device pixels**, through
`Theme::wholeDevicePixels()` at its `setFixedSize`. Widget geometry is logical and the
backing store is device-sized, so a card 93 logical rows tall at 150% scaling occupies
139.5 device rows: Qt flushes 140 and the paint event's clip - logical too - stops the
widget's own painter at 139. Nothing the widget paints can cross its own clip and the
viewport cannot paint underneath a child, so `paintSurface()` cannot save it and the size
is the only cure. The leftover row is the corner-nub failure one scale down, and it is
exactly as black: the bevel chip's first magnified capture carried a 264-device-pixel
`0,0,0` hairline along its bottom edge. Rounding to a multiple of four is whole at every
quarter-step Windows scale, so it does not read `devicePixelRatioF()` - a size that is only
right on the monitor it was written on is the same bug with a longer fuse.

**A whole size only helps if the near edge is whole too**, so a card's POSITION goes through
`Theme::snapToDevicePixels()`. `ViewportOverlay::relayout()` grows every anchored card and
snaps the rail's stretched height; the two chips that follow a projected 3D point
(`PullArrow`, `BevelArrow`) snap their own `move()`. That one reads the live ratio, unlike
the size rule - a size is set once at construction where a live read goes stale, a position
is recomputed on every camera move where it cannot, and reading it buys a 2-pixel step at
150% instead of the 4 a ratio-blind rule must assume. Both halves were found by measurement,
one card at a time: the rail's bottom edge carried a 113-device-pixel black line at 225%
that no crop showed and no 1:1 render could.

**Verify appearance with measured pixels, never by eyeballing a crop.** This phase's worst
finding was a commit message claiming a magnified crop confirmed a 3px gap while the real
gap was 12px - `QBoxLayout` silently ignores negative spacing, and no crop was ever checked
against a number. The suite now measures: gap rows counted between adjacent rail buttons'
borders, corner pixels sampled for exact token colours at alpha 255, whole-perimeter
sweeps with non-vacuity assertions (a sweep that runs zero iterations must fail, not
pass). A probe guarded by a condition that can quietly skip is how five shadow checks
went silent instead of red when the behaviour under them changed. The black-hairline sweep
above is the same discipline one step further out: it measures the whole **composited**
`PrintWindow` capture rather than one widget rendered on its own, because `renderExact()`
draws a widget at 1:1 into an image of exactly its logical size, where the offending pixel
cannot exist - and because mapping a widget's rect into a capture that includes Windows 11's
invisible resize frame needs a scale *and* an offset, and a region a few pixels out reports
clean exactly as loudly as a clean window does.

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

Event wiring: `paintEvent`→`Redraw()`, `resizeEvent`→`MustBeResized()`, RMB drag→turntable orbit around the current view target (Unity-style, the user's explicit preference — no cursor-anchored pivoting), MMB drag→pan, wheel→zoomToward cursor; camera state lives in CameraController and is pushed via SetEye/SetCenter/SetUp. FOVy is fixed at 45° for the life of the view; **which projection is drawn with it moves** — see below.

#### Projection: a base mode and a loan

`CameraController` holds **two** pieces of projection state. The **base** is what the user
chose with the bar's toggle and is persisted; **temporary ortho** is a loan taken by a
gesture that puts the camera square onto something (a gizmo arm, a locked face), because a
face-on view with perspective convergence is not a face-on view. `effectiveOrtho()` —
`temporary || base == Orthographic` — is what the renderer follows, written to the OCCT
camera in the single site `applyCameraState()`.

**Who hands the loan back:** `orbit()` does, but only when it actually *turned* the camera
(measured before-against-after, so a drag pushing further into the elevation clamp, or a
zero-delta move event, spends nothing). **The toggle does too** — a control whose entire
subject is the projection must never be outvoted by a loan the user never asked for; keeping
it made the button visibly do nothing twice in a row after a face lock. Pan, zoom,
`setPivot`, `frame` and every `setState` route deliberately do **not**: panning across a
face-on drawing is ordinary drafting, and snap flights land *through* `setState`, so
clearing there would mean no flight was ever orthographic at all. Note the split — the bare
`CameraController::setBaseProjection` moves one field; `OcctViewWidget::setBaseProjection`
is the user-facing route that also drops the loan.

**The `SetScale` order trap:** `SetScale()` on a camera still marked perspective moves the
*distance* instead, so `applyCameraState()` sets `SetProjectionType` **first**, then the
scale. And the orthographic half needs its scale set explicitly at all, because
`Graphic3d_Camera` keeps `Scale` and `Distance` linked only for a perspective camera —
switch the type alone and the parallel scale sits at its 1000 default and the scene jumps
size.

**The `worldPerPixel()` invariant:** one formula serves both projections, and that is
*by construction*, not luck — `applyCameraState()` sets the parallel scale to exactly
`2·distance·tan(FOVy/2)`, the perspective visible height at target depth. Every
screen-sized thing in the scene rides on it: `DimensionRenderer`'s furniture and both drag
arrows' pixels→millimetres mapping. If it is ever broken, this is the single place to
branch. `OcctViewWidget::cameraViewHeightAtTarget()` exposes the live
`Graphic3d_Camera::ViewDimensions()` so the suite can check what OCCT was *actually told*
— comparing `worldPerPixel()` across a flip proves nothing, since it never reads the OCCT
camera.

One more ortho consequence: a parallel projection has **no horizon**, so
`pointOnSketchPlane`/`pickWorldPoint` skip their behind-the-eye guard in ortho — every ray
is the view direction, "behind" is only measured from wherever auto z-fit left the near
plane, and the perspective rule would refuse perfectly visible clicks. `Convert` and
`ConvertWithProj` themselves are projection-agnostic.

### Selection

`AIS_Shape` selection modes are integers: `0` whole shape, `1` vertex, `2` edge, `3` wire,
`4` face, `5` shell, `6` solid. `Deactivate(shape)` then `Activate(shape, 4)` for faces.
Iterate with `InitSelected()`/`MoreSelected()`/`NextSelected()`, pull topology via
`SelectedShape()`.

### The modeling loop

- **Sketch:** unproject the click with `view->ConvertWithProj(...)` into a `gp_Lin`, then
  intersect with the sketch plane via `IntAna_IntConicQuad`. The plane is the ground plane
  at Z=0 until a face is locked — arbitrary planes are **no longer deferred**, see below. Accumulate points into `BRepBuilderAPI_MakePolygon`,
  `Close()`, then `BRepBuilderAPI_MakeFace(wire, true)`. Show the in-progress polyline as a
  temporary `AIS_Shape` and remove it on commit.
- **Extrude:** `BRepPrimAPI_MakePrism(face, gp_Vec(plane.Axis().Direction()) * height)`.
- **Boolean:** `BRepAlgoAPI_Cut`/`_Fuse`/`_Common` with `SetRunParallel(true)` and
  `SetFuzzyValue(1.0e-5)`. Always follow with `ShapeUpgrade_UnifySameDomain(result, true,
  true, true)` — it merges the coplanar faces the boolean leaves behind. Skip it and the
  model accumulates junk edges that make later selection miserable.
- **STEP export:** `STEPControl_Writer` with
  `Interface_Static::SetCVal("write.step.schema", "AP214IS")`.

### Outlines, layers and gestures (Phase 7)

**A closed outline is a document item** — `Outline NN` in the drawer, one checkpoint on
close, converted to a Body by extrude in **one** checkpoint so a single undo restores the
outline and removes the body. `hasPendingFace()` is a **derived view** over the pending
outline — its truth table at every gizmo predicate, `ExtrudePreview` and the plane-change
guard is unchanged, and every outline carries **its own plane**, captured at close, so an
extrude can never sweep along a plane the user changed afterwards (the Milestone-2 bug
class, retired by construction). Outlines are not pickable viewport geometry; the drawer
row is their handle, and the pending one wears the accent inset bar. Start Sketch no
longer discards a waiting outline.

**An outline has two exits, and Delete is the second one.** Extrude was the only one, and
that was a trap: every direct-modeling gate (pull arrow, bevel arrow, transform gizmo,
Lock to Face) refuses while an outline waits, while the operations that are *not* gated —
booleans, Delete — push onto the undo stack. So "Ctrl+Z to take it back", which both
refusals advised, stopped being the outline's undo the moment the user did anything else:
close an outline, Union two bodies, and every gate is shut with no way to open one.
`Delete Selected` therefore extends to the pending outline **when no body is selected** —
the one state in which it had no work of its own, so the second meaning displaces nothing.
One checkpoint, a `Deleted Outline 01` Note with Undo, and `updateActions()` — the single
place that decides availability — owns both the enablement and the tooltip that says which
of the two meanings is live. Refusal copy says **E-or-Delete**; never Ctrl+K, never Ctrl+Z.

**`fitAll()` frames outlines too.** It walked `mySolids` alone, so an outline-only document
fell to the ±250 fallback box and an outline drawn outside it was unreachable by the one
control whose job is finding things. Both maps are what the widget displays.

**The scene is three Z-layers**: Default (bodies) → grid (depth test on, depth **write**
off) → sketch work (outline, markers, dimension, pending face, modeling preview). The
grid hides behind bodies but never draws over sketch work; sketch lines still hide behind
bodies because the sketch layer depth-tests against what Default wrote. The locked-face
octave nudge stays — the layer settles draw order, the nudge settles the depth tie.
Insert the grid layer **after** Default: inserted before, the locked-face grid vanishes
under the face it decorates (measured: 0 of 5616 grid pixels).

**Gestures, face and edge modes**: plain double-click selects the whole body and switches
to body mode; **Ctrl+double-click on a face** locks the sketch plane (the old plain
double-click route); `L`/`Shift+L`/menu unchanged. The Ctrl exemption from the
pull-arrow's double-click guard applies **only** in the face-lock branch — widened, it
lets Ctrl+double-click in edge mode yank the mode out from under a live bevel arrow.

**Notes can be silenced, Failures cannot.** `View → Show notifications` drops
`Toast::Kind::Note` only; a refusal that reports nowhere would violate the
never-silent-failure law, so `Failure` bypasses the toggle unconditionally. Every Note is
a success report carrying Undo; every refusal is a Failure — the taxonomy is load-bearing.

**Sketching**: Shift snaps the cursor onto the previous segment's direction (parameter
then grid-snapped along the line); the close-hit on the first point is tested on the RAW
plane hit and outranks the straight constraint, with the radius in one place
(`OcctViewWidget::sketchCloseTolerance()`). Ctrl+Z mid-sketch removes the last point
through Backspace's one implementation; the toast's Undo pill deliberately keeps the
document-only predicate.

## Pitfalls (read before debugging)

- **Qt speaks logical pixels; OCCT speaks device pixels.** Qt reports mouse positions and
  widget geometry in *logical* pixels, while the native window handed to OCCT is sized in
  *device* pixels — so `AIS_InteractiveContext::MoveTo`, `V3d_View::Convert` and
  `ConvertWithProj` all want device pixels. The two coincide at 100% display scaling and
  diverge by exactly the scale factor at any other, which is why passing a `QMouseEvent`
  position straight through worked for two milestones and then missed every pick by 1.5× on
  a 150% display. `OcctViewWidget::toDevicePixels()`/`fromDevicePixels()` are the only two
  places that conversion happens; everything outside them — every signal, accessor and test —
  is logical, so a projected point can be clicked and a clicked point projected without
  either side knowing the ratio exists. A projected-geometry test can pass right through
  this bug (it round-trips in the wrong space), so `gui_smoke` pins it directly: the camera's
  own target must project to the viewport's logical centre.
- **Wayland breaks the native window handle.** `winId()` under Wayland gives OCCT something
  it cannot use. Force XCB: `qputenv("QT_QPA_PLATFORM", "xcb")` before constructing
  `QApplication`, or run with `QT_QPA_PLATFORM=xcb`. This costs an afternoon if unknown.
- **`paintEngine()` must return `nullptr`** or Qt and OpenGL fight over the surface.
- **Never `delete` an OCCT handle.** `Handle(Foo)` is refcounted; let it go out of scope.
- **`AIS_InteractiveContext::DetectedInteractive()` dereferences a null on its own.** It is
  an inline that returns `myLastPicked->Selectable()` with no check, and any `MoveTo` that
  detects nothing sets `myLastPicked` to null. **Always guard it with `HasDetected()`**,
  which is literally `!myLastPicked.IsNull()`. This is not hypothetical hardening: one
  unguarded call in `detectedIsManipulator()` crashed the app in a single click — select a
  body, which attaches the transform gizmo, then click empty viewport to deselect. A hover
  over empty space did it too, through a different call site. It survived 850 green checks
  because every one of them clicked something that was there; `gui_smoke` now hovers *and*
  clicks a pixel derived to have nothing behind it with the gizmo up, and surviving to the
  next check is the assertion.
- **`Handle()` is a macro** that collides with some Windows headers. Include OCCT headers
  before `<windows.h>` where possible.
- **Tessellate before display or STL export:** `BRepMesh_IncrementalMesh(shape, 0.1)`.
  Without it, curved faces render faceted or not at all.
- **`AIS_Manipulator` is constructed with zoom persistence ON in OCCT 8.0** — undocumented
  beside `AdjustSize`'s documented default. Its drawn size never follows the camera, so any
  camera-derived sizing writes numbers that never reach a pixel, and a probe that reads
  `Size()` back is a self-oracle that stays green. `SetZoomPersistence(false)` before
  `Attach`, and measure gizmo pixels in a `Dump`, never the setter's own data.
- **`near` and `far` are Windows SDK macros defined to nothing.** A parameter named `near`
  silently becomes unnamed, `near + QPoint(...)` becomes unary plus, and the code compiles
  clean while reading the wrong corner. Do not name anything `near` or `far`.
- **`V3d_View::Dump` returns false when the output directory does not exist** and the
  failure surfaces many checks later as unrelated-looking colour-probe failures. Create the
  snapshot directory before running `gui_smoke`.
- **`projectToScreen` answers in whole logical pixels** and is ~2 dump pixels out at 175%
  scale — a pixel probe must FIND the mark it questions (scan a neighbourhood) rather than
  sampling one projected point.
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
