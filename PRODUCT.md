# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop

## Users

One confirmed user today: the author, who designs furniture and then builds it
himself. The product is written for him first, but deliberately so that a
woodworker who is not him could pick it up — which is why the teaching surfaces
(the first-run walkthrough, the one-at-a-time hint balloons, the generated
shortcut sheet) are load-bearing rather than decoration, and why they go quiet
after three completions instead of nagging.

No other user has been observed or interviewed. Future work must not invent a
second audience's needs; "other makers later" is a design constraint on
discoverability, not evidence about anyone in particular.

## Product Purpose

Design a piece of furniture directly — push and pull the geometry rather than
editing a parameter tree — and come away with two things:

1. **A plan you can build.** Real cut sizes, mitre angles, and joinery laid out
   as mark-out numbers you transfer to wood with a pencil and a square.
2. **A picture you can show.** The same piece, standing in a scene with other
   furniture, rendered well enough to present a decision.

Both halves are first-class. The scene editor and the path-tracing render tier
are not a flourish on top of the modeling; they are the second half of the
product's reason to exist.

Success is a piece of furniture that got built from this app's numbers, and a
picture of it that was worth showing someone.

## Positioning

Direct modeling, in Shapr3D's spirit, aimed specifically at wood a person will
cut by hand. Two things follow that a neighbouring CAD app could not truthfully
copy:

- **Joinery is a planning layer, not a cutting operation.** No code anywhere
  cuts a shape for a joint. The hardware is drawn ghosted through the boards and
  read off as numbers, because the wood is cut by hand. A joint is stored as a
  relationship between two bodies and its position is re-derived from the live
  shapes every time, so it follows its pieces through any edit with no
  correspondence tracked.
- **Every size is measured along the board's own sides**, never a world bounding
  box. A 600 × 300 × 18 board turned 30 degrees reads 600 × 300 × 18, not
  669.6 × 559.8. This is the product's most repeated bug class and the
  discipline that distinguishes it.

## Operating Context

Three top-level windows, each with a distinct job:

- **SelectorWindow** — the hub. A managed library of `.furnify` furniture and
  scenes, as cards. Nothing is modeled here.
- **MainWindow** — the editor. One furniture at a time: sketch, extrude,
  boolean, push/pull, fillet, chamfer, mitre, re-measure, slats, mirror, linked
  copies, joinery, versions, render mode.
- **SceneWindow** — the scene editor. Several furniture arranged in one picture.
  It can arrange furniture and cannot make it: no sketch, extrude, boolean,
  joint, mirror, version or compare exists here — absent rather than disabled,
  because an action that exists and refuses reads as broken.

The library lives at `Documents/FurnifyMe/`. One furniture is one directory
(manifest plus shapes plus thumbnail plus versions); that layout is private to
`FurnitureStore` and nothing outside it touches a path inside one.

Work is done at a desk with a mouse: right-drag orbits, middle-drag pans, the
wheel zooms toward the cursor. No touch or pen input is supported or planned.

## Capabilities and Constraints

- **Stack:** C++17, CMake, Qt 6 Widgets (not QML), OpenCascade 8.0.1 as the
  geometry kernel. The viewport is OCCT's own `V3d_View` and
  `AIS_InteractiveContext` hosted in a `QOpenGLWidget` — there is no custom
  renderer and there will not be one.
- **Platform is `desktop`**, which is outside Impeccable's documented set of
  `web` / `ios` / `android` / `adaptive`. Recorded honestly rather than forced
  into a value that would pull web or mobile guidance into a Qt Widgets desktop
  app.
- **Windows and Linux are both committed targets.** Windows is where all work
  happens and the only platform ever built or run; Linux has never been
  configured, built or run, and that gap is real, open debt rather than a
  dropped claim. Future work must not paint itself into Win32-only corners.
- **Millimetres are the only unit anything stores.** Every persisted value and
  every number handed to the kernel is millimetres; the conversion to the
  displayed unit exists only at the formatting boundary, and input is read in
  the displayed unit — typing 4 with centimetres chosen builds 40 mm.
- **Every user-facing length and size is formatted in one place** (`Measure`),
  never at a call site. Volume is never shown: furniture is specified by
  dimension.
- **The app has no modal dialogs.** Nothing it has to say requires an answer, so
  nothing blocks. Outcomes are reported by one non-blocking message at a time;
  a message reporting a document change carries Undo.
- **A refusal is never surfaced as a success.** A failed boolean, a failed file
  load, a kernel refusal — each reports its own reason and stops. Success
  notifications can be silenced; refusals cannot.
- **STEP is the only export format** (AP214). No other geometry format is in
  scope.
