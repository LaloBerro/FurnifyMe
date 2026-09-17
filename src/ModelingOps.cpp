#include "ModelingOps.h"

#include <algorithm>
#include <memory>
#include <sstream>

#include <cmath>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_LocalOperation.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_OBB.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

namespace ModelingOps {

TopoDS_Wire makePolygonWire(const std::vector<gp_Pnt>& points)
{
    if (points.size() < 3) return TopoDS_Wire();

    BRepBuilderAPI_MakePolygon poly;
    for (const gp_Pnt& p : points) poly.Add(p);
    poly.Close();
    if (!poly.IsDone()) return TopoDS_Wire();
    return poly.Wire();
}

TopoDS_Face makeFaceFromWire(const TopoDS_Wire& wire)
{
    if (wire.IsNull()) return TopoDS_Face();

    BRepBuilderAPI_MakeFace mkFace(wire, Standard_True /* only plane */);
    if (!mkFace.IsDone()) return TopoDS_Face();
    return mkFace.Face();
}

// A point genuinely ON `face`'s own material - not merely inside its
// bounding box, and NOT its area centroid. This app's canonical face has a
// hole in it (the slab-with-a-rectangular-through-hole CLAUDE.md's own STEP
// export check pins), and BRepGProp::SurfaceProperties' centroid is the
// centroid of OUTER-MINUS-INNER area: for a centred hole that point lands
// exactly in the hole, on no material at all - fix round 1, found by
// review before it ever reached the suite. Samples a grid of the face's own
// UV parameter space and classifies each candidate with
// BRepClass_FaceClassifier, the one classifier that reads every wire (the
// outer boundary AND any hole) rather than trusting a bounding box or an
// area-weighted average. False (leaving `out` untouched) only for a
// genuinely degenerate face no sample lands inside - not expected for
// anything this app builds, and the caller (outwardPlane(), below) falls
// back to the flag-based guess alone rather than refuse the pull over it.
//
// Declared in the header, and NOT in this file's anonymous namespace, since
// Joinery's contact finder needs exactly this - an interior point of a
// shared contact region, which for an L- or C-shaped region its area
// centroid is not. One sampler, one set of pitfalls learned once.
bool pointOnFace(const TopoDS_Face& face, gp_Pnt& out)
{
    Standard_Real umin = 0.0, umax = 0.0, vmin = 0.0, vmax = 0.0;
    BRepTools::UVBounds(face, umin, umax, vmin, vmax);
    const Handle(Geom_Surface) geometry = BRep_Tool::Surface(face);
    if (geometry.IsNull()) return false;

    constexpr int kGrid = 9;   // odd, so the exact centre is sampled too
    for (int iu = 0; iu < kGrid; ++iu) {
        const double u = umin + (umax - umin) * (iu + 0.5) / kGrid;
        for (int iv = 0; iv < kGrid; ++iv) {
            const double v = vmin + (vmax - vmin) * (iv + 0.5) / kGrid;
            BRepClass_FaceClassifier classifier(face, gp_Pnt2d(u, v), 1.0e-7);
            if (classifier.State() == TopAbs_IN) {
                out = geometry->Value(u, v);
                return true;
            }
        }
    }
    return false;
}

namespace {

// True when `direction` actually carries the profile off its own plane, which
// is the only way a prism can enclose a volume. A non-planar profile (nothing
// in this app builds one, but the signature allows it) is left to the kernel:
// there is no single plane to compare against.
bool sweepLeavesThePlane(const TopoDS_Face& profile, const gp_Dir& direction)
{
    const Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(profile));
    if (plane.IsNull()) return true;

    const gp_Dir normal = plane->Pln().Axis().Direction();
    return std::fabs(gp_Vec(direction).Dot(gp_Vec(normal))) > 1.0e-7;
}

// A shared, cheap sanity gate for the direct-modeling operations below:
// IsDone()/non-null alone is not enough (the whole point of the pitfall this
// project keeps rediscovering), so every result also passes
// BRepCheck_Analyzer before it is handed back as ok == true.
bool isShapeSane(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return false;
    const BRepCheck_Analyzer analyzer(shape);
    return analyzer.IsValid();
}

// BRepAdaptor_Surface carries geometry and location only - it never applies
// TopAbs_Orientation. On a plain box built by BRepPrimAPI_MakeBox three of
// six faces are REVERSED and their plane normals point into the body, and
// the flag alone says which - the fast guess below.
//
// It stops being reliable the moment `body` came from a MIRROR (Milestone
// 3's own twin bodies). A mirror is a negative-determinant transform, and
// BRepBuilderAPI_Transform's copy rebuild toggles a mirrored shape's face
// orientation flags UNIFORMLY to keep the B-rep valid - independent of
// whether a given face's own geometric normal happened to change direction
// under that particular reflection. A face square to the mirror axis (a
// box's top, mirrored across a vertical plane) keeps its raw normal exactly
// and flips its orientation flag anyway, so the flag-only rule reads it
// backwards. Measured: pulling that face by +20 (a "grow" gesture) landed
// at 6000 mm3 instead of the correct 8400 - net INWARD - before the check
// below existed, which is exactly the edit-then-mirror /
// mirror-then-edit commute property this file is pinned against.
//
// Settled with ground truth rather than trusted a second time:
// BRepClass3d_SolidClassifier, stepped a small distance off the face along
// the candidate normal from a point genuinely ON the face - pointOnFace(),
// above; ITS OWN comment covers why that point is not simply the face's
// centre of mass. This is orientation-flag-independent, so it is correct
// whether or not `body` was built by a mirror, and it costs one classifier
// build on the COMMIT path only (pullFaceBy calls this once per gesture,
// never per drag frame).
gp_Pln outwardPlane(const TopoDS_Shape& body, const TopoDS_Face& face,
                    const BRepAdaptor_Surface& surface)
{
    const gp_Pln plane = surface.Plane();
    gp_Dir candidate = plane.Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) candidate.Reverse();

    // Ground truth overrides the flag-based guess.
    gp_Pnt onFace;
    if (pointOnFace(face, onFace)) {
        const gp_Pnt probe = onFace.Translated(gp_Vec(candidate) * 0.01);
        BRepClass3d_SolidClassifier classifier(body);
        classifier.Perform(probe, 1.0e-6);
        if (classifier.State() == TopAbs_IN) candidate.Reverse();
    }

    // gp_Ax3's (P, N, Vx) constructor keeps Vx as the X direction when it is
    // already perpendicular to N, which it is here (candidate only ever
    // differs from the plane's own axis by a sign flip) - so only the
    // normal (and with it the derived Y direction) changes.
    return gp_Pln(gp_Ax3(plane.Location(), candidate, plane.Position().XDirection()));
}

// filletEdge/chamferEdge get this check for free - BRepFilletAPI throws on
// an edge foreign to the shape. pullFace has no such kernel-level guard: a
// prism built from a foreign face and fused/cut against `body` is a
// perfectly well-formed boolean between two unrelated shapes, so OCCT
// happily reports success. Without this, one mis-wired pick from the UI
// (the gizmo tasks feed a picked TopoDS_Face straight in) silently produces
// two disconnected solids instead of a refusal.
bool faceBelongsToBody(const TopoDS_Shape& body, const TopoDS_Face& face)
{
    for (TopExp_Explorer it(body, TopAbs_FACE); it.More(); it.Next()) {
        if (it.Current().IsSame(face)) return true;
    }
    return false;
}

// The face's OUTWARD normal at (or nearest to) `at`. outwardPlane() above
// answers the same question for a face that is known planar and is asked
// about as a whole; this one answers it at a POINT, which is what an edge's
// neighbour needs - the far side of an earlier fillet is a cylinder, and its
// normal is a different direction at every point along it.
//
// The planar short-circuit is not an optimisation for its own sake: every
// straight edge of a box-shaped body has two planar faces on it, and
// bevelAxis() runs on every appStateChanged in the app that drives it.
bool outwardNormalNear(const TopoDS_Face& face, const gp_Pnt& at, gp_Dir& out)
{
    if (face.IsNull()) return false;

    BRepAdaptor_Surface surface(face);
    gp_Dir normal;
    if (surface.GetType() == GeomAbs_Plane) {
        normal = surface.Plane().Axis().Direction();
    } else {
        const Handle(Geom_Surface) geometry = BRep_Tool::Surface(face);
        if (geometry.IsNull()) return false;
        GeomAPI_ProjectPointOnSurf projector(at, geometry);
        if (!projector.IsDone() || projector.NbPoints() < 1) return false;
        double u = 0.0;
        double v = 0.0;
        projector.LowerDistanceParameters(u, v);
        BRepLProp_SLProps properties(surface, u, v, 1, 1.0e-7);
        if (!properties.IsNormalDefined()) return false;
        normal = properties.Normal();
    }

    // The flip, for the third time in this project - see bevelAxis()'s header.
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    out = normal;
    return true;
}

// The edge counterpart of faceBelongsToBody. BRepFilletAPI_MakeFillet throws
// Standard_Failure on a foreign edge and BRepFilletAPI_MakeChamfer is
// documented to do NOTHING at all ("nothing is done if edge E does not belong
// to the initial shape") - two different wrong answers from one mistake, and
// the chamfer's is the dangerous one, since a builder with no contours can
// still hand back a perfectly valid unchanged body. Checking membership here
// makes both refuse the same way, with a reason, before either builder is
// asked.
bool edgeBelongsToBody(const TopoDS_Shape& body, const TopoDS_Edge& edge)
{
    for (TopExp_Explorer it(body, TopAbs_EDGE); it.More(); it.Next()) {
        if (it.Current().IsSame(edge)) return true;
    }
    return false;
}

bool edgeIsStraight(const TopoDS_Edge& edge)
{
    if (edge.IsNull()) return false;
    return BRepAdaptor_Curve(edge).GetType() == GeomAbs_Line;
}

