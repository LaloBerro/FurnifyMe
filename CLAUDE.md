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

### A warning about automating this GUI

Synthetic-input testing on this machine is workable but fragile, and cost far more time than
it was worth before the UI reported its own state. `GetWindowRect` reported a 1500x2900 window on a 1920x1080 screen, so PowerShell's
coordinates and the app's are separated by DPI virtualization, and clicks land somewhere
other than intended. Two earlier "failures" were the harness, not the app: clicks falling
outside an unmaximized window, and shift-clicking twice into the same solid (which XOR
correctly *deselects*). Two things make it tractable: maximize the window with
`ShowWindow(h, 3)` and **assert the resulting size** (1936x1048 here) rather than trusting
`MoveWindow`, and read the status bar's state label out of the screenshot instead of guessing
whether an action landed. Give the two solids different heights so each has a screen region
where only it is pickable - coplanar slabs make shift-click ambiguous.

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
| `ModelingOps.{h,cpp}` | pure geometry, **zero Qt includes** — the only file that exists so far |
| `main.cpp` | `QApplication` + `MainWindow` |
| `MainWindow.{h,cpp}` | menus, toolbar, mode switching |
| `OcctViewWidget.{h,cpp}` | the Qt↔OCCT bridge — the only genuinely tricky file |
| `DocumentModel.{h,cpp}` | owns the list of solids |
| `SketchController.{h,cpp}` | 2D input → wire on a plane |

**Hard rule: `ModelingOps` must not include a single Qt header.** That invariant is what
makes the headless test possible; breaking it collapses the whole testability story. It is
enforced structurally: `ModelingOps` lives in the `furnify_geometry` target, which does not
link Qt and does not link the visualization toolkits (`TKV3d`, `TKOpenGl`, `TKService`) —
those are on the `furnifyme` app target only.

`applyBoolean()` returns a `BooleanResult{ok, shape, error}` rather than a bare shape,
specifically so a failed boolean cannot be mistaken for a success. Surface `error` in the
UI; never continue past `ok == false`.

### Qt plugin deployment - do not remove

Qt will not start without a platform plugin, and it looks for one in a `platforms/`
directory next to the executable, not alongside the Qt DLLs. vcpkg's applocal deployment
copies DLLs but **not** plugins, and the `windeployqt` feature is deliberately not installed
(it would have dragged the full default feature set back in). `CMakeLists.txt` therefore
copies `QWindowsIntegrationPlugin` and `QModernWindowsStylePlugin` itself in a POST_BUILD
step. Delete that and the app dies at startup with
`could not find the Qt platform plugin "windows"`.

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

Event wiring: `paintEvent`→`Redraw()`, `resizeEvent`→`MustBeResized()`, RMB drag→
`StartRotation`/`Rotation`, MMB drag→`Pan(dx,-dy)`, wheel→`StartZoomAtPoint`/`ZoomAtPoint`,
mouse move→`MoveTo(x,y,view,true)`, LMB click→`SelectDetected()`.

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
