---
name: FurnifyMe
description: "Tinted glass capsules float with a specular rim over a full-bleed near-black workshop, and violet light pools only at the shell's edges."
colors:
  surface: "#0B0912"
  surface-raised: "#14101C"
  viewport: "#1C1C1E"
  glass-tint: "#1A122C"
  text: "#F4F1FB"
  text-muted: "#B4ABC8"
  text-quiet: "#8E86A3"
  accent: "#6A00FF"
  on-accent: "#FFFFFF"
  accent-text: "#BB9CFF"
  glow-1: "#5A12E0"
  glow-2: "#3B2BD6"
  glow-3: "#9B3DF5"
  hover: "#06D1FF"
  selected: "#FFA500"
  sizes: "#6FB6FF"
  body: "#B3B3B3"
  body-edge: "#4D4D4D"
  grid-minor: "#2D2D31"
  grid-major: "#35353B"
  danger: "#E0564A"
  danger-text: "#FF8F82"
  caution: "#FFCA4A"
  axis-x: "#E0564A"
  axis-y: "#7FC84E"
  axis-z: "#4A80E0"
typography:
  badge:
    fontFamily: "DM Sans"
    fontSize: "12px"
    fontWeight: 700
    lineHeight: "1"
  label:
    fontFamily: "DM Sans"
    fontSize: "13px"
    fontWeight: 600
    lineHeight: "1.3"
  body:
    fontFamily: "DM Sans"
    fontSize: "14px"
    fontWeight: 500
    lineHeight: "1.45"
  title:
    fontFamily: "DM Sans"
    fontSize: "17px"
    fontWeight: 700
    lineHeight: "1.25"
  wordmark:
    fontFamily: "DM Sans"
    fontSize: "17px"
    fontWeight: 800
    lineHeight: "1"
radii:
  capsule: "999px"
  panel: "26px"
  card: "20px"
  chip: "14px"
  tag: "8px"
spacing:
  s-1: "4px"
  s-2: "8px"
  s-3: "12px"
  s-4: "16px"
  s-5: "24px"
  s-6: "32px"
style:
  locked: true
  name: Violet Glass Float
  chosen: 2026-10-11
  source: dipsynator style
