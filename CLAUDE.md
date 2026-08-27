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

### Windows (vcpkg — the reproducible path)

```powershell
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg install opencascade:x64-windows qtbase:x64-windows   # OCCT: 30-60+ min first time
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Alternative: official prebuilt OCCT installer from dev.opencascade.org + the Qt online
installer, then point `OpenCASCADE_DIR` and `Qt6_DIR` at them manually.

### Linux

```bash
sudo apt install -y build-essential cmake \
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
  libocct-data-exchange-dev libocct-visualization-dev qt6-base-dev libgl1-mesa-dev
cmake -B build -S . && cmake --build build -j
```

### Current state

Scaffolded, **not yet compiled** — no build has ever run here, because CMake, OCCT and Qt6
are not installed on this machine (MSVC 2022 + Windows SDK are). What exists:
`CMakeLists.txt`, `src/ModelingOps.{h,cpp}` (the full geometry core), and
`tests/headless_geometry.cpp`. The Qt files (`main`, `MainWindow`, `OcctViewWidget`,
`DocumentModel`, `SketchController`) are deliberately **not written yet** — see the
sequencing rule under Tests. `CMakeLists.txt` skips the `furnifyme` target while they are
absent, so a fresh clone still configures and runs the test.

### Tests

`tests/headless_geometry.cpp` needs no window and no GPU — it links only the modeling
toolkits and runs on any box, including CI.

```bash
cmake -B build -S . -DFURNIFYME_BUILD_APP=OFF   # geometry + test only, no Qt needed
cmake --build build
ctest --test-dir build --output-on-failure
```

`FURNIFYME_BUILD_APP=OFF` builds the geometry library and the test without requiring Qt at
all — the fastest loop for kernel work, and what CI should run.

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
