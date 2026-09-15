//
// Mitre end (improvements item 4): cutting a board's end off at an angle,
// like a mitre saw - proven headless before any dial or chip exists.
//
// Every board here is 600 long, 90 wide and 18 thick, so a mitre of angle a
// on its end removes a right triangular prism of legs 90 and 90*tan(a),
// 18 deep: 0.5 * 90 * 90 * tan(a) * 18. The width (90) and the thickness (18)
// are deliberately far apart, so a derivation that swaps them removes a
// visibly different amount (0.5 * 18 * 18 * tan(a) * 90) rather than one that
// happens to coincide.
//
// Flip steps through four sides (ModelingOps::MitreSide): the two width sides
// remove the formula above, the two thickness sides tilt through the
// thickness and remove 0.5 * 18 * 18*tan(a) * 90. Within each pair the volume
// is equal, so which edge was kept is asserted by where the centre of mass
// moved, never by volume alone.
//
// Run:  ctest --preset windows-headless --output-on-failure
//   or: ./build-headless/RelWithDebInfo/headless_mitre
//
#include "ModelingOps.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

const double kPi = 3.14159265358979323846;

constexpr double kLength = 600.0;
constexpr double kWidth = 90.0;
constexpr double kThick = 18.0;

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n", ok ? "[ ok ]" : "[FAIL]", what.c_str(),
                actual, expected);
    if (!ok) ++g_failures;
}

double removedFor(double angleDeg, double width = kWidth, double thick = kThick)
{
    return 0.5 * width * (width * std::tan(angleDeg * kPi / 180.0)) * thick;
}

gp_Pnt faceCentroid(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

// The face of `shape` whose area centroid lies nearest `at` - how a test names
// "the +X end" of a board that has been rotated or mirrored, where the face
// objects are new and OCCT's iteration order is not a promise.
TopoDS_Face faceNearest(const TopoDS_Shape& shape, const gp_Pnt& at)
{
    TopoDS_Face best;
    double bestDistance = 1.0e300;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        const double d = faceCentroid(face).Distance(at);
        if (d < bestDistance) {
            bestDistance = d;
            best = face;
        }
    }
    return best;
}

// Every planar face normal of `shape` that is parallel to none of `known` -
// on a mitred board, exactly the one new end face the cut made.
std::vector<gp_Dir> newPlanarNormals(const TopoDS_Shape& shape, const std::vector<gp_Dir>& known)
{
    std::vector<gp_Dir> found;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if (surface.GetType() != GeomAbs_Plane) continue;
        const gp_Dir n = surface.Plane().Axis().Direction();
        bool seen = false;
        for (const gp_Dir& k : known) {
            if (n.IsParallel(k, 1.0e-6)) {
                seen = true;
                break;
            }
        }
        if (!seen) found.push_back(n);
    }
    return found;
}

bool hasVertexAt(const TopoDS_Shape& shape, const gp_Pnt& at)
{
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
        if (BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Distance(at) < 1.0e-5) return true;
    }
    return false;
}

// The board: x in [0, 600], y in [0, 90], z in [0, 18]. Its +X end face is the
// 90 x 18 rectangle at x = 600.
TopoDS_Shape board()
{
    return ModelingOps::makeBox(gp_Pnt(0.0, 0.0, 0.0), kLength, kWidth, kThick);
}

const gp_Pnt kEndCentre(kLength, kWidth / 2.0, kThick / 2.0);

TopoDS_Shape transformed(const TopoDS_Shape& shape, const gp_Trsf& trsf)
{
    BRepBuilderAPI_Transform t(shape, trsf, Standard_True);
    return t.Shape();
}

const char* sideName(ModelingOps::MitreSide side)
{
    switch (side) {
        case ModelingOps::MitreSide::WidthA: return "WidthA";
        case ModelingOps::MitreSide::ThicknessA: return "ThicknessA";
        case ModelingOps::MitreSide::WidthB: return "WidthB";
        case ModelingOps::MitreSide::ThicknessB: return "ThicknessB";
    }
    return "?";
}

// A thickness side removes 0.5 * T * T*tan(a) * W - the formula with the two
// extents' roles swapped.
double removedThroughThickness(double angleDeg, double width = kWidth, double thick = kThick)
{
    return 0.5 * thick * (thick * std::tan(angleDeg * kPi / 180.0)) * width;
}

