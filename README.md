<!--
  Screenshots live in docs/images/ and are captured from the real app.
  Name them WITHOUT hyphens: .gitignore ignores `g*-*.png` at every depth,
  so a file like `gizmo-move.png` would silently never be committed.
-->

<h1 align="center">
  <img src="assets/Icon.png" alt="FurnifyMe icon" width="96"><br>
  FurnifyMe
</h1>

<p align="center">
  <b>A desktop CAD app for designing furniture by grabbing it.</b><br>
  Draw an outline, raise it into a board, pull faces, round edges, mirror halves:
  real B-rep solids you can export to any CAD package.
</p>

<p align="center">
  C++17 · Qt 6 Widgets · OpenCascade (OCCT) · CMake
</p>

<p align="center">
  <img src="docs/images/hero.png" alt="A cabinet rendered in oak inside FurnifyMe's render mode" width="900">
</p>

---

## Contents

- [What it is](#what-it-is)
- [Highlights](#highlights)
- [A tour of the app](#a-tour-of-the-app)
  - [The furniture library](#the-furniture-library)
  - [The editor](#the-editor)
  - [Drawing outlines](#drawing-outlines)
  - [Extrude: from outline to body](#extrude-from-outline-to-body)
  - [Ready-made shapes](#ready-made-shapes)
  - [Selecting things](#selecting-things)
  - [Direct modeling: Pull, Fillet, Chamfer](#direct-modeling-pull-fillet-chamfer)
  - [Move, Rotate, Scale](#move-rotate-scale)
  - [Union, Subtract, Intersect](#union-subtract-intersect)
  - [Mirror](#mirror)
  - [Duplicates and linked copies](#duplicates-and-linked-copies)
  - [The Items drawer and Isolate](#the-items-drawer-and-isolate)
  - [Saving, autosave and versions](#saving-autosave-and-versions)
  - [Render mode](#render-mode)
  - [Camera, views and the grid](#camera-views-and-the-grid)
  - [Units and dimensions](#units-and-dimensions)
  - [Appearance](#appearance)
  - [Learning the app](#learning-the-app)
  - [Feedback without dialogs](#feedback-without-dialogs)
  - [STEP export](#step-export)
- [Keyboard and mouse](#keyboard-and-mouse)
- [In progress: joinery](#in-progress-joinery)
- [Building from source](#building-from-source)
- [Tests](#tests)
- [Architecture](#architecture)
- [Project status](#project-status)
- [License](#license)

---

## What it is

FurnifyMe is a cross-platform desktop modeler for furniture, in the spirit of
Shapr3D's *direct modeling*. There is no feature tree and no constraint solver.
You edit geometry by grabbing it: drag a face to make a shelf thicker, drag an edge
to round it over, drag an arrow to slide a side panel into place.

Under the hood every body is an exact boundary-representation solid built by
**OpenCascade**, the same geometry kernel FreeCAD uses. Curved edges stay truly
curved, booleans are real solid operations, and a finished design exports to
**STEP**, which any mainstream CAD or CAM package can open.

It is built for one job: working out what a piece of furniture looks like, at what
size, before you cut any wood. Everything is measured in millimetres (or
centimetres, if you prefer), and sizes are shown the way a woodworker reads them:
`340 × 220 × 18 mm`.

## Highlights

- **Sketch → Extrude → Union / Subtract / Intersect**, with live previews that are
  built by the same code that commits the result, so what you see is what you get.
- **Direct modeling**: face **Pull**, edge **Fillet** and **Chamfer** (including
  several edges at once), and 3D **Move / Rotate / Scale** gizmos.
- **Magnet** alignment: a dragged body sticks flush to other bodies' faces and
  centres, with a guide line showing what lined up.
- **Live Mirror**: place a mirror plane once, and every edit to one half re-creates
  its twin automatically.
- **Linked copies**: edit one leg and the other three follow.
- **Six ready-made shapes**: Box, Cylinder, Sphere, Cone, Wedge and Plank.
- **A managed library** of furniture with thumbnails, search and sort, plus
  **autosave** and **named versions** you can compare side by side with the live model.
- **Render mode** with path tracing, soft shadows, a studio floor and **wood
  materials** with adjustable grain.
- **Undo for everything** that changes the design, **no modal dialogs** anywhere,
  and a guided first build for new users.
- **Fully themeable**: every colour, the font and the text size can be edited live.

---

## A tour of the app

### The furniture library

<p align="center">
  <img src="docs/images/library.png" alt="The furniture library: a grid of thumbnail cards with search and sort" width="820">
</p>

FurnifyMe opens on your **library**, its own window with a gallery of every
piece of furniture you have made.

- Each card shows a **thumbnail** of the model as it was last saved, with its name
  and when it was last edited underneath.
- **Search** filters cards by name as you type. **Recent** and **Name** switch the sort order.
- The first card is always **+ New furniture**.
- Hover a card to reveal **Rename** (edits the name in place) and **Delete** (click
  twice to confirm).
- Click a card to open it in the editor. **File → Close furniture** (`Ctrl+W`)
  brings you back.

Furniture lives in `Documents/FurnifyMe/`. Each piece is a self-contained folder
holding its shapes, names, mirror and link settings, thumbnail and saved versions.

### The editor

<p align="center">
  <img src="docs/images/editor.png" alt="The editor: floating app bar, tool rail, Items drawer, axis gizmo and view controls around a 3D viewport" width="900">
</p>

The 3D viewport fills the whole window, and every control floats over it:

| Where | What |
|---|---|
| **Top left** | The **app bar**: the FurnifyMe mark and the menus (File, Sketch, Edit, Model, View, Help). |
| **Left edge** | The **tool rail**, a column of icon buttons: Items · Start Sketch, Extrude, Add shape · Union, Subtract, Intersect, Delete · Snap to Grid, with Undo and Redo at the bottom. Hover for a label and shortcut. |
| **Beside the rail** | The **Items drawer** and the **Versions drawer**, when open. |
| **Top right** | The **axis gizmo**, a small orientation cube. Click an axis tip to look straight down it. |
| **Under the gizmo** | **View controls**: Perspective/Orthographic, the unit chip (`mm`/`cm`), Wireframe and Fit All. |
| **Bottom** | The **status bar**: what is selected on the left, what you can do next on the right. |

The title bar is drawn by the app itself, so the window matches the rest of the
interface. Every button, menu entry and shortcut is backed by the same action, so
they can never disagree about what is available.

### Drawing outlines

<p align="center">
  <img src="docs/images/sketch.png" alt="An outline being drawn on the grid, with a live length dimension on the current segment" width="820">
</p>

An **outline** is the 2D shape that becomes a body.

1. **Start Sketch** (`Ctrl+K`).
2. **Click** to place points. A live yellow line follows the cursor, and a CAD-style
   **dimension** shows the current segment's length.
3. **Close** the outline by clicking the first point again, or press `Enter`. You
   need at least three points.

While drawing:

- **Snap to Grid** (on by default) rounds every point to the grid.
- Hold **`Shift`** to lock the segment to one of **eight compass directions**
  (every 45°), which makes square corners and clean diagonals easy.
- **`Backspace`** (or `Ctrl+Z`) removes the last point. **`Esc`** cancels.
- The cursor readout shows coordinates **in the plane you are drawing on**.

**Drawing on a face.** Outlines go on the ground by default. To put a shelf on the side
of a cabinet, select a flat face and choose **Lock to Face** (`L`), or
**Ctrl+double-click** the face. The camera flies square onto it, the grid moves onto
it, and the next outline lands exactly on that face. **Unlock Face** (`Shift+L`)
returns to the ground. Looking straight at the model from the Front or Right also
lays the grid on that vertical plane.

A closed outline is a real item: it appears in the Items drawer as `Outline 01`,
takes its own undo step, and remembers the plane it was drawn on.

### Extrude: from outline to body

<p align="center">
  <img src="docs/images/extrude.png" alt="The Extrude panel with a height field and a live preview of the body" width="820">
</p>

**Extrude** (`E`) raises the waiting outline into a **body**. A small panel asks for
the height and shows a **live preview** while you type. The preview is built by
the exact same operation that commits it, so the number you type is the body you get.

- `Enter` applies, `Esc` cancels, even while you orbit around to check the preview.
- A negative height extrudes downward.
- One `Ctrl+Z` turns the body back into the outline.

An outline you no longer want can be removed with **Delete** when no body is selected.

### Ready-made shapes

<p align="center">
  <img src="docs/images/shapes.png" alt="The Add shape flyout with six shape tiles" width="600">
</p>

**Add shape** on the rail opens a flyout of six primitives: **Box, Cylinder, Sphere,
Cone, Wedge** and **Plank**. The shape lands standing on the ground wherever the
camera is looking, already selected with its move handles up, as one undoable step.

### Selecting things

Selection follows the cursor, with no mode buttons. Hover near an **edge** (within about
8 pixels) and the edge glows; anywhere else on a body, the **face** glows. A click
takes exactly what is glowing.

| Gesture | Selects |
|---|---|
| Click | The face or edge under the cursor |
| Double-click | The whole body |
| `Shift` + click | Adds another face or edge of the **same kind** |
| `Shift` + double-click | Adds another whole body |
| Click empty space | Clears the selection |

What you select decides which handle appears: a **face** raises the Pull arrow, an
**edge** raises the Fillet/Chamfer arrow, and a **body** raises the Move, Rotate or Scale gizmo.
Only one can be up at a time.

### Direct modeling: Pull, Fillet, Chamfer

<p align="center">
  <img src="docs/images/pull.png" alt="A face selected with the Pull arrow and its distance chip" width="440">
  &nbsp;
  <img src="docs/images/fillet.png" alt="Two edges selected with the Fillet arrow and its radius chip" width="440">
</p>

**Pull a face.** Select a face and drag its arrow. Outward adds material, inward carves
it away. You can also type an exact distance into the value chip. Distances snap to the
grid when Snap to Grid is on.

**Fillet and Chamfer an edge.** Select an edge and drag its arrow:

- drag **inward** for a **Fillet**, a rounded edge (`R 20 mm`);
- drag **outward** for a **Chamfer**, a flat bevelled edge (`C 20 mm`).

`Shift`+click more edges to treat them all in **one** operation, **one** undo step
and one message, even across several bodies. The chip reads `Fillet — 3 edges`.

The geometry kernel refuses rather than guessing. A radius too large for the
neighbouring faces is reported with a reason and a suggestion, and the body is left
untouched. If some edges can only be treated one at a time, the app says so instead of
telling you to try a smaller size. A fillet also stays on the edges you picked:
OCCT likes to spread a fillet along neighbouring tangent edges, and FurnifyMe clips
that back to your selection.

### Move, Rotate, Scale

<p align="center">
  <img src="docs/images/gizmos.png" alt="The Move, Rotate and Scale gizmos on a selected body" width="900">
</p>

Select a whole body and a **3D gizmo** appears on it. Press **`Space`** to switch
between the three tools:

| Tool | Handles | Drag |
|---|---|---|
| **Move** | Three coloured arrows | Slide along an axis. The gizmo travels with the body. |
| **Rotate** | Three rings | Turn about an axis, in 15° steps with Snap on. |
| **Scale** | Cube-tipped arms | Uniform scale, in 5% steps with Snap on. |

Handles brighten on hover and show a grab cursor. A ghost preview shows the result
before you let go, and `Esc` cancels mid-drag. With Snap to Grid on, moves go in
10 mm steps. The gizmo keeps a constant on-screen size at any zoom, and its size can be
changed in Appearance.

**Magnet** (View menu, on by default): while you Move a body, it sticks when
one of its faces lines up flush with another body's face, or when their centres
align, and a guide line shows what snapped. Handy for sliding a shelf until it sits
exactly between two sides.

### Union, Subtract, Intersect

Select two bodies, then:

| Operation | Result |
|---|---|
| **Union** | One body made from both. Overlapping material is kept once. |
| **Subtract** | The second body cut out of the first, like a chisel removing waste. |
| **Intersect** | Only the volume the two bodies share. |

The two inputs are replaced by the result. Afterwards, faces that ended up in the same
plane are merged, so the new body stays clean to select and edit. A failed operation
is always reported and never leaves a half-finished body behind.

### Mirror

<p align="center">
  <img src="docs/images/mirror.png" alt="Placing a mirror plane beside a selected body" width="820">
</p>

Furniture is usually symmetric, so FurnifyMe lets you model half of it.

1. Select one or more bodies and press **`S`** (**Model → Mirror**).
2. A **mirror plane** appears, touching the side of your selection. Drag its handle to
   move it, and press **`X`**, **`Y`** or **`Z`** to aim it along another axis.
3. Press **`Enter`** to confirm, or `Esc` to back out.

Each body gets a **live mirrored twin**. From then on any edit to one side (a pull,
a fillet, a move, a boolean) re-creates the other side in the same undo step. New bodies
you extrude while Mirror is on get twins too. A body that sits across the plane itself is
left alone, since mirroring it would just overlap itself.

**Set Mirror Plane** uses a flat face you pick as the plane, and
**Turn Mirroring Off** drops every pairing (the bodies themselves stay).

### Duplicates and linked copies

| Command | Shortcut | What you get |
|---|---|---|
| **Duplicate** | `Ctrl+D` | An independent copy, offset by one grid step and selected. |
| **Duplicate linked** | `Ctrl+Shift+D` | A **linked copy**: edit any member and every other member follows. |
| **Link selected** | — | Makes two or more existing bodies one linked group, all taking the first body's shape. |
| **Unlink** | — | Takes a body out of its group; it keeps its current shape. |

Linked copies are ideal for identical parts: four legs, a row of drawer fronts, the
shelves of a bookcase. Each copy keeps its own position, and only the shape is shared.
(A body can be linked or mirrored, not both.)

### The Items drawer and Isolate

The **Items** drawer (`Ctrl+Alt+S`) lists every body (`Body 01`, `Body 02`, …) and
every waiting outline.

- Click a row to select it; the outline that Extrude will use is highlighted.
- **Double-click** a row or press **`F2`** to **rename** in place ("Left side",
  "Top shelf"). Renames can be undone.
- The button at the end of each row **shows or hides** that item. That choice is
  saved with the furniture.

**View → Isolate** (`I`) temporarily shows only the selected bodies, which helps when
working on the inside of a carcass. Bodies you create while isolated join the
isolation. Press `I` again to bring everything back. Isolate is a view setting and
never changes the design.

### Saving, autosave and versions

<p align="center">
  <img src="docs/images/versions.png" alt="The Versions drawer beside a side-by-side compare of a saved version and the live model" width="900">
</p>

**Saving.** `Ctrl+S` saves. **File → Autosave** offers five modes:

| Mode | Behaviour |
|---|---|
| Off | Saves only when you ask, or when you close the furniture. |
| **After every change** *(default)* | Saves shortly after each edit (bursts of edits save once). |
| Every minute / 5 minutes / 15 minutes | Saves on a timer, only if something changed. |

Closing always saves first. If a save fails, the app stays open and tells you why,
so you never lose work silently.

**Versions** are named snapshots of the whole design: bodies, names, visibility,
mirror and link settings. Open the **Versions** drawer (`Ctrl+Alt+V`) or use
**File → Save version...**. Each version is a card with a thumbnail:

- **Restore** replaces the current design with the version, as one undo step, so an
  accidental restore is a single `Ctrl+Z` away.
- **Compare** opens the version **side by side** with the live model in a second,
  read-only viewport. The two cameras stay in sync as you orbit, pan and zoom.
- **Delete** needs two clicks and is final, because a version is part of the file, not an edit.

### Render mode

<p align="center">
  <img src="docs/images/render.png" alt="Render mode: the furniture on a studio floor in oak, with the render studio panel on the right" width="900">
</p>

**View → Render mode** hides every tool, grid and handle, leaving only your
furniture on a studio backdrop with a soft floor that catches shadows. Clicking the
viewport or making any change returns you to modeling.

On first use FurnifyMe checks what your graphics card can handle smoothly and picks
the best of four **quality tiers**:

| Tier | What you get |
|---|---|
| **Path tracing** | Physically based light that refines over time: soft shadows, bounce light, filmic tone. |
| **Ray tracing** | Crisp ray-traced shadows and reflections. |
| **Shadows** | Fast shadow-mapped rendering. |
| **Plain** | Shaded bodies, for older hardware. |

The **studio panel** on the right edge is organised as:

- **Light**: light angle and strength.
- **Material**: preset tiles, including **wood** textures. Oak Natural and Oak
  Veneer are bundled, and any image you drop into `Documents/FurnifyMe/materials`
  shows up as another tile. **Grain size** and **grain angle** adjust the wood.
  **Surface** (glossiness) and **Metal** apply on the path-traced tier.
- **Scene**: background colour.
- **Camera**: field of view.
- **Quality**: *Deep* or *Simple*.
- A footer showing the active tier, how far the path-traced image has refined, and
  a **shutter** button that saves the render.

The path-traced image keeps refining while the camera is still, and a screenshot
waits for it to finish, so the saved image matches what is on screen. On the other tiers,
**Save Screenshot** exports at twice the screen resolution.

### Camera, views and the grid

| Control | Action |
|---|---|
| Right mouse drag | **Orbit** around the view target |
| Middle mouse drag | **Pan** |
| Mouse wheel | **Zoom** toward the cursor |
| `F` | **Fit All**: frame every body and outline |
| `0` / `1` / `2` / `3` | Axonometric / Top / Front / Right, with a smooth camera flight |
| Axis gizmo tip | Look straight down that axis |
| `O` | Toggle **Orthographic** / perspective |

The **grid** is a soft pool of light around where you are working that fades out
with distance and adapts its spacing as you zoom. It follows whatever plane you are
drawing on: the ground, a locked face, or the vertical plane you are looking straight
at. **View → Grid** turns it off. **Wireframe** draws bodies as edges only, so you can
see what is behind them.

### Units and dimensions

Everything is stored in **millimetres**. The unit chip (or **View → Units**) switches
between **millimetres** and **centimetres**, and every field **reads what you type in the
unit on screen**: with centimetres selected, typing `4` makes a 40 mm body.

Lengths are always formatted the same way: `340 mm`, `1,200 mm`, `18.5 mm`, sizes
as `340 × 220 × 18 mm`. Hover or select an edge and a dimension shows its length.

### Appearance

<p align="center">
  <img src="docs/images/appearance.png" alt="The Appearance panel editing the viewport and accent colours" width="820">
</p>

**View → Appearance...** (`Ctrl+Alt+A`) opens every colour in the interface for
editing: viewport, grid, accent, panels, text, hover and selection highlights, gizmo axis
colours and more, plus the font, the text size, the gizmo size and the grid density.
Changes apply **live** as you pick them, persist between sessions, and can be reset.
The default look is a near-black graphite theme with a violet accent.

### Learning the app

FurnifyMe teaches itself and then gets out of the way:

- **Guided first build.** A small panel in the corner walks a new user through
  their first body: *Press Ctrl+K to start an outline → Click at least 3 points on
  the ground → Press Enter to close the outline → Press E and give it a height.* Steps tick
  off as you actually do them.
- **Hint balloons** appear the first time a capability becomes available. Select two
  bodies and one explains that *Union combines them, Subtract cuts the second out of
  the first, Intersect keeps only the overlap.* Once you have used a feature three
  times, its hint never appears again.
- **Keyboard Shortcuts** (`?` or `F1`) lists every shortcut, grouped by menu and
  generated from the app's own commands, so it is never out of date.
- **Tooltips** on every control give the name, the shortcut and one sentence of explanation.
- **Help → Show tips again** brings the guide and hints back, which is handy for
  showing the app to someone else.

### Feedback without dialogs

The app never interrupts you with a pop-up dialog. Instead:

- **Toasts** at the bottom of the viewport report outcomes. A success
  (`Body 03 created — 600 × 400 × 18 mm`) carries an **Undo** button. A refusal
  explains what went wrong and how to fix it, and stays up longer so there is time to read it.
- **View → Show notifications** can silence success messages; refusals always show.
- The **status bar** always says what is selected and what you can do next.
- **Undo** (`Ctrl+Z`) and **Redo** (`Ctrl+Y`) cover every change to the design.

### STEP export

**File → Export STEP...** (`Ctrl+Shift+E`) writes the design as an **AP214 STEP**
file containing the exact solids, not a triangle mesh, ready for FreeCAD, Fusion,
SolidWorks, Onshape or a CAM tool.

---

## Keyboard and mouse

| Area | Key | Action |
|---|---|---|
| **Sketch** | `Ctrl+K` | Start Sketch |
| | `Enter` | Finish Sketch (close the outline) |
| | `Backspace` | Undo last point |
| | `Esc` | Cancel sketch |
| | hold `Shift` | Snap the segment to 8 directions |
| | `L` / `Shift+L` | Lock to Face / Unlock Face |
| **Model** | `E` | Extrude |
| | `Space` | Next tool (Move → Rotate → Scale) |
| | `S` | Mirror (then `X` `Y` `Z` to aim, `Enter` to confirm) |
| | `Ctrl+D` | Duplicate |
| | `Ctrl+Shift+D` | Duplicate linked |
| **Edit** | `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| | `Del` | Delete selected |
| | `F2` | Rename |
| **File** | `Ctrl+S` | Save |
| | `Ctrl+W` | Close furniture (back to the library) |
| | `Ctrl+Shift+E` | Export STEP |
| **View** | `F` | Fit All |
| | `0` `1` `2` `3` | Axonometric, Top, Front, Right |
| | `O` | Orthographic |
| | `I` | Isolate |
| | `Ctrl+Alt+S` | Items drawer |
| | `Ctrl+Alt+V` | Versions drawer |
| | `Ctrl+Alt+A` | Appearance |
| **Help** | `?` or `F1` | Keyboard Shortcuts |

| Mouse | Action |
|---|---|
| Right drag | Orbit |
| Middle drag | Pan |
| Wheel | Zoom toward cursor |
| Click | Select face or edge |
| Double-click | Select body |
| `Shift` + click / double-click | Add to selection |
| `Ctrl` + double-click a face | Lock the sketch plane to it |

---

## In progress: joinery

The `joinery` branch is turning a model into a **build plan**. Select two pieces
that touch, and FurnifyMe will find where they meet and lay out a real woodworking
joint with the numbers you mark on the wood: positions measured from a named edge,
drill depths and housing widths.

Ten joint kinds in three families:

| Family | Kinds |
|---|---|
| **Fasteners in a row** | Dowel, Pocket screw, Biscuit, Domino, Screw |
| **Housings** | Dado, Rabbet, Groove |
| **Interlocks** | Mortise & tenon, Half-lap |

Joints are a planning layer rather than cuts in the bodies. Defaults are measured
from the wood itself (for example, a dowel about a third of the thinner board's
thickness), and a joint follows its pieces when they move. A joint whose pieces no longer
touch is flagged in red instead of showing stale numbers. Contact finding, layout, the
readout, undo and the see-through hardware drawing are built. Placing, editing and
listing joints in the interface are still under way.

---

## Building from source

### Requirements

- A C++17 compiler: **MSVC 2022** on Windows, GCC or Clang on Linux
- **CMake 3.21+**
- **OpenCascade 7.6+** (developed against 8.0.1)
- **Qt 6** with Widgets and OpenGLWidgets (developed against 6.11.1)
- An OpenGL-capable GPU

### Windows (vcpkg)

Install the dependencies. Both triplet flags matter: `x64-windows-release` skips
every debug build, and a matching `--host-triplet` stops vcpkg from building a
second, full-featured Qt just for its build tools.

```powershell
C:\vcpkg\vcpkg.exe install opencascade `
  "qtbase[core,thread,gui,widgets,opengl,freetype,harfbuzz,png,jpeg,zstd,doubleconversion,pcre2]" `
  --triplet x64-windows-release --host-triplet x64-windows-release --clean-after-build
```

Then configure, build and run:

```powershell
cmake --preset windows
cmake --build --preset windows
.\build\RelWithDebInfo\furnifyme.exe
```

The presets expect vcpkg at `C:/vcpkg`. Edit `CMAKE_TOOLCHAIN_FILE` in
[CMakePresets.json](CMakePresets.json) if yours lives elsewhere.

Because the dependencies are release-only, build **RelWithDebInfo** (the preset's
default), not Debug. You still get full symbols for FurnifyMe's own code. The build
copies the Qt platform, style and JPEG plugins and the bundled wood textures next
to the executable.

### Linux

```bash
sudo apt install -y build-essential cmake \
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
  libocct-data-exchange-dev libocct-visualization-dev qt6-base-dev libgl1-mesa-dev
cmake --preset linux && cmake --build --preset linux
./build/furnifyme
```

On Wayland the app runs through XWayland (it sets `QT_QPA_PLATFORM=xcb`
automatically), because OCCT's viewer needs an X11 window.

> **Note:** Linux is a first-class target in the code and the build files, but it
> has not been built or run yet. Reports and fixes are welcome.

### Geometry only (no Qt)

The geometry library and its tests build without Qt at all, which is the fastest
loop for kernel work and what CI should run:

```bash
cmake --preset windows-headless      # or linux-headless
cmake --build --preset windows-headless
ctest --preset windows-headless
```

---

## Tests

FurnifyMe keeps geometry and interface strictly apart, and tests each on its own terms.

**Headless tests** (`ctest`) need no window and no GPU:

| Test | Covers |
|---|---|
| `headless_geometry` | Outline → face → extrude → boolean → face counts and volumes → STEP export |
| `headless_sketch_document` | Outline accumulation, the document model, undo/redo |
| `headless_camera` | Turntable orbit, projection, ray/plane maths |
| `headless_measure` | Every length and size string, unit parsing |
| `headless_progress` | The "learned after three uses" counter |
| `headless_direct_modeling` | Pull, fillets, chamfers, transforms, mirroring |
| `headless_furnify_serial` | The file format's exact shape round-trip |
| `headless_joinery` | Contact finding, joint layout and the readout |

**`gui_smoke`** drives the real application window, with more than 3,400 checks. It
sends Qt events straight to the widgets, so it never moves your mouse or steals
focus, and it blocks real input so a stray scroll cannot disturb it. It checks
behaviour end to end (picking, gizmos, booleans, undo, files, render mode) and measures
the actual pixels. It also enforces the app's **vocabulary**: no button or tooltip may say
"solid", "fuse" or "bevel" where the app's own words are *body*, *Union* and *Fillet*.

```powershell
.\build\RelWithDebInfo\gui_smoke.exe <snapshot-dir>            # the full run
.\build\RelWithDebInfo\gui_smoke.exe <snapshot-dir> <filter>   # only matching blocks
.\build\RelWithDebInfo\gui_smoke.exe --list                    # list block names
```

It needs a GPU and a desktop session, so it is not registered with `ctest`.

---

## Architecture

```
src/
├── ModelingOps          Pure geometry on OCCT: extrude, booleans, pull, fillet,
│                        chamfer, transform, mirror, primitives, STEP. Zero Qt.
├── DocumentModel        Bodies, outlines, names, mirror pairs, link groups,
│                        joints; undo/redo checkpoints.
├── SketchController     2D clicks → outline on any plane.
├── CameraController     Turntable camera maths.
├── Measure              Every user-facing length string; unit parsing.
├── Joinery              Contact finding, joint layout, readout.
├── FurnifySerial        Exact B-rep (de)serialization.
├── FurnitureStore       The on-disk library: furniture, thumbnails, versions.
├── OcctViewWidget       The Qt ↔ OCCT bridge: a QOpenGLWidget OCCT renders into;
│                        picking, gizmos, render tiers.
├── MainWindow           Actions, menus, and the single place that decides what is
│                        available.
└── ui/                  Theme, rail, drawers, gizmos, toasts, panels, library window.
```

A few rules keep it maintainable:

- **Geometry is Qt-free.** `ModelingOps` and everything in the `furnify_geometry`
  library link no Qt, so the kernel can be tested anywhere. Once those tests pass,
  every remaining bug is an interface bug.
- **Failures are values.** Operations return `{ok, shape, error}`, and a refusal
  always carries a null shape, so a failure can never be mistaken for a success.
- **Previews use the commit path.** Extrude, Pull, Fillet and Move previews are built
  by the same call that commits, so a preview can never promise something different.
- **One source of truth.** Every control mirrors a `QAction`, the shortcut sheet is
  generated from those actions, and visibility is derived from state rather than
  remembered.
- **The viewport is OCCT's own.** Tessellation, OpenGL rendering, ray and path tracing
  and topological picking all come from OpenCascade's `V3d_View` and
  `AIS_InteractiveContext`; FurnifyMe draws only its own gizmos and overlays.

---

## Project status

| Milestone | Scope | State |
|---|---|---|
| 1 | Sketch → extrude → boolean, STEP export | Built and verified on Windows |
| 2 | Direct modeling: pull, fillet, chamfer, transform | Merged |
| 3 | Library, autosave, versions & compare, live mirror, render mode | Merged |
| 4 | Separate library window, placed mirror plane, linked copies, render tiers | Merged |
| 5 | Add shape, Magnet, Isolate, wood materials, studio panel, gallery, custom title bar | Merged |
| — | Joinery | In progress |

**Not yet verified:** building and running on **Linux**, and opening an exported STEP
file in **FreeCAD**. The exported file is structurally correct AP214, but until
both are confirmed Milestone 1 cannot honestly be called complete on both platforms.

**Deliberately out of scope for now:** a parametric history tree, a constraint
solver, 2D drawings, assemblies, and file formats other than STEP.

---

## License

No license has been chosen yet. Until one is added, all rights are reserved by the
author.
