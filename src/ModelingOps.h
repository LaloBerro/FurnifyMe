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
};

struct StepResult {
    bool ok = false;
    std::string error;
};

// Closed polyline through `points`. Returns a null wire if fewer than 3 points.
TopoDS_Wire makePolygonWire(const std::vector<gp_Pnt>& points);

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
// outward normal is derived here, the same way MainWindow::lockToFace does
// it: BRepAdaptor_Surface never applies TopAbs_Orientation, so a REVERSED
// face's plane normal is flipped before use.
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
// WHAT THE CLIP DOES NOT REMOVE, stated rather than hidden: at a corner where
// the picked edge meets its neighbour at anything other than a right angle,
// the part of the propagated strip lying on the picked edge's OWN side of
// that end plane survives, as a patch a fraction of the bevel size across. It
// cannot be cut away without cutting the picked edge's own bevel short at
// exactly the corner where it should run right up to the neighbour, and a
// bevel that stops short of its own corner is the worse of the two. What the
// clip removes is the whole-edge case the bug was about - a neighbouring edge
// rounded along its entire length. gui_smoke pins this by counting only
// strips longer than four radii, which is the difference between a rounded
// edge and a corner.
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

// Must run before display or STL export, or curved faces render faceted / not at all.
void tessellate(const TopoDS_Shape& shape, double linearDeflection = 0.1);

StepResult exportStep(const TopoDS_Shape& shape, const std::string& path);

double volume(const TopoDS_Shape& shape);
int countFaces(const TopoDS_Shape& shape);
int countSolids(const TopoDS_Shape& shape);

}  // namespace ModelingOps
