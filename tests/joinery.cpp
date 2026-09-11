// Headless oracle for the joinery maths (spec:
// docs/superpowers/specs/2026-09-10-joinery-design.md). No Qt, no GPU -
// every function under test is a pure function of shapes and numbers.
#include "Joinery.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <cmath>
#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("[%s] %s\n", condition ? " ok " : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tolerance, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("[%s] %s (got %.4f, wanted %.4f)\n", ok ? " ok " : "FAIL", what.c_str(),
                actual, expected);
    if (!ok) ++g_failures;
}

// One direction against another, component by component - a frame's Z is
// three numbers and reporting only "they differ" would not say how.
void checkDir(const gp_Dir& actual, const gp_Dir& expected, const std::string& what)
{
    const bool ok = std::fabs(actual.X() - expected.X()) <= 1.0e-9 &&
                    std::fabs(actual.Y() - expected.Y()) <= 1.0e-9 &&
                    std::fabs(actual.Z() - expected.Z()) <= 1.0e-9;
    std::printf("[%s] %s (got %+.4f,%+.4f,%+.4f, wanted %+.4f,%+.4f,%+.4f)\n",
                ok ? " ok " : "FAIL", what.c_str(), actual.X(), actual.Y(), actual.Z(),
                expected.X(), expected.Y(), expected.Z());
    if (!ok) ++g_failures;
}

void checkPnt(const gp_Pnt& actual, const gp_Pnt& expected, double tolerance,
              const std::string& what)
{
    const bool ok = actual.Distance(expected) <= tolerance;
    std::printf("[%s] %s (got %.3f,%.3f,%.3f, wanted %.3f,%.3f,%.3f)\n",
                ok ? " ok " : "FAIL", what.c_str(), actual.X(), actual.Y(), actual.Z(),
                expected.X(), expected.Y(), expected.Z());
    if (!ok) ++g_failures;
}
}  // namespace

