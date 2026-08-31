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
#include <vector>

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
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

// The straight edge whose two endpoints are `a` and `b` in either order -
// how the spreading-bevel block names a specific edge without depending on
// OCCT's iteration order, which changes with every rebuild.
TopoDS_Edge edgeBetween(const TopoDS_Shape& shape, const gp_Pnt& a, const gp_Pnt& b)
{
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        if (v1.IsNull() || v2.IsNull()) continue;
        const gp_Pnt p1 = BRep_Tool::Pnt(v1);
        const gp_Pnt p2 = BRep_Tool::Pnt(v2);
        if ((p1.Distance(a) < 1.0e-6 && p2.Distance(b) < 1.0e-6) ||
            (p1.Distance(b) < 1.0e-6 && p2.Distance(a) < 1.0e-6))
            return edge;
    }
    return TopoDS_Edge();
}

double edgeLength(const TopoDS_Edge& edge)
{
    if (edge.IsNull()) return 0.0;
    BRepAdaptor_Curve curve(edge);
    return GCPnts_AbscissaPoint::Length(curve);
}

// How many rounded strips a body carries. A fillet makes exactly one
// cylindrical face per edge it rounds, so this counts the operations the
// kernel actually performed - which is the assertion a volume check cannot
// make when a spread happens to remove a plausible amount.
int countCylindricalFaces(const TopoDS_Shape& shape)
{
    int found = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() == GeomAbs_Cylinder)
            ++found;
    }
    return found;
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

    // A shape unrelated to `box`, entirely off in space - used to build
    // "foreign" faces/edges for the membership-guard and foreign-topology
    // refusal tests below.
    const TopoDS_Shape distantBox = makeBox(gp_Pnt(1000.0, 1000.0, 1000.0), 10.0, 10.0, 10.0);
    check(!distantBox.IsNull(), "distant box (for foreign-topology tests) built");

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

    // The Phase 4 lesson, now covered here too: BRepAdaptor_Surface never
    // applies TopAbs_Orientation, and on a plain box three of the six faces
    // are REVERSED with their raw plane normal pointing INTO the body. Every
    // pullFace assertion above used the top face, which happens to be
    // FORWARD - stubbing the REVERSED flip out would leave that whole block
    // green, which is exactly the hole that hid the Phase 4 picker bug.
    // Pull the bottom face (confirmed REVERSED below) instead, so a broken
    // flip fails loudly here.
    {
        const TopoDS_Face bottomFace = faceAtZ(box, 0.0);
        check(!bottomFace.IsNull(), "bottom face (Z=0) found");
        check(bottomFace.Orientation() == TopAbs_REVERSED,
              "the bottom face is REVERSED, so this test actually exercises the flip "
              "rather than passing vacuously");

        const BooleanResult grownDown = pullFace(box, bottomFace, 5.0);
        check(grownDown.ok, "pull +5 on the REVERSED bottom face succeeds" +
                            (grownDown.ok ? std::string() : ": " + grownDown.error));
        if (grownDown.ok) {
            checkNear(volume(grownDown.shape) - boxVolume, 100.0 * 80.0 * 5.0, 1.0e-3,
                      "pull +5 on the REVERSED bottom face grows volume by exactly 100*80*5");
            check(centreOfMass(grownDown.shape).Z() < centreOfMass(box).Z(),
                  "pull +5 on the bottom face moves the centre of mass DOWN - outward for "
                  "that face is -Z, so this only passes if the REVERSED flip actually fired");
        }
    }

    // pullFace has no kernel-level guard against a face that simply does not
    // belong to `body` - a prism built from it and fused/cut against `body`
    // is a perfectly well-formed boolean between two unrelated shapes, so
    // OCCT alone would report success. One mis-wired pick from the UI (the
    // gizmo tasks feed a picked TopoDS_Face straight in) would otherwise
    // silently produce two disconnected solids.
    {
        TopExp_Explorer faceIt(distantBox, TopAbs_FACE);
        check(faceIt.More(), "distant box has at least one face");
        const TopoDS_Face foreignFace = TopoDS::Face(faceIt.Current());

        const BooleanResult foreign = pullFace(box, foreignFace, 5.0);
        check(!foreign.ok,
              "pull with a face that does not belong to the body is refused, not silently "
              "fused/cut into two disconnected solids");
        check(foreign.shape.IsNull(),
              "and the refusal carries a null shape, per BooleanResult's contract");
    }

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
        check(tooBig.shape.IsNull(),
              "and the refusal carries a null shape, per BooleanResult's contract");
        checkNear(volume(box), boxVolume, 1.0e-9, "body volume untouched after the refusal");
        check(countFaces(box) == 6, "body face count untouched after the refusal");
    }
    check(!filletEdge(box, longEdge, 0.0).ok, "fillet r=0 is refused");
    check(!filletEdge(box, longEdge, -1.0).ok, "fillet with negative radius is refused");
    check(!filletEdge(box, TopoDS_Edge(), 3.0).ok, "fillet with a null edge is refused");
    check(!chamferEdge(box, longEdge, 0.0).ok, "chamfer d=0 is refused");
    check(!chamferEdge(box, longEdge, -1.0).ok, "chamfer with negative distance is refused");
    check(!chamferEdge(box, TopoDS_Edge(), 3.0).ok, "chamfer with a null edge is refused");

    // filletEdge/chamferEdge get the "does this belong to the body" guard
    // for free: BRepFilletAPI throws Standard_Failure ("no suitable edges")
    // on an edge foreign to the shape, rather than politely failing
    // IsDone(). These two lines pin that the catch converts it to a refusal
    // rather than letting the exception escape.
    {
        TopExp_Explorer edgeIt(distantBox, TopAbs_EDGE);
        check(edgeIt.More(), "distant box has at least one edge");
        const TopoDS_Edge foreignEdge = TopoDS::Edge(edgeIt.Current());

        const BooleanResult foreignFillet = filletEdge(box, foreignEdge, 2.0);
        check(!foreignFillet.ok,
              "fillet on an edge foreign to the body is refused, not an uncaught "
              "Standard_Failure");
        check(foreignFillet.shape.IsNull(), "the caught throw still yields a null shape");
        const BooleanResult foreignChamfer = chamferEdge(box, foreignEdge, 2.0);
        check(!foreignChamfer.ok,
              "chamfer on an edge foreign to the body is refused for the same reason");
        check(foreignChamfer.shape.IsNull(), "with a null shape likewise");
    }

    // --- the spreading bevel, and the multi-edge list forms -------------------
    //
    // The user-reported bug: rounding an edge that ENDS on an earlier
    // fillet's strip also rounded a neighbouring edge nobody picked.
    // BRepFilletAPI's Add() is documented to build a contour "composed of
    // edges of the shape which are tangential to one another and which
    // delimit two series of tangential faces" - a fillet strip is exactly
    // such a series, so the contour walks straight out through the picked
    // edge's endpoint and takes the far neighbour with it. Measured on this
    // box: one Add produced a contour of THREE edges, and the volume removed
    // came out 13.8% over the single-edge formula.
    //
    // Every number below is arithmetic, not eyeballing: the second fillet
    // must remove exactly (1-pi/4)*r^2*len and nothing else, and the first
    // fillet's strip must still be the full 100 mm it was built at. Stub the
    // containment out and both fail.
    std::printf("\n-- the spreading bevel (item 8) --\n");
    {
        // Edge A: the top-front long edge, (0,0,10)-(100,0,10).
        const TopoDS_Edge edgeA = edgeBetween(box, gp_Pnt(0, 0, 10), gp_Pnt(100, 0, 10));
        check(!edgeA.IsNull(), "the top-front edge found on the plain box");

        const BooleanResult first = filletEdge(box, edgeA, 3.0);
        check(first.ok, "fillet r=3 on it succeeds" +
                        (first.ok ? std::string() : ": " + first.error));
        if (first.ok) {
            const TopoDS_Shape rounded = first.shape;
            const double roundedVolume = volume(rounded);
            check(countFaces(rounded) == 7, "the rounded body carries the one new strip");

            // Edge B: the top-left edge, shortened by A's fillet to 77 mm and
            // now running (0,3,10)-(0,80,10). It ENDS on A's strip, which is
            // the whole point.
            const TopoDS_Edge edgeB =
                edgeBetween(rounded, gp_Pnt(0, 3, 10), gp_Pnt(0, 80, 10));
            check(!edgeB.IsNull(),
                  "the neighbouring top-left edge, now ending on that strip, found");
            checkNear(edgeLength(edgeB), 77.0, 1.0e-6,
                      "and it really is the shortened 77 mm edge, not the original 80");

            const BooleanResult second = filletEdge(rounded, edgeB, 3.0);
            check(second.ok, "fillet r=3 on that neighbour succeeds" +
                             (second.ok ? std::string() : ": " + second.error));
            if (second.ok) {
                const double expected = (1.0 - kPi / 4.0) * 9.0 * 77.0;
                checkNear(roundedVolume - volume(second.shape), expected, expected * 1.0e-4,
                          "and removes EXACTLY its own (1-pi/4)*9*77 - no neighbouring "
                          "edge came with it");
                check(countSolids(second.shape) == 1, "the twice-rounded body is one body");
                check(BRepCheck_Analyzer(second.shape).IsValid(),
                      "and passes BRepCheck_Analyzer");

                // The other half of the assertion, and the one a volume check
                // alone cannot make: the FIRST fillet's strip is untouched.
                // Its lower boundary was built at the body's full 100 mm; the
                // spread shortened it to 97.
                const TopoDS_Edge aStripEdge =
                    edgeBetween(second.shape, gp_Pnt(0, 0, 7), gp_Pnt(100, 0, 7));
                check(!aStripEdge.IsNull(),
                      "the first strip's lower boundary is still there at its own corners");
                if (!aStripEdge.IsNull())
                    checkNear(edgeLength(aStripEdge), 100.0, 1.0e-6,
                              "and still spans the body's full 100 mm - the spread cut it "
                              "back to 97");
                check(countCylindricalFaces(second.shape) == 2,
                      "exactly two rounded strips exist on the body: A's and B's, and not "
                      "a third on an edge nobody picked");
            }
        }
    }

    std::printf("\n-- multi-edge bevels (item 9) --\n");
    {
        // Two edges as far apart as this box allows: the top-front and the
        // bottom-back long edges. They share no vertex, so one build removes
        // exactly twice one edge's worth.
        const TopoDS_Edge top = edgeBetween(box, gp_Pnt(0, 0, 10), gp_Pnt(100, 0, 10));
        const TopoDS_Edge bottom = edgeBetween(box, gp_Pnt(0, 80, 0), gp_Pnt(100, 80, 0));
        check(!top.IsNull() && !bottom.IsNull(), "two far-apart 100 mm edges found");

        const std::vector<TopoDS_Edge> pair{top, bottom};
        const BooleanResult both = filletEdges(box, pair, 3.0);
        check(both.ok, "one fillet build over both succeeds" +
                       (both.ok ? std::string() : ": " + both.error));
        if (both.ok) {
            const double single = (1.0 - kPi / 4.0) * 9.0 * 100.0;
            checkNear(boxVolume - volume(both.shape), single * 2.0, single * 2.0 * 1.0e-4,
                      "and removes exactly TWICE the single-edge formula");
            check(countCylindricalFaces(both.shape) == 2, "leaving two rounded strips");
            check(countSolids(both.shape) == 1, "on one body");
            check(BRepCheck_Analyzer(both.shape).IsValid(), "which is valid");
        }

        const BooleanResult bothFlat = chamferEdges(box, pair, 3.0);
        check(bothFlat.ok, "and one chamfer build over both succeeds" +
                           (bothFlat.ok ? std::string() : ": " + bothFlat.error));
        if (bothFlat.ok)
            checkNear(boxVolume - volume(bothFlat.shape), 0.5 * 9.0 * 100.0 * 2.0,
                      0.5 * 9.0 * 100.0 * 2.0 * 1.0e-4,
                      "removing exactly twice the single-edge chamfer wedge");

        // All-or-nothing: one bad edge refuses the whole gesture. A partial
        // bevel - two of the three edges rounded - would be the worst
        // possible answer, because the user would have to work out which.
        TopExp_Explorer foreignIt(distantBox, TopAbs_EDGE);
        const TopoDS_Edge foreign = TopoDS::Edge(foreignIt.Current());
        const BooleanResult mixed = filletEdges(box, {top, foreign}, 3.0);
        check(!mixed.ok, "one good edge and one foreign edge refuses the WHOLE call");
        check(mixed.shape.IsNull(), "with a null shape, per BooleanResult's contract");
        const BooleanResult withNull = filletEdges(box, {top, TopoDS_Edge()}, 3.0);
        check(!withNull.ok, "so does one good edge and one null edge");
        check(withNull.shape.IsNull(), "likewise null");
        const BooleanResult tooBigTogether = filletEdges(box, {top, bottom}, 20.0);
        check(!tooBigTogether.ok,
              "and a radius that fails on either of them refuses both, rather than "
              "rounding the one it could");
        check(tooBigTogether.shape.IsNull(), "null shape again");
        checkNear(volume(box), boxVolume, 1.0e-9,
                  "after all three refusals the body is untouched");
        check(countFaces(box) == 6, "with its original face count");

        check(!filletEdges(box, {}, 3.0).ok, "an empty edge list is refused");
        check(!chamferEdges(box, {}, 3.0).ok, "for the chamfer too");
        check(!filletEdges(TopoDS_Shape(), {top}, 3.0).ok, "as is a null body");
        check(!filletEdges(box, {top}, 0.0).ok, "as is a radius of zero");

        // The one-edge spellings must be the list forms, not a second
        // implementation that can drift from them.
        const BooleanResult viaOne = filletEdge(box, top, 3.0);
        const BooleanResult viaList = filletEdges(box, {top}, 3.0);
        check(viaOne.ok && viaList.ok, "the one-edge and one-element-list spellings agree");
        if (viaOne.ok && viaList.ok)
            checkNear(volume(viaOne.shape), volume(viaList.shape), 1.0e-9,
                      "to the same volume, because they are the same call");
    }

    // --- bevelAxis: every edge of a box, against an independent oracle -------
    //
    // The drag axis for the round-or-flatten gesture, and the one piece of
    // that gesture no volume check can verify: an axis built from un-flipped
    // REVERSED normals points INTO the body, so "against the bisector rounds"
    // sends the drag the opposite physical way - and every assertion that
    // measures the drag against that same axis still passes, consistently
    // wrong. Three of a plain box's six faces are REVERSED, which is exactly
    // enough for the bug to be invisible on some edges and fatal on others.
    //
    // gui_smoke drives one edge end to end, but which edge depends on where
    // the camera left the body, so it can only ever cover whichever one was
    // reachable. This walks ALL TWELVE, deterministically, with no window:
    // BRepClass3d_SolidClassifier says a step ALONG the axis leaves the body
    // and a step against it stays inside, and the dot product against the
    // edge's own direction says the axis is perpendicular. That pairing is
    // Phase 4's six-face lockToFace probe, one operation over.
    {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(box, TopAbs_EDGE, edges);
        check(edges.Extent() == 12, "the known box has twelve edges to walk");

        BRepClass3d_SolidClassifier classifier(box);
        int derived = 0;
        int outwardCorrect = 0;
        int perpendicular = 0;
        // Well inside the 10 mm thickness, so a step either way lands in open
        // material rather than on another face.
        const double kStep = 1.0;
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(i));
            gp_Pnt centre;
            gp_Dir outward;
            if (!bevelAxis(box, edge, centre, outward)) continue;
            ++derived;

            classifier.Perform(centre.Translated(gp_Vec(outward) * kStep), 1.0e-7);
            const bool leaves = classifier.State() == TopAbs_OUT;
            classifier.Perform(centre.Translated(gp_Vec(outward) * -kStep), 1.0e-7);
            const bool enters = classifier.State() == TopAbs_IN;
            if (leaves && enters) ++outwardCorrect;

            TopoDS_Vertex v1, v2;
            TopExp::Vertices(edge, v1, v2);
            const gp_Vec along(BRep_Tool::Pnt(v1), BRep_Tool::Pnt(v2));
            if (along.Magnitude() > 1.0e-7 &&
                std::fabs(gp_Vec(outward).Dot(gp_Vec(gp_Dir(along)))) < 1.0e-9)
                ++perpendicular;
        }
        check(derived == 12, "an axis is derived for every one of them");
        check(outwardCorrect == 12,
              "and every axis points OUT of the body and into it the other way, so "
              "\"drag inward rounds\" means inward on all twelve");
        check(perpendicular == 12,
              "with no component along the edge on any of them, so the arrow cannot "
              "slide off the edge it belongs to");
    }
    {
        // The refusals, so the twelve above are not the only shape this
        // function is ever asked about.
        gp_Pnt centre;
        gp_Dir outward;
        check(!bevelAxis(TopoDS_Shape(), TopoDS_Edge(), centre, outward),
              "a null body and edge derive no axis");
        TopExp_Explorer distantEdges(distantBox, TopAbs_EDGE);
        check(distantEdges.More(), "the distant box has an edge to offer");
        check(!bevelAxis(box, TopoDS::Edge(distantEdges.Current()), centre, outward),
              "nor does an edge that belongs to some other body");

        // A curved edge: one drag axis needs one perpendicular, and an edge
        // whose direction changes along its length has none. The rounded body
        // from the fillet block above is where one exists.
        const BooleanResult rounded = filletEdge(box, longEdge, 3.0);
        check(rounded.ok, "a rounded body for the curved-edge refusal");
        if (rounded.ok) {
            int curved = 0;
            int curvedDerived = 0;
            for (TopExp_Explorer it(rounded.shape, TopAbs_EDGE); it.More(); it.Next()) {
                const TopoDS_Edge candidate = TopoDS::Edge(it.Current());
                if (BRepAdaptor_Curve(candidate).GetType() == GeomAbs_Line) continue;
                ++curved;
                if (bevelAxis(rounded.shape, candidate, centre, outward)) ++curvedDerived;
            }
            check(curved > 0, "which really does carry curved edges to refuse");
            check(curvedDerived == 0, "and not one of them derives an axis");
        }
    }

    // --- pullFace on non-trivial topology ------------------------------------
    //
    // A C-shaped body whose pulled face grows past a notch's own depth and
    // genuinely overlaps material already belonging to the body's other
    // limb - an overlapping fuse, not a merely touching one. Built so the
    // numbers are exact: outer bounding block 100 x 40 x 100 (400000 mm3);
    // notch 60 x 40 x 20 removed from the right side (48000), leaving a
    // spine (X:0-40) and two limbs (X:40-100, split by the notch at
    // Z:40-60). Pulling the bottom limb's top face (Z=40) up by 25 fills
    // the entire 20mm notch and juts 5mm into the top limb, which is
    // already solid there - so the union exactly reconstructs the outer
    // block.
    {
        const TopoDS_Shape outer = makeBox(gp_Pnt(0.0, 0.0, 0.0), 100.0, 40.0, 100.0);
        const TopoDS_Shape notchTool = makeBox(gp_Pnt(40.0, 0.0, 40.0), 60.0, 40.0, 20.0);
        const BooleanResult cResult = applyBoolean(BooleanKind::Cut, outer, notchTool);
        check(cResult.ok, "C-shaped body built (outer block minus a notch)" +
                          (cResult.ok ? std::string() : ": " + cResult.error));
        if (cResult.ok) {
            const TopoDS_Shape cShape = cResult.shape;
            checkNear(volume(cShape), 352000.0, 1.0e-3, "C-shape volume is 400000 - 48000");

            const TopoDS_Face shelf = faceAtZ(cShape, 40.0);
            check(!shelf.IsNull(), "the bottom limb's top face (Z=40) found on the C-shape");

            const BooleanResult filled = pullFace(cShape, shelf, 25.0);
            check(filled.ok,
                  "pulling the shelf +25 - past the notch and into the top limb - succeeds" +
                  (filled.ok ? std::string() : ": " + filled.error));
            if (filled.ok) {
                checkNear(volume(filled.shape), 400000.0, 1.0e-3,
                          "the overlapping fuse exactly reconstructs the outer block's "
                          "400000 mm3");
                check(countSolids(filled.shape) == 1, "C-shape pull result is one solid");
                check(countFaces(filled.shape) == 6,
                      "C-shape pull result unifies back down to a plain 6-face box");
                const BRepCheck_Analyzer analyzer(filled.shape);
                check(analyzer.IsValid(), "C-shape pull result passes BRepCheck_Analyzer");
            }
        }
    }

    // A stepped body (a wide base with a narrower, taller step on one side)
    // carved by a distance that protrudes clean through the step's own
    // height and continues into the base beneath it - a carve whose tool
    // outlives the local feature it started on. Base 100 x 80 x 10
    // (80000 mm3); step 30 x 80 x 20 flush with the base's right edge and
    // running its full depth, so the fused body is a clean 6-edge step
    // prism (128000 mm3, 8 faces). Carving the step's top face by -25
    // removes the whole step (30*80*20 = 48000) plus a 5mm-deep slice of
    // the base under its footprint (30*80*5 = 12000): 128000 - 60000 =
    // 68000, and because the notch's footprint is flush with the body's
    // edge and spans its full depth, the result is still a clean 6-edge
    // step prism - 8 faces, just a shorter step.
    {
        const TopoDS_Shape base = makeBox(gp_Pnt(0.0, 0.0, 0.0), 100.0, 80.0, 10.0);
        const TopoDS_Shape step = makeBox(gp_Pnt(70.0, 0.0, 10.0), 30.0, 80.0, 20.0);
        const BooleanResult steppedResult = applyBoolean(BooleanKind::Fuse, base, step);
        check(steppedResult.ok, "stepped body built (base fused with a flush step)" +
                                (steppedResult.ok ? std::string() : ": " + steppedResult.error));
        if (steppedResult.ok) {
            const TopoDS_Shape steppedBody = steppedResult.shape;
            checkNear(volume(steppedBody), 128000.0, 1.0e-3,
                      "stepped body volume is 80000 + 48000");
            check(countFaces(steppedBody) == 8, "stepped body is a clean 8-face step prism");

            const TopoDS_Face stepTop = faceAtZ(steppedBody, 30.0);
            check(!stepTop.IsNull(), "the step's top face (Z=30) found");

            const BooleanResult carved = pullFace(steppedBody, stepTop, -25.0);
            check(carved.ok,
                  "carving the step -25 - through the step and into the base - succeeds" +
                  (carved.ok ? std::string() : ": " + carved.error));
            if (carved.ok) {
                checkNear(volume(carved.shape), 68000.0, 1.0e-3,
                          "the protruding carve leaves exactly 68000 mm3");
                check(countFaces(carved.shape) == 8,
                      "the carved stepped body is still a clean 8-face step prism");
                check(countSolids(carved.shape) == 1, "the carved stepped body is one solid");
                const BRepCheck_Analyzer analyzer(carved.shape);
                check(analyzer.IsValid(), "carved stepped body passes BRepCheck_Analyzer");
            }
        }
    }

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

    // --- snapping a gizmo drag onto steps the user can predict --------------
    // The transform gizmo's release path runs every drag through this before
    // it is baked, so the arithmetic is proven here rather than inferred from
    // a body that happened to land somewhere plausible.
    std::printf("\n-- snapTransform --\n");
    {
        const gp_Pnt pivot(37.0, -11.0, 5.0);   // deliberately not the origin

        gp_Trsf move;
        move.SetTranslation(gp_Vec(23.0, -4.0, 71.0));
        const gp_Trsf snapped = snapTransform(move, pivot, 10.0, 15.0, 0.05);
        checkNear(snapped.TranslationPart().X(), 20.0, 1.0e-9, "23 mm snaps to 20");
        checkNear(snapped.TranslationPart().Y(), 0.0, 1.0e-9, "-4 mm snaps to 0");
        checkNear(snapped.TranslationPart().Z(), 70.0, 1.0e-9, "71 mm snaps to 70");
        checkNear(snapped.ScaleFactor(), 1.0, 1.0e-9, "and a pure move stays scale 1");
    }
    {
        // A rotation about a pivot away from the origin carries a translation
        // part of its own (pivot - R.pivot). Snapping that part directly - the
        // obvious wrong implementation - would drag the body off the axis it
        // was turned about; snapTransform decomposes about the pivot instead,
        // so the pivot must come back exactly where it started.
        const gp_Pnt pivot(120.0, 80.0, 5.0);
        gp_Trsf turn;
        turn.SetRotation(gp_Ax1(pivot, gp_Dir(0.0, 0.0, 1.0)), 32.0 * kPi / 180.0);
        const gp_Trsf snapped = snapTransform(turn, pivot, 10.0, 15.0, 0.05);
        gp_Vec axis;
        Standard_Real angle = 0.0;
        snapped.GetRotation().GetVectorAndAngle(axis, angle);
        checkNear(angle * 180.0 / kPi, 30.0, 1.0e-6, "32 degrees snaps to 30");
        checkNear(pivot.Transformed(snapped).Distance(pivot), 0.0, 1.0e-9,
                  "and the pivot the rotation turned about does not move");
        checkNear(snapped.ScaleFactor(), 1.0, 1.0e-9, "a pure rotation stays scale 1");
    }
    {
        const gp_Pnt pivot(50.0, 40.0, 5.0);
        gp_Trsf grow;
        grow.SetScale(pivot, 1.263);
        const gp_Trsf snapped = snapTransform(grow, pivot, 10.0, 15.0, 0.05);
        checkNear(snapped.ScaleFactor(), 1.25, 1.0e-9, "x1.263 snaps to x1.25");
        checkNear(pivot.Transformed(snapped).Distance(pivot), 0.0, 1.0e-9,
                  "and a scale leaves its own pivot where it is");
    }
    {
        // A COMPOUND gesture - turned about a pivot and moved - is the case the
        // T . S . R rebuild exists for: snapping the components separately is
        // only possible because they were pulled apart about the pivot first.
        // Nothing in the gizmo produces one today, because exactly one mode is
        // armed at a time - which is precisely why it is pinned here. A
        // refactor that "simplified" the rebuild into snapping the trsf's own
        // translation part would still pass every single-mode case above and
        // silently break this one.
        const gp_Pnt pivot(200.0, -60.0, 12.0);
        gp_Trsf turn;
        turn.SetRotation(gp_Ax1(pivot, gp_Dir(0.0, 0.0, 1.0)), 41.0 * kPi / 180.0);
        gp_Trsf move;
        move.SetTranslation(gp_Vec(48.0, -13.0, 6.0));

        const gp_Trsf snapped = snapTransform(move * turn, pivot, 10.0, 15.0, 0.05);
        gp_Vec axis;
        Standard_Real angle = 0.0;
        snapped.GetRotation().GetVectorAndAngle(axis, angle);
        checkNear(angle * 180.0 / kPi, 45.0, 1.0e-6,
                  "in a compound move-and-turn, 41 degrees still snaps to 45");
        checkNear(axis.Z(), 1.0, 1.0e-9, "about the axis it was actually turned on");
        const gp_Pnt moved = pivot.Transformed(snapped);
        checkNear(moved.X() - pivot.X(), 50.0, 1.0e-9,
                  "and the 48 mm of X in it snaps to 50, not swallowed by the rotation");
        checkNear(moved.Y() - pivot.Y(), -10.0, 1.0e-9, "-13 mm of Y snaps to -10");
        checkNear(moved.Z() - pivot.Z(), 10.0, 1.0e-9, "6 mm of Z snaps to 10");
        checkNear(snapped.ScaleFactor(), 1.0, 1.0e-9, "with no scale invented on the way");
    }
    {
        // A snap must never leave a transform the kernel cannot apply: gp_Trsf
        // and BRepBuilderAPI_Transform want a positive factor, so a shrink that
        // would round to nothing is held at one step. A caller's own sanity
        // band may still refuse that value - MainWindow's does - but that is a
        // refusal with a reason, not a degenerate transform.
        const gp_Pnt origin(0.0, 0.0, 0.0);
        gp_Trsf shrink;
        shrink.SetScale(origin, 0.01);
        const gp_Trsf snapped = snapTransform(shrink, origin, 10.0, 15.0, 0.05);
        check(snapped.ScaleFactor() > 0.0,
              "a shrink that would round to zero is held at one step, not made illegal");
        check(transformShape(box, snapped).ok,
              "so the kernel still accepts what the snap produced");
    }
    {
        // Steps <= 0 leave their component alone, which is how Snap-off works.
        gp_Trsf move;
        move.SetTranslation(gp_Vec(23.0, -4.0, 71.0));
        const gp_Trsf untouched = snapTransform(move, gp_Pnt(0.0, 0.0, 0.0), 0.0, 0.0, 0.0);
        checkNear(untouched.TranslationPart().X(), 23.0, 1.0e-9,
                  "a step of zero leaves the translation exactly as dragged");
    }
    {
        check(isIdentityTransform(gp_Trsf()), "a default transform is the identity");
        gp_Trsf nudge;
        nudge.SetTranslation(gp_Vec(0.0, 0.0, 1.0));
        check(!isIdentityTransform(nudge), "a 1 mm move is not");
        gp_Trsf turn;
        turn.SetRotation(gp_Ax1(gp_Pnt(9.0, 9.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                         1.0 * kPi / 180.0);
        check(!isIdentityTransform(turn),
              "nor is a one-degree turn about a pivot away from the origin");
        gp_Trsf grow;
        grow.SetScale(gp_Pnt(0.0, 0.0, 0.0), 1.05);
        check(!isIdentityTransform(grow), "nor a 5% growth");
        // The whole point of the cancel path: a drag snapped back to nothing
        // has to read as identity, however far the cursor actually travelled.
        gp_Trsf small;
        small.SetTranslation(gp_Vec(3.0, -2.0, 1.0));
        check(isIdentityTransform(snapTransform(small, gp_Pnt(0.0, 0.0, 0.0),
                                                10.0, 15.0, 0.05)),
              "a drag the 10 mm snap rounds away reads as no change at all");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
