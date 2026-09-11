#include "Joinery.h"

#include <algorithm>
#include <cmath>

namespace Joinery {
namespace {

// The nearest real drill/dowel size at or below `wanted`, so a default is
// something a workshop actually owns rather than 5.94 mm.
double realDowelSize(double wanted)
{
    static const double kSizes[] = {4.0, 5.0, 6.0, 8.0, 10.0, 12.0};
    double best = kSizes[0];
    for (const double size : kSizes) {
        if (size <= wanted + 1.0e-9) best = size;
    }
    return best;
}

}  // namespace

Family familyOf(Kind kind)
{
    switch (kind) {
        case Kind::Dowel:
        case Kind::PocketScrew:
        case Kind::Biscuit:
        case Kind::Domino:
        case Kind::Screw:
            return Family::Fasteners;
        case Kind::Dado:
        case Kind::Rabbet:
        case Kind::Groove:
            return Family::Housing;
        case Kind::MortiseTenon:
        case Kind::HalfLap:
            return Family::Interlock;
    }
    return Family::Fasteners;
}

std::string kindName(Kind kind)
{
    switch (kind) {
        case Kind::Dowel:        return "Dowel";
        case Kind::PocketScrew:  return "Pocket screw";
        case Kind::Biscuit:      return "Biscuit";
        case Kind::Domino:       return "Domino";
        case Kind::Screw:        return "Screw";
        case Kind::Dado:         return "Dado";
        case Kind::Rabbet:       return "Rabbet";
        case Kind::Groove:       return "Groove";
        case Kind::MortiseTenon: return "Mortise and tenon";
        case Kind::HalfLap:      return "Half-lap";
    }
    return "Joint";
}

Parameters defaultsFor(Kind kind, double thinnerThicknessMm)
{
    const double t = thinnerThicknessMm > 1.0e-6 ? thinnerThicknessMm : 18.0;
    Parameters p;
    p.widthMm = t;
    p.insetMm = t / 2.0;
    p.thicknessMm = t / 3.0;

    switch (familyOf(kind)) {
        case Family::Fasteners: {
            p.sizeMm = realDowelSize(t / 3.0);
            p.count = 3;
            // Deep enough to hold, short enough to leave material: a third
            // of the board on the through side, and a shade under half the
            // board's own thickness on the end-grain side.
            p.depthAMm = std::max(t * 0.75, 10.0);
            p.depthBMm = std::max(t * 0.75, 10.0);
            // endMarginMm is left at the struct default (40 mm) - fasteners
            // have no board-derived reason to move it, and housings and
            // interlocks never read the field.
            p.angleDeg = kind == Kind::PocketScrew ? 15.0 : 0.0;
            break;
        }
        case Family::Housing: {
            // A third of the host deep is the standard rule of thumb; the
            // channel is exactly as wide as the piece that sits in it.
            p.depthAMm = t / 3.0;
            p.depthBMm = 0.0;
            p.stopped = false;
            p.stopMm = 10.0;
            break;
        }
        case Family::Interlock: {
            p.thicknessMm = t / 3.0;
            p.lengthMm = std::max(t * 1.5, 25.0);
            p.depthAMm = p.lengthMm + 2.0;   // mortise a hair deeper than the tenon
            p.haunched = false;
            break;
        }
    }
    return p;
}

}  // namespace Joinery
