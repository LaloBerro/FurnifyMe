#pragma once
//
// Pure geometry operations on the OCCT kernel.
//
// HARD RULE: neither this header nor ModelingOps.cpp may include a single Qt
// header, directly or transitively. Everything here has to stay runnable from
// tests/headless_geometry.cpp with no window and no GPU.
//
#include <string>
#include <vector>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

namespace ModelingOps {

enum class BooleanKind { Fuse, Cut, Common };

// Booleans on near-tangent geometry are OCCT's known weak spot. Callers must
// look at `ok` - never present a failed boolean as a success.
//
// Refusal contract, binding for every function in this file that returns
// one: when ok == false, `shape` is left null. Never a partially-applied
// shape, never the pre-operation body, never a stale value from a previous
// call - null. Callers (the three direct-modeling gizmos in particular) may
// rely on `!ok` implying `shape.IsNull()` without checking both.
struct BooleanResult {
    bool ok = false;
    TopoDS_Shape shape;
    std::string error;

    // Set only by filletEdges/chamferEdges, and only on a refusal: true when
    // what was refused is this COMBINATION of edges rather than the size
    // asked for - the kernel took none of them, or took only some. Every
    // other operation leaves it false.
    //
    // It exists because the two cases need opposite advice. "Try a smaller
    // size" is the right sentence for a radius that would eat a neighbouring
    // face; said about a combination the kernel will not bevel together it is
    // simply false, because no size works, and the user shrinks the number
    // until they give up. `error` cannot serve: it is written for this file,
    // never shown, and a caller matching substrings of it would break the
    // first time a sentence was reworded.
    bool combinationRefused = false;
};

struct StepResult {
    bool ok = false;
    std::string error;
};

// Closed polyline through `points`. Returns a null wire if fewer than 3 points.
TopoDS_Wire makePolygonWire(const std::vector<gp_Pnt>& points);

// A point genuinely ON `face`'s own material - not merely inside its bounding
// box, and NOT its area centroid, which for a face with a hole lands in the
// hole and for an L- or C-shaped face lands off the face entirely. Samples
// the face's own UV grid and classifies each candidate with
// BRepClass_FaceClassifier, which reads every wire. False (leaving `out`
// untouched) only for a degenerate face no sample lands inside.
//
// Public because two callers need it: outwardPlane() here, to decide which
// side of a face is outward without trusting TopAbs_Orientation, and
// Joinery::findContact, to decide which side of a contact plane each solid
// occupies. Both are "classify a point that is genuinely on the geometry",
// and both got it wrong first by using a centroid.
bool pointOnFace(const TopoDS_Face& face, gp_Pnt& out);

// Planar face from a closed wire. Null face if the wire is not planar/closed.
TopoDS_Face makeFaceFromWire(const TopoDS_Wire& wire);

// Prism of `height` along `direction`. Negative height extrudes the other way.
TopoDS_Shape extrude(const TopoDS_Face& profile, const gp_Dir& direction, double height);

// Axis-aligned box with its min corner at `corner`.
TopoDS_Shape makeBox(const gp_Pnt& corner, double dx, double dy, double dz);

// Runs the operation, then ShapeUpgrade_UnifySameDomain to merge the coplanar
// faces the boolean leaves behind (skip that and selection gets miserable).
BooleanResult applyBoolean(BooleanKind kind,
                           const TopoDS_Shape& a,
                           const TopoDS_Shape& b,
                           double fuzzyValue = 1.0e-5);

// Single compound of several shapes, for exporting a whole document at once.
TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape>& shapes);

// --- Direct modeling (Milestone 2) -----------------------------------------
//
// Pull a planar face of `body` by `distance` along its OUTWARD normal.
// Positive grows (prism fused on), negative carves (prism cut away). The
// outward normal starts from the same flag MainWindow::lockToFace reads -
// BRepAdaptor_Surface never applies TopAbs_Orientation, so a REVERSED face's
// plane normal is flipped before use - and is then CONFIRMED against the
// body itself with BRepClass3d_SolidClassifier before use. The flag alone
// is not enough: a MIRRORED body (Milestone 3's symmetry twins) is built by
// a negative-determinant transform, and BRepBuilderAPI_Transform's copy
// rebuild toggles a mirrored shape's face-orientation flags uniformly, so a
// face whose raw geometric normal the mirror never touched can still have
// its flag flipped. See outwardPlane() in the .cpp for the measured case
// this fixed - a "grow" pull that landed net inward on a mirrored twin.
// Refuses: a null body/face, a face that is not one of `body`'s own faces
// (checked by TopoDS_Shape::IsSame - a foreign face would otherwise fuse or
// cut a perfectly valid boolean between two unrelated shapes), a non-planar
// face, |distance| < 1e-7, a carve that consumes the body entirely (result
// empty or volume ~0), and any kernel failure. Result goes through
// ShapeUpgrade_UnifySameDomain, same as applyBoolean, so pulled faces do not
// accumulate junk edges.
BooleanResult pullFace(const TopoDS_Shape& body, const TopoDS_Face& face,
                       double distance);

