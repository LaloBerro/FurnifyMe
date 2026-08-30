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

// Round one edge of `body` with radius r / flatten it with distance d.
// Refuses a null body/edge, r/d <= 0, and any BRepFilletAPI failure
// (IsDone false, null or empty/invalid result) - OCCT fillets legitimately
// fail on hard geometry (e.g. a radius that would eat a neighbouring face)
// and BRepFilletAPI can throw Standard_Failure rather than politely fail;
// both are caught at this boundary and converted to ok == false, body
// untouched.
BooleanResult filletEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                         double radius);
BooleanResult chamferEdge(const TopoDS_Shape& body, const TopoDS_Edge& edge,
                          double distance);

// Bake a rigid transform (+ uniform scale) into the shape's geometry via
// BRepBuilderAPI_Transform with copy = true. gp_Trsf carries
// rotation/translation/uniform scale; refuses a null body or a transform
// whose scale factor is <= 0.
BooleanResult transformShape(const TopoDS_Shape& body, const gp_Trsf& trsf);

// Must run before display or STL export, or curved faces render faceted / not at all.
void tessellate(const TopoDS_Shape& shape, double linearDeflection = 0.1);

StepResult exportStep(const TopoDS_Shape& shape, const std::string& path);

double volume(const TopoDS_Shape& shape);
int countFaces(const TopoDS_Shape& shape);
int countSolids(const TopoDS_Shape& shape);

}  // namespace ModelingOps
