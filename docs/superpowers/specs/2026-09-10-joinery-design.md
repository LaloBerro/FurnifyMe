# Joinery — planning real wood joints between pieces

**Status:** design agreed 2026-09-10, not yet planned or implemented.

## The problem

FurnifyMe models what a piece of furniture *looks like*. It says nothing
about how the boards are held together, so a finished model is a picture
rather than a plan: it cannot tell you where to drill, how deep, how many
dowels, or how wide to cut a dado.

This feature adds joinery as a planning layer — the connections between
pieces, with the numbers you mark on the wood — so a design can be carried
to a workbench and built with hand tools.

## Scope decisions, and why

Four forks were settled in conversation before any design was written.

**A planning layer, not real geometry.** A joint records *what the
connection is*; it does not cut mortises or add tenons to the bodies. The
user builds by hand — drill, router, chisel, pocket-hole jig — so what they
need is positions and dimensions, not a CNC-accurate part file. Cutting for
real was rejected for this version on three grounds: it commits the model
irreversibly while the design is still being decided; this app deliberately
has no parametric history, so cut geometry would go silently stale and the
model would lie about a piece the user then builds; and every boolean makes
a body's face structure harder to pick, fillet and pull, on exactly the thin
near-tangent geometry where OCCT's booleans are weakest. The ordering wastes
nothing: cutting a mortise requires knowing where and how big it is, which
is this feature's data. "Apply for real" can be built later on top, and is
out of scope here.

**All nine kinds, on three behaviours.** Dowels, pocket screws, biscuits,
Dominos, plain screws, dado, rabbet, groove, mortise & tenon, half-lap. They
are not nine features: they are three placement behaviours (fasteners in a
row, housings, interlocks) with presets and parameters on top. If that
framing is wrong, each kind becomes a special case — so the framing is the
part to get right first.

**Auto-placed, adjustable afterwards.** Selecting two pieces finds their
contact and lays the joint out with sensible defaults; any joint can then be
adjusted, and individual items moved.

**Joints follow their pieces.** A joint stores the *relationship*, never a
world position. Positions are re-derived from where the bodies currently
are, so the numbers are always current. A joint whose pieces no longer meet
is broken loudly — flagged in the drawer, drawn in red, no numbers — rather
than showing values that are quietly wrong. A build sheet that goes stale in
silence is worse than no build sheet.

**On screen only.** No printable or exported build sheet in this version.
Revisited after the user has taken it to a real build.

## Architecture

### The record

A joint lives in `DocumentModel::State`, alongside the mirror pairing map
and link groups already there, and for the same reason: it is a relationship
between bodies that changes only inside checkpointed commits, so undo must
restore it exactly as it stood.

```
Joint {
  int              id;
  JointKind        kind;       // nine kinds, see below
  int              bodyA, bodyB;
  JointParameters  params;     // per-kind; see the families
  std::vector<ItemAdjustment> adjustments;   // per-item overrides
}
```

Deliberately absent: any world coordinate. Nothing about where the joint
*is* is stored.

### Derivation, in three steps

Each step is a pure function over shapes and numbers, with no Qt and no
GPU, so all of it is provable in the headless suite:

1. **Contact.** Given two shapes and a tolerance, find the region where they
   meet — in practice a rectangle with its own local frame (origin, in-plane
   axes, extents). Returns "no contact" rather than guessing.
2. **Layout.** Given a contact and parameters, produce the joint's items as
   points/regions in the contact's own coordinates, applying count or
   spacing, insets and end margins, then any stored per-item adjustments.
3. **Readout.** Express each item as distances from a *named reference edge*
   of each piece — the numbers a pencil and square need.

Adjustments are stored in the contact's coordinates, not the world's, so a
manually positioned dowel keeps its intent when the pieces move.

### Validity

Each kind declares the contact it is valid for (a dado wants end-against-
face; a half-lap wants two pieces crossing). The placement UI offers only
valid kinds for the contact at hand and greys out the rest with the reason,
so a joint that cannot exist cannot be created.

### Defaults from the wood

The app measures the boards, so proposed values are already sane: dowel
diameter about one third of the thinner piece, tenon thickness about one
third of the stile, dado depth one third to one half of the host, housing
width equal to the housed piece's real thickness. Everything is overridable.

