#pragma once
// Joinery: the wood joints between two pieces, as a PLAN rather than as
// geometry (spec: docs/superpowers/specs/2026-09-10-joinery-design.md).
// Nothing here cuts a body. A joint records what the connection IS - its
// kind, its two pieces, its parameters - and this file derives, on demand,
// where its items fall and what numbers to mark on the wood.
//
// ZERO Qt, by the same law ModelingOps keeps: this lives in the
// furnify_geometry target, which does not link Qt, so every function below
// is provable in the headless suite with no window and no GPU.
//
// Millimetres throughout. Measure is the only place a unit is ever
// converted for display, and it is a Qt-free header this one does not need.
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <string>
#include <vector>

namespace Joinery {

// The ten kinds, grouped by the three behaviours that actually differ.
// A kind is a preset over its family: same placement rule, same derivation,
// different parameters and a different thing drawn.
enum class Kind {
    Dowel,
    PocketScrew,
    Biscuit,
    Domino,
    Screw,
    Dado,
    Rabbet,
    Groove,
    MortiseTenon,
    HalfLap,
};

enum class Family {
    Fasteners,   // N discrete items in a row along the contact
    Housing,     // a channel in one piece that the other sits in
    Interlock,   // complementary material removed from both
};

Family familyOf(Kind kind);
// The real woodworking name, for every painted string and every message.
std::string kindName(Kind kind);

// One parameter block serves all three families - a kind reads the fields
// that apply to it and ignores the rest. One struct rather than a variant
// because it is persisted, undone and edited as a unit, and a variant would
// buy type-safety at the cost of three serializers.
struct Parameters {
    // Fasteners.
    int count = 3;             // how many items along the joint
    double sizeMm = 6.0;       // dowel diameter, Domino width, screw gauge
    double depthAMm = 15.0;    // into bodyA
    double depthBMm = 15.0;    // into bodyB
    double insetMm = 9.0;      // in from the reference face
    double endMarginMm = 40.0; // first and last item's distance from the ends
    double angleDeg = 0.0;     // an angle belongs to the kinds that drill at
                                // one (pocket screws) - defaultsFor() sets it

    // Housings.
    double widthMm = 18.0;     // channel width, defaults to the housed piece
    bool stopped = false;      // blind-ended rather than through
    double stopMm = 10.0;      // how far short it stops