// Round the given edges of `body` with radius r / flatten them with distance
// d, in ONE kernel build. Refuses a null body, an empty list, any null edge,
// r/d <= 0, and any BRepFilletAPI failure (IsDone false, null or
// empty/invalid result) - OCCT fillets legitimately fail on hard geometry
// (e.g. a radius that would eat a neighbouring face) and BRepFilletAPI can
// throw Standard_Failure rather than politely fail; both are caught at this
// boundary and converted to ok == false, body untouched.
//
// The refusal is ALL-OR-NOTHING: one foreign, null or unbuildable edge
// refuses the whole call. There is no partial bevel - a gesture the user
// made over three edges either produces one body with three of them changed
// or changes nothing at all, and the caller can rely on `!ok` implying
// `shape.IsNull()`.
//
// That is ENFORCED, not merely intended, and the enforcement is not
// `NbContours() > 0`. BRepFilletAPI's Add() accepts or drops each edge on its
// own - a cylinder's seam edge, for one, is a perfectly ordinary straight
// edge that yields no contour at all - so a three-edge list with one dropped
// leaves two contours, builds happily, and would hand back a body with two of
// the three bevelled and no word about the third. Every requested edge must
// therefore turn up in some contour before the build is allowed to run, and
// the refusal carries `combinationRefused` so the caller can say "try them
// one at a time" instead of "try a smaller size". Contours are NOT one per
// edge (two edges of one tangent chain share a contour, two far apart get
// one each), so counting them cannot answer this.
//
// CONTAINMENT - the fix for the spreading bevel. BRepFilletAPI's Add() is
// documented to build a CONTOUR by propagation: "the contour is composed of
// edges of the shape which are tangential to one another and which delimit
// two series of tangential faces". A fillet strip made by an EARLIER
// operation is exactly such a tangential series, so rounding an edge that
// ends on one pulls that strip's far neighbour into the same contour and
// bevels an edge the user never picked. Nothing in the OCCT API turns that
// off (SetContinuity, ChFi3d_FilletShape and ShapeUpgrade_UnifySameDomain
// were all measured against it and none changes the contour), so the spread
// is CLIPPED here instead: a contour that carries an edge nobody asked for
// is rebuilt with the material outside the picked edges' own extents put
// back. Propagation enters and leaves through the picked edge's END points,
// which is why clipping at the two planes perpendicular to it there is
// exactly the containment and not an approximation - the volume removed
// comes out at the single-edge formula to six figures.
//
// The clip only runs when propagation is actually detected, so the ordinary
// case (a fresh box, or edges far apart) takes the plain kernel path with no
// extra booleans and no extra risk. A picked edge that is not straight has
// no such perpendicular pair, so if propagation is detected on one, the call
// is REFUSED rather than answered with a shape that quietly bevels more than
// was asked for.
//
// WHEN THE CLIP HAS NOTHING TO PUT BACK, the raw kernel result stands. If the
// picked edges' extents between them already cover the whole body - which one
// edge spanning the body in its own direction is enough to do, and a
// Shift-selection of two or three edges on a box reaches easily - then
// `body minus the slabs` is empty and there is no material outside the picked
// extents to restore. Every propagated edge is then inside some picked edge's
// extent: material within the reach of what the user asked to bevel. Refusing
// there was worse than useless, because the only sentence the UI had for it
// was "try a smaller size" and NO size works - the geometry, not the number,
// is what makes the slabs cover the body. So the call succeeds with what the
// kernel built, which is exactly what this app shipped before the clip
// existed. It is a weaker answer, not a wrong one, and it is bounded: the
// clip still contains every case where there IS something to put back.
//
// WHAT THE CLIP DOES NOT REMOVE, stated rather than hidden. At a corner where
// the picked edge meets its neighbour at anything other than a right angle,
// the part of the propagated strip lying on the picked edge's OWN side of
// that end plane survives. It is THIN but not SHORT: measured on a skew prism
// whose faces meet at 80 degrees, r = 4, the remnant is a cylindrical sliver
// about 6% of the operation's volume that runs the FULL LENGTH of the
// unpicked corner edge - so on a 700 mm post it is a 700 mm sliver, not a
// patch near the corner. It cannot be cut away without cutting the picked
// edge's own bevel short at exactly the corner where it should run right up
// to its neighbour, and a bevel that stops short of its own corner is the
// worse of the two. What the clip does remove is the case the bug was about:
// a neighbouring edge rounded at the FULL radius along its entire length,
// which is what a user sees and reports. gui_smoke pins the difference by
// counting only strips longer than four radii.
BooleanResult filletEdges(const TopoDS_Shape& body,
                          const std::vector<TopoDS_Edge>& edges, double radius);