// All four sides of one board end at 45 degrees, asserted against the board's
// OWN axes as measured by the WidthA frame - so a side that quietly borrowed
// another side's pivot is caught by where the mass went, not only by how much
// of it left (the two width sides remove equal volumes, and so do the two
// thickness sides).
//
// Expected, from MitreSide's table: the mass moves TOWARD the kept edge -
//   WidthA     along -widthAxis      WidthB     along +widthAxis
//   ThicknessA along +thicknessAxis  ThicknessB along -thicknessAxis
// and not at all along the other in-plane axis.
void checkFourSides(const TopoDS_Shape& shape, const TopoDS_Face& end, const std::string& tag)
{
    using namespace ModelingOps;
    MitreFrame base;
    const bool haveBase = mitreFrame(shape, end, MitreSide::WidthA, base);
    check(haveBase, tag + ": the end has a frame");
    if (!haveBase) return;
    check(base.thicknessAxis.IsEqual(base.outward.Crossed(base.widthAxis), 1.0e-9),
          tag + ": thicknessAxis = outward x widthAxis (the sides' fixed turn)");
    const double shapeVolume = volume(shape);
    const gp_Pnt centre = centreOfMass(shape);

    for (MitreSide side : {MitreSide::WidthA, MitreSide::ThicknessA, MitreSide::WidthB,
                           MitreSide::ThicknessB}) {
        const std::string name = tag + " " + sideName(side);
        const bool thicknessSide = mitreSideIsThickness(side);
        MitreFrame frame;
        check(mitreFrame(shape, end, side, frame), name + ": has a frame");
        checkNear(frame.span, thicknessSide ? kThick : kWidth, 1.0e-6,
                  name + ": span is the extent the cut swings across");
        checkNear(frame.sweep, thicknessSide ? kWidth : kThick, 1.0e-6,
                  name + ": sweep is the extent along the pivot edge");
        check(frame.pivotAxis.IsParallel(thicknessSide ? base.widthAxis : base.thicknessAxis,
                                         1.0e-9),
              name + (thicknessSide ? ": pivots on an edge along the WIDTH"
                                    : ": pivots on an edge along the THICKNESS"));

        const BooleanResult cut = mitreEnd(shape, end, 45.0, side);
        check(cut.ok, name + ": mitre 45 succeeds" + (cut.ok ? std::string() : ": " + cut.error));
        if (!cut.ok) continue;
        const double expected = thicknessSide ? removedThroughThickness(45.0) : removedFor(45.0);
        checkNear(shapeVolume - volume(cut.shape), expected, expected * 1.0e-3,
                  name + (thicknessSide ? ": removes exactly 0.5 * T * T*tan(a) * W"
                                        : ": removes exactly 0.5 * W * W*tan(a) * T"));

        const gp_Vec shift(centre, centreOfMass(cut.shape));
        const double alongW = shift.Dot(gp_Vec(base.widthAxis));
        const double alongT = shift.Dot(gp_Vec(base.thicknessAxis));
        const double moved = thicknessSide ? alongT : alongW;
        const double still = thicknessSide ? alongW : alongT;
        const bool wantPositive = side == MitreSide::WidthB || side == MitreSide::ThicknessA;
        check(wantPositive ? moved > 1.0e-3 : moved < -1.0e-3,
              name + (side == MitreSide::WidthA       ? ": the mass moves to -width (the LOW width edge is kept)"
                      : side == MitreSide::WidthB     ? ": the mass moves to +width (the HIGH width edge is kept)"
                      : side == MitreSide::ThicknessA ? ": the mass moves to +thickness (the HIGH thickness edge is kept)"
                                                      : ": the mass moves to -thickness (the LOW thickness edge is kept)") +
                  " (" + std::to_string(moved) + ")");
        check(std::fabs(still) < 1.0e-6,
              name + ": and does not move along the other in-plane axis (" +
                  std::to_string(still) + ")");

        // The kept long edge, by its two end corners: still on the end face.
        const gp_Vec half = gp_Vec(frame.pivotAxis) * (0.5 * frame.sweep);
        check(hasVertexAt(cut.shape, frame.pivot.Translated(half)) &&
                  hasVertexAt(cut.shape, frame.pivot.Translated(-half)),
              name + ": the pivot edge keeps the board's full length");
        const gp_Pnt farPivot = frame.pivot.Translated(gp_Vec(frame.across) * frame.span);
        check(!hasVertexAt(cut.shape, farPivot.Translated(half)) &&
                  !hasVertexAt(cut.shape, farPivot.Translated(-half)),
              name + ": the opposite edge's end corners are gone");

        // The new face contains the pivot edge's direction and sits at 45
        // degrees to the length axis - no compound tilt.
        std::vector<gp_Dir> known;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const BRepAdaptor_Surface s(TopoDS::Face(it.Current()));
            if (s.GetType() == GeomAbs_Plane) known.push_back(s.Plane().Axis().Direction());
        }
        const std::vector<gp_Dir> fresh = newPlanarNormals(cut.shape, known);
        check(fresh.size() == 1, name + ": makes exactly one new planar face");
        if (fresh.size() == 1) {
            check(std::fabs(fresh.front().Dot(frame.pivotAxis)) < 1.0e-9,
                  name + ": the cut plane contains the pivot edge");
            checkNear(std::acos(std::fabs(fresh.front().Dot(frame.outward))) * 180.0 / kPi, 45.0,
                      1.0e-6, name + ": at the mitre angle to the length axis");
        }
    }
}

}  // namespace