themes:
  - id: violet-glass-float
    name: "Violet Glass Float"
    role: default
    from: "four directions page, B"
    tokens: top-level colors, typography, radii and spacing above
  - id: aurora-mesh-gradient
    name: "Aurora / Mesh Gradient"
    role: theme
    from: "ten styles page, C"
    tokens:
      colors:
        surface: "#0B0B1A"
        surface-raised: "#15152B"
        viewport: "#07070F"
        text: "#F5F5FA"
        text-muted: "#A8A8C0"
        text-faint: "#7C7C98"
        line: "#2A2A44"
        accent: "#A78BFA"
        on-accent: "#1A1033"
        accent-text: "#C4B5FD"
        aurora-violet: "#7C3AED"
        aurora-blue: "#2563EB"
        aurora-cyan: "#22D3EE"
        aurora-pink: "#F472B6"
        aurora-mint: "#A7F3D0"
        body: "#B3B3B8"
        selection: "#B9A4FB"
        sizes: "#67E8F9"
        caution: "#FBBF24"
        danger: "#FB7185"
        axis-x: "#F87171"
        axis-y: "#86EFAC"
        axis-z: "#60A5FA"
      typography:
        title:
          fontFamily: "Sora"
          fontSize: "26px"
          fontWeight: 300
          lineHeight: "1"
        body:
          fontFamily: "Outfit"
          fontSize: "13px"
          fontWeight: 400
          lineHeight: "1.4"
        label:
          fontFamily: "Outfit"
          fontSize: "12px"
          fontWeight: 500
          lineHeight: "1.3"
        badge:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "11px"
          fontWeight: 600
          lineHeight: "1"
        wordmark:
          fontFamily: "Sora"
          fontSize: "13px"
          fontWeight: 600
          lineHeight: "1"
        measure:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "12px"
          fontWeight: 600
          lineHeight: "1"
      radii:
        card: "20px"
        chip: "12px"
        control: "10px"
        pill: "999px"
      spacing:
        s1: "4px"
        s2: "8px"
        s3: "12px"
        s4: "16px"
        s5: "24px"
        s6: "32px"
  - id: nightshade-glass
    name: "Nightshade Glass"
    role: theme
    from: "ten styles page, G"
    tokens:
      colors:
        surface: "#08060E"
        surface-raised: "#110D1B"
        surface-sunk: "#0D0A16"
        glass: "#1A132C"
        accent-wash: "#2A1F48"
        text: "#F3F0FA"
        text-soft: "#DCD6EA"
        text-muted: "#B1A9C6"
        text-disabled: "#5E5773"
        accent: "#A57BFF"
        accent-text: "#C9B1FF"
        on-accent: "#08060E"
        glow: "#7A3FE6"
        cyan: "#5EE4F0"
        caution: "#F4C783"
        danger: "#FF5C8A"
        danger-text: "#FF9DB3"
        viewport: "#0A0812"
        grid-minor: "#1C1828"
        grid-major: "#2B2542"
        body: "#B3B3B3"
        body-edge: "#4D4D52"
        sizes: "#8EC5FF"
        gizmo-x: "#E0564A"
        gizmo-y: "#7FC84E"
        gizmo-z: "#4A80E0"
      typography:
        title:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "16px"
          fontWeight: 700
          lineHeight: "1.25"
        body:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "13px"
          fontWeight: 500
          lineHeight: "1.45"
        label:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "12px"
          fontWeight: 600
          lineHeight: "1.35"
        badge:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "11px"
          fontWeight: 700
          lineHeight: "1"
        wordmark:
          fontFamily: "Plus Jakarta Sans"
          fontSize: "13px"
          fontWeight: 800
          lineHeight: "1"
      radii:
        panel: "14px"
        control: "10px"
        row: "9px"
        badge: "7px"
        pill: "999px"
      spacing:
        s-1: "4px"
        s-2: "8px"
        s-3: "12px"
        s-4: "16px"
        s-5: "24px"
        edge-margin: "12px"
        stack-gap: "8px"
  - id: ultraviolet-grid
    name: "Ultraviolet Grid"
    role: theme
    from: "ten styles page, H"
    tokens:
      colors:
        surface: "#030305"
        surface-raised: "#0B0B10"
        surface-sunken: "#08080C"
        surface-hover: "#14131C"
        viewport: "#030305"
        text: "#F5F4FA"
        text-soft: "#CFCDDA"
        text-muted: "#A9A7B8"
        text-disabled: "#5A5866"
        rule: "#2A2933"
        rule-faint: "#1C1B24"
        rule-strong: "#3B3A47"
        rule-hi: "#5A5866"
        accent: "#9466FF"
        accent-text: "#B79BFF"
        accent-dim: "#5E4A9E"
        on-accent: "#030305"
        danger: "#FF6B81"
        body: "#B3B3B3"
        body-edge: "#4D4D4D"
        grid-minor: "#17161E"
        grid-major: "#26252F"
      typography:
        body:
          fontFamily: "Inter Tight"
          fontSize: "14px"
          fontWeight: 400
          lineHeight: "24px"
        small:
          fontFamily: "Inter Tight"
          fontSize: "13px"
          fontWeight: 400
          lineHeight: "24px"
        title:
          fontFamily: "Inter Tight"
          fontSize: "16px"
          fontWeight: 600
          lineHeight: "24px"
        heading:
          fontFamily: "Inter Tight"
          fontSize: "32px"
          fontWeight: 600
          lineHeight: "40px"
        kpi-numeral:
          fontFamily: "Inter Tight"
          fontSize: "32px"
          fontWeight: 500
          lineHeight: "32px"
        label-mono:
          fontFamily: "IBM Plex Mono"
          fontSize: "12px"
          fontWeight: 500
          lineHeight: "16px"
      radii:
        none: "0px"
        all: "0px"
      spacing:
        s-1: "4px"
        s-2: "8px"
        s-3: "16px"
        s-4: "24px"
        s-5: "32px"
        s-6: "48px"
        s-7: "64px"
  - id: graphite
    name: "Graphite"
    role: theme
    from: "today's shipped look"
    tokens:
      colors:
        chrome: "#1B1B1D"
        panel: "#232326"
        chip: "#2C2C31"
        chip-hover: "#34343A"
        chip-active: "#3D3D45"
        accent: "#6A00FF"
        accent-text: "#BB9CFF"
        danger: "#E0564A"
        danger-text: "#FF8F82"
        caution: "#FFCA4A"
        text: "#F0F0F0"
        text-muted: "#9A9AA2"
        text-disabled: "#5C5C64"
        border: "#3A3A40"
        viewport: "#1C1C1E"
        body: "#B3B3B3"
      typography:
        badge:
          fontFamily: "DM Sans"
          fontSize: "10.667px"
          fontWeight: 500
          lineHeight: "1"
        label:
          fontFamily: "DM Sans"
          fontSize: "12px"
          fontWeight: 500
          lineHeight: "1.3"
        body:
          fontFamily: "DM Sans"
          fontSize: "13.333px"
          fontWeight: 400
          lineHeight: "1.4"
        title:
          fontFamily: "DM Sans"
          fontSize: "17.333px"
          fontWeight: 600
          lineHeight: "1.25"
      radii:
        card: "10px"
        surface: "8px"
        chip: "6px"
      spacing:
        tight: "8px"
        snug: "12px"
        wide: "24px"
