// Headless oracle for the joinery maths (spec:
// docs/superpowers/specs/2026-09-10-joinery-design.md). No Qt, no GPU -
// every function under test is a pure function of shapes and numbers.
#include "Joinery.h"
#include "DocumentModel.h"

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
#include <cctype>
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

    // A half-lap from one number: half of it, for both pieces - never the
    // tenon numbers the interlock branch used to hand it (on 26 mm those were
    // a 41 mm "lap" in a 26 mm rail). 26 collides with no struct default.
    const Joinery::Parameters lap26 = Joinery::defaultsFor(Joinery::Kind::HalfLap, 26.0);
    checkNear(lap26.depthAMm, 13.0, 1.0e-9, "a half-lap removes half of a 26 mm piece A");
    checkNear(lap26.depthBMm, 13.0, 1.0e-9, "and half of piece B");
    check(lap26.depthAMm < 26.0 && lap26.depthBMm < 26.0,
          "so neither half is deeper than the board it is cut in");

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

    // --- the region-shortfall caveat: a fact, never a veto -------------
    // Fix round 1: `Contact::regionAreaMm2` and `regionShortfallCaveat()`.
    // The bounding-rectangle gap Task 6's own report identified - that
    // `uLength() * vLength()` can cover area not actually in contact for an
    // L-shaped, C-shaped or rounded region - is closed here for the ONE
    // question that has an exact answer (does the region's own real area
    // equal its bounding rectangle's), while `validityOf`/`validKindsFor`
    // are asserted UNCHANGED by it: a shortfall is a caveat, never a
    // refusal, so every earlier assertion in this file about which kinds
    // are offered must still hold once this field exists.
    {
        // The ordinary case: a real, findContact()-sourced rectangular
        // contact fills its own rectangle exactly (both sides come from
        // exact geometry - a planar-face boolean Common and a curve-aware
        // extent read - so they agree far tighter than the tolerance).
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult flush = Joinery::findContact(panel, shelf);
        check(flush.ok, "the flush rectangular contact is found, as in the earlier block");
        if (flush.ok) {
            // Pins the PLUMBING directly: this is what goes red if
            // findContact() stops writing regionAreaMm2 at all, independent
            // of regionShortfallCaveat()'s own threshold logic - the
            // sentinel is -1.0, nowhere near the true ~5400.
            checkNear(flush.contact.regionAreaMm2, 300.0 * 18.0, 1.0e-3,
                      "a plain rectangular contact's measured area matches "
                      "its own bounding rectangle almost exactly");
            check(Joinery::regionShortfallCaveat(flush.contact).empty(),
                  "and carries no shortfall caveat");
        }

        // A NON-CONVEX host (the stepped rabbet from the earlier block) is
        // still a plain RECTANGULAR contact where the board actually lands
        // - the step lives 300 mm away from the joint, not inside it - so
        // geometric complexity ELSEWHERE on the host must not trigger the
        // caveat. Distinguishes "the CONTACT is not a rectangle" from "the
        // HOST is a complicated shape", which a cruder signal (e.g. solid
        // face count) could not.
        const TopoDS_Shape slab =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 36.0, 300.0, 800.0).Shape();
        const TopoDS_Shape rabbetCut =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 0.0), 18.0, 300.0, 400.0).Shape();
        const TopoDS_Shape stepped = BRepAlgoAPI_Cut(slab, rabbetCut).Shape();
        const TopoDS_Shape onStep =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 100.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::ContactResult stepJoint = Joinery::findContact(stepped, onStep);
        check(stepJoint.ok, "the stepped-rabbet contact is found, as in the earlier block");
        if (stepJoint.ok) {
            check(Joinery::regionShortfallCaveat(stepJoint.contact).empty(),
                  "a board on a rabbeted step still reports no shortfall - "
                  "the CONTACT itself is a plain rectangle even though the "
                  "host it sits on is not");
        }

        // The L-shaped contact from the earlier block, rebuilt fresh (each
        // block in this file is self-contained): a real 11,952 mm2 region
        // inside a 382 x 300 = 114,600 mm2 bounding rectangle - the exact
        // numbers the Contact struct's own header comment documents. This
        // is the case the whole fix exists for.
        const TopoDS_Shape upstand =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 418.0), 600.0, 18.0, 364.0).Shape();
        ShapeUpgrade_UnifySameDomain unifyL(BRepAlgoAPI_Fuse(shelf, upstand).Shape(),
                                           Standard_True, Standard_True, Standard_True);
        unifyL.Build();
        const Joinery::ContactResult lJoint = Joinery::findContact(panel, unifyL.Shape());
        check(lJoint.ok, "the L-shaped contact is found, as in the earlier block");
        if (lJoint.ok) {
            checkNear(lJoint.contact.regionAreaMm2, 11952.0, 1.0e-3,
                      "the L-shaped region's own real area is measured, not "
                      "assumed to fill its rectangle");
            const std::string caveat = Joinery::regionShortfallCaveat(lJoint.contact);
            check(!caveat.empty(),
                  "and a real shortfall now surfaces as a caveat (" + caveat + ")");
            // validityOf/validKindsFor are UNCHANGED by any of this - a
            // shortfall is a fact, never a veto. Re-asserts the earlier
            // block's own promise on THIS non-rectangular contact
            // specifically, which is the case that actually matters.
            check(Joinery::validityOf(Joinery::Kind::Dowel, lJoint.contact).empty(),
                  "an L-shaped contact still offers a dowel - the caveat "
                  "does not refuse it");
            check(!Joinery::validKindsFor(lJoint.contact).empty(),
                  "and validKindsFor still offers real kinds on it");
        }

        // The C-shaped contact from the earlier block: a real 34,000 mm2
        // region (a 300 x 300 square less a 280 x 200 notch) inside a
        // 300 x 300 = 90,000 mm2 bounding rectangle - again the exact
        // numbers the struct comment documents.
        const TopoDS_Shape slabC =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 300.0).Shape();
        const TopoDS_Shape notchC =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 20.0, 450.0), 600.0, 280.0, 200.0).Shape();
        ShapeUpgrade_UnifySameDomain unifyC(BRepAlgoAPI_Cut(slabC, notchC).Shape(),
                                           Standard_True, Standard_True, Standard_True);
        unifyC.Build();
        const Joinery::ContactResult cJoint = Joinery::findContact(panel, unifyC.Shape());
        check(cJoint.ok, "the C-shaped contact is found, as in the earlier block");
        if (cJoint.ok) {
            checkNear(cJoint.contact.regionAreaMm2, 34000.0, 1.0e-3,
                      "the C-shaped region's own real area is measured too");
            check(!Joinery::regionShortfallCaveat(cJoint.contact).empty(),
                  "and it carries the caveat as well");
        }

        // An Overlap contact (two crossing rails) carries NO measured area
        // today - the decision this fix round documented rather than
        // guessed at: the only already-computed area-shaped quantity on
        // that branch is the lap SOLID's own volume, in the wrong unit
        // entirely, so nothing is written and the field stays at the
        // sentinel. Pinned here so a later change that quietly starts
        // writing a volume into this field - the exact hazard the sentinel
        // exists to catch - is caught by exact-value comparison, not
        // merely "some number came out".
        const TopoDS_Shape railA =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 400.0, 40.0, 20.0).Shape();
        const TopoDS_Shape railB =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -100.0, 0.0), 60.0, 300.0, 20.0).Shape();
        const Joinery::ContactResult crossed = Joinery::findContact(railA, railB);
        check(crossed.ok && crossed.contact.type == Joinery::Contact::Type::Overlap,
              "the crossing-rails overlap is found, as in the earlier block");
        if (crossed.ok) {
            check(crossed.contact.regionAreaMm2 == Joinery::kUnmeasuredRegionAreaMm2,
                  "an overlap contact's regionAreaMm2 is left at the exact "
                  "sentinel, not a wrong-unit number");
            check(Joinery::regionShortfallCaveat(crossed.contact).empty(),
                  "so an overlap carries no caveat either way - unmeasured, "
                  "not 'fills its rectangle'");
        }

        // A hand-built Contact left at its OWN struct default (regionAreaMm2
        // never touched) reads the same way: unmeasured, not "full". This is
        // the property the sentinel exists for - a Contact built by hand in
        // a test (as several fixtures in this file are) must not silently
        // look like a genuine rectangular measurement.
        Joinery::Contact handBuilt;
        handBuilt.type = Joinery::Contact::Type::Face;
        handBuilt.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        handBuilt.uMin = 0.0; handBuilt.uMax = 300.0;
        handBuilt.vMin = 0.0; handBuilt.vMax = 18.0;
        check(handBuilt.regionAreaMm2 == Joinery::kUnmeasuredRegionAreaMm2,
              "Contact's own default member initializer is the sentinel, "
              "not zero or a value that happens to look like a real area");
        check(Joinery::regionShortfallCaveat(handBuilt).empty(),
              "and a hand-built contact therefore carries no caveat");

        // The threshold itself, hand-built rather than found - this pins
        // the TOLERANCE the brief's report identified as the one place an
        // "arbitrary" number is actually safe (a rectangle either IS its
        // own region or it is not), not merely that some caveat appears
        // for some sufficiently-short region. A 100 x 100 mm rectangle
        // (10,000 mm2) two values apart: comfortably inside the fill
        // tolerance, and comfortably outside it - not pinned to the exact
        // boundary itself, since the tolerance constant is private to the
        // .cpp and re-deriving its literal value here would test this
        // file's own arithmetic rather than the implementation's.
        Joinery::Contact square;
        square.type = Joinery::Contact::Type::Face;
        square.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        square.uMin = 0.0; square.uMax = 100.0;
        square.vMin = 0.0; square.vMax = 100.0;

        Joinery::Contact almostFull = square;
        almostFull.regionAreaMm2 = 10000.0 * (1.0 - 5.0e-5);  // fraction 0.99995
        check(Joinery::regionShortfallCaveat(almostFull).empty(),
              "a region 0.005% short of its rectangle reads as filling it - "
              "inside the floating-point tolerance band");

        Joinery::Contact meaningfullyShort = square;
        meaningfullyShort.regionAreaMm2 = 10000.0 * (1.0 - 2.0e-4);  // fraction 0.9998
        check(!Joinery::regionShortfallCaveat(meaningfullyShort).empty(),
              "a region 0.02% short is a genuine shortfall, not noise - "
              "four times the almost-full case's own gap");

        // The caveat text itself: user-visible (Task 11/12 paint it), so it
        // is held to the same vocabulary law as validityOf's own reason
        // strings - no banned word, CASE-INSENSITIVELY per the law's own
        // wording, singular/plural written out.
        const std::string caveatText = Joinery::regionShortfallCaveat(meaningfullyShort);
        check(!caveatText.empty(), "sanity: the short case really does carry a caveat");
        std::string lowerCaveat = caveatText;
        std::transform(lowerCaveat.begin(), lowerCaveat.end(), lowerCaveat.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        for (const std::string& banned :
             {std::string("solid"), std::string("fuse"), std::string("merge"),
              std::string("bevel"), std::string("round"), std::string("flatten"),
              std::string("symmetry"), std::string("occt"), std::string("mm3"),
              std::string("(s)")}) {
            check(lowerCaveat.find(banned) == std::string::npos,
                  "the caveat text does not contain the banned word \"" + banned + "\"");
        }
        // "join" as a bare word is banned too, but "joint" is explicitly the
        // app's own word and must not false-positive this check.
        check(lowerCaveat.find("joint") != std::string::npos ||
                  lowerCaveat.find("join") == std::string::npos,
              "if \"join\" appears at all it is as part of \"joint\", the "
              "permitted noun");
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
            checkNear(lap24[0].spanVMm, 40.0, 1.0e-6,
                      "and the whole overlap ACROSS it too - a half-lap is cut over its full "
                      "footprint, not a tenon-thick 8 mm strip");
            checkNear(lap24[0].depthAMm, 12.0, 1.0e-6,
                      "removing half of a 24 mm piece A - not a tenon's 38 mm mortise");
            checkNear(lap24[0].depthBMm, 12.0, 1.0e-6,
                      "and half of piece B - not a tenon's 36 mm length");
        }
    }

    // --- defaultsForContact: each piece's OWN thickness at the joint --------
    // THE defaults placement calls. defaultsFor() only ever sees the thinner
    // piece, so it cannot say "half of EACH" or "no deeper than the host";
    // every thickness below collides with no struct default and differs from
    // its partner, so the thinner-piece answer and the per-piece one cannot
    // coincide.
    {
        // Unequal rails crossing - real geometry, so the thicknesses are what
        // findContact() measures through the lap, not numbers typed in.
        const TopoDS_Shape thinRail =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 400.0, 40.0, 22.0).Shape();
        const TopoDS_Shape deepRail =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -100.0, 0.0), 60.0, 300.0, 70.0).Shape();
        const Joinery::ContactResult lapContact = Joinery::findContact(thinRail, deepRail);
        check(lapContact.ok && lapContact.contact.type == Joinery::Contact::Type::Overlap,
              "a 22 mm rail crossing a 70 mm one is an overlap");
        if (lapContact.ok) {
            checkNear(lapContact.contact.thicknessAMm, 22.0, 1.0e-6,
                      "the thin rail carries 22 mm through the lap");
            checkNear(lapContact.contact.thicknessBMm, 70.0, 1.0e-6, "and the deep one 70");
            const Joinery::Parameters lap =
                Joinery::defaultsForContact(Joinery::Kind::HalfLap, lapContact.contact);
            checkNear(lap.depthAMm, 11.0, 1.0e-9, "a half-lap removes half of the thin rail's 22 mm");
            checkNear(lap.depthBMm, 35.0, 1.0e-9,
                      "and half of the deep rail's OWN 70 mm - not half of the thinner (11)");
            const std::vector<Joinery::Item> lapItems =
                Joinery::layout(Joinery::Kind::HalfLap, lap, lapContact.contact, {});
            check(lapItems.size() == 1, "the placed half-lap is one item");
            if (lapItems.size() == 1) {
                checkNear(lapItems[0].depthAMm, 11.0, 1.0e-9, "and the item carries A's half");
                checkNear(lapItems[0].depthBMm, 35.0, 1.0e-9, "and B's");
                checkNear(lapItems[0].spanUMm * lapItems[0].spanVMm,
                          lapContact.contact.uLength() * lapContact.contact.vLength(), 1.0e-6,
                          "over the whole 60 x 40 overlap footprint");
            }
        }

        // A mortise and tenon: the mortise is cut in A, the host. A 14 mm host
        // against a 50 mm tenon piece - the uncapped rule proposes a 27 mm
        // mortise, which comes 13 mm out of the back of the host.
        Joinery::Contact thinHost;
        thinHost.type = Joinery::Contact::Type::Face;
        thinHost.frame = gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        thinHost.uMin = 0.0; thinHost.uMax = 300.0;
        thinHost.vMin = 0.0; thinHost.vMax = 50.0;
        thinHost.thicknessAMm = 14.0;
        thinHost.thicknessBMm = 50.0;
        const Joinery::Parameters capped =
            Joinery::defaultsForContact(Joinery::Kind::MortiseTenon, thinHost);
        checkNear(capped.depthAMm, 14.0, 1.0e-9,
                  "a mortise is at most a through mortise - capped at the 14 mm host, "
                  "not the 27 mm the uncapped rule proposes");
        checkNear(capped.lengthMm, 12.0, 1.0e-9, "and the tenon stays a hair shorter than it");
        const std::vector<Joinery::Item> cappedItems =
            Joinery::layout(Joinery::Kind::MortiseTenon, capped, thinHost, {});
        check(cappedItems.size() == 1 && cappedItems[0].depthAMm <= thinHost.thicknessAMm,
              "so the placed mortise is no deeper than its host");

        // A host deeper than the rule's mortise is left alone: 64 mm against a
        // 22 mm tenon piece keeps the 35 mm mortise and 33 mm tenon.
        Joinery::Contact thickHost = thinHost;
        thickHost.thicknessAMm = 64.0;
        thickHost.thicknessBMm = 22.0;
        const Joinery::Parameters uncapped =
            Joinery::defaultsForContact(Joinery::Kind::MortiseTenon, thickHost);
        checkNear(uncapped.depthAMm, 35.0, 1.0e-9, "a host thick enough keeps the rule's mortise");
        checkNear(uncapped.lengthMm, 33.0, 1.0e-9, "and its tenon");

        // A housing: a third of the HOST deep, as wide as the HOUSED piece.
        // Two contacts, because one cannot tell both apart from "the thinner":
        // a thick host shows the depth, a thick housed piece shows the width.
        Joinery::Contact deepHost = thinHost;
        deepHost.thicknessAMm = 42.0;
        deepHost.thicknessBMm = 16.0;
        const Joinery::Parameters deepDado =
            Joinery::defaultsForContact(Joinery::Kind::Dado, deepHost);
        checkNear(deepDado.depthAMm, 14.0, 1.0e-9,
                  "a dado is a third of its 42 mm host deep - not a third of the thinner 16");
        Joinery::Contact wideHoused = thinHost;
        wideHoused.thicknessAMm = 16.0;
        wideHoused.thicknessBMm = 42.0;
        const Joinery::Parameters wideDado =
            Joinery::defaultsForContact(Joinery::Kind::Dado, wideHoused);
        checkNear(wideDado.widthMm, 42.0, 1.0e-9,
                  "and as wide as the 42 mm piece it houses - not the thinner host's 16");

        // A fastener's angle rides on its items, so what draws it never has
        // to reach back for the parameters. 22 degrees collides with nothing.
        Joinery::Parameters leaning =
            Joinery::defaultsForContact(Joinery::Kind::PocketScrew, thinHost);
        checkNear(leaning.angleDeg, 15.0, 1.0e-9, "a pocket screw still defaults to 15 degrees");
        leaning.angleDeg = 22.0;
        const std::vector<Joinery::Item> pins =
            Joinery::layout(Joinery::Kind::PocketScrew, leaning, thinHost, {});
        check(!pins.empty() && std::all_of(pins.begin(), pins.end(), [](const Joinery::Item& pin) {
                  return std::fabs(pin.angleDeg - 22.0) < 1.0e-9;
              }),
              "and every placed pocket screw carries the parameters' own 22 degrees");
        const std::vector<Joinery::Item> straight = Joinery::layout(
            Joinery::Kind::Dowel, Joinery::defaultsForContact(Joinery::Kind::Dowel, thinHost),
            thinHost, {});
        check(!straight.empty() && std::all_of(straight.begin(), straight.end(),
                                               [](const Joinery::Item& pin) {
                                                   return pin.angleDeg == 0.0;
                                               }),
              "while a dowel is driven straight");
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

    // --- joints live in the document, and die with their pieces -------
    {
        DocumentModel doc;
        const int panel = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
        const int shelf = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
        check(panel > 0 && shelf > 0, "two bodies for the joint probe");

        const Joinery::Parameters params =
            Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        const int jointId =
            doc.addJoint(Joinery::Kind::Dowel, panel, shelf, params);
        check(jointId > 0, "a joint between two real bodies is created");
        check(doc.joints().size() == 1, "and listed once");
        check(doc.addJoint(Joinery::Kind::Dowel, panel, 9999, params) == 0,
              "a joint to a body that does not exist is refused");
        check(doc.addJoint(Joinery::Kind::Dowel, panel, panel, params) == 0,
              "and so is a joint from a piece to itself");

        // Editing parameters is one call, and it sticks.
        Joinery::Parameters five = params;
        five.count = 5;
        check(doc.updateJointParameters(jointId, five), "parameters can be updated");
        check(doc.joints().front().params.count == 5, "and the new value is stored");

        // jointsOn() answers for either side.
        check(doc.jointsOn(panel).size() == 1 && doc.jointsOn(shelf).size() == 1,
              "a joint is found from either of its pieces");

        // A joint dies with either piece - and comes back with undo.
        doc.checkpoint();
        doc.removeSolid(shelf);
        check(doc.joints().empty(), "removing a piece takes its joints with it");
        doc.undo();
        check(doc.joints().size() == 1, "and one undo brings both back");
        check(doc.jointsOn(shelf).size() == 1, "still attached to the same pieces");
    }

    // --- undo/redo restore a joint's CONTENT, not merely its existence,
    // and a joint is never resurrected by an undo that reaches past its
    // own creation -----------------------------------------------------
    {
        DocumentModel doc;
        const int a = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
        check(doc.joints().empty(), "no joints exist before either body is placed");

        doc.checkpoint();   // captures: {a}, no b, no joints
        const int b = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
        check(a > 0 && b > 0, "two bodies for the undo-content probe");

        doc.checkpoint();   // captures: {a, b}, no joints
        Joinery::Parameters params = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        // A non-default value on purpose (the default count is 3) - an
        // assertion that only reaches a struct default proves nothing about
        // whether undo actually restored anything.
        params.count = 7;
        const int jointId = doc.addJoint(Joinery::Kind::Dowel, a, b, params);
        check(jointId > 0, "the joint under test is created");
        const std::vector<Joinery::Adjustment> adjustments = {
            Joinery::Adjustment{0, 12.5, -3.0}, Joinery::Adjustment{1, -6.0, 4.25}};
        check(doc.setJointAdjustments(jointId, adjustments),
              "adjustments can be attached to the joint");

        doc.checkpoint();   // captures: {a, b}, one joint (count=7, 2 adjustments)
        doc.removeSolid(b);
        check(doc.joints().empty(), "deleting b takes the joint with it, again");

        // One undo: back to "{a, b}, one joint" - content must match exactly,
        // not merely "a joint exists".
        check(doc.undo(), "first undo succeeds");
        check(doc.joints().size() == 1, "the joint is back after one undo");
        check(doc.jointsOn(b).size() == 1, "still attached to body b specifically");
        if (doc.joints().size() == 1) {
            const DocumentModel::Joint& restored = doc.joints().front();
            check(restored.params.count == 7,
                  "undo restores the joint's PARAMETERS, not a fresh default");
            check(restored.adjustments.size() == 2,
                  "undo restores the joint's ADJUSTMENTS too");
            if (restored.adjustments.size() == 2) {
                check(restored.adjustments[0].du == 12.5 && restored.adjustments[0].dv == -3.0,
                      "adjustment 0 came back exactly");
                check(restored.adjustments[1].du == -6.0 && restored.adjustments[1].dv == 4.25,
                      "adjustment 1 came back exactly");
            }
        }

        // Redo replays the deletion: the joint must die again along with b.
        check(doc.redo(), "redo succeeds");
        check(doc.joints().empty(), "redo restores the deletion - joint and body b both gone");
        check(!doc.contains(b), "b itself is gone again after redo");

        // Undo back to the joint, then PAST the checkpoint that created it -
        // the joint must not survive an undo that reaches behind its own
        // creation.
        check(doc.undo(), "undo back past the redo");
        check(doc.joints().size() == 1, "the joint is present at its own checkpoint");
        check(doc.undo(), "undo again, past the joint's own creation");
        check(doc.joints().empty(),
              "the joint is NOT resurrected by an undo landing before it existed");
        check(doc.contains(a) && doc.contains(b),
              "both bodies still exist at this earlier point - only the joint is gone");
    }

    // --- restoreFrom() carries joints - Task 7 fix round 1 -------------
    // The reviewer's reproduction: checkpoint() then restoreFrom(snapshot)
    // used to leave myJoints untouched while mySolids was replaced wholesale,
    // so a joint could survive pointing at an id the snapshot never had, or
    // - worse, since ids restart at 1 in every fresh DocumentModel - at an id
    // that now names a completely different, real body in the restored
    // document.
    {
        DocumentModel doc;
        const int oldA = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
        const int oldB = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
        check(oldA > 0 && oldB > 0, "two bodies in the document about to be restored over");

        const Joinery::Parameters oldParams = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        const int oldJoint = doc.addJoint(Joinery::Kind::Dowel, oldA, oldB, oldParams);
        check(oldJoint > 0, "a joint exists before the restore");
        check(doc.joints().size() == 1, "listed once before the restore");

        // A "version" snapshot with NO joints of its own, and (since a fresh
        // DocumentModel's ids count again from 1, same as `doc`'s did) a body
        // whose id genuinely COLLIDES with oldA's - the sharper case the
        // review named: pre-fix, doc.contains(oldA) would read true again
        // after the restore, but against an unrelated real body, not oldA.
        DocumentModel version;
        const int freshId = version.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 1.0, 1.0, 1.0).Shape());
        check(freshId == oldA,
              "sanity: the snapshot's own first id collides with the pre-restore body's id "
              "- the exact setup the id-collision hazard needs");
        check(version.joints().empty(), "the snapshot itself carries no joints");

        doc.checkpoint();   // the caller's own checkpoint - restoreFrom() takes none of its own
        doc.restoreFrom(version);

        check(doc.joints().empty(),
              "restoreFrom() clears joints when the snapshot carries none - no joint can "
              "outlive the document state it described");
        // Defensive invariant, not merely "empty": even if this ever carried
        // a joint, every one of them must reference bodies that genuinely
        // exist post-restore - guarded on size, never a blind index.
        for (const DocumentModel::Joint& j : doc.joints()) {
            check(doc.contains(j.bodyA) && doc.contains(j.bodyB),
                  "no surviving joint references a body where contains() is false");
        }

        // One undo restores the pre-restore document AND its joint together
        // - restoreFrom() sits behind the caller's own checkpoint, exactly
        // like every other commit path here.
        check(doc.undo(), "the checkpoint taken before restoreFrom is still on the stack");
        check(doc.joints().size() == 1, "undo brings the pre-restore joint back");
        check(doc.jointsOn(oldB).size() == 1, "still attached to the same two bodies");
    }

    // --- restoreFrom() installs exactly the snapshot's OWN joints -------
    {
        DocumentModel doc;
        const int stub = doc.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 5.0, 5.0, 5.0).Shape());
        check(stub > 0 && doc.joints().empty(),
              "a fresh document has one body and no joints");

        DocumentModel version;
        const int vA = version.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
        const int vB = version.addSolid(
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
        Joinery::Parameters vParams = Joinery::defaultsFor(Joinery::Kind::Domino, 18.0);
        // Non-default on purpose (Joinery::Parameters::count defaults to 3) -
        // an assertion that only reaches the struct default proves nothing.
        vParams.count = 9;
        const int vJoint = version.addJoint(Joinery::Kind::Domino, vA, vB, vParams);
        check(vJoint > 0, "the version's own joint is created");
        const std::vector<Joinery::Adjustment> vAdj = {Joinery::Adjustment{0, 2.0, -1.5}};
        check(version.setJointAdjustments(vJoint, vAdj), "and carries an adjustment");

        doc.checkpoint();
        doc.restoreFrom(version);

        check(doc.joints().size() == 1,
              "restoreFrom() installs exactly the snapshot's one joint");
        if (doc.joints().size() == 1) {
            const DocumentModel::Joint& installed = doc.joints().front();
            check(installed.kind == Joinery::Kind::Domino, "the same kind");
            check(installed.bodyA == vA && installed.bodyB == vB,
                  "the same body ids - copied VERBATIM, like the pairing map and link groups");
            check(installed.params.count == 9,
                  "the same (non-default) parameters, not a fresh default block");
            check(installed.adjustments.size() == 1, "and the same adjustment count");
            if (installed.adjustments.size() == 1) {
                check(installed.adjustments[0].du == 2.0 && installed.adjustments[0].dv == -1.5,
                      "with the exact same values");
            }
        }

        // The joint id counter must not collide with what was just
        // installed - a later addJoint() gets a genuinely fresh id.
        const int freshJoint = doc.addJoint(Joinery::Kind::Dowel, vA, vB,
                                            Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
        check(freshJoint > 0 && freshJoint != vJoint,
              "a joint created after the restore never collides with the restored one's id");
    }

    // --- derive: the whole chain, and the loud break -------------------
    {
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);

        const Joinery::Derivation d =
            Joinery::derive(Joinery::Kind::Dowel, p, {}, panel, shelf);
        check(d.ok, "a joint between touching pieces derives");
        check(d.items.size() == static_cast<std::size_t>(p.count),
              "with one item per fastener");
        check(d.readout.alongMm.size() == d.items.size(),
              "and a readout number for each");
        check(d.error.empty(), "a successful derivation carries no error");

        // Guarded, not indexed on faith - CLAUDE.md's own Task 4 lesson: an
        // unguarded d.items[0] below would turn a derive() regression into a
        // silent SIGSEGV rather than a reported failure. And an ABSOLUTE
        // anchor, not merely a struct default or "matches some other run's
        // own number": this fixture is the same 300 mm contact, 40 mm end
        // margin and 3 dowels as the readout block earlier in this file, so
        // the real mark-out numbers are known independently to be 40, 150
        // and 260 mm.
        check(d.items.size() >= 3 && d.readout.alongMm.size() >= 3,
              "at least three items and three readout numbers to index below");
        if (d.items.size() >= 3 && d.readout.alongMm.size() >= 3) {
            checkNear(d.readout.alongMm[0], 40.0, 1.0e-6, "the first dowel sits 40 mm in");
            checkNear(d.readout.alongMm[1], 150.0, 1.0e-6, "the second at the 150 mm middle");
            checkNear(d.readout.alongMm[2], 260.0, 1.0e-6, "the third at the 260 mm far margin");
        }

        // MOVE the shelf: the numbers follow, without anything being stored.
        gp_Trsf up;
        up.SetTranslation(gp_Vec(0.0, 0.0, 120.0));
        const TopoDS_Shape moved =
            BRepBuilderAPI_Transform(shelf, up, Standard_True).Shape();
        const Joinery::Derivation after =
            Joinery::derive(Joinery::Kind::Dowel, p, {}, panel, moved);
        check(after.ok, "the joint still derives after the shelf moves");
        check(after.items.size() >= 3 && after.readout.alongMm.size() >= 3,
              "the moved derivation still carries three items and three readout numbers");
        if (after.items.size() >= 3 && d.items.size() >= 3) {
            checkNear(std::fabs(after.items[0].centre.Z() - d.items[0].centre.Z()), 120.0,
                      1.0e-6,
                      "and its items moved with the piece - by exactly the 120 mm shift");
        }
        // Absolute, not only "matches d's own number" - the brief's own
        // checkNear(after.readout.alongMm[0], d.readout.alongMm[0], ...) is
        // relative-only, and both sides could be wrong identically and still
        // pass, the exact vacuity a Task 5 implementer's own test caught.
        if (after.readout.alongMm.size() >= 3) {
            checkNear(after.readout.alongMm[0], 40.0, 1.0e-6,
                      "the mark-out numbers are unchanged - same joint, new place - "
                      "the real 40 mm, not merely d's own");
            checkNear(after.readout.alongMm[1], 150.0, 1.0e-6, "the real 150 mm");
            checkNear(after.readout.alongMm[2], 260.0, 1.0e-6, "the real 260 mm");
        }

        // PART them: broken, loudly, with no numbers at all.
        // NOT named `far`: that is a Windows SDK macro defined to nothing,
        // so the identifier silently vanishes and the expression quietly
        // means something else - CLAUDE.md's own Pitfalls entry, and a name
        // this feature has already had to rename twice over.
        gp_Trsf farAway;
        farAway.SetTranslation(gp_Vec(0.0, 0.0, 900.0));
        const TopoDS_Shape partedShelf =
            BRepBuilderAPI_Transform(shelf, farAway, Standard_True).Shape();
        const Joinery::Derivation broken =
            Joinery::derive(Joinery::Kind::Dowel, p, {}, panel, partedShelf);
        check(!broken.ok, "pieces that no longer meet cannot derive");
        check(!broken.error.empty(), "the break carries a reason");
        check(broken.items.empty() && broken.readout.alongMm.empty(),
              "and NO numbers - a stale measurement is worse than none");
        // The reason is findContact's OWN reason, propagated verbatim - not
        // merely non-empty. Caught by mutation: deleting derive()'s
        // `if (!contact.ok) return out;` guard still left broken.ok false
        // here (validityOf's independent 30 mm run floor happens to refuse
        // an empty/default Contact too), so a check of emptiness ALONE could
        // not tell the two refusal paths apart - this pins WHICH one fired.
        check(broken.error == Joinery::findContact(panel, partedShelf).error,
              "the break's error is findContact's own text, not a different "
              "refusal's (" + broken.error + ")");

        // The SECOND refusal branch: findContact succeeds - the pieces
        // genuinely meet, face to face - but the kind cannot exist on that
        // contact, a half-lap asked of two pieces that only touch, per
        // Task 6's validityOf. The brief's own test only exercises the
        // findContact refusal; deleting derive()'s validityOf branch would
        // leave the whole rest of this suite green while a half-lap silently
        // laid out a joint on a contact it has no business on.
        const Joinery::Parameters hl = Joinery::defaultsFor(Joinery::Kind::HalfLap, 18.0);
        const Joinery::Derivation invalidKind =
            Joinery::derive(Joinery::Kind::HalfLap, hl, {}, panel, shelf);
        check(!invalidKind.ok, "a half-lap on a plain face contact cannot derive");
        check(invalidKind.error == "the pieces aren't crossing",
              "and the reason is validityOf's OWN reason, spelled out exactly - not "
              "merely non-empty, and not findContact's refusal text");
        check(invalidKind.items.empty(), "no items are laid out for a refused kind");
        check(invalidKind.readout.alongMm.empty(), "and no readout numbers either");
        check(invalidKind.readout.referenceEdgeA.empty(),
              "the readout struct itself is left at its default, untouched");
    }

    // --- derive under a genuinely 3D rotation: the whole chain, not just
    // one piece of it, measured in the joint's own terms -----------------
    // Task 2 spent four fix rounds on exactly one error: an oriented
    // quantity measured in world-axis-aligned terms. Every earlier fix was
    // verified piece by piece - findContact alone, layout alone, readout
    // alone - but never as the one thing a user actually does: derive a
    // joint on a body that has been rotated off every world axis at once.
    // (1, 1, 1) is deliberately not a world axis and not even in a
    // coordinate plane, and the angle is deliberately not a multiple of 90
    // degrees, so nothing here can pass by accidentally landing back on an
    // axis-aligned case.
    {
        const TopoDS_Shape panel =
            BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape();
        const TopoDS_Shape shelf =
            BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape();
        const Joinery::Parameters p = Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0);
        const Joinery::Derivation flatDerivation =
            Joinery::derive(Joinery::Kind::Dowel, p, {}, panel, shelf);
        check(flatDerivation.ok, "sanity: the unrotated pair still derives");
        check(flatDerivation.items.size() >= 1,
              "sanity: with at least one item to compare a world centre against");

        gp_Trsf spin3D;
        const double angle = 53.0 * (4.0 * std::atan(1.0)) / 180.0;  // not a multiple of 90
        spin3D.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 1.0, 1.0)), angle);
        const TopoDS_Shape panelSpun =
            BRepBuilderAPI_Transform(panel, spin3D, Standard_True).Shape();
        const TopoDS_Shape shelfSpun =
            BRepBuilderAPI_Transform(shelf, spin3D, Standard_True).Shape();
        // Both pieces move through the SAME rotation, so they still touch -
        // this tests derive() end to end on a rotated joint, not whether two
        // arbitrarily-rotated boxes happen to meet by luck.
        const Joinery::ContactResult stillTouching =
            Joinery::findContact(panelSpun, shelfSpun);
        check(stillTouching.ok,
              "sanity: rotating both pieces through the same transform leaves them touching");

        const Joinery::Derivation spun =
            Joinery::derive(Joinery::Kind::Dowel, p, {}, panelSpun, shelfSpun);
        check(spun.ok,
              "the joint derives under a genuinely 3D rotation - off every world axis");
        check(spun.items.size() >= 3 && spun.readout.alongMm.size() >= 3,
              "still three items and three readout numbers, rotated or not");
        if (spun.items.size() >= 3 && spun.readout.alongMm.size() >= 3 &&
            flatDerivation.items.size() >= 1) {
            // The ABSOLUTE 40/150/260 mm, not merely "matches the unrotated
            // run" - both could be wrong identically under a shared error in
            // how an oriented quantity is measured, exactly the class of bug
            // Task 2 paid four rounds to fix. The joint's own measurements -
            // run length, margin, count - are unchanged by a rigid rotation,
            // so the real numbers must still be 40, 150 and 260.
            checkNear(spun.readout.alongMm[0], 40.0, 1.0e-6,
                      "rotated: the first dowel is still the real 40 mm in");
            checkNear(spun.readout.alongMm[1], 150.0, 1.0e-6,
                      "rotated: the second is still the real 150 mm middle");
            checkNear(spun.readout.alongMm[2], 260.0, 1.0e-6,
                      "rotated: the third is still the real 260 mm far margin");

            // The items' WORLD centres, by contrast, must genuinely differ -
            // a rotation that failed to reach the world-position derivation
            // at all (the bug class this whole block exists to catch from
            // the other side) would leave them sitting at the unrotated
            // coordinates while the readout above still happened to read
            // right.
            const double moved =
                spun.items[0].centre.Distance(flatDerivation.items[0].centre);
            check(moved > 50.0,
                  "and the items' world centres genuinely moved with the rotated pieces "
                  "(moved " + std::to_string(moved) + " mm)");
        }

        // Deliberately NOT asserting a compass word here (front/back/left/
        // right/top/bottom) - Task 5 reports an honest "no single edge"
        // sentence when a direction is not dominated by one axis, and a
        // rotation about (1,1,1) is exactly that case. Asserting a specific
        // word would pin an incidental property of this one angle rather
        // than the invariant this block actually tests.
        check(!spun.readout.referenceEdgeA.empty(),
              "the rotated readout still names SOMETHING, not an empty string");
        check(spun.readout.referenceEdgeA != "front" &&
                  spun.readout.referenceEdgeA != "back" &&
                  spun.readout.referenceEdgeA != "left" &&
                  spun.readout.referenceEdgeA != "right" &&
                  spun.readout.referenceEdgeA != "top" &&
                  spun.readout.referenceEdgeA != "bottom",
              "and at this angle no single compass word is honest (" +
                  spun.readout.referenceEdgeA + ")");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
