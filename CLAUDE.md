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
fillets, chamfers, and a transform gizmo now exist — see "Direct modeling" below.
**Milestone 3 (files, versions, symmetry and render) is merged**: a managed library of
`.furnify` furniture with autosave, named versions with a side-by-side
compare, live mirror twins, inline rename, a render mode, and a bottom-bar toggle — see
"Files, versions and the library" below.
**Milestone 4 (two windows, mirror, links, deeper render) is merged**: the library moved
out of the editor into a `SelectorWindow` of its own, versions grew thumbnails, Mirror
became a placed-plane gesture that pairs bodies retroactively, linked copies arrived, the
grid follows a face-on orthographic look, sketching snaps to eight compass directions, and
render mode gained a path-tracing tier with its own settings card — see "Milestone 4: two
windows, Mirror, links and render tiers" below. Still out of scope: history/parametric
tree, constraint solver, 2D drawings, assemblies, materials, and any file format beyond
STEP.

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

Verified by the headless tests - **eleven of them now, 2,046 checks, `ctest` 11/11**
(measured 2026-09-17; it was two tests and 49 checks at Milestone 1, and the sentence is
kept in its original shape so the growth is legible): wire -> face -> prism -> cut -> exact
face counts and volumes -> 494-entity `out.step`; ray/plane unprojection including the
parallel-ray case; sketch accumulation; document id lifecycle; compound building - plus
direct modeling, the serial round trip, `Measure`, `UserProgress`, the oriented measured
box, the mitre frame, the whole joinery layer (665 on its own) and `resizeAlongAxis` with
its stretch.

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

**A second, optional argument filters which blocks run**, so a targeted fix
can iterate in seconds instead of the ~5 minute full sweep:
`gui_smoke.exe <output-dir> [filter]` runs only the blocks whose short name
contains `filter` (case-insensitive substring); `gui_smoke.exe --list` prints
every registered block name (with `[always]`/`[*shared-state]` tags) and
exits before touching Qt at all. Block independence is **not** guaranteed —
most blocks run against the one shared `window` built at the top of `main()`
and assume everything earlier already happened, so filtering does not try to
repair that coupling. Each block is tagged instead: `[always]` blocks
establish the shared window and its first body and always run regardless of
the filter, because dozens of later blocks assume that scene exists;
untagged blocks build their own `MainWindow` and are genuinely independent —
filtering to one of these (e.g. the hover block at the very end of the file)
is the intended, fast use of this feature; `[*shared-state]` blocks touch the
shared mid-sequence state honestly, and `blockEnabled()` prints a `[warn]`
line when a filter selects one of these directly, since the result is not
authoritative on its own. **A filtered run never enforces `kCheckFloor`** —
it prints `FILTERED (N failures, M checks) - floor not enforced` in place of
the floor check, so a filtered run can never be mistaken for an official
one; only a plain, filter-less invocation is the real gate, and that
invocation's accounting is unchanged by any of this.

**`kCheckFloor` is 4914, measured twice on the tree that carries it.** The improvements
branch re-ratcheted it and **the joinery merge's ~4-check debt is settled with it**: that
floor sat below its own true total because two commits landed after the run that measured
it, and this one was taken on the final tree. Two consecutive unfiltered runs measured
**4781 checks + 1 environment skip = 4782**, agreeing to the digit, and the second ran
against the binary the branch ships — built minutes before it, after the crash-hunt
diagnostics came out, and the counts did not move, which is what says those diagnostics were
inert rather than merely believed to be. (It was 4782 at the improvements merge; the boolean
rework's own block and repairs took it to a twice-measured **4872 + 1 = 4873**, and the
UI-review branch's alignment sweep and folder-duplicate block took it to a twice-measured
**4913 + 1 = 4914**.) The one environment skip is the RayTracing
floor-blend measurement, which does not apply when PathTracing is the session's tier.

**Measured, never computed** — this project has now chosen measuring over arithmetic three
times, and the improvements branch is the clearest case for it: fifty checks changed their
expectation and a dozen were added, so any predicted delta on 4126 would have been a number
nobody had watched the suite print. A floor below the true total cannot false-pass, but it
lets exactly that many checks stop running unnoticed, which is the whole failure this
constant exists to catch.

**`gui_smoke` links `/STACK:33554432`, and the reason is this file's own shape.** The suite
started dying at `window.show()` with a stack overflow 34 frames deep, inside the first GL
context creation, with **no recursion anywhere in the stack** — which is why it looked
impossible and took a vectored exception handler to read at all. The cause is that `main()`
here is one function of some thirty thousand lines, and **MSVC allocates a function's entire
frame at entry**: every local in every one of its blocks, whether that block ever runs or
not. The frame grows with each block added, Windows gives a thread 1 MB by default, and the
deep call through the display driver is simply what finally ran off the end of it. Reserve,
not commit, so it costs address space and nothing else. The suite also keeps its own
stack-overflow reporter (`traceStackOverflow()`, `SetThreadStackGuarantee()` +
`AddVectoredExceptionHandler()`, symbols through DbgHelp, frames to **stderr**): there is no
debugger installed on this machine, and a crash whose location cannot be read costs far more
than one link and forty lines. Two lessons generalize. **A stack overflow with no recursion
in it is a frame-size problem, not a loop** — look at the size of the functions on the stack
before hunting for a cycle. And **the reporter writes to stderr rather than a file**: a
hard-coded path is a diagnostic that works on exactly one machine, and stderr is unbuffered
by the standard, which is the one property a dying process actually needs.

**And it compiles `/bigobj`, for the same reason one step along.** A COFF object holds at
most 65,279 sections and MSVC emits them per function and per COMDAT — which in this file
means per block, per lambda and per string literal folded out of that one enormous `main()`.
The folder-duplicate block is simply the one that crossed the line: `fatal error C1128:
number of sections exceeded object file format limit`, which names its own fix and is not a
symptom of anything else. `/bigobj` widens the count to 2^32, costs a slightly larger `.obj`
and changes no generated code. Note what the two flags have in common: **both are the price
of `main()` being one function of thirty thousand lines**, and both are cheaper than the
restructuring that would remove the need for them — but a third symptom of the same shape is
the point at which that trade should be re-examined rather than paid again.

**The no-input law runs both ways.** `gui_smoke` installs an application-wide filter that
drops every *spontaneous* mouse, wheel and key event, so the machine's own user cannot drive
the app under test either. That is not belt-and-braces: Windows' "scroll inactive windows on
hover" delivers real wheel events to whatever the resting cursor happens to sit over, with
no focus and no click, and a single one of them moved the camera between `show()` and the
`startup distance is 700mm` check — the 1.75× flake that cascaded to 43 failures, and
scale-dependent because the window covers more screen at 1.75×.

A window still appears - OCCT's `V3d_View` needs a real native window and a GL surface, so
`-platform offscreen` cannot work - but it is shown with `WA_ShowWithoutActivating` and
never takes focus, and `KeepSuiteWindowsBehind` keeps every top-level window the suite shows
at the **back** of the desktop z-order. A single push at Show was NOT enough and the user
said so - a run opens dozens of windows, and anything that shows or re-shows one puts it
back over whatever they are working in, which left them minimizing and restoring their own
window to get it forward again. So the push repeats: `SetWindowPos(HWND_BOTTOM)` on every
visible top-level this process owns, every 125 ms, plus `WS_EX_NOACTIVATE` on each, so a
stray click on a suite window cannot activate it either (the suite drives itself through
`sendEvent()` and never needs one activated). Not minimized: a minimized window gets no surface and
every `Dump`- and `PrintWindow`-measured check would fail. Occluded costs nothing the suite
reads — the viewport renders into its own framebuffer, `Dump` reads that, and
`PrintWindow(PW_RENDERFULLCONTENT)` captures a covered window; the render-mode shadow block,
the selection-sizes block, the startup layout block and the SelectorWindow composited
black-line sweep were each run green with the windows sent behind. `gui_smoke` is deliberately **not** registered with ctest: it needs a GPU
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
| `ui/PanelCloseButton.{h,cpp}` | the hover-only x in a panel's corner; triggers the panel's own `QAction`, holds no state |
| `ui/AxisGizmo.{h,cpp}` | Unity-style orientation gizmo; each axis tip snaps the view |
| `ui/WalkthroughPanel.{h,cpp}` | the guided first build; steps derived from live state |
| `ui/HintBalloon.{h,cpp}` | one hint at a time, retired when its trigger stops holding |
| `ui/ShortcutSheet.{h,cpp}` | shortcut list generated from the window's own `QAction`s |
| `ui/Toast.{h,cpp}` | one non-blocking message at a time, with Undo where it applies |
| `ui/ExtrudePreview.{h,cpp}` | height entry with a live preview built by the commit's own path |
| `ui/DimensionRenderer.{h,cpp}` | CAD length annotation; one renderer for the sketch and edges |
| `ui/SelectionSizesRenderer.{h,cpp}` | width/depth/height around a selection; a composition over `DimensionRenderer` |
| `ui/PullArrow.{h,cpp}` | face pull: 3D arrow + value chip, preview by the commit's own call |
| `ui/BevelArrow.{h,cpp}` | edge drag: inward fillets, outward chamfers, same contract |
| `ui/MitreTool.{h,cpp}` | Mitre end chip: typed angle, Flip, preview by the commit's own call; the dial is `OcctViewWidget`'s |
| `ui/ReMeasureTool.{h,cpp}` | Re-Measure chip: a size number becomes a field; preview by the commit's own call |
| `ui/ResizePinRenderer.{h,cpp}` | the Re-Measure pin: which end stays put, three marks on the dimension line |
| `ui/TransformGizmo.{h,cpp}` | the gizmo we draw: shared `GizmoRenderer` base + the Move tool |
| `ui/AppearancePanel.{h,cpp}` | the **Settings** drawer — five tabs; Theme tokens live and debounced, every other row a mirror of a `QAction` (class name kept, see its header) |
| `ui/AppBar.{h,cpp}` | the floating pill: app mark, wordmark, real `QMenuBar` |
| `FurnifySerial.{h,cpp}` | binary shape (de)serialization via `BinTools`, **zero Qt includes** |
| `FurnitureStore.{h,cpp}` | owns the managed library — enumerate/create/save/load/rename/versions |
| `ui/SelectorWindow.{h,cpp}` | the library, a top-level window of its own — cards, New, rename, delete |
| `EditorSelectorHandoff.{h,cpp}` | the ONE wiring that swaps editor and selector; `main.cpp` and `gui_smoke` share it |
| `ui/InlineRename.{h,cpp}` | the one `QLineEdit`-in-place helper: selector cards and drawer rows both use it |
| `ui/VersionsPanel.{h,cpp}` | versions drawer: thumbnail cards, Compare, Restore, two-click Delete |
| `ui/UnsavedCloseCard.{h,cpp}` | the close question over a dimmed viewport, and the status bar's unsaved dot (`FurnitureNameMark`) |
| `ui/RenderSettingsPanel.{h,cpp}` | the render-mode settings card and its shutter (`RenderShutterButton`, same file) |
| `ui/CardSlide.{h,cpp}` | slide-in/out for an overlay card; only the rail uses it. `setShown()` is idempotent against what was ASKED for, `settle()` re-aims a live flight at relayout's new home rather than stopping it |
| `ui/NameFurnitureCard.{h,cpp}` | the "what should this be called" question; creates nothing until it is answered |
| `ui/SlatsTool.{h,cpp}` | the slats chip - Width/Gap/Depth/Flip and a live count, bottom-centre |
| `ui/LoadingCard.{h,cpp}` | the app mark, the name and a progress bar while a heavy furniture opens |
| `ui/MaterialCard.{h,cpp}` | one material's colour and brightness, bottom-left, over a live viewport |
| `ui/BooleanBadgeRenderer.{h,cpp}` | the KEEP/USED pills a live boolean puts on each body; a click picks the survivor |
| `ui/BooleanTool.{h,cpp}` | the boolean chip - the verb, one switch and the two keys; the smallest chip here, deliberately |
| `SceneModel.{h,cpp}` | a scene: pieces that REFERENCE furniture, plus a rigid placement each; Qt-free, headless-tested |
| `ui/RenderStudio.{h,cpp}` | the render layer, lifted out of `MainWindow` so a second window can have it; also the ONE place the list of what a shot carries is written |
| `ui/SceneWindow.{h,cpp}` | the scene editor - a third top-level window; arranges furniture, cannot make it |
| `ui/ScenePiecesPanel.{h,cpp}` | the pieces in a scene, listed; `ItemsPanel`'s shape at a fraction of its size |
| `ui/AddPieceCard.{h,cpp}` | which furniture goes into this scene - a card over the viewport, never a dialog |

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
| Cutting a board's end off at an angle | Mitre, `Mitre end`, `45°` | bevel, miter, angle cut, cut |
| Retyping one of a body's three sizes | Re-measure, `Width` / `Depth` / `Height` | resize, rescale, stretch-to, set length |
| Repositioning a body | Move / Rotate / Scale | transform, translate |
| A live mirrored twin | Mirror | symmetry, mirroring-mode, reflect |
| A copy that follows its source | Linked copy, `Duplicate linked` | instance, clone, reference |
| The settings drawer | Settings | preferences, options, config |
| A named container of items in the drawer | folder | collection, layer, node, bin |
| A run of evenly spaced thin boards | slats | battens, ribs, louvres, sticks |
| One furniture standing in a scene | piece | instance, item, object, copy |
| A material's own colour and brightness | look | shader, finish, skin |

**"item" is in the piece row's Never column but is NOT swept, and the reason is the "Group"
exception one paragraph down.** This app owns *item* for something else and says it
constantly: the Items drawer, `ItemsPanel::Row`, `setItemGroup()`, `itemsInGroup()`, and this
file's own "improvements item 5". A **piece** is one whole furniture standing in a scene; an
**item** is a row in the editor's drawer — a body or an outline inside one furniture. Two
concepts, two words. The row says what a piece must not be called *in the scene window's own
copy*; it does not retire a word the editor needs. Nothing in the enforced list changed: the
swept words are still `OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, `Join`, `bevel`,
`symmetry`, `round` and `flatten`, and adding "item" to that list would redden the Items
drawer, which is correct English for what it holds.

**"Group" is not in the folder row's Never column, deliberately.** This app already owns
that word for something else and says it constantly: a multi-body selection is a group, and
`selectionSizesKind() == Group`, "3 bodies selected — overall …" and "a group's overall size
is read-only" all depend on it. A **folder** is a named, saved container in the Items drawer;
a **group** is whatever happens to be selected right now. Two concepts, two words, and
banning either would make one of them unsayable.

**The Settings row was renamed when the panel grew, and the rename is deliberate.** It
used to read "The colours-and-fonts panel | Appearance | theme, **settings**, preferences",
and that was right for a card holding twenty-eight colours and a type scale. Improvements
item 10 turned it into a four-tab drawer that also holds the viewport switches, the display
unit, the drawing aids and the autosave mode — at which point "Appearance" named a third of
what the user was looking at, and the banned word was the honest one. So the row flipped:
**Settings** is the word, "Appearance" is simply retired rather than banned (it is still the
right word for a colour, and the Colours tab is where colours live), and "preferences",
"options" and "config" are what it must never be called. `AppearancePanel` keeps its class
and file name — a hundred references' worth of churn for no reader's benefit — and says so
in its own header comment rather than leaving the code claiming to be about appearance
alone.

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
tests for no visible gain. `ModelingOps::stretchAlongAxis()` is the same case one row
down: "stretch-to" is Re-measure's own Never column, and this is the function the tool
calls when an end cannot be pulled (improvements item 13). Nothing painted says stretch —
the user typed a size and got a board of that size, which is all Re-measure ever claims. The enforced bans match the bare word (case-insensitive):
`OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, `Join` and `bevel` are forbidden
everywhere in action text and widget tooltips, regardless of capitalization —
and so is `symmetry`, added in Milestone 4's fix wave. The kernel-facing
`DocumentModel::symmetryOn()`/`setSymmetry()` and every private member keep that spelling
for the same reason `BooleanKind::Fuse` does; nothing PAINTED may. The Model menu reads
**Mirror** (`S`) and **Turn Mirroring Off**, the status label reads `Mirror on — …`, and
the plane messages read `Mirror plane …`. The old `&Symmetry` entry was the vocabulary
law broken in the one direction that matters most: the word on the control the user had
to press was not the word any of its own feedback used.

**An override declared one line into a `signals:` block becomes a SIGNAL.** moc reads the
section, not the `override` keyword, so `QSize sizeHint() const override;` placed there had
moc generate a definition of its own and the linker reported it as a duplicate of the real
one - a confusing error that names neither moc nor the section. This project has now hit the
same trap twice, the first time with a `QAction` member in a slots section. When adding a
member to an existing class, read UP to the nearest access specifier before trusting where
an anchor landed.

**`round` and `flatten` are banned too, but matched at a word boundary.** They are the
Never column for Fillet and Chamfer and they shipped for a whole branch inside two Failure
sentences ("will only round some of them") because the sweep could not see them — while
substring matching would red-flag "background", "ground" and "surround", which this app is
entitled to say. `gui_smoke`'s `usesBannedWord()` is the one matcher every sweep goes
through, and its boundary rule is pinned in both directions: a rule living at one call site
is not a rule.

**User-typed text is exempt from the sweep; this app's own copy embedding it is not.**
`usesBannedWord()` takes an `isUserData` flag (default false, so every pre-Milestone-3 call
site sweeps exactly as before) that short-circuits to "clean" outright — an item's name, a
furniture's name, a version's name is the owner's word choice, not this app's copy, and
"Fuse My Table" must pass the same sweep that correctly fails an action tooltip saying "Fuse
the two bodies". Each surface that paints user text — `ItemsPanel::paintedTexts()`,
`SelectorWindow`'s cards, `VersionsPanel`'s rows — exposes it and passes `isUserData=true` at its
own sweep site, following `WalkthroughPanel`/`HintBalloon`/`ShortcutSheet`'s existing
generate-don't-duplicate pattern. **`Toast` has no user-data channel of its own** — it paints
one plain string with no way to mark part of it exempt, so a rename toast that reads
`Renamed to "Fuse My Table"` genuinely contains the banned word and genuinely fails the sweep
if one is run against it. That is not a gap to close; the exemption lives one layer up, at
the surface that *knows* which substring is the user's, and `gui_smoke` pins both directions
at once — the same string passes through `ItemsPanel`'s exempted sweep and fails a plain
`usesBannedWord()` call, proving the mechanism does something rather than nothing.

Numbers are formatted by `Measure` (`src/Measure.h`), never by hand at a call
site: lengths as `340 mm` / `1,200 mm` / `18.5 mm`, sizes as `340 × 220 × 18 mm`
(`formatSize(a, b, c)` when the three numbers are already in hand, in the caller's order).
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
again. **`recordProgress()` is WRITE-THROUGH** — it builds a `QSettings` and serializes the
whole blob every call — so an event recorded on a per-gesture path must be gated on
`hasLearned()` first. `subPick.used` (a face or an edge picked, the auto-pick hint's own
event) is: without the gate it was one registry write per click, forever, on the app's
hottest interaction, and every write past the third changes no answer anybody asks. Same
class of mistake as an overlay `paintEvent` decoding an asset — cheap-looking work moved onto
a per-gesture path. **Storage is injected, not built in:** `MainWindow` persists `serialize()` through
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
- **A probe that finds a control BY SHAPE finds whatever else has that shape.** The suite
  scanned the Items drawer for "the first visible `QPushButton`" to get a row's eye toggle,
  which was exact until the folders work put a `+` in the title row — also a `QPushButton`,
  also always visible, and **added to the panel before any row exists**, so it was the first
  thing the scan reached. Every such probe then clicked it, and each click made a folder
  called "Folder" that landed at row 0 and broke every row-index check downstream in the same
  shared window: **eighteen failures, none of them in the code that changed.** The fix is to
  tell them apart STRUCTURALLY — an eye belongs to a ROW, the `+` is a direct child of the
  panel — in `firstVisibleRowToggle()`, which is now the one implementation every such scan
  goes through, because the two local copies of it had already drifted apart once. The rule
  generalizes past this one button: a scan whose predicate is "the first widget of type T"
  is a scan that will one day find a different T, and the cheapest insurance is that it
  lives in exactly one place where the next person can fix it once.

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
`ExtrudePreview` requires a pending face; the pull arrow, the body-transform gizmo (all
three tools ours — see "The custom transform gizmos" below) and the bevel arrow all
require none, plus three different values of `selectionKind()` —
Face, Body and Edge respectively. (That last clause read "three different selection modes" until the
auto-selection spec's Phase 2 deleted the modes; see "Selection" for the re-keyed argument
and why kind-locked accumulation is what keeps the new foundation solid.) At most one
app-wide Enter/Escape claim can therefore exist at a time. Gizmo previews go through the
**dedicated `setModelingPreview` channel** (selection mode −1), never the sketch/extrude
`setPreview` slot — two features sharing that slot already cost one bug.

- **Face pull**: drag or type; outward grows, inward carves; snap follows Snap to Grid.
  The distance is the closest-point parameter of the mouse ray against the outward-normal
  line (`CameraController::axisParameterForRay`, headless-tested; a near-parallel ray
  keeps the last value, and a press whose angle refuses still claims the gesture).
- **Transform**: all three tools are OUR OWN 3D gizmos since the custom gizmo's Phase 2
  (see below) — `AIS_Manipulator` is deleted from the app. Deltas about the body's own
  pivot (naive `TranslationPart()` snapping displaces the pivot), snapped when Snap to
  Grid is on (10 mm / 15° / 5%); Scale bakes are clamped to [0.05, 20] at the UI, and
  scale is uniform by kernel law (`gp_Trsf` has no per-axis form).
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

#### Mitre end: a board's end at an angle, measured in the end's own frame

Improvements item 4. The user picked "C+" from an HTML mockup round (the protractor dial
plus a chip with a typed angle and Flip), and the page's "The same in all three" list is
the contract: select a board's end face, **Model → Mitre end** or **M**; a live preview; Enter
applies, Esc cancels; 1°–89°, snapping to 5° while Snap to Grid is on when the dial is
dragged, typed values exact; one undo step, the mirror twin and linked copies follow, joints
re-derive. **Flip steps through the end face's four edges** (a follow-up the user picked from
a preview, drawn looking straight at the end): left → top → right → bottom → left, where
left/right cut **across the width** (the mitre) and top/bottom tilt **through the
thickness** — one or the other, never a compound cut. The edge Flip lands on is the one that
keeps the board's full length.

- **The geometry is `ModelingOps::mitreEnd`, headless-tested first** (`tests/mitre.cpp`,
  written red against a stub). **The board frame is the END FACE'S OWN, never a world axis**
  — this branch's most repeated bug was an oriented quantity measured in world terms. Length
  axis = the face's outward normal from `pullFace`'s `outwardPlane()` (flag as a guess, then
  the classifier probe); the in-plane axes are the face's longest boundary edge and outward ×
  that; the face's own **vertices** projected on both give width (longer) and thickness
  (shorter) — `Joinery::regionAxis`/`planeExtent`'s discipline. Removed volume is exactly
  `0.5 × W × W·tan(a) × T`, pinned at 45° and 30°, on a board rotated 37° about (1,2,3)
  through an off-origin point, and on a mirrored board. Two mutations were each made to go
  red on named checks: swapping width and thickness (20 red, starting with `frame width is
  the end face's LONGER extent`), and the flag-only outward normal (red only on the mirrored
  board — `mirrored: the outward normal is +X` — which is exactly why that case is in the
  suite).
- **The four sides are `ModelingOps::MitreSide`, named by the board's own axes, never by
  the user's picture** — the model has no camera. `WidthA`/`WidthB` pivot on the width's low
  and high edge (a pivot edge running along the thickness; the cut swings across the width,
  removing `0.5 × W × W·tan(a) × T`); `ThicknessA`/`ThicknessB` pivot on the thickness's
  high and low edge (a pivot edge running along the width; the tilt removes
  `0.5 × T × T·tan(a) × W`). Declared in Flip's order, and `nextMitreSide()` is the one place
  that order is written. The mapping to left/top/right/bottom holds because the frame
  **signs** its in-plane axes: `thicknessAxis = outward × widthAxis`, always, so looking at
  the end from outside the wood width points right and thickness up (which way "right" is on a
  given board still follows its longest edge). `MitreFrame` carries `pivotAxis` (along the
  pivot edge — the tool's sweep and the dial's normal), `span` (across from the pivot edge —
  what `checkMitre()`'s `RunsPastTheEnd` multiplies by tan a, so a thin board refuses far
  less often through its thickness) and `sweep`; one tool construction serves all four.
  `tests/mitre.cpp` pins each side's formula, and **which edge was kept by centre of mass**
  (within a pair the volumes are equal, so volume alone cannot tell A from B), plus the new
  face containing the pivot edge, on the square board, on a board rotated 53° about
  (−2,1,4), on a mirrored board through the thickness, and the length refusal on a 10 mm
  stub (18·tan 30° = 10.39 refused, 25° fits). Mutations: `ThicknessA` mapped onto `WidthA`
  went red on 13 checks (first `square ThicknessA: pivots on an edge along the WIDTH`); A/B
  swapped on the thickness pair went red on 6 (`square ThicknessA: the mass moves to
  +thickness (the HIGH thickness edge is kept)`).