---
# FurnifyMe design contract

Locked 2026-10-11 by `dipsynator style`. **Violet Glass Float is the default style**; four more whole styles ship beside it as themes the user can switch between in Settings. Until a screen is restyled it keeps its current look (Graphite).

## What a theme is

A theme is a **whole style**, not a palette: its colours, typefaces, type scale, radii, spacing, surfaces (glass, flat, sharp), shadows and signature elements all change together. Every theme is a complete, self-consistent style; nothing is mixed across themes at run time.

- **Where it applies.** Fully, on every surface drawn by the HTML layer (the hub, the toasts and status strip, the Settings card, and each card as it moves to HTML). On the surfaces still drawn by Qt, and in the 3D viewport, a theme applies its **colours** through `Theme::Spec` (the viewport, grid, hover, selection, sizes and axis inks included); shapes, glass and typefaces reach a Qt surface only when it moves to HTML.
- **Fonts ship with the app.** Every theme's typefaces are bundled (Google Fonts, OFL): DM Sans, Sora, Outfit, Plus Jakarta Sans, Inter Tight, IBM Plex Mono. A page never fetches a font from the network.
- **The user's own colour edits** (Settings, Colours tab, Save/Load colours) stay: they adjust the active theme's colours and are saved with it.

## Rules every theme keeps

These are product rules, not style choices; a theme that contradicts one loses that part of itself.

- No keyboard focus ring anywhere; a field being edited marks itself with its accent border.
- No modal dialogs; outcomes are one toast at a time, with Undo for a document change.
- Words on dark surfaces use the accent-text ink at 4.5:1 or better; the accent itself is a fill and a stroke.
- The bodies are a neutral grey while modelling; glow, glass and gradients never sit behind a body, a dimension number or body text.
- Hover lives only under the cursor; selection, sizes and the three axis inks stay mutually distinct.
- The vocabulary law (CLAUDE.md) holds in every theme's copy.
- Animated effects (aurora drift, glow) stop under reduced motion.

## Theme decisions (2026-10-11)