// The region of space between the two planes perpendicular to `edge` at its
// own endpoints - wide enough, in the other two directions, to swallow
// `body` whole. Everything a bevel of `edge` may legitimately remove lies
// inside it, and everything a propagated contour reached by walking OUT
// through one of those endpoints lies outside it. Null for an edge with no
// single direction (a curved or zero-length one), which is what makes the
// caller refuse rather than clip.
TopoDS_Shape edgeExtentSlab(const TopoDS_Shape& body, const TopoDS_Edge& edge)
{
    if (!edgeIsStraight(edge)) return TopoDS_Shape();

    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2);
    if (v1.IsNull() || v2.IsNull()) return TopoDS_Shape();
    const gp_Pnt p1 = BRep_Tool::Pnt(v1);
    const gp_Pnt p2 = BRep_Tool::Pnt(v2);
    const gp_Vec along(p1, p2);
    const double length = along.Magnitude();
    if (length < 1.0e-7) return TopoDS_Shape();

    Bnd_Box bounds;
    BRepBndLib::Add(body, bounds);
    if (bounds.IsVoid()) return TopoDS_Shape();
    // The body's own diagonal, so the slab is wide enough for any body
    // without a magic number that a large one would outgrow.
    const double reach = std::sqrt(bounds.SquareExtent()) * 2.0 + 10.0;

    const gp_Dir direction(along);
    const gp_Ax2 frame(p1, direction);
    const gp_Pnt corner =
        p1.Translated(gp_Vec(frame.XDirection()) * -reach + gp_Vec(frame.YDirection()) * -reach);
    BRepPrimAPI_MakeBox slab(gp_Ax2(corner, direction, frame.XDirection()), reach * 2.0,
                             reach * 2.0, length);
    slab.Build();
    if (!slab.IsDone()) return TopoDS_Shape();
    return slab.Shape();
}

// Did the builder's contours pull in an edge nobody asked for? See
// filletEdges()' header for why they do.
bool contourReachesBeyond(const BRepFilletAPI_LocalOperation& op,
                          const std::vector<TopoDS_Edge>& requested)
{
    for (int contour = 1; contour <= op.NbContours(); ++contour) {
        for (int index = 1; index <= op.NbEdges(contour); ++index) {
            const TopoDS_Edge& inContour = op.Edge(contour, index);
            bool asked = false;
            for (const TopoDS_Edge& edge : requested) {
                if (inContour.IsSame(edge)) { asked = true; break; }
            }
            if (!asked) return true;
        }
    }
    return false;
}

// Put back everything `raw` removed outside the picked edges' own extents.
// Refuses only where there is no honest answer at all: a picked edge with no
// perpendicular pair (curved, degenerate) gives nothing to clip against.
BooleanResult clipBevelToPickedEdges(const TopoDS_Shape& body, const TopoDS_Shape& raw,
                                     const std::vector<TopoDS_Edge>& requested,
                                     const std::string& what)
{
    BooleanResult out;

    TopoDS_Shape restore = body;
    for (const TopoDS_Edge& edge : requested) {
        const TopoDS_Shape slab = edgeExtentSlab(body, edge);
        if (slab.IsNull()) {
            out.error = what + ": the kernel spread this onto neighbouring edges and "
                               "a curved edge gives nothing to clip it against";
            return out;
        }
        const BooleanResult trimmed = applyBoolean(BooleanKind::Cut, restore, slab);
        if (!trimmed.ok) {
            out.error = what + ": containing the spread failed - " + trimmed.error;
            return out;
        }
        restore = trimmed.shape;
        if (restore.IsNull() || countSolids(restore) == 0) break;
    }

    // NOTHING OUTSIDE THE PICKED EXTENTS - so nothing to put back, and the
    // kernel's own result is the answer. This is not a failure and must never
    // be reported as one: one picked edge that spans the body in its own
    // direction is enough to make the slabs cover it, which a Shift-selection
    // on a box reaches in two clicks. Every propagated edge is then inside
    // some picked edge's extent, and the shape that comes back is exactly the
    // one this app shipped before the clip existed. Refusing here told the
    // user to try a smaller size when no size could ever work - the geometry
    // decides whether the slabs cover the body, not the radius. See the
    // header.
    if (restore.IsNull() || countSolids(restore) == 0) {
        out.ok = true;
        out.shape = raw;
        return out;
    }

    const BooleanResult rejoined = applyBoolean(BooleanKind::Fuse, raw, restore);
    if (!rejoined.ok) {
        out.error = what + ": containing the spread failed - " + rejoined.error;
        return out;
    }
    out.ok = true;
    out.shape = rejoined.shape;
    return out;
}

// Every requested edge, present in some contour the builder made? See
// filletEdges()' header: Add() takes or drops each edge on its own, so a list
// with one dropped edge still builds - and bevels the rest, silently.
bool everyRequestedEdgeWasTaken(const BRepFilletAPI_LocalOperation& op,
                                const std::vector<TopoDS_Edge>& requested)
{
    for (const TopoDS_Edge& edge : requested) {
        bool taken = false;
        for (int contour = 1; contour <= op.NbContours() && !taken; ++contour) {
            for (int index = 1; index <= op.NbEdges(contour); ++index) {
                if (op.Edge(contour, index).IsSame(edge)) { taken = true; break; }
            }
        }
        if (!taken) return false;
    }
    return true;
}

// One body for fillet and chamfer both: they differ only in which OCCT
// builder does the work, and every refusal, the contour check and the
// containment are the same rules for the two. Writing them twice is how the
// two drift.
template <typename Builder>
BooleanResult bevelEdgesWith(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges,
                             double size, const std::string& what)
{
    BooleanResult out;
    if (body.IsNull()) {
        out.error = what + ": body is null";
        return out;
    }
    if (edges.empty()) {
        out.error = what + ": no edge given";
        return out;
    }
    if (size <= 0.0) {
        out.error = what + ": size must be positive";
        return out;
    }
    for (const TopoDS_Edge& edge : edges) {
        if (edge.IsNull()) {
            out.error = what + ": one of the edges is null";
            return out;
        }
        if (!edgeBelongsToBody(body, edge)) {
            out.error = what + ": an edge does not belong to the body";
            return out;
        }
    }

    try {
        Builder builder(body);
        for (const TopoDS_Edge& edge : edges) builder.Add(size, edge);
        // ALL-OR-NOTHING, enforced here and nowhere else. NbContours() == 0
        // alone catches only "took none of them": Add() decides per edge, so
        // a three-edge list with one dropped leaves two contours, builds, and
        // returns a body with two of the three bevelled - a partial result
        // reported as a success, which is the exact shape of failure this
        // file exists to prevent. Both cases carry combinationRefused,
        // because the answer to them is a different edge selection and never
        // a different size.
        if (builder.NbContours() == 0) {
            out.combinationRefused = true;
            out.error = what + ": the kernel accepted none of these edges";
            return out;
        }
        if (!everyRequestedEdgeWasTaken(builder, edges)) {
            out.combinationRefused = true;
            out.error = what + ": the kernel accepted only some of these edges, and a "
                               "partial bevel is not an outcome this offers";
            return out;
        }
        const bool spread = contourReachesBeyond(builder, edges);

        builder.Build();
        // OCCT bevels legitimately fail on hard geometry (e.g. a radius that
        // would eat a neighbouring face) - IsDone() false is a normal
        // outcome here, not a bug, and must carry through as ok == false.
        if (!builder.IsDone()) {
            out.error = what + ": the kernel could not build this size on these edges";
            return out;
        }
        TopoDS_Shape result = builder.Shape();
        if (!isShapeSane(result)) {
            out.error = what + ": result is empty or invalid";
            return out;
        }

        if (spread) {
            const BooleanResult contained = clipBevelToPickedEdges(body, result, edges, what);
            if (!contained.ok) return contained;
            result = contained.shape;
            if (!isShapeSane(result) || countSolids(result) != 1) {
                out.error = what + ": containing the spread left an invalid result";
                return out;
            }
        }

        out.ok = true;
        out.shape = result;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.shape = TopoDS_Shape();
        out.error = what + ": kernel exception - " +
                    (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

}  // namespace

TopoDS_Shape extrude(const TopoDS_Face& profile, const gp_Dir& direction, double height)
{
    if (profile.IsNull() || height == 0.0) return TopoDS_Shape();

    // A sweep direction that lies IN the profile's own plane sweeps the face
    // across itself: the result has no volume at all. BRepPrimAPI_MakePrism
    // still reports IsDone() for it, so a caller checking only IsDone() would
    // put a flat, empty body into the document, name it, and export it - the
    // exact shape of "a failed operation surfaced as a success" that CLAUDE.md
    // forbids. Refuse it here, in the geometry, rather than trusting every UI
    // path to have thought of it: the app reaches this by locking a different
    // plane while a closed outline is still pending, and the UI blocks that
    // too, but a rule enforced only where somebody remembered it is not a rule.
    //
    // The threshold is on the sine of the angle between the sweep and the
    // plane's normal, so it is exactly "in the plane" that is refused and a
    // legitimately shallow sweep still builds.
    if (!sweepLeavesThePlane(profile, direction)) return TopoDS_Shape();

    const gp_Vec sweep = gp_Vec(direction) * height;
    BRepPrimAPI_MakePrism prism(profile, sweep);
    prism.Build();
    if (!prism.IsDone()) return TopoDS_Shape();
    return prism.Shape();
}

TopoDS_Shape makeBox(const gp_Pnt& corner, double dx, double dy, double dz)
{
    BRepPrimAPI_MakeBox box(corner, dx, dy, dz);
    box.Build();
    if (!box.IsDone()) return TopoDS_Shape();
    return box.Shape();
}

BooleanResult applyBoolean(BooleanKind kind,
                           const TopoDS_Shape& a,
                           const TopoDS_Shape& b,
                           double fuzzyValue)
{
    BooleanResult out;
    if (a.IsNull() || b.IsNull()) {
        out.error = "boolean: one or both operands are null";
        return out;
    }

    std::unique_ptr<BRepAlgoAPI_BooleanOperation> algo;
    switch (kind) {
        case BooleanKind::Fuse:   algo.reset(new BRepAlgoAPI_Fuse());   break;
        case BooleanKind::Cut:    algo.reset(new BRepAlgoAPI_Cut());    break;
        case BooleanKind::Common: algo.reset(new BRepAlgoAPI_Common()); break;
    }

    TopTools_ListOfShape arguments;
    TopTools_ListOfShape tools;
    arguments.Append(a);
    tools.Append(b);
    algo->SetArguments(arguments);
    algo->SetTools(tools);
    algo->SetRunParallel(Standard_True);
    algo->SetFuzzyValue(fuzzyValue);
    algo->Build();

    if (!algo->IsDone() || algo->HasErrors()) {
        std::ostringstream why;
        algo->DumpErrors(why);
        out.error = why.str().empty() ? "boolean failed (no detail reported)" : why.str();
        return out;
    }

    // Merge the coplanar faces the boolean leaves behind.
    ShapeUpgrade_UnifySameDomain unify(algo->Shape(), Standard_True, Standard_True, Standard_True);
    unify.Build();

    out.ok = true;
    out.shape = unify.Shape();
    return out;
}

TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape>& shapes)
{
    if (shapes.empty()) return TopoDS_Shape();
    if (shapes.size() == 1) return shapes.front();

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const TopoDS_Shape& s : shapes) {
        if (!s.IsNull()) builder.Add(compound, s);
    }
    return compound;
}