    // Interlocks.
    double thicknessMm = 6.0;  // tenon thickness / lap depth
    double lengthMm = 30.0;    // tenon length
    bool haunched = false;
};

// Real-world-sane defaults, measured off the wood: `thinnerThicknessMm` is
// the thinner of the two pieces at the joint. Dowels land on real drill
// sizes rather than an arbitrary third of a millimetre.
Parameters defaultsFor(Kind kind, double thinnerThicknessMm);

struct Contact;

// THE defaults function placement and kind-switching call - the first kind
// that fits when J places a joint, and every later change of kind on a live
// one. defaultsFor() above answers from ONE number, the thinner piece, and so
// cannot express "half of EACH piece" or "no deeper than the host"; this
// starts from defaultsFor(kind, thinner) and refines it with each piece's own
// at-the-joint thickness from `contact` (thicknessAMm, thicknessBMm - see
// Contact below):
//
//   - a HALF-LAP removes half of each piece where they cross:
//     depthAMm = thicknessAMm / 2, depthBMm = thicknessBMm / 2;
//   - a MORTISE is cut in piece A - layout() puts depthAMm on A's side of the
//     contact and the tenon's length on B's - so A is the host, and the mortise
//     is capped at A's thickness (a through mortise at most), with the tenon
//     kept a hair shorter than its mortise. A default that punches out of the
//     back of the host is not a sane proposal;
//   - a HOUSING is cut in A too: a third of A deep, as wide as B, the piece
//     it houses.
//
// A thickness the contact did not measure (0, as on a hand-built Contact)
// leaves that refinement to defaultsFor()'s own rule.
Parameters defaultsForContact(Kind kind, const Contact& contact);

// Sentinel for `Contact::regionAreaMm2`: no real area is ever negative, so
// this cannot be mistaken for a measurement - a `Contact` built by hand (as
// several test fixtures are) reads as "not measured" rather than silently
// looking like a full rectangle, and `findContact()` leaves it exactly here
// for an Overlap contact, which has no equally meaningful area to report
// (see the field's own comment below).
inline constexpr double kUnmeasuredRegionAreaMm2 = -1.0;

// Which piece meets a contact END-ON (or edge-on) - `Contact::endOn` - is read
// off FACE COVERAGE: how much of each piece's own contacting face the shared
// region covers (`Contact::coverageA`/`coverageB`). A piece meeting a contact
// end-on brings a face the region covers almost whole - a rail's end face IS
// the region. The host brings a face much larger than the region - a stile's
// long edge, a panel's face. The rule is RELATIVE:
//
//   piece X is end-on  iff  coverage(X) > kEndOnMinCoverage
//                      and  coverage(X) > kEndOnCoverageRatio * coverage(other)
//
// and the contact names Neither when no piece qualifies (with these numbers two
// pieces cannot both qualify).
//
// Why RELATIVE, never an absolute "fully covered": a 300 mm deep shelf standing
// on a 280 mm deep panel covers only 93% of its own end while covering 2% of the
// panel's face, and it is as end-on as any shelf. A near-1.0 cut-off would call
// it Neither.
//
// Why a floor of 0.5: the end-on piece's face has to be MOSTLY covered. Without
// it, two boards laid face to face that overlap only at a corner - a 300 x 800
// face touching a 600 x 1600 one over 100 x 100, 4.2% against 1.0% - would name
// a host, when neither meets the other end-on at all.
//
// Why a ratio of 2: coverage(X) / coverage(Y) is exactly faceArea(Y) /
// faceArea(X), so this reads "the host's contacting face is more than twice the
// end's". Real end-on joints sit far above it - a shelf on a panel face 44x, a
// frame rail into a 700 mm stile's edge 10x, an apron into a leg 10x - and the
// shortest, a 50 mm rail into a 150 mm drawer-front stile, is 3x. Two equal
// faces butted, and boards laid face to face, sit at 1x. What lies between is
// lamination of unequal boards, where either reading is harmless: a board glued
// face to face on a larger one takes fasteners, not a mortise.
//
// Both lines are ratios of AREA between two different pieces' faces, never a
// ratio inside one piece's cross-section, so no standard stock sits on them -
// unlike the depth-against-thickness rule this replaced (fix round 1), whose 2x
// line fell exactly on 19 x 38 batten and which gave the frame mortise and tenon
// no host at all: a stile has its WIDTH behind a rail's end, 3.2x its thickness,
// so both pieces read end-on and collapsed to Neither. The one exact case left,
// a board face-glued centred on one exactly twice its size, sits on the ratio's
// line and may read either way; both readings are harmless there.
//
// One named limit. A 6 mm back panel laid on a carcase side's back EDGE reads
// the SIDE as end-on - its edge is covered whole, the panel's broad face barely -
// and so makes the back panel the host, though a rabbet or a groove for a back
// belongs in the side. The spec lists "which piece is host and which is housed"
// as a setting the user chooses, and the joint's chip carries that choice.
//
// An Overlap contact (crossing rails) always names Neither - a lap has no end
// grain - and carries no coverage.
//
// A second named limit, of the measurement rather than the rule: a contacting
// face split into DISJOINT coplanar strips (a slotted board, say) is compared
// strip by strip, so it can read a definite - and wrong - host where the same
// geometry unsplit reads Neither; the user's host override on the joint's chip
// (a later task) is the answer there too.
inline constexpr double kEndOnMinCoverage = 0.5;
inline constexpr double kEndOnCoverageRatio = 2.0;

// Where two pieces meet, in the contact's OWN frame - the one coordinate
// system every derived position is expressed in, so nothing downstream
// needs to know a world axis. A joint stores no world position; this is
// recomputed from the live shapes on every read, which is what makes a
// joint follow its pieces.
struct Contact {
    enum class Type {
        Face,      // two faces meet - the ordinary case
        Overlap,   // the solids intersect - what a half-lap is cut from
    };

    Type type = Type::Face;