- **Type scale: a theme keeps its own extra sizes, on named spots only.** The four-size scale (badge, label, body, title) is the rule everywhere; a theme may add sizes only for the surfaces its style names - Aurora's 26px card title, Ultraviolet Grid's 32px heading and its 32px measured numerals with mono units. No other surface takes an extra size.
- **Viewport inks are the theme's.** A theme may recolour the 3D view's state inks - selection, hover, size lines, axes - (Aurora's lavender selection, for one). Within one theme they stay mutually distinct, hover still appears only under the cursor, and each theme's inks are checked against its own viewport and body colours.
- **Ultraviolet Grid's sharp corners** reach a Qt surface only once it moves to HTML; until then that theme looks half-applied on the rail and drawers.

## Violet Glass Float (default)

*Capsule controls floating like glass over a glowing, full-bleed workshop*  
From: four directions page, B. Built on: Liquid Glass (Apple 2025) / Spatial UI, Nightshade Glass, Aurora / Mesh Gradient.

**Do**

- Put every floating control (pill, rail, drawers, gizmo card, toast) on .glass: violet-tinted blur with the bright top-left specular rim and a neutral offset lift shadow
- Use capsule and concentric organic shapes: pills at 999px, panels at 26px, chips at 14px, the radius always half the height on a pill
- Keep labels heavy (600 for rows and menus, 700 for titles and badges) on DM Sans's four sizes 12/13/14/17 so they read through translucency
- Use #6A00FF only as a fill (checked chip, primary button, selection wash) and #BB9CFF for accent words and marks
- Keep viewport inks exactly as tokens: cyan #06D1FF for hover only, orange for selection, #6FB6FF for size lines, axis reds/greens/blues for gizmos
- Let aurora glow (blurred violet blobs plus fine grain) sit at the window's corners and edges, behind the shell

**Don't**

- Never paint a keyboard focus ring; a field being edited marks itself with the accent-text border instead
- Never put glow or glass behind numbers, dimension labels or the 3D bodies; the scene centre stays dark and legible
- No coloured or zero-offset glow shadows; depth comes from offset neutral shadows and the specular rim
- Do not use cyan anywhere at rest; it appears only under the cursor
- Do not add a fifth type size, a second typeface or monospace costume for numbers; use DM Sans tabular figures
- No modal scrims or dialog cards for outcomes; one glass toast at a time, with Undo for a document change

**Signature elements**

- floating detached glass capsule (liquid-glass): The app-bar pill, the window-button capsule, the rail and the view-control cluster all float as rounded capsules, detached from the window edges
- specular edge highlight (liquid-glass): Every .glass surface (pill, rail, Items drawer, gizmo card, toast) carries a 1.2px rim that is brightest at the top-left and catches again at the bottom-right
- full-bleed album art (liquid-glass): Reinterpreted as the full-bleed 3D viewport: the cabinet, grid and dimension lines run edge to edge under every control, to the window's own top edge
- concentric rounded controls (liquid-glass): The rail's 14px-radius chips inside its 29px capsule, and the drawer's 14px rows inside its 26px panel, keep concentric radii
- frosted overlay panels (nightshade-glass): The Items drawer and the axis-gizmo card are frosted, violet-tinted blur panels over the live scene
- deep violet ambient glow (nightshade-glass): Blurred violet pools sit behind the shell at the top-left (behind pill and rail), bottom-left and right edges; the scene centre stays dark
- thin light panel borders (nightshade-glass): Glass-edge hairlines on the filter field, badges, Undo pill and the status bar's top edge
- cyan focus rings (nightshade-glass): Adapted to the product's no-focus-ring rule: cyan appears only as hover, on the Top board's hovered edge in the viewport and on chip/button hover borders
- mesh gradient background (aurora-mesh-gradient): Four neighbouring hues (violet #5A12E0, indigo #3B2BD6, orchid #9B3DF5) blended into soft fields at the shell's corners
- film grain overlay (aurora-mesh-gradient): A fractal-noise grain layer at 22% overlay sits over the whole window, so the glow reads as light rather than a flat gradient
- crisp minimal controls on glow (aurora-mesh-gradient): The rail chips and window buttons stay crisp, stroked icons where they sit directly over the brightest top-left glow

## Aurora / Mesh Gradient

*Large, soft, blended clouds of luminous color - often on dark -*  
From: ten styles page, C. Built on: Aurora / Mesh Gradient.

**Do**

- Keep the mesh gradient at the viewport's edges and corners, behind the glass cards, and always lay the radial calm zone over the middle so the furniture and its dimension labels sit on near-black.
- Make every overlay card dark glass (--glass with a 20px backdrop blur) with a 20px radius and an offset soft shadow, so the aurora tints it from behind without ever sitting under text.
- Draw controls crisp on the glow: 40px chips with a 1px white ring, an icon in one 1.6 stroke weight, solid white for the one checked chip, and a faint ring with --text-faint icons when disabled.
- Use Sora 300 for one large airy title per card (26px), Outfit for everything else, and Plus Jakarta Sans for every number so sizes line up as tabular figures.
- Lay the film grain over the whole window at about 0.3 overlay opacity, and keep the aurora's drift slow (tens of seconds) and switched off under reduced motion.
- Keep the product's own signals as distinct hues: lavender only for state (selection, the checked row, the primary pill), cyan for sizes, amber for the unsaved dot, and the three axis colours for gizmos.

**Don't**

- Never let a saturated aurora blob sit directly behind body text, a dimension label or the selected body. If it would, move the blob or deepen the calm zone.
- No gradient text, no glowing zero-offset halos around controls, and no coloured border-left on rows. The selected row gets a 3px rounded marker inside its own rectangle.
- Do not add a fifth font size or a second display face. The scale is 11/12/13/26 and nothing else.
- No focus rings. A field being edited marks itself with the accent border instead.
- Do not tint the modelling bodies with the aurora. They stay the neutral body grey, and only the selection turns lavender.
- Never use the accent for words below 4.5:1. On dark surfaces the words take --accent-text (#C4B5FD), and the accent itself is kept for fills and strokes.

**Signature elements**

- mesh gradient background (aurora-mesh-gradient): Viewport backdrop. Blurred violet, blue, cyan, pink and mint clouds sit at the viewport's corners and edges, behind the rail, the Items drawer, the axis-gizmo card and the view cluster. A radial calm zone keeps the cabinet and its dimension labels on near-black.
- film grain overlay (aurora-mesh-gradient): One fractal-noise SVG laid over the whole 1280x800 window in overlay blend at 0.3 opacity. It grains the aurora, the glass chrome and the viewport together.
- large airy title (aurora-mesh-gradient): The Items drawer heading, set in Sora Light 26px with tight tracking, and the round + (New Folder with These) control beside it.
- crisp minimal controls on glow (aurora-mesh-gradient): The left rail's ten chips and the top-right view cluster, which have thin white rings over the violet and cyan glow. Items, the checked chip, is solid white, the same move as the sample's play button. The toast's Undo is the single solid lavender pill.

## Nightshade Glass

*Restrained glass: translucent overlays and side panels over a deep violet glow,*  
From: ten styles page, G. Built on: Nightshade Glass.

**Do**

- Use frosted glass (blur 26px, 50% violet-black fill, 1px rgba(236,228,255,.14) edge, top inner highlight) only on cards that float over the live viewport: the pill, rail, drawers, gizmo card, view cluster and toast.
- Put the violet ambient glow behind the shell at the viewport's edges (top-left behind the rail and drawer, right behind the gizmo, bottom behind the toast) and keep the centre of the scene dark so bodies read cleanly.
- Mark selection and live state with the violet accent family: a tinted fill plus a thin lilac edge, and a 3px rounded accent pill inside the selected row; counts of a partial selection in accent-text.
- Keep the type to four sizes of Plus Jakarta Sans (11/12/13/16) with weight steps doing the hierarchy, tabular numerals everywhere a size or count appears.
- Keep cyan for the viewport hover highlight only, and let a field being typed in mark itself with an accent border instead of a ring.
- Keep the status bar and any list read in bulk on solid surfaces (surface-sunk / surface-raised), never behind blur.

**Don't**

- Do not paint keyboard focus rings anywhere, cyan included: the product removed them on the author's explicit request.
- Do not let the glow or glass tint the bodies or the dimension labels; the wood and its numbers must stay neutral and legible.
- Do not use gradient text, colored side borders thicker than 1px, or hard zero-blur offset shadows.
- Do not add a fifth type size or a second typeface; the wordmark is the same face at 800.
- Do not redraw or substitute the app mark; it is the author's artwork shown as an image in the pill.
- Do not frost a card nested inside another glass card; one layer of glass per overlay.

**Signature elements**

- frosted overlay panels (nightshade-glass): Every card floating over the viewport: the app-bar pill, the left tool rail, the Items drawer, the window buttons, the axis-gizmo card, the 3-chip view cluster and the Note toast (blur 26px, saturate 150%, violet-black fill with a top inner highlight)
- deep violet ambient glow (nightshade-glass): Behind the shell at the viewport's edges: top-left behind the pill, rail and Items drawer; right edge behind the gizmo card and view cluster; bottom centre behind the toast, leaving the cabinet's centre dark
- thin light panel borders (nightshade-glass): 1px rgba(236,228,255,.14) edges on every glass card, the filter field and the Undo pill; a softer edge on the status bar's top rule and between its three channels
- cyan focus rings (nightshade-glass): Not painted as focus rings (the product removed them at the author's request); the style's cyan is kept as the viewport hover highlight, outlining the hovered outer face of the right side panel

## Ultraviolet Grid

*Strict 8-point grid analytics: big KPI numerals, hairline tables and no decoration*  
From: ten styles page, H. Built on: Ultraviolet Grid.

**Do**

- Put every position and size on the 8-point grid, and mark overlay card corners with the hairline registration crosshair so the grid stays visible.
- Give the furniture's real numbers the oversized treatment: 32px Inter Tight tabular numerals with a small mono unit, boxed in a 1px ultraviolet frame on the dimension line.
- Separate list rows with 1px hairlines and head lists with an IBM Plex Mono uppercase column row; mark selection with the 4px white bar and a #14131C fill.
- Spend ultraviolet only on data (dimension lines, partial-selection counts like 1/4) and on the single primary action, such as a toast's Undo.
- Keep every shape sharp: 0px radius on cards, chips, fields, toasts and badges, separation by 1px borders rather than shadows.
- Use mono uppercase for labels, axes, units and counts; use Inter Tight for names, menus and sentences.

**Don't**

- Don't round anything, and don't add drop shadows, glows, gradients or texture to cards.
- Don't use ultraviolet for selection outlines, chrome, icons or decoration; selection is white.
- Don't paint focus rings; a field being edited marks itself with an ultraviolet border instead.
- Don't set an off-grid gap; every padding, margin and offset is a multiple of 8, with 4 only inside a glyph.
- Don't use mono for prose or for user-typed names; it is for numbers, units and labels only.
- Don't scale numerals in the status bar or rows; oversized numerals belong to the measured sizes alone.

**Signature elements**

- oversized KPI numerals (ultraviolet-grid): The selected Top board's three dimension labels in the viewport: 32px Inter Tight tabular numerals (600 / 300 / 18) with a mono MM unit and a mono WIDTH / DEPTH / HEIGHT label, boxed on the dimension line
- hairline table rules (ultraviolet-grid): The Items drawer: a mono NAME / GRAIN · EYE column row over 1px rules between every folder and body row, with the selected Top row marked by the 4px white bar
- visible 8-point grid alignment (ultraviolet-grid): Registration crosshairs at the corners of the app bar, rail, drawer, axis-gizmo card and toast; every card sits on 8px multiples; the status bar carries the 8px dot field
- ultraviolet data series only (ultraviolet-grid): Ultraviolet appears only on data and the primary action: the dimension lines, ticks and label frames, the drawer's 1/4 partial-selection count, and the toast's Undo button. Selection and chrome stay white and grey

## Graphite

*Today's shell: flat graphite cards with crisp 1px borders over a near-black viewport, the violet accent for state, DM Sans in four sizes.*  
From: today's shipped look.

**Do**

- Cards are flat Theme::paintSurface() panels with a crisp 1px border; separation by borders, never shadows.
- Keep every colour a Theme token; the user can edit all of them in Settings and save them as a colour file.

**Don't**

- No shadows, no glass, no glow.
