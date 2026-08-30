//
// Milestone 2's first deliverable: the four kernel operations behind
// direct modeling (pull, fillet, chamfer, transform), proven headless
// before any gizmo exists. Every numeric assertion here runs against the
// same known box: 100 x 80 x 10, min corner at the origin.
//
// Run:  ctest --preset windows-headless --output-on-failure
//   or: ./build-headless/RelWithDebInfo/direct_modeling
//
#include "ModelingOps.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

const double kPi = 3.14159265358979323846;

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n",
                ok ? "[ ok ]" : "[FAIL]", what.c_str(), actual, expected);
    if (!ok) ++g_failures;
}

gp_Pnt centreOfMass(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.CentreOfMass();
}

// The one planar face of `box` whose whole extent sits at Z == z - used to
// pick the top face of the known box without depending on face order.
TopoDS_Face faceAtZ(const TopoDS_Shape& box, double z)
{
    for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        Bnd_Box bbox;
        BRepBndLib::Add(face, bbox);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        if (std::fabs(zmin - z) < 1.0e-6 && std::fabs(zmax - z) < 1.0e-6) return face;
    }
    return TopoDS_Face();
}

// A straight edge of `box` running parallel to X with the given length, at
// whichever Y/Z corner is found first - any of the 4 matches is an equally
// valid "one long edge" for the fillet/chamfer assertions below.
TopoDS_Edge longEdgeAlongX(const TopoDS_Shape& box, double length)
{
    for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        const gp_Pnt p1 = BRep_Tool::Pnt(v1);
        const gp_Pnt p2 = BRep_Tool::Pnt(v2);
        if (std::fabs(p1.Y() - p2.Y()) < 1.0e-6 && std::fabs(p1.Z() - p2.Z()) < 1.0e-6 &&
            std::fabs(std::fabs(p1.X() - p2.X()) - length) < 1.0e-6) {
            return edge;
        }
    }
    return TopoDS_Edge();
}

}  // namespace