BooleanResult pullFace(const TopoDS_Shape& body, const TopoDS_Face& face, double distance)
{
    BooleanResult out;
    if (body.IsNull() || face.IsNull()) {
        out.error = "pull: body or face is null";
        return out;
    }
    if (std::fabs(distance) < 1.0e-7) {
        out.error = "pull: distance is effectively zero";
        return out;
    }
    if (!faceBelongsToBody(body, face)) {
        out.error = "pull: face does not belong to the body";
        return out;
    }

    try {
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) {
            out.error = "pull: face is not planar";
            return out;
        }

        const gp_Pln plane = outwardPlane(body, face, surface);
        const gp_Dir outward = plane.Axis().Direction();
        // Growing sweeps outward and fuses the prism on; carving sweeps
        // INWARD by the same amount and cuts that prism away - a carve tool
        // built along the outward normal would sit entirely outside the
        // body and remove nothing.
        const gp_Dir sweepDir = (distance > 0.0) ? outward : outward.Reversed();

        const TopoDS_Shape prism = extrude(face, sweepDir, std::fabs(distance));
        if (prism.IsNull()) {
            out.error = "pull: prism build failed";
            return out;
        }

        const BooleanKind kind = (distance > 0.0) ? BooleanKind::Fuse : BooleanKind::Cut;
        const BooleanResult result = applyBoolean(kind, body, prism);
        if (!result.ok) {
            out.error = "pull: " + result.error;
            return out;
        }

        // A carve that consumes the body entirely is a valid boolean and a
        // useless result - IsDone() alone would surface it as a success.
        if (result.shape.IsNull() || countSolids(result.shape) == 0 ||
            volume(result.shape) < 1.0e-6) {
            out.error = "pull: the carve removed the entire body";
            return out;
        }
        if (!isShapeSane(result.shape)) {
            out.error = "pull: result failed validity check";
            return out;
        }

        out.ok = true;
        out.shape = result.shape;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.error = std::string("pull: kernel exception - ") +
                     (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

BooleanResult filletEdges(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges,
                          double radius)
{
    return bevelEdgesWith<BRepFilletAPI_MakeFillet>(body, edges, radius, "fillet");
}

// Symmetric chamfer, both adjacent faces - BRepFilletAPI_MakeChamfer's
// Add(distance, edge) overload.
BooleanResult chamferEdges(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges,
                           double distance)
{
    return bevelEdgesWith<BRepFilletAPI_MakeChamfer>(body, edges, distance, "chamfer");
}

BooleanResult filletEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge, double radius)
{
    return filletEdges(body, std::vector<TopoDS_Edge>{edge}, radius);
}

BooleanResult chamferEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge, double distance)
{
    return chamferEdges(body, std::vector<TopoDS_Edge>{edge}, distance);
}

bool bevelAxis(const TopoDS_Shape& body, const TopoDS_Edge& edge, gp_Pnt& centre,
               gp_Dir& outward)
{
    if (body.IsNull() || edge.IsNull()) return false;

    // Straight only. BRepFilletAPI will round a curved edge perfectly well,
    // but the gesture this drives measures a drag against ONE fixed axis, and
    // an edge whose direction changes along its length has no single
    // perpendicular for that axis to be.
    if (BRepAdaptor_Curve(edge).GetType() != GeomAbs_Line) return false;

    TopoDS_Vertex first, last;
    TopExp::Vertices(edge, first, last);
    if (first.IsNull() || last.IsNull()) return false;
    const gp_Pnt a = BRep_Tool::Pnt(first);
    const gp_Pnt b = BRep_Tool::Pnt(last);
    const gp_Vec along(a, b);
    if (along.Magnitude() < 1.0e-7) return false;
    const gp_Pnt midpoint(0.5 * (a.X() + b.X()), 0.5 * (a.Y() + b.Y()),
                          0.5 * (a.Z() + b.Z()));

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    const int index = edgeToFaces.FindIndex(edge);
    if (index <= 0) return false;   // not this body's edge at all
    const TopTools_ListOfShape& faces = edgeToFaces.FindFromIndex(index);
    // Exactly two. A seam or a free edge has one, and a non-manifold junction
    // has more; neither has a bisector to drag along.
    if (faces.Extent() != 2) return false;

    gp_Vec bisector(0.0, 0.0, 0.0);
    for (const TopoDS_Shape& neighbour : faces) {
        gp_Dir normal;
        if (!outwardNormalNear(TopoDS::Face(neighbour), midpoint, normal)) return false;
        bisector += gp_Vec(normal);
    }

    // Perpendicular to the edge, by construction rather than by luck - see the
    // header. On a box the subtraction removes nothing; on a body whose faces
    // meet the edge at an angle it is what keeps the arrow on the edge.
    // Braces, not parentheses: `gp_Vec direction(gp_Dir(along))` is a function
    // declaration, not a variable - C++'s most vexing parse, and MSVC's error
    // for it names the wrong line.
    const gp_Vec direction{gp_Dir(along)};
    bisector -= direction * bisector.Dot(direction);
    if (bisector.Magnitude() < 1.0e-7) return false;   // opposed normals: no bisector

    centre = midpoint;
    outward = gp_Dir(bisector);
    return true;
}

