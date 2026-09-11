// Headless oracle for the joinery maths (spec:
// docs/superpowers/specs/2026-09-10-joinery-design.md). No Qt, no GPU -
// every function under test is a pure function of shapes and numbers.
#include "Joinery.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
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
        // classification either side of a point genuinely ON the region.
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
            // 18, not the 36 the panel is elsewhere: a housing cut here is a
            // third as deep as the host is thick HERE, and a dowel driven here
            // goes into 18 mm of wood. The whole-solid answer was 36.
            checkNear(stepJoint.contact.thicknessAMm, 18.0, 1.0e-6,
                      "the rabbeted panel is 18 mm of wood where the shelf lands, "
                      "not the 36 it is elsewhere");
        }

        // Two pieces of DIFFERENT thickness, both ways round. Every other pair
        // in this file is equal-thickness, so nothing told thicknessAMm from
        // thicknessBMm: swapping the two assignments in the implementation
        // reran green. These four assertions cannot both hold with them
        // swapped, in either argument order.
        const TopoDS_Shape panel36 =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 36.0, 300.0, 800.0).Shape();
        const TopoDS_Shape thinShelf =
            BRepPrimAPI_MakeBox(gp_Pnt(36.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult unequal = Joinery::findContact(panel36, thinShelf);
        check(unequal.ok, "a 36 mm panel and an 18 mm shelf have a contact");
        if (unequal.ok) {
            checkNear(unequal.contact.thicknessAMm, 36.0, 1.0e-6,
                      "thicknessA is bodyA's own 36 mm");
            checkNear(unequal.contact.thicknessBMm, 18.0, 1.0e-6,
                      "and thicknessB is bodyB's own 18 mm");
        }
        const Joinery::ContactResult unequalSwapped =
            Joinery::findContact(thinShelf, panel36);
        check(unequalSwapped.ok, "and the same contact the other way round");
        if (unequalSwapped.ok) {
            checkNear(unequalSwapped.contact.thicknessAMm, 18.0, 1.0e-6,
                      "where thicknessA is now the 18 mm shelf");
            checkNear(unequalSwapped.contact.thicknessBMm, 36.0, 1.0e-6,
                      "and thicknessB the 36 mm panel - the two follow the arguments");
        }

        // A HOLLOW host: an 18 mm-walled 300 mm carcase with a shelf butting
        // one wall. The carcase is 300 mm across as a solid and 18 mm of wood
        // at every joint on it, so a whole-solid measure reported 300 - which
        // defaultsFor() would turn into a 300 mm wide dado a third of 300 deep.
        // Thickness is measured at the joint now, along the contact normal from
        // inside the wall.
        const TopoDS_Shape carcaseOuter =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 300.0, 300.0, 300.0).Shape();
        const TopoDS_Shape carcaseVoid =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 18.0, 18.0), 264.0, 264.0, 264.0).Shape();
        const TopoDS_Shape carcase =
            BRepAlgoAPI_Cut(carcaseOuter, carcaseVoid).Shape();
        const TopoDS_Shape onCarcase =
            BRepPrimAPI_MakeBox(gp_Pnt(300.0, 40.0, 100.0), 400.0, 200.0, 18.0).Shape();
        const Joinery::ContactResult hollow = Joinery::findContact(carcase, onCarcase);
        check(hollow.ok, "a board butting a hollow carcase's wall has a contact");
        if (hollow.ok) {
            checkNear(hollow.contact.thicknessAMm, 18.0, 1.0e-6,
                      "and the carcase is 18 mm of wood at that joint, not 300");
            checkNear(hollow.contact.thicknessBMm, 18.0, 1.0e-6,
                      "with the board's own 18 on the other side");
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
            // uMin/vMin against 0.0 only restate literals the implementation
            // writes; what actually pins the origin convention is at() landing
            // on a real corner of the lap, which is asserted for the Face
            // branch and was not for this one - deleting the in-plane half of
            // the Overlap origin shift reran green. The true lap is
            // x[150,210] y[0,40] z[0,20].
            checkNear(crossed.contact.uMin, 0.0, 1.0e-9,
                      "the lap's origin is its own corner too - the same convention");
            checkNear(crossed.contact.vMin, 0.0, 1.0e-9, "in both directions");
            checkPnt(crossed.contact.at(0.0, 0.0), gp_Pnt(150.0, 0.0, 0.0), 1.0e-6,
                     "and at(0, 0) is a real corner of the lap, not its centre");
            checkPnt(crossed.contact.at(crossed.contact.uMax, crossed.contact.vMax),
                     gp_Pnt(210.0, 40.0, 0.0), 1.0e-6,
                     "with (uMax, vMax) the diagonally opposite one");
            // Deliberately at(uMax, 0) rather than the midpoint: the midpoint
            // is the affine average of the two corners above and so cannot
            // fail on its own, while this one pins the u axis separately from
            // v - it is what tells a u/v swap from a correct frame.
            checkPnt(crossed.contact.at(crossed.contact.uMax, 0.0),
                     gp_Pnt(210.0, 0.0, 0.0), 1.0e-6,
                     "and (uMax, 0) is the corner along u alone");
            // The lap depth is the rails' 20 mm thickness, which here runs
            // along world Z. The frame's origin sits on the lap's own
            // minimum-depth face, so the lap spans [0, 20] from it.
            checkNear(std::fabs(crossed.contact.frame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0))),
                      1.0, 1.0e-9, "and Z is the lap DEPTH direction");
            const double z = crossed.contact.frame.Location().Z();
            check(std::fabs(z) < 1.0e-6 || std::fabs(z - 20.0) < 1.0e-6,
                  "with the frame's plane on one of the lap's own two depth faces");
        }

        // A crossing where the two rails are NOT the same thickness. Both
        // numbers here are what a half-lap splits in half, and the deeper rail
        // is the one that tells the at-the-joint measurement from a whole-solid
        // one: a 60 x 300 x 100 rail's smallest oriented-bounding-box side is
        // its 60 mm WIDTH, while the material a lap cut in it has to halve is
        // the 100 mm it carries along the lap depth.
        const TopoDS_Shape deepRail =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -100.0, 0.0), 60.0, 300.0, 100.0).Shape();
        const Joinery::ContactResult uneven = Joinery::findContact(railA, deepRail);
        check(uneven.ok && uneven.contact.type == Joinery::Contact::Type::Overlap,
              "a thin rail crossing a deep one is an overlap");
        if (uneven.ok) {
            checkNear(uneven.contact.uLength(), 60.0, 1.0e-6,
                      "with the same 60 mm lap footprint one way");
            checkNear(uneven.contact.vLength(), 40.0, 1.0e-6, "and 40 the other");
            checkNear(uneven.contact.thicknessAMm, 20.0, 1.0e-6,
                      "the thin rail carries 20 mm through the lap");
            checkNear(uneven.contact.thicknessBMm, 100.0, 1.0e-6,
                      "and the deep one 100 - not the 60 mm its bounding box is "
                      "narrowest across");
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

    // --- regions that are not a plain rectangle -----------------------
    // Every case here was a wrong answer before this round: two of them
    // refused a real contact outright, one under-reported its run by 29%,
    // and one measured a 40 mm leg as a 700 mm board.
    {
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();

        // An L-SHAPED contact region: a shelf with an upstand on its end, the
        // whole L face butting the panel. Its AREA CENTROID is at roughly
        // (y 72.7, z 513.7), which at z > 418 is off the L entirely - so a
        // probe placed there classifies as outside BOTH solids, and a real
        // 11,952 mm2 contact with no interpenetration whatever was refused
        // with "these two pieces don't meet". A notched board butting a panel
        // is bread-and-butter furniture; the probe has to sit on a point
        // genuinely ON the region, which is what ModelingOps::pointOnFace
        // answers.
        const TopoDS_Shape upstand =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 418.0), 600.0, 18.0, 364.0).Shape();
        ShapeUpgrade_UnifySameDomain unifyL(BRepAlgoAPI_Fuse(shelf, upstand).Shape(),
                                           Standard_True, Standard_True, Standard_True);
        unifyL.Build();
        const Joinery::ContactResult lJoint = Joinery::findContact(panel, unifyL.Shape());
        check(lJoint.ok, "an L-shaped contact region is found, not refused");
        if (lJoint.ok) {
            checkNear(lJoint.contact.uLength(), 382.0, 1.0e-6,
                      "spanning the whole L - 18 of shelf plus 364 of upstand");
            checkNear(lJoint.contact.vLength(), 300.0, 1.0e-6, "by the board's 300 mm depth");
            checkDir(lJoint.contact.frame.Direction(), gp_Dir(1.0, 0.0, 0.0),
                     "with the normal still running from the panel into the board");
        }

        // A C-SHAPED region - a channel end, no hole anywhere - puts its area
        // centroid in the notch for the same reason. 300 x 300 bounding
        // rectangle, and the header now says plainly that a non-rectangular
        // region's extents cover area that is not in contact.
        const TopoDS_Shape slabC =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 300.0).Shape();
        const TopoDS_Shape notchC =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 20.0, 450.0), 600.0, 280.0, 200.0).Shape();
        ShapeUpgrade_UnifySameDomain unifyC(BRepAlgoAPI_Cut(slabC, notchC).Shape(),
                                           Standard_True, Standard_True, Standard_True);
        unifyC.Build();
        const Joinery::ContactResult cJoint = Joinery::findContact(panel, unifyC.Shape());
        check(cJoint.ok, "and so is a C-shaped one");
        if (cJoint.ok) {
            checkNear(cJoint.contact.uLength(), 300.0, 1.0e-6,
                      "reported as its bounding rectangle - 300 mm");
            checkNear(cJoint.contact.vLength(), 300.0, 1.0e-6, "by 300 mm");
        }

        // A CURVED boundary is measured now, not cut short at its vertices. A
        // stadium-ended post's top is one face of area 100*40 + pi*400: its
        // vertices sit at x = +/-50 where the straight sides end, so a vertex-
        // only walk reported a 100 mm run against a true 140 - 29% short, and
        // reported it as a success, which is the same "a wrong size is a
        // refusal surfacing as a success" law in the permissive direction.
        const TopoDS_Shape core =
            BRepPrimAPI_MakeBox(gp_Pnt(-50.0, -20.0, 0.0), 100.0, 40.0, 700.0).Shape();
        const TopoDS_Shape capL =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-50.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                                     20.0, 700.0).Shape();
        const TopoDS_Shape capR =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                                     20.0, 700.0).Shape();
        ShapeUpgrade_UnifySameDomain unifyPost(
            BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(core, capL).Shape(), capR).Shape(),
            Standard_True, Standard_True, Standard_True);
        unifyPost.Build();
        const TopoDS_Shape lid =
            BRepPrimAPI_MakeBox(gp_Pnt(-150.0, -150.0, 700.0), 300.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult stadium =
            Joinery::findContact(unifyPost.Shape(), lid);
        check(stadium.ok, "a stadium-ended post under a board has a contact");
        if (stadium.ok) {
            checkNear(stadium.contact.uLength(), 140.0, 1.0e-6,
                      "measured across its rounded ends - 140 mm, not the 100 between "
                      "its vertices");
            checkNear(stadium.contact.vLength(), 40.0, 1.0e-6, "by 40 mm");
        }
        // The flat control: the same 140 x 40 with square ends must not move.
        const TopoDS_Shape flatPost =
            BRepPrimAPI_MakeBox(gp_Pnt(-70.0, -20.0, 0.0), 140.0, 40.0, 700.0).Shape();
        const Joinery::ContactResult square = Joinery::findContact(flatPost, lid);
        check(square.ok, "and so does a square-ended one of the same size");
        if (square.ok) {
            checkNear(square.contact.uLength(), 140.0, 1.0e-6,
                      "which still measures exactly 140 mm - sampling arcs changed "
                      "nothing for a straight boundary");
            checkNear(square.contact.vLength(), 40.0, 1.0e-6, "by 40 mm");
        }

        // A fat cylinder standing on a narrow board, the board deliberately
        // OFF CENTRE. Three things at once:
        //
        // the region is the disc clipped to the board's 75 mm width, so its
        // straight edges are 2*sqrt(100^2 - 30^2) = 190.788 and
        // 2*sqrt(100^2 - 45^2) = 178.606 long while the region really spans
        // 200 - the arc's own extreme, at x = 0, is not a vertex and not an
        // endpoint of any straight edge;
        //
        // the off-centre 75 mm (rather than a symmetric 60) is what makes the
        // arc ASYMMETRIC in its own parameter, so its extreme does not land on
        // a sample point. On a symmetric arc it lands exactly on the middle
        // sample and a coarse walk is accidentally exact; here it is 0.0036 mm
        // out, which only a refinement inside the bracketing interval closes;
        //
        // and the cylinder's thickness is its 200 mm DIAMETER. Its only planar
        // faces are its two end discs, so measuring the smallest extent over a
        // solid's own face normals - this file's previous answer - reported the
        // 700 mm HEIGHT of a 200 mm-wide post. The oriented bounding box has
        // no such blind spot.
        const TopoDS_Shape fatLeg =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                                     100.0, 700.0).Shape();
        const TopoDS_Shape narrowBoard =
            BRepPrimAPI_MakeBox(gp_Pnt(-30.0, -150.0, -18.0), 75.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult clipped =
            Joinery::findContact(fatLeg, narrowBoard);
        check(clipped.ok, "a fat round leg on a narrow board has a contact");
        if (clipped.ok) {
            checkNear(clipped.contact.uLength(), 200.0, 1.0e-6,
                      "spanning the disc's full 200 mm, not the 190.788 between the "
                      "clipped edges' ends");
            checkNear(clipped.contact.vLength(), 75.0, 1.0e-6, "by the board's 75 mm width");
            checkNear(clipped.contact.thicknessAMm, 200.0, 1.0e-6,
                      "and the leg is a 200 mm-thick piece of wood, not a 700 mm one");
            checkNear(clipped.contact.thicknessBMm, 18.0, 1.0e-6,
                      "while the board is still 18 mm");
        }
    }

    // --- toleranceMm is permissive only, in both directions -----------
    {
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();

        // Raising the tolerance must never take a contact away. It did: with
        // the probe offset derived from toleranceMm, the probe outgrew the
        // piece it was probing once the tolerance reached half a thickness,
        // and this exact flush joint was found at 8.9 and REFUSED at 9.0. The
        // header documents the parameter as purely permissive with no upper
        // bound and six later tasks call it.
        const double wide[] = {0.0, 9.0, 12.0, 50.0, 200.0};
        for (const double tol : wide) {
            const Joinery::ContactResult loose =
                Joinery::findContact(panel, shelf, tol);
            check(loose.ok, "a flush joint is still found at a wide tolerance");
            if (loose.ok) {
                checkNear(loose.contact.runLength(), 300.0, 1.0e-6,
                          "and measures the same 300 mm however loose the tolerance is");
            }
        }

        // And a deliberately loose tolerance must still reach a genuinely
        // gappy joint, which is the direction the parameter exists for. The
        // probe on bodyA's side only has to clear zero - a's own face lies on
        // the contact plane - while the probe on bodyB's side has to clear the
        // gap first or it lands in the slop between the two faces and reports
        // "outside everything". Two offsets, not one: here the 5 mm gap is
        // wider than the 1 mm the wood allows, so a single shared offset
        // cannot reach bodyB at all.
        // NOT named `far`: that is a Windows SDK macro defined to nothing, so
        // the identifier silently disappears and the expression quietly means
        // something else - see CLAUDE.md's Pitfalls. It compiles here only
        // because <windows.h> is not in this file's include chain today.
        gp_Trsf fiveAway;
        fiveAway.SetTranslation(gp_Vec(5.0, 0.0, 0.0));
        const TopoDS_Shape apart =
            BRepBuilderAPI_Transform(shelf, fiveAway, Standard_True).Shape();
        check(!Joinery::findContact(panel, apart).ok,
              "a 5 mm gap is no contact at the default tolerance");
        const Joinery::ContactResult gappy = Joinery::findContact(panel, apart, 6.0);
        check(gappy.ok, "but it is one when the caller allows 6 mm of slop");
        if (gappy.ok) {
            checkNear(gappy.contact.runLength(), 300.0, 1.0e-6,
                      "and it measures the same 300 mm across the gap");
            checkDir(gappy.contact.frame.Direction(), gp_Dir(1.0, 0.0, 0.0),
                     "with the normal still running from the panel into the shelf");
        }

        // The same mechanism from the other end: a piece thinner than the
        // probe offset was refused. The thin direction has to be the one the
        // probe TRAVELS - across the joint, along the contact normal - or the
        // test cannot reproduce the defect it names: a 0.15 mm-deep strip whose
        // thinness lies IN the contact plane passes with the old
        // probe = 2 * tolerance too, because the probe never leaves the wood.
        // So this veneer is 0.15 mm in x, the normal's own direction.
        const TopoDS_Shape veneer =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 0.15, 300.0, 18.0).Shape();
        const Joinery::ContactResult sliver = Joinery::findContact(panel, veneer);
        check(sliver.ok, "and a 0.15 mm-deep piece is found rather than refused for "
                         "being thinner than the probe");
        if (sliver.ok) {
            checkNear(sliver.contact.uLength(), 300.0, 1.0e-6, "300 mm along the joint");
            checkNear(sliver.contact.vLength(), 18.0, 1.0e-6, "by 18 mm across it");
            checkNear(sliver.contact.thicknessBMm, 0.15, 1.0e-9,
                      "with 0.15 mm of wood behind the joint on the veneer's side");
            checkNear(sliver.contact.thicknessAMm, 18.0, 1.0e-6,
                      "and the panel's own 18 on the other");
        }
    }

    // --- what a given contact can actually take -----------------------
    {
        // A face contact: fasteners always; housings and mortises too,
        // because an end lands on a face here.
        Joinery::Contact face;
        face.type = Joinery::Contact::Type::Face;
        face.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        face.uMin = 0.0; face.uMax = 300.0;
        face.vMin = 0.0; face.vMax = 18.0;
        face.thicknessAMm = 18.0;
        face.thicknessBMm = 18.0;

        check(Joinery::validityOf(Joinery::Kind::Dowel, face).empty(),
              "a face contact takes dowels");
        check(Joinery::validityOf(Joinery::Kind::Dado, face).empty(),
              "and a dado");
        check(!Joinery::validityOf(Joinery::Kind::HalfLap, face).empty(),
              "but NOT a half-lap - the pieces are not crossing");

        // An overlap: half-laps yes, dowels no.
        Joinery::Contact cross = face;
        cross.type = Joinery::Contact::Type::Overlap;
        check(Joinery::validityOf(Joinery::Kind::HalfLap, cross).empty(),
              "crossing pieces take a half-lap");
        check(!Joinery::validityOf(Joinery::Kind::Dowel, cross).empty(),
              "and not a row of dowels");

        // A contact too small for the joint refuses with a reason.
        Joinery::Contact tiny = face;
        tiny.uMax = 12.0;
        const std::string why = Joinery::validityOf(Joinery::Kind::Dowel, tiny);
        check(!why.empty(), "a contact smaller than the joint refuses");
        check(why.find("small") != std::string::npos ||
                  why.find("narrow") != std::string::npos,
              "and says it is too small (" + why + ")");

        const std::vector<Joinery::Kind> offered = Joinery::validKindsFor(face);
        check(!offered.empty(), "a real contact offers at least one kind");
        check(std::find(offered.begin(), offered.end(), Joinery::Kind::HalfLap) ==
                  offered.end(),
              "and never offers one that cannot exist there");

        // A refusal names WHICH refusal, not merely that one exists - so a
        // mutation that always returns the same generic reason (or the wrong
        // one) still turns red. Every one of the brief's four documented
        // reasons is exercised here by exact string, not merely emptiness.
        check(Joinery::validityOf(Joinery::Kind::HalfLap, face) ==
                  "the pieces aren't crossing",
              "a half-lap on a face contact names the crossing reason exactly");
        check(Joinery::validityOf(Joinery::Kind::Dowel, cross) ==
                  "the pieces overlap rather than meet",
              "a dowel on a crossing names the overlap reason exactly");
        check(Joinery::validityOf(Joinery::Kind::Dowel, tiny) ==
                  "the contact is too small for a joint",
              "a too-short contact names the small reason exactly, not the "
              "narrow one");

        // The three narrow-family reasons: each family has its own sentence,
        // so a mutation that shared one string across all three (or shuffled
        // which family got which) would still leave every "is it empty"
        // check above green.
        Joinery::Contact narrow = face;
        narrow.vMax = 3.0;  // across is 3 mm - clears the 30 mm run floor, so
                            // only the per-family narrow check can fire
        check(Joinery::validityOf(Joinery::Kind::Dowel, narrow) ==
                  "the contact is too narrow for fasteners",
              "a narrow contact refuses a fastener by name");
        check(Joinery::validityOf(Joinery::Kind::Dado, narrow) ==
                  "the contact is too narrow to house a piece",
              "and a housing by its own name, not the fastener one");
        Joinery::Contact narrowCross = narrow;
        narrowCross.type = Joinery::Contact::Type::Overlap;
        check(Joinery::validityOf(Joinery::Kind::HalfLap, narrowCross) ==
                  "the contact is too narrow for an interlock",
              "and an interlock by its own name too");

        // The 6 mm / 9 mm thresholds themselves, not merely which side of
        // some unstated line 3 mm sits on: a fastener and a housing tolerate
        // exactly 6 mm across, an interlock needs 9. Boundary-exact, so a
        // mutation sliding either threshold by so much as a millimetre turns
        // this red without touching the 3 mm case above at all.
        Joinery::Contact sixAcross = face;
        sixAcross.vMax = 6.0;
        check(Joinery::validityOf(Joinery::Kind::Dowel, sixAcross).empty(),
              "6 mm across is enough for a fastener - the floor is inclusive");
        Joinery::Contact justUnderSix = face;
        justUnderSix.vMax = 5.9;
        check(!Joinery::validityOf(Joinery::Kind::Dowel, justUnderSix).empty(),
              "5.9 mm is not - one tenth of a millimetre either side of the "
              "same floor");
        Joinery::Contact nineAcross = face;
        nineAcross.vMax = 9.0;
        Joinery::Contact nineAcrossCross = nineAcross;
        nineAcrossCross.type = Joinery::Contact::Type::Overlap;
        check(Joinery::validityOf(Joinery::Kind::HalfLap, nineAcrossCross).empty(),
              "9 mm across is enough for an interlock - the floor is "
              "inclusive there too");
        Joinery::Contact justUnderNine = nineAcrossCross;
        justUnderNine.vMax = 8.9;
        check(!Joinery::validityOf(Joinery::Kind::HalfLap, justUnderNine).empty(),
              "8.9 mm is not, on the interlock's own higher floor");

        // The 30 mm run floor is independent of the across-extent checks: a
        // contact can be plenty wide and still too short to run a joint
        // along, and that has to be caught before familyOf() is even asked.
        Joinery::Contact tooShort = face;
        tooShort.uMax = 29.0;  // across (v) stays 18 mm - plenty wide
        check(Joinery::validityOf(Joinery::Kind::Dowel, tooShort) ==
                  "the contact is too small for a joint",
              "a 29 mm run refuses regardless of how wide the contact is");
        Joinery::Contact justEnough = face;
        justEnough.uMax = 30.0;
        check(Joinery::validityOf(Joinery::Kind::Dowel, justEnough).empty(),
              "and exactly 30 mm is enough - the run floor is inclusive too");

        // validKindsFor in full: every kind the brief promises a plain
        // rectangular face contact offers, and the one it must not -
        // guarded by size so an implementation returning the wrong COUNT
        // (dropping one silently, or offering a duplicate) is caught before
        // any index is read, rather than skipped over quietly. Only HalfLap
        // is gated on crossing (the `if (kind == Kind::HalfLap)` branch);
        // every other kind, mortise and tenon included, is judged on size
        // alone here - matching the brief's own comment that "housings and
        // mortises too" land on a face, because an end can land flat on one.
        check(offered.size() == 9,
              "a face contact offers every kind except the half-lap - all "
              "five fasteners, all three housings, and the mortise and tenon");
        if (offered.size() == 9) {
            for (const Joinery::Kind k :
                 {Joinery::Kind::Dowel, Joinery::Kind::PocketScrew, Joinery::Kind::Biscuit,
                  Joinery::Kind::Domino, Joinery::Kind::Screw, Joinery::Kind::Dado,
                  Joinery::Kind::Rabbet, Joinery::Kind::Groove,
                  Joinery::Kind::MortiseTenon}) {
                check(std::find(offered.begin(), offered.end(), k) != offered.end(),
                      Joinery::kindName(k) + " is among the kinds a face contact offers");
            }
        }
        const std::vector<Joinery::Kind> offeredCross = Joinery::validKindsFor(cross);
        check(offeredCross.size() == 1 && offeredCross[0] == Joinery::Kind::HalfLap,
              "a crossing offers exactly one kind - the half-lap, and nothing else "
              "(every other kind refuses with \"overlap rather than meet\")");
    }

    // --- validity on an OBLIQUE frame: unchanged by how the furniture is
    // rotated ------------------------------------------------------------
    // Every task in this feature carries one of these, and it is not a
    // formality: a Task 3 review mutated a world-axis derivation and only
    // the oblique block went red, every axis-aligned test staying green.
    // validityOf reads only contact.type/uLength()/vLength()/runLength(), so
    // this tilts the FRAME while leaving those four numbers untouched - which
    // is exactly what proves the function answers from the joint's own
    // measurements and never from where the joint happens to point in the
    // world. Tilts about the frame's own X (in-plane), not its Z (normal) -
    // rotating about the normal alone leaves the region's edges lined up
    // with whatever axes gp_Ax3 already picked and proves nothing, the exact
    // mistake three separate Task 2 reviews caught.
    {
        Joinery::Contact flat;
        flat.type = Joinery::Contact::Type::Face;
        flat.frame = gp_Ax3(gp_Pnt(5.0, -3.0, 2.0), gp_Dir(0.0, 0.0, 1.0),
                            gp_Dir(1.0, 0.0, 0.0));
        flat.uMin = 0.0; flat.uMax = 300.0;
        flat.vMin = 0.0; flat.vMax = 18.0;
        flat.thicknessAMm = 18.0;
        flat.thicknessBMm = 18.0;

        const double tiltAngle = 37.0 * (4.0 * std::atan(1.0)) / 180.0;  // not 45/90
        gp_Trsf tilt;
        tilt.SetRotation(gp_Ax1(flat.frame.Location(), flat.frame.XDirection()), tiltAngle);
        const gp_Ax3 obliqueFrame = flat.frame.Transformed(tilt);
        check(std::fabs(flat.frame.XDirection().Dot(flat.frame.Direction())) < 1.0e-9,
              "sanity: the validity tilt axis is in-plane, not the normal");
        check(obliqueFrame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0)) < 1.0 - 1.0e-6,
              "and it genuinely moves the plane's own normal off world Z");

        Joinery::Contact oblique = flat;
        oblique.frame = obliqueFrame;

        check(Joinery::validityOf(Joinery::Kind::Dowel, oblique).empty(),
              "a dowel is still valid on the tilted contact");
        check(Joinery::validityOf(Joinery::Kind::Dado, oblique).empty(),
              "so is a dado");
        check(!Joinery::validityOf(Joinery::Kind::HalfLap, oblique).empty(),
              "and a half-lap is still refused - crossing has nothing to do "
              "with which way the joint is tilted");
        check(Joinery::validityOf(Joinery::Kind::HalfLap, oblique) ==
                  Joinery::validityOf(Joinery::Kind::HalfLap, flat),
              "and the refusal reads exactly the same on both frames, not "
              "merely both non-empty");

        Joinery::Contact obliqueCross = oblique;
        obliqueCross.type = Joinery::Contact::Type::Overlap;
        check(Joinery::validityOf(Joinery::Kind::HalfLap, obliqueCross).empty(),
              "a tilted crossing still takes a half-lap");
        check(!Joinery::validityOf(Joinery::Kind::Dowel, obliqueCross).empty(),
              "and still refuses a row of dowels");

        // A too-small contact on the same tilted frame refuses for the same
        // reason as on the flat one - the size check has to survive the tilt
        // too, not only the ordinary-size case above.
        Joinery::Contact obliqueTiny = oblique;
        obliqueTiny.uMax = 12.0;
        check(Joinery::validityOf(Joinery::Kind::Dowel, obliqueTiny) ==
                  "the contact is too small for a joint",
              "and a too-small tilted contact still names the small reason");

        // The whole offered set, not merely a handful of spot checks: a joint
        // that is possible must not become impossible because the furniture
        // was rotated, and this is the assertion that would catch a mutation
        // reading contact.frame anywhere in validityOf or validKindsFor.
        const std::vector<Joinery::Kind> offeredFlat = Joinery::validKindsFor(flat);
        const std::vector<Joinery::Kind> offeredOblique = Joinery::validKindsFor(oblique);
        check(offeredFlat.size() == offeredOblique.size() && !offeredFlat.empty(),
              "the same number of kinds are offered whichever way the joint "
              "is tilted, and it is a real, non-empty set");
        check(offeredFlat == offeredOblique,
              "and it is the identical set of kinds, in the identical order");
    }

    // --- laying fasteners out along the contact -----------------------
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        c.uMin = 0.0; c.uMax = 300.0;    // along the joint
        c.vMin = 0.0; c.vMax = 18.0;     // across the board's thickness
        c.thicknessAMm = 18.0;
        c.thicknessBMm = 18.0;

        Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        p.count = 3;
        p.endMarginMm = 40.0;
        // 7.5, deliberately NOT Parameters' own 9.0 default (which is also
        // what defaultsFor gives an 18 mm board): an expected value equal to
        // the struct default cannot tell a real passthrough from a silent
        // substitution of the default, which is the trap that cost Task 1 two
        // fix rounds.
        p.insetMm = 7.5;

        const std::vector<Joinery::Item> items =
            Joinery::layout(Joinery::Kind::Dowel, p, c, {});
        check(items.size() == 3, "three dowels means three items");
        if (items.size() == 3) {
            // 40 mm in from each end, the rest evenly between.
            checkNear(items[0].u, 40.0, 1.0e-6, "the first sits at the end margin");
            checkNear(items[2].u, 260.0, 1.0e-6, "the last mirrors it at the far end");
            checkNear(items[1].u, 150.0, 1.0e-6, "and the middle one is centred");
            checkNear(items[1].v, 7.5, 1.0e-6, "all of them inset 7.5 mm across");
            checkNear(items[0].sizeMm, p.sizeMm, 1.0e-9, "each carries its own size");
            checkNear(items[0].centre.X(), 40.0, 1.0e-6,
                      "and a world position derived from the contact frame");
        }

        // One item is centred rather than jammed against the margin.
        Joinery::Parameters single = p;
        single.count = 1;
        const std::vector<Joinery::Item> one =
            Joinery::layout(Joinery::Kind::Dowel, single, c, {});
        check(one.size() == 1 && std::fabs(one[0].u - 150.0) < 1.0e-6,
              "a single fastener is centred on the joint");

        // A margin wider than the joint cannot push items past each other.
        Joinery::Parameters silly = p;
        silly.endMarginMm = 400.0;
        const std::vector<Joinery::Item> squashed =
            Joinery::layout(Joinery::Kind::Dowel, silly, c, {});
        check(squashed.size() == 3, "an over-wide margin still lays out its items");
        check(squashed[0].u >= c.uMin - 1.0e-9 && squashed[2].u <= c.uMax + 1.0e-9,
              "and keeps every one of them inside the joint");

        // An adjustment moves ONE item, in the contact's own coordinates.
        const std::vector<Joinery::Adjustment> moved = {{1, 12.0, 0.0}};
        const std::vector<Joinery::Item> nudged =
            Joinery::layout(Joinery::Kind::Dowel, p, c, moved);
        checkNear(nudged[1].u, 162.0, 1.0e-6, "an adjusted item moves by its own delta");
        checkNear(nudged[0].u, 40.0, 1.0e-6, "and its neighbours do not move with it");

        // --- an OBLIQUE frame: the layout arithmetic must not care -----
        // Tilt the whole frame about its own U axis - which is NOT the
        // frame's normal - so the plane itself tips out of the world XY
        // plane and the rectangle spanned by (X, Y) is genuinely oblique in
        // 3D, not merely spun about its own normal (the mistake three
        // separate Task 2 reviews caught: rotating about the normal alone
        // leaves the plane's orientation untouched and proves nothing about
        // a frame used in the wrong space).
        const gp_Ax3 flatFrame(gp_Pnt(5.0, -3.0, 2.0), gp_Dir(0.0, 0.0, 1.0),
                               gp_Dir(1.0, 0.0, 0.0));
        const double tiltAngle = 37.0 * (4.0 * std::atan(1.0)) / 180.0;  // not a multiple of 45/90
        gp_Trsf tilt;
        tilt.SetRotation(gp_Ax1(flatFrame.Location(), flatFrame.XDirection()), tiltAngle);
        const gp_Ax3 obliqueFrame = flatFrame.Transformed(tilt);
        // The rotation axis is the frame's own X (in-plane), not its Z
        // (normal) - so this genuinely exercises "not the frame's own
        // normal" rather than merely renaming the same rotation.
        check(std::fabs(flatFrame.XDirection().Dot(flatFrame.Direction())) < 1.0e-9,
              "sanity: the tilt axis really is in-plane, not the normal");
        check(obliqueFrame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0)) < 1.0 - 1.0e-6,
              "the tilt genuinely moves the plane's own normal off world Z");

        Joinery::Contact oblique = c;
        oblique.frame = obliqueFrame;
        const std::vector<Joinery::Item> obliqueItems =
            Joinery::layout(Joinery::Kind::Dowel, p, oblique, {});
        check(obliqueItems.size() == 3, "the oblique contact still lays out three items");
        if (obliqueItems.size() == 3) {
            // The LOCAL (u, v) arithmetic must be identical to the
            // axis-aligned case - proving layout() never reads the frame's
            // orientation to decide where along the run an item sits.
            checkNear(obliqueItems[0].u, items[0].u, 1.0e-9,
                      "the oblique frame's first item sits at the same u as the flat one");
            checkNear(obliqueItems[1].u, items[1].u, 1.0e-9,
                      "and the middle one too - local layout does not see the frame");
            checkNear(obliqueItems[2].u, items[2].u, 1.0e-9, "and the last");
            checkNear(obliqueItems[1].v, items[1].v, 1.0e-9,
                      "the inset across the joint is unchanged too");

            // The DERIVED world position must be correct for the tilted
            // frame - computed here from the frame's own raw components,
            // independently of Contact::at(), so a layout() bug that calls
            // at() with the wrong u/v (or skips deriving centre at all)
            // cannot hide behind at()'s own already-tested arithmetic.
            const gp_XYZ expectedMid = obliqueFrame.Location().XYZ() +
                                       obliqueFrame.XDirection().XYZ() * obliqueItems[1].u +
                                       obliqueFrame.YDirection().XYZ() * obliqueItems[1].v;
            checkPnt(obliqueItems[1].centre, gp_Pnt(expectedMid), 1.0e-6,
                     "the middle item's world position matches the tilted frame");
            // And it must differ from the flat frame's answer at the same
            // (u, v) - otherwise the frame was never actually consulted.
            check(obliqueItems[1].centre.Distance(items[1].centre) > 1.0,
                  "and it is genuinely a different point than the flat frame gave");
            checkDir(obliqueItems[1].axis, obliqueFrame.Direction(),
                     "the item's axis follows the tilted contact normal");
        }
    }

    // --- housings and interlocks are regions, not points --------------
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        c.uMin = 0.0; c.uMax = 300.0;
        c.vMin = 0.0; c.vMax = 18.0;
        c.thicknessAMm = 18.0;
        c.thicknessBMm = 18.0;

        // A dado: ONE channel, as wide as the housed piece, running the
        // whole way across unless it is stopped.
        Joinery::Parameters dado = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        const std::vector<Joinery::Item> channel =
            Joinery::layout(Joinery::Kind::Dado, dado, c, {});
        check(channel.size() == 1, "a dado is a single channel");
        if (channel.size() == 1) {
            checkNear(channel[0].spanUMm, 300.0, 1.0e-6,
                      "running the full length of the contact");
            checkNear(channel[0].spanVMm, 18.0, 1.0e-6,
                      "and as wide as the piece it houses");
            checkNear(channel[0].depthAMm, 6.0, 1.0e-6, "cut a third deep into the host");
        }

        // Stopped: short of the far end by its stop distance.
        Joinery::Parameters stopped = dado;
        stopped.stopped = true;
        stopped.stopMm = 10.0;
        const std::vector<Joinery::Item> blind =
            Joinery::layout(Joinery::Kind::Dado, stopped, c, {});
        check(blind.size() == 1, "a stopped dado is still a single channel");
        if (blind.size() == 1) {
            checkNear(blind[0].spanUMm, 290.0, 1.0e-6, "a stopped dado runs 10 mm short");
        }

        // A tenon: one block, its own thickness and length.
        Joinery::Parameters mt =
            Joinery::defaultsFor(Joinery::Kind::MortiseTenon, 18.0);
        const std::vector<Joinery::Item> tenon =
            Joinery::layout(Joinery::Kind::MortiseTenon, mt, c, {});
        check(tenon.size() == 1, "a mortise and tenon is one interlock");
        if (tenon.size() == 1) {
            checkNear(tenon[0].spanVMm, mt.thicknessMm, 1.0e-6,
                      "the tenon is as thick as its parameter says");
            check(tenon[0].spanUMm > 0.0 && tenon[0].spanUMm < 300.0,
                  "and narrower than the joint, leaving shoulders");
            checkNear(tenon[0].depthAMm, mt.depthAMm, 1.0e-6,
                      "with the mortise cut to match");
        }
    }

    // --- housing/interlock at a thickness that collides with no struct
    // default -----------------------------------------------------------
    // The block above uses an 18 mm board, where a dado's widthMm (18.0)
    // and a tenon's thicknessMm (6.0) both happen to equal Parameters' own
    // struct defaults (`widthMm = 18.0`, `thicknessMm = 6.0`) - so a
    // mutation that ignored params.widthMm/thicknessMm outright and read
    // the untouched struct default instead would still pass every
    // assertion above. 24 mm shares no field value with either struct
    // default, so this block is what actually pins that housingRegion and
    // interlockRegion read their OWN parameters rather than coincide with
    // them - the same discipline the file's earlier "at 24 mm" blocks
    // already apply to defaultsFor() itself.
    {
        // vMax is 40 mm, not 18 - deliberately wider than the 24 mm width
        // this block reads, so a WIDTH assertion here is not entangled with
        // fix round 1's width clamp (which now bounds width to the ACROSS
        // extent): this block exists to pin that params.widthMm/thicknessMm
        // are read at all, and the clamp itself has its own dedicated block
        // below with a deliberately small across extent.
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        c.uMin = 0.0; c.uMax = 300.0;
        c.vMin = 0.0; c.vMax = 40.0;
        c.thicknessAMm = 24.0;
        c.thicknessBMm = 24.0;

        const Joinery::Parameters dado24 = Joinery::defaultsFor(Joinery::Kind::Dado, 24.0);
        const std::vector<Joinery::Item> channel24 =
            Joinery::layout(Joinery::Kind::Dado, dado24, c, {});
        check(channel24.size() == 1, "a dado at 24 mm is still one channel");
        if (channel24.size() == 1) {
            checkNear(channel24[0].spanVMm, 24.0, 1.0e-6,
                      "reads the housed piece's OWN 24 mm width, not the 18 mm "
                      "struct default");
            checkNear(channel24[0].sizeMm, 24.0, 1.0e-6,
                      "and carries that width in sizeMm too");
            checkNear(channel24[0].depthAMm, 8.0, 1.0e-6,
                      "and its own depth - a third of a 24 mm host");
            checkNear(channel24[0].depthBMm, 0.0, 1.0e-9, "a housing has no second depth");
            checkNear(channel24[0].u, 150.0, 1.0e-6,
                      "centred on the full run when unstopped");
            checkNear(channel24[0].v, 20.0, 1.0e-6, "and centred across the joint");
            checkPnt(channel24[0].centre, gp_Pnt(150.0, 20.0, 0.0), 1.0e-6,
                     "with a world centre derived from the contact frame");
        }

        // Position/anchoring for a stopped dado: it starts at uMin and stops
        // short of the far end, so its centre shifts toward the START of the
        // run rather than staying at the full joint's own midpoint - the
        // formula anchors the shrunk run at uMin, it does not keep the
        // original centre and merely shrink the span around it.
        Joinery::Parameters stopped24 = dado24;
        stopped24.stopped = true;
        stopped24.stopMm = 10.0;
        const std::vector<Joinery::Item> blind24 =
            Joinery::layout(Joinery::Kind::Dado, stopped24, c, {});
        check(blind24.size() == 1, "a stopped dado is still one channel");
        if (blind24.size() == 1) {
            checkNear(blind24[0].spanUMm, 290.0, 1.0e-6, "290 mm of the 300 mm run");
            checkNear(blind24[0].u, 145.0, 1.0e-6,
                      "centred on the run it actually occupies, anchored at the "
                      "start - not on the full joint's own midpoint (150)");
        }

        const Joinery::Parameters mt24 =
            Joinery::defaultsFor(Joinery::Kind::MortiseTenon, 24.0);
        const std::vector<Joinery::Item> tenon24 =
            Joinery::layout(Joinery::Kind::MortiseTenon, mt24, c, {});
        check(tenon24.size() == 1, "a tenon at 24 mm is still one interlock");
        if (tenon24.size() == 1) {
            checkNear(tenon24[0].spanVMm, 8.0, 1.0e-6,
                      "reads its own 8 mm thickness, not the 6 mm struct default");
            checkNear(tenon24[0].sizeMm, 8.0, 1.0e-6, "and carries it in sizeMm too");
            checkNear(tenon24[0].spanUMm, 210.0, 1.0e-6,
                      "leaves a 15% shoulder at each end of the 300 mm run "
                      "(300 - 2*45)");
            checkNear(tenon24[0].depthAMm, 38.0, 1.0e-6,
                      "with the mortise cut to its own 38 mm depth");
            checkNear(tenon24[0].depthBMm, mt24.lengthMm, 1.0e-6,
                      "carrying the tenon's own length");
            checkNear(tenon24[0].u, 150.0, 1.0e-6,
                      "centred on the joint - unlike a stopped housing, an "
                      "interlock is never anchored to one end");
            checkNear(tenon24[0].v, 20.0, 1.0e-6, "and across it too");
        }

        // A half-lap has NO shoulder at all - it fills the whole overlap
        // outright, unlike a tenon's inset shoulders. Untested by the block
        // above, whose only interlock kind is MortiseTenon, so the
        // shoulder-is-zero branch of interlockRegion's ternary had no
        // coverage at all.
        const Joinery::Parameters hl24 = Joinery::defaultsFor(Joinery::Kind::HalfLap, 24.0);
        const std::vector<Joinery::Item> lap24 =
            Joinery::layout(Joinery::Kind::HalfLap, hl24, c, {});
        check(lap24.size() == 1, "a half-lap is one interlock too");
        if (lap24.size() == 1) {
            checkNear(lap24[0].spanUMm, 300.0, 1.0e-6,
                      "a half-lap fills the whole run - no shoulder, unlike a tenon's 210");
            checkNear(lap24[0].spanVMm, 8.0, 1.0e-6, "as thick as its own parameter says");
        }
    }

    // --- housing edge cases: width and stop must stay within the joint -----
    // Fix round 1 findings: housingRegion's width clamp bounded the wrong
    // dimension (the ALONG run rather than the ACROSS extent a channel's
    // width is actually measured against), and stopMm had no clamp of its
    // own at either end, so a negative stop inflated the span past the
    // joint and an over-large one drove it negative before std::max ever
    // saw it.
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        c.uMin = 0.0; c.uMax = 300.0;   // the run
        c.vMin = 0.0; c.vMax = 18.0;    // the across extent - small on purpose
        c.thicknessAMm = 18.0;
        c.thicknessBMm = 18.0;

        // An over-wide widthMm must be capped to the ACROSS extent (18 mm),
        // not the ALONG run (300 mm). Measured before the fix: a 250 mm
        // width passed through uncapped, describing a dado 13.9x wider than
        // the joint it crosses.
        Joinery::Parameters wide = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        wide.widthMm = 250.0;
        const std::vector<Joinery::Item> overWide =
            Joinery::layout(Joinery::Kind::Dado, wide, c, {});
        check(overWide.size() == 1, "an over-wide dado is still one channel");
        if (overWide.size() == 1) {
            checkNear(overWide[0].spanVMm, 18.0, 1.0e-6,
                      "a 250 mm width is capped to the 18 mm the joint is actually "
                      "across, not the 300 mm run");
            checkNear(overWide[0].sizeMm, 18.0, 1.0e-6, "and sizeMm carries the same cap");
        }

        // A NEGATIVE stop must not lengthen the channel past the joint it
        // crosses. Measured before the fix: -10 mm yielded a 310 mm span on
        // this 300 mm contact.
        Joinery::Parameters negStop = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        negStop.stopped = true;
        negStop.stopMm = -10.0;
        const std::vector<Joinery::Item> negative =
            Joinery::layout(Joinery::Kind::Dado, negStop, c, {});
        check(negative.size() == 1, "a negative-stop dado is still one channel");
        if (negative.size() == 1) {
            checkNear(negative[0].spanUMm, 300.0, 1.0e-6,
                      "a negative stop clamps to 0 - the span stays the full "
                      "300 mm run, never 310");
            check(negative[0].spanUMm <= c.runLength() + 1.0e-9,
                  "and never exceeds the contact it is cut into");
        }

        // A stop AT OR PAST the whole run must floor the span at zero, not
        // drive it negative.
        Joinery::Parameters overStop = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        overStop.stopped = true;
        overStop.stopMm = 400.0;
        const std::vector<Joinery::Item> overrun =
            Joinery::layout(Joinery::Kind::Dado, overStop, c, {});
        check(overrun.size() == 1, "an over-stopped dado is still one channel");
        if (overrun.size() == 1) {
            checkNear(overrun[0].spanUMm, 0.0, 1.0e-9,
                      "a 400 mm stop on a 300 mm run floors the span at zero, "
                      "not -100");
            check(overrun[0].spanUMm >= 0.0, "and it is never negative");
        }

        // stopped == false must ignore stopMm entirely, however large or
        // strange the value left in that field - a blind flag left off must
        // not let a stray stopMm leak into a through housing.
        Joinery::Parameters ignored = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        ignored.stopped = false;
        ignored.stopMm = 999.0;
        const std::vector<Joinery::Item> through =
            Joinery::layout(Joinery::Kind::Dado, ignored, c, {});
        check(through.size() == 1, "an unstopped dado is still one channel");
        if (through.size() == 1) {
            checkNear(through[0].spanUMm, 300.0, 1.0e-6,
                      "stopped == false ignores stopMm outright - still the full "
                      "300 mm run");
        }
    }

    // --- an OBLIQUE frame: housing and interlock arithmetic must not care --
    // Same requirement as the fastener row's own oblique test above, and for
    // the same reason: Task 2 lost four fix rounds to an oriented quantity
    // measured or built in world-axis-aligned terms, looking right every
    // time and measuring wrong every time, and a Task 3 review proved a
    // rotation about the frame's own NORMAL alone cannot catch that class of
    // bug - it leaves the region's edges lined up with whatever axes gp_Ax3
    // picked, so it tests an oblique normal and never an oblique rectangle.
    // This tilts about the frame's own X (in-plane, not the normal), so the
    // rectangle itself tips out of the world plane, and asserts BOTH halves:
    // the LOCAL (u, v, span) arithmetic is unchanged by the tilt, and the
    // DERIVED world centre and axis are correct for the tilted frame.
    {
        Joinery::Contact flat;
        flat.type = Joinery::Contact::Type::Face;
        flat.frame = gp_Ax3(gp_Pnt(5.0, -3.0, 2.0), gp_Dir(0.0, 0.0, 1.0),
                            gp_Dir(1.0, 0.0, 0.0));
        flat.uMin = 0.0; flat.uMax = 300.0;
        flat.vMin = 0.0; flat.vMax = 18.0;
        flat.thicknessAMm = 18.0;
        flat.thicknessBMm = 18.0;

        const Joinery::Parameters dado = Joinery::defaultsFor(Joinery::Kind::Dado, 18.0);
        const std::vector<Joinery::Item> flatChannel =
            Joinery::layout(Joinery::Kind::Dado, dado, flat, {});
        const Joinery::Parameters mt = Joinery::defaultsFor(Joinery::Kind::MortiseTenon, 18.0);
        const std::vector<Joinery::Item> flatTenon =
            Joinery::layout(Joinery::Kind::MortiseTenon, mt, flat, {});

        const double tiltAngle = 37.0 * (4.0 * std::atan(1.0)) / 180.0;  // not 45/90
        gp_Trsf tilt;
        tilt.SetRotation(gp_Ax1(flat.frame.Location(), flat.frame.XDirection()), tiltAngle);
        const gp_Ax3 obliqueFrame = flat.frame.Transformed(tilt);
        // The rotation axis is the frame's own X (in-plane), not its Z
        // (normal) - so this genuinely exercises "not the frame's own
        // normal" rather than merely renaming the same rotation.
        check(std::fabs(flat.frame.XDirection().Dot(flat.frame.Direction())) < 1.0e-9,
              "sanity: the housing/interlock tilt axis is in-plane, not the normal");
        check(obliqueFrame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0)) < 1.0 - 1.0e-6,
              "and it genuinely moves the plane's own normal off world Z");

        Joinery::Contact oblique = flat;
        oblique.frame = obliqueFrame;

        const std::vector<Joinery::Item> obliqueChannel =
            Joinery::layout(Joinery::Kind::Dado, dado, oblique, {});
        check(obliqueChannel.size() == 1, "the oblique dado is still a single channel");
        if (obliqueChannel.size() == 1 && flatChannel.size() == 1) {
            checkNear(obliqueChannel[0].spanUMm, flatChannel[0].spanUMm, 1.0e-9,
                      "span U is identical on the tilted frame - local arithmetic "
                      "never reads the frame's orientation");
            checkNear(obliqueChannel[0].spanVMm, flatChannel[0].spanVMm, 1.0e-9,
                      "and so is span V");
            checkNear(obliqueChannel[0].u, flatChannel[0].u, 1.0e-9,
                      "and its local u position");
            checkNear(obliqueChannel[0].v, flatChannel[0].v, 1.0e-9,
                      "and its local v position");

            // Computed here from the tilted frame's own raw components,
            // independently of Contact::at(), so a layout() bug that calls
            // at() with the wrong u/v (or skips deriving centre at all)
            // cannot hide behind at()'s own already-tested arithmetic.
            const gp_XYZ expectedCentre =
                obliqueFrame.Location().XYZ() +
                obliqueFrame.XDirection().XYZ() * obliqueChannel[0].u +
                obliqueFrame.YDirection().XYZ() * obliqueChannel[0].v;
            checkPnt(obliqueChannel[0].centre, gp_Pnt(expectedCentre), 1.0e-6,
                     "the channel's world centre matches the tilted frame");
            check(obliqueChannel[0].centre.Distance(flatChannel[0].centre) > 1.0,
                  "and is genuinely a different point than the flat frame gave");
            checkDir(obliqueChannel[0].axis, obliqueFrame.Direction(),
                     "the channel's axis follows the tilted contact normal");
        }

        const std::vector<Joinery::Item> obliqueTenon =
            Joinery::layout(Joinery::Kind::MortiseTenon, mt, oblique, {});
        check(obliqueTenon.size() == 1, "the oblique tenon is still one interlock");
        if (obliqueTenon.size() == 1 && flatTenon.size() == 1) {
            checkNear(obliqueTenon[0].spanUMm, flatTenon[0].spanUMm, 1.0e-9,
                      "the tenon's span U is unchanged by the tilt");
            checkNear(obliqueTenon[0].spanVMm, flatTenon[0].spanVMm, 1.0e-9,
                      "and its span V");
            checkNear(obliqueTenon[0].u, flatTenon[0].u, 1.0e-9, "and its local u position");
            checkNear(obliqueTenon[0].v, flatTenon[0].v, 1.0e-9, "and its local v position");

            const gp_XYZ expectedTenonCentre =
                obliqueFrame.Location().XYZ() +
                obliqueFrame.XDirection().XYZ() * obliqueTenon[0].u +
                obliqueFrame.YDirection().XYZ() * obliqueTenon[0].v;
            checkPnt(obliqueTenon[0].centre, gp_Pnt(expectedTenonCentre), 1.0e-6,
                     "the tenon's world centre matches the tilted frame");
            check(obliqueTenon[0].centre.Distance(flatTenon[0].centre) > 1.0,
                  "and is genuinely a different point than the flat frame gave");
            checkDir(obliqueTenon[0].axis, obliqueFrame.Direction(),
                     "the tenon's axis follows the tilted contact normal");
        }
    }

    // --- the readout: what you write on the wood ----------------------
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        // A contact whose long axis runs along world +Y ("from the front").
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 400.0), gp_Dir(1.0, 0.0, 0.0),
                         gp_Dir(0.0, 1.0, 0.0));
        c.uMin = 0.0; c.uMax = 300.0;
        c.vMin = 0.0; c.vMax = 18.0;
        c.thicknessAMm = 18.0;
        c.thicknessBMm = 18.0;

        Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        p.count = 3;
        p.endMarginMm = 40.0;
        p.insetMm = 7.5;   // not the 9.0 struct default - see the layout block
        const std::vector<Joinery::Item> items =
            Joinery::layout(Joinery::Kind::Dowel, p, c, {});
        const Joinery::Readout r = Joinery::readout(Joinery::Kind::Dowel, p, c, items);

        check(r.alongMm.size() == 3, "one distance per item");
        // Guarded, not indexed on faith: an unguarded r.alongMm[0] here would
        // turn a layout regression into a silent SIGSEGV rather than a
        // reported failure - exactly the Task 4 trap CLAUDE.md warns against.
        if (r.alongMm.size() == 3) {
            checkNear(r.alongMm[0], 40.0, 1.0e-6, "measured from the reference edge");
            checkNear(r.alongMm[1], 150.0, 1.0e-6, "the second at the middle");
            checkNear(r.alongMm[2], 260.0, 1.0e-6, "the third at the far margin");
        }
        checkNear(r.insetMm, 7.5, 1.0e-6, "with the inset across the face");
        checkNear(r.depthAMm, p.depthAMm, 1.0e-9, "and the drill depth for each side");
        check(!r.referenceEdgeA.empty(), "the edge you measure from is NAMED");
        check(r.referenceEdgeA == "front" || r.referenceEdgeA == "back" ||
                  r.referenceEdgeA == "left" || r.referenceEdgeA == "right" ||
                  r.referenceEdgeA == "top" || r.referenceEdgeA == "bottom",
              "by a word a person can find on the wood (" + r.referenceEdgeA + ")");
        check(r.referenceEdgeB == r.referenceEdgeA,
              "and bodyB's own reference edge is carried too, not left empty");
    }

    // --- the readout distinguishes families: depthB and width -----------
    // The block above only exercises a Fastener kind, where depthBMm passes
    // params.depthBMm through and widthMm falls back to thicknessMm - both
    // branches of readout()'s two ternaries untested by that block alone,
    // since a Housing kind takes the OTHER branch of each. 24 mm shares no
    // field value with Parameters' struct defaults, so a mutation that
    // swapped either ternary's branches cannot hide behind a coincidental
    // default matching what the mutation happens to produce anyway.
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0),
                         gp_Dir(1.0, 0.0, 0.0));
        c.uMin = 0.0; c.uMax = 300.0;
        c.vMin = 0.0; c.vMax = 40.0;
        c.thicknessAMm = 24.0;
        c.thicknessBMm = 24.0;

        const Joinery::Parameters dowel24 = Joinery::defaultsFor(Joinery::Kind::Dowel, 24.0);
        const std::vector<Joinery::Item> dowelItems =
            Joinery::layout(Joinery::Kind::Dowel, dowel24, c, {});
        const Joinery::Readout dowelReadout =
            Joinery::readout(Joinery::Kind::Dowel, dowel24, c, dowelItems);
        checkNear(dowelReadout.depthBMm, dowel24.depthBMm, 1.0e-9,
                  "a fastener's readout carries the far-side drill depth too");
        checkNear(dowelReadout.widthMm, dowel24.thicknessMm, 1.0e-9,
                  "and a fastener's width falls back to the interlock/fastener "
                  "thickness field, not the housing width");

        // A stray, deliberately non-zero depthBMm left sitting in a Housing's
        // own params - defaultsFor() never sets one, but readout() must not
        // simply pass depthBMm through regardless of family. A Housing kind
        // whose own depthBMm happened to be 0.0 already (the ordinary case)
        // could not catch a mutation that deleted the family check outright;
        // this stray 99.0 is what actually proves the check runs rather than
        // reading a coincidentally-zero field.
        Joinery::Parameters dado24 = Joinery::defaultsFor(Joinery::Kind::Dado, 24.0);
        dado24.depthBMm = 99.0;
        const std::vector<Joinery::Item> dadoItems =
            Joinery::layout(Joinery::Kind::Dado, dado24, c, {});
        const Joinery::Readout dadoReadout =
            Joinery::readout(Joinery::Kind::Dado, dado24, c, dadoItems);
        checkNear(dadoReadout.depthBMm, 0.0, 1.0e-9,
                  "a housing's readout has no second depth, even if one is left "
                  "sitting in params");
        checkNear(dadoReadout.widthMm, dado24.widthMm, 1.0e-9,
                  "and a housing's width is its own channel width, not the "
                  "fastener/interlock thickness field");
    }

    // --- reference-edge naming: a dominant axis wins, an ambiguous one is
    // named honestly instead of guessed -----------------------------------
    // On a rotated board no face is honestly "front" any more, and a
    // confidently wrong name here is worse than anywhere else in this
    // feature - the reader measures from this word with a pencil and a
    // square. The rule: a direction 0.95 along an axis (about 18 degrees off
    // it) still reads as that axis's word; one split evenly between two axes
    // (0.7071/0.7071 each - a board tilted diagonally) does not, and reports
    // an honest phrase instead of a coin-flip name.
    {
        Joinery::Contact c;
        c.type = Joinery::Contact::Type::Face;
        c.uMin = 0.0; c.uMax = 300.0;   // the run - u dominates so runsAlongU()
        c.vMin = 0.0; c.vMax = 18.0;
        c.thicknessAMm = 18.0;
        c.thicknessBMm = 18.0;
        const Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);

        // 0.95 along Y, the remainder along Z - clearly one axis's word.
        const double rem95 = std::sqrt(1.0 - 0.95 * 0.95);
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0),
                         gp_Dir(0.0, 0.95, -rem95));
        const std::vector<Joinery::Item> items95 =
            Joinery::layout(Joinery::Kind::Dowel, p, c, {});
        const Joinery::Readout r95 = Joinery::readout(Joinery::Kind::Dowel, p, c, items95);
        check(r95.referenceEdgeA == "front",
              "0.95 along an axis still names that axis's edge (" +
                  r95.referenceEdgeA + ")");

        // Exactly 45 degrees between two axes - neither wins, and the
        // readout must say so rather than pick one.
        const double half = std::sqrt(0.5);
        c.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0),
                         gp_Dir(0.0, half, -half));
        const std::vector<Joinery::Item> items45 =
            Joinery::layout(Joinery::Kind::Dowel, p, c, {});
        const Joinery::Readout r45 = Joinery::readout(Joinery::Kind::Dowel, p, c, items45);
        check(r45.referenceEdgeA != "front" && r45.referenceEdgeA != "back" &&
                  r45.referenceEdgeA != "left" && r45.referenceEdgeA != "right" &&
                  r45.referenceEdgeA != "top" && r45.referenceEdgeA != "bottom",
              "at 45 degrees between two axes, no single named edge is honest (" +
                  r45.referenceEdgeA + ")");
        check(!r45.referenceEdgeA.empty(),
              "the ambiguous case still says SOMETHING, not an empty string");
    }

    // --- the readout on an OBLIQUE frame: distances don't move, the edge
    // name tells the truth -------------------------------------------------
    // Same requirement as every earlier oblique-frame test in this file, and
    // it matters more here: edgeName() reads a direction taken straight from
    // the contact frame, so a frame tilted in 3D is not a formality for the
    // readout, it is the exact case the naming logic has to survive. Tilts
    // about the frame's own X - IN-PLANE, not the normal - so the rectangle
    // itself tips out of the world plane (rotating about the normal alone
    // leaves the region's edges exactly where gp_Ax3 already put them and
    // proves nothing, per the Task 2 reviews recorded earlier in this file).
    {
        Joinery::Contact flat;
        flat.type = Joinery::Contact::Type::Face;
        flat.frame = gp_Ax3(gp_Pnt(5.0, -3.0, 2.0), gp_Dir(0.0, 0.0, 1.0),
                            gp_Dir(1.0, 0.0, 0.0));
        // The run is along V here, not U - V is the axis a tilt about the
        // frame's own X (in-plane, orthogonal to V) actually rotates. A run
        // along U would stay put under this exact tilt (X IS the rotation
        // axis), and the naming half of this test would exercise nothing.
        flat.uMin = 0.0; flat.uMax = 18.0;
        flat.vMin = 0.0; flat.vMax = 300.0;
        flat.thicknessAMm = 18.0;
        flat.thicknessBMm = 18.0;

        Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        p.count = 3;
        p.endMarginMm = 40.0;
        p.insetMm = 9.0;

        const std::vector<Joinery::Item> flatItems =
            Joinery::layout(Joinery::Kind::Dowel, p, flat, {});
        const Joinery::Readout flatReadout =
            Joinery::readout(Joinery::Kind::Dowel, p, flat, flatItems);
        check(flatReadout.referenceEdgeA == "front",
              "on the flat frame the run's low end is honestly the front edge (" +
                  flatReadout.referenceEdgeA + ")");
        // Pinned against the real expected numbers, not merely "whatever the
        // flat frame produced" - a mutation reading the wrong local
        // coordinate (item.u where the run is along v) would still agree
        // with itself on both the flat and tilted frame (neither depends on
        // the frame's orientation), so a flat-vs-oblique comparison ALONE
        // cannot catch it. Caught by mutation: deleting the alongU ternary
        // in favour of a bare item.u passed the whole suite until this
        // absolute check was added, because this contact runs along v.
        check(flatReadout.alongMm.size() == 3, "three dowels means three distances");
        if (flatReadout.alongMm.size() == 3) {
            checkNear(flatReadout.alongMm[0], 40.0, 1.0e-6,
                      "the flat frame's own first distance is the real 40 mm margin");
            checkNear(flatReadout.alongMm[1], 150.0, 1.0e-6,
                      "and its middle one the real 150 mm centre");
            checkNear(flatReadout.alongMm[2], 260.0, 1.0e-6,
                      "and its last the real 260 mm far margin");
        }

        const double tiltAngle = 37.0 * (4.0 * std::atan(1.0)) / 180.0;  // not 45/90
        gp_Trsf tilt;
        tilt.SetRotation(gp_Ax1(flat.frame.Location(), flat.frame.XDirection()), tiltAngle);
        const gp_Ax3 obliqueFrame = flat.frame.Transformed(tilt);
        check(std::fabs(flat.frame.XDirection().Dot(flat.frame.Direction())) < 1.0e-9,
              "sanity: the readout's own tilt axis is in-plane, not the normal");
        check(obliqueFrame.Direction().Dot(gp_Dir(0.0, 0.0, 1.0)) < 1.0 - 1.0e-6,
              "and it genuinely moves the plane's own normal off world Z");

        Joinery::Contact oblique = flat;
        oblique.frame = obliqueFrame;
        const std::vector<Joinery::Item> obliqueItems =
            Joinery::layout(Joinery::Kind::Dowel, p, oblique, {});
        const Joinery::Readout obliqueReadout =
            Joinery::readout(Joinery::Kind::Dowel, p, oblique, obliqueItems);

        check(obliqueReadout.alongMm.size() == flatReadout.alongMm.size(),
              "the tilted frame still reports one distance per item");
        if (obliqueReadout.alongMm.size() == flatReadout.alongMm.size() &&
            !flatReadout.alongMm.empty()) {
            for (std::size_t i = 0; i < flatReadout.alongMm.size(); ++i) {
                checkNear(obliqueReadout.alongMm[i], flatReadout.alongMm[i], 1.0e-9,
                          "along-the-run distance " + std::to_string(i) +
                              " is unchanged by a 3D tilt of the frame");
            }
        }
        checkNear(obliqueReadout.insetMm, flatReadout.insetMm, 1.0e-9,
                  "and the inset across the face is unchanged too");

        // This tilt rotates the run direction (V, orthogonal to the X
        // rotation axis) to (0, cos37, sin37) - dominant component ~0.799,
        // which clears neither this project's 45-degree ambiguous case nor
        // its 0.95 confident one. The readout must say so rather than pick
        // "front" or "top" by whichever narrowly edges out.
        check(obliqueReadout.referenceEdgeA.find("no single edge") != std::string::npos,
              "a genuinely tilted board gets an honest answer, not a guessed edge (" +
                  obliqueReadout.referenceEdgeA + ")");
        check(obliqueReadout.referenceEdgeA != flatReadout.referenceEdgeA,
              "and it is not silently reusing the flat frame's own answer");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