BooleanResult chamferEdges(const TopoDS_Shape& body,
                           const std::vector<TopoDS_Edge>& edges, double distance);

// The one-edge spellings, kept because most callers and most tests have
// exactly one edge in hand. Both delegate to the list forms above, so the
// containment rule and every refusal are the same code, not a second copy.
BooleanResult filletEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                         double radius);
BooleanResult chamferEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                          double distance);

// Where a round-or-flatten gesture on `edge` is measured, and which way is
// "out of the body" there: the edge's midpoint, and the bisector of its two
// adjacent faces' OUTWARD normals, with the component along the edge removed.
//
// It lives HERE, in the Qt-free library, and not beside the widget that drives
// it, for the reason the whole library exists: this is the piece a volume
// check cannot verify and an end-to-end drag can only test at whatever single
// edge the camera happened to make reachable. `tests/direct_modeling.cpp`
// walks all twelve edges of a box against a BRepClass3d_SolidClassifier
// oracle, deterministically and with no window - which is not something a
// function sitting in src/ui can be asked to do.
//
// Both halves of the derivation are earned:
//
//   - OUTWARD, derived properly. BRepAdaptor_Surface never applies
//     TopAbs_Orientation, so on a REVERSED face the surface normal points
//     INTO the body - three of six faces of a plain box are REVERSED. Get it
//     wrong and the bisector points inward, so dragging away from the body
//     rounds it and dragging into it flattens it: the gesture reads exactly
//     backwards, and every check that measures the drag against this same
//     axis passes anyway. That is Phase 4's lockToFace lesson and Task 2's
//     PullArrow::begin lesson, applied a third time rather than assumed.
//   - PERPENDICULAR. The two normals are perpendicular to the edge on a box,
//     so their sum already is - but on a body whose faces meet the edge at an
//     angle it is not, and an axis with a component ALONG the edge would slide
//     the arrow off the edge it belongs to as the drag went on. The component
//     along the edge is removed explicitly.
//
// NOTE ON CONCAVE EDGES. The axis is the outward bisector wherever the two
// faces meet, so at an INSIDE corner it points out of the notch and the
// mapping still reads "against the bisector rounds". A fillet there adds
// material and bulges toward the notch - visually opposite the drag, and
// correct CAD behaviour. The convex case is the common one in furniture and
// is what the tests pin; the mapping is deliberately not flipped per-edge,
// because a gesture whose direction depends on which edge you grabbed is
// worse than one that is occasionally counter-intuitive.
//
// False - leaving both outputs untouched - unless `edge` is straight, belongs
// to `body`, has exactly two adjacent faces, and those faces' outward normals
// actually define a bisector (opposed normals, which a seam edge or a
// zero-thickness sliver gives, have none).
bool bevelAxis(const TopoDS_Shape& body, const TopoDS_Edge& edge, gp_Pnt& centre,
               gp_Dir& outward);

// Bake a rigid transform (+ uniform scale) into the shape's geometry via
// BRepBuilderAPI_Transform with copy = true. gp_Trsf carries
// rotation/translation/uniform scale; refuses a null body or a transform
// whose scale factor is <= 0.
BooleanResult transformShape(const TopoDS_Shape& body, const gp_Trsf& trsf);

// Round `delta` onto steps the user can predict, and hand back a rebuilt
// transform - never a nudged copy of the original, because the three
// components are not independent. `pivot` is the point `delta`'s rotation
// and scale leave fixed (the transform gizmo's own position), and it is
// what makes the decomposition well posed:
//
//   delta = Translate(t) . Scale(pivot, s) . Rotate(axis through pivot, a)
//
// so t is exactly `pivot.Transformed(delta) - pivot`, and snapping t on its
// own is meaningful. Snapping gp_Trsf::TranslationPart() instead would be
// wrong for anything but a pure translation: a rotation about a pivot away
// from the origin carries a translation part of `pivot - R*pivot`, which is
// a consequence of the rotation rather than a movement of its own.
//
// A step <= 0 leaves that component alone, so a caller can snap one thing
// and not another. `rotationStepDeg` is in degrees for readability at the
// call site; the angle itself is radians throughout.
//
// A scale that would round to zero or below is pulled back up to one step.
// That is about the RESULT being well formed, not about avoiding a refusal:
// gp_Trsf and BRepBuilderAPI_Transform want a positive factor, and handing
// them a zero one is a kernel error rather than an answer. A caller with its
// own sanity band is free to refuse the one-step value that comes back - and
// MainWindow's does, since its band excludes both ends - but it refuses a
// meaningful number with an explanation, which is not the same thing as the
// snap having produced a degenerate transform.
gp_Trsf snapTransform(const gp_Trsf& delta, const gp_Pnt& pivot,
                      double translationStep, double rotationStepDeg,
                      double scaleStep);

