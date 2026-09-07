# Auto selection — one behaviour replaces three modes

Approved by the user 2026-09-06 ("Replace the modes entirely", then "yes" to the design).

## The behaviour

- **Hover decides what a click takes.** Within a small pixel tolerance of an edge → the
  edge glows; otherwise on a face → the face glows. The highlight is the contract: a
  click picks exactly what glows.
- **Double-click picks the whole body** (existing gesture, unchanged);
  **Ctrl+double-click locks the face** (unchanged). **Shift accumulates the same KIND
  as the first pick** (edges with edges — including across bodies per Milestone 5 item
  5 — bodies with bodies); a Shift-click on a different kind is a quiet no-op with the
  status label saying why. Clicking empty space clears, as today.
- **The mode buttons and their actions are REMOVED** (rail slims by three; the
  ShortcutSheet regenerates itself; retired shortcuts go unbound).
- **Gizmos re-key from mode to selection content**: exactly one face selected → pull
  arrow; one or more straight edges → bevel arrow; exactly one body → transform gizmo.
  Kind-locked accumulation makes the selection hold one kind at a time, so the gizmos'
  mutual exclusivity is preserved BY CONSTRUCTION — the same guarantee, new foundation.
  ExtrudePreview (pending outline) and the mirror placement claims are untouched.
- Teaching surfaces (walkthrough, hints, tooltips, state label) rewrite their copy to
  the new model. No painted string names a "mode" — there is nothing to name.

## Mechanism

OCCT activates edge and face selection modes simultaneously on every body;
hover priority is TUNED BY MEASUREMENT (edge wins within its tolerance; the task
records the tolerance and pins it with pixel probes). `OcctViewWidget`'s internal
kind-forcing API survives as a TEST SEAM only (no UI reaches it): new auto-behaviour
tests drive the real hover path; existing tests that merely need a selection of a
given kind may use the seam where re-driving via hover adds nothing — each such use
justified in place.

## Phases (user tests between)

1. **The auto-pick core, invisibly.** Simultaneous modes, hover priority, kind-locked
   accumulation, all behind the existing UI (mode buttons still present and
   functional). Suite green with auto-path tests added.
2. **The switch.** Mode UI removed, gizmo predicates re-keyed to selection content,
   teaching copy rewritten, test migration (seam-uses justified). This is the phase
   the user's feel-test judges.

## Out of scope

Magnet/snap (its own list item), the custom transform gizmo (parked), any change to
what the gizmos DO once raised.
