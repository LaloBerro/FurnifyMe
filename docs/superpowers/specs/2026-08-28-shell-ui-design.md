# Shell UI redesign — design

Date: 2026-08-28
Status: implemented 2026-08-28

## Goal

Replace FurnifyMe's conventional menu-and-toolbar shell with a dark, flat shell in the
visual language of Shapr3D: floating tool clusters over an edge-to-edge viewport, a left
Items panel with per-solid visibility, and view controls anchored to the right edge.

The reference screenshot supplied by the user is a Shapr3D window. **We are copying the
UI style and layout grammar, not the feature set.**

## Non-goals

Deliberately excluded, because we have nothing behind them and dead buttons are worse than
absent ones:

- Modeling / Visualization / Drawings mode switches — we have one mode.
- History panel — Milestone 1 has no history tree, and the brief defers it.
- Share, Upgrade, account UI.
- Measure and Section View — no such tools yet.
- A unit system. The readout is static `mm`.

Also out of scope: changing any modelling behaviour. This is a shell change. `ModelingOps`
is untouched.

## Settled by probe

A throwaway probe (`tests/overlay_probe.cpp`, since deleted) placed two `QLabel` chips over
`OcctViewWidget` — one with `WA_NativeWindow`, one an ordinary child — and churned the
camera to force repeated OpenGL redraws.

**Both stayed crisp; no flicker, no z-order loss.** Qt 6 on Windows composites children of a
`WA_PaintOnScreen` widget correctly, so `WA_NativeWindow` is not required. Floating overlays
are therefore viable and the side-panel fallback is not needed.

This was the one genuine unknown; everything below assumes it.

## Architecture

New `src/ui/` module, all of it inside the existing `furnify_app` library so `gui_smoke` can
drive it:

| Component | Responsibility |
|---|---|
| `Theme` | Loads the stylesheet resource, exposes the colour tokens, applies the app palette |
| `ToolChip` | One button, built **from a `QAction`**: icon, label, shortcut badge, states |
| `ToolCluster` | A titled stack of `ToolChip`s with consistent spacing |
| `ViewportOverlay` | Transparent child of `OcctViewWidget`; anchors clusters, re-lays out on resize |
| `ItemsPanel` | Left list of solids: name, eye toggle, selection sync |
| `IconSet` | Resolves named icons, tinted per state |

### The action-binding rule

Every chip is constructed from an existing `QAction` and never duplicates its state.
`ToolChip` connects to the action's `changed()` signal and mirrors `isEnabled()`,
`isChecked()`, `text()` and `shortcut()`.

This matters for three reasons: menu items, chips and keyboard shortcuts cannot drift apart;
enable/disable logic in `updateActions()` needs no changes at all; and `gui_smoke`'s
`findChildren<QAction*>` lookup keeps working untouched, so the existing 23 GUI checks
survive the redesign and act as its regression net.

The menu bar is **kept**, slimmed and themed. It is the discoverability fallback and costs
almost nothing once actions are the shared source of truth.

## Theme tokens

| Token | Value | Use |
|---|---|---|
| `chrome` | `#1b1b1d` | Menu bar, status bar, window background |
| `panel` | `#232326` | Items panel |
| `chip` | `#2b2b2e` | Chip background |
| `chip-hover` | `#34343a` | Hover |
| `chip-active` | `#3d3d45` | Pressed / checked |
| `accent` | `#3d7eff` | Checked state accent, focus ring |
| `text` | `#f0f0f0` | Labels |
| `text-muted` | `#9a9aa2` | Shortcut badges, secondary labels |
| `text-disabled` | `#5c5c64` | Disabled chips |
| `border` | `#3a3a40` | Separators, chip outline |
| `viewport` | `#45454b` | OCCT background, set via `SetBackgroundColor` |

The viewport is deliberately lighter than the chrome, as in the reference: the model reads
against mid-grey, the chrome recedes.

## Overlay layout

Clusters anchor to viewport edges with a 16px margin, 8px between clusters, 4px between
chips. Positions recompute in `ViewportOverlay::resizeEvent`.

| Anchor | Cluster | Chips |
|---|---|---|
| Top-left | Panels | Items (toggles the panel) |
| Left, centred | Tools | Sketch, Extrude, Fuse, Cut, Intersect, Delete |
| Bottom-left | Options | Snap to Grid, Select Solids, Select Faces |
| Top-right | Orientation | `AIS_ViewCube`, static `mm` readout |
| Right, below cube | View | Display Mode, Screenshot, Fit All |
| Menu bar corner | History | Undo, Redo |