int main()
{
    using namespace ModelingOps;

    const TopoDS_Shape plain = board();
    const TopoDS_Face end = faceNearest(plain, kEndCentre);
    check(!end.IsNull(), "the board's +X end face is found");
    const double boardVolume = volume(plain);
    checkNear(boardVolume, kLength * kWidth * kThick, 1.0e-6, "board volume is 600*90*18");

    // --- the frame, measured in the end face's own terms -------------------
    {
        MitreFrame frame;
        std::string why;
        check(mitreFrame(plain, end, MitreSide::WidthA, frame, &why),
              "a board end has a mitre frame" + (why.empty() ? std::string() : ": " + why));
        checkNear(frame.width, kWidth, 1.0e-6, "frame width is the end face's LONGER extent (90)");
        checkNear(frame.thickness, kThick, 1.0e-6,
                  "frame thickness is the end face's SHORTER extent (18)");
        checkNear(frame.length, kLength, 1.0e-6, "frame length is the board behind the end (600)");
        check(frame.outward.IsEqual(gp_Dir(1.0, 0.0, 0.0), 1.0e-9),
              "frame outward is the end face's OUTWARD normal (+X)");
        check(frame.across.IsParallel(gp_Dir(0.0, 1.0, 0.0), 1.0e-9),
              "frame across runs along the board's width (Y)");
        check(frame.thicknessAxis.IsParallel(gp_Dir(0.0, 0.0, 1.0), 1.0e-9),
              "frame thickness axis runs along the board's thickness (Z)");
        checkNear(frame.pivot.X(), kLength, 1.0e-6, "the pivot lies on the end face's plane");
        checkNear(frame.pivot.Z(), kThick / 2.0, 1.0e-6, "the pivot is at mid-thickness");
        check(std::fabs(frame.pivot.Y()) < 1.0e-6 || std::fabs(frame.pivot.Y() - kWidth) < 1.0e-6,
              "the pivot stands on one of the end face's two width edges");

        MitreFrame flipped;
        check(mitreFrame(plain, end, MitreSide::WidthB, flipped), "the flipped frame exists too");
        checkNear(flipped.pivot.Distance(frame.pivot), kWidth, 1.0e-6,
                  "flip moves the pivot to the OTHER width edge, a full width away");
        check(flipped.across.IsOpposite(frame.across, 1.0e-9),
              "and flip reverses the across direction");
    }

    // --- 45 and 30 degrees on an axis-aligned board ------------------------
    for (double angle : {45.0, 30.0}) {
        const std::string tag = std::to_string(static_cast<int>(angle)) + " deg";
        const BooleanResult cut = mitreEnd(plain, end, angle, MitreSide::WidthA);
        check(cut.ok, "mitre " + tag + " succeeds" + (cut.ok ? std::string() : ": " + cut.error));
        if (!cut.ok) continue;
        check(countSolids(cut.shape) == 1, "mitre " + tag + " leaves one solid");
        const double removed = boardVolume - volume(cut.shape);
        checkNear(removed, removedFor(angle), removedFor(angle) * 1.0e-3,
                  "mitre " + tag + " removes exactly 0.5 * W * W*tan(a) * T");

        const std::vector<gp_Dir> fresh =
            newPlanarNormals(cut.shape, {gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), gp_Dir(0, 0, 1)});
        check(fresh.size() == 1, "mitre " + tag + " makes exactly one new planar face");
        if (fresh.size() == 1) {
            const double measured =
                std::acos(std::fabs(fresh.front().Dot(gp_Dir(1, 0, 0)))) * 180.0 / kPi;
            checkNear(measured, angle, 1.0e-6,
                      "mitre " + tag + "'s new end face is at the mitre angle to the length axis");
            check(std::fabs(fresh.front().Dot(gp_Dir(0, 0, 1))) < 1.0e-9,
                  "mitre " + tag + "'s cut contains the thickness axis (no compound tilt)");
        }
    }

    // --- flip removes the OTHER corner ------------------------------------
    {
        const BooleanResult normal = mitreEnd(plain, end, 45.0, MitreSide::WidthA);
        const BooleanResult flipped = mitreEnd(plain, end, 45.0, MitreSide::WidthB);
        check(normal.ok && flipped.ok, "mitre 45 succeeds both ways round");
        if (normal.ok && flipped.ok) {
            checkNear(volume(flipped.shape), volume(normal.shape), 1.0e-3,
                      "flip removes the same amount");
            MitreFrame frame, flippedFrame;
            mitreFrame(plain, end, MitreSide::WidthA, frame);
            mitreFrame(plain, end, MitreSide::WidthB, flippedFrame);
            const gp_Pnt centre = centreOfMass(plain);
            const double side = gp_Vec(centre, centreOfMass(normal.shape)).Dot(gp_Vec(frame.across));
            const double flippedSide =
                gp_Vec(centre, centreOfMass(flipped.shape)).Dot(gp_Vec(frame.across));
            check(side < -1.0e-3,
                  "unflipped: the mass moves toward the pivot edge - the far corner came off");
            check(flippedSide > 1.0e-3,
                  "flipped: the mass moves the other way - the other corner came off");
            // The kept long edge: the pivot's own edge still reaches the end
            // of the board, and the far edge no longer does.
            const gp_Vec halfT = gp_Vec(frame.thicknessAxis) * (kThick / 2.0);
            const gp_Pnt keptA = frame.pivot.Translated(halfT);
            const gp_Pnt keptB = frame.pivot.Translated(-halfT);
            const gp_Pnt flippedA = flippedFrame.pivot.Translated(halfT);
            const gp_Pnt flippedB = flippedFrame.pivot.Translated(-halfT);
            check(hasVertexAt(normal.shape, keptA) && hasVertexAt(normal.shape, keptB),
                  "unflipped: the pivot edge keeps the board's full length");
            check(!hasVertexAt(normal.shape, flippedA) && !hasVertexAt(normal.shape, flippedB),
                  "unflipped: the far edge's end corners are gone");
            check(hasVertexAt(flipped.shape, flippedA) && hasVertexAt(flipped.shape, flippedB),
                  "flipped: the OTHER edge keeps the full length");
            check(!hasVertexAt(flipped.shape, keptA) && !hasVertexAt(flipped.shape, keptB),
                  "flipped: and the unflipped pivot's corners are the ones gone");
        }
    }

    // --- the same cut on a board rotated about a NON-world axis ------------
    {
        gp_Trsf spin;
        spin.SetRotation(gp_Ax1(gp_Pnt(35.0, -20.0, 7.0), gp_Dir(1.0, 2.0, 3.0)), 37.0 * kPi / 180.0);
        const TopoDS_Shape rotated = transformed(plain, spin);
        const TopoDS_Face rotatedEnd = faceNearest(rotated, kEndCentre.Transformed(spin));
        check(!rotatedEnd.IsNull(), "the rotated board's end face is found");
        MitreFrame frame;
        check(mitreFrame(rotated, rotatedEnd, MitreSide::WidthA, frame), "the rotated board end has a frame");
        checkNear(frame.width, kWidth, 1.0e-6,
                  "rotated: width is measured in the face's own frame, not a world box (90)");
        checkNear(frame.thickness, kThick, 1.0e-6, "rotated: thickness likewise (18)");
        checkNear(frame.length, kLength, 1.0e-6, "rotated: length likewise (600)");
        for (double angle : {45.0, 30.0}) {
            const std::string tag = std::to_string(static_cast<int>(angle)) + " deg";
            const BooleanResult cut = mitreEnd(rotated, rotatedEnd, angle, MitreSide::WidthA);
            check(cut.ok, "rotated: mitre " + tag + " succeeds" +
                              (cut.ok ? std::string() : ": " + cut.error));
            if (!cut.ok) continue;
            checkNear(volume(rotated) - volume(cut.shape), removedFor(angle),
                      removedFor(angle) * 1.0e-3,
                      "rotated: mitre " + tag + " removes the same volume as the square board");
        }
    }

    // --- a MIRRORED board (negative determinant) cuts outward --------------
    //
    // Mirrored across y = 0, the +X end face keeps its raw geometric normal and
    // has its orientation flag toggled by BRepBuilderAPI_Transform - exactly the
    // face the flag-only outward rule reads backwards (see pullFace).
    {
        const BooleanResult mirrored = mirrorShape(plain, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)));
        check(mirrored.ok, "the board mirrors");
        if (mirrored.ok) {
            const gp_Pnt mirroredEndCentre(kLength, -kWidth / 2.0, kThick / 2.0);
            const TopoDS_Face mirroredEnd = faceNearest(mirrored.shape, mirroredEndCentre);
            MitreFrame frame;
            const bool haveFrame = mitreFrame(mirrored.shape, mirroredEnd, MitreSide::WidthA, frame);
            check(haveFrame, "mirrored: the end has a frame");
            check(haveFrame && frame.outward.IsEqual(gp_Dir(1.0, 0.0, 0.0), 1.0e-9),
                  "mirrored: the outward normal is +X, out of the board, whatever the flag says");
            const BooleanResult cut = mitreEnd(mirrored.shape, mirroredEnd, 45.0, MitreSide::WidthA);
            check(cut.ok, "mirrored: mitre 45 succeeds" + (cut.ok ? std::string() : ": " + cut.error));
            if (cut.ok) {
                checkNear(volume(mirrored.shape) - volume(cut.shape), removedFor(45.0),
                          removedFor(45.0) * 1.0e-3,
                          "mirrored: mitre 45 removes exactly the formula's volume");
                check(centreOfMass(cut.shape).X() < centreOfMass(mirrored.shape).X(),
                      "mirrored: the cut came off the END, so the mass moved back along the board");
            }
        }
    }

    // --- all four sides: Flip's cycle, on a square board ------------------
    {
        MitreSide s = MitreSide::WidthA;
        check((s = nextMitreSide(s)) == MitreSide::ThicknessA, "cycle: left -> top (WidthA -> ThicknessA)");
        check((s = nextMitreSide(s)) == MitreSide::WidthB, "cycle: top -> right (ThicknessA -> WidthB)");
        check((s = nextMitreSide(s)) == MitreSide::ThicknessB, "cycle: right -> bottom (WidthB -> ThicknessB)");
        check((s = nextMitreSide(s)) == MitreSide::WidthA, "cycle: bottom -> left - four steps come home");
        check(!mitreSideIsThickness(MitreSide::WidthA) && mitreSideIsThickness(MitreSide::ThicknessA) &&
                  !mitreSideIsThickness(MitreSide::WidthB) && mitreSideIsThickness(MitreSide::ThicknessB),
              "cycle: the kinds alternate width, thickness, width, thickness");
        checkFourSides(plain, end, "square");
    }

    // --- all four sides on a board rotated about a NON-world axis ----------
    {
        gp_Trsf spin;
        spin.SetRotation(gp_Ax1(gp_Pnt(-40.0, 15.0, 22.0), gp_Dir(-2.0, 1.0, 4.0)), 53.0 * kPi / 180.0);
        const TopoDS_Shape rotated = transformed(plain, spin);
        const TopoDS_Face rotatedEnd = faceNearest(rotated, kEndCentre.Transformed(spin));
        checkFourSides(rotated, rotatedEnd, "rotated");
    }

    // --- a MIRRORED board, through the thickness ---------------------------
    {
        const BooleanResult mirrored = mirrorShape(plain, gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)));
        if (mirrored.ok) {
            const TopoDS_Face mirroredEnd =
                faceNearest(mirrored.shape, gp_Pnt(kLength, -kWidth / 2.0, kThick / 2.0));
            for (MitreSide side : {MitreSide::ThicknessA, MitreSide::ThicknessB}) {
                const std::string name = std::string("mirrored ") + sideName(side);
                MitreFrame frame;
                const bool haveFrame = mitreFrame(mirrored.shape, mirroredEnd, side, frame);
                check(haveFrame && frame.outward.IsEqual(gp_Dir(1.0, 0.0, 0.0), 1.0e-9),
                      name + ": the outward normal is +X, whatever the flag says");
                const BooleanResult cut = mitreEnd(mirrored.shape, mirroredEnd, 45.0, side);
                check(cut.ok, name + ": mitre 45 succeeds" + (cut.ok ? std::string() : ": " + cut.error));
                if (!cut.ok || !haveFrame) continue;
                checkNear(volume(mirrored.shape) - volume(cut.shape), removedThroughThickness(45.0),
                          removedThroughThickness(45.0) * 1.0e-3,
                          name + ": removes exactly 0.5 * T * T*tan(a) * W");
                const gp_Vec shift(centreOfMass(mirrored.shape), centreOfMass(cut.shape));
                check(shift.X() < -1.0e-3,
                      name + ": the cut came off the END, so the mass moved back along the board");
                const double alongT = shift.Dot(gp_Vec(frame.thicknessAxis));
                check(side == MitreSide::ThicknessA ? alongT > 1.0e-3 : alongT < -1.0e-3,
                      name + ": the mass moves toward the kept thickness edge");
            }
        }
    }

    // --- refusals ----------------------------------------------------------
    {
        for (double angle : {0.0, 90.0, 120.0, 0.5, 89.5, -45.0}) {
            const BooleanResult r = mitreEnd(plain, end, angle, MitreSide::WidthA);
            check(!r.ok && r.shape.IsNull() && !r.error.empty(),
                  "angle " + std::to_string(angle) + " is refused with a sentence and a null shape");
        }
        check(mitreEnd(plain, end, 1.0, MitreSide::WidthA).ok, "angle 1 (the lower bound) is accepted");
        // 89 on a 600 mm board is too long (below); on a 6 m one it fits.
        const TopoDS_Shape longBoard = makeBox(gp_Pnt(0, 0, 0), 6000.0, kWidth, kThick);
        const TopoDS_Face longEnd = faceNearest(longBoard, gp_Pnt(6000.0, kWidth / 2, kThick / 2));
        check(mitreEnd(longBoard, longEnd, 89.0, MitreSide::WidthA).ok,
              "angle 89 (the upper bound) is accepted where the board is long enough");

        const TopoDS_Shape post = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                                                           20.0, 300.0)
                                      .Shape();
        TopoDS_Face side;
        for (TopExp_Explorer it(post, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face f = TopoDS::Face(it.Current());
            if (BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder) side = f;
        }
        check(!side.IsNull(), "a cylinder's curved side face is found");
        const BooleanResult curved = mitreEnd(post, side, 45.0, MitreSide::WidthA);
        check(!curved.ok && curved.shape.IsNull(), "a non-planar face is refused");
        check(!canMitreEnd(post, side), "canMitreEnd says no to a non-planar face");

        const TopoDS_Shape other = makeBox(gp_Pnt(1000, 1000, 1000), kLength, kWidth, kThick);
        const TopoDS_Face foreign = faceNearest(other, gp_Pnt(1000 + kLength, 1000 + kWidth / 2, 1000 + kThick / 2));
        const BooleanResult wrongBody = mitreEnd(plain, foreign, 45.0, MitreSide::WidthA);
        check(!wrongBody.ok && wrongBody.shape.IsNull(), "a face from another body is refused");
        check(!canMitreEnd(plain, foreign), "canMitreEnd says no to a face from another body");
        check(!mitreEnd(plain, TopoDS_Face(), 45.0, MitreSide::WidthA).ok, "a null face is refused");
        check(!mitreEnd(TopoDS_Shape(), end, 45.0, MitreSide::WidthA).ok, "a null body is refused");

        // 90 * tan(89) is about 5156 mm, far past a 600 mm board.
        const BooleanResult tooLong = mitreEnd(plain, end, 89.0, MitreSide::WidthA);
        check(!tooLong.ok && tooLong.shape.IsNull() && !tooLong.error.empty(),
              "a cut longer than the board is refused");

        // A 50 mm stub: 90 * tan(30) = 51.96 runs past it, 90 * tan(25) = 41.97 fits.
        const TopoDS_Shape stub = makeBox(gp_Pnt(0, 0, 0), 50.0, kWidth, kThick);
        const TopoDS_Face stubEnd = faceNearest(stub, gp_Pnt(50.0, kWidth / 2, kThick / 2));
        check(!mitreEnd(stub, stubEnd, 30.0, MitreSide::WidthA).ok,
              "on a 50 mm stub, 30 deg (51.96 mm of cut) is refused");
        const BooleanResult fits = mitreEnd(stub, stubEnd, 25.0, MitreSide::WidthA);
        check(fits.ok, "on a 50 mm stub, 25 deg (41.97 mm of cut) is accepted");
        if (fits.ok)
            checkNear(volume(stub) - volume(fits.shape), removedFor(25.0), removedFor(25.0) * 1.0e-3,
                      "and removes the formula's volume");

        // Through the thickness the span is only 18, so the length refusal
        // needs a far shorter board to reach it: on a 10 mm stub,
        // 18 * tan(30) = 10.39 runs past it and 18 * tan(25) = 8.39 fits -
        // while across the width the same 30 degrees on the 50 mm stub above
        // was refused, and through its thickness it fits easily.
        const TopoDS_Shape wafer = makeBox(gp_Pnt(0, 0, 0), 10.0, kWidth, kThick);
        const TopoDS_Face waferEnd = faceNearest(wafer, gp_Pnt(10.0, kWidth / 2, kThick / 2));
        for (MitreSide side : {MitreSide::ThicknessA, MitreSide::ThicknessB}) {
            const std::string name = sideName(side);
            const BooleanResult refused = mitreEnd(wafer, waferEnd, 30.0, side);
            check(!refused.ok && refused.shape.IsNull() && !refused.error.empty(),
                  "on a 10 mm stub, " + name + " at 30 deg (10.39 mm of cut) is refused");
            check(checkMitre(wafer, waferEnd, 30.0, side) == MitreCheck::RunsPastTheEnd,
                  "checkMitre: " + name + " at 30 deg on a 10 mm stub is RunsPastTheEnd");
            const BooleanResult waferFits = mitreEnd(wafer, waferEnd, 25.0, side);
            check(waferFits.ok, "on a 10 mm stub, " + name + " at 25 deg (8.39 mm of cut) is accepted");
            if (waferFits.ok)
                checkNear(volume(wafer) - volume(waferFits.shape), removedThroughThickness(25.0),
                          removedThroughThickness(25.0) * 1.0e-3,
                          "and " + name + " removes the thickness formula's volume");
            const BooleanResult stubFits = mitreEnd(stub, stubEnd, 30.0, side);
            check(stubFits.ok, "on the 50 mm stub, " + name +
                                   " at 30 deg fits where the width side was refused");
        }
        check(checkMitre(plain, end, 89.0, MitreSide::ThicknessA) == MitreCheck::RunsPastTheEnd,
              "checkMitre: ThicknessA at 89 on a 600 mm board (18 * tan 89 = 1031 mm) is RunsPastTheEnd");
        check(checkMitre(plain, end, 88.0, MitreSide::ThicknessA) == MitreCheck::Ok,
              "checkMitre: ThicknessA at 88 on a 600 mm board (515 mm) is Ok");

        check(canMitreEnd(plain, end), "canMitreEnd says yes to a board's end face");

        // The classifier the app puts its sentences to - one implementation
        // of every pre-kernel refusal, named.
        check(checkMitre(plain, end, 45.0, MitreSide::WidthA) == MitreCheck::Ok, "checkMitre: 45 on a board is Ok");
        check(checkMitre(plain, end, 0.0, MitreSide::WidthA) == MitreCheck::AngleOutOfRange,
              "checkMitre: 0 is AngleOutOfRange");
        check(checkMitre(plain, end, std::nan(""), MitreSide::WidthA) == MitreCheck::AngleOutOfRange,
              "checkMitre: NaN (an unreadable typed angle) is AngleOutOfRange");
        check(checkMitre(post, side, 45.0, MitreSide::WidthA) == MitreCheck::NotABoardEnd,
              "checkMitre: a curved face is NotABoardEnd");
        check(checkMitre(plain, end, 89.0, MitreSide::WidthA) == MitreCheck::RunsPastTheEnd,
              "checkMitre: 89 on a 600 mm board is RunsPastTheEnd");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
