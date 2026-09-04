# Hosting OCCT in QOpenGLWidget — the compositing migration

Approved in direction by the user 2026-09-04 ("yes please let make that big change").

## Why

The 3D view is a native OS window today (`WNT_Window` over a `WA_PaintOnScreen` widget).
Qt cannot see its pixels, so nothing can blend with it. That single fact is the root of:
the opaque-paint-family law, the square corners behind every rounded card, the jaggy
window-mask workaround the user rejected, and the stale-native-HWND pitfall class from
Milestone 3. Hosting OCCT inside `QOpenGLWidget` puts the 3D frame through Qt's own
compositor: overlays gain true antialiased corners and real translucency, and the whole
workaround stratum is deleted rather than maintained.

## The approach (OCCT's own supported path)

OCCT ships an official QOpenGLWidget sample; the shape is:

- `initializeGL()`: create the `OpenGl_GraphicDriver` **without** creating its own GL
  context; wrap Qt's current context (`OpenGl_Context::Init` with the live WGL
  handles); attach the view to an `Aspect_NeutralWindow` sized in device pixels.
- `paintGL()`: wrap Qt's default framebuffer (`OpenGl_FrameBuffer::InitWrapper`), hand
  it to the view as the default FBO, `Redraw()`.
- `resizeGL()`: resize the neutral window + `MustBeResized()` (this REPLACES the
  Milestone-3 `SetWindowPos` defensive hack, which is deleted).
- A `QSurfaceFormat` set before `QApplication` construction (depth, stencil, and the
  profile OCCT's ray-tracing tiers need — determined empirically in Phase 3, not
  assumed).

Everything above the hosting layer — CameraController, picking, selection, gestures,
gizmos, grids, DocumentModel — is logically unchanged; input and projection still cross
one logical↔device conversion point.

## Scope rules

- **Visuals stay identical except where the old architecture forced ugliness.** No
  overlay redesign rides along. The one intended visible change: floating cards get
  genuinely transparent, antialiased rounded corners (ground fills and masks deleted).
- The compare pane (second viewer) migrates identically; `myViewerOnly` semantics
  unchanged.
- `V3d_View::Dump`/snapshots, thumbnails, and the render tiers must all still work;
  the tier probe re-verifies per session by design, and Phase 3 re-measures the
  calibrated pixels (floor blend, shadow ratios) in the new context.
- CLAUDE.md's affected laws are rewritten at the end of each phase (opaque family →
  retired with an explanatory tombstone; the native-HWND pitfall → marked historical).

## Phases (user tests between each)

1. **The hosting swap.** OcctViewWidget renders through QOpenGLWidget; orbit, pan,
   zoom, picking, sketching, snapshots, both windows, compare pane all work; the suite
   adapted where it pinned the old architecture (each adaptation justified in the
   commit, never silently weakened). The masks and paint workarounds stay in place this
   phase — one change at a time.
2. **Deleting the workaround stratum.** Remove: window masks + `installCardMask`, the
   opaque ground fills in `paintSurface` (cards paint card + border only; corners
   genuinely transparent), the chip QSS patch where superseded, `WA_PaintOnScreen`/
   `paintEngine()==nullptr` remnants, the `SetWindowPos` hack. Every floating card now
   composites with antialiased corners — verified by sampled pixels (a corner pixel
   over a known scene colour reads the scene, and an edge pixel reads a BLEND —
   the thing the old architecture could never produce).
3. **Render tiers in the new context.** RayTracing/PathTracing/Shadows re-verified and
   re-measured (probe thresholds, floor blend, shadow ratios, convergence export
   gating); `QSurfaceFormat` finalized; any tier that genuinely cannot run in a wrapped
   context is parked honestly with measurements, exactly like the PT parking was.

## Risks named up front

- The lazy-`initializeViewer` contract maps onto `initializeGL` (Qt calls it on first
  show) — startup determinism checks (`startup distance is 700mm`, focus) are the
  canary; treat any change there as a regression, not an environment story, and A/B it.
- Device-pixel discipline: the neutral window is device-sized while Qt speaks logical —
  the same one-conversion-point law holds and the suite's projection pins must stay
  green untouched.
- gui_smoke has years of checks that touch the old architecture obliquely (renderExact
  grounds, corner tokens, mask pins from Milestone 5 item 2, composited captures).
  Test migration is part of Phases 1–2's work, with the no-silent-weakening rule.

## Acceptance

Per phase: both suites green, CLAUDE.md truthful, user test passed. Project done when
the user confirms the corners (the original complaint) look right in Phase 2 and render
mode survives Phase 3 with its measured look intact.
