#include "ModelingOps.h"

#include <memory>
#include <sstream>

#include <cmath>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
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
// TopAbs_Orientation. On a plain box three of six faces are REVERSED and
// their plane normals point into the body; the same one-line fix
// MainWindow::lockToFace already carries for the sketch plane belongs here
// too, so pullFace's caller never has to know about it.
gp_Pln outwardPlane(const TopoDS_Face& face, const BRepAdaptor_Surface& surface)
{
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        plane = gp_Pln(gp_Ax3(plane.Location(), plane.Axis().Direction().Reversed(),
                              plane.Position().XDirection()));
    }
    return plane;
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

    try {
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) {
            out.error = "pull: face is not planar";
            return out;
        }

        const gp_Pln plane = outwardPlane(face, surface);
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

BooleanResult filletEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge, double radius)
{
    BooleanResult out;
    if (body.IsNull() || edge.IsNull()) {
        out.error = "fillet: body or edge is null";
        return out;
    }
    if (radius <= 0.0) {
        out.error = "fillet: radius must be positive";
        return out;
    }

    try {
        BRepFilletAPI_MakeFillet mkFillet(body);
        mkFillet.Add(radius, edge);
        mkFillet.Build();
        // OCCT fillets legitimately fail on hard geometry (e.g. a radius
        // that would eat a neighbouring face) - IsDone() false is a normal
        // outcome here, not a bug, and must carry through as ok == false.
        if (!mkFillet.IsDone()) {
            out.error = "fillet: the kernel could not build this radius on this edge";
            return out;
        }
        const TopoDS_Shape result = mkFillet.Shape();
        if (!isShapeSane(result)) {
            out.error = "fillet: result is empty or invalid";
            return out;
        }

        out.ok = true;
        out.shape = result;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.error = std::string("fillet: kernel exception - ") +
                     (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
}

BooleanResult chamferEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge, double distance)
{
    BooleanResult out;
    if (body.IsNull() || edge.IsNull()) {
        out.error = "chamfer: body or edge is null";
        return out;
    }
    if (distance <= 0.0) {
        out.error = "chamfer: distance must be positive";
        return out;
    }

    try {
        BRepFilletAPI_MakeChamfer mkChamfer(body);
        mkChamfer.Add(distance, edge);  // symmetric chamfer, both adjacent faces
        mkChamfer.Build();
        if (!mkChamfer.IsDone()) {
            out.error = "chamfer: the kernel could not build this distance on this edge";
            return out;
        }
        const TopoDS_Shape result = mkChamfer.Shape();
        if (!isShapeSane(result)) {
            out.error = "chamfer: result is empty or invalid";
            return out;
        }

        out.ok = true;
        out.shape = result;
    } catch (const Standard_Failure& e) {
        out.ok = false;
        out.error = std::string("chamfer: kernel exception - ") +
                     (e.GetMessageString() ? e.GetMessageString() : "unknown");
    }
    return out;
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
