# Phase 7: Polish and fixes — design

Date: 2026-08-31
Status: approved by standing pattern — the user listed sixteen items with reference
images, answered four scoping questions, and the build proceeds without further
questions. Numbers below are the user's own numbering.

## The sixteen items, as decided

**1 + 15 — Orthographic projection.** The app gains a real projection mode. The bar's
view label button becomes a **Persp / Ortho toggle** (15). Clicking an axis-gizmo arm
snaps to that axis view **in orthographic**; `Lock to Face` (and its new gesture, see 13)
flies the camera square onto the face, also orthographic (1, image 1). Orbiting
afterwards returns to **whatever base mode the toggle holds** — a user in perspective
gets perspective back the moment they orbit; a user who chose Ortho stays orthographic.
One projection state lives in `CameraController` (Qt-free, headless-tested); the OCCT
camera follows it.

**2 — Sketch geometry above the grid.** The in-progress outline, its markers, the
dimension annotation and the pending face render **above** the ground grid (image 3),
never interleaved with it (image 2). The grid is ground; sketch work is foreground.

**3 — Markers restyled to image 3.** First point: small accent square. Cursor: violet
ring. Placed points keep a small dot. Same three-state distinction, image 3's anatomy.

**4 — Outlines are document items.** A closed outline becomes an **Outline** item
(`Outline 01`, the vocabulary's own word) in the Items drawer, visible in the viewport,
with the same visibility eye. Extruding it **converts** it to a Body (the outline item
disappears, the body appears). Sketch-in-progress is not an item; closing creates one.

**6 — Undo covers sketching.** While sketching, `Ctrl+Z` removes the last placed point
(Backspace already does; Ctrl+Z joins it). Closing an outline is a document change:
undo removes the outline item, redo restores it. Extrude's undo returns the outline
item, not just the pre-extrude document.

**5 — Shift continues the line straight.** Holding Shift while placing the next point
snaps the cursor onto the previous segment's direction, extended — a straight
continuation. Qt-free snap helper, headless-tested. With fewer than two points, Shift
does nothing.

**7 — Colours export and import.** The Appearance panel gains `Save colours…` /
`Load colours…` (file dialogs, modeless where Qt allows), writing the serialized spec to
a small text file. The user will hand such a file back to be baked in as
`defaultSpec()` for everyone in a later change — the format is therefore the exact
`serializeSpec()` string, versioned by its own key set.

**8 — The spreading-bevel bug.** Filleting an edge adjacent to an existing fillet also
bevels the neighbouring edge (images 4–5). Diagnose first — the likely culprits are
tangent-chain propagation inside `BRepFilletAPI` or the picked edge mapping onto a
merged chain after `UnifySameDomain` — then fix so exactly the selected edge is
filleted, with a regression test on a body that already carries a fillet.

**9 — Multi-edge bevels.** Edge mode allows Shift-click to add edges; the bevel arrow
appears for one or many, and applies the same radius/distance to all in **one** kernel
call (`BRepFilletAPI` accepts multiple edges natively) — one undo checkpoint, one toast.

**10 — Live font preview.** Scrolling the font combo with arrow keys applies the
highlighted family immediately (the `highlighted` signal, not just `activated`), so the
app previews while the list is open; Escape restores the previous family.

**11 — The gizmo stays a sane size.** The manipulator's size follows the body's bounding
box today, so a zoomed-in or large body fills the screen with gizmo (image 6). Clamp its
on-screen footprint: size derived from the view so it stays within a fixed screen
fraction at any zoom.

**12 — Toasts can be turned off.** A `Show notifications` toggle (View menu). Off
silences **Note** toasts only: **Failure toasts always show** — a refusal that reports
nowhere would violate the never-silent-failure law. The toggle persists with the other
settings. Undo remains reachable via Ctrl+Z and the menu when notes are off.

**13 — Double-click selects the body; Ctrl+double-click locks the face.** In face and
edge modes, plain double-click on any part of a body selects the whole body (switching
to body mode's selection). The face-lock gesture moves to **Ctrl+double-click**; `L` /
`Shift+L` and the menu entries stay.

**14 — Thicker accent stripe.** The toast's kind stripe grows from 3 px to 6 px
(image 8) — same colours, more presence.

**16 — App icon.** A painted placeholder icon (IconSet's idiom: the wordmark glyph on a
Graphite tile) rendered to `.ico` at build time or committed as an asset; wired into the
executable (`.rc`) and `QApplication::setWindowIcon`.

## Non-goals

No arcs/circles in sketches (item 2's "circles" refers to the marker rings, which are
covered by 3). No per-edge radii in a multi-edge bevel — one value for all. No colour
marketplace — export is a file. No constraint solver behind Shift-straight.

## Testing

Headless: projection state round-trips and survives orbit; Shift-straight snap maths;
outline items in `DocumentModel` (add, convert-on-extrude, undo/redo). `gui_smoke`: the
Persp/Ortho toggle changes the OCCT camera's projection; a gizmo-arm click lands ortho
and an orbit restores the base mode; sketch pixels sit above grid pixels (probe along a
grid line under the outline); marker styles per image 3; Ctrl+Z removes a point
mid-sketch; an outline item appears on close, converts on extrude, undoes back;
Shift-continuation lands collinear (cross-product ~0); the spreading-bevel regression
(fillet one edge on an already-filleted body — face count grows by exactly the one
strip); multi-edge bevel volume delta = k×single; combo `highlighted` applies; gizmo
screen-size clamp at two zooms; notifications toggle silences a Note but never a
Failure; double-click and Ctrl+double-click routes; stripe width; icon present on the
window. All existing 965 checks and 6 headless suites stay green at native, 1.25× and
1.75×.

## Acceptance criteria

1. Persp/Ortho is a real, persistent mode; gizmo arms and face-lock fly orthographic
   face-on; orbit restores the base mode.
2. Sketch work renders above the grid with image 3's marker anatomy.
3. Outlines are items that convert to bodies on extrude, with undo across the whole
   sketch lifecycle including Ctrl+Z mid-sketch.
4. Shift continues straight; the snap maths is headless-tested.
5. Exactly the selected edges get beveled — the adjacent-fillet bug is fixed with a
   regression test — and multiple selected edges bevel in one gesture.
6. Colours export/import; fonts preview live; the gizmo never exceeds its screen
   fraction; Note toasts can be disabled while Failures cannot; double-click selects
   the body with Ctrl+double-click locking; the stripe is 6 px; the exe has an icon.
7. Both suites green at three scales throughout.