    // The contact's own frame, and the ONE convention BOTH types honour -
    // every position Tasks 3 onward derive is measured from this origin, so
    // two conventions on one struct would be a trap rather than a detail:
    //
    //   ORIGIN is the corner of the region's own BOUNDING RECTANGLE at
    //   (uMin, vMin). `uMin` and `vMin` are therefore ALWAYS 0.0, and
    //   at(0, 0) is that corner.
    //
    //   `uMax`/`vMax` are the bounding rectangle's SIZE, which is the
    //   region's own size only when the region is a rectangle - which it is
    //   for every joint this app makes today. For a region that is not (a
    //   notched board's L-shaped end, a C), the rectangle COVERS AREA THAT IS
    //   NOT IN CONTACT: measured, a 300 x 300 square with a 280 x 200 notch
    //   taken out of it - 34,000 mm2 of a 90,000 mm2 rectangle - reports
    //   u = 300, v = 300. A caller laying items out across that span has to
    //   accept that some of them may fall over the notch; nothing here can
    //   tell it which, because `Contact` describes a rectangle by design.
    //
    //   X comes from the REGION'S OWN GEOMETRY - the direction of its
    //   longest boundary edge - never from a world axis and never from
    //   gp_Ax3(point, dir)'s arbitrary default pick, which measures the
    //   region's bounding box in a WORLD orientation and reports a true
    //   300 x 18 contact as 224.86 x 224.86 at 45 degrees of in-plane
    //   rotation. So u runs ALONG the joint and v ACROSS it by
    //   construction, which is what makes runsAlongU() and runLength()
    //   mean what they say at every orientation. Y is Z x X, right-handed.
    //
    //   Z is the contact normal. For a Face contact it points FROM bodyA
    //   INTO bodyB, settled by classifying a point a hair either side of a
    //   point genuinely ON the region against each solid - not from a face's
    //   orientation flag, which carries no such information, and not from
    //   the two planes' signed offset, which is exactly 0.0 for a flush
    //   contact and so answers the same for findContact(a, b) and
    //   findContact(b, a). For an OVERLAP there is no "into" - neither piece
    //   is on one side of a lap - so Z is the lap's own THINNEST direction
    //   (the lap depth), oriented from a's centre of mass toward b's when
    //   that is decisive, and the lap spans [0, lap depth] along it from the
    //   frame's plane.
    gp_Ax3 frame;
    double uMin = 0.0, uMax = 0.0;
    double vMin = 0.0, vMax = 0.0;
    // How much wood each piece has AT THIS JOINT, which is what defaultsFor()
    // wants: a housing is a third as deep as the host is thick where the
    // channel is cut, and a dowel is a third the diameter of the wood it is
    // actually driven into.
    //
    // Measured as the material depth along the contact normal, from inside the
    // piece out to the first surface behind the joint (for an Overlap, through
    // an interior point of the lap along the lap depth - each rail's own
    // thickness), capped by the smallest side of that piece's own ORIENTED
    // bounding box. Both halves earn their place:
    //
    //   the local depth is what makes a hollow carcase honest - 18 mm walls
    //   around a 300 mm box is 300 mm as a solid and 18 mm of wood at every
    //   joint on it, and the whole-solid answer would have had defaultsFor()
    //   propose a 300 mm dado. A panel rabbeted to 18 mm reports 18 where the
    //   shelf lands, not the 36 it is elsewhere;
    //
    //   the cap is what keeps a butt joint right - a shelf meeting a panel
    //   END-ON has its whole 600 mm of length behind the joint, and it is an
    //   18 mm board.
    //
    // Orientation-independent either way: a board the transform gizmo has spun
    // 45 degrees still measures 18 mm, where a world bounding box would say
    // 224.86.
    //
    // One shape defeats both halves and is worth knowing about: a board bent
    // into an L or a U and joined on its END has its own length behind the
    // joint AND a bounding box the size of the L, so an 18 mm L-section board
    // reports the L's 300 mm. Getting that right needs the local minimum WIDTH
    // of the material, which has no stable cheap measure (a chord through a
    // sampled point collapses near any boundary).
    double thicknessAMm = 0.0;
    double thicknessBMm = 0.0;

