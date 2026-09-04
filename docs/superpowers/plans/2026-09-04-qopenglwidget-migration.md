# QOpenGLWidget Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Host OCCT's viewport in `QOpenGLWidget` so Qt composites the 3D frame,
then delete the native-window workaround stratum and give every floating card true
antialiased transparent corners, then re-verify the render tiers in the new context.

**Architecture:** OCCT's official QOpenGLWidget pattern — wrap Qt's GL context
(`OpenGl_Context::Init` on live WGL handles) instead of creating one, attach the view
to a device-pixel-sized `Aspect_NeutralWindow`, wrap Qt's default FBO per frame
(`OpenGl_FrameBuffer::InitWrapper`), `Redraw()` inside `paintGL()`.

**Tech Stack:** C++17, Qt 6.11 (QOpenGLWidget), OCCT 8.0.1, existing CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-04-qopenglwidget-migration-design.md`

## Global Constraints

- PHASE GATE after each task: suites green, commit, exe rebuilt (tasklist check;
  never kill the user's furnifyme.exe), STOP for the user's test.
- One logical↔device conversion point survives (`toDevicePixels`/`fromDevicePixels`).
- Suite adaptations must be justified per check in the commit message — a check may be
  REWRITTEN for the new architecture, never silently weakened; the no-silent-weakening
  rule from the spec binds. Floor may only rise except where checks are deleted WITH a
  same-commit replacement pinning the equivalent truth (state the arithmetic).
- The 3 known environmental pixel failures (silhouette sweep + locked-face grid span
  ×2) may appear when the machine is in use; anything else is the task's own.
- CLAUDE.md updated at each phase end (opaque-family law and native-HWND pitfall get
  tombstones in Phase 2, not deletions — the history explains the code).
- Repo-voice commits; trailer: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: The hosting swap (Phase 1 — visually invisible)

**Files:**
- Modify: `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp` (the hosting layer),
  `src/main.cpp` (QSurfaceFormat before QApplication), `tests/gui_smoke.cpp`
- Reference: OCCT's QOpenGLWidget sample pattern (headers under
  `C:/vcpkg/installed/x64-windows-release/include/opencascade/` — `OpenGl_Context.hxx`,
  `OpenGl_FrameBuffer.hxx`, `Aspect_NeutralWindow.hxx`; read them, do not trust memory)

**Interfaces:**
- Consumes: everything OcctViewWidget already exposes.
- Produces: the identical public surface — no caller changes outside the files above.

- [ ] **Step 1: baseline.** Record BASE; run the suite once for the honest before-count.
- [ ] **Step 2: reparent the class.** `OcctViewWidget : QOpenGLWidget` (Widgets module
  already links QtOpenGLWidgets? — check CMake, add `Qt6::OpenGLWidgets` if absent).
  Remove `WA_PaintOnScreen`, `WA_NoSystemBackground`, `WA_OpaquePaintEvent`,
  `setAutoFillBackground(false)`, the `paintEngine() == nullptr` override, the
  `WNT_Window`/`Xw_Window` attach, and the `resizeEvent` `SetWindowPos` hack — the
  MASKS AND PAINT WORKAROUNDS STAY (Phase 2's job).
- [ ] **Step 3: the GL plumbing.** `initializeGL()`: driver with no own context;
  `OpenGl_Context` wrapping the current Qt context; `Aspect_NeutralWindow` sized via
  `toDevicePixels(size())`; view `SetWindow(window, glContext->RenderingContext())`
  per the OCCT sample. `paintGL()`: wrap `defaultFramebufferObject()` via
  `OpenGl_FrameBuffer::InitWrapper`, `myView->SetImmediateModeDrawToFront(false)`,
  set the wrapped FBO as default, `Redraw()`. `resizeGL(w,h)`: neutral window
  device-pixel resize + `MustBeResized()`. `paintEvent`→`update()` routing dies;
  every old `Redraw()` call site becomes `update()` (schedule) unless a synchronous
  frame is genuinely required (Dump paths — verify Dump still renders offscreen
  correctly; if Dump needs a current context, wrap with `makeCurrent()`).
- [ ] **Step 4: lazy-init mapping.** The `initializeViewer()` contract maps to
  `initializeGL` (Qt calls it at first show). Everything that called
  `initializeViewer()` defensively must keep its record-state-then-apply-on-first-
  paint discipline (GridRenderer's no-op-until-context rule). The startup checks
  (`startup distance is 700mm`, focus-visible) are the canary — any drift is a
  regression to fix, not explain away (A/B against BASE mandatory before any
  environment claim).
- [ ] **Step 5: QSurfaceFormat.** In main.cpp before QApplication: depth 24, stencil 8,
  swap behavior default; profile: start with CompatibilityProfile (OCCT's ray tracing
  historically wants it) — Phase 3 finalizes. gui_smoke sets the same format in its
  own main.
- [ ] **Step 6: adapt the suite.** Expected touch points: any check reading
  `paintEngine()`, the native-rect/SetWindowPos pin from M3 (rewrite to assert the
  neutral window's size matches `toDevicePixels(size())` after a splitter round
  trip — same truth, new mechanism), snapshot dirs, timing settles. Both windows,
  compare pane, selector handoff, render mode entry/exit must pass untouched.
- [ ] **Step 7: full suites; commit.** gui_smoke + headless; the commit message
  carries the per-check adaptation justifications.

### Task 2: Deleting the workaround stratum (Phase 2 — the visible payoff)

**Files:**
- Modify: `src/ui/Theme.{h,cpp}` (paintSurface loses its ground fill; installCardMask
  deleted), every masked widget from commit 4d96865 (mask calls removed),
  `src/ui/ToolChip.cpp` (QSS patch reviewed — keep the transparent background, it is
  correct regardless), `tests/gui_smoke.cpp`, `CLAUDE.md`

- [ ] **Step 1:** delete `installCardMask` + all call sites; delete the ground fill in
  `paintSurface` (card + crisp border only); widgets over the viewport get
  `background: transparent` QSS or `WA_StyledBackground` handling so the QSS chrome
  rule cannot stamp them (the makeTransparent pattern, applied at the family level).
- [ ] **Step 2:** verify every floating card composites with antialiased corners:
  NEW pixel checks — over a known scene state, a corner pixel of each reachable card
  reads the SCENE, and an edge-of-curve pixel reads a BLEND between card and scene
  (neither pure card nor pure scene — the thing the old architecture could never
  produce; tolerance generous, non-vacuous). Remove the mask structural pins WITH
  these replacements (state the floor arithmetic).
- [ ] **Step 3:** sweep for stale law references: the opaque-family sections in
  CLAUDE.md become a tombstone (what the law was, why it existed, what replaced it);
  the native-HWND pitfall marked historical; `wholeDevicePixels`/`snapToDevicePixels`
  REMAIN (still real — device-pixel geometry did not change).
- [ ] **Step 4:** full suites; commit; exe rebuild; USER GATE — this is the corners
  test, the project's reason to exist.

### Task 3: Render tiers in the new context (Phase 3)

**Files:**
- Modify: `src/OcctViewWidget.cpp` (tier probe/params as measurements dictate),
  `src/main.cpp` (QSurfaceFormat finalized), `tests/gui_smoke.cpp`, `CLAUDE.md`

- [ ] **Step 1:** measure each tier in the wrapped context: probe outcomes, the PT
  floor blend + shadow ratio, the Shadows-tier calibration numbers, convergence
  timing + the export gating (the accumulation-buffer capture path may differ against
  a wrapped FBO — measure `Dump` vs on-screen convergence again).
- [ ] **Step 2:** retune ONLY what measurement says moved; every retuned constant gets
  its measured before/after in the commit. A tier that cannot run in the wrapped
  context is parked behind a constant with the measurement, PT-parking style.
- [ ] **Step 3:** full suites; commit; exe rebuild; USER GATE — render mode judged
  against the same reference as before.

## Self-review

- Spec coverage: hosting swap→T1, workaround deletion + corners→T2, tiers→T3. ✔
- No placeholders; interfaces stable by design (T1 changes no public surface). ✔
- The masks-stay-in-T1 / masks-die-in-T2 split is deliberate spec law, restated in
  both tasks so an implementer reading one task cannot miss it. ✔