BooleanResult transformShape(const TopoDS_Shape& body, const gp_Trsf& trsf)
{
    BooleanResult out;
    if (body.IsNull()) {
        out.error = "transform: body is null";
        return out;
    }
    if (trsf.ScaleFactor() <= 0.0) {
        out.error = "transform: scale factor must be positive";
        return out;
    }

    try {
        BRepBuilderAPI_Transform transform(body, trsf, Standard_True /* copy geometry */);
        if (!transform.IsDone()) {
            out.error = "transform: kernel failed to apply the transform";
            return out;
        }
        const TopoDS_Shape result = transform.Shape();
        if (!isShapeSane(result)) {
            out.error = "transform: result is empty or invalid";
            return out;
        }

        out.ok = true;
        out.shape = result;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.error = std::string("transform: kernel exception - ") +
                     (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

namespace {
constexpr double kPi = 3.14159265358979323846;

// One step, or the value untouched when the step is not a step. Every
// component of snapTransform() rounds through here so "a step <= 0 leaves
// that component alone" is one rule rather than three copies of it.
double snapToStep(double value, double step)
{
    if (step <= 0.0) return value;
    return std::round(value / step) * step;
}
}  // namespace

gp_Trsf snapTransform(const gp_Trsf& delta, const gp_Pnt& pivot,
                      double translationStep, double rotationStepDeg,
                      double scaleStep)
{
    // The three components, pulled apart about the pivot - see the header for
    // why the translation is read off the pivot's own movement rather than
    // gp_Trsf::TranslationPart().
    const double scale = delta.ScaleFactor();
    gp_Vec axisVec;
    Standard_Real angle = 0.0;
    delta.GetRotation().GetVectorAndAngle(axisVec, angle);
    const gp_Vec movement(pivot, pivot.Transformed(delta));

    double snappedScale = snapToStep(scale, scaleStep);
    // A snap must never be what makes a transform illegal: the kernel refuses
    // a factor <= 0, so a shrink that rounds to nothing is held at one step
    // instead. The caller's own sanity clamp then has something to refuse.
    if (snappedScale <= 0.0) snappedScale = scaleStep > 0.0 ? scaleStep : scale;

    const double stepRad = rotationStepDeg * kPi / 180.0;
    const double snappedAngle = snapToStep(angle, stepRad);

    const gp_Vec snappedMove(snapToStep(movement.X(), translationStep),
                             snapToStep(movement.Y(), translationStep),
                             snapToStep(movement.Z(), translationStep));

    // Rebuilt in the order the decomposition names, not edited in place.
    // Rotation and scale both fix the pivot, so they compose either way round;
    // the translation has to come last, or it would itself be scaled.
    gp_Trsf out;
    if (std::fabs(snappedAngle) > 1.0e-12 && axisVec.Magnitude() > 1.0e-12) {
        gp_Trsf rotation;
        rotation.SetRotation(gp_Ax1(pivot, gp_Dir(axisVec)), snappedAngle);
        out = rotation;
    }
    if (std::fabs(snappedScale - 1.0) > 1.0e-12) {
        gp_Trsf scaling;
        scaling.SetScale(pivot, snappedScale);
        out = scaling * out;
    }
    if (snappedMove.Magnitude() > 1.0e-12) {
        gp_Trsf translation;
        translation.SetTranslation(snappedMove);
        out = translation * out;
    }
    return out;
}

bool isIdentityTransform(const gp_Trsf& trsf, double linearTolerance,
                         double angularToleranceDeg)
{
    if (std::fabs(trsf.ScaleFactor() - 1.0) > linearTolerance) return false;
    if (trsf.TranslationPart().Modulus() > linearTolerance) return false;

    gp_Vec axis;
    Standard_Real angle = 0.0;
    trsf.GetRotation().GetVectorAndAngle(axis, angle);
    return std::fabs(angle) <= angularToleranceDeg * kPi / 180.0;
}

// --- Re-Measure (improvements item 8) ---------------------------------------

namespace {

// The body's own span along `axis`, from its vertices. Used to decide WHICH
// end a face sits at; the size itself never comes from here (that is
// measuredBox()'s answer, see the header) - this only has to agree with it
// about which end is which, which two extremes of the same direction always
// do.
bool vertexSpanAlong(const TopoDS_Shape& shape, const gp_Dir& axis, double& lo, double& hi)
{
    const gp_Vec along(axis);
    bool any = false;
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
        const double d = gp_Vec(p.X(), p.Y(), p.Z()).Dot(along);
        if (!any) {
            lo = hi = d;
            any = true;
        } else {
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
    }
    return any;
}

// The ONE planar face square to `axis` at `highEnd`'s end of the body, or a
// null face. `found` reports how many were there, so a caller can tell "this
// end is a mitre" (0) from "this end is a step or an L" (2 or more) - both
// refuse, and neither may be approximated by picking one of them.
TopoDS_Face endFaceAlong(const TopoDS_Shape& body, const gp_Dir& axis, bool highEnd, int& found)
{
    found = 0;
    double lo = 0.0, hi = 0.0;
    if (!vertexSpanAlong(body, axis, lo, hi)) return TopoDS_Face();
    const double target = highEnd ? hi : lo;
    const double tol = 1.0e-6 * std::max(1.0, hi - lo) + 1.0e-7;
    const gp_Vec along(axis);

    TopoDS_Face best;
    for (TopExp_Explorer it(body, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) continue;
        const gp_Pln pln = surface.Plane();
        // Square to the axis - the geometric normal, no orientation flag
        // needed: both senses of a perpendicular plane are square to it.
        if (std::fabs(gp_Vec(pln.Axis().Direction()).Dot(along)) < 1.0 - 1.0e-6) continue;
        const gp_Pnt at = pln.Location();
        if (std::fabs(gp_Vec(at.X(), at.Y(), at.Z()).Dot(along) - target) > tol) continue;
        ++found;
        best = face;
    }
    if (found != 1) return TopoDS_Face();
    return best;
}

// The measured box's extent along `axis`, whichever of its three axes that is
// (either sign). False when `axis` is not one of them.
bool measuredExtentAlong(const MeasuredBox& box, const gp_Dir& axis, double& extent)
{
    const gp_Vec along(axis);
    const gp_Dir axes[3] = {box.widthAxis, box.depthAxis, box.heightAxis};
    const double sizes[3] = {box.width, box.depth, box.height};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(gp_Vec(axes[i]).Dot(along)) >= 1.0 - 1.0e-6) {
            extent = sizes[i];
            return true;
        }
    }
    return false;
}

}  // namespace

ResizeCheck checkResize(const TopoDS_Shape& body, const gp_Dir& axis, double newExtentMm,
                        ResizeAnchor anchor, double* currentExtent, std::string* why)
{
    auto refuse = [&](ResizeCheck code, const char* text) {
        if (why) *why = text;
        return code;
    };
    if (body.IsNull()) return refuse(ResizeCheck::NotMeasurable, "resize: body is null");
    if (!(newExtentMm > kResizeNoChange))
        return refuse(ResizeCheck::SizeNotPositive, "resize: the new size must be above zero");

    const MeasuredBox box = measuredBox(std::vector<TopoDS_Shape>{body});
    if (!box.ok) return refuse(ResizeCheck::NotMeasurable, "resize: the body has no extent");

    double extent = 0.0;
    if (!measuredExtentAlong(box, axis, extent))
        return refuse(ResizeCheck::AxisNotASide, "resize: that direction is not one of the "
                                                 "body's own sides");
    if (currentExtent) *currentExtent = extent;

    const double delta = newExtentMm - extent;
    // Already that size: nothing has to move, so no end has to be flat.
    if (std::fabs(delta) < kResizeNoChange) return ResizeCheck::Ok;

    // Which end(s) this anchor moves - Centre moves both, so both must be
    // flat for the gesture to be honest about what it is going to do.
    const bool movesHigh = anchor != ResizeAnchor::High;
    const bool movesLow = anchor != ResizeAnchor::Low;
    int found = 0;
    const bool highPullable = !movesHigh || !endFaceAlong(body, axis, true, found).IsNull();
    const bool lowPullable = !movesLow || !endFaceAlong(body, axis, false, found).IsNull();
    if (highPullable && lowPullable) return ResizeCheck::Ok;

    // AN END THAT CANNOT BE PULLED IS NO LONGER A REFUSAL (improvements item
    // 13). A mitred, chamfered or rounded end is not one flat face square to
    // the axis, and pulling it would shear the feature off - so the length
    // comes out of the MIDDLE instead (stretchAlongAxis). What this check
    // still answers is whether there is a middle to take it out of: the cut
    // goes through the halfway point, so the board has to be longer than
    // twice the difference for the piece being copied or removed to be
    // straight board rather than one of its own ends.
    if (extent <= 2.0 * std::fabs(delta))
        return refuse(ResizeCheck::NoStraightPart,
                      "resize: this shape has no straight part to take the length out of");
    return ResizeCheck::Ok;
}

// --- the stretch (improvements item 13) -------------------------------------
//
// Re-Measure moves an end FACE, which needs that end to be one flat face
// square to the axis. A mitred, chamfered or rounded end is not, so the tool
// used to refuse the board outright - and a board with a 45 on one end is
// exactly the board somebody wants to make 150 shorter.
//
// So when the end cannot be pulled, the length is taken out of the MIDDLE
// instead: cut the board across its straight part, slide the end piece along
// the axis by the difference, and join the two again. Every feature survives
// at its own size - a 45 stays 45, an 8 mm chamfer stays 8 mm - and the
// thickness and depth are untouched, which is the promise re-measure makes
// everywhere else.
//
// It is all booleans against a bounded box, never a half-space: a half-space
// would also take anything of the body lying beyond the plane well away from
// this board (the mitre tool's own ruling, one file over).
namespace {

// Defined further down this file (the mitre frame's own helper): the range of
// a shape's vertices projected on a direction. Declared here rather than
// moved, so the one implementation stays where its own neighbours are.
void vertexRange(const TopoDS_Shape& shape, const gp_Dir& axis, double& lo, double& hi);

// A solid slab covering the body's whole cross-section, between `from` and
// `to` measured along `axis` - the cutting tool every step below commons
// against.
TopoDS_Shape axisSlab(const TopoDS_Shape& body, const gp_Dir& axis, double from, double to)
{
    if (!(to > from)) return TopoDS_Shape();
    // Any two directions square to the axis; which two does not matter, since
    // the slab is grown past the body in both.
    // Any direction square to the axis: cross it with whichever world axis it
    // is least parallel to, so the cross product is never degenerate.
    const gp_Dir world = std::fabs(axis.Z()) < 0.9 ? gp_Dir(0.0, 0.0, 1.0) : gp_Dir(1.0, 0.0, 0.0);
    const gp_Dir u = axis.Crossed(world);
    const gp_Dir v = axis.Crossed(u);

    double uLo = 0.0, uHi = 0.0, vLo = 0.0, vHi = 0.0;
    vertexRange(body, u, uLo, uHi);
    vertexRange(body, v, vLo, vHi);
    if (uHi < uLo || vHi < vLo) return TopoDS_Shape();
    // A margin proportional to the body, so no slab face ever lands coplanar
    // with a body face it merely touches.
    const double margin = 10.0 + (uHi - uLo) + (vHi - vLo);

    const gp_Pnt corner(u.XYZ() * (uLo - margin) + v.XYZ() * (vLo - margin) +
                        axis.XYZ() * from);
    // gp_Ax2's Y is Z x X, so with Z = axis and X = u the box runs along
    // axis x u = v - which is the third axis of the same frame.
    const gp_Ax2 axes(corner, axis, u);
    BRepPrimAPI_MakeBox maker(axes, (uHi - uLo) + 2.0 * margin, (vHi - vLo) + 2.0 * margin,
                              to - from);
    maker.Build();
    return maker.IsDone() ? maker.Shape() : TopoDS_Shape();
}

TopoDS_Shape partOf(const TopoDS_Shape& body, const gp_Dir& axis, double from, double to)
{
    const TopoDS_Shape slab = axisSlab(body, axis, from, to);
    if (slab.IsNull()) return TopoDS_Shape();
    const BooleanResult common = applyBoolean(BooleanKind::Common, body, slab);
    return common.ok ? common.shape : TopoDS_Shape();
}

TopoDS_Shape movedAlong(const TopoDS_Shape& shape, const gp_Dir& axis, double distance)
{
    if (shape.IsNull()) return shape;
    gp_Trsf move;
    move.SetTranslation(gp_Vec(axis) * distance);
    const BooleanResult moved = transformShape(shape, move);
    return moved.ok ? moved.shape : TopoDS_Shape();
}

}  // namespace

BooleanResult stretchAlongAxis(const TopoDS_Shape& body, const gp_Dir& axis, double delta)
{
    BooleanResult out;
    if (body.IsNull()) {
        out.error = "stretch: body is null";
        return out;
    }
    if (std::fabs(delta) < kResizeNoChange) {
        out.ok = true;
        out.shape = body;
        return out;
    }

    try {
        double lo = 0.0, hi = 0.0;
        vertexRange(body, axis, lo, hi);
        if (hi - lo < 1.0e-6) {
            out.error = "stretch: the body has no length along that side";
            return out;
        }

        // THE CUT GOES THROUGH THE MIDDLE, which is the part of a board least
        // likely to carry a feature: the ends are where mitres, chamfers and
        // rounds live, and anything else this cut lands on is still a
        // straight section as long as the board's own profile does not change
        // there. The result is checked against the size that was asked for
        // below, so a cut that lands somewhere it should not be reports
        // rather than shipping a wrong board.
        const double cut = 0.5 * (lo + hi);

        // The low part stays where it is; the high part slides by `delta`.
        // Which END that leaves standing still is the ANCHOR's business, and
        // the caller shifts the whole result afterwards - one convention
        // here, three anchors there.
        TopoDS_Shape low;
        if (delta > 0.0) {
            // Growing: the new material is a copy of the straight slab just
            // below the cut, moved up into the gap. It is a piece of this
            // very board, so its cross-section IS the board's - no section
            // face to build and no chance of one being built wrong.
            if (cut - delta <= lo + 1.0e-6) {
                out.error = "stretch: there is not enough straight board to grow from";
                return out;
            }
            low = partOf(body, axis, lo - 1.0, cut);
            const TopoDS_Shape filler = movedAlong(partOf(body, axis, cut - delta, cut), axis, delta);
            if (low.IsNull() || filler.IsNull()) {
                out.error = "stretch: the board could not be cut across its middle";
                return out;
            }
            const BooleanResult joined = applyBoolean(BooleanKind::Fuse, low, filler);
            if (!joined.ok) {
                out.error = "stretch: " + joined.error;
                return out;
            }
            low = joined.shape;
        } else {
            // Shrinking: the low part simply stops `delta` earlier, and the
            // high part comes back to meet it.
            if (cut + delta <= lo + 1.0e-6) {
                out.error = "stretch: that size is shorter than the board's own ends";
                return out;
            }
            low = partOf(body, axis, lo - 1.0, cut + delta);
            if (low.IsNull()) {
                out.error = "stretch: the board could not be cut across its middle";
                return out;
            }
        }

        const TopoDS_Shape high = movedAlong(partOf(body, axis, cut, hi + 1.0), axis, delta);
        if (high.IsNull()) {
            out.error = "stretch: the board could not be cut across its middle";
            return out;
        }

        const BooleanResult joined = applyBoolean(BooleanKind::Fuse, low, high);
        if (!joined.ok) {
            out.error = "stretch: " + joined.error;
            return out;
        }
        if (countSolids(joined.shape) != 1) {
            out.error = "stretch: the result is not one body";
            return out;
        }
        // THE RESULT IS MEASURED, not assumed. The cut is placed at the
        // middle on the expectation that the middle is straight; if it was
        // not - a shape that tapers, or one whose ends reach past halfway -
        // the board that comes out is not the length that was asked for, and
        // a wrong board reported as a success is the one outcome this file
        // forbids.
        double newLo = 0.0, newHi = 0.0;
        vertexRange(joined.shape, axis, newLo, newHi);
        if (std::fabs((newHi - newLo) - ((hi - lo) + delta)) > 1.0e-3) {
            out.error = "stretch: this shape has no straight part to take the length out of";
            return out;
        }
        out.ok = true;
        out.shape = joined.shape;
        return out;
    } catch (const Standard_Failure& e) {
        out.error = std::string("stretch: ") + e.GetMessageString();
        return out;
    }
}

BooleanResult resizeAlongAxis(const TopoDS_Shape& body, const gp_Dir& axis, double newExtentMm,
                              ResizeAnchor anchor)
{
    BooleanResult out;
    std::string why;
    double extent = 0.0;
    const ResizeCheck check = checkResize(body, axis, newExtentMm, anchor, &extent, &why);
    if (check != ResizeCheck::Ok) {
        out.error = why;
        return out;
    }

    const double delta = newExtentMm - extent;
    if (std::fabs(delta) < kResizeNoChange) {
        // Already that size - see the header: a no-op is not a refusal.
        out.ok = true;
        out.shape = body;
        return out;
    }

    // How far each end moves OUTWARD. Centre is half each, which is what makes
    // it a third case rather than a spelling of Low: the centre of mass stays
    // where it was, and neither end does.
    struct Step {
        bool high;
        double move;
    };
    std::vector<Step> steps;
    switch (anchor) {
        case ResizeAnchor::Low: steps.push_back({true, delta}); break;
        case ResizeAnchor::High: steps.push_back({false, delta}); break;
        case ResizeAnchor::Centre:
            steps.push_back({true, 0.5 * delta});
            steps.push_back({false, 0.5 * delta});
            break;
    }

    // WHICH MECHANISM: pulling an end face is exact and is what re-measure has
    // always done, so it stays the path whenever both ends this anchor moves
    // are flat and square. When one is not - a mitre, a chamfer, a rounded
    // end - the difference is taken out of the middle instead, and every
    // feature comes through at its own size (improvements item 13).
    {
        int found = 0;
        const bool movesHigh = anchor != ResizeAnchor::High;
        const bool movesLow = anchor != ResizeAnchor::Low;
        const bool pullable = (!movesHigh || !endFaceAlong(body, axis, true, found).IsNull()) &&
                              (!movesLow || !endFaceAlong(body, axis, false, found).IsNull());
        if (!pullable) {
            // The stretch keeps the LOW end still, so the whole result is
            // shifted afterwards to put the anchor where the caller asked
            // for it - one convention in the geometry, three anchors here.
            const BooleanResult stretched = stretchAlongAxis(body, axis, delta);
            if (!stretched.ok) {
                out.error = stretched.error;
                return out;
            }
            double shift = 0.0;
            switch (anchor) {
                case ResizeAnchor::Low: shift = 0.0; break;
                case ResizeAnchor::Centre: shift = -0.5 * delta; break;
                case ResizeAnchor::High: shift = -delta; break;
            }
            if (std::fabs(shift) > kResizeNoChange) {
                gp_Trsf move;
                move.SetTranslation(gp_Vec(axis) * shift);
                const BooleanResult moved = transformShape(stretched.shape, move);
                if (!moved.ok) {
                    out.error = "resize: " + moved.error;
                    return out;
                }
                out.shape = moved.shape;
            } else {
                out.shape = stretched.shape;
            }
            out.ok = true;
            return out;
        }
    }

    TopoDS_Shape current = body;
    for (const Step& step : steps) {
        // Re-derived from the SHAPE IN HAND rather than from the original: the
        // first pull rebuilds the body through ShapeUpgrade_UnifySameDomain,
        // so the second end's face is a different TopoDS_Face than it was
        // (CLAUDE.md's topological-naming warning, met by never carrying one
        // across a rebuild).
        int found = 0;
        const TopoDS_Face face = endFaceAlong(current, axis, step.high, found);
        if (face.IsNull()) {
            out.error = "resize: no single flat end square to that size";
            return out;
        }
        // pullFace() owns the outward normal, the mirrored-body probe and
        // every kernel refusal - positive grows that end outward, negative
        // carves it in, which is exactly "move this end by `move`".
        const BooleanResult pulled = pullFace(current, face, step.move);
        if (!pulled.ok) {
            out.error = "resize: " + pulled.error;
            return out;
        }
        current = pulled.shape;
    }

    if (countSolids(current) != 1) {
        out.error = "resize: the result is not one body";
        return out;
    }
    out.ok = true;
    out.shape = current;
    return out;
}

BooleanResult mirrorShape(const TopoDS_Shape& shape, const gp_Pln& plane)
{
    BooleanResult out;
    if (shape.IsNull()) {
        out.error = "mirror: shape is null";
        return out;
    }

    try {
        gp_Trsf trsf;
        trsf.SetMirror(gp_Ax2(plane.Location(), plane.Axis().Direction()));
        BRepBuilderAPI_Transform transform(shape, trsf, Standard_True /* copy geometry */);
        if (!transform.IsDone()) {
            out.error = "mirror: kernel failed to apply the transform";
            return out;
        }
        const TopoDS_Shape result = transform.Shape();
        if (!isShapeSane(result)) {
            out.error = "mirror: result is empty or invalid";
            return out;
        }

        out.ok = true;
        out.shape = result;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.error = std::string("mirror: kernel exception - ") +
                     (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

namespace {

// The end face's own in-plane axis: the direction of its LONGEST boundary
// edge, with any component along `normal` removed - Joinery's regionAxis(),
// for the reason recorded there: gp_Ax3(point, dir) picks X and Y out of world
// space, so extents measured against it are a world-oriented box of the face,
// which is right at 0 and 90 degrees of in-plane rotation and wrong everywhere
// in between.
bool faceOwnAxis(const TopoDS_Face& face, const gp_Dir& normal, gp_Dir& out)
{
    const gp_Vec along(normal);
    double bestLength = 0.0;
    gp_Vec best;
    for (TopExp_Explorer ie(face, TopAbs_EDGE); ie.More(); ie.Next()) {
        TopoDS_Vertex v0, v1;
        TopExp::Vertices(TopoDS::Edge(ie.Current()), v0, v1);
        if (v0.IsNull() || v1.IsNull()) continue;
        gp_Vec chord(BRep_Tool::Pnt(v0), BRep_Tool::Pnt(v1));
        chord -= along * chord.Dot(along);
        const double length = chord.Magnitude();
        if (length > bestLength + 1.0e-9) {
            bestLength = length;
            best = chord;
        }
    }
    if (bestLength <= 1.0e-9) return false;
    out = gp_Dir(best);
    return true;
}

// The [lo, hi] of `shape`'s own vertices projected onto `axis`, as absolute
// projections. lo > hi when there is no vertex at all.
void vertexRange(const TopoDS_Shape& shape, const gp_Dir& axis, double& lo, double& hi)
{
    lo = 1.0e300;
    hi = -1.0e300;
    for (TopExp_Explorer iv(shape, TopAbs_VERTEX); iv.More(); iv.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(iv.Current()));
        const double t = p.XYZ().Dot(axis.XYZ());
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
}

bool refuseFrame(std::string* why, const char* sentence)
{
    if (why) *why = sentence;
    return false;
}

}  // namespace

MitreSide nextMitreSide(MitreSide side)
{
    // left -> top -> right -> bottom -> left, the order the user picked.
    switch (side) {
        case MitreSide::WidthA: return MitreSide::ThicknessA;
        case MitreSide::ThicknessA: return MitreSide::WidthB;
        case MitreSide::WidthB: return MitreSide::ThicknessB;
        case MitreSide::ThicknessB: return MitreSide::WidthA;
    }
    return MitreSide::WidthA;
}

bool mitreSideIsThickness(MitreSide side)
{
    return side == MitreSide::ThicknessA || side == MitreSide::ThicknessB;
}

bool mitreFrame(const TopoDS_Shape& body, const TopoDS_Face& endFace, MitreSide side,
                MitreFrame& out, std::string* why)
{
    if (body.IsNull() || endFace.IsNull())
        return refuseFrame(why, "mitre: body or face is null");
    if (!faceBelongsToBody(body, endFace))
        return refuseFrame(why, "mitre: face does not belong to the body");

    try {
        const BRepAdaptor_Surface surface(endFace);
        if (surface.GetType() != GeomAbs_Plane)
            return refuseFrame(why, "mitre: face is not planar");

        // OUTWARD, the way pullFace() derives it - the flag as a guess, then
        // the classifier probe that a mirrored twin's flag needs.
        const gp_Dir outward = outwardPlane(body, endFace, surface).Axis().Direction();

        gp_Dir u;
        if (!faceOwnAxis(endFace, outward, u))
            return refuseFrame(why, "mitre: face has no straight extent to measure");
        const gp_Dir v = outward.Crossed(u);

        double uLo = 0.0, uHi = 0.0, vLo = 0.0, vHi = 0.0;
        vertexRange(endFace, u, uLo, uHi);
        vertexRange(endFace, v, vLo, vHi);
        if (uLo > uHi || vLo > vHi) return refuseFrame(why, "mitre: face has no vertices");

        // WIDTH is the longer in-plane extent, THICKNESS the shorter - measured
        // along the face's own axes, never a world box.
        // Signed so thicknessAxis = outward x widthAxis on both branches: with
        // v = outward x u, that is v itself when u is the width, and -u (its
        // range negated) when v is - outward x v = outward x (outward x u) = -u.
        const bool uIsWidth = (uHi - uLo) >= (vHi - vLo);
        const gp_Dir widthAxis = uIsWidth ? u : v;
        const gp_Dir thicknessAxis = uIsWidth ? v : u.Reversed();
        const double wLo = uIsWidth ? uLo : vLo;
        const double wHi = uIsWidth ? uHi : vHi;
        const double tLo = uIsWidth ? vLo : -uHi;
        const double tHi = uIsWidth ? vHi : -uLo;
        const double width = wHi - wLo;
        const double thickness = tHi - tLo;
        if (width < 1.0e-6 || thickness < 1.0e-6)
            return refuseFrame(why, "mitre: face has no width or no thickness");

        // How far the body reaches behind the end face, along -outward.
        const double facePlane = surface.Plane().Location().XYZ().Dot(outward.XYZ());
        double nLo = 0.0, nHi = 0.0;
        vertexRange(body, outward, nLo, nHi);
        const double length = facePlane - nLo;
        if (length < 1.0e-6)
            return refuseFrame(why, "mitre: there is no board behind this face");

        // The three axes are orthonormal, so a point is the sum of its three
        // absolute projections. Each side names the edge that keeps the
        // length (see MitreSide): its coordinate on the axis the cut swings
        // along, and halfway along the other.
        double pivotW = 0.5 * (wLo + wHi);
        double pivotT = 0.5 * (tLo + tHi);
        gp_Dir across = widthAxis;
        gp_Dir pivotAxis = thicknessAxis;
        switch (side) {
            case MitreSide::WidthA:       // [left]: the width's low edge
                pivotW = wLo;
                across = widthAxis;
                pivotAxis = thicknessAxis;
                break;
            case MitreSide::WidthB:       // [right]: the width's high edge
                pivotW = wHi;
                across = widthAxis.Reversed();
                pivotAxis = thicknessAxis;
                break;
            case MitreSide::ThicknessA:   // [top]: the thickness's high edge
                pivotT = tHi;
                across = thicknessAxis.Reversed();
                pivotAxis = widthAxis;
                break;
            case MitreSide::ThicknessB:   // [bottom]: the thickness's low edge
                pivotT = tLo;
                across = thicknessAxis;
                pivotAxis = widthAxis;
                break;
        }
        const bool throughThickness = mitreSideIsThickness(side);
        const gp_XYZ pivot = widthAxis.XYZ() * pivotW + thicknessAxis.XYZ() * pivotT +
                             outward.XYZ() * facePlane;

        out.pivot = gp_Pnt(pivot);
        out.across = across;
        out.outward = outward;
        out.pivotAxis = pivotAxis;
        out.widthAxis = widthAxis;
        out.thicknessAxis = thicknessAxis;
        out.span = throughThickness ? thickness : width;
        out.sweep = throughThickness ? width : thickness;
        out.width = width;
        out.thickness = thickness;
        out.length = length;
        out.side = side;
        return true;
    } catch (const Standard_Failure&) {
        return refuseFrame(why, "mitre: kernel exception while measuring the face");
    }
}

bool canMitreEnd(const TopoDS_Shape& body, const TopoDS_Face& endFace, std::string* why)
{
    MitreFrame ignored;
    return mitreFrame(body, endFace, MitreSide::WidthA, ignored, why);
}

MitreCheck checkMitre(const TopoDS_Shape& body, const TopoDS_Face& endFace, double angleDeg,
                      MitreSide side, MitreFrame* frameOut, std::string* why)
{
    // Angle first: it is the one refusal the frame cannot make, and an
    // out-of-range angle should say so even about a perfectly good board.
    // Written so a NaN fails it too.
    if (!(angleDeg >= 1.0 - 1.0e-9 && angleDeg <= 89.0 + 1.0e-9)) {
        if (why) *why = "mitre: the angle must be between 1 and 89 degrees";
        return MitreCheck::AngleOutOfRange;
    }

    MitreFrame frame;
    if (!mitreFrame(body, endFace, side, frame, why)) return MitreCheck::NotABoardEnd;

    // How far back along the board the cut's far end reaches - the span
    // across from the pivot edge, so the width on a width side and the
    // thickness on a thickness side. Past the length it would take the whole
    // end off, which is not a mitre.
    const double depth = frame.span * std::tan(angleDeg * kPi / 180.0);
    if (depth > frame.length + 1.0e-7) {
        if (why) *why = "mitre: the cut would run past the far end of the board";
        return MitreCheck::RunsPastTheEnd;
    }
    if (frameOut) *frameOut = frame;
    return MitreCheck::Ok;
}

BooleanResult mitreEnd(const TopoDS_Shape& body, const TopoDS_Face& endFace, double angleDeg,
                       MitreSide side)
{
    BooleanResult out;
    MitreFrame frame;
    if (checkMitre(body, endFace, angleDeg, side, &frame, &out.error) != MitreCheck::Ok)
        return out;
    const double tanA = std::tan(angleDeg * kPi / 180.0);

    try {
        // The tool, in the (across, outward) plane through the pivot, swept
        // along the pivot edge. The cut line runs from the pivot along
        // across*cos - outward*sin; the tool is the region on the END side of
        // it, bounded to the face's own span and sweep plus a margin so no
        // tool face is coplanar with a board face it merely touches. One
        // construction serves all four sides: only the frame differs.
        const double margin = std::max(1.0, 0.1 * frame.width);
        const gp_Vec a(frame.across);
        const gp_Vec n(frame.outward);
        const gp_Vec p(frame.pivotAxis);
        const gp_Pnt base = frame.pivot.Translated(p * -(0.5 * frame.sweep + margin));
        const auto at = [&](double w, double h) { return base.Translated(a * w + n * h); };

        const double nearW = -margin;
        const double farW = frame.span + margin;
        const double top = margin * tanA + margin;   // clear of the end face on both ends
        const std::vector<gp_Pnt> profile = {
            at(nearW, -nearW * tanA),   // on the cut line, behind the pivot
            at(farW, -farW * tanA),     // on the cut line, past the far edge
            at(farW, top),
            at(nearW, top),
        };
        const TopoDS_Face section = makeFaceFromWire(makePolygonWire(profile));
        if (section.IsNull()) {
            out.error = "mitre: the cutting tool could not be built";
            return out;
        }
        const TopoDS_Shape tool = extrude(section, frame.pivotAxis,
                                          frame.sweep + 2.0 * margin);
        if (tool.IsNull()) {
            out.error = "mitre: the cutting tool could not be built";
            return out;
        }

        const BooleanResult cut = applyBoolean(BooleanKind::Cut, body, tool);
        if (!cut.ok) {
            out.error = "mitre: " + cut.error;
            return out;
        }
        if (cut.shape.IsNull() || countSolids(cut.shape) != 1 || volume(cut.shape) < 1.0e-6) {
            out.error = "mitre: the cut did not leave exactly one board";
            return out;
        }
        if (!isShapeSane(cut.shape)) {
            out.error = "mitre: result failed validity check";
            return out;
        }
        out.ok = true;
        out.shape = cut.shape;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.shape = TopoDS_Shape();
        out.error = std::string("mitre: kernel exception - ") +
                    (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

bool boundingBoxStraddlesPlane(const TopoDS_Shape& shape, const gp_Pln& plane, double tolerance)
{
    if (shape.IsNull()) return false;

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return false;

    double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    const gp_Pnt origin = plane.Location();
    const gp_Dir normal = plane.Axis().Direction();
    const gp_Pnt corners[8] = {
        gp_Pnt(xmin, ymin, zmin), gp_Pnt(xmax, ymin, zmin),
        gp_Pnt(xmin, ymax, zmin), gp_Pnt(xmax, ymax, zmin),
        gp_Pnt(xmin, ymin, zmax), gp_Pnt(xmax, ymin, zmax),
        gp_Pnt(xmin, ymax, zmax), gp_Pnt(xmax, ymax, zmax),
    };

    double minDist = 0.0;
    double maxDist = 0.0;
    bool first = true;
    for (const gp_Pnt& corner : corners) {
        const double d = gp_Vec(origin, corner).Dot(gp_Vec(normal));
        if (first) { minDist = maxDist = d; first = false; }
        else {
            minDist = std::min(minDist, d);
            maxDist = std::max(maxDist, d);
        }
    }
    return minDist < -tolerance && maxDist > tolerance;
}

TopoDS_Shape makePrimitive(PrimitiveKind kind, const gp_Pnt& base)
{
    // Every builder gets an axis frame whose origin is the shape's own
    // ground-contact point (or, for the sphere, its centre lifted by one
    // radius), so "standing on z = base.Z(), centred on base" holds by
    // construction for all six - the header's contract, asserted by the
    // headless test per kind.
    switch (kind) {
        case PrimitiveKind::Box:
            return BRepPrimAPI_MakeBox(gp_Pnt(base.X() - 200.0, base.Y() - 200.0, base.Z()),
                                       400.0, 400.0, 400.0)
                .Shape();
        case PrimitiveKind::Cylinder:
            return BRepPrimAPI_MakeCylinder(gp_Ax2(base, gp_Dir(0.0, 0.0, 1.0)), 150.0,
                                            400.0)
                .Shape();
        case PrimitiveKind::Sphere:
            return BRepPrimAPI_MakeSphere(
                       gp_Pnt(base.X(), base.Y(), base.Z() + 150.0), 150.0)
                .Shape();
        case PrimitiveKind::Cone:
            return BRepPrimAPI_MakeCone(gp_Ax2(base, gp_Dir(0.0, 0.0, 1.0)), 150.0, 0.0,
                                        400.0)
                .Shape();
        case PrimitiveKind::Wedge:
            // ltx = 0: the top edge collapses to the back, a clean ramp.
            return BRepPrimAPI_MakeWedge(
                       gp_Ax2(gp_Pnt(base.X() - 200.0, base.Y() - 200.0, base.Z()),
                              gp_Dir(0.0, 0.0, 1.0), gp_Dir(1.0, 0.0, 0.0)),
                       400.0, 400.0, 400.0, 0.0)
                .Shape();
        case PrimitiveKind::Plank:
            return BRepPrimAPI_MakeBox(gp_Pnt(base.X() - 400.0, base.Y() - 200.0, base.Z()),
                                       800.0, 400.0, 18.0)
                .Shape();
    }
    return TopoDS_Shape();
}

void tessellate(const TopoDS_Shape& shape, double linearDeflection)
{
    if (shape.IsNull()) return;
    BRepMesh_IncrementalMesh mesher(shape, linearDeflection);
    mesher.Perform();
}

StepResult exportStep(const TopoDS_Shape& shape, const std::string& path)
{
    StepResult out;
    if (shape.IsNull()) {
        out.error = "STEP export: shape is null";
        return out;
    }

    STEPControl_Writer writer;
    Interface_Static::SetCVal("write.step.schema", "AP214IS");

    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
        out.error = "STEP export: transfer failed";
        return out;
    }
    if (writer.Write(path.c_str()) != IFSelect_RetDone) {
        out.error = "STEP export: write failed for " + path;
        return out;
    }

    out.ok = true;
    return out;
}

double volume(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

gp_Pnt centreOfMass(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return gp_Pnt(0.0, 0.0, 0.0);
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.CentreOfMass();
}

bool boundingBoxCentre(const TopoDS_Shape& shape, gp_Pnt& out)
{
    if (shape.IsNull()) return false;
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return false;
    Standard_Real x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    out = gp_Pnt(0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1));
    return true;
}

// --- Selection sizes ------------------------------------------------------------

namespace {
long long g_measuredBoxCalls = 0;
}  // namespace

long long measuredBoxCallCount()
{
    return g_measuredBoxCalls;
}

gp_Pnt MeasuredBox::corner(int widthSign, int depthSign, int heightSign) const
{
    const gp_Vec v = gp_Vec(widthAxis) * (0.5 * width * widthSign) +
                     gp_Vec(depthAxis) * (0.5 * depth * depthSign) +
                     gp_Vec(heightAxis) * (0.5 * height * heightSign);
    return centre.Translated(v);
}

MeasuredBox measuredBox(const std::vector<TopoDS_Shape>& shapes)
{
    ++g_measuredBoxCalls;
    MeasuredBox out;
    if (shapes.empty()) {
        out.error = "Nothing to measure: no shapes were given.";
        return out;
    }
    for (const TopoDS_Shape& s : shapes) {
        if (s.IsNull()) {
            out.error = "Nothing to measure: a shape in the list is null.";
            return out;
        }
    }
    const TopoDS_Shape all = shapes.size() == 1 ? shapes.front() : makeCompound(shapes);

    // The world-aligned box, exact: no triangulation, no tolerance enlargement
    // (a plain BRepBndLib::Add pads every box by the shape tolerance).
    Bnd_Box world;
    BRepBndLib::AddOptimal(all, world, false, false);
    if (world.IsVoid()) {
        out.error = "Nothing to measure: the shapes have no extent.";
        return out;
    }
    Standard_Real x0, y0, z0, x1, y1, z1;
    world.Get(x0, y0, z0, x1, y1, z1);

    // The oriented box: optimal mode on the exact geometry - see the header
    // for the measurements behind both flags.
    Bnd_OBB obb;
    try {
        BRepBndLib::AddOBB(all, obb, /*triangulation*/ false, /*optimal*/ true,
                           /*shapeTolerance*/ false);
    } catch (const Standard_Failure&) {
        obb.SetVoid();
    }

    gp_Dir axes[3] = {gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), gp_Dir(0, 0, 1)};
    double ext[3] = {x1 - x0, y1 - y0, z1 - z0};
    gp_Pnt centre(0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1));
    bool worldAligned = true;

    if (!obb.IsVoid()) {
        // A zero extent (a flat piece) must not make every volume equal zero
        // and so compare "within tolerance" whatever the frame - a hair of
        // floor keeps the comparison about the other two sides.
        const double hair = 1.0e-6;
        const double worldVolume = std::max(ext[0], hair) * std::max(ext[1], hair) *
                                   std::max(ext[2], hair);
        const double obbExt[3] = {2.0 * obb.XHSize(), 2.0 * obb.YHSize(), 2.0 * obb.ZHSize()};
        const double obbVolume = std::max(obbExt[0], hair) * std::max(obbExt[1], hair) *
                                 std::max(obbExt[2], hair);
        if (worldVolume > obbVolume * (1.0 + kPreferWorldBoxTolerance)) {
            worldAligned = false;
            axes[0] = obb.XDirection();
            axes[1] = obb.YDirection();
            axes[2] = obb.ZDirection();
            for (int i = 0; i < 3; ++i) ext[i] = obbExt[i];
            centre = gp_Pnt(obb.Center());
        }
    }

    // Height: the axis closest to world Z.
    int h = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::abs(axes[i].Z()) > std::abs(axes[h].Z()) + 1.0e-12) h = i;
    }
    // Width: the longer of the other two; a tie keeps the first.
    int a = (h + 1) % 3, b = (h + 2) % 3;
    if (a > b) std::swap(a, b);
    int w = a, d = b;
    if (ext[b] > ext[a] + 1.0e-9) std::swap(w, d);

    gp_Dir heightAxis = axes[h];
    if (heightAxis.Z() < 0.0) heightAxis.Reverse();
    gp_Dir widthAxis = axes[w];
    {
        const double cx = widthAxis.X(), cy = widthAxis.Y(), cz = widthAxis.Z();
        double big = cx;
        if (std::abs(cy) > std::abs(big)) big = cy;
        if (std::abs(cz) > std::abs(big)) big = cz;
        if (big < 0.0) widthAxis.Reverse();
    }

    out.ok = true;
    out.centre = centre;
    out.widthAxis = widthAxis;
    out.heightAxis = heightAxis;
    out.depthAxis = gp_Dir(gp_Vec(heightAxis).Crossed(gp_Vec(widthAxis)));
    out.width = ext[w];
    out.depth = ext[d];
    out.height = ext[h];
    out.worldAligned = worldAligned;
    return out;
}

// --- Slats (improvements item 11) --------------------------------------------

namespace {

// The layout, in one place, so checkSlats(), slatsOnFace() and slatsOnArea()
// cannot disagree about how many slats there are or where they sit. `extent`
// is the face's own extent along the axis they REPEAT on.
//
// Count first, pitch second: the count is what the asked-for width and gap
// buy, and the pitch is then stretched so the two end slats land flush with
// the ends of the face. The gap that falls out is the asked-for gap or a hair
// more - never less, which would be a panel that does not fit.
struct SlatLayout {
    int count = 0;
    double pitch = 0.0;   // low edge to low edge
};

SlatLayout layOutSlats(double extent, double width, double gap)
{
    SlatLayout out;
    if (width <= 0.0 || extent < width - 1.0e-9) return out;
    out.count = static_cast<int>(std::floor((extent + gap) / (width + gap) + 1.0e-9));
    if (out.count < 1) return out;
    out.pitch = out.count > 1 ? (extent - width) / (out.count - 1) : 0.0;
    return out;
}

// The three orthonormal axes are a basis, so a point is the sum of its three
// projections. Everything below measures in projections (a dot product
// against a direction, from the world origin) and builds points this way -
// the same arithmetic mitreFrame() uses to put its pivot on an edge.
gp_Pnt pointFromProjections(const gp_Dir& a, double da, const gp_Dir& b, double db,
                            const gp_Dir& c, double dc)
{
    return gp_Pnt(a.XYZ() * da + b.XYZ() * db + c.XYZ() * dc);
}

// The face's frame: outward, the axis the slats REPEAT along, the axis they
// RUN along, and the rectangle in those two. `along` is always
// `outward x repeat`, which is what lets a slat be built from a gp_Ax2 whose
// Y direction the kernel derives for itself.
struct SlatFrame {
    gp_Dir outward{0.0, 0.0, 1.0};
    gp_Dir repeat{1.0, 0.0, 0.0};
    gp_Dir along{0.0, 1.0, 0.0};
    double rLo = 0.0, rHi = 0.0;
    double sLo = 0.0, sHi = 0.0;
    double planeOffset = 0.0;   // the face plane's own projection on `outward`
};

// `u`/`v` are the plane's two in-plane axes with v = outward x u, and
// `repeatOnU` says which of them the slats repeat along. The caller decides
// that - the face path from the face's own proportions, the rebuild path from
// the rectangle it measured earlier - so this function never guesses.
SlatFrame frameFor(const gp_Dir& outward, const gp_Dir& u, double uLo, double uHi, double vLo,
                   double vHi, bool repeatOnU, double planeOffset)
{
    const gp_Dir v = outward.Crossed(u);

    SlatFrame frame;
    frame.outward = outward;
    frame.planeOffset = planeOffset;
    if (repeatOnU) {
        frame.repeat = u;
        frame.along = v;   // = outward x u
        frame.rLo = uLo;
        frame.rHi = uHi;
        frame.sLo = vLo;
        frame.sHi = vHi;
    } else {
        // outward x v = outward x (outward x u) = -u, so the run axis is u
        // reversed and its range is negated with it.
        frame.repeat = v;
        frame.along = u.Reversed();
        frame.rLo = vLo;
        frame.rHi = vHi;
        frame.sLo = -uHi;
        frame.sHi = -uLo;
    }
    return frame;
}

bool slatSizesInRange(const SlatPlan& plan)
{
    if (!std::isfinite(plan.width) || !std::isfinite(plan.gap) || !std::isfinite(plan.depth))
        return false;
    if (plan.width < 1.0 || plan.width > 1000.0) return false;
    if (plan.gap < 0.0 || plan.gap > 1000.0) return false;
    if (plan.depth < 0.5 || plan.depth > 1000.0) return false;
    return true;
}

SlatResult refuseSlats(const std::string& why)
{
    SlatResult result;
    result.ok = false;
    result.error = why;
    return result;
}

SlatResult slatsOnFrame(const SlatFrame& frame, const SlatPlan& plan)
{
    const SlatLayout layout = layOutSlats(frame.rHi - frame.rLo, plan.width, plan.gap);
    if (layout.count < 1) return refuseSlats("slats: not one slat fits across this face");

    SlatResult result;
    result.slats.reserve(static_cast<std::size_t>(layout.count));
    const double run = frame.sHi - frame.sLo;
    for (int i = 0; i < layout.count; ++i) {
        const double r0 = layout.count > 1
                              ? frame.rLo + layout.pitch * i
                              : frame.rLo + 0.5 * ((frame.rHi - frame.rLo) - plan.width);
        const gp_Pnt corner = pointFromProjections(frame.repeat, r0, frame.along, frame.sLo,
                                                   frame.outward, frame.planeOffset);
        const gp_Ax2 axes(corner, frame.outward, frame.repeat);
        BRepPrimAPI_MakeBox maker(axes, plan.width, run, plan.depth);
        maker.Build();
        if (!maker.IsDone()) return refuseSlats("slats: the kernel could not build a slat");
        result.slats.push_back(maker.Shape());
    }
    result.ok = true;
    return result;
}

// The face's frame, or false with a reason. The outward normal comes from
// pullFace()'s own derivation - the flag as a guess, then the classifier
// probe a mirrored twin needs - so slats stand OUT of a mirrored panel too.
bool slatFrameForFace(const TopoDS_Shape& body, const TopoDS_Face& face, bool runAcross,
                      SlatFrame& out, std::string* why)
{
    if (body.IsNull() || face.IsNull()) return refuseFrame(why, "slats: body or face is null");
    if (!faceBelongsToBody(body, face))
        return refuseFrame(why, "slats: face does not belong to the body");

    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane)
        return refuseFrame(why, "slats: face is not flat");

    const gp_Dir outward = outwardPlane(body, face, surface).Axis().Direction();
    gp_Dir u;
    if (!faceOwnAxis(face, outward, u))
        return refuseFrame(why, "slats: face has no straight extent to measure");
    const gp_Dir v = outward.Crossed(u);

    double uLo = 0.0, uHi = 0.0, vLo = 0.0, vHi = 0.0;
    vertexRange(face, u, uLo, uHi);
    vertexRange(face, v, vLo, vHi);
    if (uHi - uLo < 1.0e-6 || vHi - vLo < 1.0e-6)
        return refuseFrame(why, "slats: face has no area to fill");

    // They RUN along the face's shorter extent and REPEAT along its longer
    // one - a wide front in vertical slats, which is what the reference
    // photograph of this feature is. runAcross swaps it for the case the
    // geometry cannot guess: a tall narrow door in horizontal slats.
    const bool repeatOnU = ((uHi - uLo) >= (vHi - vLo)) != runAcross;
    const double planeOffset = surface.Plane().Location().XYZ().Dot(outward.XYZ());
    out = frameFor(outward, u, uLo, uHi, vLo, vHi, repeatOnU, planeOffset);
    return true;
}

// One of a measured box's three axes / extents by index, so the run below can
// walk them rather than writing every comparison three times.
gp_Dir boxAxis(const MeasuredBox& box, int index)
{
    return index == 0 ? box.widthAxis : (index == 1 ? box.depthAxis : box.heightAxis);
}
double boxExtent(const MeasuredBox& box, int index)
{
    return index == 0 ? box.width : (index == 1 ? box.depth : box.height);
}

}  // namespace

SlatCheck checkSlats(const TopoDS_Shape& body, const TopoDS_Face& face, const SlatPlan& plan,
                     int* countOut, std::string* why)
{
    if (countOut) *countOut = 0;
    if (!slatSizesInRange(plan)) {
        if (why) *why = "slats: width, gap or depth is out of range";
        return SlatCheck::SizeOutOfRange;
    }
    SlatFrame frame;
    if (!slatFrameForFace(body, face, plan.runAcross, frame, why)) return SlatCheck::NotAFlatFace;

    const SlatLayout layout = layOutSlats(frame.rHi - frame.rLo, plan.width, plan.gap);
    if (layout.count < 1) {
        if (why) *why = "slats: not one slat fits across this face";
        return SlatCheck::NoneFit;
    }
    if (countOut) *countOut = layout.count;
    return SlatCheck::Ok;
}

SlatResult slatsOnFace(const TopoDS_Shape& body, const TopoDS_Face& face, const SlatPlan& plan)
{
    if (!slatSizesInRange(plan))
        return refuseSlats("slats: width, gap or depth is out of range");
    try {
        SlatFrame frame;
        std::string why;
        if (!slatFrameForFace(body, face, plan.runAcross, frame, &why)) return refuseSlats(why);
        return slatsOnFrame(frame, plan);
    } catch (const Standard_Failure& e) {
        return refuseSlats(std::string("slats: ") + e.GetMessageString());
    }
}

SlatResult slatsOnArea(const SlatArea& area, const SlatPlan& plan)
{
    if (!slatSizesInRange(plan))
        return refuseSlats("slats: width, gap or depth is out of range");
    try {
        // A SlatArea's own convention: u is the axis the slats RUN along and
        // v the one they repeat along, which is how slatRunFromBodies()
        // writes it. runAcross swaps that, so flipping the direction on a
        // rebuild re-runs them the other way inside the same rectangle.
        const gp_Dir outward = area.plane.Axis().Direction();
        const gp_Dir u = area.plane.Position().XDirection();
        const gp_Dir v = outward.Crossed(u);
        // The rectangle is in OFFSETS from the plane's own location, while a
        // frame measures absolute projections - so the plane's own
        // projections are added back here.
        const double planeOffset = area.plane.Location().XYZ().Dot(outward.XYZ());
        const double uOrigin = area.plane.Location().XYZ().Dot(u.XYZ());
        const double vOrigin = area.plane.Location().XYZ().Dot(v.XYZ());
        const SlatFrame frame =
            frameFor(outward, u, uOrigin + area.uLo, uOrigin + area.uHi, vOrigin + area.vLo,
                     vOrigin + area.vHi, /*repeatOnU=*/plan.runAcross, planeOffset);
        return slatsOnFrame(frame, plan);
    } catch (const Standard_Failure& e) {
        return refuseSlats(std::string("slats: ") + e.GetMessageString());
    }
}

bool slatRunFromBodies(const std::vector<TopoDS_Shape>& slats, SlatPlan& plan, SlatArea& area)
{
    // Two is the fewest that can name a pitch, and a pitch is what makes a
    // run a run rather than two battens that happen to be parallel.
    if (slats.size() < 2) return false;

    // Every slat is measured in its OWN sides (measuredBox), so a run on a
    // panel turned 30 degrees reads its real width and depth rather than a
    // world box's idea of them - the bug class this project has paid for most
    // often.
    std::vector<MeasuredBox> boxes;
    boxes.reserve(slats.size());
    for (const TopoDS_Shape& slat : slats) {
        if (slat.IsNull()) return false;
        const MeasuredBox box = measuredBox({slat});
        if (!box.ok) return false;
        boxes.push_back(box);
    }

    // All the same size, within a tenth of a millimetre. A run somebody has
    // since edited one slat of is no longer a run this tool can rebuild, and
    // rebuilding it anyway would silently throw that edit away.
    const double tol = 0.1;
    const MeasuredBox& first = boxes.front();
    for (const MeasuredBox& box : boxes) {
        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(boxExtent(box, axis) - boxExtent(first, axis)) > tol) return false;
        }
    }

    // The line the centres sit on, taken from the two furthest apart.
    gp_Pnt lo = first.centre;
    gp_Pnt hi = first.centre;
    double longest = 0.0;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            const double d = boxes[i].centre.Distance(boxes[j].centre);
            if (d > longest) {
                longest = d;
                lo = boxes[i].centre;
                hi = boxes[j].centre;
            }
        }
    }
    if (longest < 1.0e-6) return false;
    const gp_Dir repeat(gp_Vec(lo, hi));

    // Which of the slat's own three axes the centres march along.
    int repeatAxis = -1;
    double best = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const double alignment = std::fabs(boxAxis(first, axis).Dot(repeat));
        if (alignment > best) {
            best = alignment;
            repeatAxis = axis;
        }
    }
    if (repeatAxis < 0 || best < 0.999) return false;   // centres not along a slat's own axis

    // Every centre on that one line, at one pitch. Sorted by their projection,
    // so a folder whose rows were reordered still reads as a run.
    std::vector<double> at;
    at.reserve(boxes.size());
    for (const MeasuredBox& box : boxes) {
        const gp_Vec offset(lo, box.centre);
        const double across = offset.Dot(gp_Vec(repeat));
        if ((offset - gp_Vec(repeat) * across).Magnitude() > tol) return false;
        at.push_back(across);
    }
    std::sort(at.begin(), at.end());
    const double pitch = (at.back() - at.front()) / static_cast<double>(at.size() - 1);
    if (pitch < 1.0e-6) return false;
    for (std::size_t i = 1; i < at.size(); ++i) {
        if (std::fabs((at[i] - at[i - 1]) - pitch) > tol) return false;
    }

    // Of the two axes left, the longer extent is the RUN and the shorter is
    // the DEPTH - that is what makes a batten a batten.
    int runAxis = -1;
    int depthAxis = -1;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis == repeatAxis) continue;
        if (runAxis < 0) runAxis = axis;
        else depthAxis = axis;
    }
    if (runAxis < 0 || depthAxis < 0) return false;
    if (boxExtent(first, depthAxis) > boxExtent(first, runAxis)) std::swap(runAxis, depthAxis);

    plan.width = boxExtent(first, repeatAxis);
    plan.gap = std::max(0.0, pitch - plan.width);
    plan.depth = boxExtent(first, depthAxis);
    // The rectangle is written with u as the run axis, which IS the area's own
    // convention - so a rebuild of what is there needs no swap.
    plan.runAcross = false;

    // The plane they stand ON is their own back face. The depth axis points
    // along the depth but its sign is the measured box's, so `outward` is
    // resolved by stepping half a depth off the centre line and calling that
    // the back: both signs give the same plane, and the normal is then made
    // to point the way the slats stand.
    const gp_Dir depthDir = boxAxis(first, depthAxis);
    const gp_Pnt centre((lo.XYZ() + hi.XYZ()) * 0.5);
    const gp_Pnt back(centre.XYZ() - depthDir.XYZ() * (plan.depth * 0.5));
    const double runExtent = boxExtent(first, runAxis);
    const double repeatExtent = (at.back() - at.front()) + plan.width;
    // gp_Ax3(origin, normal, xDirection): the x direction is the run axis, so
    // the plane's own (u, v) read (run, repeat) exactly as the area promises.
    area.plane = gp_Pln(gp_Ax3(back, depthDir, boxAxis(first, runAxis)));
    area.uLo = -0.5 * runExtent;
    area.uHi = 0.5 * runExtent;
    area.vLo = -0.5 * repeatExtent;
    area.vHi = 0.5 * repeatExtent;
    return true;
}

static int countOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    if (shape.IsNull()) return 0;
    int n = 0;
    for (TopExp_Explorer it(shape, type); it.More(); it.Next()) ++n;
    return n;
}

int countFaces(const TopoDS_Shape& shape)  { return countOf(shape, TopAbs_FACE); }
int countSolids(const TopoDS_Shape& shape) { return countOf(shape, TopAbs_SOLID); }

}  // namespace ModelingOps
