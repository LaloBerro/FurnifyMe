#include "ModelingOps.h"

#include <memory>
#include <sstream>

#include <cmath>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_ListOfShape.hxx>
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
