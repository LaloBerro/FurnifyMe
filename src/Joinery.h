#pragma once
// Joinery: the wood joints between two pieces, as a PLAN rather than as
// geometry (spec: docs/superpowers/specs/2026-09-10-joinery-design.md).
// Nothing here cuts a body. A joint records what the connection IS - its
// kind, its two pieces, its parameters - and this file derives, on demand,
// where its items fall and what numbers to mark on the wood.
//
// ZERO Qt, by the same law ModelingOps keeps: this lives in the
// furnify_geometry target, which does not link Qt, so every function below
// is provable in the headless suite with no window and no GPU.
//
// Millimetres throughout. Measure is the only place a unit is ever
// converted for display, and it is a Qt-free header this one does not need.
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

namespace Joinery {

// The nine kinds, grouped by the three behaviours that actually differ.
// A kind is a preset over its family: same placement rule, same derivation,
// different parameters and a different thing drawn.
enum class Kind {
    Dowel,
    PocketScrew,
    Biscuit,
    Domino,
    Screw,
    Dado,
    Rabbet,
    Groove,
    MortiseTenon,
    HalfLap,
};

enum class Family {
    Fasteners,   // N discrete items in a row along the contact
    Housing,     // a channel in one piece that the other sits in
    Interlock,   // complementary material removed from both
};

Family familyOf(Kind kind);
// The real woodworking name, for every painted string and every message.
std::string kindName(Kind kind);

// One parameter block serves all three families - a kind reads the fields
// that apply to it and ignores the rest. One struct rather than a variant
// because it is persisted, undone and edited as a unit, and a variant would
// buy type-safety at the cost of three serializers.
struct Parameters {
    // Fasteners.
    int count = 3;             // how many items along the joint
    double sizeMm = 6.0;       // dowel diameter, Domino width, screw gauge
    double depthAMm = 15.0;    // into bodyA
    double depthBMm = 15.0;    // into bodyB
    double insetMm = 9.0;      // in from the reference face
    double endMarginMm = 40.0; // first and last item's distance from the ends
    double angleDeg = 15.0;    // pocket screws only

    // Housings.
    double widthMm = 18.0;     // channel width, defaults to the housed piece
    bool stopped = false;      // blind-ended rather than through
    double stopMm = 10.0;      // how far short it stops

    // Interlocks.
    double thicknessMm = 6.0;  // tenon thickness / lap depth
    double lengthMm = 30.0;    // tenon length
    bool haunched = false;
};

// Real-world-sane defaults, measured off the wood: `thinnerThicknessMm` is
// the thinner of the two pieces at the joint. Dowels land on real drill
// sizes rather than an arbitrary third of a millimetre.
Parameters defaultsFor(Kind kind, double thinnerThicknessMm);

}  // namespace Joinery