int main()
{
    // --- families -----------------------------------------------------
    check(Joinery::familyOf(Joinery::Kind::Dowel) == Joinery::Family::Fasteners,
          "a dowel is a fastener");
    check(Joinery::familyOf(Joinery::Kind::PocketScrew) == Joinery::Family::Fasteners,
          "so is a pocket screw");
    check(Joinery::familyOf(Joinery::Kind::Dado) == Joinery::Family::Housing,
          "a dado is a housing");
    check(Joinery::familyOf(Joinery::Kind::Rabbet) == Joinery::Family::Housing,
          "so is a rabbet");
    check(Joinery::familyOf(Joinery::Kind::MortiseTenon) == Joinery::Family::Interlock,
          "a mortise and tenon is an interlock");
    check(Joinery::familyOf(Joinery::Kind::HalfLap) == Joinery::Family::Interlock,
          "so is a half-lap");

    // --- names are the real woodworking words -------------------------
    check(Joinery::kindName(Joinery::Kind::Dowel) == "Dowel", "a dowel is called a dowel");
    check(Joinery::kindName(Joinery::Kind::MortiseTenon) == "Mortise and tenon",
          "and a mortise and tenon says so in full");

    // --- defaults come from the wood ----------------------------------
    // An 18 mm board: a dowel about a third of it, rounded to a real
    // drill size, and a depth that does not punch through.
    const Joinery::Parameters dowel = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
    checkNear(dowel.sizeMm, 6.0, 1.0e-9, "an 18 mm board gets a 6 mm dowel");
    check(dowel.count == 3, "with three of them by default");
    checkNear(dowel.depthAMm, 13.5, 1.0e-9,
               "drilled deep enough to hold and shallow enough not to break through");
    const Joinery::Parameters thick = Joinery::defaultsFor(Joinery::Kind::Dowel, 36.0);
    checkNear(thick.sizeMm, 12.0, 1.0e-9, "a 36 mm board gets a 12 mm dowel");

    // A dado's width is the housed piece; its depth is a third of the host.
    const Joinery::Parameters dado = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
    checkNear(dado.widthMm, 18.0, 1.0e-9, "a dado is as wide as the piece it houses");
    checkNear(dado.depthAMm, 6.0, 1.0e-9, "and a third as deep as the host is thick");

    // A tenon is a third of the stile, the classic rule.
    const Joinery::Parameters tenon =
        Joinery::defaultsFor(Joinery::Kind::MortiseTenon, 18.0);
    checkNear(tenon.thicknessMm, 6.0, 1.0e-9, "a tenon is a third of the stile");

    // --- defaults at a thickness that collides with no struct default -----
    // 24 mm shares no field value with Parameters' own defaults, so every
    // assertion below fails if its corresponding defaultsFor() line were
    // ever deleted - unlike the 18 mm block above, where several computed
    // values happen to equal the untouched struct defaults.
    const Joinery::Parameters dowel24 = Joinery::defaultsFor(Joinery::Kind::Dowel, 24.0);
    checkNear(dowel24.sizeMm, 8.0, 1.0e-9, "a 24 mm board gets an 8 mm dowel");
    checkNear(dowel24.depthAMm, 18.0, 1.0e-9, "depth A is three quarters of 24 mm");
    checkNear(dowel24.depthBMm, 18.0, 1.0e-9, "depth B matches depth A for a dowel");
    checkNear(dowel24.insetMm, 12.0, 1.0e-9, "inset is half the board thickness");
    checkNear(dowel24.endMarginMm, 40.0, 1.0e-9,
               "and the first and last sit 40 mm from the ends");
    checkNear(dowel24.angleDeg, 0.0, 1.0e-9, "a dowel is not driven at an angle");

    const Joinery::Parameters pocketScrew24 =
        Joinery::defaultsFor(Joinery::Kind::PocketScrew, 24.0);
    checkNear(pocketScrew24.angleDeg, 15.0, 1.0e-9,
               "a pocket screw is the one kind driven at an angle");

    const Joinery::Parameters dado24 = Joinery::defaultsFor(Joinery::Kind::Dado, 24.0);
    checkNear(dado24.widthMm, 24.0, 1.0e-9, "a dado is as wide as the piece it houses");
    checkNear(dado24.depthAMm, 8.0, 1.0e-9, "and a third as deep as the host is thick");
    checkNear(dado24.depthBMm, 0.0, 1.0e-9, "a dado has no second depth");

    const Joinery::Parameters tenon24 =
        Joinery::defaultsFor(Joinery::Kind::MortiseTenon, 24.0);
    checkNear(tenon24.thicknessMm, 8.0, 1.0e-9, "a tenon is a third of the stile");
    checkNear(tenon24.lengthMm, 36.0, 1.0e-9, "a tenon is one and a half times as long as it is thick");
    checkNear(tenon24.depthAMm, 38.0, 1.0e-9, "the mortise is a hair deeper than the tenon");

    // --- the contact: where two boards actually meet ------------------
    {
        // A 600x300x18 shelf whose END lands flat on the FACE of an
        // 800x18x400 upright. They touch over 300 x 18.
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();

        const Joinery::ContactResult found = Joinery::findContact(panel, shelf);
        check(found.ok, "a shelf resting against a panel has a contact");
        check(found.error.empty(), "and a successful find carries no error");
        if (found.ok) {
            const double along = found.contact.uMax - found.contact.uMin;
            const double across = found.contact.vMax - found.contact.vMin;
            const double longSide = std::max(along, across);
            const double shortSide = std::min(along, across);
            checkNear(longSide, 300.0, 1.0e-6, "the contact is as long as the shelf is deep");
            checkNear(shortSide, 18.0, 1.0e-6, "and as wide as the shelf is thick");
            checkNear(found.contact.thicknessBMm, 18.0, 1.0e-6,
                      "the shelf's own thickness is measured for the defaults");
            checkNear(found.contact.thicknessAMm, 18.0, 1.0e-6,
                      "and so is the panel's");

            // The frame is the region's OWN frame, so its X runs along the
            // joint: u is the 300 mm run and v the 18 mm width, not the
            // other way round and not a world-derived mixture of the two.
            checkNear(found.contact.uLength(), 300.0, 1.0e-6,
                      "u runs ALONG the joint by construction");
            checkNear(found.contact.vLength(), 18.0, 1.0e-6, "and v across it");
            check(found.contact.runsAlongU(),
                  "so runsAlongU() is true - the fastener row follows u");
            checkNear(found.contact.runLength(), 300.0, 1.0e-6,
                      "and runLength() is the 300 mm run");

            // One origin convention: the region's own (uMin, vMin) corner.
            checkNear(found.contact.uMin, 0.0, 1.0e-9,
                      "the frame's origin IS the region's own corner, so uMin is 0");
            checkNear(found.contact.vMin, 0.0, 1.0e-9, "and vMin is 0");

            // at() turns (u, v) into a world point, and the centre of the
            // (u, v) rectangle must be the centre of the real 300 x 18
            // region - which pins the origin, both axes and at()'s
            // arithmetic in one number, whichever of the four corners the
            // origin landed on and whichever way the axes point.
            const gp_Pnt middle = found.contact.at(found.contact.uLength() / 2.0,
                                                   found.contact.vLength() / 2.0);
            checkPnt(middle, gp_Pnt(18.0, 150.0, 409.0), 1.0e-6,
                     "at(mid, mid) is the region's own centre, on the contact plane");
            // And at(0, 0) is a real corner of it, not the underlying
            // plane's arbitrary origin.
            const gp_Pnt corner = found.contact.at(0.0, 0.0);
            check(std::fabs(corner.X() - 18.0) < 1.0e-6 &&
                      (std::fabs(corner.Y()) < 1.0e-6 || std::fabs(corner.Y() - 300.0) < 1.0e-6) &&
                      (std::fabs(corner.Z() - 400.0) < 1.0e-6 ||
                       std::fabs(corner.Z() - 418.0) < 1.0e-6),
                  "and at(0, 0) is one of the region's four real corners");

            // Z points from bodyA into bodyB. The panel occupies x[0,18] and
            // the shelf x[18,618], so from the panel into the shelf is +X.
            checkDir(found.contact.frame.Direction(), gp_Dir(1.0, 0.0, 0.0),
                     "the contact normal points from the panel INTO the shelf");
        }

        // The same joint, arguments the other way round: "from bodyA into
        // bodyB" requires the opposite normal. This is the single assertion
        // that catches a normal derived from anything but where the two
        // solids actually are - the two planes' signed offset is exactly
        // 0.0 for a flush contact and answers identically both ways round.
        const Joinery::ContactResult swapped = Joinery::findContact(shelf, panel);
        check(swapped.ok, "the same joint is found with the arguments swapped");
        if (swapped.ok) {
            checkDir(swapped.contact.frame.Direction(), gp_Dir(-1.0, 0.0, 0.0),
                     "and its normal is REVERSED - Z always runs from bodyA into bodyB");
            checkNear(swapped.contact.frame.Direction().Dot(found.contact.frame.Direction()),
                      -1.0, 1.0e-9, "exactly opposed, not merely different");
            checkNear(swapped.contact.runLength(), 300.0, 1.0e-6,
                      "and the joint is the same size from either end");
        }

        // A missing piece is refused, with a reason.
        const Joinery::ContactResult missing = Joinery::findContact(TopoDS_Shape(), panel);
        check(!missing.ok, "a missing piece has no contact");
        check(!missing.error.empty(), "and says which way it failed");

        // Pull the shelf 2 mm away: no contact, and it says so.
        gp_Trsf gap;
        gap.SetTranslation(gp_Vec(2.0, 0.0, 0.0));
        const TopoDS_Shape floating =
            BRepBuilderAPI_Transform(shelf, gap, Standard_True).Shape();
        const Joinery::ContactResult apart = Joinery::findContact(panel, floating);
        check(!apart.ok, "two pieces 2 mm apart have no contact");
        check(!apart.error.empty(), "and the refusal says why");

        // Within tolerance is still a contact - a model is never perfect.
        gp_Trsf hair;
        hair.SetTranslation(gp_Vec(0.05, 0.0, 0.0));
        const TopoDS_Shape nearly =
            BRepBuilderAPI_Transform(shelf, hair, Standard_True).Shape();
        check(Joinery::findContact(panel, nearly).ok,
              "a 0.05 mm gap is still a contact - within tolerance");

        // ...and so is a shelf biting 0.05 mm INTO the panel. The header
        // promises a face contact wins over an overlap when both exist, and
        // these two both touch over 300 x 18 and share a sliver of volume.
        gp_Trsf bite;
        bite.SetTranslation(gp_Vec(-0.05, 0.0, 0.0));
        const TopoDS_Shape biting =
            BRepBuilderAPI_Transform(shelf, bite, Standard_True).Shape();
        const Joinery::ContactResult both = Joinery::findContact(panel, biting);
        check(both.ok && both.contact.type == Joinery::Contact::Type::Face,
              "a face contact wins over an overlap when both exist");
        if (both.ok) {
            checkNear(both.contact.runLength(), 300.0, 1.0e-6,
                      "and it is the real 300 mm face, not the sliver");
        }

        // Two boards that merely share an edge are not a joint surface.
        const TopoDS_Shape edgeOnly =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 300.0, 800.0), 600.0, 300.0, 18.0).Shape();
        check(!Joinery::findContact(panel, edgeOnly).ok,
              "two boards touching only at an edge have no contact face");

        // A NON-CONVEX host: a 36 mm panel rabbeted back to 18 mm over its
        // lower half, with a board butting flat on the step. A real 300 x 18
        // contact with no interpenetration at all - but the panel's own
        // x-extent runs to 36 and the board's starts at 18, so a criterion
        // that compares the two WHOLE solids' extents along the normal sees
        // an 18 mm straddle, 300 mm away from the joint, and refuses it.
        // Whether a plane separates two solids is a question about where
        // they are AT THE CONTACT, which is why it is asked with a point
        // classification either side of the region's own centroid.
        const TopoDS_Shape slab =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 36.0, 300.0, 800.0).Shape();
        const TopoDS_Shape rabbet =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 0.0), 18.0, 300.0, 400.0).Shape();
        const TopoDS_Shape stepped = BRepAlgoAPI_Cut(slab, rabbet).Shape();
        const TopoDS_Shape onStep =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 100.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult stepJoint = Joinery::findContact(stepped, onStep);
        check(stepJoint.ok, "a board butting flat on a rabbeted panel has a contact");
        if (stepJoint.ok) {
            checkNear(stepJoint.contact.runLength(), 300.0, 1.0e-6,
                      "as long as the board is deep, step or no step");
            checkNear(std::min(stepJoint.contact.uLength(), stepJoint.contact.vLength()),
                      18.0, 1.0e-6, "and as wide as the board is thick");
            checkDir(stepJoint.contact.frame.Direction(), gp_Dir(1.0, 0.0, 0.0),
                     "with the normal still running from the panel into the board");
        }

        // A flat board on a ROUND leg's top touches along a line, not over a
        // rectangle: the shared region is a disc whose single seam vertex
        // collapses both extents to zero. That has to be a refusal with a
        // reason - ok == true carrying uLength() == 0 would be a refusal
        // surfacing as a success, which this project forbids outright.
        const TopoDS_Shape leg =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                                     20.0, 700.0).Shape();
        const TopoDS_Shape tabletop =
            BRepPrimAPI_MakeBox(gp_Pnt(-150.0, -150.0, 700.0), 300.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult round = Joinery::findContact(leg, tabletop);
        check(!round.ok,
              "a board on a round leg is refused, not reported as a zero-size contact");
        check(round.error.find("curved") != std::string::npos,
              "and the refusal names the curved region rather than denying they meet");

        // Spin the WHOLE touching assembly 45 degrees about X. Both pieces
        // move together, so the joint is unchanged and still 300 x 18 - but
        // the contact normal stays world +X while the REGION'S OWN edges
        // rotate away from every world axis. That is the half a spin about Z
        // cannot reach: rotating about the normal's own axis leaves the
        // region's edges lined up with the axes gp_Ax3 picks out of world
        // space, so it tests an oblique NORMAL and never an oblique
        // RECTANGLE. Measured against a world-derived frame, this exact
        // 300 x 18 contact reads 224.86 x 224.86.
        gp_Trsf spin;
        const double angle = std::atan(1.0);  // pi/4 radians - 45 degrees
        spin.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)), angle);
        const TopoDS_Shape panelSpun =
            BRepBuilderAPI_Transform(panel, spin, Standard_True).Shape();
        const TopoDS_Shape shelfSpun =
            BRepBuilderAPI_Transform(shelf, spin, Standard_True).Shape();

        const Joinery::ContactResult oblique = Joinery::findContact(panelSpun, shelfSpun);
        check(oblique.ok,
              "a joint on an oblique contact plane is still found, not refused");
        if (oblique.ok) {
            check(oblique.contact.type == Joinery::Contact::Type::Face,
                  "and it reads as a face contact, not a misdetected overlap");
            checkNear(oblique.contact.uLength(), 300.0, 1.0e-6,
                      "rotating the joint in its own plane does not change its length");
            checkNear(oblique.contact.vLength(), 18.0, 1.0e-6,
                      "or its width - the joint's size is frame-invariant");
            checkNear(oblique.contact.runLength(), 300.0, 1.0e-6,
                      "so the run is still 300 mm at 45 degrees");
            // The spun shelf's world bounding box is (300+18)/sqrt(2) =
            // 224.86 mm across its two rotated axes, so this is the
            // assertion that separates "the solid's own thickness" from
            // "the smallest side of its world box".
            checkNear(oblique.contact.thicknessBMm, 18.0, 1.0e-6,
                      "and the spun shelf is still an 18 mm board, not a 224.86 mm one");
            checkDir(oblique.contact.frame.Direction(), gp_Dir(1.0, 0.0, 0.0),
                     "the normal is untouched by a rotation about itself");
        }

        // The same joint spun about Z instead, which is what puts the PANEL's
        // own two thin axes off the world axes: its world box is 224.86 mm
        // across both of them while the board is still 18 mm thick.
        gp_Trsf spinZ;
        spinZ.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), angle);
        const Joinery::ContactResult obliqueZ = Joinery::findContact(
            BRepBuilderAPI_Transform(panel, spinZ, Standard_True).Shape(),
            BRepBuilderAPI_Transform(shelf, spinZ, Standard_True).Shape());
        check(obliqueZ.ok, "and a joint spun about Z is found too");
        if (obliqueZ.ok) {
            checkNear(obliqueZ.contact.runLength(), 300.0, 1.0e-6,
                      "still 300 mm long");
            checkNear(obliqueZ.contact.thicknessAMm, 18.0, 1.0e-6,
                      "and the spun panel is still an 18 mm board");
        }
    }

    // --- overlap: two pieces crossing, for a half-lap -----------------
    {
        // Deliberately DIFFERENT widths: a 60 x 60 lap cannot tell u from v,
        // so an axis mix-up in the extent read would be invisible.
        const TopoDS_Shape railA =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 400.0, 40.0, 20.0).Shape();
        const TopoDS_Shape railB =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -100.0, 0.0), 60.0, 300.0, 20.0).Shape();
        const Joinery::ContactResult crossed = Joinery::findContact(railA, railB);
        check(crossed.ok && crossed.contact.type == Joinery::Contact::Type::Overlap,
              "two crossing rails report an OVERLAP, not a face contact");
        if (crossed.ok) {
            checkNear(crossed.contact.uMax - crossed.contact.uMin, 60.0, 1.0e-6,
                      "the overlap is as wide as the crossing rail");
            checkNear(crossed.contact.vLength(), 40.0, 1.0e-6,
                      "and as deep as the rail it crosses - u and v are told apart");
            checkNear(crossed.contact.thicknessAMm, 20.0, 1.0e-6,
                      "each rail's own thickness is measured");
            checkNear(crossed.contact.thicknessBMm, 20.0, 1.0e-6, "both of them");
            checkNear(crossed.contact.uMin, 0.0, 1.0e-9,
                      "the lap's origin is its own corner too - the same convention");
            checkNear(crossed.contact.vMin, 0.0, 1.0e-9, "in both directions");
            // The lap depth is the rails' 20 mm thickness, which here runs
            // along world Z. The frame's origin sits on the lap's own
            // minimum-depth face, so the lap spans [0, 20] from it.
            checkNear(std::fabs(crossed.contact.frame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0))),
                      1.0, 1.0e-9, "and Z is the lap DEPTH direction");
            const double z = crossed.contact.frame.Location().Z();
            check(std::fabs(z) < 1.0e-6 || std::fabs(z - 20.0) < 1.0e-6,
                  "with the frame's plane on one of the lap's own two depth faces");
        }

        // Spin the whole crossing 45 degrees about Z: the lap footprint is
        // still 60 x 40. A world bounding box of the intersection inflates
        // it to 84.85 x 84.85 - 41% over in each direction.
        gp_Trsf spin;
        spin.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                         std::atan(1.0));
        const Joinery::ContactResult spunLap = Joinery::findContact(
            BRepBuilderAPI_Transform(railA, spin, Standard_True).Shape(),
            BRepBuilderAPI_Transform(railB, spin, Standard_True).Shape());
        check(spunLap.ok && spunLap.contact.type == Joinery::Contact::Type::Overlap,
              "a crossing spun in its own plane is still an overlap");
        if (spunLap.ok) {
            checkNear(spunLap.contact.uLength(), 60.0, 1.0e-6,
                      "and the lap footprint is still 60 mm one way");
            checkNear(spunLap.contact.vLength(), 40.0, 1.0e-6, "and 40 the other");
        }

        // Stand the whole crossing on edge (90 degrees about X): the lap
        // plane's normal is now world -Y, not +Z. A hardcoded world +Z
        // returns the top of the world box and the rails' THICKNESS as one
        // of the two footprint extents.
        gp_Trsf onEdge;
        onEdge.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
                           2.0 * std::atan(1.0));
        const Joinery::ContactResult edgeLap = Joinery::findContact(
            BRepBuilderAPI_Transform(railA, onEdge, Standard_True).Shape(),
            BRepBuilderAPI_Transform(railB, onEdge, Standard_True).Shape());
        check(edgeLap.ok && edgeLap.contact.type == Joinery::Contact::Type::Overlap,
              "a crossing standing on edge is still an overlap");
        if (edgeLap.ok) {
            checkNear(std::fabs(edgeLap.contact.frame.Direction().Dot(gp_Dir(0.0, 1.0, 0.0))),
                      1.0, 1.0e-9, "and its lap depth runs along Y now, not Z");
            checkNear(edgeLap.contact.uLength(), 60.0, 1.0e-6,
                      "with the footprint still 60 mm one way");
            checkNear(edgeLap.contact.vLength(), 40.0, 1.0e-6, "and 40 the other");
        }

        // Two slabs stacked with a 2 mm bite: no coplanar separating face
        // anywhere, so it is an overlap, and here "from bodyA into bodyB" IS
        // decisive - the lower slab's centre of mass is 18 mm below the
        // upper's, straight along the lap depth. Asserted both ways round,
        // because a sense read off the lap's own face normal instead would
        // answer the same for both orders.
        const TopoDS_Shape lower =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 100.0, 100.0, 20.0).Shape();
        const TopoDS_Shape upper =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 18.0), 100.0, 100.0, 20.0).Shape();
        const Joinery::ContactResult up = Joinery::findContact(lower, upper);
        check(up.ok && up.contact.type == Joinery::Contact::Type::Overlap,
              "two slabs biting into each other are an overlap");
        if (up.ok) {
            checkDir(up.contact.frame.Direction(), gp_Dir(0.0, 0.0, 1.0),
                     "the lap depth runs from the lower slab up into the upper one");
        }
        const Joinery::ContactResult down = Joinery::findContact(upper, lower);
        check(down.ok, "and the same overlap is found the other way round");
        if (down.ok) {
            checkDir(down.contact.frame.Direction(), gp_Dir(0.0, 0.0, -1.0),
                     "with the depth reversed, as bodyA-into-bodyB requires");
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