Chips consume mouse events; the viewport keeps everything else, so an orbit drag beginning
outside a chip behaves exactly as now.

## Items panel

A `QWidget` in a `QSplitter` to the left of the viewport — not a `QDockWidget`, which brings
float/close chrome we do not want and is awkward to style. Collapsed and expanded by the
Items chip.

Each row: solid name, and an eye button toggling visibility.

Supporting changes:

- **Naming.** `DocumentModel::Solid` gains `std::string name`. `addSolid` auto-names
  `Body 01`, `Body 02`, … from a monotonic counter that, like ids, is never rolled back by
  undo. `renameSolid(id, name)` is available but no UI exposes it yet.
- **Visibility** lives in `OcctViewWidget` (`setSolidVisible(id, bool)` / `isSolidVisible(id)`),
  implemented with `AIS_InteractiveContext::Display`/`Erase`. It is presentation state, not
  document state, so it is *not* captured by undo snapshots and does not belong in
  `DocumentModel`.
- **Change notification.** `MainWindow` gains `signals: void documentChanged()`, emitted
  after every mutation (extrude, boolean, delete, undo, redo). `ItemsPanel` rebuilds from it.

**`DocumentModel` stays free of Qt.** Making it a `QObject` to emit its own signals would
drag Qt into `furnify_geometry`, which is exactly what keeps the headless tests buildable
with no Qt at all. `MainWindow` already knows every mutation point, so it does the
announcing.

Selection syncs both ways: clicking a row selects that solid in the viewport; viewport
selection highlights the corresponding rows.

## Icons

15 monochrome glyphs: sketch, extrude, fuse, cut, intersect, delete, undo, redo, items,
snap, select-solid, select-face, display-mode, screenshot, fit.

**Open decision — needs the user's answer before implementation.** Qt's SVG support lives in
the separate `qtsvg` module, which is *not* part of `qtbase`, and our vcpkg install is
qtbase-only.

1. **Add `qtsvg` via vcpkg** (recommended). Icons are editable `.svg` files under
   `resources/icons/`, compiled in as Qt resources. Small module, builds quickly against the
   existing qtbase. Cost: one more dependency install on the user's machine, which needs
   their approval.
2. **Draw glyphs with `QPainter` in code.** No new dependency, perfect at any DPI, recolours
   trivially per state. Cost: the icons live in C++ rather than as editable assets, and
   tweaking one means editing code.

If the user declines the extra install, option 2 is a genuine fallback rather than a
degradation — these are simple geometric glyphs.

**Default if unanswered: option 2.** Implementation must not stall on this, and must not
install anything on the user's machine without being asked.

## Testing

- **Headless** (`tests/sketch_document.cpp`): auto-naming sequence, no name reuse after
  removal or undo, `renameSolid`, and that names survive undo/redo snapshots.
- **`gui_smoke`**: a chip's enabled state mirrors its action; `ItemsPanel` row count tracks
  the document across extrude/delete/undo; the eye toggle flips `isSolidVisible`; clicking a
  row selects that solid. The existing 23 checks must keep passing unchanged — that is the
  main guard against the redesign breaking behaviour.
- **Visual**: `saveSnapshot` covers the viewport only, so overlay appearance still needs a
  window grab. One capture per layout change, reviewed by eye.

## Risks

- **`AIS_ViewCube` on OCCT 8.0.1** — present in `TKV3d` historically, but unverified here.
  If it is missing or awkward, fall back to a six-button orientation chip cluster, which we
  can already implement with the existing standard-view actions.
- **Resize and DPI.** The probe covered a static window. Overlay anchoring under live resize
  and DPI change needs explicit checking.
- **Stylesheet reach.** `QMenuBar` and `QStatusBar` style cleanly; the `AIS_ViewCube` is
  drawn by OCCT and will not obey the stylesheet, so its colours are set through OCCT's own
  API and must be matched to the tokens by hand.

## Acceptance criteria

1. Dark theme applied consistently across menu bar, panel, chips, status bar and viewport.
2. Tool clusters float over the viewport at the anchors above, without flicker during orbit.
3. Every chip drives the same `QAction` as its menu item, with matching enabled state.
4. Items panel lists every solid, names them `Body NN`, and its eye toggles hide and show.
5. Selection syncs both directions between panel and viewport.
6. View cube (or fallback cluster) orients the camera.
7. Headless suite still passes; `gui_smoke`'s existing checks still pass, plus the new ones.
8. Clean configure and build from scratch.