// True when `trsf` moves nothing: no translation past `linearTolerance`, no
// rotation past `angularToleranceDeg`, and a scale factor within
// `linearTolerance` of one. The one definition of "this drag netted
// nothing", so the gizmo's cancel path and any test asserting it agree.
bool isIdentityTransform(const gp_Trsf& trsf, double linearTolerance = 1.0e-7,
                         double angularToleranceDeg = 1.0e-5);

// --- Mitre end (improvements item 4) -----------------------------------------
//
// Cuts a board's end off at an angle, the way a mitre saw does - across the
// board's WIDTH, or tilted through its THICKNESS, one or the other and never
// both at once (no compound cut). Which of the end face's four edges keeps the
// board's full length is a MitreSide (below).
//
// THE BOARD FRAME IS THE END FACE'S OWN, and never a world axis. This branch's
// most repeated bug was an oriented quantity measured in world terms - a world
// bounding box of a rotated board reports the box's diagonal, not the board -
// so every number below comes off the face itself:
//
//   - outward:   the end face's OUTWARD normal, which is the board's length
//                axis pointing out of the wood. Derived exactly as pullFace()
//                derives it (the orientation flag as a guess, then a
//                BRepClass3d_SolidClassifier probe from a point genuinely on
//                the face), because the flag alone reads a mirrored twin's end
//                backwards.
//   - the two in-plane axes: the face's LONGEST boundary edge (its component
//                along `outward` removed) and outward x that. The face's own
//                VERTICES are projected onto both, and the longer extent is
//                the board's WIDTH, the shorter its THICKNESS -
//                Joinery::findContact's regionAxis/planeExtent discipline.
//   - length:    how far the body reaches behind the end face along -outward,
//                from the body's own vertices.
//
//   The two in-plane axes are then SIGNED so that, looking at the end face
//   from outside the wood, widthAxis points right and thicknessAxis up:
//   thicknessAxis = outward x widthAxis, always. (Which way "right" is on a
//   given board still follows from its longest edge - the model has no camera
//   - but the four sides are then a fixed turn around the face.)
//
// THE CUT pivots about one of the end face's four EDGES - the pivot edge,
// which keeps the board's full length. The cut plane contains that edge's
// direction and is rotated by `angleDeg` away from the end face's plane about
// the edge; the triangular prism beyond the plane comes off. `angleDeg` is the
// saw's own angle, measured from a square cut: 45 removes a right-isosceles
// prism. With `span` the end face's extent ACROSS from the pivot edge and
// `sweep` its extent ALONG it, the volume removed is exactly
//
//     0.5 * span * (span * tan(angleDeg)) * sweep
//
// whenever span * tan(angleDeg) does not exceed the length:
//   - a WIDTH side pivots on an edge running along the thickness, so the cut
//     swings across the width (a mitre): 0.5 * W * (W tan a) * T;
//   - a THICKNESS side pivots on an edge running along the width, so the cut
//     tilts through the thickness: 0.5 * T * (T tan a) * W.
//
// The tool is a triangular-ish prism bounded to the end face's own width and
// thickness (plus a margin), not an unbounded half-space: a half-space would
// also shear off anything of the body that happens to lie beyond the plane
// well away from this end.
//
// Refuses (ok == false, null shape, a sentence in `error`): a null body or
// face; a face that is not one of `body`'s own faces (pullFace's guard - a
// mis-wired pick must refuse, not cut an unrelated boolean); a non-planar
// face; a degenerate face with no width or thickness; an angle outside
// [1, 89]; a cut whose far end would run past the board's length (it would
// take the whole end off); and any kernel failure. The result goes through
// ShapeUpgrade_UnifySameDomain like every boolean here.
// Which edge of the end face keeps the board's full length. Named by the
// board's own axes; the words in brackets are the user's picture, looking at
// the end from outside the wood with widthAxis pointing right (see above):
//
//   WidthA      pivot on the width's LOW edge   [left]   - across the width
//   ThicknessA  pivot on the thickness's HIGH edge [top] - through the thickness
//   WidthB      pivot on the width's HIGH edge  [right]  - across the width
//   ThicknessB  pivot on the thickness's LOW edge [bottom] - through the thickness
//
// Declared in the order Flip steps through them: left -> top -> right ->
// bottom -> left. nextMitreSide() is that step, the one place it is written.
enum class MitreSide { WidthA, ThicknessA, WidthB, ThicknessB };
MitreSide nextMitreSide(MitreSide side);
// True for the two sides whose cut tilts through the thickness.
bool mitreSideIsThickness(MitreSide side);

