# Camera controller and adaptive grid — design

Date: 2026-08-28
Status: implemented 2026-08-28

## Goal

Make the viewport feel deliberate instead of merely functional: a turntable camera that
never rolls the horizon, orbiting around the point under the cursor, smooth animated view
transitions, a considered startup view, an adaptive ground grid that reads as infinite, and
the de-facto-standard CAD mouse mapping.

## Diagnosis of the current behaviour (all confirmed in code)

- Startup view is `SetProj(V3d_XposYnegZpos)` — an arbitrary corner with no pinned
  up-vector, which is the "weird angle" the user named.
- The grid is a finite 500×500 patch (`SetRectangularGridGraphicValues`), a hard-edged
  diamond floating in space, with fixed subdivision at every zoom level.
- Orbit is OCCT's free `V3d_View::Rotation()` — it rolls the model, tilts the horizon, and
  pivots around the view centre rather than anything meaningful.
- Every view change (standard views, view cube, Fit All) teleports.
- Orbit is on RMB-drag, permanently blocking a future context menu; pan is MMB.

## Decisions (settled with the user)

1. **Turntable rotation, Z always up.** Elevation clamped to ±88°.
2. **Orbit pivots around the cursor point** — model hit if present, else the ground plane,
   else the current target.
3. **MMB orbit, Shift+MMB pan, wheel zoom-at-cursor.** RMB is freed and left unbound,
   reserved for a future context menu.
4. **Adaptive infinite-looking grid** replacing OCCT's built-in grid.

## Architecture

### CameraController — new, Qt-free, in `furnify_geometry`

`src/CameraController.{h,cpp}`. Owns the turntable state and does all the maths, so it is
headless-testable like the rest of the geometry layer. No Qt, no OCCT visualization
toolkits; it may use `gp_*` types (TKMath) only.

State: `target` (gp_Pnt), `azimuthDeg`, `elevationDeg`, `distance`.

| Method | Behaviour |
|---|---|
| `orbit(dxDeg, dyDeg)` | azimuth += dx; elevation clamped to [−88, +88] |
| `setPivot(p)` | re-anchors the orbit around `p`, preserving the eye position (recomputes target/azimuth/elevation/distance so the camera does not jump) |
| `pan(rightUnits, upUnits)` | moves `target` along the view-plane right/up vectors |
| `zoom(factor)` | `distance *= factor`, clamped to [1mm, 100m] |
| `zoomToward(p, factor)` | zooms while moving `target` toward `p` proportionally, so the cursor point stays put on screen |
| `eyePosition()`, `upVector()` | derived outputs; up is always +Z-derived (never rolls) |
| `frame(bbox, aspect)` | computes target+distance to fit a bounding box with ~10% margin |

Angle convention: azimuth 0 looks along −Y (from +Y toward origin); positive azimuth
rotates counterclockwise seen from above; elevation 0 is horizontal, +90 looks straight
down. Distance is eye-to-target.

**Startup state: azimuth −45°, elevation +30°, target (0,0,0), distance 700mm.** Replaces `V3d_XposYnegZpos`. Standard views map
to: Top (el +89 — inside the clamp, azimuth kept), Front (az 0, el 0), Right (az −90,
el 0), Axonometric (az −45, el +30). Top deliberately stays inside the clamp rather than
using a true straight-down view: at exactly ±90° the azimuth becomes degenerate (gimbal
point) and the first orbit drag after it would snap. +89° is visually indistinguishable
from straight down and keeps the maths regular.

### OcctViewWidget — becomes a thin input translator

- Each frame that the controller changes, the widget pushes eye/target/up into
  `myView->Camera()` (`SetEye/SetCenter/SetUp`) and redraws. `V3d_View::Rotation`,
  `StartRotation`, `Pan`, `StartZoomAtPoint/ZoomAtPoint` are no longer used for camera
  motion.
- **Orbit start (MMB press):** pick the pivot — `myContext->MoveTo` + detected entity point
  if the cursor is over geometry, else the existing ray/ground-plane intersection
  (`SketchController::intersectRayWithPlane` against Z=0), else keep the current target.
  Call `setPivot`, then feed `orbit()` per move with degrees proportional to pixel delta
  (~0.4°/px horizontal, ~0.3°/px vertical).
- **Pan (Shift+MMB):** convert pixel delta to world units at target depth, call `pan()`.
- **Wheel:** ray-hit the cursor point (model or ground), `zoomToward(hit, exp(−delta·k))`.
- Sketch mode, selection, hover: unchanged. LMB behaviour untouched.