    // Which piece meets this contact END-ON (or edge-on): a shelf standing on
    // its end against a panel's face is end-on and the panel is not; so is a
    // frame rail's end against a stile's edge, and the stile is not. That
    // decides which piece is the HOST - the one a mortise or a housing is cut
    // into, which layout() always puts on piece A - so placement orders its two
    // pieces by this rather than by which one the user happened to click first.
    //
    // It has to be its own field: thicknessAMm/thicknessBMm cannot answer it,
    // because the cap described above makes both read 18 mm for exactly the
    // shelf-on-panel case this exists for. Set by findContact() from FACE
    // COVERAGE (coverageA/coverageB below; the rule, why it is relative, and
    // its one named limit are on kEndOnCoverageRatio). Neither for two equal
    // faces butted or laid face to face, for a region too small a part of
    // either face, for every Overlap contact (a lap has no end grain), and for
    // a hand-built Contact - which reads as "no host known", never as a guess.
    enum class EndOn { Neither, A, B };
    EndOn endOn = EndOn::Neither;

    // How much of each piece's OWN contacting face the region covers, 0..1:
    // the region's real area over the area of that piece's face in the winning
    // pair. A rail's end face reads 1; the stile edge it lands on reads 0.1.
    // What endOn is decided from, exposed so the decision can be read and
    // pinned rather than trusted. -1 - no real coverage is negative - for an
    // Overlap contact, which has no contacting faces, and for a hand-built
    // Contact.
    double coverageA = -1.0;
    double coverageB = -1.0;

    // The shared region's own REAL area, measured from the actual boolean
    // intersection - not derived from `uLength()`/`vLength()`, which is the
    // bounding rectangle's area and, for an L-shaped, C-shaped or rounded
    // region, is larger than this. This is what lets a later reader tell a
    // genuinely rectangular contact from one whose rectangle merely bounds
    // an irregular region (see `regionShortfallCaveat()` below): the two
    // areas agree, within floating-point tolerance, exactly when the
    // rectangle IS the region.
    //
    // Populated for a Face contact only, from the exact planar area
    // `findContact()` already computes to pick the best candidate face pair
    // - no new measurement, just one that used to be discarded. LEFT AT
    // `kUnmeasuredRegionAreaMm2` for an Overlap contact: the only
    // already-computed area-shaped quantity there is the lap SOLID's own
    // volume (mm3, from `BRepGProp::VolumeProperties`), and writing a volume
    // into a field named and compared as an area would be exactly the wrong
    // kind of number this sentinel exists to prevent - it would silently
    // look like a real measurement and compare against `uLength() *
    // vLength()` (mm2) in the wrong units. A real footprint area for a lap
    // (the area of its own cross-section, perpendicular to the lap depth)
    // is not computed anywhere today and would need a genuinely new
    // measurement (a section cut through the lap solid) rather than a
    // reused one; left undone rather than guessed at.
    double regionAreaMm2 = kUnmeasuredRegionAreaMm2;