struct MitreFrame {
    // On the end face's plane, on the pivot edge, halfway along it.
    gp_Pnt pivot;
    // In the end face's plane, from the pivot edge toward the opposite edge.
    gp_Dir across{0.0, 1.0, 0.0};
    // The end face's outward normal - the board's length axis, out of the wood.
    gp_Dir outward{1.0, 0.0, 0.0};
    // Along the pivot edge: in the end face's plane, perpendicular to
    // `across`. The cut plane contains it, so the angle is measured in the
    // plane square to it - which is where the app's dial lies.
    gp_Dir pivotAxis{0.0, 0.0, 1.0};
    // The board's own in-plane axes, signed as described above
    // (thicknessAxis = outward x widthAxis), whichever side is asked for.
    gp_Dir widthAxis{0.0, 1.0, 0.0};
    gp_Dir thicknessAxis{0.0, 0.0, 1.0};
    double span = 0.0;        // the end face's extent along `across`
    double sweep = 0.0;       // its extent along `pivotAxis`
    double width = 0.0;       // the end face's longer in-plane extent
    double thickness = 0.0;   // its shorter one
    double length = 0.0;      // the body behind the end face, along -outward
    MitreSide side = MitreSide::WidthA;
};

// The frame above for `endFace`, with the pivot on the edge `side` names.
// False - with a sentence in *why when given, and `out` untouched - for every
// refusal mitreEnd() makes that does not depend on the angle. mitreEnd() is
// built on this, so the two can never disagree about what a board is.
bool mitreFrame(const TopoDS_Shape& body, const TopoDS_Face& endFace, MitreSide side,
                MitreFrame& out, std::string* why = nullptr);

// Whether `endFace` can be mitred at all - mitreFrame()'s own answer, so the
// app's enabled state asks the geometry's one implementation of every
// refusal rather than a copy of it.
bool canMitreEnd(const TopoDS_Shape& body, const TopoDS_Face& endFace,
                 std::string* why = nullptr);

// Which of mitreEnd()'s refusals applies BEFORE the kernel is asked, as a
// value a caller can put its own sentence to - the app's chip and its Failure
// toast both read this, so neither re-derives a rule this file owns (the same
// reason BooleanResult::combinationRefused exists rather than a substring
// match on `error`). Ok means mitreEnd() will ask the kernel; the kernel can
// still refuse, which mitreEnd()'s own ok == false reports.
enum class MitreCheck { Ok, AngleOutOfRange, NotABoardEnd, RunsPastTheEnd };
// RunsPastTheEnd is `span * tan(angleDeg)` past the length - the width on a
// width side, the thickness on a thickness side.
MitreCheck checkMitre(const TopoDS_Shape& body, const TopoDS_Face& endFace, double angleDeg,
                      MitreSide side, MitreFrame* frame = nullptr, std::string* why = nullptr);

BooleanResult mitreEnd(const TopoDS_Shape& body, const TopoDS_Face& endFace, double angleDeg,
                       MitreSide side);

// --- Slats (improvements item 11) --------------------------------------------
//
// A slatted front: pick the flat face of a panel and the tool fills it with
// evenly spaced battens standing proud of it. The user's own reference is a
// wardrobe front in vertical lamas - two dozen of them, all the same, and
// laying those one at a time is the work this replaces.
//
// THE FACE IS THE AREA. It gives all three things the layout needs - the
// rectangle to fill, the plane to lay them on, and which way is out - so
// there is nothing to aim and nothing for a caller to supply that could
// disagree with the wood. The frame is the FACE'S OWN, never a world axis:
// the same discipline mitreFrame() keeps, and for the same reason (a panel
// turned 30 degrees is still a panel).
//
// WHICH WAY THE SLATS RUN is derived, not asked for: they run along the
// face's SHORTER extent and repeat along its longer one, which is what a
// wide cabinet front in vertical slats looks like. `runAcross` swaps that for
// the case the geometry cannot guess (a tall narrow door in horizontal
// slats).
//
// THE ENDS COME OUT FLUSH. Given the width and the gap the user asked for,
// the count is floor((extent + gap) / (width + gap)) and the PITCH is then
// re-derived as (extent - width) / (count - 1), so the first slat's edge sits
// on one end of the face and the last slat's on the other. The gap that
// results is the asked-for gap or a hair more, never less, and never a stub
// of leftover face at one end - which is what a made panel actually looks
// like, and what nobody wants to work out by hand.
struct SlatPlan {
    double width = 20.0;    // across the run, millimetres
    double gap = 12.0;      // the smallest gap between two slats
    double depth = 18.0;    // how far they stand proud of the face
    bool runAcross = false;  // true swaps which axis they run along
};

// What slatsOnFace() would refuse before the kernel is asked - a value, not a
// sentence, so the app maps it to its own copy and nothing re-derives a rule
// this file owns (checkMitre()/checkResize()'s contract). `countOut` carries
// how many slats would be laid, which the chip shows live.
enum class SlatCheck { Ok, NotAFlatFace, SizeOutOfRange, NoneFit };
SlatCheck checkSlats(const TopoDS_Shape& body, const TopoDS_Face& face, const SlatPlan& plan,
                     int* countOut = nullptr, std::string* why = nullptr);