## The three families

### Fasteners in a row
*dowels, pocket screws, biscuits, Dominos, plain screws*

Parameters: count or spacing; size (dowel diameter, biscuit number, Domino
size, screw gauge); depth into each piece; inset from the face; end margins;
for pocket screws, the angle and the face drilled from.

Produces: mark-out positions along the joint and the per-side drill depths.

### Housings
*dado, rabbet, groove*

Parameters: which piece is host and which is housed; width (defaulting to
the housed piece's thickness); depth; position along the host; through or
stopped, and by how much.

Produces: the channel's position from a reference edge, its width, depth and
length.

### Interlocks
*mortise & tenon, half-lap*

Parameters: tenon thickness, width and length with matching mortise depth;
shoulder offsets; haunched or not; for a half-lap, the depth removed from
each piece, defaulting to half of each.

Produces: both pieces' cut dimensions and where they fall.

## Interaction

**Placing.** Select two pieces, press `J` (or Model → Joint). The tool finds
the contact, offers the valid kinds, and creates the joint immediately with
defaults. No contact means one explanatory sentence and nothing created.

**Editing.** A selected joint raises the app's existing value chip beside it
— the same control the pull, bevel and transform gestures use, with the same
Enter/Escape contract — carrying count, size, depth and inset.

**Drawing.** Joints draw as ghosted hardware: dowels as cylinders, screws as
angled pins, housings as an outlined channel, tenons as an outlined block.
Ghosted because they are a plan, not material. They draw only while the
joints drawer is open or a joint is selected, so the model is not
permanently full of hardware, and render mode hides them entirely like every
other piece of scene decoration.

**The drawer.** A third drawer beside Items and Versions, listing every
joint as "Shelf ↔ Left panel — 3 dowels, 8 × 30 mm". A row expands to the
mark-out numbers for both pieces: distances from a named reference edge,
inset, and drill depth. Clicking a row highlights that joint in the
viewport. Broken joints sort to the top, in red, with the reason.

## Behaviour with what already exists

- **Delete / boolean.** A joint dies with either of its pieces, inside the
  same checkpoint, and returns with undo.
- **Undo/redo.** Joints ride in `State`, so they restore exactly.
- **Save/load.** Joints persist in the furniture's manifest; an absent
  joints key loads as "no joints", the forward-compatible rule the manifest
  already follows.
- **Mirror.** Mirroring a piece that carries joints mirrors the joints too,
  which is what a symmetric cabinet wants.
- **Isolate.** A joint is hidden when either of its pieces is hidden.
- **Render mode.** Joints are hidden outright.
- **Vocabulary.** "Joint" is the word, everywhere. The kinds keep their real
  woodworking names (dowel, dado, rabbet, groove, mortise, tenon, half-lap,
  pocket screw, biscuit, Domino). Note that "rabbet" and "groove" are
  *housings*, distinct from the banned "bevel" family — no existing banned
  word is affected, and none of these are added to the ban list.

## Testing

**Headless (no GPU).** The three derivation steps are pure functions with
exact expected answers: contact detection on known shapes, including the
refusals (pieces apart, pieces overlapping, pieces merely touching at a
line); layout arithmetic (even spacing, insets, end margins, adjustments);
readout arithmetic against hand-computed distances. Validity rules per kind.
Defaults derived from board thickness.

**gui_smoke.** Placing a joint through the real gesture; the refusal when
two pieces do not meet; the chip's edit committing through the same path;
the drawer's rows and expanded numbers; a joint following its pieces when
one is moved; a joint going broken when they part; deletion and undo taking
joints with them; joints hidden in render mode.

## Out of scope, stated plainly

- Cutting real geometry (joint-shaped material removal) — a later feature on
  this same data.
- Validation of whether a joint is *sound*: tenons punching through, dowels
  colliding inside a panel, insufficient remaining material.
- Grain direction, and any advice that depends on it.
- Saved joint presets ("my usual dowel setup").
- Printable or exported build sheets.
- Hardware totals across the piece (a sum over this data; easy to add once
  the readout exists, deliberately not promised now).
