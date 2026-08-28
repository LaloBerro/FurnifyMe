# Camera Controller and Adaptive Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the free-rotation camera and finite grid with a turntable camera (Z always up, cursor-anchored pivot, animated transitions, MMB mapping) and an adaptive ground grid with no visible edge.

**Architecture:** A Qt-free `CameraController` in `furnify_geometry` owns turntable state (target/azimuth/elevation/distance) and derives eye/up; `OcctViewWidget` becomes an input translator that pushes that state into the OCCT camera. A `GridPresentation` (custom `AIS_InteractiveObject`) replaces `ActivateGrid`, faking distance fade by blending band colours toward the viewport background.

**Tech Stack:** C++17, Qt 6.11.1 Widgets (qtbase only), OpenCascade 8.0.1, CMake presets + vcpkg, MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-08-28-camera-grid-design.md`

## Global Constraints

- **`CameraController` must be Qt-free** — it lives in `furnify_geometry`, may use `gp_*`/`Bnd_Box` (TKMath) only. No Qt header, no visualization toolkit.
- `src/ModelingOps.*`, `src/DocumentModel.*`, `src/SketchController.*` stay Qt-free as before.
- Qt 6 Widgets, qtbase only. No `qtsvg`, no Qt Test module, no new dependency, never install anything.
- No `Debug` configuration; build `RelWithDebInfo` via presets. cmake at `C:/Program Files/CMake/bin/cmake.exe`.
- **Kill running app processes before every build** (`Get-Process furnifyme,gui_smoke -ErrorAction SilentlyContinue | Stop-Process -Force`) or link fails with `LNK1168`.
- **No OS-level input simulation** (`SetCursorPos`, `mouse_event`, `keybd_event`, SendKeys) in code or verification. Verify with `gui_smoke` (in-process Qt events) and still screenshots.
- Never `delete` an OCCT `Handle()`.
- Do not change any existing `QAction`'s visible text.
- The GUI suite currently prints `PASS (0 failures)` with **58 checks**; trust the actual run over any stale number. Headless suite is 2 executables, both must pass.
- Camera constants (from spec, verbatim): elevation clamp ±88°; distance clamp [1mm, 100m]; startup azimuth −45°, elevation +30°, target (0,0,0), distance 700; Top = elevation +89 (azimuth kept); Front = az 0/el 0; Right = az −90/el 0; Axonometric = az −45/el +30; animation ~250ms OutCubic; orbit sensitivity ~0.4°/px horizontal, ~0.3°/px vertical.
- **Decision recorded here (spec is silent):** the camera switches to **perspective projection** (FOVy 45°) at init. The turntable model is distance-based; OCCT's default orthographic camera zooms by scale, not distance, which would make `zoomToward` meaningless. A projection toggle stays out of scope.

---

### Task 1: CameraController core — state, orbit, eye/up

**Files:**
- Create: `src/CameraController.h`, `src/CameraController.cpp`
- Modify: `CMakeLists.txt` (add to `furnify_geometry` sources; add test target)
- Test: `tests/camera_controller.cpp` (new headless test, registered with ctest)

**Interfaces:**
- Consumes: nothing.
- Produces (used by every later task):

```cpp
struct CameraState {
    gp_Pnt target{0.0, 0.0, 0.0};
    double azimuthDeg = -45.0;
    double elevationDeg = 30.0;
    double distance = 700.0;
};

class CameraController {
public:
    static constexpr double kMinElevation = -88.0;
    static constexpr double kMaxElevation = 88.0;
    static constexpr double kMinDistance = 1.0;        // 1mm
    static constexpr double kMaxDistance = 100000.0;   // 100m

    const CameraState& state() const;
    void setState(const CameraState& s);   // clamps elevation and distance

    void orbit(double dAzimuthDeg, double dElevationDeg);

    gp_Pnt eyePosition() const;
    gp_Dir upVector() const;
    gp_Dir viewDirection() const;          // eye -> target, normalized
};
```

- [ ] **Step 1: Write the failing tests**

Create `tests/camera_controller.cpp`:

```cpp
//
// Headless tests for the turntable camera maths. No window, no GPU, no Qt -
// CameraController lives in furnify_geometry precisely so this file can exist.
//
#include "CameraController.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tol, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tol;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual, expected);
    if (!ok) ++g_failures;
}
}  // namespace