// One shape per slat, in the order they are laid across the face. They are
// separate bodies, deliberately: the user renames, hides, joints and mirrors
// them like any other piece, and a single fused shell could do none of that.
// Never fused to the host panel either - the panel is still its own body.
struct SlatResult {
    bool ok = false;
    std::vector<TopoDS_Shape> slats;
    std::string error;
};
SlatResult slatsOnFace(const TopoDS_Shape& body, const TopoDS_Face& face, const SlatPlan& plan);

// The numbers a run of slats ALREADY laid is made of - read back off the
// bodies themselves rather than stored anywhere. That is what lets the tool
// reopen on a folder of slats and change their size: measure what is there,
// show it, rebuild it. Nothing persists, so nothing can go stale, and a file
// written before this tool existed opens exactly as it always did.
//
// False when `slats` is not a run this tool could have made: fewer than two
// shapes, boxes that are not all the same size, or centres that do not sit on
// one line at one pitch. `plane` comes back as the face they stand on - their
// own back plane, its normal pointing OUT the way they stand - with `area`
// the rectangle they cover, measured in that plane's own axes.
struct SlatArea {
    gp_Pln plane{gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)};
    // The rectangle, in the plane's own (u, v) as ElSLib::Parameters reads
    // them - the same coordinates snapToPlaneGrid() and the grid already use.
    double uLo = 0.0, uHi = 0.0, vLo = 0.0, vHi = 0.0;
};
bool slatRunFromBodies(const std::vector<TopoDS_Shape>& slats, SlatPlan& plan, SlatArea& area);

// The same layout as slatsOnFace(), over a rectangle measured earlier rather
// than a face measured now - what a rebuild uses, so the two paths lay slats
// by ONE implementation and a rebuilt run cannot drift from a fresh one.
SlatResult slatsOnArea(const SlatArea& area, const SlatPlan& plan);

// --- Re-Measure (improvements item 8) ----------------------------------------
//
// Retype one of a body's three sizes and the body changes to match. The user
// right-clicks a size number drawn around the selection (the selection sizes,
// below) and types a new one; this is the geometry underneath that.
//
// THE FACES MOVE; NOTHING IS SCALED. gp_Trsf has no per-axis scale and
// GTransform would turn every planar face into a NURBS surface (CLAUDE.md's
// own ruling on transformShape), so a 600 mm board asked for 450 would come
// back 0.75x in its thickness too - which is not what "make it 450 long"
// means to anybody cutting wood. So the END FACE at the moving end is pulled
// by the difference, through pullFace() itself: one implementation of the
// outward normal, of the mirrored-body classifier probe, and of every kernel
// refusal. Every other side is untouched, to the micron.
//
// THE SIZE IS MEASURED OFF measuredBox(), never off a world bounding box. The
// number the user right-clicked came from that box, so the number being
// changed has to come from the same place or the two disagree the moment a
// board is turned: the world AABB of a 600 x 300 board at 30 degrees about Z
// reads 669.6, and typing 450 against THAT would move the end by -219.6
// instead of -150. `axis` is therefore one of the measured box's own three
// axes (either sign - a direction, not an end), and a direction that is not
// one of them is refused rather than guessed at.
//
// WHICH END STAYS PUT is the Anchor, in the axis's own direction:
//   Low     - the end at the low side of `axis` stays; the high end moves.
//   Centre  - BOTH ends move by half. The default (the user's own change to
//             the picked mockup), and the reason the three cases cannot be
//             told apart by volume alone: a test that only measures the
//             result's extent passes for all three.
//   High    - the high end stays; the low end moves.
//
// Refuses (ok == false, null shape, a sentence in `error` - the file's own
// contract): a null body; a size that is not greater than zero; an `axis`
// that is not one of the body's own sides; an end that must move and has no
// SINGLE planar face square to `axis` there (a mitred, rounded or stepped
// end - expected, and reported rather than approximated); a result that is
// not one solid; and any kernel refusal pullFace() makes, including a shrink
// that would consume the body. A refusal writes nothing.
//
// A new size equal to the current one within kResizeNoChange is not a
// refusal: it succeeds with the body handed straight back, so a caller that
// commits on Enter without retyping anything cannot be told a no-op failed.
enum class ResizeAnchor { Low, Centre, High };

// Whether resizeAlongAxis() will reach the kernel at all, as a value a caller
// can put its own sentence to - checkMitre()'s own contract, and for its own
// reason: `error` is written for this file and never shown, and a caller
// matching substrings of it would break the first time a sentence was
// reworded. `currentExtent`, when given, receives the body's size along
// `axis` as measuredBox() reads it, so a UI can seed its field from the same
// number the operation will compare against.
// AN END THAT CANNOT BE PULLED IS NOT A REFUSAL AT ALL (improvements item
// 13). A board whose end is a mitre, a chamfer or a rounded corner is not one
// flat face square to the axis, and this used to report EndNotFlat and stop
// there - which is the exact case the user reported ("if I add bevel or a cut
// angle in the shape i could not use the re measure tool"). Its length is
// taken out of the MIDDLE now (stretchAlongAxis below), so the enumerator is
// gone rather than left unreachable: a code no caller can ever see is a
// sentence in the UI that no user can ever be shown. What is left is
// NoStraightPart - there is a middle, or there is not.
enum class ResizeCheck { Ok, NotMeasurable, SizeNotPositive, AxisNotASide,
                         NoStraightPart };