    double uLength() const { return uMax - uMin; }
    double vLength() const { return vMax - vMin; }
    // The longer in-plane direction - the line a row of fasteners runs
    // along, and the length a housing is cut across.
    //
    // Note for a caller thinking of branching on it: u is taken from the
    // region's longest boundary edge, so for any RECTANGULAR region - every
    // joint this app makes today - runsAlongU() is true BY CONSTRUCTION and
    // runLength() == uLength() always. The false branch is unreachable in
    // practice; write it if you like, but do not expect to exercise it.
    bool runsAlongU() const { return uLength() >= vLength(); }
    double runLength() const { return std::max(uLength(), vLength()); }
    // A point in the contact's own coordinates, in the world.
    gp_Pnt at(double u, double v) const;
};

// ok == false ALWAYS carries an empty contact and a non-empty error - the
// BooleanResult contract, so a refusal can never be read as a success.
struct ContactResult {
    bool ok = false;
    Contact contact;
    std::string error;
};

// The largest place `a` and `b` meet. A face contact wins over an overlap
// when both exist (two boards can touch AND intersect slightly - the
// separating test is asked LOCALLY, at the shared region, so slop of the
// same order as `toleranceMm` does not suppress a real contact); an overlap
// is reported only when there is no face contact, which is the
// crossing-rails case a half-lap is cut from.
//
// `toleranceMm` is how far apart two faces may be and still count as
// touching - a model is never perfect, and 0.1 mm of slop is not a gap. It is
// purely PERMISSIVE and has no upper bound: raising it can only widen what
// counts as a contact, never narrow it, and nothing else in here is derived
// from it.
//
// A curved contact boundary is MEASURED, not cut short at its endpoints - an
// arc that bulges past the vertices it runs between contributes its own
// extremes. Refuses, with a reason, when the pieces do not meet, when a piece
// is missing, when the kernel throws, and when the region has no straight
// boundary edge at all to take u from (a flat board on a round leg touches
// along a line, and ok == true with a zero-size contact would be a refusal
// surfacing as a success).
ContactResult findContact(const TopoDS_Shape& a, const TopoDS_Shape& b,
                          double toleranceMm = 0.1);

// Which joints a contact can actually take, and the reason when one cannot.

// Empty when `kind` can exist on `contact`; otherwise the reason, in one
// clause, ready to show beside a greyed-out choice. A joint that cannot
// exist is never offered, so it can never be created.
//
// Deliberately does NOT read `Contact::regionAreaMm2` (see that field's
// comment, and `regionShortfallCaveat()` just below): `Contact` describes a
// BOUNDING RECTANGLE (see the long comment on the struct above), and for an
// L-shaped, C-shaped or rounded contact region, part of that rectangle is
// not actually in contact - a row laid out across the full span can land an
// item where there is no wood. That is real, but it is not a reason to
// refuse the KIND - an L-shaped contact can carry a perfectly good dowel
// row if the dowels happen to land on wood, and refusing every kind there
// over a shape this function was never asked about would block real work.
// The fact still has to reach the user; it does, through
// `regionShortfallCaveat()`, which answers a different question
// ("might this contact surprise you") rather than this one ("can this kind
// exist here at all").
std::string validityOf(Kind kind, const Contact& contact);

// Every kind this contact can take, in menu order.
std::vector<Kind> validKindsFor(const Contact& contact);

// Non-empty when `contact`'s own region may not fill its bounding
// rectangle, so items `layout()` places inside it can land where the two
// pieces do not actually touch - the CAVEAT half of the gap `validityOf()`
// documents above. This is additive, never a refusal: a kind `validityOf()`
// already approved stays approved regardless of what this returns: a
// shortfall is a fact worth showing beside the joint, not a veto over it.
//
// Compares `regionAreaMm2` against the bounding rectangle's own area
// (`uLength() * vLength()`) - the region's REAL area against the area of
// the box `Contact` reports, both taken from exact geometry (no
// tessellation on either side), so they agree to a handful of parts in a
// million for a genuine rectangle and are nowhere near that band for a real
// shortfall (an L-shaped 90,000 mm2 rectangle over an 11,952 mm2 real
// region is 87% short - see the struct comment's own numbers). That is why
// this can use a tight floating-point tolerance rather than an invented
// threshold: unlike the thickness-plausibility question this feature
// considered and declined (no safe case to anchor a threshold against), a
// rectangle either IS its own region or it is not, exactly.
//
// Empty - not "fills its rectangle", but literally "nothing to say" - when
// `regionAreaMm2` is `kUnmeasuredRegionAreaMm2` (today, always true for an
// Overlap contact; see the field's own comment). A caller that wants to
// know whether a contact's shortfall is KNOWN rather than merely absent
// reads `contact.regionAreaMm2` directly.
std::string regionShortfallCaveat(const Contact& contact);

// One placed piece of the joint - a dowel, a screw, a whole channel, a
// tenon. `u`/`v` are its position in the contact's own coordinates (what
// an adjustment moves, and what the readout measures); `centre` is that
// same point in the world, derived.
struct Item {
    double u = 0.0;
    double v = 0.0;
    gp_Pnt centre;
    gp_Dir axis{0.0, 0.0, 1.0};   // into the wood, from A toward B
    double sizeMm = 0.0;
    double depthAMm = 0.0;
    double depthBMm = 0.0;
    // Housings and interlocks are regions rather than points; a fastener
    // leaves these zero.
    double spanUMm = 0.0;
    double spanVMm = 0.0;
    // The angle a fastener is driven at, tilted ACROSS the joint about the run
    // it lies along - a pocket screw's 15 degrees, 0 for anything driven
    // straight. Copied from the parameters by layout(), so whatever draws or
    // reads an item never has to reach back for them.
    double angleDeg = 0.0;
};

// A per-item override, stored in the CONTACT's coordinates so it survives
// the pieces moving - the whole reason adjustments are not world points.
struct Adjustment {
    int index = 0;
    double du = 0.0;
    double dv = 0.0;
};

// Where this joint's items fall. Pure: same inputs, same answer, no state.
//
// PLACES ITEMS WITHIN THE CONTACT'S BOUNDING RECTANGLE - it cannot do
// otherwise, because that rectangle is all `Contact` describes (see the
// long comment on `Contact` above). For a non-rectangular region - an
// L-shaped or C-shaped contact, a round one - part of that rectangle is
// NOT in contact at all, so a row laid out across the span can place an
// item where there is no wood; `at(0, 0)` itself is not guaranteed to sit
// on the region for a shape like that. This function has no way to know
// which of its returned items, if any, landed off the wood - doing so
// would mean inventing a containment test `Contact` does not carry, which
// is deliberately out of scope here. Deciding what to do about an
// off-region item - refuse it, flag it, nudge it - belongs to the validity
// rules a later task owns; nothing below should be read as a guarantee
// that every `Item` this returns is actually on material.
std::vector<Item> layout(Kind kind, const Parameters& params, const Contact& contact,
                         const std::vector<Adjustment>& adjustments);

// The numbers a pencil and a square need. Distances are measured from a
// NAMED edge - "from the front edge: 60, 150, 240" - because an
// unlabelled number is not a measurement you can transfer to wood.
struct Readout {
    std::string referenceEdgeA;   // the edge of bodyA these are measured from
    std::string referenceEdgeB;
    // True when referenceEdgeA/B is one of the six edge WORDS; false when it is
    // the honest no-single-edge sentence. Decided in the one place the word is
    // (edgeName() in the .cpp), so a caller branching on it - the drawer puts
    // the named edge at a ruler's zero, and writes numbers when there is none -
    // never keeps a second copy of the word list.
    bool referenceEdgeNamed = false;
    std::vector<double> alongMm;  // one per item, from the reference edge
    double insetMm = 0.0;         // across the face
    double depthAMm = 0.0;        // into A: a drill depth, a channel, a mortise, A's lap
    // Into B: a fastener's far-side drill depth, a tenon's LENGTH, the depth a
    // half-lap removes from B. 0 for a housing - B sits in the channel uncut.
    double depthBMm = 0.0;
    // A housing's channel width, a tenon's thickness, and for a HALF-LAP the
    // lap's own span ACROSS the overlap (not the half-board thicknessMm, which
    // describes nothing a person marks). A fastener carries thicknessMm here.
    double widthMm = 0.0;
};

// referenceEdgeA/B name the edge the numbers are measured from - one of
// "front"/"back"/"left"/"right"/"top"/"bottom" when the run genuinely lines
// up with a world axis, or an honest sentence saying no single edge applies
// when it does not (a board the transform gizmo has spun 40 degrees has no
// face that is honestly "front"). See edgeName() in the .cpp for the
// dominance threshold this is derived from - ONE place, so the word painted
// in a drawer and the word any other surface reads can never disagree.
Readout readout(Kind kind, const Parameters& params, const Contact& contact,
                const std::vector<Item>& items);

// Everything a joint is, right now, derived from the live shapes: where it
// sits, where its items fall, and the numbers to mark. ok == false carries
// NO items and NO readout - the contract that keeps a broken joint from ever
// showing a stale measurement. Holds no state of its own: a joint stores its
// kind, its two bodies and its parameters (see DocumentModel::Joint), and
// this is what turns those into a Derivation on demand, so a joint follows
// its pieces rather than remembering where they used to be.
struct Derivation {
    bool ok = false;
    Contact contact;
    std::vector<Item> items;
    Readout readout;
    std::string error;
};

// The whole chain, end to end: find where `a` and `b` meet, confirm `kind`
// can actually exist on that contact, lay out its items, and read the
// numbers off them - or refuse, loudly, the moment either step cannot
// honestly proceed. Two distinct refusals, both carrying a non-empty reason
// and both leaving `items` and `readout` empty: `findContact` failing (the
// pieces no longer meet, or meet in a way that cannot be measured) and
// `validityOf` refusing (they meet, but not in a way `kind` can use - a
// half-lap asked of two pieces that only touch, say). World position is
// produced exactly once anywhere in this chain, inside `layout()`'s shared
// tail through `Contact::at()`; nothing here derives one any other way.
Derivation derive(Kind kind, const Parameters& params,
                  const std::vector<Adjustment>& adjustments,
                  const TopoDS_Shape& a, const TopoDS_Shape& b);

}  // namespace Joinery
