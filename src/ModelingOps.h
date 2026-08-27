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

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace ModelingOps {

enum class BooleanKind { Fuse, Cut, Common };

// Booleans on near-tangent geometry are OCCT's known weak spot. Callers must
// look at `ok` - never present a failed boolean as a success.
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

// Must run before display or STL export, or curved faces render faceted / not at all.
void tessellate(const TopoDS_Shape& shape, double linearDeflection = 0.1);

StepResult exportStep(const TopoDS_Shape& shape, const std::string& path);

double volume(const TopoDS_Shape& shape);
int countFaces(const TopoDS_Shape& shape);
int countSolids(const TopoDS_Shape& shape);

}  // namespace ModelingOps
