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
#include <BRepPrimAPI_MakePrism.hxx>
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
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
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
// the candidate normal from a point actually ON the face (its own centre of
// mass - the face is known planar here, so that point is unambiguous). This
// is orientation-flag-independent, so it is correct whether or not `body`
// was built by a mirror, and it costs one classifier build on the COMMIT
// path only (pullFaceBy calls this once per gesture, never per drag frame).
gp_Pln outwardPlane(const TopoDS_Shape& body, const TopoDS_Face& face,
                    const BRepAdaptor_Surface& surface)
{
    const gp_Pln plane = surface.Plane();
    gp_Dir candidate = plane.Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) candidate.Reverse();

    GProp_GProps faceProps;
    BRepGProp::SurfaceProperties(face, faceProps);
    const gp_Pnt probe = faceProps.CentreOfMass().Translated(gp_Vec(candidate) * 0.01);
    BRepClass3d_SolidClassifier classifier(body);
    classifier.Perform(probe, 1.0e-6);
    if (classifier.State() == TopAbs_IN) candidate.Reverse();   // ground truth overrides the guess

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