ResizeCheck checkResize(const TopoDS_Shape& body, const gp_Dir& axis, double newExtentMm,
                        ResizeAnchor anchor, double* currentExtent = nullptr,
                        std::string* why = nullptr);

// How near the current size counts as no change at all - a micron, which is
// three orders below anything furniture is cut to and two above the 1e-7
// pullFace() refuses a distance below (so a Centre resize, which moves each
// end by half, can never hand the kernel a distance it will refuse).
constexpr double kResizeNoChange = 1.0e-6;

// Takes the difference out of the MIDDLE of the body rather than off its end:
// cuts across the straight part, slides the far piece along `axis` by `delta`
// (positive grows, negative shrinks), and joins the two again. The LOW end
// stays where it is and the high end moves - the anchor is the caller's to
// apply, by shifting the whole result afterwards.
//
// What it is for: a board with a mitre, a chamfer or a rounded end cannot
// have that end pulled, and every feature has to come through the edit at its
// own size. The result is MEASURED against the length that was asked for
// before it is returned, so a shape with no straight middle refuses rather
// than coming back the wrong length.
BooleanResult stretchAlongAxis(const TopoDS_Shape& body, const gp_Dir& axis, double delta);

BooleanResult resizeAlongAxis(const TopoDS_Shape& body, const gp_Dir& axis, double newExtentMm,
                              ResizeAnchor anchor);

// --- Symmetry (Milestone 3) -------------------------------------------------
//
// Reflects `shape` across `plane` - gp_Trsf::SetMirror(gp_Ax2(plane.Location(),
// plane.Axis().Direction())), applied through BRepBuilderAPI_Transform with
// copy = true, the same builder transformShape() uses. A mirror is a
// negative-determinant transform; BRepBuilderAPI_Transform is documented to
// correct face orientation for one, so the result's volume comes back
// POSITIVE - pinned by test rather than assumed, because a flipped-orientation
// solid that measures negative is exactly the "looks fine, measures wrong"
// mistake this file exists to catch.
//
// Refuses a null shape and any kernel exception - the same contract as every
// other function here: ok == false always carries a null shape.
BooleanResult mirrorShape(const TopoDS_Shape& shape, const gp_Pln& plane);

// True when `shape`'s AXIS-ALIGNED bounding box has corners on both sides of
// `plane` - the minimum signed distance among its eight corners is negative
// past `tolerance` AND the maximum is positive past it. This is what decides
// whether a freshly extruded body gets a mirror twin: a body that already
// straddles the symmetry plane would produce a twin overlapping the body
// itself, which is not a second piece of furniture. False (never straddling)
// for a null shape or a void bounding box.
bool boundingBoxStraddlesPlane(const TopoDS_Shape& shape, const gp_Pln& plane,
                               double tolerance = 1.0e-7);

// The six ready-made shapes (Milestone 5, "add primitive shapes", pick A).
// Each is built STANDING on the plane z = base.Z(), centred on base in XY -
// the "placed on the ground where the camera looks" contract the flyout
// promises - at a fixed furniture-sensible size in millimetres: a 400 box,
// a 300-diameter cylinder/sphere/cone (400 tall where a height exists), a
// 400 ramp wedge, an 800 x 400 x 18 plank. Sizes live HERE, not at the UI
// call site, so the headless test asserts the same numbers the app places
// and the two cannot drift.
enum class PrimitiveKind { Box, Cylinder, Sphere, Cone, Wedge, Plank };
TopoDS_Shape makePrimitive(PrimitiveKind kind, const gp_Pnt& base);

// Must run before display or STL export, or curved faces render faceted / not at all.
void tessellate(const TopoDS_Shape& shape, double linearDeflection = 0.1);

StepResult exportStep(const TopoDS_Shape& shape, const std::string& path);

double volume(const TopoDS_Shape& shape);
int countFaces(const TopoDS_Shape& shape);
int countSolids(const TopoDS_Shape& shape);

// BRepGProp::VolumeProperties' own centre of mass - the same measure
// `volume()` reads Mass() off. Origin for a null shape. Milestone 4's
// linked copies use this to place a translation-only link "centre to
// centre" (DocumentModel::linkExisting) without duplicating the
// GProp_GProps call the headless suite already had its own local copy of.
gp_Pnt centreOfMass(const TopoDS_Shape& shape);