- **Explicitly out of scope,** and not gestured at anywhere in the code: a
  history or parametric tree, a constraint solver, 2D drawings, assemblies,
  per-body materials inside one furniture, scene-level joinery, nesting a scene
  in a scene, and exporting joinery data of any kind.
- **Known honest limits** worth preserving as facts: a furniture records a look
  per material name and nothing about which was last on screen, so a furniture
  with several materials uses the first recorded; a mirrored joint is a copy
  taken at the moment of mirroring and nothing keeps the pair's plans in step
  afterwards; and the scene magnet's 8-pixel threshold was chosen for
  editor-scale bodies and has not been re-measured for scene-scale distances.

## Brand Commitments

- **Name:** FurnifyMe. CMake project `FurnifyMe`, app target `furnifyme`.
- **The app mark is the author's own artwork** (`assets/Icon.png`, shipped as
  `:/icons/app.png`). It is not to be redrawn or substituted.
- **One word per concept, and the vocabulary is enforced by test.** The suite
  fails if any action text or widget tooltip contains a banned word. Body not
  solid; Union not fuse or merge; Subtract not cut; Pull not push/pull; Mitre
  not bevel; outline not polyline; viewport not canvas; folder not layer; piece
  not instance; look not shader. "Joint" is this app's own word and passes while
  "join" does not. User-typed names are exempt; the app's own copy embedding one
  is not.
- **Voice:** status-bar text takes no trailing period; dialog bodies are full
  sentences; tooltips lead with a fragment and may add one teaching sentence.
  Clauses are separated by an em dash. Singular and plural are written out —
  never `(s)`.
- **The default look is the author's own**, baked from
  `assets/defaultcolors.furnifytheme`: near-black viewport and grids, a violet
  accent, tinted hover cyan, 2px chip strokes. Every colour in the app is a
  token; hex lives in exactly one file.
- **Type is DM Sans** (`assets/fonts/`), on a four-size scale and no more.
- **No control paints a keyboard focus ring.** The author asked for every one of
  them removed, twice and explicitly. Two retired tokens stay in the theme spec
  so older saved themes still load, but nothing paints them. Future work must
  not reintroduce focus rings as an accessibility improvement without asking.

## Evidence on Hand

- **A real library with real work in it:** four furniture and one scene under
  `Documents/FurnifyMe/`. It is the author's own data — copy it to test against,
  never write to it. One of those furniture carries 122 bodies and is what
  exposed a render-mode crash the toy fixtures never could.
- **Shipped wood materials:** `assets/materials/oak-natural.jpg` and
  `oak-veneer.jpg`.
- **Design specs for every feature**, dated, under `docs/superpowers/specs/` —
  camera and grid, help and learnability, language and numbers, shell UI,
  dimensions and units, direct modeling, files/versions/symmetry, auto
  selection, the custom gizmo, joinery, the scene editor, scene snapping.
- **Two test suites as evidence of behaviour:** eleven headless geometry tests
  (no window, no GPU, runnable in CI) and `gui_smoke`, which drives the real
  windows in-process by delivering Qt events straight to the widgets — 5,278
  checks, and it never touches the OS input queue, so the machine stays usable
  while it runs.
- **No pricing, licensing, install, update, support, distribution, testimonial,
  customer or benchmark exists.** None has been discussed. Future work must not
  fabricate any of them.

## Product Principles

1. **One word per concept, everywhere.** A user who reads "Body 03 rounded" has
   no word to look for in the interface. The vocabulary is executable, not
   aspirational, and it is the first thing new copy is checked against.
2. **Nothing blocks, and nothing fails silently.** No modal dialogs; one
   non-blocking message at a time; a change to the document offers Undo; a
   refusal always carries the reason it refused.
3. **Geometry is edited by grabbing it, and the kernel refuses before the UI can
   lie.** Every preview is built by the same call the commit will make. A
   preview built by a different path is a lie, and a shape the user can see that
   exists in no document is the worst thing a tool here can produce.
4. **A relationship is derived, never stored as a coordinate.** A mirror twin is
   re-mirrored from its source, a joint's hardware is re-derived from the live
   shapes, a scene piece references its furniture instead of copying it. The
   alternative is a stored position every edit site must remember to update, and
   the site somebody misses is the one that lies.
5. **It breaks loudly, with no numbers.** When pieces stop meeting, a joint
   refuses with a reason and shows nothing, because mark-out numbers that no
   longer describe the wood still look transferable to it. A broken reference
   keeps its row, says why, draws nothing, and survives the next save.

## Accessibility & Inclusion

No accessibility standard has been established for this product, and none
should be assumed.

One decision is recorded here because it is deliberate and runs against the
usual advice: **keyboard focus rings are removed everywhere**, at the author's
explicit and repeated request. A field being edited marks itself with the accent
colour instead. Treat this as a product fact to preserve, and raise it as a
question rather than quietly reverting it.