### Animated transitions

One `QVariantAnimation` (in the widget — Qt stays out of the controller) interpolating a
`CameraController` state struct over ~250ms with `OutCubic` easing. Used by: standard
views, the view cube (its clicks are intercepted by syncing OCCT's cube-driven camera into
the controller and animating), Fit All, and double-click-to-frame-solid (new: double LMB
on a solid frames it). Live drags are never animated. Starting a drag mid-animation stops
the animation at its current state.

Azimuth interpolates along the shortest arc (e.g. 350°→10° goes +20°, not −340°).

### GridRenderer — new, in the app layer

`src/GridRenderer.{h,cpp}` (app target, not furnify_geometry — it builds OCCT
presentation objects). Replaces `ActivateGrid` entirely.

- Draws minor lines (10mm) and major lines (100mm) as line segments in a non-pickable
  presentation (selection mode −1, like the sketch preview), Z=0 plane.
- **Adaptive extent:** sized from the camera's distance-to-ground and footprint so edges
  stay outside the view. Rebuilt when the camera settles (on drag end / animation end /
  zoom debounce ~100ms), reused unchanged during motion — never rebuilt per frame.
- **Adaptive subdivision:** one level per ~10× zoom. Levels: 1mm/10mm, 10mm/100mm (default),
  100mm/1m. Chosen so on-screen minor spacing stays in roughly 8–80px. Snapping is not
  coupled to this — it stays 10mm as configured.
- **Distance fade:** minor lines fade to transparent beyond ~60% of the drawn extent,
  major beyond ~85%, so there is no visible boundary. Implemented by splitting the grid
  into a few concentric bands with decreasing line transparency (OCCT per-presentation
  transparency), not per-vertex alpha.
- **Axis tint:** the X line through the origin drawn in a muted red, Y in muted green
  (`Theme` supplies both), slightly wider than major lines.
- Colours from `Theme`: minor near `#3a3a40`, major near `#4a4a52`, both to be finalized
  against the `#45454b` viewport in the visual pass.

**Fallback, pre-agreed with the user:** if rebuilding at camera-settle visibly shimmers or
stutters, drop adaptivity of extent and use a fixed 5m extent with the same fade and
subdivision behaviour. This reads as infinite at furniture scale.

### Input mapping summary (after)

| Input | Action |
|---|---|
| MMB drag | orbit (turntable, cursor pivot) |
| Shift+MMB drag | pan |
| Wheel | zoom at cursor |
| RMB | unbound (reserved: context menu) |
| Double LMB on solid | animated frame-to-solid |
| LMB | pick / sketch point (unchanged) |

Status-bar hint text and CLAUDE.md are updated to match.

## Out of scope

Shader/GLSL infinite grid; perspective/orthographic toggle; camera bookmarks; touch and
trackpad gestures; inertia after drag release; changing snapping behaviour.

## Testing

- **Headless (new):** CameraController — orbit accumulation and clamping at ±88°; up
  vector always +Z-derived (no roll) at extreme angles; `setPivot` preserves the eye
  position; `zoomToward` keeps the pivot point on its eye-ray (invariant that makes
  zoom-at-cursor work); distance clamps; `frame()` fits a known bbox; shortest-arc
  azimuth interpolation helper.
- **gui_smoke (extended):** after startup the camera state equals the startup constants;
  triggering Front/Top/Right leaves the controller at the specified angles once the
  animation completes (animation is run to completion by processing events); a synthetic
  MMB drag changes azimuth but never the up vector; Shift+MMB moves the target; existing
  58 checks keep passing.
- **Visual pass:** screenshots at startup, mid-orbit, zoomed far out and far in, checking
  the grid has no visible edge, no mush, and level horizon; final judgment by the user's
  hands.

## Acceptance criteria

1. App opens on the −45°/+30° three-quarter view with a level horizon.
2. Orbit never rolls the horizon and cannot pass the poles.
3. Orbit pivots around the point under the cursor; that point stays fixed on screen while
   orbiting.
4. Wheel zoom keeps the cursor point stationary on screen.
5. Standard views, view cube and Fit All animate (~250ms) rather than teleport.
6. The grid shows no hard boundary in any normal working view, and its density stays
   readable from 10mm-scale zoom to 5m-scale zoom.
7. MMB orbits, Shift+MMB pans, RMB does nothing.
8. Headless suite and all existing gui_smoke checks pass, plus the new ones.