- **The tool is a bounded prism, not a half-space.** It spans the face's own width and
  thickness plus a margin, with its near edge extended behind the pivot so no tool face lies
  coplanar with a board side it merely touches. A half-space would also shear off anything
  of the body lying beyond the plane well away from this end.
- **One implementation of every refusal: `ModelingOps::checkMitre()` returns a value**
  (`AngleOutOfRange` — NaN included, which is how an unreadable typed angle arrives —
  `NotABoardEnd`, `RunsPastTheEnd`), the `combinationRefused` precedent. `mitreEnd()` is built
  on it, `canMitreEnd()` is its angle-free half, and `MainWindow::mitreRefusalFor()` only maps
  the value to a sentence; nothing in the app re-derives "W·tan(a) past the length".
- **The gesture's state is `MainWindow`'s, and it ends by derivation.** `beginMitreEnd()`
  captures the face, the body and `revision()`; `updateActions()` prunes a gesture that no
  longer holds — selection not exactly that face, revision moved (undo, delete, a boolean),
  sketch, pending outline, render mode, compare pane, mirror placement, library, close
  question — one predicate (`mitreGestureStillHolds()`) rather than a cancel at every site.
  `beginMitreEnd()` re-checks `canMitreSelectedFace()` itself, because `QAction::trigger()`
  does not consult `isEnabled()`. `canMitreSelectedFace()` caches the geometry's answer on the
  face and the revision, since it is asked on every selection click.
- **Disjointness is kept by a term, not by luck.** A live mitre stands on exactly the
  selection the pull arrow wants (one face), so `canPullSelectedFace()` refuses while
  `myMitreActive` — the arrow retires and its Enter/Escape claim with it. Rename and
  Save version exclude the gesture the way they exclude a mirror placement.
- **The dial is `OcctViewWidget`'s, the chip is `MitreTool`'s** — PullArrow's split. The dial
  is a half circle with a tick every 15° **in the plane the angle is measured in — square to
  the pivot edge** — so on a thickness face for a width side and on a width face for a
  thickness side, whichever of the two faces the pivot edge pierces is turned toward the
  camera, with a `gizmoAxisX` radius and handle at the
  live angle; angle a points along `across·cos a − outward·sin a`, so the red radius lies
  exactly on the line the mitre leaves on that face. Flip re-places the chip, because the
  dial's projected extent moves with its plane (measured: without it the chip covered the
  thickness-side dial's lower quarter). The status label carries `— across the width` /
  `— through the thickness` and Flip's and the field's tooltips say the same, all from
  `MainWindow::mitreSideText()`. Sized at `kMitreDialPx` (90) through
  `worldPerPixelAt()`, drawn in `gizmoZLayer()` (depth-cleared, Immediate — the shadow-map
  pitfall), equal-guarded in `applyCameraState()`. The handle takes a press at
  `kHandleGrabPx`, the frame is **frozen at the press** (the Move gizmo's lesson), the angle is
  the cursor ray against the dial's own plane, snapped to 5° with Snap to Grid and clamped to
  [1, 89]; the release is swallowed and does **not** commit — Enter does. A press anywhere
  else picks as usual, and a pick that changes the selection ends the gesture, by contract.
- **The chip follows ExtrudePreview's contract clause for clause.** The preview is
  `ModelingOps::mitreEnd` on `setModelingPreview` (cached on revision/angle/side and re-shown
  on every `appStateChanged`, because every other gizmo's `refresh()` clears that channel on
  the way past — which is also why `MitreTool` is constructed after every other chip:
  connection order is emission order). Unreadable or out-of-range input keeps the last good
  ghost and marks the field; an angle the board is too short for shows **no** ghost and a
  reason row says why. Dragging writes the field. Field and Flip are viewport siblings with
  `WA_NoMousePropagation`, pinned by `childAt`; Flip takes no focus; the field takes focus at
  begin so typed digits do not reach the 0–3 view shortcuts. `Measure::formatAngle`/
  `parseAngle` are the one angle format (`45°`, `32.5°`), Qt-free and headless-tested.
- **Commit is `commitReplaceBody()`** — one checkpoint, render mode exits, the twin
  re-derives by mirroring, links propagate, joints re-derive off the moved revision — with a
  `Mitred Body 03 — 45°` Note carrying Undo, or a Failure carrying the named reason with the
  gesture left live.
- **`gui_smoke` block `mitre-end-a-board-end-at-an-angle`** (self-contained, 67 checks)
  seeds a 300 × 60 × 18 board and a post through `FurnitureStore` and pins availability on
  nothing/body/edge/curved face/board end, the dial and chip with the arrow gone, typing 30
  and an exact 32.5 against the formula, invalid input, a too-short refusal on screen and on
  Enter, Flip moving the removed corner, a snapped (60 from an aim at 62) and a free drag,
  Esc byte-identical with no checkpoint, a selection change cancelling, Enter with focus off
  the field committing exactly one checkpoint equal to the ghost, one Undo, and a mirrored
  twin following. Three mutations each went red on a named check: the preview built through a
  5°-snapped angle (`a typed 32.5 is EXACT`), Enter left unclaimed (`Enter, with focus on the
  viewport, commits`), and an extra checkpoint (`exactly ONE checkpoint (0 -> 2)`).
  The four-sides follow-up grew it to 86: four real clicks of Flip walk
  WidthA → ThicknessA → WidthB → ThicknessB → WidthA, each ghost against its own formula,
  the kept corner differing A/B by centre of mass on both pairs (Y for width, Z for
  thickness), the dial's normal moving from Z to Y and back, the chip clear of the moved
  dial, the label and tooltip naming the kind, and a thickness-side mitre committing one
  checkpoint that one Undo restores. A Flip that toggled only two sides went red on 11 checks
  (first `clicking Flip steps left -> top (WidthA -> ThicknessA)`); dropping the re-place went
  red on `after Flip the chip stands clear of the moved dial`.
  `kCheckFloor` was **not** touched: this branch ran filtered blocks only, and the floor is
  re-ratcheted by measuring an official run, never by adding to it.
- **Honest limits.** The preview wears the app's one preview look — the yellow ghost with the
  body caged — not the mockup's translucent red plane. The frame's extents come from vertices,
  so a board end bounded by an arc is measured at its endpoints (Joinery's curve-aware
  `projectedRange` was not ported). A board already mitred at its FAR end is length-checked
  against its longest reach, and only the result's one-solid check stands behind the corner
  there.

#### Re-Measure: right-click a size number and type a new one

Improvements item 8. Two HTML mockup rounds, both picked A: **the tool is "right-click a
size number"** (not an edge menu, not a face's distance-to-its-opposite), and **a pin marks
the end that stays put** — with one change the user stated outright, that **the pin starts at
the CENTRE**, so both ends move by half until somebody pins one. Select a body so its sizes
show (item 5), right-click one of the three numbers, type a size, Enter applies and Esc
cancels; one undo, the twin and linked copies follow, joints re-derive.

- **The faces move; nothing is scaled.** `ModelingOps::resizeAlongAxis(body, axis,
  newExtentMm, anchor)` pulls the END FACE at the moving end by the difference, through
  `pullFace()` itself — one implementation of the outward normal, of the mirrored-body
  classifier probe and of every kernel refusal. `gp_Trsf` has no per-axis scale and
  `GTransform` would turn every planar face into a NURBS surface (the `transformShape`
  ruling), and a 600 mm board asked for 450 would come back 0.75× in its thickness too,
  which is not what "make it 450 long" means to anybody cutting wood. The two untyped sides
  come back untouched to the micron, and the headless suite asserts exactly that.
- **The size is read off `measuredBox()`, never a world bounding box.** The number the user
  right-clicked came from that box, so the number being changed has to come from the same
  place: the world AABB of a 600 × 300 board turned 30° reads 669.6, and typing 450 against
  it would move the end by −219.6 instead of −150. `axis` is therefore one of the measured
  box's own three axes (either sign — a direction, not an end), and a direction that is not
  one of them is refused rather than guessed at.
- **`ResizeAnchor` is `Low` / `Centre` / `High`, and volume cannot tell them apart.** All
  three produce a board of exactly the typed length, so every check in
  `tests/resize_axis.cpp` (140, written before a pixel of the gesture existed) reads the
  CENTRE OF MASS too: on 600 → 450 from x = 100, Low leaves it at 325, Centre at 400 and
  High at 475. Three anchors × shrinking and growing × three axes, a board turned 30° about
  Z then 37° about (1,2,3), a mirrored body, and every refusal by name. `Centre` is two
  pulls of half each, and the second end's face is re-derived FROM THE SHAPE IN HAND —
  `UnifySameDomain` rebuilds the body, so a face carried across that is a different
  `TopoDS_Face` (the topological-naming warning, met by never carrying one). Two mutations:
  `Centre` stepping like `Low` turned six named centre-of-mass checks red; reading the
  extent off the world AABB turned `turned board 600 -> 450, anchor Low: accepted` red.
- **`checkResize()` returns a value, not a sentence** — `checkMitre()`'s contract for its
  reason: `error` is written for `ModelingOps` and never shown, so a caller matching
  substrings of it would break the first time a sentence was reworded. `SizeNotPositive`,
  `AxisNotASide`, `EndNotFlat`, `NotMeasurable`; `MainWindow::reMeasureRefusalFor()` only
  maps the value. A new size equal to the current one within `kResizeNoChange` (a micron) is
  **not** a refusal — it succeeds with the body handed straight back, and the window then
  ends the gesture with a status line and **no checkpoint**, because that is not a change.
- **A right CLICK opens it; a right DRAG still orbits, and the two are told apart by
  DISTANCE.** `OcctViewWidget` records where the right press landed and the furthest the
  cursor has been from it since (the max, not the sum — a drag out and back is still a
  drag); on the release, within `kRightClickSlopPx` (4) and over a number, it emits
  `sizeLabelRightClicked(index)`. **The obvious probe for this is vacuous and a mutation run
  caught it being so**: a long drag ends where there is no number at all, and a release over
  nothing opens nothing whatever the threshold says — so the suite drags eight pixels,
  ENDING on the number, and asserts the number is still under the cursor at the release
  before asserting that nothing opened.
- **Which number is under the cursor is the renderer's own answer.** `DimensionRenderer`
  records the boxed label's anchor and half-sizes (DEVICE pixels, the zoom-rotate
  persistence's own unit) and the drawn line's two ends where it BUILDS them;
  `SelectionSizesRenderer` indexes them width/depth/height — which is `MeasuredBox`'s axis
  order, so an index carries its axis without a lookup — and
  `OcctViewWidget::selectionSizeLabelRect()` is the ONE derivation the hit test and the
  chip's placement both read, so the pixels a press is tested against and the pixels the
  chip stands clear of cannot be two different boxes.
- **The pin is three marks in the scene, and only the centre one steps off the line.**
  `ResizePinRenderer` draws the active mark as a filled disc with a ring and a head in
  `Theme::accent()`, the two alternatives as smaller open rings in `Theme::sizesOneBody()`,
  all screen-sized under `Graphic3d_TMF_ZoomRotatePers` like the boxed number. A press
  within `kHandleGrabPx` of a mark is claimed outright and its release swallowed (every
  handle in that file's rule — a re-pick there would change the selection and so end the
  gesture the click was adjusting). **The centre mark is nudged one label's clearance out
  along the annotation's own `ext` direction**, because the boxed number sits exactly at the
  line's midpoint and there is no room on the line there: measured on the first capture,
  where the centred pin covered half of "600 mm". The two end marks stay exactly on the
  line's ends, where the picked mockup draws them.
- **Disjointness, by a term rather than by luck.** The gesture stands on exactly ONE whole
  body — which is also what the transform gizmo and a mirror placement want, the one genuine
  overlap — so `transformableBodyId()` returns 0 while it is live (the mirror gesture's own
  precedent, and it matters: `MoveTool` claims **Escape** application-wide, and the Escape
  that backs out of a retyped size must reach the chip), `mirrorPlacementEnvironmentOk()`
  refuses while it is live and `canReMeasureSize()` refuses while a placement is, Rename and
  Save version exclude it as they exclude a placement, and the pull arrow, the bevel arrow,
  the mitre chip and `ExtrudePreview` are already held apart by needing a Face, an Edge, a
  Face and a pending outline respectively. **The joint chip is the one that stopped being
  held apart by its selection** — improvements item 2 made it a live gesture, so
  `canReMeasureSize()` carries an explicit `jointChipJointId() > 0` stand-down beside the
  others rather than relying on the card wanting two bodies where this gesture wants one.
- **A group's overall size is read-only, and it says so in the STATUS BAR.** A group's box
  is not any one body's side, so there is no honest answer to which piece a retyped number
  should change. Not a Failure toast: nothing failed, the gesture simply cannot begin, which
  is `beginMitreEnd()`'s own refusal taxonomy.
- **The chip follows ExtrudePreview's contract clause for clause.** The ghost is
  `MainWindow::reMeasureResult()` — the very call `reMeasureTo()` commits, `bevelPreview()`'s
  contract — on `setModelingPreview`, cached on revision/size/anchor and re-shown on every
  `appStateChanged` (every other gizmo's `refresh()` clears that channel on the way past,
  which is also why `ReMeasureTool` is constructed after every other chip). Input is read in
  the DISPLAYED unit and the field is SEEDED in it, so 600 mm reads `60` with centimetres
  chosen and typing 45 there makes the body 450 mm; the seed strips both the unit suffix and
  `formatLength()`'s thousands comma, which `parseLength()` refuses by grammar — a 1,200 mm
  board would otherwise seed a field that reads as invalid the instant it appears. Invalid
  input keeps the last good ghost; a size the geometry refuses shows **no** ghost and a
  reason row says why, and Enter on it reports rather than doing nothing silently. Moving the
  pin re-previews immediately, because which end stays put changes the shape.
- **The gesture ends by derivation, in the same place the mitre's does** — one
  `reMeasureGestureStillHolds()` in `updateActions()` covering a selection change, any
  document change (revision moved), a sketch, a pending outline, render mode, a compare
  pane, the library and the close question, rather than a cancel at every one of those
  sites. Commit is `commitReplaceBody()`: one checkpoint, render mode exits, the twin
  re-derives, links propagate, joints re-derive, and a `Body 01 — 600 mm to 450 mm` Note
  carries Undo.
- **`gui_smoke` block `re-measure-right-click-a-size-and-type-a-new-one`** (independent, 81
  checks, measured) seeds two boards and one mitred board, and pins the hit test, the right-drag, the
  right-click, the field's reachability by `childAt` and its focus through the window's own
  `focusWidget()` (never `hasFocus()` — these windows are never OS-active), the centred pin,
  the ghost's own centre of mass, Enter's single checkpoint and the body's measured extent,
  one Undo, a pin click moving the anchor and the pinned end staying put, Esc leaving the
  shape `IsEqual`, centimetres, the group refusal, the mitred-end refusal on screen and in
  the toast — **and then the same size previewing once the mitred end is PINNED**, which is
  what makes that refusal about the end that moves rather than about the body — plus the
  banned-word sweep over the chip's own painted copy. Three mutations each went red on a
  named check: the pin defaulting to an end (`and it starts at the CENTRE`, plus two
  centre-of-mass checks), the preview deriving its own axis and anchor instead of asking the
  window (`and it kept its centre (x 225, expected 300)`), and the travel threshold ignored
  (`a right DRAG opens no field`). `kCheckFloor` was **not** touched: this branch ran
  filtered blocks only, and the floor is re-ratcheted by measuring an official run.
- **An end that cannot be PULLED is stretched from the middle instead (improvements item
  13).** The user's report is the whole specification: *"is like for example if I add bevel
  or a cut angle in the shape i could not use the re measure tool"* — a mitred, chamfered,
  rounded, stepped or L-shaped end is not one flat face square to the axis, so there was
  nothing for `pullFace()` to move and the tool refused the board outright. It refused the
  exact board somebody wants to make 150 shorter. `ModelingOps::stretchAlongAxis()` takes
  the length out of the board's STRAIGHT PART instead: cut across the middle, slide the end
  piece along the axis by the difference, fuse the two again — so every feature survives at
  its own size (a 45 stays a 45, an 8 mm chamfer stays an 8 mm chamfer) and the two untyped
  sides are untouched to the micron, which is the promise re-measure makes everywhere else.
  Growing copies the straight slab `[cut-delta, cut]` as the filler, and the result is
  MEASURED against the requested length before it is accepted. `resizeAlongAxis()` routes an
  unpullable end here and shifts the result per anchor, so Low/Centre/High still mean exactly
  what they meant. `ResizeCheck::EndNotFlat` is **deleted, not left unreachable** — a code no
  caller can see is a sentence no user can be shown — and what remains is
  `NoStraightPart`: there is a middle to take the length out of, or there is not. The
  headless oracle is volume arithmetic rather than a picture: a 45 through an 18 mm thickness
  removes `0.5 × 18 × 18 × 300` whatever the board's length, so a board shortened by 150 must
  come back at the straight board's volume less exactly the same corner, and a stretch that
  sheared the mitre off would come back bigger.
- **Honest limits.** The ghost wears the app's one preview look (the yellow body caged in
  its own outline), not the mockup's green. The mockup's "stays" and "moves" captions are
  not drawn beside the line — the status bar carries that instead (`… — keeping the centre
  — both ends move by half`).

#### The custom transform gizmos: three 3D tools, one chip, no AIS_Manipulator

Spec: `docs/superpowers/specs/2026-09-06-custom-gizmo-design.md`, complete through Phase 2
plus a user-directed redesign (2026-09-08): the original "exactly like the axis card"
view-plane drawing shipped, went through four visual feedback rounds (proportions, then
foreshortening, then cropped rings twice), and was **rejected by the user** in favour of a
true 3D Unity/Blender-style gizmo. Do not resurrect the card-parity drawing; its lessons
that still bind (ratio pins beat shared constants, trust the pixel) are recorded where
they generalize.

- **The design: unlit 3D solids along the world axes.** `MoveGizmoRenderer` is three
  arrows (cylinder shaft + cone of revolution, `BRepPrimAPI`) with a neutral pivot sphere;
  `RotateGizmoRenderer` is three tori; `ScaleGizmoRenderer` is cube-tipped arms plus the
  sphere. Everything draws UNLIT (`Graphic3d_TypeOfShadingModel_Unlit` per aspect) in the
  exact `Theme::gizmoAxisX/Y/Z` tokens — a rendered pixel IS the token, which is what the
  suite's colour counts stand on — with `SetFaceBoundaryDraw(false)` and meshed by
  `BRepMesh_IncrementalMesh` before display (an untessellated face draws nothing).
  Perspective foreshortens the arms naturally; a sphere is a perfect circle from every
  angle, so the flat-ring crop defect cannot exist by construction.
- **Sized in screen pixels at its own depth.** `kArmPixels` (92) × `Theme::gizmoScale()`
  (a persisted Appearance token, "Gizmo size", 0.5–2×) × `worldPerPixelAt(pivot)` —
  `worldPerPixel()` answers at the camera TARGET's depth, a gizmo stands at the body's
  pivot, and in perspective the difference made its screen size drift with every zoom
  until the depth ratio joined. `GizmoRenderer::viewDependent()` is false for all three:
  the drawing depends on the pivot and the zoom only, so an orbit at constant distance
  rebuilds nothing at all.
- **Hover is ours, and so is the cursor.** The handles have no `ComputeSelection`, so
  OCCT's hover pipeline cannot see them; `updateBodyGizmoHover()` runs the screen-space
  hit tests on the ordinary hover path, pushes the hovered axis into the showing renderer
  (which redraws that handle at `.lighter(155)` — still unlit, still exact), and derives
  the `PointingHandCursor` from the same answer. A live drag keeps the highlight the
  press set.
- **Drag maths, one rule per tool, all frozen at the press.** Move measures against
  `myMoveDragLine`, the world axis line captured at the press — never the renderer's live
  `armAxis()`, because the Move gizmo TRAVELS with its drag (standing where the body will
  land, same offset as the ghost) and a live line's origin would carry the very offset
  being measured. Rotate intersects the cursor ray with the grabbed ring's own plane and
  takes the signed angle from the press vector about the frozen axis (15° snap); Rotate
  and Scale gizmos stay put, their pivot being the fixed point of the edit. Scale is the
  Move maths read out as a factor of the arm's press length (5% snap, clamped to the
  Milestone 2 band in `dragTransform()` and at the viewport alike). Hit tests:
  `moveGizmoAxisAt()`/`scaleGizmoAxisAt()` (nearest handle, dead inner third),
  `rotateGizmoAxisAt()` (the drawn torus sampled as a screen polyline), all through
  `segmentPixelDistance()` at the shared 14 px `kHandleGrabPx`.
- **One chip serves all three.** `MoveTool` (name kept — the suite and MainWindow address
  it) reads `MainWindow::bodyTool()`, shows the active tool's renderer (each `show*Gizmo()`
  clears its two siblings — the missing clear in `showMoveGizmo()` was the user-caught
  fourth-Space bug), previews through `dragTransform()` — ONE derivation for ghost and
  commit — and commits through `MainWindow::transformBody()` unchanged. Escape cancels via
  `cancelBodyGizmoDrag()`; every release is swallowed. `moveToolBodyId()` is
  `transformableBodyId()` whole — the "and the tool is Move" term died with the
  manipulator.
- **`AIS_Manipulator` is DELETED** (Phase 2 cleanup, 2026-09-08): the attach/detach/size
  machinery, the drag branches, the Deactivate-around-picks shields, the double-click
  `GizmoPickShield` and the `gizmoReleased` signal are gone from
  `OcctViewWidget`/`MainWindow`, and the selector tolerance stand-down went with them —
  `applySelectionTolerance()` keeps Auto's raised tolerance up unconditionally now, since
  our handles register nothing it could blur, and the suite pins that the edge's full
  reach survives a gizmo. Tombstones at the old API sites; the measured findings
  (zoom-persistence default, the styling wall) stay in Pitfalls as history.
- **The suite's gizmo blocks were rewritten with the deletion** —
  `rotate-and-scale-are-ours-rings-cubes-and-drags` (ring and cube drags end to end),
  `the-3d-gizmos-unlit-tokens-hover-and-handles` (hover brighten + cursor, dead negative
  handles, ink at fifteen tool×camera combinations), and the size block now pins ONE
  screen size at every zoom plus the `gizmoScale` token. `kCheckFloor` was LOWERED
  deliberately to 3100 with the deletions. **That debt is settled** — the joinery branch
  re-ratcheted it to a measured **4126** (see the joinery section's closing note for the
  one caveat that came with it).

**`Theme` is spec-backed** since the Appearance panel: every colour accessor and the four
derived fonts (badge = base−2, label = base−1, body = base, title = base+3 pt) read
`Theme::Spec`; `defaultSpec()` is **the user's own look** — Graphite plus six baked deltas
from `assets/defaultcolors.furnifytheme` (near-black viewport and grids, `#6a00ff` accent,
tinted hover cyan, 2px chip strokes; Milestone 5 item 1) — and all 29 colour defaults are
pinned to hex in the suite (three of them, `gizmoAxisX/Y/Z`, added in Milestone 3 — see
"Direct modeling"'s gizmo restyle note; the last two, `sizesOneBody`/`sizesGroup`, by the
selection sizes — see "Sizes around the selection"). `graphite()` stays as the readable base the deltas
diff against. Edits apply live through one `themeChanged` broadcast — no
widget may cache a colour across it — and persist **debounced** (400 ms, flushed on close),
because a colour-wheel drag fires per mouse-move. The picker opens with `show()`, never
`open()`: `QDialog::open()` forces window-modality regardless of `setModal(false)`, and
nothing in this app blocks. The OCCT body/preview materials are the remaining untokenised
colours, by scope ruling.