int main()
{
    using namespace ModelingOps;

    const TopoDS_Shape box = makeBox(gp_Pnt(0.0, 0.0, 0.0), 100.0, 80.0, 10.0);
    check(!box.IsNull(), "100x80x10 box built");
    const double boxVolume = volume(box);
    checkNear(boxVolume, 80000.0, 1.0e-6, "box volume is 100*80*10");

    const TopoDS_Face topFace = faceAtZ(box, 10.0);
    check(!topFace.IsNull(), "top face (Z=10) found");

    // --- pullFace ---------------------------------------------------------
    {
        const BooleanResult grown = pullFace(box, topFace, 5.0);
        check(grown.ok, "pull +5 on the top face succeeds" +
                        (grown.ok ? std::string() : ": " + grown.error));
        if (grown.ok) {
            checkNear(volume(grown.shape) - boxVolume, 100.0 * 80.0 * 5.0, 1.0e-3,
                      "pull +5 grows volume by exactly 100*80*5");
            check(countSolids(grown.shape) == 1, "pull +5 result is one solid");
            check(countFaces(grown.shape) == 6, "pull +5 result still has 6 faces after unify");
            check(centreOfMass(grown.shape).Z() > centreOfMass(box).Z(),
                  "pull +5 raises the centre of mass");
        }
    }
    {
        const BooleanResult shrunk = pullFace(box, topFace, -5.0);
        check(shrunk.ok, "pull -5 on the top face succeeds" +
                          (shrunk.ok ? std::string() : ": " + shrunk.error));
        if (shrunk.ok) {
            checkNear(boxVolume - volume(shrunk.shape), 100.0 * 80.0 * 5.0, 1.0e-3,
                      "pull -5 shrinks volume by exactly 100*80*5");
            check(centreOfMass(shrunk.shape).Z() < centreOfMass(box).Z(),
                  "pull -5 lowers the centre of mass");
        }
    }
    check(!pullFace(box, topFace, -10.0).ok,
          "pull -10 (the full thickness) is refused, not surfaced as an empty success");
    check(!pullFace(box, TopoDS_Face(), 5.0).ok, "pull with a null face is refused");
    check(!pullFace(box, topFace, 0.0).ok, "pull with zero distance is refused");
    check(!pullFace(box, topFace, 1.0e-9).ok,
          "pull with a distance under the 1e-7 threshold is refused");

    // --- filletEdge / chamferEdge ------------------------------------------
    const TopoDS_Edge longEdge = longEdgeAlongX(box, 100.0);
    check(!longEdge.IsNull(), "a 100mm edge along X found");

    {
        const BooleanResult filleted = filletEdge(box, longEdge, 3.0);
        check(filleted.ok, "fillet r=3 on the long edge succeeds" +
                            (filleted.ok ? std::string() : ": " + filleted.error));
        if (filleted.ok) {
            // Removed cross-section of a constant-radius fillet on a 90deg
            // edge is r^2 - (pi*r^2/4) = r^2*(1 - pi/4), times the edge length.
            const double expectedDelta = (1.0 - kPi / 4.0) * 9.0 * 100.0;
            checkNear(boxVolume - volume(filleted.shape), expectedDelta,
                      expectedDelta * 1.0e-3, "fillet r=3 removes (1-pi/4)*9*100");
            const BRepCheck_Analyzer analyzer(filleted.shape);
            check(analyzer.IsValid(), "fillet result passes BRepCheck_Analyzer");
        }
    }
    {
        const BooleanResult chamfered = chamferEdge(box, longEdge, 3.0);
        check(chamfered.ok, "chamfer d=3 on the long edge succeeds" +
                             (chamfered.ok ? std::string() : ": " + chamfered.error));
        if (chamfered.ok) {
            // Removed cross-section of a symmetric chamfer on a 90deg edge
            // is a right triangle of leg d: 0.5*d^2, times the edge length.
            const double expectedDelta = 0.5 * 9.0 * 100.0;
            checkNear(boxVolume - volume(chamfered.shape), expectedDelta,
                      expectedDelta * 1.0e-3, "chamfer d=3 removes 0.5*9*100");
            const BRepCheck_Analyzer analyzer(chamfered.shape);
            check(analyzer.IsValid(), "chamfer result passes BRepCheck_Analyzer");
        }
    }
    {
        const BooleanResult tooBig = filletEdge(box, longEdge, 20.0);
        check(!tooBig.ok,
              "fillet r=20 on the 10mm-thick box is refused, not surfaced as a success");
        checkNear(volume(box), boxVolume, 1.0e-9, "body volume untouched after the refusal");
        check(countFaces(box) == 6, "body face count untouched after the refusal");
    }
    check(!filletEdge(box, longEdge, 0.0).ok, "fillet r=0 is refused");
    check(!filletEdge(box, longEdge, -1.0).ok, "fillet with negative radius is refused");
    check(!filletEdge(box, TopoDS_Edge(), 3.0).ok, "fillet with a null edge is refused");
    check(!chamferEdge(box, longEdge, 0.0).ok, "chamfer d=0 is refused");
    check(!chamferEdge(box, longEdge, -1.0).ok, "chamfer with negative distance is refused");
    check(!chamferEdge(box, TopoDS_Edge(), 3.0).ok, "chamfer with a null edge is refused");

    // --- transformShape ------------------------------------------------------
    {
        gp_Trsf t;
        t.SetTranslation(gp_Vec(10.0, 20.0, 30.0));
        const BooleanResult moved = transformShape(box, t);
        check(moved.ok, "translate (10,20,30) succeeds" +
                        (moved.ok ? std::string() : ": " + moved.error));
        if (moved.ok) {
            const gp_Pnt before = centreOfMass(box);
            const gp_Pnt after = centreOfMass(moved.shape);
            checkNear(after.X() - before.X(), 10.0, 1.0e-6, "translate moves centre of mass X by 10");
            checkNear(after.Y() - before.Y(), 20.0, 1.0e-6, "translate moves centre of mass Y by 20");
            checkNear(after.Z() - before.Z(), 30.0, 1.0e-6, "translate moves centre of mass Z by 30");
            checkNear(volume(moved.shape), boxVolume, 1.0e-6, "translate leaves volume unchanged");
        }
    }
    {
        gp_Trsf t;
        t.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), kPi / 2.0);
        const BooleanResult rotated = transformShape(box, t);
        check(rotated.ok, "rotate 90deg about Z succeeds" +
                          (rotated.ok ? std::string() : ": " + rotated.error));
        if (rotated.ok) {
            Bnd_Box bbox;
            BRepBndLib::Add(rotated.shape, bbox);
            double xmin, ymin, zmin, xmax, ymax, zmax;
            bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            checkNear(xmax - xmin, 80.0, 1.0e-6, "rotate 90 maps the X extent to the old Y extent (80)");
            checkNear(ymax - ymin, 100.0, 1.0e-6, "rotate 90 maps the Y extent to the old X extent (100)");
        }
    }
    {
        gp_Trsf t;
        t.SetScale(gp_Pnt(0.0, 0.0, 0.0), 2.0);
        const BooleanResult scaled = transformShape(box, t);
        check(scaled.ok, "scale 2.0 succeeds" +
                         (scaled.ok ? std::string() : ": " + scaled.error));
        if (scaled.ok) {
            checkNear(volume(scaled.shape), boxVolume * 8.0, boxVolume * 8.0 * 1.0e-6,
                      "scale 2.0 gives volume x8");
        }
    }
    {
        gp_Trsf t;
        t.SetScale(gp_Pnt(0.0, 0.0, 0.0), -1.0);
        check(!transformShape(box, t).ok, "a negative scale factor is refused");
    }
    {
        gp_Trsf t;
        t.SetScale(gp_Pnt(0.0, 0.0, 0.0), 0.0);
        check(!transformShape(box, t).ok, "a zero scale factor is refused");
    }
    check(!transformShape(TopoDS_Shape(), gp_Trsf()).ok, "transform of a null body is refused");

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
