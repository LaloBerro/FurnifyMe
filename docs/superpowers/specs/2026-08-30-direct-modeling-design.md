# Milestone 2: Direct modeling — design

Date: 2026-08-30
Status: approved by standing instruction — the user answered four scoping questions and
asked for everything to be built without further questions. Decisions below were made
rather than asked.

Milestone 2. Milestone 1 plus five UX phases are merged. This milestone makes the app a
direct-modeling tool: geometry is edited by grabbing it, not only created by sketching.

## The four features, in the user's words and as decided

Reference images are Shapr3D throughout; as with the UX phases, the images fix the
interaction style, not the pixel look.

### 1. Face pull — "arrow selector when I select a face, this extrudes the body"

Selecting a single flat face of a body (face mode) shows a **pull arrow** at the face's
centre, pointing along the **outward** normal (Phase 4's orientation lesson: derive the
outward normal, `BRepAdaptor_Surface` never applies `TopAbs_Orientation`). Dragging the
arrow pulls the face:

- **Outward drag grows the body** — a prism of the face by the drag distance, fused on.
- **Inward drag carves** — the same prism cut away, so pushing a tabletop down makes it
  thinner. Both directions, as the user chose.
- A **typed field** beside the arrow shows the live distance in the display unit and
  accepts input, exactly the `ExtrudePreview` contract: the preview is built by the same
  kernel call the commit uses, invalid input keeps the last good preview, Enter commits,
  Escape cancels, and the panel owns those keys while visible.
- Release after a drag also commits — the drag is the gesture; Enter is for typed values.
- The commit replaces the body in the document (one undo checkpoint) and reports through
  a toast with Undo, like every other document change.
- A pull of zero, or one that would carve the body away entirely, is refused with a
  cause-and-fix toast. A non-planar face shows no arrow.

### 2. The transform gizmo — "move, rotate and spawn a body", refined to move/rotate/scale

Selecting **exactly one body** (body mode) shows a transform gizmo at the body's centre:
translate arrows, rotate rings, and a **uniform scale** handle. The user's word "spawn"
was clarified to mean scale; there is no copy control and no primitives menu.

- Built on **`AIS_Manipulator`**, OCCT's own 3D manipulator: it lives in the scene,
  scales with the camera, and OCCT does its hit-testing. Styled to theme colours as far
  as its API allows; its stock look is acceptable where not.
- Dragging previews the transform on the presentation; release **bakes** it through
  `BRepBuilderAPI_Transform` and replaces the body (one undo checkpoint, toast with
  Undo). Scale is uniform — `gp_Trsf` cannot express per-axis scaling, and per-axis
  stretch through `BRepBuilderAPI_GTransform` converts faces to NURBS, which degrades
  later booleans and fillets. A furniture stretch is better served by face pull anyway.
- **Snapping follows the existing Snap to Grid toggle**, as chosen: when on, translations
  snap to the 10 mm grid step, rotations to 15°, scale to 5% steps. When off, free.
- The gizmo hides while sketching, while a face is pending, and when selection is not
  exactly one body.

### 3. Bevels — "one direction makes a curve bevel, the other a hard bevel"

Selecting a single edge (edge mode) shows a **bevel arrow** at the edge's midpoint,
perpendicular to the edge. Dragging one way rounds, the other way flattens:

- **Dragging into the body makes a Fillet** — a curved bevel of radius r
  (`BRepFilletAPI_MakeFillet`).
- **Dragging outward makes a Chamfer** — a hard bevel of distance d
  (`BRepFilletAPI_MakeChamfer`).
- The label follows the drag: `R 2 cm` for a fillet, `C 2 cm` for a chamfer, through
  `Measure`. A typed field works as in face pull; the current kind is named beside it.
- **Vocabulary:** the words are **Fillet** and **Chamfer** — standard, transferable CAD
  terms, the same reasoning that chose Union/Subtract/Intersect in Phase 1. Tooltips
  teach: a fillet *rounds* the edge, a chamfer *flattens* it. Both join the vocabulary
  table; "bevel" itself appears nowhere in the UI.
- **OCCT fillets fail on hard geometry** — the project brief calls this its known weak
  spot, and an early failure is not evidence of a mistake. A failed fillet or chamfer is
  reported as cause-and-fix through a Failure toast, never surfaced as success; the body
  is untouched. The radius that fits is the user's to find; the app refuses honestly.

### 4. The Appearance panel — "change all the colors of the app and make the font variables"