**HISTORICAL: the AIS_Manipulator styling wall** (the class is deleted since the custom
gizmo's Phase 2, and the wall is WHY the custom gizmo exists). Milestone 3 tried to carry
`gizmoAxisX/Y/Z` onto `AIS_Manipulator` and read its header end to end: no setter reaches
`Axis::myColor` at any access level, and a `SlimAxisManipulator` subclass that reached the
proportions measured the rendered cross-section **growing** as the radius shrank (34 px
stock → 80 px at 0.02 scale, monotonic — the thinning shaft revealing same-material parts
underneath). Reverted rather than shipped. The lesson that outlives the class: **trust the
pixel over the setter's name** — a setter that compiles and a header that looks reachable
are not evidence a control is doing what its name says. The 3D gizmos wear the exact
tokens unlit, which is what finally closed the ask.

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

### Sizes around the selection

Improvements item 5, picked from mockups (option B, "one overall size for the group"): a
selected body carries its width, depth and height as three dimension lines with boxed
numbers, in `Theme::sizesOneBody()`; a selected group carries ONE dashed box around
everything selected and the group's overall three, in `Theme::sizesGroup()` — no per-piece
numbers. The status bar says it too: `1 body selected — 600 × 300 × 18 mm`,
`3 bodies selected — overall 636 × 300 × 718 mm`. `View → Show sizes` turns the drawing off;
on by default, persisted as `showSizes`, no shortcut.

- **"Measured along its own sides" is an oriented box, and the world axes win when they fit
  just as well.** `ModelingOps::measuredBox()` (Qt-free, `headless_measured_box`) builds
  OCCT's `Bnd_OBB` in **optimal mode on the exact B-rep**. Measured on OCCT 8.0.1: every mode
  reads a 600 × 300 × 18 box to six decimals square, turned 30° about Z and tilted 37° about
  (1,2,3), but on a tessellated cylinder the triangulation modes read 299.9 (the mesh sits
  inside the surface) and non-optimal-exact 300.1; optimal-exact reads 300. The world
  AABB of that turned board reads 669.6 × 559.8 — neither side — which is this app's most
  repeated bug class, an oriented quantity measured in world terms. But an OBB's axes are
  **arbitrary wherever the shape does not pin them**: a sphere's came back turned
  (0.104, 0.994, −0.027), a cone's by 0.002, and a skewed frame draws slanted lines and odd
  numbers on an ordinary cabinet. So the exact world box is computed too and **used whenever
  its volume is within `kPreferWorldBoxTolerance` (1%) of the oriented box's**. 1% because
  the gap it must absorb is optimizer slack (zero to six decimals on every shape measured),
  while a visibly turned board inflates its world box far more — 600 × 300 × 18 at 1° is
  +4.4%, at 0.25° +1.1%. The rule is about FIT, not about how the pieces sit: three
  axis-aligned squares stepping along a diagonal fit a turned box of a third the volume and
  are measured along that diagonal — pinned, and stated on the header. **Height** is the
  axis closest to world Z; **width** the longer of the other two. Both directions are
  mutation-pinned: forcing the world box reddens the turned/tilted board checks (headless
  and `gui_smoke`'s "a board turned 30 degrees reads its own 600 x 300 x 18"), dropping the
  prefer-world rule reddens the cabinet and free-turning group frame checks.
- **One predicate, derived.** `MainWindow::selectionSizesVisible()`: Show sizes on, a
  furniture open, not sketching, `selectionKind() == Body` (a face or an edge keeps its own
  annotations), not render mode, no body-gizmo drag live. `refreshSelectionSizes()` applies
  it from `appStateChanged` **and from the gizmo drag/release signals**, because a drag
  emits no `appStateChanged` until it commits. Render mode clears the selection on entry, so
  its term is only reachable by a body selected *inside* render mode — which is exactly what
  the suite does to pin it (the mutation that drops the term goes red there and nowhere
  else).
- **Measure once, lay out on every camera move.** The box is cached in `MainWindow` on the
  sorted selected ids and `DocumentModel::revision()` (dropped by `resyncView()`, the joint
  cache's reason), so a pull, a scale or an undo re-measures while an orbit never does —
  the suite counts `ModelingOps::measuredBoxCallCount()` across a 40-step orbit and requires
  zero, and a mutation invalidating the cache on `cameraChanged` read 40. `OcctViewWidget`
  keeps only the box it was handed and re-lays it out from `applyCameraState()`: which of
  each extent's four parallel edges carries the line (width and depth share bottom/left by
  which runs more across the screen, height goes right, pushed out along the edge's face
  that points furthest that way). The pixel size is `worldPerPixelAt(box centre)`
  **quantized to 5% steps**, and the layout choices carry **hysteresis** — both measured
  necessities: the orbit-pacing probe wiggles about the fit-all view at azimuth −45, which is
  precisely the tie between a square box's width and depth, and without hysteresis the two
  swapped on every step (60 rebuilds in 60 moves, send cost 0.19 ms/move); with it, 0
  rebuilds and 0.05 ms. The frame count stayed 1.00 per move throughout.
- **Composition over `DimensionRenderer`, not a sibling.** The three lines are the edge
  annotation asked for with a `DimensionRenderer::Style` — a token accessor (read at every
  build) and `boxedLabel`: a panel-filled box with a 2 px border in the token, **centred on
  the dimension line** so the number interrupts it and stays readable at any angle. The box
  is its own `AIS_InteractiveObject` under `Graphic3d_TMF_ZoomRotatePers` anchored at the
  line's midpoint — under that persistence one local unit is one device pixel and the axes
  are the screen's — sized from `QFontMetricsF` of the same face at the same 13 px OCCT
  draws the text at. The fill is a `Graphic3d_ArrayOfTriangles` with Unlit shading and
  double-sided face culling set on its aspect, and it DOES draw — seen in both captures and
  counted by the suite's token-pixel checks — against the arrowhead note above, whose
  triangles drew nothing; which of the two settings made the difference was not measured. The style
  says how it looks, never which case is asking.
- **Its own layer: Immediate, no depth test, no depth write**, inserted before the joints'
  layer. An annotation around a body has to read over it — a far edge's line or an outline
  edge behind the body is exactly what a depth-tested layer hides — and Immediate keeps it
  out of the normal layer list the shadow pass walks. With no depth test, display order is
  paint order, and **display order is not stable across rebuilds**, so priority does the
  work: boxed numbers `Graphic3d_DisplayPriority_Above`, the dashed outline `_Below` (the
  first group capture had the outline striking through "300 mm").
- **Live status, never somebody else's sentence.** `selectionStatusText()` is the one author;
  `refreshSelectionSizes()` rewrites the bar only while it still shows the sentence this
  window last painted (`myLastSelectionStatus`), the refusal-withdrawal rule — so a
  commit's own Note (`Body 01 scaled — …`) or a kind-lock refusal stands. The size rides only
  on a Body selection, and it stays in the status bar with Show sizes off: the preference
  governs the drawing, and a line of status text covers nothing.
- **The Items drawer stopped showing a size once this existed.** A row is name and eye button
  only now — `ItemsPanel::Row` carries no dimension label, and `rowTextAt()` reports the name
  alone. The card then went from 240 to **200 px** at the shipped scale by the user's call
  ("adjust the width a little bit"), `cardWidth()` now growing it by what a typical renamed
  piece ("Left side panel") costs at a larger type scale rather than by a dimension string
  no row paints. Side benefit: the row used to
  read `Measure::formatDimensions()`, the WORLD-axis bounding box — the exact "neither side"
  bug the oriented-box paragraph above describes (a 600 × 300 × 18 board turned 30° read
  669.6 × 559.8 in the list) — while the viewport's own selection sizes read the board's true
  600 × 300 × 18 from day one of this feature. (The drawer grew folders and a scroll area
  later — see "Improvements" — but a row is still name and eye button, and the folders work
  did not put a size back on one.)

### Files, versions and the library

Milestone 3's rule: a furniture is a file, and the file always round-trips or refuses —
never half-loads and never lies about having saved.

- **`FurnitureStore` owns `Documents/FurnifyMe/`** (created on demand via
  `QStandardPaths::DocumentsLocation`; the root is *injected*, never looked up inside the
  class, the same discipline `UserProgress` and `ScopedTestSettings` already established, so
  the suite never touches a real user's files). "One `.furnify` file is one furniture" is the
  spec's words, not the literal disk layout: a furniture is a **directory** under the root,
  named by id — `manifest.json` + `shapes.bin` + `thumb.png` + `versions/*.bin` — and nothing
  outside `FurnitureStore` touches a path inside it. **That layout is store-private** and can
  change (zipping the directory into one real file is the obvious future move) without any
  other file in the app noticing.
- **`.furnify` = a JSON manifest (names, visibility, the symmetry block, the versions index)
  plus the shapes themselves via `FurnifySerial`**, a thin, Qt-free wrapper (`furnify_geometry`
  target, headless-tested) around OCCT's own `BinTools_ShapeSet` — exact B-rep, no
  tessellation loss. `FurnifySerial` knows nothing about names or manifests; a blob format
  that also carried labels could not be reused the day something other than a whole document
  needs the same round-trip (a version snapshot already is that reuse). Both the blob and the
  manifest carry their own format version and **refuse a future one outright** rather than
  guessing at a newer layout — guessing is exactly how a document silently loses data.
- **Every load is scratch-then-swap.** `FurnitureStore::loadFurniture`/`loadVersion` decode
  into a fresh `DocumentModel` and only assign to the caller's document on success;
  `DocumentModel::fromSerialized` additionally validates everything — vector-length matches,
  no null shape, every outline face actually `TopAbs_FACE` — *before* mutating `this`, so the
  scratch discipline is belt, and the validation is suspenders. A refusal is a Failure toast
  naming the file; the init screen stays up. **A refusal must never surface as a success** —
  the same law `BooleanResult::ok` already enforces for a failed boolean, now enforced for a
  failed file.
- **An absent manifest key defaults off — symmetry pre-dates files by one task.** Task 1
  reserved the `symmetry` key before Task 4 existed to write it, specifically so a version or
  furniture saved in that gap loads as symmetry-off rather than refusing outright. The rule
  generalizes past this one key: a manifest is read forward-compatibly, never assumed
  complete.
- **Save (`Ctrl+S`) and autosave are never a checkpoint** and never touch the undo stack —
  writing a file to disk is not an edit to the document, the same distinction visibility and
  the symmetry mode already draw against `checkpoint()`. Autosave is a persisted, checkable
  option: on, a save runs after every checkpoint, **debounced 400 ms** — the Appearance
  panel's own discipline, because a drag or a fast sequence of edits must not thrash the disk
  once per intermediate state — and flushed on close or on returning to the init screen so a
  debounce window can never eat the last edit.
- **Close asks — only when there is something to lose, and never in a dialog.** (Improvements
  item 3, the user's Option A; it replaced "close-saves-first", which saved silently and left no
  way to throw an experiment away.) Nothing unsaved — nothing changed, or autosave already
  caught up — and both exits close straight away, exactly as before. Unsaved changes, and
  both exits ask the SAME question: `UnsavedCloseCard`, a sibling over `OcctViewWidget` that
  dims the viewport edge to edge and asks "Save changes to <furniture> before closing?" with
  **Save and close** (Enter), **Close without saving** and **Keep editing** (Esc). The X
  still quits and File → Close furniture still returns to the library; only what an answer
  carries out differs (`MainWindow::CloseRoute`). The card is `ExtrudePreview`'s shape: an
  app-wide key filter installed on show and removed on hide that claims EVERY
  `ShortcutOverride` in its window (a Ctrl+Z reaching its action behind the question would
  change the very document it is about), a scrim that swallows every mouse event, buttons
  carrying `WA_NoMousePropagation` and reached by `childAt()` in the suite; `updateActions()`
  closes the modeling gate while it stands. Three rules each answer holds. **Save and close
  is one save attempt, and a failure aborts the close** — the card hides *before* reporting
  the answer, so the Failure toast lands on a clear viewport, and the window stays open on
  the dirty furniture. **Close without saving writes nothing on its way out**, which is a
  claim about three writers, not one: the autosave debounce is stopped when the question is
  asked, `flushAutosave()` and the timed tick both refuse while it stands, and both refuse the
  revision the discard threw away (`myDiscardedRevision`) — so `showInitScreen()`'s own
  pending-debounce flush cannot put it on disk either. The suite's oracle is the whole
  library directory's bytes, never a reload that happens to count the same bodies. **Keep
  editing** closes and saves nothing, and re-arms the debounce it stopped. **Render mode is
  left first**: asking is leaving the editor, which render mode's own gate already treats as a
  document change, and the question then stands over the modeling viewport where its dot is
  on screen. While there are unsaved changes the status bar names the furniture beside a
  `Theme::caution()` dot (`FurnitureNameMark`), pushed from `isFurnitureDirty()` on every
  `appStateChanged` — derived, never stored — so the question is never a surprise.
- **A version is a named snapshot living inside the furniture's own file**, captured on
  demand only, never automatically — shapes, item names, visibility, and (the ruling that
  resolved the one real T1/T4 conflict) the symmetry pairings and plane, because a version is
  a snapshot of the *whole document* and a restore that silently dropped pairings would break
  live symmetry the moment the user undid the restore. **Restore replaces the current
  document through one undoable checkpoint** — an accidental restore is one Ctrl+Z away —
  while **version delete is file data, not document data**: it does not touch the undo stack
  at all, so instead of a toast with Undo it is a **two-click confirm** on the row itself
  ("Delete — click again to confirm"), final once taken. That taxonomy — document data goes
  through checkpoint+Undo, file data goes through a second click — is the same line "Save is
  never a checkpoint" draws, applied to deletion instead of writing.
- **Compare is a second, read-only `OcctViewWidget`** (`myViewerOnly`, no gizmos, no
  selection, no hover, no picking channels attached at all — skipped structurally in the
  constructor rather than left wired and merely unused) beside the live one in a `QSplitter`,
  with **cameras synced both ways, epsilon-guarded against recursion**: each side applies the
  other's state only when it actually differs, the same `appStateChanged` no-reentry
  discipline used everywhere else in this app. Restore closes the compare pane first, so
  "replace the document" and "look at two documents side by side" can never overlap.

### Live symmetry is twins, not replay

Milestone 3's live symmetry works because it never tries to *replay* an edit onto a second
body — it keeps two independent bodies and re-derives one from the other every time either
changes.

- **`mirror(edit(A))` replaces the twin, in the same checkpoint that committed the edit to
  A.** No operation is recorded and no face correspondence is tracked: whatever `edit(A)`
  produced, its mirror through the symmetry plane is *definitionally* the correct twin, so
  `ModelingOps::mirrorShape` (Qt-free, headless-tested — mirror twice is the identity within
  tolerance, volume is preserved, the centre of mass reflects exactly) is the entire
  mechanism. One gesture — pull, bevel, transform, a boolean, a rename — produces one
  checkpoint covering both bodies and one toast naming both.
- **`symmetryOn()` gates every `twinOf()` read**, not just the ones that create pairings.
  `DocumentModel::State` keeps the **pairing map** (`twin`) as checkpointed, undo-tracked
  content — an edit under symmetry pairs bodies inside a commit, so undoing that commit must
  restore the pairing exactly as it stood, the same way it restores names. The **mode**
  (`symmetryOn`/`symmetryPlane`) deliberately does **not** ride in `State`: it is a session
  setting set outside any checkpoint, the same rule presentation visibility already follows,
  and treating it as undo content let an undo land *after* "turn symmetry off" and silently
  turn it back on, resurrecting whatever pairing that older checkpoint had captured — a mode
  switch reanimated by a Ctrl+Z aimed at something else. Every consumer of a pairing therefore
  checks `symmetryOn()` **as well as** `twinOf()`: a pairing entry surviving in `State` is
  inert the instant the live mode is off, whatever undo does to the map underneath it.
- **Plane change and toggling symmetry off both unpair everything, with no checkpoint, but
  both bump `revision()`.** Unpairing is not an edit a Ctrl+Z should have its own entry for —
  it is a mode/plane change, the same category as visibility — but the manifest's symmetry
  block has genuinely changed, so a furniture with autosave on has to notice and write it
  back. A new plane invalidates every existing pairing's *meaning* (each mirror was computed
  against the old plane), which is why changing the plane unpairs rather than re-mirroring in
  place. Turning symmetry back on does not re-pair what was unpaired — pairing happens at
  creation only, and there is no record of what used to go with what once the map is cleared.
- **A boolean between a body and its own twin collapses the pair to one unpaired body** — a
  body fused with its own mirror is the symmetric whole, not two halves any more — while a
  boolean between a paired body and an unrelated one leaves the edited side paired as usual
  (its twin replaced by the mirrored result, same as any other edit).
- **A body that straddles the symmetry plane at creation is left unpaired.** Straddling means
  the user is modeling on the centreline itself, where a mirror twin would be redundant
  geometry sitting on top of the original — pairing is only offered to a body that is wholly
  on one side.
- The kernel bug the commute test caught is worth remembering past this milestone:
  `pullFace` used to trust `TopAbs_Orientation` to decide which side of a face is "outward",
  which is wrong for a mirrored (negative-determinant) body — a `BRepClass_FaceClassifier`
  probe at a genuine on-face point replaced it. `bevelAxis`/`outwardNormalNear` carry the same
  flag-only-normal risk on mirrored bodies and are **not yet fixed** — ledgered, out of scope
  for the task that found it, worth checking before trusting a bevel gesture on a mirrored
  body.

### Render mode

`View → Render mode` (menu-only, checkable, **session-only** — it is never persisted, the app
always starts in modeling) strips the viewport down to the furniture and nothing else.

- **"The viewport is the furniture alone" reaches every overlay, not just the obvious ones.**
  Grid, axis-gizmo card, rail, drawers, dimension, markers, every live gizmo, and the symmetry
  plane indicator all hide — the indicator is scene decoration exactly like the grid, so the
  same rule applies to it, and it re-derives its "genuinely on screen now" state off
  `myRenderModeActive` rather than off "the mode is on", closing a hole where it survived an
  orbit mid-render-mode. The two teaching surfaces, `WalkthroughPanel` and `HintBalloon`, could
  **not** take the same blunt hide: both classes read their own `isHidden()` as a semantic
  signal — "restart the walkthrough from scratch", "never show this hint again" — so an
  external, generic hide would have corrupted progress or permanently burned a hint's one
  showing. Both suppress **non-destructively** instead, inside their own `refresh()`/
  `reconsider()`, so progress resumes rather than restarts when render mode exits.
- **Exits are any state or document change, including the Symmetry toggle.** Every
  `myDocument.checkpoint()` call site runs through one `checkpointDocument()` choke point that
  exits render mode first; undo/redo, Start Sketch, opening compare and returning to the init
  screen each get their own explicit one-line exit, because they either take no *new*
  checkpoint or are literally render mode's own gate reached by menu with the rail hidden.
  `setSymmetryEnabled()` also exits first even though unpairing takes no checkpoint of its
  own — it bumps `revision()` and dirties the furniture, which is document-changing by the
  same test every other exit uses. A viewport press is intercepted at the top of
  `mousePressEvent()` (left button only) and swallowed; orbit/pan/zoom/Fit All are untouched,
  because framing a shot is not a modeling gesture.
- **PBR belongs to path tracing alone; every other tier is Phong.** Milestone 4 fix round 2
  scoped the PBR shading model, filmic tone mapping and the PBR material set to *both*
  ray-traced tiers (`isRayTracedTier()`); the user-feedback round narrowed it to
  PathTracing (`usesPbrMaterials()`), for a measured reason. Whitted ray tracing does not
  tone-map — OCCT's own header says `ToneMappingMethod` is for path tracing — and has no
  indirect bounce to fill a shadow, so a PBR floor under it rendered its cast shadow at
  **0.32 of the lit floor**, the near-black the user rejected. Handed the same Phong model
  and the same Milestone-3-calibrated floor and body materials the Shadows tier has always
  used, the identical scene measures a floor that blends to 2/255 and a **0.86** cast
  shadow: not the reference's 0.70, but a readable shadow rather than a hole. `isRayTracedTier()`
  still answers "which tiers drive OCCT's ray-tracing `Method`"; the two questions are now
  two predicates.
- **The studio rig is per-tier, and its gains are sampled pixels.** `SetDefaultLights()`
  gives a directional key at intensity **20** beside an ambient at **1** — calibrated for
  flat modeling legibility and wildly over-driven for an integrated render, which clipped
  the path-traced floor to white at every exposure worth having. `applyRenderLightsForTier()`
  applies `kPathTracingAmbientGain`/`kPathTracingKeyGain`/`kPathTracingKeySmoothAngleRad`
  for PathTracing and `kRasterAmbientGain` for the rest, **on top of** `myRenderLightStrength`
  rather than instead of it, so the Light strength control still opens the key by the factor
  it always did. The rasterized tiers' unlit faces read 0.55 of the lit floor before that
  ambient fill and 0.72 (Shadows — *calibration-run number, 0.747 today; see the Phase-3 A/B
  below*) / 0.65 (RayTracing) after, **and the floor does not move
  with it** — the Milestone-3 floor material's ambient reflectance is zero by construction,
  which a measurement confirmed rather than assumed. Ambient lights and the key's cone angle
  are saved at entry and restored at exit like the direction and intensity already were:
  an ambient left scaled has *no symptom inside render mode*, only ordinary modeling coming
  back washed out, once, forever.
- **The tier probe** tries `Graphic3d_RM_RAYTRACING` first, timed against one redraw at a
  roughly **100 ms** threshold (1500 ms for the path-traced tier, which pays a one-time
  shader compile on its first frame; try/catch around the OCCT call, since ray tracing can
  throw on hardware that does not support it), falls back to shadow-mapped rasterization
  (`Graphic3d_CLight::SetCastShadows`, Dump-pixel-probed to confirm shadows are actually
  drawn), and falls back again to plain rasterization. The chosen tier is **cached for the
  session** — probed once, at first activation — and reported in a **Note** toast, which means
  it goes quiet with `View → Show notifications` off: Notes can be silenced, Failures cannot,
  and a tier announcement is a Note by that same taxonomy, not a refusal.
  **A redraw that is not waited for is not a timing**, and the QOpenGLWidget migration took
  the wait away: the probe's whole method is "time one redraw", which measures nothing unless
  the call returns after the GPU has done the work. It used to — OCCT owned the surface and
  ended `Redraw()` with a buffer swap, and a swap synchronizes. Qt owns the frame now, the
  driver runs with `buffersNoSwap`, and GL commands are asynchronous: the first path-traced
  redraw timed **1 ms** against a 1500 ms threshold, which is not a fast GPU, it is no
  measurement at all — every GPU would have been handed the top tier, including the ones the
  probe exists to protect from it. A `glFinish()` now closes each timed redraw (3 ms measured
  here, hot shader cache), and `TierProbeTimings` records what was measured beside the tier
  that was chosen, including whether the sync was even available — `gui_smoke` asserts that
  the tier **follows from** those numbers rather than only reporting the outcome.
- **The studio dressing (2026-09-02, user-directed rework):** a flat light warm-grey backdrop
  (`renderBackdropColour()` — the user's reference shot, blended 4:1 toward the viewport
  token so Appearance edits still move it; the original gradient was rejected because only a
  flat colour shared with the floor makes the floor's seam invisible), a **shadow-catcher
  floor** (`showRenderFloor()` — a large matte **disc** a hair below the lowest *displayed*
  body, backdrop-coloured, selection mode −1 so it can never be picked or hovered, rebuilt
  on a theme edit, absent on an empty document. A disc since the user asked for one — "in
  the render mode we have the floor that currently is a plane, can be a circle?" — built as
  `BRepBuilderAPI_MakeEdge(gp_Circ)` → wire → face and tessellated at `radius/400`, which is
  fine enough that the rim reads as a curve rather than a polygon at any framing this app
  offers. The user asked for a circle "that fades", and what carries the fade is the floor
  material's existing calibration rather than any gradient added for it: the floor is built
  to land within a few levels of the backdrop, so the rim measures a step of **3 to 7 of 255**
  (backdrop `(193,191,186)` against a floor of `(196,195,193)` just inside it, sampled on
  `render-mode-after.png`) and reads as a soft arc. There is **no alpha ramp on the disc**,
  and there deliberately is not one yet: the floor is a shadow catcher, and blending it is
  the one change that would take the cast shadow with it), an **angled key light** (every directional
  light's direction, intensity and headlight flag saved at entry and restored at exit —
  straight down, the whole shadow hides under the body; **OCCT's default directional light
  is a HEADLIGHT whose direction is read in VIEW space**, so `SetHeadlight(false)` must come
  first or the "studio key" silently follows the camera — a doubled intensity changed no
  pixel on the top face until that flag fell, which is how it was found), **forced shaded with no edge ink**
  (a render is never a wireframe and draws no face boundary lines — the GRAY30 edges
  `displaySolid()` puts on shaded bodies read as "still wireframe" in a shot; `myWireframe`
  is untouched, `setWireframe()` records-without-repainting while active, and exit
  re-applies flag and boundaries unconditionally),
  and `ShadowMapResolution` at 4096 on the Shadows tier only. The floor's material is
  **calibrated against sampled Dump() pixels, not derived from the lighting equations**
  (trust the pixel): the default rig is too weak for any lit diffuse to reach the backdrop
  tone, so EMISSIVE carries 87.5% of it — shadow-immune, which is what makes the seam
  invisible — and the white diffuse layer on top is exactly what the shadow map subtracts,
  landing the lit floor within 3/255 of the backdrop (*calibration-run number, 6/255 at
  today's sample point; see the Phase-3 A/B below*) and the shadow ~25% under it. The
  floor goes up **before** the tier probe, deliberately: the tier-2 pixel probe must measure
  the scene the user will see — with no floor, a straight-down shadow could touch no pixel
  and the probe would fall to Plain on hardware that shadow-maps fine.
- **`Save Screenshot` exports at the size the user picked, on every tier, through an
  OFFSCREEN buffer that survives several redraws.** `ToPixMap()` renders its own buffer and
  reads it in ONE call, and on a progressive tier that is exactly one sample: six successive
  calls returned the identical pixel bit for bit, `SamplesPerPixel` ×
  `AdaptiveScreenSampling` swept across ten combinations changed nothing, and it exported a
  floor at 140 where the on-screen buffer at rest reads 194 — a third darker than the picture
  the user was looking at when they asked for it. The first fix was to export the CONVERGED
  ON-SCREEN buffer at 1× instead (only it accumulates, and only `Dump()` reads it), and that
  bought the right image at the window's own size and nothing else — which is what the
  resolution picker then broke: asked for 1440, the user got a 1200×800 file and said so.
  `OcctViewWidget::dumpOffscreen(path, pixels, withAlpha)` is the answer to both at once.
  `FBOCreate()`/`SetFBO()` hand the view a buffer that survives repeated `Redraw()`s, and a
  path tracer accumulates across redraws **of the same target** — so the passes go in at the
  requested size, the buffer is read once with `BufferDump()`, and a 4K path-traced export is
  the picture that was on screen with four times the pixels. The camera's aspect follows the
  buffer and is restored, the accumulation the export spent is restarted, and
  `renderExportSizeApplies()` now answers true for every tier because the reason it did not
  is gone. **Two OCCT facts this cost, both measured rather than reasoned:**
  `Image_AlienPixMap::InitTrash(format, w, h)` + `SetTopDown(false)` must run BEFORE
  `BufferDump()` — it reads into a pre-sized image and an empty pixmap simply fails, which
  surfaced to the user as "Screenshot failed — Couldn't save the image"; and alpha writes
  alone do not stop the opaque backdrop being drawn, so a cut-out needs
  `Graphic3d_TOB_NONE` as well as `buffersOpaqueAlpha = false`. There is one
  `saveSnapshot()`, so the menu entry, the shutter and the furniture thumbnail all get this.
- **`ExportSize` is `Viewport / 720 / 1080 / 1440 / 2160`**, a picker on the render settings
  card ("i notice something, the render is not Full HD, can you add an option to select the
  resolution"). The height is what is chosen and the width follows the viewport's aspect, so
  a framing the user set is never recomposed by an export. **The cut-out is its own export**
  (`saveCutoutSnapshot()`, "an option to make .png images of the render with out the floor
  and background"): the floor comes down, the background type goes to `Graphic3d_TOB_NONE`,
  the driver's `buffersOpaqueAlpha` goes false, and `dumpOffscreen(..., withAlpha=true)`
  writes RGBA. Both settings are put back afterwards whatever happened.
- **The calibrated numbers survived the QOpenGLWidget migration unchanged — measured, not
  assumed.** Phase 3 re-ran the identical suite against the pre-migration commit and against
  the migrated branch on the same machine and scene, and **every render-mode number is
  byte-identical**: PathTracing chosen both times; Shadows floor blend Δ 3/5/6; PathTracing
  floor blend Δ 7/7/8; Shadows shadow ratio 139/186 = **0.747**; PathTracing shadow ratio
  122/191 = **0.639**; converged export floor 194 against an at-rest 191. Nothing was
  retuned, because nothing moved. Two documented numbers *above* are older than that A/B and
  were already stale against this scene — the Shadows ratio is 0.747 here, not the 0.72 the
  ambient-gain paragraph quotes, and the floor lands within 6/255 of the backdrop at this
  sample point rather than 3 — so read those two as the calibration run's own numbers, not as
  a claim about today.
- **The Dump and the screen are one buffer, and the convergence tick is what nearly broke
  that.** Every calibrated pixel above is read out of a `V3d_View::Dump`, and OCCT no longer
  owns the surface — so "the probes measure the render" and "the user sees the render" became
  two claims. Measured side by side (a `Dump` beside a `PrintWindow` capture of the live
  window, compared as median colour and as median neighbour delta, both mapping-free), they
  disagreed: the Dump converged and clean, the screen a **grain of 24** against the Dump's 1
  and a full 85/255 darker. The cause was one line — the migration rewrote every "put this on
  screen" `Redraw()` into `scheduleRedraw()`, and the path-tracing convergence timer's tick
  was one of them, so each 50 ms tick told OCCT the scene had moved and restarted the
  progressive accumulation. The render the user was looking at never got past its first
  sample no matter how long they left it alone; the probes never noticed, because they drive
  `Redraw()` directly inside a `GlScope`. `scheduleAccumulationFrame()` — `update()` with no
  `Invalidate()` — is the tick's own route now, and the pair reads **grain 1 against 1,
  medians 2.4/255 apart**. `gui_smoke` pins both halves.
- **The polish does not stop while the user is still looking.** The user gate on Phase 3 was
  "it converges and looks right, but visible fine grain remains at rest", and the cause was
  `kPathTracingConvergeMs` running out: 4000 ms at a 50 ms tick is 80 passes, and the image
  then froze on whatever noise was left. That window is now the **burst** — the responsive
  first polish after a camera move — and `kPathTracingIdlePasses` (1200) carries on at a
  wider interval afterwards, on one timer with two budgets, both restarted by every
  `applyCameraState()`. Measured on the composited window, mean neighbour delta ×1000 over
  open floor: **1794 frozen at depth 66**, against **1201** at 10 s, **445** at 30 s and
  **237** at 60 s — four times cleaner at half a minute, seven at one. The idle interval is
  `max(50 ms, 2 × the tier probe's measured frame cost)`, so a GPU that scraped into this
  tier at several hundred milliseconds a frame polishes at its own pace instead of queueing
  paints an orbit would have to wait behind.
- **OCCT's sampling knobs do not help here, and that is a measurement, not an assumption.**
  At equal wall clock and equal depth (~160 passes, 10 s), `RadianceClampingValue` at 3.0 and
  at 1.5 measured 1431 and 1413 against the default 30.0's 1450 — inside the ~20%
  run-to-run spread, which is what a studio scene lit by one soft key should do, there being
  no fireflies anywhere near a clamp of 30. `AdaptiveScreenSampling` **off** measured 1861
  and p95 7 against 4: meaningfully **worse**, so the existing choice is confirmed rather
  than changed. Nothing was adopted; the win is depth, not parameters.
- **An export empties the buffer the user is watching.** `V3d_View::Dump()` restarts OCCT's
  accumulation — `awaitPathTracingConvergence()` already recorded that, which is why the
  export drives a fixed pass count rather than sampling until it converges — and the
  consequence nobody had followed through is that the frame left *on screen* after a
  screenshot is a single sample: measured at median (147,145,144) and grain 6 against the
  (196,195,192) and grain 1 it had been. `saveSnapshot()` therefore restarts the convergence
  and zeroes `accumulationDepth()`, so the picture polishes back within the burst window and
  the counter keeps meaning what its name says — a suite waiting on it to recover was
  otherwise already past its target before the first fresh frame arrived.

### Milestone 4: two windows, Mirror, links and render tiers

**The library is a second top-level window, and that is a quit trap unless wired once.**
`SelectorWindow` replaced the in-editor init screen; `EditorSelectorHandoff::wire()` is
the ONE implementation of the swap, called by both `main.cpp` and `gui_smoke` — a suite
driving its own copy of the handoff proves nothing about the app, and the first review
round caught exactly that drift. Two belts, both required: every leg shows the **target
first** and hides the source second, so at least one window is always visible; and
`setQuitOnLastWindowClosed(false)`, because Qt fires the last-window-closed check on a
mere `hide()` and a posted `QEvent::Quit` cannot be taken back later in the same call
stack. Quitting is TWO deliberate gestures since Milestone 5 ("dont show project selector
when app closes"): closing the selector, and closing the EDITOR - whose `closeEvent()`
asks first when there are unsaved changes (see "Close asks" above; a failed Save and close
aborts the quit with its toast readable, never silently) and only once answered emits
`quitRequested()` into the same handoff quit hook, so the X no longer
bounces the user to the library; File -> Close furniture remains the route back. The
Windows binary is also a GUI-subsystem executable now (`WIN32_EXECUTABLE` +
`/ENTRY:mainCRTStartup`, so `main()` stays portable) - no console window opens with the
app, while `gui_smoke` keeps the console subsystem it prints through.

**A live Mirror placement owns every left gesture in the viewport.** Auto's switch re-keyed
`mirrorPlacementEnvironmentOk()` from "body selection mode" to `selectionKind() == Body`, and
that quietly turned a deliberate act into an accident: under the old modes a press that missed
the plane handle changed the selection but never the MODE, so `refreshMirrorPlacement()`'s
self-cancel left a live placement alone; under auto the same press picks a face, an edge or
empty space, all three of which fail a Body term, and one stray click destroyed the gesture
silently. Two things fix it, and both are rulings rather than workarounds. The selection term
moved to `canBeginMirrorPlacement()`, because a changed selection genuinely does not
invalidate a running placement — `beginMirrorPlacement()` captured the ids it will pair and
never re-reads them — while what DOES invalidate one (a sketch starting, an outline waiting,
render mode, the handoff to the library, a compare pane) all still cancels it. And
`OcctViewWidget` suspends ordinary picking outright while a placement is live: the plane
handle owns its clicks, every other left press/release/double-click is swallowed, and RMB
orbit and MMB pan pass through untouched so framing the plane still works. It is the mouse
half of a claim the gesture already made on Enter, Escape and X/Y/Z.

**Mirror is a placed plane, not a toggle.** `S` begins a gesture: a plane with a draggable
handle, `X`/`Y`/`Z` to aim, Enter to commit, Escape to back out — one application-wide key
claim, disjoint from the other three by construction. The plane spawns **tangent** to the
selection's combined bounding box on the positive side of the active axis, never through
its centre: `DocumentModel::pairWithMirror()` skips a body straddling the plane it would be
mirrored across, so a centred default made the headline flow (one body, `S`, Enter) refuse
deterministically — the whole-branch review's C1, and the mechanism behind the user's
"could not make it work". An X/Y/Z flip re-places tangent on the new axis for the same
reason. Dragging can still put the plane inside a body; the skip rule then applies and the
refusal says so. `S` **always begins a placement**, including while mirroring is already on,
so additional bodies can join an existing mirror (`pairWithMirror()` skips already-paired
ones honestly); turning mirroring off is its own shortcut-less Model-menu entry, because
unpairing everything must not share a key with the gesture people reach for constantly.
`mirrorPlacementEnvironmentOk()` carries `myShowingInitScreen` and `isCompareOpen()` as well
as the sketch/pending-outline/render-mode terms — without them a live gesture survived the
editor↔selector handoff and carried stale body ids into a **different** furniture, where
they resolve to real, unrelated bodies because `myNextId` restarts at 1 per document.

**Linked copies are a group, and a body may not be both linked and mirrored.**
`DocumentModel::createLinkedCopy()`/`linkExisting()`/`unlink()` own `LinkGroup`; an edit to
any member re-derives every other through `propagateLinkedEdit()`, inside the **same**
checkpoint the edit took, at `commitReplaceBody()`'s choke. Link propagation and mirror-twin
follow are mutually exclusive by construction (the v1 exclusion, enforced from both
directions, and a serialized document naming a body in both is refused validate-before-
mutate). A refused propagation writes nothing and must not be reported as "linked copy
updated" — `linkedGroupSuffix()` takes a negative sentinel and says the copies could not
follow, because a group's membership count is not evidence anything was written to it.

**The grid follows a face-on orthographic look, and a sketch pins it.** `gridPlane()`'s
priority is: a locked face's plane → the plane of a sketch **in progress** (pinned by value
at Start Sketch) → the vertical world plane an effectively-orthographic Front/Back/Left/Right
look is squared onto (`faceOnOrthoPlane()`, the same construction `onStartSketch()` reads) →
the ground. The sketch term is what keeps an orbit mid-sketch from dropping the grid back to
the ground while the outline is still being built on XZ — a grid that stops showing where the
next click lands has stopped doing its job. One name per plane, too:
`MainWindow::faceOnDirectionLabel()` is read by both the Start Sketch message and the
persistent cue, so the world XZ plane is "Front" and YZ is "Right" whichever side the camera
is on. `Theme::Spec::gridDensity` is an Appearance token; the camera's elevation clamp is a
true ±90° with a pole-safe `upVector()`.

**Render tiers, best first: PathTracing → RayTracing → Shadows → Plain**, probed once per
session at first activation and cached. The lesson that cost the most: `SetPBRMaterial()`
never writes `Graphic3d_BSDF`, and the BSDF is the only description OCCT's path tracer
integrates — so the whole path-traced scene rendered black, bodies included, and three
rounds of floor-material theories chased a symptom. PBR + tone mapping are **PathTracing's
alone** (`usesPbrMaterials()`); Whitted ray tracing does not tone-map and has no indirect
bounce, so it and the two rasterizing tiers keep Phong and the Milestone-3-calibrated
shadow-catcher floor. Consequence the settings card has to admit: **Surface and Metal are
read by PathTracing only**, so on every other tier those two sliders move and change
nothing — the card shows a muted note saying where they apply, derived on every
`appStateChanged` from `OcctViewWidget::renderMaterialControlsApply()`, which IS the gate
the setters are wrapped in rather than a second copy of the tier list. On a ray-traced tier
a live material edit does not reach the next frame on this build at all (six distinct
redraw strategies measured, none moved a pixel); the value applies on re-entering render
mode, and the forced rasterize-and-back round trip that stays as the implementation is
wrapped in `try/catch` so a throw cannot strand `params.Method`. An **under-converged
path-traced Dump is systematically dark** — the accumulation buffer is a running mean — so
every measuring probe and `Save Screenshot` wait for convergence first.

### Joinery: a joint is a relationship, never a world position

Spec `docs/superpowers/specs/2026-09-10-joinery-design.md`, fourteen tasks, all merged. This is
a **planning layer and nothing else: no code anywhere cuts a shape.** A joint is a plan for
wood the user will cut by hand — drawn as ghosted hardware seen through the boards, read off
as numbers to mark with a pencil and a square.

- **`DocumentModel::Joint` is `{id, kind, bodyA, bodyB, params, adjustments}` and holds not
  one coordinate.** Where the hardware actually is gets DERIVED from the two live shapes every
  time it is wanted — `Joinery::derive()` runs `findContact` → `validityOf` → `layout` →
  `readout` end to end — so a joint follows its pieces for free: pull a face, bevel an edge,
  move a body, and the dowels are in the new wood with no correspondence tracked anywhere.
  This is the same argument "twins, not replay" makes for symmetry, one layer up: a stored
  world point would have to be re-derived at every edit site, and the site somebody missed
  would be the one that lied. Even a per-item override is stored in the CONTACT's own (u, v)
  frame (`Joinery::Adjustment`) rather than as a point, for exactly that reason.
- **Ten kinds on three families** (`Joinery::familyOf`): **Fasteners** — Dowel, Pocket screw,
  Biscuit, Domino, Screw — lay N discrete items in a row along the contact; **Housings** —
  Dado, Rabbet, Groove — cut a channel in one piece that the other sits in; **Interlocks** —
  Mortise and tenon, Half-lap — remove complementary material from both. One `Parameters`
  block serves all three and a kind reads the fields that apply to it, because the block is
  persisted, undone and edited as a unit; a variant would buy type-safety at the cost of three
  serializers. Defaults come off the wood (`defaultsForContact()` knows each piece's own
  thickness at the joint, so a half-lap is half of EACH piece and a mortise is capped at its
  host's thickness — a default that punches out of the back of the host is not a proposal).
- **Invalid is never offered.** `validKindsFor()` answers off the real measured contact, so `J`
  places the first kind that fits and the chip's kind menu lists only what this contact can
  take; a refusal names the kind and the reason, never a bare "no".
- **It breaks LOUDLY, and with no numbers.** When the pieces stop meeting, `derive()` refuses
  with a reason and empty items, the drawer floats the broken joints to the top and prints the
  reason where the mark-out numbers go. A joint that kept its last good numbers after the
  pieces moved apart would be worse than no joint at all: those numbers still look
  transferable to wood.
- **One derivation, cached on `revision()`, dropped by `resyncView()`.**
  `MainWindow::cachedJointDerivations()` is THE place this window derives a joint — the
  viewport and the drawer read the same answer, so a number on screen and a number in the list
  cannot disagree, and a broken joint is broken in both at once. Deriving runs `findContact()`
  per joint (booleans, a classifier probe, a ray cast) while `appStateChanged` fires on every
  selection click, so re-deriving there would put a kernel pass per joint on the app's hottest
  gesture. The cache is keyed on the revision **and** dropped by `resyncView()`, because a
  freshly opened furniture can land on the same revision number as the one it replaced.
  `jointDerivations()` hands callers a COPY: the cache is rebuilt in place the next time
  anything reaches it after the document moves, so a reference held across a delete or a kind
  switch would read destroyed-and-rebuilt, possibly shorter data — and never crash doing it.
  `refreshJoints()` is the one place drawings are pushed to the viewport, and the one place
  that decides which joints are drawn at all (the drawer open draws every joint; otherwise
  only the selected one, which placement sets).
- **The joint's card is a LIVE GESTURE, and the selection is not a term in it (improvements
  item 2).** `jointChipJointId()` used to require the joint's own two pieces to BE the
  whole-body selection, which read as sound until auto selection removed the modes: with no
  mode to be in, the first click anywhere — a face, an edge, empty space — stopped being
  "those two bodies" and retired the card the user was working in. The user reported exactly
  that: *"having Join activated and press the face selector disables the joinery mode."* So
  the selection is no longer consulted. The card opens when a joint is selected and stays
  until it is **closed** (the × or Escape with nothing typed), until the joint itself is
  **gone** (deleted, or undone away — `jointOf()` validates against the live document), or
  until the **editor is left** (`jointEditEnvironmentOk()`: the init screen, a sketch, a
  pending outline, render mode, a compare pane, a live Mirror placement).
  **What that costs, and where it is paid.** Disjointness from every other gesture used to
  fall out of the selection term for free — the card simply lost whichever pick came next —
  and now it does not. So `canPullSelectedFace()`, `bevelTarget()`, `transformableBodyId()`,
  `canMitreSelectedFace()`, `canReMeasureSize()`, `canBeginSlats()` and
  `canBeginMirrorPlacement()` each carry an explicit `jointChipJointId() > 0` stand-down,
  exactly as they already carry one another's. At most one application-wide Enter/Escape
  claim, still **by construction** rather than by luck — just written down in seven places
  instead of inferred from one. `gui_smoke` pins each stand-down AND its non-vacuity: close
  the card and the very same body pick raises the transform gizmo, so a green run cannot mean
  "the gizmo was never coming anyway".
- **Every whole-document operation carries the joints, and each is one rule in one place.**
  **Mirror** copies a joint onto its pieces' twins, both ways round: mirroring two pieces that
  already carry a joint copies it inside `pairWithMirror()`'s own checkpoint, and placing a
  joint on pieces that are already mirrored places the twin's inside the placement's
  checkpoint — so one undo takes both, whichever order the user works in. A joint whose other
  piece has no twin is not copied (half a mirrored joint is not a plan), and the guard against
  a SECOND copy is **freshness** — which bodies this gesture just paired, which is exact,
  since a twin that was just created can carry no joints — rather than comparing kinds and
  pieces, which would be wrong in both directions: two dowel joints between one pair of pieces
  are two joints. Kind and parameters copy verbatim with bodyA staying bodyA (which piece
  HOSTS the joint is the joint's own plan); the adjustments deliberately do not, because a
  nudge lives in the contact's own frame and the twin's frame is re-derived from mirrored wood,
  whose longest boundary edge — which is what orients that frame — can run the other way.
  **Isolate** and the eye button needed no code of their own: `refreshJoints()`'s gate asks
  `OcctViewWidget::isSolidVisible()`, the COMPOSED answer `applyIsolation()` writes in one
  place, so a joint draws only while both its pieces are on screen — hardware floating against
  a piece that is not there reads as a joint to nothing. **Render mode** clears them on entry
  inside `setRenderMode()` (scene decoration, exactly like the grid), and the only thing that
  brings them back on exit is the `appStateChanged` → `refreshJoints()` connection: deleting
  that one line leaves the exit at 0 joints drawn and leaves STALE hardware standing through
  an Isolate — measured, 4 drawn where 0 and 2 were required. **Delete** needed no code either:
  `DocumentModel::removeSolid()` drops every joint touching the body inside the caller's own
  checkpoint, so one undo restores the piece, its joint and its drawer row together.
- **The ledgered gap, recorded rather than implied:** a mirrored joint is a COPY taken at the
  moment of mirroring or placement, and nothing keeps the two in step afterwards — switching a
  kind, editing a number or deleting one of the pair touches that one alone. The twin's SHAPE
  still follows (the twin engine's job, untouched by any of this); the twin's PLAN does not.
- **What it deliberately does not do,** out of scope by the spec and not gestured at anywhere
  in the code: no geometry is cut (the bodies stay the B-reps they were; the hardware is a
  separate presentation channel), no soundness or strength checking, no grain direction, no
  saved joint presets, and no export of joinery data of any kind — STEP carries solids, and a
  joint is not one.
- **Vocabulary.** "Joint" is this app's own word, which put it head-on against the banned
  `Join` family, so `gui_smoke`'s `usesBannedWord()` carries ONE stem exception: "Joint" and
  "Joints" pass while "join", "joined", "joining" and "joins" still fail, pinned in both
  directions in the same place as the word-boundary rule — do not weaken it. "Rabbet" and
  "groove" are the real woodworking names for two of the housings and collide with nothing:
  neither is in the banned `bevel` family, which is Fillet's and Chamfer's Never column, and a
  housing is material removed to seat another board rather than an edge rounded or flattened.

**Closing note on how far the suite was actually run, since `kCheckFloor` points here.** The
branch's own gate is honest about one gap. Two consecutive official runs measured **4125 checks
+ 1 environment skip = 4126**, agreeing to the digit, and that measured number is what
`kCheckFloor` carries — the 3100 the gizmo deletion left behind is settled. But the last two
commits (`readout()` reading every number off the `Item` that `layout()` built, and the fixture
that pins the one exception) landed **after** that run and added about five checks, and the
merge was taken deliberately without a third run. So the floor sits roughly four low: it cannot
false-pass, but about four checks could stop running unnoticed. The final tree's evidence is all
six joinery blocks green at 963 checks filtered, and headless at 665 with `ctest` 8/8 — not a
filter-less invocation. **That debt is settled**: the improvements branch re-ratcheted the
floor to a twice-measured 4782 on its own final tree (see `kCheckFloor` above). The rule the
note existed to state still stands - **measure it rather than computing it**: a predicted
floor is a number nobody has watched the suite produce, which is the whole reason this
project measured it twice instead of adding six to the last one.

**What the green run is worth, and why.** The number to trust on this feature is not 4125 checks
passing — it is that roughly two dozen mutations were each made to produce a **named** red line,
that **seven vacuous assertions** were found and closed (a check satisfied by a default value; one
passing because a helper cleared the selection first; one whose arithmetic coincided with the
defaults at a single board thickness; two indexed reads gated behind a bare `if (size == N)` with
no counted `check()`; a "no adjustments" check a default-constructed object also satisfies; and an
exception whose fixture could not tell its two candidate fields apart), and that **three separate
harness mechanisms were caught reporting GREEN while never applying the mutation at all** — a
CRLF anchor against LF files, an anchor spanning an em dash that PowerShell's
`ReadAllLines`/`WriteAllLines` did not round-trip, and `Copy-Item` preserving mtime so MSBuild
skipped the rebuild and the run measured a stale exe. All three are in Pitfalls. The rule they
add up to: **a mutation counts only when it produces a real red line naming the expected check**,
and a run that produces no output file, or a check that simply vanishes from the output, is a
failure to apply rather than a pass.

### The scene editor: several furniture in one picture

Spec `docs/superpowers/specs/2026-10-02-scene-editor-design.md`, plan
`docs/superpowers/plans/2026-10-02-scene-editor-phase-1.md`, **phase 1's** eleven tasks, all
merged. The user's ask: *"i want a new editor separetly, which allows me to select multiple
furniture in a common scene and make renders like the render mode, this editor is open in the
hub"*. Four forks were settled by the user before a line was written, and each one is
load-bearing below: references rather than copies, move and rotate with snapping (**no
scale**), snapping to four things, and each furniture keeping its own wood.

**PHASE 1 SHIPPED TWO OF THE FOUR SNAP KINDS**, and the spec says so deliberately rather than
as a shortfall: the grid-and-angle snap and the floor are here; **snapping against another
piece and on top of another piece are phase 2** and are not implemented — the spec calls them
"the largest single item in the feature" and gives them their own planning round. What phase 1
owed them is that it must not foreclose them, and it does not: a placement stays a plain
rigid transform that a snapper can correct before it is committed. Do not read the rest of
this section as claiming a piece can be snapped to another piece today.

- **A PIECE REFERENCES ITS FURNITURE; it never copies it.** `SceneModel::Piece` is
  `{id, furnitureId, name, placement}` and holds not one vertex. The furniture on disk is the
  single source of truth, so a table edited in the editor is the table the scene shows the
  next time it opens - and a scene can never display wood the user has since changed. Same
  argument "twins, not replay" makes for symmetry and "a joint is a relationship" makes for
  joinery, one layer further out: the alternative is a copy that silently goes stale, and
  nothing would ever tell the user which of the two they were looking at.
- **TWO PIECES MAY NAME ONE FURNITURE, and nothing may key off the id as if it were
  unique.** Six chairs round a table are six pieces naming one `furnitureId`; they rename,
  move, turn and render independently. Every id this window hands out is **scene-local**
  (`myNextBodyId`), precisely because the furniture's own body ids are not unique in here.
  This is the Review Focus item the suite pins hardest, in both directions.
- **A placement is RIGID, and rigidity is read off the matrix rather than from
  `gp_Trsf::Form()`.** `SceneModel::checkPlacement()` checks unit-length columns, mutual
  orthogonality and a determinant of +1. `Form()` cannot answer the question: it reports how a
  transform was *built*, returning `gp_CompoundTrsf` for an ordinary gizmo
  rotation-plus-translation **and** for a non-rigid compound alike, so a `Form()`-based guard
  refuses exactly the placements the gizmo produces. The plan carried that mistake and the
  headless test caught it on its first run.
- **A piece's placement lives on the AIS PRESENTATION, which inverts the editor's own law.**
  `OcctViewWidget::setSolidPlacement()` sets a local transformation; nothing is re-tessellated
  and the furniture's saved shape is never mutated. The editor's suite asserts the **opposite**
  — `presentationIsClean()` exists so a modelling drag BAKES its transform — and the reason
  that law exists does not hold here: in the editor a presentation transform means the screen
  and the document disagree, while in a scene `Piece::placement` **is** the document and a
  placement is rigid by contract, so a presentation carrying exactly that placement is the two
  **agreeing**. Same mechanism, opposite meaning, decided by which side owns the truth.
  `placedShapesForPiece()` is the one accessor that applies a placement, and every measurement
  goes through it.
- **Move and Rotate, and NO SCALE ANYWHERE.** There is no `Tool::Scale`, no action, no
  renderer reached by any path - absent, not disabled, because a chair scaled to 1.4× is not a
  chair any more, and a furniture is resized by Re-measure, in the editor, where a size means
  something. Snap to Grid defaults **on** here and off in the editor (aligning furniture to a
  grid is the gesture, not an aid) at the editor's own 10 mm step, so a cabinet aligned in one
  is alignable in the other. A piece **stands on the floor** from the moment it arrives,
  measured from its own `measuredBox()` and never a world bounding box — a piece turned on the
  floor has a world box taller than the furniture is, and settling against that leaves it
  hovering. Settled on the way OUT of a gesture, not per step: a piece climbing back to the
  floor mid-drag would fight the hand moving it.
- **A new piece lands clear of what is already there**, not at the origin. That is *not*
  improvements item 15's deleted nudge, which concerned a duplicate of one body where the
  copy's position is the thing the user is about to set; two whole furniture dropped on one
  spot interpenetrate, and a scene whose pieces start inside each other is unusable.
- **Clicking any body selects the whole PIECE**, and the selection mode is
  `SelectionMode::Solid`. That makes `SceneWindow` the **first non-test caller of
  `setSelectionMode()`** — the sentence in `OcctViewWidget.h` and the one under "Selection"
  below that said nothing in the shipped app calls it were true until this window existed, and
  both are corrected. A scene has no gesture that wants a face or an edge, so arbitrating
  between candidates no tool can use would be work for nothing.
- **Each piece renders in ITS OWN furniture's wood.** `refreshWoodOverlays()` already built one
  `WoodBodyObject` per body, each with its own material, tile and grain angle, and was simply
  handed the same material every time; `OcctViewWidget::setBodyWood()` adds a per-body lookup
  that **falls back to the single live material when a body has no entry**, so the furniture
  editor sets no entries and behaves exactly as before. The honest limit: a furniture records a
  look per MATERIAL NAME and records nothing about which was last on screen, so with one entry
  (the common case) its wood is unambiguous and with several the first recorded is used.
  Persisting the active wood name would close it and touches `FurnitureStore`'s manifest. This
  is deliberately **not** generalised into per-body materials inside one furniture — that is a
  real feature with its own gesture and its own design round.
- **A BROKEN REFERENCE BREAKS LOUDLY AND SURVIVES.** A piece whose furniture cannot be read
  keeps its row, prints its reason **where the name goes**, renders nothing at all, and is
  still in the file after the next save. Each clause answers a different way of being wrong: a
  row that reads like an ordinary piece and draws nothing is a row the user believes; and a
  save that wrote only the pieces it could draw would delete the reference permanently, on the
  next autosave, silently. Joinery's "it breaks loudly, and with no numbers" rule, applied to a
  file reference.
- **The library is the hub, and the handoff has three legs.** `EditorSelectorHandoff::wire()`
  took a third window; choosing a scene card shows the scene window **first** and hides the
  selector **second**, `File → Close scene` reverses it, and the scene window's X quits through
  the same single hook the editor's X and the selector's close already use. `scene` is a
  nullable pointer purely so the suite's thirteen `wireSelector()` probes need not each stand up
  another `OcctViewWidget`; `main.cpp` and the handoff block both pass a real one.
- **`RenderStudio` is the render layer with two callers, not two layers that resemble each
  other.** It came out of `MainWindow` under one gate: the full suite had to report an
  **identical check count** across the move, which it did (5028 both sides). `MainWindow` keeps
  its panel pointers as non-owning handles assigned from the studio, which is what let ~40
  wiring sites move zero lines. It also owns `shotFrom()`/`applyShotTo()` — the ONE place the
  user's own list of what a shot stores ("camera pose, aspect, perpsective and light") is
  written down, with `MainWindow` delegating, so a fifth field cannot reach one window and miss
  the other.
- **What the scene window deliberately does NOT have** is as much the point as what it does: no
  sketch, extrude, boolean, joint, mirror, linked copy, version or compare — absent rather than
  disabled, because an action that exists and refuses reads as broken. The suite asserts that
  by NAME over every `QAction` the window owns, and a mutation adding one `Union` entry reddens
  it.
- **An empty scene is a real state.** The studio floor is built from the lowest *displayed*
  body, so with no pieces it is legitimately absent — absent rather than wrong — and the export
  still writes a real PNG.
- **Out of scope by the spec, and not gestured at anywhere:** no scale, no per-body materials
  inside one furniture, no lighting rig beyond the studio's, no scene-level joinery, no export
  of a scene as geometry, and no nesting a scene inside a scene.

**THREE LAYOUT BUGS REACHED THE USER, and all three were invisible to a suite that drove
this window dozens of times.** Every check written for the scene window asked what it DID -
does it open, does it list pieces, does a drag snap - and none asked what it LOOKED like. The
user found each one by glancing at the window.

- **The pill was stretched down the whole viewport and painted as an enormous ellipse.**
  `ViewportOverlay` treats the LAST `Anchor::LeftEdge` entry as the SPINE and stretches it to
  the viewport's bottom, because in the editor that entry is the rail with the pill above it
  as a header. This window has no rail, so the pill was both first and last, became the spine,
  and - `AppBar`'s corner radius being half its own height - a full-height pill is an ellipse.
  **A header is only a header when something follows it:** with no spine to lead, the pill is
  an ordinary `Anchor::TopLeft` card.
- **The pieces list drew its rows across its own title.** A row inserted into the layout of a
  parent that is **not visible yet stays hidden**, and a hidden child contributes nothing to
  `sizeHint()` - so the card measured itself as having no rows, `adjustSize()` had nothing to
  grow to, and `QVBoxLayout` crushed the title and the rows into 40 px against a `sizeHint` of
  133. `row->show()` at insertion. Note what this is NOT: `updateGeometry()` was already
  called and `ViewportOverlay::relayout()` already calls `adjustSize()` on every anchored
  card. Both were working; the hint itself was wrong.
- **The add-a-piece card came up at 0,0, behind the pill, with its title clipped.** Its action
  called `relayout()` directly instead of the window's own `refreshSurfaces()`, skipping both
  the centring and the raise - and **a raise must come AFTER the layout pass**, because
  `relayout()` raises every anchored card as it places it. The clipped title was separate: the
  card added its own height up by hand. **A height worked out by adding up what the author
  remembered is a height that forgets a margin** - the same lesson `QScrollArea` taught this
  project twice. It asks its layout now.

**And FOUR of the checks written to catch those bugs passed while the window was visibly
broken.** "Every row fits inside the card" and "the first row is below the title" were both
satisfied by a layout crushed into 40 px - the rows genuinely did fit, in a card far too
small. Then, once the list gained a scroll area, two of them were reading `QWidget::y()`,
which is relative to the **immediate parent** - the rows' scroll host, not the card - so they
measured the wrong coordinate space entirely and passed for no reason at all. Only the
height-against-`sizeHint()` check ever had teeth. **A geometry check must name the space it
measures in** (`mapTo()` the widget it is making a claim about), and a check that cannot
distinguish "fits" from "crushed" is not measuring fit.

The standing repair: **the scene block writes composited `printWindowCapture()` images on
every run** - the empty window, the window with pieces, and the add card open - so this
window's chrome is looked at rather than only asserted about. Behavioural checks cannot see a
layout, and this project already knew that ("Verify appearance with measured pixels, never by
eyeballing a crop"); what it did not have written down is the converse, which is that
**driving a window is not looking at it**.

**Three vacuous checks were found and closed while building this, each by a mutation that
reddened nothing**, and the pattern is worth naming because it recurred three times in one
branch. A `hasBodyWood()` presence check passes while every body wears the same default look —
a presence check cannot answer a per-thing question. The floor rule had nothing to fail
against until a furniture modelled 500 mm above its own origin existed, and adding one exposed
a real defect behind it: `addPiece()` was not settling at all, so such a furniture arrived
hovering with no discoverable cure. And "clicking a body selects the whole piece" was
satisfied by doing nothing while every piece had exactly one body. **A check whose fixture
cannot express the defect is not a check**, and the only way to find out is to make the defect
and watch for a named red line.

**Two of this branch's own checks also passed while lying**, which is the other half of the
same discipline: one printed `0 of 6` and still passed, because the message and the
`loadScene()` call sat in the same `check(...)` invocation and argument evaluation order is
unspecified, so the message read the model before the load ran; and one was a tautology left as
a placeholder (`shots().empty() || !shots().empty()`). **Read the numbers a green check prints,
not only its colour.** A third scare was the *mutation's* own bug rather than the app's — it
called `removePiece()` while iterating `pieces()` — so a mutation producing nonsense numbers is
suspect itself before the code is.

### Booleans: roles on the wood, and the region drawn

The user's report is the whole specification: *"right now works poorly, for example i
cant decide with one substract and which one keep, so would be great make this a real
polished high end tool"*. Two separate gaps behind that sentence, and only the first is
about the interface.

**Which body survived was decided by `std::sort(ids)`** — the LOWER DOCUMENT ID, so the
answer depended on the order the bodies were drawn in, possibly months earlier, and no
control anywhere could change it. **The default is now the BIGGEST body**, which is at
least a fact about the furniture: a cut is nearly always a small tool into a large piece.
And when the default is wrong, the badge saying so is sitting on the wood.

**The design is option B of a mockup round** (`docs`-less; the canvas is the user's).
Option A put named Base/Tool slots on a chip; B puts the answer ON THE BODIES and was
picked. So there is nothing to read off a panel and match back to wood by name:

- **`BooleanBadgeRenderer`** draws one stadium pill per body in the gesture, reading
  **KEEP** or **USED**, standing at that body's own **centre of mass** — a hollow or
  L-shaped piece's bounding-box middle can sit in fresh air outside the wood, and a badge
  there names nothing the user can see. Screen-sized under `Graphic3d_TMF_ZoomRotatePers`,
  in the sizes layer (Immediate, no depth test), so a label always reads over the body it
  names. A click on one says which body survives; the press is claimed in SCREEN space and
  **its release is swallowed too**, because the viewport picks on the release and a re-pick
  there would change the selection and so end the very gesture the click was adjusting.
  It carries the BODY ID rather than an index: the badges are rebuilt on every camera move.
- **"USED", not "REMOVE"** — a Union removes nothing, it joins in, and an Intersect narrows
  a body rather than taking it away. One word that is honest on all three beats three words
  that each need a lookup. The words are `MainWindow`'s, not the renderer's: painted copy
  invented inside a renderer is copy the banned-word sweep never reaches.
- **The region** is the volume the operation will act on, drawn before it happens so the
  hole is visible before it is made — the user's own follow-up ("highlight the part that is
  being substracted"). ONE shape for all three kinds and the COLOUR says what happens to it:
  Subtract's overlap is coming out, Union's doubled wood and Intersect's surviving volume
  are staying. Translucent, with its boundary at full strength, in the JOINTS' layer —
  depth-cleared, which is exactly "ghosted geometry seen THROUGH the wood", and a cut volume
  lives inside the board it is cutting. Reusing that layer also avoids a second custom
  depth-clearing layer, each of which carries the shadow-map trap in Pitfalls.

**Two `Theme` tokens, and neither borrows the accent.** `booleanOut` (#ff5a4a) and
`booleanStay` (#2ad4b0). `accent()` already means *state* — what is selected, what is live —
and a user who themes their accent red would otherwise have "selected" and "about to be cut
away" render identically, on the one gesture in this app where confusing those two destroys
wood. Named in the Colours tab by what happens to the wood (*Material coming out* /
*Material staying*) rather than by an operation, because the same pair serves all three and
"Subtract colour" would be wrong on two of them.

**The geometry is `ModelingOps`, Qt-free, headless first** (`tests/boolean_multi.cpp`, 35
checks, volume arithmetic throughout):

- **`applyBooleanMulti(kind, base, tools)`** — one base, any number of tools, ONE build, so
  six dowel holes are one operation, one checkpoint and one undo.
  `BRepAlgoAPI_BooleanOperation` already takes argument and tool LISTS, so Subtract and Union
  are the call `applyBoolean()` already made with a longer list, never a loop around it.
- **INTERSECT IS A FOLD, and that is a ruling.** OCCT's multi-tool `Common` answers
  `base ∧ (A ∨ B)` — the union of the overlaps — while the word means the volume common to
  EVERY body picked, `((base ∧ A) ∧ B)`. The two differ the moment the tools do not overlap
  each other, and the fixture is shaped so they differ by 8,000 mm³, which is what lets a
  check tell them apart.
- **AN EMPTY RESULT IS A REFUSAL**, and this closed a real hole in the old two-body path
  rather than a hypothetical one: Intersect on bodies that never touch builds *cleanly* and
  hands back an empty compound, which the app would have added to the document as a body with
  no volume — nameable, selectable, never visible. "Never surface a failed boolean as a
  success" covers the kernel reporting failure; this is the case where it reports success and
  the ANSWER is empty.
- **`booleanRegion(kind, base, tools)`** is what the preview draws: for Subtract and Union
  the base's own SHARE of the tools, **not the tools themselves** — those coincide until a
  tool sticks out past the base, which is the normal case for a dowel — and for Intersect the
  result itself. An empty region is `ok` with an empty shape, never a refusal: a tool that
  misses has nothing to highlight, which is a picture and not a failure.

**The gesture has the shape every other tool here has**: begin, look at it, adjust, Enter or
Escape; `booleanGestureStillHolds()` prunes it in `updateActions()` on any document change, a
sketch, render mode, a compare pane, the close question or a body going away — one predicate
rather than a cancel at every site. The prune clears state **in place** rather than calling
`cancelBoolean()`, which calls `updateActions()` and would recurse.

**Disjointness cost more terms than it used to.** A boolean stands on TWO OR MORE bodies, and
since the multi-body transform so does the gizmo — so the two want exactly the same selection
rather than merely similar ones. `transformableBodyId()` and `canBeginMirrorPlacement()` each
carry a `myBooleanActive` stand-down. **Both were documented in the header before they were
written, and the suite's own disjointness checks caught the lie on their first run** — which
is the argument for writing the check and the claim at the same time.

**An outline waiting is NOT a reason to refuse, and that had to be re-learned.** Every
direct-modeling gate shuts while an outline waits; booleans and Delete are the two that do
not, deliberately, because the outline's only exits are Extrude and Delete and the operations
that stay open are what keep a user from being trapped. The first build of this gesture gated
on `hasPendingFace()` and quietly closed that door; the suite's own *"booleans were never
gated on it"* check said so. The real risk was narrower: a second application-wide Enter
claim, which exists only while the extrude PANEL is up. So the term is the panel, and
`Extrude` stands down while a boolean is live — between them the two can never both be open.

**`applyBooleanToSelection()` is 22 lines now, not 170.** It begins the gesture and applies
it, so the linked-group refusal, the twin follow, the link propagation, the single checkpoint
and the toast all live in exactly one place. Two copies of that would drift, and the one that
drifted would be the one nobody was reading. It takes an optional `keepId`, and callers that
care which body survives pass it: a caller that leans on a default is a caller that breaks
when the default is improved, which is precisely what this rework did to the old rule.

**Honest limits.** Union and Intersect are commutative, so they have no "which survives"
question — the badge there marks whose NAME and id the result inherits. They still take the
same Enter, because a tool that commits instantly for two operations and asks for
confirmation on the third is harder to learn than one that always behaves the same way; that
is a judgement, not a measurement, and it is the one part of this a user could reasonably
want changed. A twin pair's Union now keeps one of the two ids where it used to remove both
and add a fresh body — better, since the survivor keeps its name, but it was a consequence
rather than an aim and the suite says so where it is pinned.

### Improvements: eighteen items, and the follow-ups that came out of testing them

One user-written list, worked top to bottom, then a run of asks that came out of the user
actually driving the result. Items with their own machinery are written up in their own
sections (item 5 under "Sizes around the selection", item 8 and item 13 under "Re-Measure",
item 10 under "Settings", item 18 under "The shell"); what follows is everything else, plus
the rulings each one settled.

**Folders in the Items drawer (item 11).** `DocumentModel::Group` is `{id, name, parent}`,
held in `State` — so a folder is **undoable content**, like a name and unlike visibility:
grouping three boards is an edit, and one `Ctrl+Z` has to take it back whole. The API is
`createGroup`/`removeGroup`/`setGroupParent`/`setItemGroup`/`groupOf`/`childGroups`/
`itemsInGroup`/`bodiesUnderGroup`/`groupContains`/`groupExists`/`groupNameOf`/`setGroupName`,
and `bodiesUnderGroup()` is the one that matters to every caller outside the drawer: a
folder's contents are its whole subtree, and nothing else should be re-deriving that walk.
`DocumentMeta` carries `GroupRecord{name, parentIndex}` with **parents emitted first**, so a
load never has to resolve a forward reference, and the whole block is validated before
anything is mutated — the same scratch-then-swap law every other load in this app follows.
`ItemsPanel` grew tree rows (`addItemRow`, `addGroupRow`, `buildGroupRows`, `kIndentPx = 12`),
multi-select (plain/Ctrl/Shift through one `selectionRequested(std::vector<int>)` signal),
drag-and-drop onto a folder (`itemsDropped(std::vector<int>, int)`, `kDragThresholdPx = 6`),
and a right-click menu of New folder with these / Rename / Ungroup / **Delete folder and its
contents**. Folders start **folded** (`myExpanded` is empty until something opens one), which
is the user's call and the right default for the drawer's actual problem — a long list.

**The drawer scrolls, and its height comes from the rows (item 12).** The user's words were
"the items list is too long and it does not have a scroll bar, can you add an invisible
scroll bar?", and invisible is literal: a `QScrollArea` with **both bars off**, so nothing is
drawn and nothing is clickable, the card's painted rectangle is unchanged, and the wheel is
the whole interaction. Then the card was too short, and this is the finding worth keeping,
because it cost three wrong fixes in a row (the cap, then `setSizeAdjustPolicy`, then the cap
again): **`QScrollArea::sizeHint()` does not answer for its widget.** It returns a cached size
bounded to roughly 24 text lines and it ignores `sizeAdjustPolicy` entirely, so every
attempt to raise a cap was raising a cap that was never the binding constraint.
`ItemsPanel::sizeHint()` reads `myRows->sizeHint()` — the row container's own height —
directly, and never `heightForWidth()`. The title row's `+` makes a folder from the
selection; there is **no hover × on this drawer**, by the user's call, and the rail chip and
the menu entry still close it.

**Slats (item 14).** Two rounds of pictures settled what the user wanted: not one stick at a
time but *"defining a certain area, the tool put those stick and i can edit they sizes"*.
`ModelingOps` owns it, Qt-free: `SlatPlan{width, gap, depth, runAcross}`,
`SlatCheck{Ok, NotAFlatFace, SizeOutOfRange, NoneFit}`, `checkSlats()` returning a value the
UI maps to a sentence (`checkMitre()`'s contract), `slatsOnFace()`, `slatsOnArea()` and
`slatRunFromBodies()`. The layout rule is one line and it is why the result looks made rather
than generated: `count = floor((extent + gap) / (width + gap))`, then
`pitch = (extent - width) / (count - 1)` — so the count comes from the gap the user asked for
and the pitch is then re-derived to land **both end slats flush** with the area's edges. The
chip is `SlatsTool` (Width / Gap / Depth / Flip, with a live count), placed bottom-centre at a
fixed `kBottomMargin = 24` because the user asked for it there, previewing through
`MainWindow::slatsResult()` — the call the commit itself uses — and the run lands in a folder
of its own so it can be moved, hidden or deleted as one thing.

**Multi-object transform.** Move, Rotate and Scale act on the **whole selection**, and on a
selected folder's whole subtree. `MainWindow::transformableBodyIds()` is the one derivation
(it is `transformableBodyId()`'s plural, carrying the same stand-downs) and
`transformBodies()` applies one delta about the selection's own pivot inside one checkpoint,
so a multi-body move is one undo and one toast.

**The loading card.** A heavy furniture took seconds to open behind a blank window, and the
ask was Unity's or Photoshop's answer: show something. `LoadingCard` is the app mark, the
furniture's name and a progress bar, driven from `openFurniture()`
(`begin()`/`setTotal()`/`step()`/`end()`) and ticked from `resyncView()`, whose
`displaySolid()` loop is where the seconds actually go — one step per body. `pump()` calls
`QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents)`: the card has to paint
while the load runs, and **excluding user input is not optional** — a click delivered in the
middle of a half-built document reaches a window that is not ready for it.

**Per-material colour and brightness.** Double-clicking a material tile opens `MaterialCard`
— a swatch and a 25–200% brightness slider, bottom-LEFT so it does not cover the furniture it
is about, Escape to close, and **no key claim beyond Escape**: orbiting while the slider moves
is exactly how a material gets judged. What it edits is
`DocumentModel::MaterialLook{material, red, green, blue, brightness}`, stored **outside
`State`** — presentation, the same category as visibility, so it is persisted with the
furniture and bumps `revision()` (autosave notices, the unsaved dot lights) but takes **no
checkpoint**, because a colour is not an edit to undo. `MainWindow::activeMaterialName()` is
the one derivation of which record is live (a wood names itself; everything else is the
nearest preset, derived exactly as the tiles' own highlight is), `applyMaterialLook()` pushes
it into the viewport on every `appStateChanged`, and the card closes itself when the material
it is about stops being the live one — a card editing something the viewport is not showing
is editing in the dark.

**Render quality, and the middle tier (item 16).** `RenderQuality{Simple, Balanced, Deep}` is
a picker over the probe's answer: `effectiveRenderTier()` maps the choice onto the tier list,
so Deep is what the probe found and Balanced is the rasterizing tier below it. The two-state
`setRenderQuick()` spelling is kept because callers use it.

**Sketch work is drawn on top (item 8 of the list).** `mySketchTopLayer` +
`markOnTopOfBodies()` puts the outline, the marks, the cursor ring and the dimension above the
bodies. The modeling preview and the mirror plane deliberately **stay depth-tested**: a ghost
that floats through the body it is about is not showing where the body will be.

**WASD flies the camera.** `isFlyKey()`/`stepFly()` on a 16 ms timer (`kFlyPerSecond = 1.1`,
`kFlyFastFactor = 3.0`), claimed through `ShortcutOverride` **only while orbiting**, so the
keys belong to a live gesture rather than to the viewport at all times — typing in a field is
not flying.

**The grid fades as you square up to it (item 7).** `uFaceOnFade` in the grid's fragment
shader, ramping from 1 to `kFaceOnFloor` (0.45) over the last 25 degrees with a `smoothstep`,
measured as the **eye's real height above the plane against its distance** rather than the
turntable's elevation angle — which is why a locked vertical face gets the same treatment for
free. Two `gui_smoke` checks had to change with it, and the change is a lesson rather than a
chore: **a check that counts EXACT TOKEN PIXELS cannot see an alpha-blended line.** Both now
capture the same frame with the grid switched off and count what moved, which answers the
question each was actually asking ("is the grid drawn over this face", "did raising the
detail put more lines in the frame") in a way no future change to the grid's colour or alpha
can quietly answer for.

**Naming a furniture (item 4).** The `+` card no longer makes `Furniture 03` and opens it: it
asks. `NameFurnitureCard` is `UnsavedCloseCard`'s shape — a scrim, an app-wide key claim, and
it hides **before** reporting its answer so a toast lands on a clear viewport — and it creates
nothing until the question is answered. Every route to a new furniture goes through it,
including the eight places `gui_smoke` presses `+`.

**Duplicate does not move the copy (item 15).** It used to sit one grid step away in X and Y
so it read as a second object; the user's point is that a duplicate is the start of a
deliberate move, so an automatic one is just something to undo first. One consequence landed
somewhere non-obvious and is recorded rather than quietly absorbed: duplicating a body that is
itself a mirror SOURCE used to leave the copy unpaired, because the nudge pushed it across the
gesture's own tangent plane and `pairWithMirror()` skips a straddling body. With no nudge the
copy is where its source is — wholly on one side — so the ordinary new-body rule fires and the
copy gets a **fresh twin of its own**, which is what the rule always said should happen.

**Ctrl+D on a FOLDER duplicates the folder.** The user's ask in full: *"ctrl+D when a folder
is selected should duplicate the folder"*. Clicking a folder row selects every body under it,
so a plain duplicate already copied those bodies — straight back into the **same** folder,
which is nobody's idea of duplicating a folder. `MainWindow::duplicateFolderId()` is the one
derivation and it is **derived from the selection, never reported by the drawer**: the drawer
is one of several things that can select bodies, and a rule that only worked when the click
came from the list is a rule with a hole in it. It walks **up** from the first selected body —
a folder with subfolders has no single `groupOf()` its bodies share — and answers with the
first folder whose `bodiesUnderGroup()` **is** the selection exactly. Anything less is a
duplicate of some bodies that happen to live in a folder, and cloning around that would be
inventing an intent the user did not express. `groupOf()` already answers for a folder (its
own parent), which is what lets the walk ask one question rather than branching on the kind.
Inside the commit, a `cloneOf` map plus a recursive `cloneFolder` lambda build the copies'
folders on demand: the picked folder's clone lands **beside** its source (same parent) named
`"<name> copy"`, and a folder nested inside it is cloned into its own parent's clone, so the
tree comes out the shape it went in — and only the folder the user picked is renamed, because
two levels of "copy copy" is noise. Creation-paired mirror twins follow their own copy into
the clone rather than staying behind in the source folder. One checkpoint still covers all of
it, so **one `Ctrl+Z` takes the bodies and the cloned folders back together** — a clone left
standing by an undo is an empty folder nobody made. The toast names the folder
(`Pillars copy created — 6 bodies`), since "6 bodies duplicated" says nothing about where they
landed. `gui_smoke`'s `ctrl-d-on-a-folder-duplicates-the-folder` block (independent, 42 checks)
pins the derivation in both directions — the whole subtree names the folder, one body of two
names nothing — pins that **the ordinary one-body-in-a-folder case is unchanged**, and pins
the nested shape rather than only a count. Two mutations each went red on named checks: the
derivation returning 0 reddened 12, starting with *the whole folder selected — the gesture
knows which folder that is*; parenting every clone flat reddened 4, starting with *it holds
exactly one subfolder, not a flattened pair*.

**Ctrl+double-click is gone (item 9)**, **the `mm` chip is gone (item 18)**, and the outline's
line has **its own colour token** (`dimensionLine`, item 17 — *"i mean this purple line, which
is using the accent, i want a separate color for it"*). The token ships at the shipped
accent's own violet, so splitting it off moved no pixel: what the user asked for was a
control, not a new colour.

**Small things that are still contracts.** Save dialogs remember the folder they last wrote
to (`saveDialogPath()`/`rememberSaveDialogPath()`), a clicked point in the UI during a sketch
does not become a sketch point, and Join plus a face pick no longer disables joinery — that
last is item 2, written up where the joint card lives.

### Qt plugin deployment - do not remove

Qt will not start without a platform plugin, and it looks for one in a `platforms/`
directory next to the executable, not alongside the Qt DLLs. vcpkg's applocal deployment
copies DLLs but **not** plugins, and the `windeployqt` feature is deliberately not installed
(it would have dragged the full default feature set back in). `CMakeLists.txt` therefore
copies `QWindowsIntegrationPlugin` and `QModernWindowsStylePlugin` itself in a POST_BUILD
step. Delete that and the app dies at startup with
`could not find the Qt platform plugin "windows"`.

**No control in this app paints a keyboard focus ring any more.** They were amber and the
user asked for every one of them gone ("why its have that yellow stroke when i selected a
tab? can you remove it?" - then, of the rest: "i dont wanna see them"). The nine paint
sites in `ToolChip`, the Settings drawer and the render settings card are deleted, and
`JointChip`'s typed field marks itself with `Theme::accent()` while it is being edited, as
the mitre and re-measure fields already did. `focusRing`/`focusRingMuted` STAY in
`Theme::Spec` - a `.furnifytheme` saved before this would be refused for an unknown key
otherwise - but they are retired: `AppearancePanel::isRetiredToken()` keeps them out of the
Colours tab, since a swatch for a colour nothing paints is a control that does nothing, and
`gui_smoke` pins the exemption by name rather than letting the every-token-has-a-row check
quietly cover two fewer tokens.

### Settings: one drawer, five tabs, and every row a mirror

Improvements item 10, mockup pick **B** ("grow the Appearance panel into a Settings
drawer"). The settings used to live in three places — the colours card, the View menu,
and autosave under File — and nothing had a single home. `AppearancePanel` is now the
**Settings** drawer: same floating card, same `Ctrl+Alt+A`, same `ViewportOverlay`
TopRight anchor under the axis gizmo, same 296×380 at the same `Theme::wholeDevicePixels()`
size — with a tab bar under the title and a `QStackedWidget` below it.

| Tab | Rows |
|---|---|
| Colours | 27 colour swatches (scrolled; 29 tokens less the two retired) · Save/Load colours · Reset |
| Text & lines | Text size · Font · Edge lines · Outline lines · Button border |
| Viewport | Show the grid · **Grid detail** · Show sizes around a selection · Projection · **Gizmo size** · Show notifications |
| Units | Sizes in (Millimetres/Centimetres) · Snap to Grid · Magnet |
| Files | Autosave (Off / After every change / Every minute / 5 / 15) |

**Colours split into Colours and Text & lines, and only that split changed.** A user
review of the four-tab drawer put it plainly — "the color tab doesnt not make any sense,
maybe divide it in two tabs" — and they were right: the tab held 28 swatches *and* five
unrelated type/line controls (Text size, Font, Edge lines, Outline lines, Button border),
none of which is a colour. Colours kept the swatches and the three file-shaped actions
that act on the whole look (Save, Load, Reset); the five type/line rows moved to a new
Text & lines tab, in the order this table lists them. All five stayed exactly the
`Theme::Spec` values and the exact mechanism they always were — only their page changed.

**Five tabs do not fit one row of this card's 272 logical pixels of content width** — Text
& lines alone runs close to what four tabs used to share between them — so the tab bar
wraps to a **second row**. The wrap is measured, not a hand-picked split: each chip's own
`sizeHint()` (built from `QFontMetrics(Theme::labelFont())`, the exact font `OptionChip`
paints its label with — `chip->setFont(Theme::labelFont())` at construction and again on
every `applyTheme()`, so a probe reading the chip's own `font()` reads what is actually on
screen) is packed greedily into row one until the next chip would overflow the card's
content width, and everything after that goes to row two. `gui_smoke` pins both the
no-clipping property (every chip's real width is measured against an independent
`QFontMetrics(chip->font())` re-measurement, not a call back into the chip's own
`sizeHint()`, which would be grading the implementation against itself) and that the wrap
actually happened (two distinct row positions, not five chips narrow enough to squeeze
onto one line today).

**Grid detail and Gizmo size MOVED from Colours to Viewport, and only their place moved.**
They are `Theme::Spec` values still: an edit writes `Theme::setSpec()`, the notifier
broadcasts, `applyTheme()` re-reads every control, `MainWindow`'s 400 ms debounce stores
it. They sat on the colours card because that is where the persistence mechanism lives,
which is a reason about the code and not about the user — a number describing the grid
belongs beside the grid's own switch. `gui_smoke` pins both halves of the move: each is
on the Viewport page and each is *not* on the Colours page, because "it arrived" and "it
left" are two different mistakes. The Colours/Text & lines split is pinned the same way,
both directions, for all five moved rows and for what stayed behind.

**Every row that is not a `Theme::Spec` value is a MIRROR of a `QAction`, and holds no
state whatever.** `addToggleRow()` (a pill), `addChoiceRow()` (a segmented or stacked set
of exclusive actions) and `addBinaryChoiceRow()` (one checkable action drawn as two chips)
each take the action `MainWindow` already owns; `syncMirrors()` pushes `isChecked()` and
`isEnabled()` onto the control on every `QAction::changed`, and a click calls
`trigger()` and sets no pixel of its own. So the View menu, the File menu and this drawer
cannot disagree, `updateActions()` stays the single place availability is decided, and the
vocabulary sweep keeps covering these settings through the actions it already finds by
text. `QAction::changed` is the one signal watched, deliberately: Qt emits it for a
`setChecked()`, a `setEnabled()` and a text change alike, so there is no list of signals
to keep in step with `updateActions()`.

`addBinaryChoiceRow()` exists because **Projection has one action and two words**. The app
has an `Orthographic` toggle and no `Perspective` action, perspective being simply that
toggle off — and a row that painted "Orthographic" beside a pill would make the user work
that out. Both chips read the same `isChecked()`; the OFF half lives in `myOffMirrors`
rather than carrying a polarity flag every ordinary row would then have to carry.

**Autosave is stacked, not segmented** — five chips in a 272-logical-pixel row would be
five unreadable chips — and its name label is top-aligned, because a label centred against
a five-row column reads as a heading for the gap between the third and fourth entries.

**Which tab is open is session state**, held in one member and persisted nowhere. It is
not a `Theme::Spec` value, so storing it would mean a second persistence mechanism on this
card for something that is not a preference at all, only where the user last looked; and
Colours is the tab this drawer has always opened on. `setCurrentTab()` is the single place
the current tab is decided and the single place every chip is re-synced from it — it
is deliberately *not* guarded on "it did not change", because the constructor calls it to
push the initial state onto chips nothing has told anything yet.

The class and the file are still called `AppearancePanel`; see the rename note under the
vocabulary table and the header's own first paragraph for why, and for what the type
actually is now.

### The shell is action-driven, and composed as bar + rail + drawer

Every control is constructed from a `QAction` and mirrors it - enabled state, checked
state, label, shortcut. Never give a control its own state: menus, rail buttons and
shortcuts would drift, and `gui_smoke` finds actions by text, so the controls are covered
for free. `updateActions()` remains the single place that decides what is available.

The shell's composition, settled in Phase 5 against HTML mockups the user chose from,
reworked once more in Milestone 5, item 3 against a second round of mockups ("Option A -
compact pill, top left"), and adjusted again by a user feedback round on that same item: the
pill now **leads the rail's own column** (same left margin, one stacking gap above it)
rather than floating beside it:

- **The app bar is a floating rounded pill now, not a window-spanning strip.**
  `QMainWindow::setMenuWidget` is gone; the viewport is full-bleed to the window's own top
  edge, and `AppBar` is a `ViewportOverlay::Anchor::LeftEdge` card now - the FIRST of two
  entries sharing that anchor, added before the rail, so it is the **header** rather than the
  **spine** (see `ViewportOverlay.h`'s Anchor comment for how that split is derived from
  insertion order alone, never flagged, and never confused by which one render mode happens
  to be hiding). Same x as the rail (`kEdgeMargin`), so the two read as one column the way
  the feedback round asked for - not the `Anchor::TopLeft` card it was at first ship, floated
  beside the rail by `leftX`. Adding it first still means the items drawer and the versions
  drawer (both `Anchor::TopLeft`) stack downward BELOW its bottom edge - now via
  `relayout()`'s `leftEdgeHeaderBottom`, the symmetric counterpart to the `leftX` shift the
  rail's own width already causes, rather than via the ordinary TopLeft cursor a plain corner
  anchor would have given it for free. It carries the app mark (the user's own artwork,
  `IconSet::appMarkPixmap()`), the wordmark, and the window's **real `QMenuBar`**
  (reparented in - menus, shortcuts, the generated sheet and the vocabulary sweep all keep
  working untouched: nothing about the menus themselves changed, only what holds them).
  Fully rounded ends - **radius = half the pill's own height**, read fresh in `paintEvent()`
  every time rather than cached, so the fill and the curve can never disagree - and it is
  one of `Theme::paintSurface()`'s family now (`Theme::makeSurfaceTransparent()` +
  `Qt::WA_NoSystemBackground` in the constructor, the pill's corners composite through to
  the live GL scene exactly as the rail's and the gizmo's already do), where it used to
  paint its own flat `chrome()` strip and bottom rule by hand. `AppBar::updatePillMargins()`
  derives the pill's own horizontal padding from `radius`, recomputed on every theme change:
  a stadium shape's semicircular ends are the widest point of the curve at every vertical
  position spanning the full height (not merely near the corners, the way a small-radius
  rounded rect's corners are), so anything painted inside has to clear a full radius's worth
  of horizontal inset or the curve clips it. Since a `QHBoxLayout`'s height depends only on
  its top/bottom margins and its tallest child's own `sizeHint()` - never on left/right -
  asking for that height BEFORE the radius-derived horizontal margins are set is not a stale
  read, it is the one order that avoids a circular layout pass.
  `MainWindow::syncChromeHeights()` no longer has anything to do for the pill - it is a
  `ViewportOverlay`-anchored card now, and `ViewportOverlay::relayout()` already rounds every
  anchored card's size up through `Theme::wholeDevicePixels()` for free; only the status bar
  is still a real window-spanning strip needing that function's own bespoke fix.
- **The view controls that used to live as bar buttons moved to a dedicated icon-only
  `ToolCluster`, anchored `Anchor::TopRight` under the axis gizmo card** (stacking one gap
  below it, the exact mechanism the Appearance and RenderSettings cards already share that
  slot through - see `ViewportOverlay.h`'s "clusters sharing an anchor stack downward in the
  order they were added"). **THREE chips, not four, since improvements item 18** - the
  **Persp/Ortho toggle** (it triggers the checkable `Orthographic` action and holds no state;
  it does **not** snap to Axonometric, which belongs to the gizmo, keys 0-3 and the View
  menu, and it records no `view.changed`, because a projection flip is not a look in a named
  direction and would otherwise retire the hint teaching the gizmo), **Wireframe** and
  **Fit All** - all three plain `ToolChip(action, IconSet::Glyph, ChipMode::IconOnly)` calls
  mirroring their actions the same way every rail chip already does
  (`IconSet::Glyph::Projection/Wireframe/FitAll`, all added when the bar's text buttons went;
  the projection toggle never had an icon before, since it used to paint its own word).
  **The unit chip is gone** (the user's own call: "remove the mm button, keep mm in the View
  tab"). It was the one chip in this app that painted a WORD instead of an icon - a `mm`/`cm`
  glyph through `ToolChip`'s text-glyph constructor, with its text pushed in on every
  `appStateChanged` rather than mirroring a `QAction::changed()` - and a unit is a thing you
  set once and then read off a number, not a tool you reach for beside Fit All. The unit
  itself did not move anywhere secret: it is the View menu's two entries and the Settings
  drawer's Units tab, both of which already existed and both of which are where the rest of
  this app's settings live. `ToolChip`'s text-glyph constructor STAYS - it is a real second
  spelling of a chip and the next word-shaped control should use it rather than inventing a
  sibling class - but nothing constructs one today, which is stated here so its absence from
  the shell does not read as the constructor being dead.
  This cluster is deliberately **not** hidden by render mode - these controls stayed
  reachable through render mode when they lived in the bar, and moving them onto chips does
  not change that; if the gizmo above it hides, the cluster simply reflows up into the
  gizmo's own slot, which `ViewportOverlay`'s existing "hidden entries occupy no slot" rule
  already gives for free. `Save Screenshot` is still menu-only. Nothing paints
  `OcctViewWidget::viewDirectionName()` any more (unchanged from Phase 7); the suite still
  asserts snap flights against it.
- **The rail** is a second, separate `ToolCluster` in `ChipMode::IconOnly`, also anchored
  `Anchor::LeftEdge` - every tool as an icon button, labels and shortcuts in tooltips that
  auto-update from the actions. Being the SECOND `Anchor::LeftEdge` entry (the pill, above,
  is the first) makes it the **spine**: the one that stretches to reach the viewport's
  bottom edge, starting one `ViewportOverlay::kStackGap` below wherever the pill's own
  bottom edge actually is - read live off the pill's placed geometry every `relayout()`,
  never a constant, so a pill that grows a row when the type scale does moves the rail's
  start down with it for free. `MainWindow::buildOverlay()` sets the viewport's own minimum
  height from a **stacked SUM** now, not a `std::max()` of two independent demands: the
  pill's `sizeHint()`, one `kStackGap`, the rail's `sizeHint()`, and both `ViewportOverlay`
  edge margins - derived from both real `sizeHint()`s and both `ViewportOverlay` constants
  rather than any literal, so it cannot go stale the day either grows, and matches exactly
  what `relayout()` places against because it reads the same two named constants relayout()
  does. Before this feedback round the two never shared a column and a `std::max()` was the
  correct floor; now that they read as one column, the floor has to be the sum of both,
  because the viewport must clear the header AND the spine stacked, not whichever alone
  happens to be taller. The rail carries TEN chips (Items · Start Sketch, Extrude, Add
  shape · Union, Subtract, Intersect, Delete · Undo, Redo) — the auto-selection switch
  deleted Select Bodies/Faces/Edges, and Snap to Grid moved to the View menu alone by the
  user's call, since it is a setting flipped rarely — and the floor followed for free because it is
  derived from `rail->sizeHint()` rather than from a count. An eleventh rail tool still
  raises that floor rather than
  reintroducing the clip that cost Redo, then Undo, but the user's actual screen height is a
  real ceiling the floor cannot push past, so the rail still wants a rework - scrolling,
  grouping, something - well before it gets there. **Symmetry stayed off the rail for
  exactly this reason** — it is a Model-menu-only checkable action (`S`), not an eleventh
  chip, so live symmetry did not raise the floor further. Render mode and the bottom-bar
  toggle are View-menu-only for the same load-bearing reason, not merely by omission. Two
  `ToolCluster`s float over the viewport (the rail and the view-controls cluster under the
  gizmo, not the pill - that one is an `AppBar`, not a `ToolCluster`); a caller wanting "the
  rail" specifically still gets it as the first match of `findChild<ToolCluster*>()` (added
  first, in `buildOverlay()`), and `MainWindow::viewControls()` is the accessor for the
  other one, rather than a caller having to guess which of `findChildren<ToolCluster*>()`'s
  two results it wants.
- **The items drawer** floats beside the rail - and, since it stacks in the same
  `Anchor::TopLeft` column the pill used to occupy alone, below the pill's own bottom edge
  too - toggled by the existing Items action: visibility is derived from the action's
  checked state, both directions, and nothing else may show or hide it. The viewport is
  full-bleed; there is no dock.

Overlay widgets are **direct children of `OcctViewWidget`**. A probe confirmed Qt
composites plain children over OCCT's OpenGL surface correctly on Windows - but see the
opacity rule below: translucency over that surface is the unreliable variant, and this
project no longer paints any.

### TOMBSTONE: one opaque paint family, and the rule that made it

**This law is retired (QOpenGLWidget migration, Phase 2, 2026-09-04).** It held from
Milestone 3 through Phase 1 of the migration: **no widget could paint a translucent pixel
over the GL surface, and no widget could leave a rounded card's corners unpainted either.**
Both halves traced back to the same root cause, not two - the viewport was a native OS window
(`WNT_Window` over a `WA_PaintOnScreen` widget), so Qt's own backing store held nothing behind
a Qt child floating over it. An unpainted pixel there was not transparent, it was whatever the
GL driver had last left, which read as black; a translucent pixel blended against that same
garbage, which read as noise. Phase 5 paid twice to learn this was already the law: the chip
shadows introduced early in that phase "worked" only by blending alpha over garbage, and the
rail's unpainted slack rendered as a solid black band down the app.

Two workarounds stood in for what genuine compositing would have given for free:
`Theme::paintSurface()` filled every card's **entire widget rect** with an opaque ground colour
(`viewport()` by default, `chrome()` for a card sitting on the chrome bar instead) before
painting the rounded panel and border on top, so an unpainted corner read as flat
viewport()/chrome() grey rather than black - a paint-something answer. `Theme::installCardMask()`
was the other lawful answer the same constraint left open: a `QRegion` window mask, rebuilt
from the same rounded-rect path on every resize, that stopped Qt from painting - or hit-testing
- those corner pixels at all, which is what actually cut the corner rather than merely
recolouring it. Both existed at once for a while (the mask on top of the fill, never as an
alternative to it), because a resize landing between a repaint and the next mask update would
otherwise bare the old flat corners for one frame.

**Hosting `OcctViewWidget` in a `QOpenGLWidget` (Phase 1) removed the root cause; deleting both
workarounds (Phase 2) is what actually cashes that out.** Qt's compositor now holds the real,
live GL frame behind every overlay child, the same as it holds any other widget's content, so
an unpainted pixel is finally what it always should have been: genuinely see-through, showing
whatever is really there. `paintSurface()` therefore paints **only** the rounded panel and its
crisp border now - nothing fills the four small triangles outside the rounded shape any more,
and nothing needs to. `installCardMask()` is deleted outright, not replaced by anything that
also cuts a shape: cutting is no longer necessary once nothing is painted there in the first
place *for painting*.

**And that sentence is only about painting, which is why the mask's second job needs naming
rather than quietly dropping.** A `QRegion` mask excluded those corner pixels from
**hit-testing** as well - Qt honours `QWidget::mask()` in `childAt()` - so during the mask era
a click in a card's corner triangle fell straight through to the viewport behind it. Nothing
replaces that half, and nothing is meant to: **a corner click now lands on the card**, which
swallows it. That is the accepted trade of maskless corners, ruled rather than overlooked. It
costs a few pixels per corner on a card that is already a click target everywhere else, and
the alternative - reintroducing a per-widget `QRegion`, rebuilt on every resize, purely to
route input - buys back a pass-through nobody has ever asked for at exactly the cost this
phase existed to delete. Recorded so the next reader knows the corners look see-through and
are not click-through, and does not read it as a bug when they find it. What replaced the
mask's OTHER job - keeping the app-wide
`QMainWindow, QWidget { background-color: @chrome }` stylesheet rule from stamping an opaque
square over that same unpainted area before `paintEvent()` ever runs - is
`Theme::makeSurfaceTransparent()`: a plain `background: transparent` per-widget stylesheet,
the exact mechanism `ToolChip::applyTheme()` had already found and fixed for its own corners
one task earlier, now applied to the family's own top-level members (every one of them, in
its own constructor, beside the `Qt::WA_NoSystemBackground` it already carried). `gui_smoke`'s
`checkCardCorners()` is the replacement for the mask's own structural pin: rendered in
isolation (`renderExact()`, transparent fill), a family card's own corner reads alpha 0 (paint
never reaches it), its straight edge reads alpha 255 (the border still does), and somewhere
along the actual antialiased arc a pixel reads a genuine partial alpha - neither the card's own
paint nor a bare hole, which is exactly what Qt's live compositor blends the real scene through
at that same screen pixel. A `QRegion` query used to prove the shape; the paint itself proves
it now.

There are still no shadows; borders still carry the separation, unaffected by any of this.
`surfaceShadowMargin()` still returns 0 and stays only so caller arithmetic keeps working.
`Theme::drawCrispBorder` is still the one half-pixel-alignment idiom - an antialiased 1px pen
at an integer coordinate smears across two rows at half intensity - and none of that changed;
only what happened *outside* the rounded shape did.

**One ruled exception, unaffected by any of this:** `Toast::paintEvent()`'s
`painter.setOpacity(myOpacity)` blends the whole toast during its 160 ms dismiss fade -
permitted because it is transient and motion-token-driven rather than a resting translucent
surface, and because gui_smoke runs with animations off, so the opacity/colour sweeps never
actually see a blended pixel. It was never part of the ground-fill/mask stratum and needed no
change when that stratum was deleted.

### One dirty card repaints all of them, so the tree's paint cost is a per-frame budget

**The viewport is a QOpenGLWidget — a *texture* widget — and Qt cannot partially update a
window that holds one.** The instant ANY raster overlay child is dirty, **every visible
overlay widget in the window repaints**: measured at 34 widget paint events per orbit step
(app bar, its menu bar, both clusters and all 17 chips, the drawer, the balloon, the gizmo,
`MainWindow` itself) against **1** when nothing raster is dirty. `AxisGizmo` is dirty on
every `cameraChanged` — correctly, it rotates with the scene — so **the whole overlay tree's
paint cost is charged to every frame of an orbit, pan or zoom.** That is a budget, not a
one-off, and it is the reason a widget nobody is looking at can make the camera feel slow.

That budget was blown by one line. `AppBar::paintEvent()` called `IconSet::appMarkPixmap()`,
which decoded `:/icons/app.png` — **the user's 2000×2000 artwork** — and ran a
`SmoothTransformation` downscale of it to 20×20, **per paint**. One app-bar repaint measured
**17.9 ms**, against 0.02 ms for a `ToolChip` and 0.08 ms for the entire axis gizmo. So an
orbit step cost 21 ms where the frame itself cost 4, and modeling ran at **47 fps while
path-traced render mode ran at 240** — the user's report, and the paradox in it, exactly: the
heavier mode was the smoother one **because render mode hides every repainting overlay**, so
its frames pay no raster cost at all. `appMarkPixmap()` is `QPixmapCache`-backed now (cleared
by Qt at shutdown, so no `QPixmap` outlives its `QGuiApplication`), the bar paints in 0.18 ms,
and both modes measure one frame per orbit step at 4.1 ms.

Three things this cost, each ruled out **by measurement** before the cause was found, and
each worth not re-deriving: it is **not** extra frames (paints-per-orbit-step was exactly
1.00 the whole time, in both modes — the standing double-redraw suspicion is innocent here);
it is **not** per-move CPU on the camera path (the whole synchronous `sendEvent` — orbit
maths, the grid's rebuild guard, `updateManipulatorSize`, the symmetry and mirror indicators,
every `cameraChanged` slot — measured 0.06 ms/move, 0.3% of the frame); and it is **not**
pacing or swap-chain warmth (forcing `swapInterval` to 0 left the laggy orbit at 20.7 ms
against 20.9 with vsync on, byte for byte, while the healthy path moved 4.1 → 0.6 ms; the
lag was never in the present, which is why `surfaceFormat()` still asks for no swap interval).
Marking the gizmo or the viewport `WA_OpaquePaintEvent`, making the gizmo native, and
reparenting it out of the viewport all changed nothing either — the full-tree repaint is
Qt's texture-widget rule, not something this app configured.

**The standing rule, then: no overlay `paintEvent` may decode, load or rescale an asset.**
Rasterize once and cache; a paint event that touches a resource is paying for it on every
frame of the next camera gesture. `gui_smoke`'s `an-orbit-step-costs-one-frame-in-both-modes`
block pins it three ways — paints-per-move (a frame count, so it cannot flake), the app bar's
and the whole tree's own paint cost against half a 60 Hz frame (wall clock, but ~40× the
measured value and less than half the defect's), and a clock-free structural check that
`appMarkPixmap()` returns the same `cacheKey()` twice. The **wall-clock frame rate is
deliberately not asserted**: the healthy floor is the display's refresh, so the same healthy
app reads 4.1 ms here and 16.7 ms on a 60 Hz panel, and any bound tight enough to catch a
21 ms defect would fail an ordinary monitor. `OcctViewWidget::PaintSample` /
`recentPaints()` is the instrument the block prints its table from, kept so the finding stays
re-measurable rather than being a story about a number nobody can take again.

**A floating card's logical size must cover whole device pixels**, through
`Theme::wholeDevicePixels()` at its `setFixedSize`. Widget geometry is logical and the
backing store is device-sized, so a card 93 logical rows tall at 150% scaling occupies
139.5 device rows: Qt flushes 140 and the paint event's clip - logical too - stops the
widget's own painter at 139. Nothing the widget paints can cross its own clip and the
viewport cannot paint underneath a child, so `paintSurface()` cannot save it and the size
is the only cure. The leftover row is the corner-nub failure one scale down: the bevel chip's
first magnified capture, taken while the viewport was still the native window this migration
has since replaced, carried a 264-device-pixel `0,0,0` hairline along its bottom edge - exactly
as black as an unpainted corner was, back when Qt's own backing store held nothing behind it.
Since the QOpenGLWidget migration the same leftover row instead shows whatever is genuinely
behind the card there - a far smaller defect than a black hairline, but still a defect: the
rule below exists so no card ever has a row to leave unpainted in the first place. Rounding to
a multiple of four is whole at every quarter-step Windows scale, so it does not read
`devicePixelRatioF()` - a size that is only right on the monitor it was written on is the same
bug with a longer fuse.

**`QScrollArea::sizeHint()` DOES NOT ANSWER FOR ITS WIDGET, and any layout
holding one inherits that lie.** It returns a cached size bounded to roughly 24 text lines
and ignores `sizeAdjustPolicy` entirely. This has now bitten twice, in two unrelated cards,
and cost four wrong fixes between them: the Items drawer came back too short and three
rounds went into raising a cap that was never the binding constraint; then the render
settings card, the moment it stopped being stretched to the viewport's full height and had
to answer for its own size, showed a scrollbar and put its Quality rows out of reach. Both
are fixed the same way and it is the only way that works: **add the height up from the
parts** - the widgets outside the scroll area, the layout's margins, and the SCROLL
CONTENT's own `sizeHint()` - and never ask the scroll area. `sizeHint`, not
`heightForWidth`: the content lives in a `widgetResizable` scroll area, so asking its layout
what height it wants AT A WIDTH re-enters the very layout pass that is asking.

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

**An EXACT-TOKEN pixel count measures the token as much as the thing, so it dies the day
anything blends.** Two checks read zero the moment the grid gained its face-on alpha
(improvements item 7) — "the work-plane grid is drawn ON TOP of the face it is locked to"
and "raising Grid detail densifies the LIVE grid" — and both were still true: the grid was
plainly there, every line of it just arrived as the token blended with whatever was behind
it rather than as the token. Each now captures the same frame with the grid switched **off**
and counts the pixels that moved, which is what each was asking all along and which no
future change to the grid's colour, width or alpha can quietly answer for. The general rule:
count exact token pixels when the token IS the subject (a gizmo arm must be its axis colour,
and a fully covered pixel resolves to exactly that colour even under MSAA); **differ two
frames when PRESENCE is the subject** — is it drawn, is there more of it, is it in front.
The differ is also cheaper to keep honest, because it needs no threshold argument: a pixel
either moved when the feature was switched off or it did not.

**And a ratio between two pixel COUNTS is a threshold in disguise — it encodes the geometry
that happened to be on screen the day it was written.** The auto-hover block asserted
`faceTint > edgeTint * 3` for "the middle of a face glows the whole face, not a line", which
held at 1216/226 and broke at 1933/692 — **on an unchanged, committed binary**, an hour
apart, because this machine's display arrangement moved that window onto a differently-scaled
monitor and the block's search then picked a longer edge. The A/B is what established that
(the same commit, rebuilt and re-run, failed identically), and it is the reason the rule in
Pitfalls about proving "pre-existing" with a comparison run exists. The repair was to measure
the CLAIM instead: flood the tinted pixels' own bounding box inward from its border, and
whatever the flood cannot reach is enclosed BY the tint. A face highlight closes a loop and
encloses its face (0.92 of its box); an edge highlight is an open line and encloses nothing
(0.00), however long, diagonal or thick. Scale-free, angle-free, monitor-free — and it
asserts BOTH directions, so the measure is proved to discriminate rather than to be satisfied
by any picture with the hover tint in it.

### CMake note

OCCT 7.8 renamed the data-exchange toolkits — `CMakeLists.txt` branches on
`OpenCASCADE_VERSION` (`TKDESTEP`/`TKDESTL` for ≥7.8, `TKSTEP`/`TKSTL` below). Modeling
toolkit names are unchanged across those versions.

Since the QOpenGLWidget migration the app also needs `Qt6::OpenGLWidgets` (a separate
`find_package` component from `Widgets`; the installed `qtbase[...opengl...]` feature set
already provides it) and, on Windows, `opengl32` — `wglGetCurrentDC` is the one Win32 GL
call `OcctViewWidget.cpp` makes, and Qt links opengl32 for itself without exporting it.

### `OcctViewWidget` — the bridge

**`OcctViewWidget` is a `QOpenGLWidget`** since the migration's Phase 1 (2026-09-04). OCCT
renders into the framebuffer object Qt hands it and Qt composites that frame with the rest
of the widget tree; the 3D view is no longer a native OS window. Everything above the
hosting layer is unchanged.

Construction splits in two, and the split is what retires the old lazy-init pitfall:

- `initializeViewer()` builds `Aspect_DisplayConnection` → `OpenGl_GraphicDriver` →
  `V3d_Viewer` (`SetDefaultLights()` + `SetLightOn()`) → `viewer->CreateView()` →
  `AIS_InteractiveContext`, and **touches no window and no GL context**, so it is safe from
  any entry point at any time. The driver is constructed with `theToInitialize = false` (Qt
  owns the context) and carries `buffersNoSwap` / `buffersOpaqueAlpha` /
  `useSystemBuffer=false` / `contextCompatible` matching `OcctViewWidget::surfaceFormat()`.
- `initializeGL()` — Qt's own callback, first at the first show, and **again after any
  context reset**, which is not the same as "once" and is exactly the case
  `releaseGlResources()` exists for — wraps the bound Qt context with
  a throw-away `OpenGl_Context` and hands its `RenderingContext()` to
  `myView->SetWindow(Aspect_NeutralWindow, ctx)`. The neutral window is **virtual**, sized
  in DEVICE pixels through `toDevicePixels()`, and its `DevicePixelRatio()` is left at the
  1.0 default (`WNT_Window` never overrode it either), so the one logical↔device conversion
  point is still the only place the ratio is applied. Its native handle is
  `WindowFromDC(wglGetCurrentDC())` — the window the *bound context* belongs to, never
  `winId()`, which would re-create the native surface this migration removed.

**A non-null `V3d_View` is not a view that can convert a point, and one signal reaches every
overlay in exactly that state.** `initializeViewer()` creates the `V3d_View` with **no
window** — `initializeGL()` is what grants it one — so between those two moments
`myView.IsNull()` is false while every `Convert`/`ConvertWithProj` has no window to answer
against. The route there is ordinary, not exotic: `resyncView()` → `displaySolid()` →
`initializeViewer()`, which emits `cameraChanged` while still windowless, and every overlay
that projected a point faulted on it (found on the joint chip's context-loss path, which is
precisely where that rebuild runs). `OcctViewWidget::viewReady()` is the missing half of the
question `IsNull()` was already asking, consulted in `projectToScreen()` — the ONE function
every projecting overlay goes through. The residual, stated honestly: `initializeViewer()`
still emits `cameraChanged` while windowless, and the overlays are safe only *because* they
all route through that choke point. A projection written straight against `myView` somewhere
new would fault exactly as before.

`paintGL()` wraps Qt's current FBO (`OpenGl_FrameBuffer::InitWrapper`, through a subclass
that calls `SetFrameBufferSRGB(true, false)` because Qt's colour attachment is `GL_RGBA8`
and not sRGB — without it every measured `Dump` colour comes back through the wrong curve),
syncs the neutral window to the FBO's size, scrubs the GL state in both directions (Qt's
bound program/texture/blend before; pixel-store alignment and active texture after) and
calls `Redraw()`. `resizeGL()` resizes the neutral window and calls `MustBeResized()`.

**`scheduleRedraw()` replaced every `Redraw()`/`UpdateCurrentViewer()` that meant "put this
on screen".** OCCT no longer owns the surface, so it does not get to decide when a frame is
presented — `update()` does. The exceptions are the paths that need pixels *before they
return* (`saveSnapshot`, the render-mode tier probe and every measuring probe,
`awaitPathTracingConvergence`, `redrawRenderModeLive`): those open a `GlScope`, which makes the context
current, keeps the framebuffer wrapper in step, and asks for a composite on the way out. It
is nesting-safe, so a probe calling a probe cannot have the context pulled from under it.

**One caller must NOT use it, and finding that out cost a whole phase's sharpest bug.**
`scheduleRedraw()` is `Invalidate()` + `update()`, and `Invalidate()` means "the scene
moved" — which OCCT's progressive path tracer obeys by throwing away its accumulation. The
path-tracing convergence timer's tick is the one caller that is not reporting a change; it
is asking for one more sample of a scene that has not changed at all. It goes through
`scheduleAccumulationFrame()` (`update()`, no `Invalidate()`). See the render-mode section
for the measurement that found it.

**Context lifetime is OWNED, and it is the one thing this migration could not leave to
chance.** OCCT holds real GPU resources against Qt's context, and they can only be released
while that context is alive and current. `releaseGlResources()` does it in OCCT's own
order — sub-renderers detach, `RemoveAll`, `myView->Remove()`, viewer, window, display
connection last — under a `makeCurrent()`, and leaves the widget in its
never-initialized state so a later `initializeGL()` rebuilds instead of reviving dead
handles. Two callers, and they are the only two moments a context can die under the widget:

- **a real destructor**, not `= default`. `~QOpenGLWidget` has not run yet at that point,
  which is what makes `makeCurrent()` still work; member-order destruction released those
  resources with no context current and in no defined order. `MainWindow` does
  `delete myCompareView` on *every* compare-pane close, so this is routine, not exit-only.
  It disconnects the hook below first — Qt disconnects in `~QObject`, which runs *after* the
  derived destructor body, so a live connection would call back into a half-destroyed object.
- **`QOpenGLContext::aboutToBeDestroyed`**, Qt's only hook for releasing while the dying
  context still exists — a driver reset, a reparent the attribute below does not cover.
  Direct connection. The widget would then render empty — every presentation map it held is
  cleared and the next `initializeGL()` rebuilds an *empty* viewer — so the same handler
  emits **`glResourcesReleased()`**, and `MainWindow` answers it with `resyncView()` plus
  `setRenderModeEnabled(false)`: the machinery undo, redo, open and restore already use, not
  a second one. The render-mode half is not tidiness — `myRenderModeActive` is cleared inside
  the widget by the release, so leaving `View → Render mode` checked would break the
  single-source-of-truth rule across the one event nobody drives. The signal is emitted from
  the *context-loss* caller only, never from the destructor's own release, where there is no
  owner left to tell.

**`Qt::AA_ShareOpenGLContexts` is set before `QApplication`, in `main.cpp` and in
`gui_smoke`, and it is load-bearing.** Without it Qt destroys a `QOpenGLWidget`'s context on
every reparent — and this app reparents its viewport for real, into and out of the compare
pane's `QSplitter`. Measured as a hard process crash before the teardown above existed.
`gui_smoke` pins the attribute, pins repeated compare open/close cycles, and pins the
*ordering* through a monotonic sequence the two events share
(`lastGlReleaseTick() < lastGlContextDeathTick()`) rather than trusting that the destructor
did the right thing.

**`paintGL()`'s re-attach branch is a backstop, not the recovery**, and it compares
**context identity**, never the native window handle: a context rebuilt on the same
top-level window leaves that handle unchanged.

**`SetImmediateModeDrawToFront(false)` is PARKED PERMANENTLY, on measurements taken twice.**
`Graphic3d_CView` defaults it to TRUE, which draws immediate structures — the hover
highlight, the manipulator mid-drag — "directly to the front buffer", and warns they "will
be missed in image dump since it is performed from back buffer"; a QOpenGLWidget has no
front buffer, so turning it off looks obligatory. Phase 1 measured it and parked it
provisionally; Phase 3 re-measured it in the finished compositing layer, on one binary with
the flag behind a throw-away switch, and settled it. Setting the flag:

- **does buy one real thing.** With the flag at its default OCCT allocates a separate
  immediate-scene framebuffer and asks for `GL_SRGB8_ALPHA8` as its colour attachment; this
  driver refuses with `GL_INVALID_OPERATION` and OCCT logs `Immediate FBO WxH@0
  initialization has failed` twice a run. With the flag set, that FBO is never allocated and
  the log is clean.
- **costs the render's correctness.** The composited window against the `Dump`, as median
  colour: **2.4/255 apart without the flag, 97.6 with it** — the on-screen studio floor a
  full step darker than the backdrop it is calibrated to dissolve into, seam and all, while
  the `Dump` every calibration is read from stays right. And a user-chosen background lands
  **118/255** from the colour picked instead of 26 — Phase 1's own number, reproduced exactly
  in a codebase whose overlay compositing changed completely in between.

The harm it guards against is measured **not to exist in this hosting layer**: OCCT's
front-buffer writes land in the bound default framebuffer, which is the one Qt composites
and the one `Dump` reads. That is a pinned fact, not a claim — `gui_smoke` Dumps a hovered
body and finds 13,941 hover-tinted pixels against 0 unhovered, with and without the flag.
A cosmetic log line against the render's correctness; the log line loses.

**Say what that last measurement covers, though.** The reason immediate content lands where
`Dump` can read it *here* is precisely that the separate immediate FBO was **refused**: with
no immediate framebuffer to draw into, the fallback is the bound default one. `GL_SRGB8_ALPHA8`
is colour-renderable in core GL 4.x, so on hardware where that allocation **succeeds** the
immediate layer goes somewhere else and the 13,941-pixel pin is untested rather than
known-good. The decision does not rest on that leg — the flag's cost is `Dump`-side and so
driver-independent — and the pin is a live check, so a machine where the allocation succeeds
and the highlight stops reaching `Dump` **reports** it rather than hiding it.

**Provenance of the A/B, since it decides how much of one column to believe.** The no-flag
**2.4** is unambiguously *post*-accumulation-fix — it is the same number the Dump-against-screen
finding below reports. The with-flag **97.6** is Phase 3's too, "in the finished compositing
layer", but the report never states whether that switch was thrown before or after the fix in
the same session — and it matters, because the *pre*-fix screen-vs-`Dump` distance was 85.5
(`#93918f` against `#c4c3c0`), so grain alone could account for most of a 97.6. Which is why
the decision does not stand on that column. It stands on the **user-chosen background: 118.2
against 26, measured in Phase 1**, before `scheduleAccumulationFrame()` existed at all, on a
`V3d_View::Dump` no amount of accumulation restarting can move, with the rest of the frame
byte-identical.

**That `Immediate FBO` error is harmless and is NOT a scaling bug — parked with its
measurement.** Phase 2 reported it at `QT_SCALE_FACTOR=1.5` and read it as the cause of two
black-line sweep failures there. A 100% A/B disproved that: the identical
`GL_INVALID_OPERATION` on `fbo0_imm:color` is logged at 100% (1200x732) and at 150%
(1800x1098) alike, and the 100% run carrying both messages passes every check in the suite.
OCCT falls back on its own and the frame is correct. The only lever over it is the flag
above, which costs more than it saves.

**The two 150% black lines were the CAPTURE, not the app.** `printWindowCapture()` took
`GetWindowRect` at face value, and at 1.5× this suite's windows are 1239 device rows tall on
a 1080-row display: DWM has no composited content for the part hanging off the bottom, it
comes back as undrawn white with a black corner, and `longestBlackRun()` duly reported a
1768 px line at y=1179 in a window with nothing wrong with it. The capture is now cropped to
the intersection with **the monitor the window is on** — not the virtual-desktop bounding
box, which on a multi-monitor desktop (4480x1920 around a 1920x1080 primary here) covers
rectangles no display occupies and made the crop a silent no-op. At 100%, where the window
fits, nothing changes. A crop rather than a wider inset: an inset big enough to clear the
strip would stop sweeping rows that are genuinely the app's.

**`wrapDefaultFramebuffer()` binds `defaultFramebufferObject()` before `InitWrapper`.**
`InitWrapper` wraps *whatever is bound*, and this function then treats that framebuffer's
size as authoritative — so a nested `GlScope` (which binds nothing, by design) entered right
after an OCCT redraw could have wrapped a shadow map or a ray-tracing accumulation buffer,
resized the view to it, taken a pixel measurement at the wrong size, and self-healed on the
next frame with nothing ever reporting it.

**No class outside `OcctViewWidget` redraws the viewer any more.** `GridRenderer`,
`DimensionRenderer` and `PullArrowRenderer` each dropped their `UpdateCurrentViewer()` and
return **whether anything actually changed** instead; `OcctViewWidget` asks for the frame,
and only on a true return. Two reasons, and the second is the sharper one: those calls ran
from ordinary Qt slots with no Qt context current and worked only because OCCT re-made *its*
context current behind Qt's back, and `updateEdgeDimension()` runs on **every hover mouse
move**, so an unconditional `Invalidate()` + `update()` there cost a discarded-and-rebuilt
OCCT frame per mouse event — three of them in edge mode. `DimensionRenderer::show()` now
carries an equal-guard on the span, the extension normal *and* `worldPerPixel`, because all
three are built into the annotation; `refresh()` deliberately bypasses it, which is its
whole job.

`QSurfaceFormat` (depth 24, stencil 8, compatibility profile) has ONE derivation,
`OcctViewWidget::surfaceFormat()`, read by `main.cpp` and `gui_smoke` before
`QApplication` — the only moment the application default can be set — and by the widget's
own constructor. The suite asserts what the context was **granted**, not what was requested.

**Phase 3 finalized it against the measured tiers, and the decision includes one thing it
deliberately does not ask for.** Compatibility stays: this machine's probe reaches
PathTracing through a compatibility context, timing one GPU-synchronized path-traced redraw
at 3 ms against a 1500 ms threshold, and every calibrated pixel in the render block reads
exactly what it read through the pre-migration native window — there is nothing for a core
profile to win back. **No `setSamples()`, ever**: multisampling the DEFAULT framebuffer is
the one attribute that would break this hosting outright, because Qt would hand the widget a
multisampled FBO, OCCT would wrap it as its default framebuffer, and every pixel this
project treats as ground truth is read back out of that buffer — a multisample colour
attachment cannot be read without a resolve step nothing here performs. Antialiasing is
OCCT's to do inside the scene, where it costs the measurements nothing.

**And the scene does it: `NbMsaaSamples = 4` (`kViewMsaaSamples`), chosen from captures.**
OCCT multisamples inside its own offscreen framebuffers and resolves before the frame reaches
Qt's FBO, so `Dump` still reads a single-sample buffer and the "no `setSamples()`" law above is
untouched. It is set **once, in `initializeViewer()` right after `CreateView()`** — the one
function the first show, the compare pane's viewer and the rebuild after
`releaseGlResources()` all go through — so it applies to ordinary modeling, not only render
mode, and render mode's save/restore does not list the field and so cannot drop it. Measured
on an RTX 4090 at 1100×776, 100%: gizmo silhouettes went from ~0% partial-coverage pixels to
50–70%, each arm's middle stayed the exact axis token (a fully covered pixel resolves to
exactly its colour, so every token count in the suite stands), the Shadows tier's cast-shadow
check passed with its ratio unchanged, and an orbit step went 4.14 → 4.17 ms. **8 was
measured and rejected**: a few more points of edge coverage at twice the per-pixel sample
cost. `gui_smoke` pins it structurally (`RenderParamsProbe::nbMsaaSamples` is 4 before render
mode, after leaving it, on a fresh viewer, and on that viewer again after its context's
`aboutToBeDestroyed` released and rebuilt it) and in pixels (the Move X arm's cross-sections:
exact token in the middle, and 9 of 24 silhouette edges blended against 0 of 24 with the
samples at 0).

**MSAA does nothing for the sketch marks, which is why they are images.** A point sprite has
no geometric edge to multisample: OCCT's stock `Aspect_TOM_RING1`/`Aspect_TOM_BALL` glyphs
measured pixel-for-pixel unchanged under it. The cursor ring, the placed-point dots, the
first point's accent square and the Mirror placement handle are therefore
`Aspect_TOM_USERDEFINED` markers built from a `QPainter`-antialiased **alpha mask**
(`sketchMarkerImage()`), which OCCT tints with the aspect's colour — so the `Theme` token is
still the one colour source and a theme edit recolours a mark around the same image. Sizes
are the stock glyphs' own device extents at 100% (ring 17, dot 9, square 7) times
`devicePixelRatioF()`, rounded odd, re-derived from `resizeGL()` on a scale change; the ring
is thin (a tenth of its width, 1.7 px at 100%) because that is the capture the user picked.
Images are **cached by shape and size** — the cursor ring is rebuilt on every hover move,
and a fresh image per move would be a texture upload per mouse event. Two findings: at equal depth
the solid dot measured covering the square completely although the square is displayed
after it, so the square carries `Graphic3d_DisplayPriority_Above`; and the outline's own line is drawn over
the marks, so a mark's centre pixel is legitimately contested and the suite asks its fill of
the ground (no bare ground in the square's interior) rather than of a 3×3 core.

Event wiring: RMB drag→turntable orbit around the current view target (Unity-style, the user's explicit preference — no cursor-anchored pivoting), MMB drag→pan, wheel→zoomToward cursor; camera state lives in CameraController and is pushed via SetEye/SetCenter/SetUp. FOVy is fixed at 45° for the life of the view **except while render mode is on**, where the settings card's Camera FOV override is read through `OcctViewWidget::effectiveFovyDeg()` by both `applyCameraState()` and `worldPerPixel()` and pushed back through `applyCameraState()` on exit, so no override ever leaks outside render mode; **which projection is drawn with it moves** — see below.

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

**Auto selection (spec `docs/superpowers/specs/2026-09-06-auto-selection-design.md`, both
phases merged) is the EDITOR's one selection behaviour**: modes 2 AND 4 activated on every body
at once, so the cursor decides what a click takes — within 8 logical pixels of an edge the
edge glows, otherwise the face does, and a click takes exactly what glows. `OcctViewWidget`
is constructed in it. **The three Select Bodies/Faces/Edges actions, their rail chips, their
menu entries and their glyphs are DELETED**, and `Solid`/`Face`/`Edge` survive as a
**documented seam**: within the editor nothing calls `setSelectionMode()` at all, and the only two callers anywhere are `gui_smoke` and **`SceneWindow`**, which picks whole bodies (`Solid`) because a scene has no gesture that wants a face or an edge — see "The scene editor" above.
`gui_smoke` uses the seam where a check merely needs a selection OF A GIVEN KIND as setup and
driving the real hover path would add nothing (each use justified in place); a check whose
SUBJECT is selection or a gizmo drives the real path — `pickFaceOf()`, `pickEdgeOf()` and
`pickBodyOf()` are the three helpers that do it, and they clear the selection first, because
a live arrow or gizmo handle takes a press outright before the picker ever runs.

**Two OCCT facts the arbitration cost a measurement each.** Raising the tolerance is not
enough on its own: `AIS_InteractiveContext::SetPixelTolerance` sets a CUSTOM tolerance that
OCCT adds to each entity's own sensitivity (`PixelTolerance()` reports the sum, so it is not
the number you set — `MainSelector()->CustomPixelTolerance()` is), and the resulting candidate
radius measured about **0.75×** the custom value (8 → 6 logical px), which is why the
candidate tolerance (12) and the promise (8) are two separate constants. And which candidate
OCCT *highlights* is `SelectMgr_SortCriterion::IsCloserDepth()`, which leads with **depth** —
so on a box's top face the edge sat at depth 1431.5 while the face ran 1429.6 one pixel in to
1410.4 at six, and the face won the hover **from one pixel out**. Neither obvious lever
helps: `SetPickClosest(false)` swaps the whole selector to priority-first (edge 7 beats face
5 — but at *any* depth, anywhere, which is x-ray picking), and the owners' own
`SetPriority()` is only consulted after the depth comparisons have already answered.
`OcctViewWidget::preferDetectedEdge()` therefore arbitrates in SCREEN space — the spec's own
sentence, measured in the space the sentence is about, using `SelectMgr_SortCriterion::Point`
projected back — and hands the choice to OCCT through `HilightNextDetected()`, so the
highlight, `DetectedShape()` and `SelectDetected()` all still read one field and a click
cannot disagree with what is glowing.

**Phase 2 corrected that arbitration twice, both times by measurement, and both corrections
are about the same thing: a rule stated in PIXELS has to be resolved in pixels.** Phase 1
took the first candidate in RANK order (= depth order) inside the tolerance, and returned
early whenever OCCT had already detected *some* edge. Both were wrong once auto became the
only pick: on a body a few tens of pixels across, all four edges of a face are candidates at
once, so depth chose between them and routinely chose one five or six pixels away over the
one the cursor was sitting exactly on — and the early return meant this function never even
looked. Measured on two small bodies: every click aimed at an edge's own projected midpoint
took a neighbour instead, four candidates running. It now walks the whole candidate list and
keeps the **nearest on screen**, with a strict comparison so ties still fall to rank order —
which is the case that matters for one edge hidden directly behind another, where both
project to the same pixel and the nearer one is still taken.

**HISTORICAL: the custom tolerance used to stand down while `AIS_Manipulator` was
attached** — OCCT's custom tolerance belongs to the SELECTOR and is added to every
registered entity's sensitivity, and it merged the manipulator's tightly-packed parts
(hovering out along the Z arm armed the rotation ring about Y at every step). Measured cost:
the edge's hover reach fell from 7 px to 2 px with a gizmo up. **The stand-down died with
the manipulator** (custom gizmo Phase 2): our handles have no `ComputeSelection`, register
nothing the tolerance could blur, and `applySelectionTolerance()` keeps Auto's raised
tolerance up unconditionally — the full 8 px edge reach now survives a gizmo on every tool,
which was the consequence the custom gizmo was built to buy, and the suite's sweep pins the
reach as EQUAL with the rings up rather than shorter.

**A refusal a double-click makes moot is WITHDRAWN, not left standing.** Qt delivers a
double-click as press/release/DblClick/release, so a Shift+double-click's own FIRST release
runs an ordinary additive pick with bodies already held — which in auto always lands on a face
or an edge, because mode 0 is not activated. The kind lock correctly refuses it and correctly
says why, half a beat before the DblClick adds the body the sentence was explaining how to
add. `showMessage()` with no timeout is permanent and `myStateLabel` beside it is a permanent
widget, so the two were legible at once: *"2 bodies selected"* next to *"Shift adds bodies to
this selection — double-click a body to add it"*, the bar instructing the user to do the thing
they had just done. A release cannot know a double-click is coming, so
`autoPickRefusalWithdrawn()` takes it back — emitted wherever a landed pick clears a standing
refusal, and emitted BEFORE `selectionChanged()` so the ordinary message is the last word.
`MainWindow` only ever clears the sentence IT painted, compared against what the bar is
actually showing.

**Shift is kind-locked, and the lock is DERIVED.** `selectionKind()` reads the live selection
rather than remembering the first pick, so undo, delete, a mode switch and
`setSelectedSolids()` all move the lock with them and there is nothing to keep in step. A
Shift-click of another kind is a quiet no-op carrying a sentence
(`OcctViewWidget::autoKindRefusalText()`, one author), emitted as `autoPickRefused()` and put
in the **status bar** by `MainWindow::onPickRefused()`. Deliberately not a Failure toast — the
spec rules this gesture a quiet no-op, and a toast on every mistaken Shift-click would shout
at a click that changed nothing — and deliberately not the state label, which describes the
selection a refused click did not change. Bodies accumulate by Shift+**double**-click, because
a plain click in auto lands on a face or an edge. Qt delivers a double-click as
press/release/DblClick/**release**, and both halves of that bit: the gesture's own first click
moves the lock (hence `myAutoKindBeforeClick`, read one event later) and the trailing release
re-picks (hence `myAutoBodyPickTaken`, which swallows it — the same "the gesture that started
owns the release that ends it" rule every drag in that file already keeps).
`resetPickGesture()` drops both flags, the refusal and the last-picked edge, and
`MainWindow::resyncView()` — the one choke point every document swap goes through — calls it.

**The gizmo predicates key on selection CONTENT now, and the disjointness argument moved with
them.** It used to be "face pull needs face mode, bevels need edge mode, the transform gizmo
needs body mode, so no two can be true at once". It is now one enum's worth, asked in three
places against three different values: `selectionKind()` derives exactly one of
`None`/`Body`/`Face`/`Edge` from the live selection, `canPullSelectedFace()` requires `Face`,
`bevelTarget()` requires `Edge` and `transformableBodyId()` requires `Body` — so at most one
can hold, by construction rather than by three predicates kept in step. Kind-locked
accumulation is what makes the foundation solid: a selection can never hold two kinds at once,
so the derived value is never a coin toss. `ExtrudePreview` is still held apart by
`hasPendingFace()`, which all three refuse on, and `mirrorPlacementEnvironmentOk()` and
`linkGestureEnvironmentOk()` take the same `Body` term for the same reason
`transformableBodyId()` does — `selectedSolidIds()` reports the OWNING body of a selected face
or edge, so a count alone cannot answer it. **The face pull's term is genuinely new**: it used
to be implicit (`selectedFace()` was null outside face mode), and with no modes left that
inference is gone.

**Two press-swallowing hazards this re-key opened, both closed:**
- **A PLAIN double-click on a face must still take the body.** In auto a plain click on the
  middle of a face raises the pull arrow AT THAT FACE'S CENTRE, which is exactly where the
  second click of a double-click aimed at the body lands — so `arrowHit()` swallowed it and
  every such gesture was a no-op. The guard stands down in auto: an arrow has no CLICK meaning
  at all (it is press-drag-release), and the gesture's own first press/release pair has
  already offered it that gesture and been answered. **The stand-down used to be for the
  PLAIN gesture only**, and the reason is worth keeping although the case is gone: a blanket
  exemption let a *Ctrl*+double-click on a live bevel arrow fall past the lock branch (which
  needs a detected FACE and finds an edge) and take the whole body out from under the arrow,
  so Ctrl kept the pre-phase guard. **Improvements item 9 deleted the Ctrl route entirely**,
  which deletes the asymmetry with it: there is no lock branch left for a modifier to aim at,
  Ctrl means nothing here, and the stand-down is now unconditional in auto. The `gui_smoke`
  probe that pinned the old guard pins the new answer on the same pixel and the same live
  arrow — Ctrl takes the whole body, exactly as no modifier does.
- **A Shift+double-click over a gizmo arm must still add the body underneath.** In the
  manipulator era `AIS_ManipulatorOwner` outranked a shape's owner and the double-click
  added nothing, so both `mouseDoubleClickEvent()` and the additive release carried a
  `Deactivate`-around-`MoveTo` shield. The hazard AND the shields died with the
  manipulator (Phase 2 cleanup): the custom gizmos' handles never enter the pick
  pipeline, so the double-click reaches the body under an arm with no dance at all.

**What is NOT closed, and is ruled rather than overlooked: a live arrow owns a band of pixels
around what it stands on.** `arrowHit()` is a 14 px screen-space test that takes a press
outright, so with an edge selected a click 8–14 px from it — over a face the user may well
want — selects nothing at all. That is unchanged behaviour (an arrow has always claimed
presses on itself) and the spec puts "what the gizmos do once raised" out of scope, but it is
newly reachable now that the plain click is the universal pick gesture. `gui_smoke` pins it in
both directions rather than leaving it to be rediscovered as a mystery dead click: it searches
for a pixel the arrow claims that also hovers as a face (finding it is its own check), then
asserts the click there changes nothing.

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

**Gestures**: plain double-click selects the whole body — performed by the viewport itself
since the auto-selection switch, there being no mode action left to announce it to;
**Ctrl+double-click locks nothing** — it was Lock to Face's second route and improvements
item 9 removed it, so Ctrl is an ordinary modifier there now and the double-click means what
a plain one means. `L`/`Shift+L`/menu are the routes that remain, and they were always the
ones the tooltip and the shortcut sheet named. The pull-arrow's double-click guard used to
carry an exemption naming the face-lock BRANCH rather than the modifier — that is what kept a
Ctrl+double-click on an EDGE from inheriting it — and with the route gone **nothing is exempt
at all**: under auto the guard stands down for the whole-body route and for every modifier
alike. See "Selection" for the two press-swallowing hazards the re-key opened and how each is
closed.

**Notes can be silenced, Failures cannot.** `View → Show notifications` drops
`Toast::Kind::Note` only; a refusal that reports nowhere would violate the
never-silent-failure law, so `Failure` bypasses the toggle unconditionally. Every Note is
a success report carrying Undo; every refusal is a Failure — the taxonomy is load-bearing.

**Sketching**: Shift snaps the cursor onto the nearest of EIGHT directions at 45° steps
from the previous point (`SketchController::snapToCompass`), rather than only the previous
segment's own direction — which could not draw a square corner. The dial is measured in
the sketch **plane's own (u, v) axes**, not world X/Y, so a locked or face-on plane gets
the same dial in its own coordinates; a candidate coinciding with the start is refused
rather than guessed at, and the projection onto the chosen line is grid-snapped along it.
The close-hit on the first point is tested on the
plane hit **snapped first when Snap to Grid is on** (raw otherwise) and outranks the
straight constraint, with the radius in one place
(`OcctViewWidget::sketchCloseTolerance()`). The raw-only comparison it replaced was the
Milestone 3 whole-branch review's 1.25× find: `myCloseTarget` is the SNAPPED first point,
so a raw probe could sit up to √2/2·step from it while the tolerance is step/2 — whether
hovering the first point closed depended on where in the grid cell the ray landed. Ctrl+Z mid-sketch removes the last point
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
- **HISTORICAL (pre-Phase-1): `paintEngine()` must return `nullptr`.** That was the law for a
  `WA_PaintOnScreen` widget owning a native GL surface. `OcctViewWidget` is a
  `QOpenGLWidget` now: it overrides no paint engine, sets none of `WA_PaintOnScreen` /
  `WA_NoSystemBackground` / `WA_OpaquePaintEvent` / `WA_NativeWindow`, and paints through
  `initializeGL`/`paintGL`/`resizeGL`. Kept only so the phrase, which still appears in older
  comments elsewhere in the tree, is findable and dated.
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
- **HISTORICAL: `AIS_Manipulator` is constructed with zoom persistence ON in OCCT 8.0** —
  undocumented beside `AdjustSize`'s documented default; its drawn size never followed the
  camera, so camera-derived sizing wrote numbers that never reached a pixel and a probe
  reading `Size()` back was a self-oracle. The class is deleted from this app (custom
  gizmo Phase 2), but the rule it taught is permanent: **measure gizmo pixels in a
  `Dump`, never the setter's own data.**
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
- **HISTORICAL (pre-Phase-1): reparenting a native GL widget out of a `QSplitter` and back
  left the real HWND client rect stale**, while Qt geometry, `V3d_View` and its own `Dump`
  all agreed on the WRONG size — Milestone 3's compare pane, closed by a defensive,
  always-on `SetWindowPos` in `resizeEvent()`. There is no HWND of ours to go stale now: the
  view renders through an `Aspect_NeutralWindow` whose size `resizeGL()` sets and `paintGL()`
  re-syncs from the FBO itself, and the `SetWindowPos` hack is deleted. `gui_smoke`'s pin was
  rewritten to the same truth on the new mechanism — `hostWindowSize()` against
  `viewportDeviceSize()` after the splitter round trip. **What the reparent can still do is
  destroy the GL context**, which is a different and worse failure — see
  `Qt::AA_ShareOpenGLContexts` in the bridge section above.
- **`initializeViewer()` must stay lazy, and since Phase 1 it is lazy BY CONSTRUCTION.**
  The old hazard: `resyncView()` legitimately needs the viewer to exist, and calling
  `initializeViewer()` unconditionally to guarantee that reached it from
  `showInitScreen()`'s path inside the constructor, ahead of the window's first `show()`,
  forcing `winId()`/native-window realization far earlier than the contract intended — which
  broke camera and focus determinism (`startup distance is 700 mm`, focus-visible) in
  `gui_smoke`. `initializeViewer()` no longer touches a window or a GL context at all, so
  that specific trap is gone; the window attach lives in `initializeGL()`, which only Qt
  calls. The rule it stood for still holds for anything that needs the CONTEXT:
  `GridRenderer::update()`'s discipline — a no-op until one exists, desired state recorded
  first and re-applied on the first real frame.
- **A custom Z-layer that clears depth also clears the SHADOW MAP, unless it is `Immediate`.**
  OCCT renders the shadow-map pass from the *normal* layer list, so a `SetClearDepth(true)`
  layer sitting in that list wipes the depth texture the Shadows render tier is built on —
  the cast shadow simply stops being drawn, with nothing logged and no error anywhere. Found
  by A/B rather than by reasoning: adding the transform gizmo's own layer turned
  `render-mode`'s cast-shadow check (a `Dump()`-differ, the same proof the tier probe itself
  uses) red, and the identical run at the parent commit was green.
  `SetRenderInDepthPrepass(false)` did **not** fix it; `SetImmediate(true)` did, because an
  immediate layer is drawn after all the normal ones and is not in the list that pass walks.
  The stock `Graphic3d_ZLayerId_Topmost` clears depth and does *not* break shadows, which is
  exactly what makes this easy to miss — the stock layers are special-cased and a custom one
  is not.
- **`Graphic3d_MaterialAspect` describes a surface THREE times, and OCCT's path
  tracer reads only the third.** The classic reflectance colours (ambient/diffuse/
  specular/emissive) drive rasterization and Whitted ray tracing; `Graphic3d_PBRMaterial`
  drives the PBR rasterizer; and `Graphic3d_BSDF` — a separate `myBSDF` member — is what
  the *path tracer* integrates, exclusively. **`SetPBRMaterial()` is an inline that
  assigns `myPBRMaterial` and nothing else**, so a material built with `SetColor` +
  `SetPBRMaterial` leaves the BSDF default-constructed and all-zero, and an all-zero BSDF
  returns zero radiance for every ray. That is the whole of the "path tracing renders the
  studio floor black" defect that survived three fix rounds and two structurally opposite
  material theories: pure-emission and pure-diffuse-albedo both measured `(0,0,0)`, which
  looked like proof the GI pass was not lighting the geometry, and was actually proof that
  neither theory was writing the field the GI pass reads. `Graphic3d_BSDF::CreateDiffuse`
  and `::CreateMetallicRoughness(pbr)` are OCCT's own conversions — use them beside
  `SetPBRMaterial` rather than instead of it, so the two descriptions of one surface cannot
  drift. The tell that a *material* rather than the *scene* is at fault: the same scene
  renders correctly under `RayTracing` and black under `PathTracing`, because only one of
  the two reads the BSDF. **Geometry was ruled out by measurement, not argument** — a
  reversed (single-sided, inward-normal) floor face and a closed `BRepPrimAPI_MakeBox` slab
  both rendered exactly as black as the original face.
- **An under-converged path-traced Dump is systematically DARK, not merely noisy.** The
  accumulation buffer is a running mean and the samples that have not arrived yet read as
  zero, so a probe that dumps too early measures a level that is wrong in a consistent
  direction. Five settle passes read a floor at 172 where twenty-four read 194 — and 172
  against a 194 backdrop is a failing seam that does not exist. `kMeasurementSettlePasses`
  is the one count every *measuring* probe shares for exactly this reason;
  `probePathTracingChangedImage()` keeps a smaller one deliberately, because it asks
  whether two frames differ rather than what either one reads.
- **Reading a path-traced frame's pixels RESETS the accumulation buffer.** `V3d_View::Dump`
  is not a free observation, so a "converge until two successive samples stop moving" loop
  measures a fresh short accumulation every iteration, agrees with itself immediately, and
  settles at the wrong level — measured: such a loop exported 136 against an at-rest 194,
  barely better than the 140 it replaced. The tell is that every probe in this file reads a
  correct value doing *N redraws and one Dump*, which would be impossible if a Dump were
  free. Convergence is therefore driven by a **pass count under a time cap**, never by a
  sampled criterion. `OpenGl_View::myAccumFrames` is the number you actually want and it is
  `protected` with no accessor, on a class the graphic driver constructs.
- **Path tracing sRGB-encodes its output, including the background colour; the other
  tiers do not.** The same `Quantity_Color` that rasterizes to the backdrop token
  `(194,191,186)` path-traces to `(227,225,222)`, which draws a horizon line across the top
  of every shot. `kPathTracingBackdropGain` is the measured pre-scale that lands it back on
  the token, kept as a *fraction* of the token so an Appearance edit still moves it.
- **HISTORICAL: `AIS_Manipulator` styling has a real API wall for colour and a separate,
  only pixel-measurable one for proportions (OCCT 8.0.1)** — the class is deleted from
  this app, kept because the finding is about OCCT.** No setter reaches a per-axis colour at
  any access level — `Axis::myColor` has none — so restyling the 3D transform gizmo to match
  a token stops there, structurally. Proportions look reachable (`protected Axis myAxes[3]`
  plus public `Axis::SetAxisRadius()`), but a measured `Dump` probe of a subclass that used
  them showed the rendered cross-section **growing** as the radius shrank — the thinning shaft
  revealing same-material neighbouring parts underneath, not a shrinking control. Measure
  pixels before believing any styling setter's name, the same law the zoom-persistence finding
  already established for this class.
- **A/B a claimed pre-existing failure against the parent commit before calling it
  pre-existing.** A Milestone 3 task asserted a scale-run flake was environmental; a review's
  A/B run against the parent commit proved the same run passed there and the branch's own
  commit deterministically broke it (the `initializeViewer()` regression above). The rule
  holds generally: "pre-existing" and "environmental" are claims that need a comparison run to
  back them, not a plausible story.
- **A mutation test that produces no red line has tested nothing — and in this repo three
  separate mechanisms each made a mutation report GREEN while the mutation never applied at
  all.** The tree's files are LF, so a patch anchor written with CRLF matched nothing. An
  anchor spanning a non-ASCII character — an em dash, which this project's copy is full of —
  did not survive PowerShell's `ReadAllLines`/`WriteAllLines` round trip. And `Copy-Item`
  **preserves mtime** on the restore, so MSBuild skipped the rebuild and the run measured a
  stale exe. Every one of the three reads exactly like a pass. The rule: a mutation counts
  only when it produces a real red line **naming the expected check**, and a run that left no
  output file is a failure to apply, never a pass. Editing the source in place — so the tool
  writes LF and moves the mtime for free — and then reading the named failure back is what
  makes the evidence worth having.

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