// The centre of `shape`'s AXIS-ALIGNED bounding box, which is NOT its centre
// of mass: a carved body's mass centre need not lie in its own material, and a
// handle standing there would float in the hole it was carved out of.
//
// It is the pivot every body-transform handle stands on, and it lives here -
// in the Qt-free library, headless-tested - rather than at the call site,
// because every consumer must agree EXACTLY: all three body gizmos
// (src/ui/TransformGizmo.cpp) stand on it, the rotate and scale transforms
// pivot about it, and the tools are interchangeable by a keypress - a handle
// that jumped a few millimetres as the user cycled Space would be reporting
// a difference that does not exist.
//
// False, leaving `out` untouched, for a null shape or a void box - there is no
// such point, and the origin would be a plausible-looking lie.
bool boundingBoxCentre(const TopoDS_Shape& shape, gp_Pnt& out);

// --- Selection sizes (improvements item 5) -----------------------------------
//
// The box the viewport draws its width, depth and height around when bodies
// are selected: one body's own box, or one box around a whole group.
//
// "MEASURED ALONG ITS OWN SIDES" MEANS AN ORIENTED BOX. The world axis-aligned
// box of a 600 x 300 board turned 30 degrees about Z reports a width of
// 600 cos 30 + 300 sin 30 = 669.6, which is not any side of that board - the
// oriented-quantity-measured-in-world-terms mistake this app has made more
// often than any other. So the box is OCCT's Bnd_OBB (BRepBndLib::AddOBB),
// built in its OPTIMAL mode on the exact B-rep rather than the triangulation.
// Both choices were measured on OCCT 8.0.1: on a 600 x 300 x 18 box axis-
// aligned, turned 30 degrees about Z and tilted 37 degrees about (1, 2, 3),
// every mode reads the sides to six decimals, but on a tessellated cylinder
// the triangulation-based modes read 299.9 (the mesh sits inside the surface)
// and the non-optimal exact mode 300.1, while optimal-on-exact reads 300.
//
// THE WORLD AXES WIN WHEN THEY FIT JUST AS WELL. An OBB's axes are arbitrary
// wherever the shape does not pin them - measured: a sphere's optimal OBB
// came back turned (0.104, 0.994, -0.027), a cone's by 0.002 - and a skewed
// frame puts slanted dimension lines, and odd numbers, on an ordinary cabinet.
// So the world-aligned box (BRepBndLib::AddOptimal, exact, no tolerance
// enlargement) is computed too and USED whenever its volume is within
// kPreferWorldBoxTolerance of the oriented box's. 1% because the gap it has
// to absorb is optimizer slack, which measured at zero to six decimals on
// every shape above, while a board turned by a visible amount inflates its
// world box by far more: a 600 x 300 x 18 board at 1 degree about Z is +4.4%,
// at 0.25 degrees +1.1%. Below a quarter of a degree a board reads its world
// extents, which is a sub-millimetre difference on furniture-sized sides.
//
// Consequence, stated rather than implied: the rule is about FIT, not about
// how the pieces themselves sit. A group of axis-aligned pieces laid out along
// a diagonal (three 100 x 100 squares stepping 200 in X and Y) fits a turned
// box of a third the volume, and is measured along that diagonal.
//
// WHICH EXTENT IS WHICH. `height` is the extent along the axis closest to
// world Z (largest |axis . Z|; heightAxis is signed to point up). Of the
// other two, `width` is the longer and `depth` the shorter - a tie gives
// width to whichever axis came first (world X in the world frame).
// depthAxis = heightAxis x widthAxis, so (width, depth, height) is right-
// handed; widthAxis is signed so its largest component is positive, which
// makes the frame deterministic for a caller laying out lines.
//
// Refuses (ok == false, `error` says why) an empty list, a list with a null
// shape, and a union with no extent at all (a void box).
constexpr double kPreferWorldBoxTolerance = 0.01;

struct MeasuredBox {
    bool ok = false;
    std::string error;
    gp_Pnt centre;
    gp_Dir widthAxis{1.0, 0.0, 0.0};
    gp_Dir depthAxis{0.0, 1.0, 0.0};
    gp_Dir heightAxis{0.0, 0.0, 1.0};
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;
    // True when the world-aligned box was chosen (see above).
    bool worldAligned = false;

    // A corner: each sign is -1 or +1 along widthAxis, depthAxis, heightAxis.
    gp_Pnt corner(int widthSign, int depthSign, int heightSign) const;
};

MeasuredBox measuredBox(const std::vector<TopoDS_Shape>& shapes);

// How many times measuredBox() has run in this process - the app caches the
// box on the selection and the document revision, and the suite counts calls
// to prove an orbit never recomputes it. A count of the real work, not of a
// cache's own bookkeeping, so a cache that silently stopped caching shows.
long long measuredBoxCallCount();

}  // namespace ModelingOps
