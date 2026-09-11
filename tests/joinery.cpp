// Headless oracle for the joinery maths (spec:
// docs/superpowers/specs/2026-09-10-joinery-design.md). No Qt, no GPU -
// every function under test is a pure function of shapes and numbers.
#include "Joinery.h"

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
    checkNear(dowel24.endMarginMm, 40.0, 1.0e-9, "end margin stays the fixed 40 mm");
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

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