Full custom, as chosen — no presets:

- **Every `Theme` colour token gets a picker row** in a scrollable floating **Appearance
  panel** (family card over the viewport), toggled from `View → Appearance…`. Clicking a
  swatch opens a **modeless** colour dialog. The no-modal rule is about the app never
  blocking to *speak*; a picker the user summons is the user acting. It is still opened
  modeless so the suite's no-`QDialog`-after-an-outcome checks stay meaningful.
- **Fonts become variable:** a family picker (system families, DM Sans remains the
  default) and a base size. The four-size scale survives as *derived* sizes — badge,
  label, body, title as fixed offsets from the base — so the "four sizes only" law and
  its sweep keep holding with the numbers no longer constant.
- **`Theme` becomes spec-backed:** tokens read from a `ThemeSpec` struct whose defaults
  are today's Graphite constants. Editing a token updates the spec, re-applies the
  stylesheet, repaints every widget, rebuilds the grid and the OCCT background — one
  `themeChanged` broadcast, not per-widget wiring.
- **Persistence** through `QSettings` under the same `persistProgress` guard as
  everything else, so the suite can never touch the developer's real theme.
- **Reset to defaults** restores Graphite in one click — full custom can produce an
  unreadable app, and the way back must not require finding twenty pickers.

## Cross-cutting decisions

- **Kernel first, Qt never.** `ModelingOps` gains `pullFace`, `filletEdge`,
  `chamferEdge`, and a transform bake — all Qt-free, all returning the
  `BooleanResult`-style ok/shape/error, all headless-tested before any UI exists.
- **Gizmo previews get their own presentation channel.** The single `setPreview` slot
  already caused one bug when two features shared it (Phase 3); pull and bevel previews
  use a dedicated channel and never touch the sketch/extrude slot.
- **Every commit is a document replace** with an undo checkpoint and a toast offering
  Undo — the Phase 3 contract, unchanged.
- **No new rail buttons and no new teaching surfaces.** All three gizmos are
  selection-driven; the status label and tooltips teach them. The Appearance action
  lives in the View menu. The rail stays at thirteen.
- The three gizmos are mutually exclusive by construction: they key off selection mode
  and selection count, and at most one can be due at a time.

## Non-goals

No copy/duplicate, no primitives library, no per-axis scale, no multi-edge fillets in
one gesture, no variable-radius fillets, no theme presets, no theme import/export, no
history tree — a pulled face or a filleted edge is a new shape, not a parametric
feature. Face indices remain unstable across rebuilds; nothing here stores one.

## Testing

- **Headless**: `pullFace` outward grows volume by exactly area×distance on a box face
  and fuses to one solid; inward carve shrinks it by the same; carving past the far face
  is refused, not returned as garbage; `filletEdge` on a box edge produces the exact
  fillet volume delta ((1 − π/4)·r²·length removed), `chamferEdge` the triangular prism
  delta; a fillet radius larger than the adjacent face is refused with `ok == false`;
  the transform bake moves the centre of mass exactly and preserves volume; uniform
  scale scales volume by s³.
- **`gui_smoke`**: selecting a flat face shows the pull arrow and field, `childAt`-real;
  dragging (synthesised in-process mouse events on the arrow) previews without touching
  the document, release commits with the body's dimensions grown accordingly; inward
  carve shrinks them; the toast offers Undo and Undo restores. One body selected shows
  the manipulator, transform bakes on release, snap follows the toggle. Edge mode drag
  fillets one way, chamfers the other, labels `R`/`C` correct, failure toast on an
  impossible radius. The Appearance panel opens, a token edit repaints (pixel-probed),
  fonts rescale with the derived offsets, reset restores Graphite, and nothing persists
  with `persistProgress=false`. All existing 618 checks keep passing.

## Acceptance criteria

1. A selected flat face can be pulled by drag or typed value, both directions, preview
   by the commit's own path, committed with undo and toast.
2. A single selected body can be moved, rotated and uniformly scaled by gizmo, snapping
   with Snap to Grid, baked on release with undo and toast.
3. A selected edge drags into a fillet one way and a chamfer the other, labelled through
   `Measure`, with kernel failures refused honestly.
4. Every theme colour and the font family/base size are editable in the Appearance
   panel, applied live, persisted under the guard, and resettable.
5. `ModelingOps` stays Qt-free; every new kernel operation is headless-tested.
6. Nothing settled in Phases 1–5 regresses; both suites green throughout.
