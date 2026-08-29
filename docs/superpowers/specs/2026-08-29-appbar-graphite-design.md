# Phase 5: Graphite × App Bar — design

Date: 2026-08-29
Status: approved in chat after two rounds of HTML mockups

Phase 5 of the UX overhaul. Phases 1–4 are merged. This phase restyles and restructures
the shell; it adds no modelling capability.

## How the decisions were made

The user asked for a whole-app restyle with HTML examples to choose from. Round one
offered four palettes rendered as full mockups of the shell; the user chose **A —
Graphite** (today's cool charcoal and blue, finished properly). Round two offered four
structural arrangements, all wearing Graphite; the user chose **H — The App Bar**. The
mockup page is the visual contract for this phase: the artifact renders both rounds, and
the final result is verified against mockup H by side-by-side capture.

## Goal

The same app, in the same colours, composed like a product: one top bar carrying the
wordmark, menus and view controls; one slim icon rail carrying the tools; a floating
items drawer; a full-bleed viewport. Underneath it, the Graphite polish: chips with a
real anatomy, floating surfaces that read as one family, a grid with one more step of
contrast.

## Architecture

### The app bar — new, `src/ui/AppBar.{h,cpp}`

`QMainWindow::setMenuWidget` accepts any widget for the menu strip. The app bar is that
widget: a horizontal container holding, left to right —

1. **The wordmark** — `▰ FurnifyMe`, the glyph in accent, painted text.
2. **The real `QMenuBar`**, unchanged. Menus, shortcuts, the generated shortcut sheet
   and the vocabulary sweep all keep working untouched, because the menu bar is still a
   `QMenuBar`; it merely lives inside the bar.
3. A stretch.
4. **The view label button** — `Persp` / `Top` / `Front` / …, moved out of the viewport.
   Clicking it snaps to Axonometric exactly as the gizmo's label chip did, records
   `view.changed` through the same `recordViewChanged()` route, and its text updates on
   `cameraChanged`. There is **one** source for that label text; the gizmo's painted
   label chip is removed rather than duplicated.
5. **The unit chip** — `mm` / `cm`, moved out of the viewport; clicking cycles the unit
   through the same actions the View menu owns.
6. **Wireframe** and **Fit All** as compact bordered buttons, mirroring their actions.

`Save Screenshot` leaves the viewport clusters and becomes menu-only — mockup H shows no
screenshot control in the bar, and it is the least-used of the three.

The 3D axis gizmo (the painted axes) **stays** in the viewport's top-right corner. Only
its label chip and the unit chip move up into the bar.

### The rail — icon-only tools on the left edge

One vertical rail pinned to the viewport's left edge, replacing all four floating chip
clusters. Top to bottom:

| Group | Buttons |
|---|---|
| drawer | Items toggle |
| sketch | Start/Cancel Sketch · Extrude |
| model | Union · Subtract · Intersect · Delete Selected |
| select | Snap to Grid · Select Bodies · Select Faces · Select Edges |
| history (bottom) | Undo · Redo |

Every button is the existing `QAction`, rendered icon-only through `IconSet`'s painted
glyphs at rail size; the label and shortcut move into the tooltip, which every action
already carries. The menus keep every command labelled and discoverable, so the rail can
afford to be terse. Checked state is the Graphite inset accent ring; disabled dims glyph
and all.

### The items drawer

`ItemsPanel` stops docking and floats as a Graphite card beside the rail, toggled by the
existing Items action (`Ctrl+Alt+S`), anchored through `ViewportOverlay` so the toast
and balloon step around it for free via `occupiedRects()`. Content, rows, visibility
eyes, dims — unchanged. The viewport becomes full-bleed.

### The Graphite polish — `Theme` and the widgets that paint

- **Tokens:** `chip` `#2b2b2e → #2c2c31`; `gridMinor → #3e3e44`; `gridMajor → #4d4d55`.
  Everything else keeps its value — the palette was chosen because it is already right.
- **Chip anatomy:** visible 1px `border()` on every chip; hover keeps `chipHover()`;
  checked is `chipActive()` fill **plus an inset accent ring**; disabled dims label,
  glyph and shortcut badge together. A soft shadow is painted inside a slightly enlarged
  widget rect — a real drop shadow cannot escape a widget's bounds over the GL surface.
- **One floating-surface family:** panel background, 1px border, 8px radius, painted
  shadow — implemented once as a shared `Theme` paint helper and used by the drawer, the
  rail, the guide, the balloon, the toast, the shortcut sheet and the bar's buttons, so
  the family cannot drift widget by widget.

## What deliberately does not change

Menus and every shortcut. All copy — Phase 1's words are settled. The status bar. The
guide, balloon and toast (their positions adapt via `occupiedRects()`, their behaviour
is Phase 2/3's). Every viewport hue: sketch yellow, marker magenta, cursor blue, focus
amber, danger red, dimension blue, axis tints. The camera. The grid's step logic — only
its two greys move. `furnify_geometry` is untouched.

## Non-goals

No new commands, no new panels beyond the drawer, no theming beyond Graphite (the unit
chip cycles units; there is no theme switcher). No blur, no translucency — the GL
surface composites plain opaque children only, and the mockups were drawn within that
constraint.

## Testing

- The existing suite (426 checks) keeps passing, with the checks that assert the old
  arrangement — docked panel geometry, cluster hit-tests, the gizmo's label chip —
  **updated to assert the new one, never deleted**. The count may not go down.
- New checks: the bar exists and holds the real `QMenuBar`; the view label button snaps
  and records through the same route the gizmo label did; the unit chip cycles; rail
  buttons are reachable by real `childAt()` hit-testing and mirror their actions'
  enabled/checked state; the drawer toggles with the Items action and is stepped around
  by the toast; chip states discriminate visually (grab-compare, the focus-ring
  pattern); the vocabulary sweep reaches any new painted text.
- Final gate: `PrintWindow` capture of the running app beside mockup H, cropped at
  bar/rail/drawer level. A visible mismatch is a failed check.

## Acceptance criteria

1. The menu strip is the app bar: wordmark, working menus, view label, unit chip,
   Wireframe, Fit All — and the viewport's top edge holds only the axis gizmo.
2. All four floating clusters are gone; every one of their commands is reachable from
   the rail with correct enabled/checked/disabled rendering, and from the menus.
3. The items panel floats, toggles with its existing action and shortcut, and the
   viewport is full-bleed.
4. Chips and floating surfaces share one painted family: border, radius, shadow, inset
   checked ring, honest disabled state.
5. Nothing settled in Phases 1–4 regresses: words, teaching surfaces, toasts, units,
   dimensions, face locking, markers.
6. Headless suite 5/5 and the full `gui_smoke` suite pass; the side-by-side against
   mockup H is presented before merge.
