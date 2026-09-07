# The split transform gizmo — Move, Rotate, Scale, drawn by us

Approved by the user 2026-09-06 (improvements item 12; the long-parked custom-gizmo
project, unblocked by the split design).

## Why ours, why now

OCCT's `AIS_Manipulator` has no styling API (measured twice, documented in CLAUDE.md);
the only route to a gizmo wearing the axis card's language is drawing our own. The
split makes that tractable: three single-purpose tools are simpler than one combined
object, and every mechanism already exists in this codebase — PullArrow/BevelArrow are
the precedents (own AIS presentation in theme colours, screen-space hit tests,
camera-derived drag math, value chip, commit through `MainWindow::transformBody`).

## The tools

- **Move**: three arrows from the body's pivot in `gizmoAxisX/Y/Z`, cone tips (the
  axis card's own vocabulary). Drag = the pull arrow's closest-point-on-axis math per
  axis. Snapped 10 mm (Snap to Grid honoured), value chip in the display unit.
- **Rotate**: three rings, one per axis, same colours. Drag = angle about the ring's
  axis from the pivot; snapped 15°; chip shows degrees.
- **Scale**: a centre handle; uniform only (the existing kernel constraint); snapped
  5%; chip shows a factor; the [0.05, 20] clamp keeps its refusal semantics.
- **Space cycles Move → Rotate → Scale** while a body is selected (an app action, so
  the ShortcutSheet carries it); the active tool is visible state (status label +
  whichever affordance the mockup pick establishes). One tool visible at a time.
- All three appear exactly where the transform gizmo appears today (exactly one body
  selected — the Auto-selection predicate) and commit through the existing
  checkpoint/undo/toast paths. Zoom-persistent sizing via `worldPerPixel` (the
  solved-problem path, never OCCT's own persistence flags).

## What dies

`AIS_Manipulator` and its documented workaround pile (the Deactivate-around-picks
hazard, the zoom-persistence pin, the styling tombstones stay as history).

## Phases (user-gated; Phase 1 is mockup-gated)

1. **Move** replaces the manipulator's translation; the manipulator still serves
   rotate/scale (both reachable: manipulator when the tool is Rotate/Scale, ours for
   Move) — ugly seam, deliberately temporary, gone next phase.
2. **Rotate + Scale** join; the manipulator is deleted; Space cycling complete.
3. **Polish** from the user's feel-test (sizes, grab tolerances, chip placement).

Rotation drag math lands Qt-free and headless-tested. The mirror/linked-copy
propagation rules apply to gizmo commits exactly as today (same commit path).

## Out of scope

Magnet/snap-to-geometry (list item 13, its own project); per-axis scale (kernel
constraint); any change to selection behaviour.