int main()
{
    // --- defaults are the spec's startup view --------------------------------
    {
        CameraController cam;
        checkNear(cam.state().azimuthDeg, -45.0, 1e-9, "startup azimuth is -45");
        checkNear(cam.state().elevationDeg, 30.0, 1e-9, "startup elevation is +30");
        checkNear(cam.state().distance, 700.0, 1e-9, "startup distance is 700mm");
        checkNear(cam.state().target.Distance(gp_Pnt(0, 0, 0)), 0.0, 1e-9,
                  "startup target is the origin");
    }

    // --- orbit accumulates and clamps ----------------------------------------
    {
        CameraController cam;
        cam.orbit(10.0, 5.0);
        checkNear(cam.state().azimuthDeg, -35.0, 1e-9, "orbit adds azimuth");
        checkNear(cam.state().elevationDeg, 35.0, 1e-9, "orbit adds elevation");

        cam.orbit(0.0, 500.0);
        checkNear(cam.state().elevationDeg, 88.0, 1e-9, "elevation clamps at +88");
        cam.orbit(0.0, -500.0);
        checkNear(cam.state().elevationDeg, -88.0, 1e-9, "elevation clamps at -88");
    }

    // --- eye position geometry ------------------------------------------------
    {
        // Elevation 0, azimuth 0 looks along -Y: the eye sits at +Y.
        CameraController cam;
        CameraState s;
        s.azimuthDeg = 0.0; s.elevationDeg = 0.0; s.distance = 100.0;
        cam.setState(s);
        const gp_Pnt eye = cam.eyePosition();
        checkNear(eye.X(), 0.0, 1e-6, "az 0 el 0: eye X is 0");
        checkNear(eye.Y(), 100.0, 1e-6, "az 0 el 0: eye Y is +distance");
        checkNear(eye.Z(), 0.0, 1e-6, "az 0 el 0: eye Z is 0");

        // Elevation raises the eye: at +30 the height is d*sin(30) = 50.
        s.elevationDeg = 30.0;
        cam.setState(s);
        checkNear(cam.eyePosition().Z(), 50.0, 1e-6, "el +30: eye height is d*sin(30)");

        // Eye-to-target distance always equals state().distance.
        checkNear(cam.eyePosition().Distance(s.target), 100.0, 1e-6,
                  "eye distance equals the state distance");
    }

    // --- up vector never rolls -----------------------------------------------
    {
        CameraController cam;
        for (double az = -720.0; az <= 720.0; az += 37.0) {
            for (double el = -88.0; el <= 88.0; el += 22.0) {
                CameraState s;
                s.azimuthDeg = az; s.elevationDeg = el; s.distance = 500.0;
                cam.setState(s);
                const gp_Dir up = cam.upVector();
                // Turntable invariant: up always has a positive Z component and
                // is perpendicular to the view direction.
                if (up.Z() <= 0.0) { check(false, "up vector lost its +Z component"); goto rollDone; }
                if (std::fabs(up.Dot(cam.viewDirection())) > 1e-6) {
                    check(false, "up vector not perpendicular to view direction"); goto rollDone;
                }
            }
        }
        check(true, "up vector keeps +Z and stays perpendicular across the whole sphere");
    rollDone:;
    }

    // --- setState clamps ------------------------------------------------------
    {
        CameraController cam;
        CameraState s;
        s.elevationDeg = 200.0; s.distance = 0.0001;
        cam.setState(s);
        checkNear(cam.state().elevationDeg, 88.0, 1e-9, "setState clamps elevation");
        checkNear(cam.state().distance, 1.0, 1e-9, "setState clamps distance to 1mm");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Wire the test into CMake and verify RED**

In `CMakeLists.txt`, add `src/CameraController.cpp` to the `furnify_geometry` source list, and next to the other test targets add:

```cmake
add_executable(headless_camera tests/camera_controller.cpp)
target_link_libraries(headless_camera PRIVATE furnify_geometry)
add_test(NAME headless_camera COMMAND headless_camera)
```

Run: `cmake --preset windows-headless` then `cmake --build --preset windows-headless`
Expected: FAIL to compile — cannot open include file `CameraController.h`.

- [ ] **Step 3: Write the implementation**

Create `src/CameraController.h`:

```cpp
#pragma once
//
// Turntable camera: target + azimuth + elevation + distance, up always derived
// from +Z so the horizon can never roll. Pure maths, no Qt, no visualization
// toolkits - lives in furnify_geometry so the headless tests cover it.
//
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

struct CameraState {
    gp_Pnt target{0.0, 0.0, 0.0};
    double azimuthDeg = -45.0;    // 0 looks along -Y; positive is CCW from above
    double elevationDeg = 30.0;   // 0 horizontal, +90 straight down at the target
    double distance = 700.0;      // eye-to-target, millimetres
};

class CameraController {
public:
    static constexpr double kMinElevation = -88.0;
    static constexpr double kMaxElevation = 88.0;
    static constexpr double kMinDistance = 1.0;
    static constexpr double kMaxDistance = 100000.0;

    const CameraState& state() const { return myState; }
    void setState(const CameraState& s);

    void orbit(double dAzimuthDeg, double dElevationDeg);

    gp_Pnt eyePosition() const;
    gp_Dir viewDirection() const;   // eye -> target
    gp_Dir upVector() const;

private:
    CameraState myState;
};
```

Create `src/CameraController.cpp`:

```cpp
#include "CameraController.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

void CameraController::setState(const CameraState& s)
{
    myState = s;
    myState.elevationDeg = std::clamp(myState.elevationDeg, kMinElevation, kMaxElevation);
    myState.distance = std::clamp(myState.distance, kMinDistance, kMaxDistance);
}

void CameraController::orbit(double dAzimuthDeg, double dElevationDeg)
{
    myState.azimuthDeg += dAzimuthDeg;
    myState.elevationDeg =
        std::clamp(myState.elevationDeg + dElevationDeg, kMinElevation, kMaxElevation);
}

gp_Pnt CameraController::eyePosition() const
{
    const double az = myState.azimuthDeg * kDegToRad;
    const double el = myState.elevationDeg * kDegToRad;
    const double horizontal = myState.distance * std::cos(el);
    // Azimuth 0 places the eye on +Y (looking along -Y); positive azimuth
    // rotates counterclockwise seen from above (+Z).
    return gp_Pnt(myState.target.X() - horizontal * std::sin(az),
                  myState.target.Y() + horizontal * std::cos(az),
                  myState.target.Z() + myState.distance * std::sin(el));
}

gp_Dir CameraController::viewDirection() const
{
    const gp_Pnt eye = eyePosition();
    return gp_Dir(myState.target.X() - eye.X(),
                  myState.target.Y() - eye.Y(),
                  myState.target.Z() - eye.Z());
}

gp_Dir CameraController::upVector() const
{
    // Project +Z onto the plane perpendicular to the view direction. Because
    // elevation is clamped short of the poles, this never degenerates.
    const gp_Dir view = viewDirection();
    const double dot = view.Z();   // view . (0,0,1)
    return gp_Dir(-dot * view.X(), -dot * view.Y(), 1.0 - dot * view.Z());
}
```

- [ ] **Step 4: Verify GREEN**

Run: `cmake --build --preset windows-headless` then `ctest --preset windows-headless`
Expected: 3/3 tests pass (the two existing plus `headless_camera`).

- [ ] **Step 5: Commit**

```bash
git add src/CameraController.h src/CameraController.cpp tests/camera_controller.cpp CMakeLists.txt
git commit -m "Add the turntable camera controller core"
```

---

### Task 2: CameraController — pivot, pan, zoom, frame, arc helper

**Files:**
- Modify: `src/CameraController.h`, `src/CameraController.cpp`
- Test: `tests/camera_controller.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:

```cpp
void setPivot(const gp_Pnt& pivot);            // re-anchor, eye stays put
void pan(double rightUnits, double upUnits);   // move target in the view plane
void zoom(double factor);                      // distance *= factor, clamped
void zoomToward(const gp_Pnt& p, double factor);
void frame(const Bnd_Box& box, double fovyDeg);// fit a bbox with ~10% margin
static double shortestArcDelta(double fromDeg, double toDeg); // in (-180, 180]
gp_Dir rightVector() const;
```

- [ ] **Step 1: Write the failing tests**

Append to `tests/camera_controller.cpp` before the final `printf` (add `#include <Bnd_Box.hxx>` at the top):

```cpp
    // --- setPivot preserves the eye ------------------------------------------
    {
        CameraController cam;
        const gp_Pnt eyeBefore = cam.eyePosition();
        cam.setPivot(gp_Pnt(200.0, -150.0, 40.0));
        const gp_Pnt eyeAfter = cam.eyePosition();
        checkNear(eyeBefore.Distance(eyeAfter), 0.0, 1e-6,
                  "setPivot leaves the eye where it was");
        checkNear(cam.state().target.Distance(gp_Pnt(200.0, -150.0, 40.0)), 0.0, 1e-6,
                  "setPivot re-targets the pivot point");
        // And orbiting afterwards keeps the pivot fixed by construction.
        cam.orbit(30.0, -10.0);
        checkNear(cam.state().target.Distance(gp_Pnt(200.0, -150.0, 40.0)), 0.0, 1e-6,
                  "orbit after setPivot keeps the pivot as target");
    }

    // --- pan moves the target in the view plane -------------------------------
    {
        CameraController cam;
        CameraState s;
        s.azimuthDeg = 0.0; s.elevationDeg = 0.0; s.distance = 100.0;
        cam.setState(s);
        // Looking along -Y: right is +X... verify against rightVector, and up
        // for this pose is +Z exactly.
        cam.pan(10.0, 5.0);
        checkNear(cam.state().target.X(), 10.0, 1e-6, "pan right moves target +X here");
        checkNear(cam.state().target.Z(), 5.0, 1e-6, "pan up moves target +Z here");
        checkNear(cam.state().target.Y(), 0.0, 1e-6, "pan does not move along the view axis");
    }

    // --- zoomToward keeps the pivot on its eye ray ----------------------------
    {
        CameraController cam;
        const gp_Pnt pivot(120.0, 80.0, 0.0);
        const gp_Pnt eye0 = cam.eyePosition();
        // Direction from eye to pivot before zooming.
        gp_Dir before(pivot.X() - eye0.X(), pivot.Y() - eye0.Y(), pivot.Z() - eye0.Z());
        cam.zoomToward(pivot, 0.5);
        const gp_Pnt eye1 = cam.eyePosition();
        gp_Dir after(pivot.X() - eye1.X(), pivot.Y() - eye1.Y(), pivot.Z() - eye1.Z());
        checkNear(before.Angle(after), 0.0, 1e-6,
                  "zoomToward keeps the pivot on the same eye ray");
        checkNear(cam.state().distance, 350.0, 1e-6, "zoomToward halves the distance");
        check(cam.eyePosition().Distance(pivot) < eye0.Distance(pivot),
              "zooming in moves the eye toward the pivot");
    }

    // --- zoom clamps ----------------------------------------------------------
    {
        CameraController cam;
        cam.zoom(1e-9);
        checkNear(cam.state().distance, 1.0, 1e-9, "zoom clamps at 1mm");
        cam.zoom(1e12);
        checkNear(cam.state().distance, 100000.0, 1e-9, "zoom clamps at 100m");
    }

    // --- frame fits a box -----------------------------------------------------
    {
        CameraController cam;
        Bnd_Box box;
        box.Update(-50.0, -50.0, 0.0, 50.0, 50.0, 100.0);
        cam.frame(box, 45.0);
        checkNear(cam.state().target.X(), 0.0, 1e-6, "frame centres X");
        checkNear(cam.state().target.Z(), 50.0, 1e-6, "frame centres Z");
        // Radius of that box is sqrt(50^2+50^2+50^2) ~ 86.6; distance must at
        // least cover radius/sin(fov/2) with the ~10% margin, and not be silly.
        const double radius = 86.6025;
        const double minimum = radius / std::sin(22.5 * 3.14159265358979323846 / 180.0);
        check(cam.state().distance >= minimum * 1.05 && cam.state().distance <= minimum * 1.3,
              "frame distance covers the bounding sphere with margin");
        // Angles are preserved - framing changes where you look, not from where.
        checkNear(cam.state().azimuthDeg, -45.0, 1e-9, "frame keeps azimuth");
    }

    // --- shortest arc ---------------------------------------------------------
    {
        checkNear(CameraController::shortestArcDelta(350.0, 10.0), 20.0, 1e-9,
                  "350 -> 10 goes +20, not -340");
        checkNear(CameraController::shortestArcDelta(10.0, 350.0), -20.0, 1e-9,
                  "10 -> 350 goes -20");
        checkNear(CameraController::shortestArcDelta(0.0, 180.0), 180.0, 1e-9,
                  "opposite angles resolve to +180");
        checkNear(CameraController::shortestArcDelta(-45.0, -45.0), 0.0, 1e-9,
                  "no movement is zero");
    }
```

- [ ] **Step 2: Verify RED**

Run: `cmake --build --preset windows-headless`
Expected: FAIL to compile — `'setPivot': is not a member of 'CameraController'`.

- [ ] **Step 3: Write the implementation**

In `src/CameraController.h`, add `#include <Bnd_Box.hxx>` won't do — **keep the header light**: forward-declare nothing, just add the methods (Bnd_Box needs the real header; TKMath provides it, include `<Bnd_Box.hxx>` in the header):

```cpp
    void setPivot(const gp_Pnt& pivot);
    void pan(double rightUnits, double upUnits);
    void zoom(double factor);
    void zoomToward(const gp_Pnt& p, double factor);
    void frame(const Bnd_Box& box, double fovyDeg);
    gp_Dir rightVector() const;

    // Signed shortest rotation from one angle to another, in (-180, 180].
    static double shortestArcDelta(double fromDeg, double toDeg);
```

In `src/CameraController.cpp` (add `#include <Bnd_Box.hxx>`):

```cpp
void CameraController::setPivot(const gp_Pnt& pivot)
{
    // Keep the eye fixed and re-derive the spherical state around the new
    // target, so switching pivots never visibly moves the camera.
    const gp_Pnt eye = eyePosition();
    const double dx = eye.X() - pivot.X();
    const double dy = eye.Y() - pivot.Y();
    const double dz = eye.Z() - pivot.Z();
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < kMinDistance) return;   // pivot at the eye: nothing sensible to do

    const double horizontal = std::sqrt(dx * dx + dy * dy);
    myState.target = pivot;
    myState.distance = std::clamp(dist, kMinDistance, kMaxDistance);
    myState.elevationDeg = std::clamp(std::atan2(dz, horizontal) / kDegToRad,
                                      kMinElevation, kMaxElevation);
    if (horizontal > 1e-9) {
        myState.azimuthDeg = std::atan2(-dx, dy) / kDegToRad;
    }
    // horizontal ~ 0 cannot happen through the UI (elevation is clamped), and
    // keeping the old azimuth is the right behaviour if it ever does.
}

gp_Dir CameraController::rightVector() const
{
    // right = view x up; both are unit and perpendicular, so this is unit too.
    return viewDirection().Crossed(upVector());
}

void CameraController::pan(double rightUnits, double upUnits)
{
    const gp_Dir right = rightVector();
    const gp_Dir up = upVector();
    myState.target = gp_Pnt(
        myState.target.X() + right.X() * rightUnits + up.X() * upUnits,
        myState.target.Y() + right.Y() * rightUnits + up.Y() * upUnits,
        myState.target.Z() + right.Z() * rightUnits + up.Z() * upUnits);
}

void CameraController::zoom(double factor)
{
    myState.distance = std::clamp(myState.distance * factor, kMinDistance, kMaxDistance);
}

void CameraController::zoomToward(const gp_Pnt& p, double factor)
{
    // Shrink the whole eye/target frame toward p by the zoom factor: the pivot
    // stays on the same eye ray, which is what keeps it fixed on screen.
    const double clamped =
        std::clamp(myState.distance * factor, kMinDistance, kMaxDistance) / myState.distance;
    myState.target = gp_Pnt(p.X() + (myState.target.X() - p.X()) * clamped,
                            p.Y() + (myState.target.Y() - p.Y()) * clamped,
                            p.Z() + (myState.target.Z() - p.Z()) * clamped);
    myState.distance *= clamped;
}

void CameraController::frame(const Bnd_Box& box, double fovyDeg)
{
    if (box.IsVoid()) return;

    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    myState.target = gp_Pnt((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);

    const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    const double radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
    const double fit = radius / std::sin(0.5 * fovyDeg * kDegToRad);
    myState.distance = std::clamp(fit * 1.1, kMinDistance, kMaxDistance);
}

double CameraController::shortestArcDelta(double fromDeg, double toDeg)
{
    double delta = std::fmod(toDeg - fromDeg, 360.0);
    if (delta > 180.0) delta -= 360.0;
    if (delta <= -180.0) delta += 360.0;
    return delta;
}
```

Note the anonymous-namespace `kDegToRad` from Task 1 is file-scope; these functions use it.

- [ ] **Step 4: Verify GREEN**

Run: `cmake --build --preset windows-headless` then `ctest --preset windows-headless`
Expected: 3/3, with all new checks `[ ok ]`.

- [ ] **Step 5: Commit**

```bash
git add src/CameraController.h src/CameraController.cpp tests/camera_controller.cpp
git commit -m "Add pivot, pan, zoom and framing to the camera controller"
```

---

### Task 3: Drive the OCCT camera from the controller

**Files:**
- Modify: `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `CameraController` (Tasks 1–2).
- Produces: on `OcctViewWidget`:
  - `CameraController& camera();` (test/observation access)
  - `void applyCameraState();` (private: pushes controller → `V3d_View`)
  - `void syncCameraFromView();` (private: decomposes `V3d_View` camera → controller; used after view-cube moves)
  - `static constexpr double kFovyDeg = 45.0;`
  - Standard views (`setViewTop/Front/Right/Axonometric`) and `fitAll` now set controller state (instant in this task; animation arrives in Task 5).

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, insert immediately after the `check(view != nullptr && view->width() > 100, ...)` line (add `#include "CameraController.h"`):

```cpp
    // --- camera startup state -------------------------------------------------
    {
        const CameraState& cam = view->camera().state();
        check(std::fabs(cam.azimuthDeg - (-45.0)) < 1e-6, "startup azimuth is -45");
        check(std::fabs(cam.elevationDeg - 30.0) < 1e-6, "startup elevation is +30");
        check(std::fabs(cam.distance - 700.0) < 1e-6, "startup distance is 700mm");
    }
```

and insert just before the final `std::printf`:

```cpp
    // --- standard views set turntable state -----------------------------------
    {
        trigger(window, QStringLiteral("Front"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg) < 1e-3,
              "Front is azimuth 0, elevation 0");

        trigger(window, QStringLiteral("Top"));
        settle(400);
        check(std::fabs(view->camera().state().elevationDeg - 89.0) < 1e-3,
              "Top is elevation +89 inside the clamp");

        trigger(window, QStringLiteral("Right"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-90.0)) < 1e-3,
              "Right is azimuth -90");

        trigger(window, QStringLiteral("Axonometric"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg - 30.0) < 1e-3,
              "Axonometric returns to the startup angles");
    }
```

- [ ] **Step 2: Verify RED**

Kill processes, `cmake --build --preset windows`.
Expected: FAIL to compile — `'camera': is not a member of 'OcctViewWidget'`.

- [ ] **Step 3: Implement**

In `src/OcctViewWidget.h`: add `#include "CameraController.h"`, and:

```cpp
    static constexpr double kFovyDeg = 45.0;

    CameraController& camera() { return myCamera; }
```

private members/methods:

```cpp
    CameraController myCamera;
    void applyCameraState();
    void syncCameraFromView();
```

In `src/OcctViewWidget.cpp`:

1. In `initializeViewer()`, after the view is created and **before** `myView->MustBeResized()`, replace `setViewAxonometric();` with:

```cpp
    // Perspective projection: the turntable model is distance-based, and OCCT's
    // default orthographic camera zooms by scale, which would make
    // zoom-toward-cursor meaningless.
    myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    myView->Camera()->SetFOVy(kFovyDeg);
    applyCameraState();
```

(add `#include <Graphic3d_Camera.hxx>`)

2. Add the two methods:

```cpp
void OcctViewWidget::applyCameraState()
{
    if (myView.IsNull()) return;

    const Handle(Graphic3d_Camera) cam = myView->Camera();
    const gp_Pnt eye = myCamera.eyePosition();
    const gp_Pnt& at = myCamera.state().target;
    const gp_Dir up = myCamera.upVector();
    cam->SetEye(eye);
    cam->SetCenter(at);
    cam->SetUp(up);
    myView->Redraw();
}

void OcctViewWidget::syncCameraFromView()
{
    if (myView.IsNull()) return;

    // Decompose whatever the view's camera is (the view cube animates it
    // behind our back) into turntable state. setPivot does exactly this
    // derivation when the eye is fixed, so reuse it: set the eye-preserving
    // state from the OCCT camera's center.
    const Handle(Graphic3d_Camera) cam = myView->Camera();
    CameraState s = myCamera.state();
    s.target = cam->Center();
    myCamera.setState(s);
    // Recompute angles/distance from the real eye by pivoting about the center.
    CameraState derived = myCamera.state();
    const gp_Pnt eye = cam->Eye();
    const double dx = eye.X() - derived.target.X();
    const double dy = eye.Y() - derived.target.Y();
    const double dz = eye.Z() - derived.target.Z();
    const double horizontal = std::sqrt(dx * dx + dy * dy);
    derived.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    derived.elevationDeg = std::atan2(dz, horizontal) * 180.0 / 3.14159265358979323846;
    if (horizontal > 1e-9) {
        derived.azimuthDeg = std::atan2(-dx, dy) * 180.0 / 3.14159265358979323846;
    }
    myCamera.setState(derived);
    // Re-assert our up vector: cube-driven views may leave a rolled camera.
    applyCameraState();
}
```

3. Rewrite the standard views and `fitAll` to set controller state (instant for now — Task 5 makes them animate through one shared entry point):

```cpp
void OcctViewWidget::setViewAxonometric()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = -45.0; s.elevationDeg = 30.0;
    myCamera.setState(s);
    applyCameraState();
}

void OcctViewWidget::setViewTop()
{
    CameraState s = myCamera.state();
    s.elevationDeg = 89.0;   // inside the clamp: a true 90 makes azimuth degenerate
    myCamera.setState(s);
    applyCameraState();
}

void OcctViewWidget::setViewFront()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = 0.0; s.elevationDeg = 0.0;
    myCamera.setState(s);
    applyCameraState();
}

void OcctViewWidget::setViewRight()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = -90.0; s.elevationDeg = 0.0;
    myCamera.setState(s);
    applyCameraState();
}

void OcctViewWidget::fitAll()
{
    if (myView.IsNull()) return;

    // Frame everything we display ourselves (the grid and view cube are
    // presentation furniture, not content).
    Bnd_Box box;
    for (const auto& entry : mySolids) {
        Bnd_Box b;
        BRepBndLib::Add(entry.second->Shape(), b);
        box.Add(b);
    }
    if (box.IsVoid()) box.Update(-250.0, -250.0, 0.0, 250.0, 250.0, 10.0);
    myCamera.frame(box, kFovyDeg);
    applyCameraState();
}
```

(add `#include <BRepBndLib.hxx>` and `#include <Bnd_Box.hxx>`; `furnify_app` already links `TKTopAlgo`/`TKBRep` transitively via `furnify_geometry` — if `BRepBndLib` fails to link, it lives in `TKBRep`, already present.)

4. In the LMB `mouseReleaseEvent` path, after `SelectDetected(...)` and before `emit selectionChanged()`, add:

```cpp
    // A click on the view cube animates the OCCT camera directly; fold whatever
    // it did back into the controller so the next orbit starts from reality.
    syncCameraFromView();
```

5. **Delete nothing else yet** — `mouseMoveEvent`'s `Rotation/Pan` paths still reference the old scheme; Task 4 replaces them. To keep this task compiling, leave the old drag handlers as they are (they will fight the controller only *during* a drag, which Task 4 removes).

- [ ] **Step 4: Verify**

Kill processes, build, run `.\build\RelWithDebInfo\gui_smoke.exe .`.
Expected: `PASS (0 failures)`. Count grows by 8 (3 startup + 5 view checks — the run's own printout is authoritative). **If pre-existing click-position checks fail:** the perspective switch changes where solids land on screen; retune the affected `clickAt` fractions the way Task 8 of the shell plan did, and say so in the report. Do not weaken assertions.

- [ ] **Step 5: Commit**

```bash
git add src/OcctViewWidget.h src/OcctViewWidget.cpp tests/gui_smoke.cpp
git commit -m "Drive the OCCT camera from the turntable controller"
```

---

### Task 4: Input remap — MMB orbit with cursor pivot, Shift+MMB pan, wheel zoomToward

**Files:**
- Modify: `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`, `src/MainWindow.cpp` (status hint)
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: private helper `bool pickWorldPoint(int px, int py, gp_Pnt& out) const` (model hit → detected point, else ground plane, else false). Test helper in gui_smoke: `dragMMB(view, from, to, modifiers)`.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add next to `clickAt` in the anonymous namespace:

```cpp
// Middle-button drag delivered as press/move/release, for camera tests.
void dragMMB(QWidget* target, const QPointF& from, const QPointF& to,
             Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, target->mapToGlobal(from),
                      Qt::MiddleButton, Qt::MiddleButton, mods);
    QCoreApplication::sendEvent(target, &press);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        QMouseEvent move(QEvent::MouseMove, p, target->mapToGlobal(p),
                         Qt::NoButton, Qt::MiddleButton, mods);
        QCoreApplication::sendEvent(target, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, to, target->mapToGlobal(to),
                        Qt::MiddleButton, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &release);
    settle(120);
}
```

Insert immediately after the camera-startup-state block from Task 3:

```cpp
    // --- turntable input ------------------------------------------------------
    {
        const double az0 = view->camera().state().azimuthDeg;
        const gp_Dir up0 = view->camera().upVector();
        dragMMB(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                      QPointF(view->width() * 0.5 + 100.0, view->height() * 0.5));
        check(std::fabs(view->camera().state().azimuthDeg - az0) > 5.0,
              "a horizontal MMB drag orbits azimuth");
        check(view->camera().upVector().Z() > 0.0 && up0.Z() > 0.0,
              "orbiting never rolls: up keeps its +Z component");

        const gp_Pnt target0 = view->camera().state().target;
        dragMMB(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                      QPointF(view->width() * 0.5 + 80.0, view->height() * 0.5 + 40.0),
                Qt::ShiftModifier);
        check(view->camera().state().target.Distance(target0) > 1.0,
              "Shift+MMB pans the target");

        // Elevation clamp holds through input: a huge vertical drag stops at 88.
        dragMMB(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                      QPointF(view->width() * 0.5, view->height() * 0.5 + 2000.0));
        check(view->camera().state().elevationDeg >= -88.0 - 1e-6 &&
              view->camera().state().elevationDeg <= 88.0 + 1e-6,
              "elevation stays inside the clamp under wild input");
    }
```

- [ ] **Step 2: Verify RED**

Build and run gui_smoke.
Expected: FAIL — the MMB drag still runs the old `Pan` path, so "a horizontal MMB drag orbits azimuth" fails (azimuth unchanged).

- [ ] **Step 3: Implement**

In `src/OcctViewWidget.h`, replace the `myRotating`/`myPanning` members with:

```cpp
    bool myOrbiting = false;
    bool myPanningDrag = false;
```

and declare `bool pickWorldPoint(int px, int py, gp_Pnt& out) const;` privately.

In `src/OcctViewWidget.cpp`:

```cpp
bool OcctViewWidget::pickWorldPoint(int px, int py, gp_Pnt& out) const
{
    // Prefer a real hit on the model: MoveTo + detection gives the picked point
    // on the surface under the cursor.
    if (!myContext.IsNull() && !myView.IsNull()) {
        myContext->MoveTo(px, py, myView, Standard_False);
        if (myContext->HasDetected()) {
            const Handle(StdSelect_ViewerSelector3d) selector = myContext->MainSelector();
            if (selector->NbPicked() > 0) {
                out = selector->PickedPoint(1);
                return true;
            }
        }
    }
    // Otherwise the ground plane, reusing the sketch unprojection.
    if (myView.IsNull()) return false;
    Standard_Real x, y, z, vx, vy, vz;
    myView->ConvertWithProj(px, py, x, y, z, vx, vy, vz);
    const gp_Lin ray(gp_Pnt(x, y, z), gp_Dir(vx, vy, vz));
    const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
    return SketchController::intersectRayWithPlane(ray, ground, out);
}
```

(add `#include <StdSelect_ViewerSelector3d.hxx>`)

`mousePressEvent` — replace the RMB/MMB branches:

```cpp
    if (event->button() == Qt::MiddleButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            myPanningDrag = true;
        } else {
            myOrbiting = true;
            // Orbit around what is under the cursor; fall back to the current
            // target when the pick finds nothing (looking at empty sky).
            gp_Pnt pivot;
            if (pickWorldPoint(myLastPos.x(), myLastPos.y(), pivot)) {
                myCamera.setPivot(pivot);
                applyCameraState();
            }
        }
    }
    // Right button: deliberately unbound - reserved for a context menu.
```

`mouseReleaseEvent` — replace the first two lines with:

```cpp
    if (event->button() == Qt::MiddleButton) { myOrbiting = false; myPanningDrag = false; }
```

`mouseMoveEvent` — replace the `myRotating` / `myPanning` branches:

```cpp
    if (myOrbiting) {
        const QPoint delta = pos - myLastPos;
        // Dragging right swings the scene right: azimuth decreases; dragging up
        // raises the eye. 0.4 deg/px and 0.3 deg/px feel close to Fusion.
        myCamera.orbit(-delta.x() * 0.4, delta.y() * 0.3);
        applyCameraState();
    } else if (myPanningDrag) {
        const QPoint delta = pos - myLastPos;
        // World units per pixel at target depth, for a perspective camera.
        const double worldPerPixel =
            2.0 * myCamera.state().distance *
            std::tan(0.5 * kFovyDeg * 3.14159265358979323846 / 180.0) /
            std::max(1, height());
        myCamera.pan(-delta.x() * worldPerPixel, delta.y() * worldPerPixel);
        applyCameraState();
    } else if (mySketchMode) {
```

`wheelEvent` — replace the body after the delta check:

```cpp
    const QPoint pos = event->position().toPoint();
    gp_Pnt pivot;
    const bool havePivot = pickWorldPoint(pos.x(), pos.y(), pivot);

    // One wheel notch (delta 120) zooms ~12%; exponential so every notch feels
    // the same at any scale.
    const double factor = std::exp(-double(delta) / 120.0 * 0.12);
    if (havePivot) myCamera.zoomToward(pivot, factor);
    else           myCamera.zoom(factor);
    applyCameraState();
```

In `src/MainWindow.cpp`, update the startup hint:

```cpp
    statusBar()->showMessage(tr("MMB drag orbits, Shift+MMB pans, wheel zooms."));
```

- [ ] **Step 4: Verify**

Kill processes, build, run gui_smoke. Expected: `PASS (0 failures)` including the four new checks. Retune click fractions only if a pre-existing selection check broke, and report it.

- [ ] **Step 5: Commit**

```bash
git add src/OcctViewWidget.h src/OcctViewWidget.cpp src/MainWindow.cpp tests/gui_smoke.cpp
git commit -m "Remap camera input: MMB turntable orbit with cursor pivot"
```

---

### Task 5: Animated transitions and double-click framing

**Files:**
- Modify: `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces:
  - `void animateTo(const CameraState& goal);` (public — MainWindow/cube/testing)
  - `void setAnimationsEnabled(bool enabled);` / `bool animationsEnabled() const;` — when disabled, `animateTo` applies instantly. **gui_smoke disables animations right after constructing the window** so every pre-existing check keeps its timing; one dedicated block re-enables them to test the animation itself.
  - `mouseDoubleClickEvent` frames the solid under the cursor.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, immediately after `window.show(); settle(900);` add:

```cpp
    window.view()->setAnimationsEnabled(false);   // deterministic camera for the suite
```

Insert before the final `std::printf`:

```cpp
    // --- animated transitions -------------------------------------------------
    {
        view->setAnimationsEnabled(true);
        CameraState goal = view->camera().state();
        goal.azimuthDeg += 90.0;
        const double azBefore = view->camera().state().azimuthDeg;
        view->animateTo(goal);
        // Mid-flight (a few event-loop turns in), the camera is between the
        // endpoints - that is what distinguishes animation from teleporting.
        settle(80);
        const double azMid = view->camera().state().azimuthDeg;
        check(std::fabs(azMid - azBefore) > 1.0 &&
              std::fabs(azMid - goal.azimuthDeg) > 1.0,
              "animateTo passes through intermediate states");
        settle(500);
        check(std::fabs(view->camera().state().azimuthDeg - goal.azimuthDeg) < 1e-3,
              "animateTo settles exactly on the goal");
        view->setAnimationsEnabled(false);
        check(true, "animations re-disabled for the rest of the suite");
    }
```

- [ ] **Step 2: Verify RED**

Build. Expected: FAIL to compile — `'setAnimationsEnabled': is not a member`.

- [ ] **Step 3: Implement**

`src/OcctViewWidget.h` — add `#include <QVariantAnimation>` is not needed in the header; use a pointer:

```cpp
    void animateTo(const CameraState& goal);
    void setAnimationsEnabled(bool enabled) { myAnimationsEnabled = enabled; }
    bool animationsEnabled() const { return myAnimationsEnabled; }
```

private:

```cpp
    class QVariantAnimation* myCameraAnimation = nullptr;
    bool myAnimationsEnabled = true;
    void stopCameraAnimation();
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
```

`src/OcctViewWidget.cpp` (add `#include <QVariantAnimation>` and `#include <QEasingCurve>`):

```cpp
void OcctViewWidget::stopCameraAnimation()
{
    if (myCameraAnimation) {
        myCameraAnimation->stop();   // leaves the camera wherever it got to
        myCameraAnimation->deleteLater();
        myCameraAnimation = nullptr;
    }
}

void OcctViewWidget::animateTo(const CameraState& goal)
{
    stopCameraAnimation();
    if (!myAnimationsEnabled) {
        myCamera.setState(goal);
        applyCameraState();
        return;
    }

    const CameraState from = myCamera.state();
    // Interpolate azimuth along the shortest arc so 350 -> 10 turns 20 degrees.
    const double azDelta = CameraController::shortestArcDelta(from.azimuthDeg, goal.azimuthDeg);

    auto* animation = new QVariantAnimation(this);
    animation->setDuration(250);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, from, goal, azDelta](const QVariant& value) {
                const double t = value.toDouble();
                CameraState s;
                s.azimuthDeg = from.azimuthDeg + azDelta * t;
                s.elevationDeg = from.elevationDeg + (goal.elevationDeg - from.elevationDeg) * t;
                s.distance = from.distance + (goal.distance - from.distance) * t;
                s.target = gp_Pnt(from.target.X() + (goal.target.X() - from.target.X()) * t,
                                  from.target.Y() + (goal.target.Y() - from.target.Y()) * t,
                                  from.target.Z() + (goal.target.Z() - from.target.Z()) * t);
                myCamera.setState(s);
                applyCameraState();
            });
    connect(animation, &QVariantAnimation::finished, this, [this, goal] {
        myCamera.setState(goal);
        applyCameraState();
        myCameraAnimation = nullptr;
    });
    myCameraAnimation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
```

Rewrite the four standard views and `fitAll` to route through `animateTo` — each builds the goal `CameraState` exactly as in Task 3 but calls `animateTo(s)` instead of `setState + applyCameraState`. (`fitAll` computes the framed state on a scratch `CameraController` copy: `CameraController scratch = myCamera; scratch.frame(box, kFovyDeg); animateTo(scratch.state());`)

A drag interrupt: at the top of `mousePressEvent`, call `stopCameraAnimation();`.

Double-click framing:

```cpp
void OcctViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || mySketchMode || myContext.IsNull()) return;

    const QPoint pos = event->position().toPoint();
    myContext->MoveTo(pos.x(), pos.y(), myView, Standard_False);
    if (!myContext->HasDetected()) return;

    const Handle(AIS_InteractiveObject) hit = myContext->DetectedInteractive();
    for (const auto& entry : mySolids) {
        if (entry.second.get() != hit.get()) continue;
        Bnd_Box box;
        BRepBndLib::Add(entry.second->Shape(), box);
        CameraController scratch = myCamera;
        scratch.frame(box, kFovyDeg);
        animateTo(scratch.state());
        return;
    }
}
```

- [ ] **Step 4: Verify**

Kill, build, run gui_smoke. Expected: `PASS (0 failures)`; the animation block's three checks pass and every pre-existing check is unaffected (animations disabled for them).

- [ ] **Step 5: Commit**

```bash
git add src/OcctViewWidget.h src/OcctViewWidget.cpp tests/gui_smoke.cpp
git commit -m "Animate view transitions and double-click framing"
```

---

### Task 6: Adaptive ground grid

**Files:**
- Create: `src/GridRenderer.h`, `src/GridRenderer.cpp`
- Modify: `CMakeLists.txt` (add to `furnify_app`), `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`, `src/ui/Theme.h`, `src/ui/Theme.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `Theme`, the camera state (distance drives subdivision), `myContext`.
- Produces:

```cpp
// GridRenderer.h
class GridRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);   // displays the grid
    void update(double cameraDistance, const gp_Pnt& cameraTarget); // rebuild if needed
    // Chosen minor step for a camera distance: 1, 10 or 100 (mm). Pure, static,
    // unit-testable in gui_smoke without a camera.
    static double minorStepFor(double cameraDistance);
};
```

Theme gains `QColor gridMinor(); QColor gridMajor(); QColor axisX(); QColor axisY();`.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp` (add `#include "GridRenderer.h"`), insert before the final `std::printf`:

```cpp
    // --- grid subdivision policy ----------------------------------------------
    {
        check(GridRenderer::minorStepFor(700.0) == 10.0,
              "default working distance uses the 10mm grid");
        check(GridRenderer::minorStepFor(50.0) == 1.0,
              "zoomed close in, the 1mm grid appears");
        check(GridRenderer::minorStepFor(8000.0) == 100.0,
              "zoomed far out, the 100mm grid takes over");
        check(GridRenderer::minorStepFor(0.0) >= 1.0 &&
              GridRenderer::minorStepFor(1e9) <= 100.0,
              "extreme distances stay inside the defined levels");
    }
```

- [ ] **Step 2: Verify RED**

Build. Expected: FAIL to compile — cannot open include file `GridRenderer.h`.

- [ ] **Step 3: Implement Theme colours**

`src/ui/Theme.h` declarations + `src/ui/Theme.cpp` definitions:

```cpp
QColor gridMinor() { return QColor("#3a3a40"); }
QColor gridMajor() { return QColor("#4a4a52"); }
QColor axisX()     { return QColor("#7a4a4a"); }   // muted red
QColor axisY()     { return QColor("#4a7a4a"); }   // muted green
```

- [ ] **Step 4: Implement GridRenderer**

Create `src/GridRenderer.h`:

```cpp
#pragma once
// The adaptive ground grid. Replaces OCCT's finite ActivateGrid patch: three
// concentric bands of line segments on Z=0 whose colours blend toward the
// viewport background with distance, so there is never a visible edge.
// App-layer only - it builds OCCT presentation objects.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

class GridRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);
    void update(double cameraDistance, const gp_Pnt& cameraTarget);

    static double minorStepFor(double cameraDistance);

private:
    Handle(AIS_InteractiveContext) myContext;
    Handle(AIS_InteractiveObject) myGrid;
    double myBuiltStep = 0.0;
    gp_Pnt myBuiltCenter{0.0, 0.0, 0.0};
    double myBuiltExtent = 0.0;

    void rebuild(double minorStep, const gp_Pnt& center, double extent);
};
```

Create `src/GridRenderer.cpp`:

```cpp
#include "GridRenderer.h"

#include "Theme.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Quantity_Color.hxx>

#include <algorithm>
#include <cmath>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

QColor lerp(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// A minimal interactive object whose whole presentation is provided by the
// renderer through a callback-free rebuild: GridRenderer computes the segment
// arrays and this object draws them.
class GridObject : public AIS_InteractiveObject {
public:
    struct Band {
        Handle(Graphic3d_ArrayOfSegments) segments;
        Quantity_Color colour;
        double width = 1.0;
    };
    std::vector<Band> bands;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        for (const Band& band : bands) {
            if (band.segments.IsNull()) continue;
            Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectLine3d) aspect =
                new Graphic3d_AspectLine3d(band.colour, Aspect_TOL_SOLID, band.width);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(band.segments);
        }
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&,
                          const Standard_Integer) override
    {
        // Never pickable.
    }
};

}  // namespace

double GridRenderer::minorStepFor(double cameraDistance)
{
    if (cameraDistance < 120.0) return 1.0;
    if (cameraDistance < 2500.0) return 10.0;
    return 100.0;
}

void GridRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void GridRenderer::update(double cameraDistance, const gp_Pnt& cameraTarget)
{
    if (myContext.IsNull()) return;

    const double step = minorStepFor(cameraDistance);
    // Extent: comfortably beyond what a camera at this distance can see of the
    // ground, snapped to the major step so lines do not crawl on rebuild.
    const double major = step * 10.0;
    double extent = std::clamp(cameraDistance * 6.0, 500.0, 200000.0);
    extent = std::ceil(extent / major) * major;
    const gp_Pnt center(std::round(cameraTarget.X() / major) * major,
                        std::round(cameraTarget.Y() / major) * major, 0.0);

    // Rebuild only when something visible changes: level, or the camera left
    // the middle half of the built area, or extent changed by >2x.
    const bool sameLevel = (step == myBuiltStep);
    const bool centered = center.Distance(myBuiltCenter) < myBuiltExtent * 0.25;
    const bool sized = myBuiltExtent > 0.0 &&
                       extent < myBuiltExtent * 2.0 && extent > myBuiltExtent * 0.5;
    if (sameLevel && centered && sized) return;

    rebuild(step, center, extent);
    myBuiltStep = step;
    myBuiltCenter = center;
    myBuiltExtent = extent;
}

void GridRenderer::rebuild(double minorStep, const gp_Pnt& center, double extent)
{
    const double major = minorStep * 10.0;
    const QColor background = Theme::viewport();

    // Three concentric bands; outer bands blend toward the background so the
    // grid has no visible boundary.
    struct BandSpec { double inner, outer, fade; };
    const BandSpec specs[3] = {{0.0, 0.5, 0.0}, {0.5, 0.75, 0.55}, {0.75, 1.0, 0.85}};

    Handle(GridObject) grid = new GridObject();

    auto addLines = [&](bool isMajor, const BandSpec& spec) {
        const double step = isMajor ? major : minorStep;
        const QColor base = isMajor ? Theme::gridMajor() : Theme::gridMinor();
        const QColor colour = lerp(base, background, spec.fade);

        // Collect segments for lines whose |coordinate| lies in the band ring.
        std::vector<gp_Pnt> points;
        const double lo = extent * spec.inner, hi = extent * spec.outer;
        for (double v = -hi; v <= hi + step * 0.5; v += step) {
            if (!isMajor && std::fmod(std::fabs(v) + step * 0.25, major) < step * 0.5)
                continue;   // skip positions covered by a major line
            const double a = std::fabs(v);
            // Lines fully inside an inner band are drawn by that band already;
            // draw the full length in the innermost band and only the ring
            // extension in outer bands.
            if (spec.inner == 0.0) {
                if (a > hi) continue;
                points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
            } else {
                if (a > hi) continue;
                // Ring: two segments per line (the parts outside the inner square),
                // plus full-length lines whose offset itself is in the ring.
                if (a >= lo) {
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                    points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
                } else {
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - lo, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + lo, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                    points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() - lo, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + lo, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
                }
            }
        }
        if (points.empty()) return;

        Handle(Graphic3d_ArrayOfSegments) array =
            new Graphic3d_ArrayOfSegments(static_cast<Standard_Integer>(points.size()));
        for (const gp_Pnt& p : points) array->AddVertex(p);

        GridObject::Band band;
        band.segments = array;
        band.colour = toOcct(colour);
        band.width = isMajor ? 1.4 : 1.0;
        grid->bands.push_back(band);
    };

    for (const BandSpec& spec : specs) {
        addLines(false, spec);
        addLines(true, spec);
    }

    // Axis lines through the origin, if the origin is inside the built area.
    if (std::fabs(center.X()) < extent && std::fabs(center.Y()) < extent) {
        auto axis = [&](const QColor& colour, bool isX) {
            Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(2);
            if (isX) {
                array->AddVertex(gp_Pnt(center.X() - extent, 0.0, 0.0));
                array->AddVertex(gp_Pnt(center.X() + extent, 0.0, 0.0));
            } else {
                array->AddVertex(gp_Pnt(0.0, center.Y() - extent, 0.0));
                array->AddVertex(gp_Pnt(0.0, center.Y() + extent, 0.0));
            }
            GridObject::Band band;
            band.segments = array;
            band.colour = toOcct(colour);
            band.width = 1.8;
            grid->bands.push_back(band);
        };
        axis(Theme::axisX(), true);
        axis(Theme::axisY(), false);
    }

    if (!myGrid.IsNull()) myContext->Remove(myGrid, Standard_False);
    myGrid = grid;
    myContext->Display(myGrid, 0, -1, Standard_False);   // mode -1: not selectable
    myContext->UpdateCurrentViewer();
}
```

- [ ] **Step 5: Wire into the widget**

`src/OcctViewWidget.h`: `#include "GridRenderer.h"`, private member `GridRenderer myGridRenderer;`.

`src/OcctViewWidget.cpp`:
- In `initializeViewer()`, **delete** the three `ActivateGrid`/`SetRectangularGridValues`/`SetRectangularGridGraphicValues` lines and add, right after the highlight styles:

```cpp
    myGridRenderer.attach(myContext);
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target);
```

- At the end of `applyCameraState()`, before `Redraw()`:

```cpp
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target);
```

(The renderer's own no-op check makes this cheap during drags; the rebuild condition fires only when level/centre/extent change materially — this satisfies the spec's "reuse while dragging" requirement without a separate settle timer. If orbiting visibly stutters at rebuild moments, apply the spec's pre-agreed fallback: fixed 5m extent, subdivision still adaptive.)

- [ ] **Step 6: Verify**

Kill, build headless + app, run both suites. Expected: headless 3/3; gui_smoke `PASS (0 failures)` with the 4 new grid-policy checks. Then run `furnifyme.exe`, capture a still (window grab, no input injection), and confirm by eye: no visible grid edge from the startup view, axis tint through the origin, grid density readable. Save the screenshot path in the report.

- [ ] **Step 7: Commit**

```bash
git add src/GridRenderer.h src/GridRenderer.cpp src/OcctViewWidget.h src/OcctViewWidget.cpp src/ui/Theme.h src/ui/Theme.cpp CMakeLists.txt tests/gui_smoke.cpp
git commit -m "Replace the finite OCCT grid with an adaptive fading ground grid"
```

---

### Task 7: View-cube smoothing, docs, visual pass

**Files:**
- Modify: `src/OcctViewWidget.cpp` (cube animation duration), `CLAUDE.md`, `docs/superpowers/specs/2026-08-28-camera-grid-design.md` (status line)

- [ ] **Step 1: Give the view cube the same transition duration**

In `setViewCubeVisible`, after the cube is constructed:

```cpp
    cube->SetDuration(0.25);   // matches animateTo, so cube clicks feel the same
```

- [ ] **Step 2: Update CLAUDE.md**

- Architecture table: add `| `CameraController.{h,cpp}` | turntable camera maths, Qt-free, headless-tested |` and `| `GridRenderer.{h,cpp}` | adaptive fading ground grid (app layer) |`.
- In the `OcctViewWidget` bridge section, replace the event-wiring line's camera parts with: `MMB drag→turntable orbit around the picked point, Shift+MMB→pan, wheel→zoomToward cursor; RMB unbound (reserved for a context menu); camera state lives in CameraController and is pushed via SetEye/SetCenter/SetUp; the projection is perspective (FOVy 45°).`
- Note that `ActivateGrid` is gone and the grid is `GridRenderer`.
- Update the keyboard/verification cheat-sheet line (`0`–`3` views now animate; gui_smoke disables animations at startup for determinism).

- [ ] **Step 3: Mark the spec**

Change `Status:` to `Status: implemented 2026-08-28`.

- [ ] **Step 4: Full verification + visual pass**

Kill processes; build both presets; `ctest --preset windows-headless` (3/3) and gui_smoke (`PASS (0 failures)`). Launch the app, grab stills at: startup, after `1` (Top), zoomed far in, zoomed far out. Confirm: level horizon at startup (−45/+30 view), no grid edge, density readable at both zoom extremes. Report what each still shows.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-08-28-camera-grid-design.md src/OcctViewWidget.cpp
git commit -m "Smooth the view cube and document the camera architecture"
```
