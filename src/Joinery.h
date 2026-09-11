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
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <string>
#include <vector>

namespace Joinery {

// The ten kinds, grouped by the three behaviours that actually differ.
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
    double angleDeg = 0.0;     // an angle belongs to the kinds that drill at
                                // one (pocket screws) - defaultsFor() sets it

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

// Where two pieces meet, in the contact's OWN frame - the one coordinate
// system every derived position is expressed in, so nothing downstream
// needs to know a world axis. A joint stores no world position; this is
// recomputed from the live shapes on every read, which is what makes a
// joint follow its pieces.
struct Contact {
    enum class Type {
        Face,      // two faces meet - the ordinary case
        Overlap,   // the solids intersect - what a half-lap is cut from
    };

    Type type = Type::Face;

    // The contact's own frame, and the ONE convention BOTH types honour -
    // every position Tasks 3 onward derive is measured from this origin, so
    // two conventions on one struct would be a trap rather than a detail:
    //
    //   ORIGIN is the region's own (uMin, vMin) corner. `uMin` and `vMin`
    //   are therefore ALWAYS 0.0, `uMax`/`vMax` ARE the region's own size,
    //   and at(0, 0) is a real corner of it.
    //
    //   X comes from the REGION'S OWN GEOMETRY - the direction of its
    //   longest boundary edge - never from a world axis and never from
    //   gp_Ax3(point, dir)'s arbitrary default pick, which measures the
    //   region's bounding box in a WORLD orientation and reports a true
    //   300 x 18 contact as 224.86 x 224.86 at 45 degrees of in-plane
    //   rotation. So u runs ALONG the joint and v ACROSS it by
    //   construction, which is what makes runsAlongU() and runLength()
    //   mean what they say at every orientation. Y is Z x X, right-handed.
    //
    //   Z is the contact normal. For a Face contact it points FROM bodyA
    //   INTO bodyB, settled by classifying a point a hair either side of the
    //   region's centroid against each solid - not from a face's
    //   orientation flag, which carries no such information, and not from
    //   the two planes' signed offset, which is exactly 0.0 for a flush
    //   contact and so answers the same for findContact(a, b) and
    //   findContact(b, a). For an OVERLAP there is no "into" - neither piece
    //   is on one side of a lap - so Z is the lap's own THINNEST direction
    //   (the lap depth), oriented from a's centre of mass toward b's when
    //   that is decisive, and the lap spans [0, lap depth] along it from the
    //   frame's plane.
    gp_Ax3 frame;
    double uMin = 0.0, uMax = 0.0;
    double vMin = 0.0, vMax = 0.0;
    // Each piece's own thickness at the joint, for defaultsFor() - the
    // smallest extent over that solid's OWN face normals, so a board the
    // transform gizmo has rotated still measures 18 mm rather than the
    // 224.86 mm its world bounding box would report.
    double thicknessAMm = 0.0;
    double thicknessBMm = 0.0;

    double uLength() const { return uMax - uMin; }
    double vLength() const { return vMax - vMin; }
    // The longer in-plane direction - the line a row of fasteners runs
    // along, and the length a housing is cut across.
    bool runsAlongU() const { return uLength() >= vLength(); }
    double runLength() const { return std::max(uLength(), vLength()); }
    // A point in the contact's own coordinates, in the world.
    gp_Pnt at(double u, double v) const;
};

// ok == false ALWAYS carries an empty contact and a non-empty error - the
// BooleanResult contract, so a refusal can never be read as a success.
struct ContactResult {
    bool ok = false;
    Contact contact;
    std::string error;
};

// The largest place `a` and `b` meet. A face contact wins over an overlap
// when both exist (two boards can touch AND intersect slightly - the
// separating test is asked LOCALLY, at the shared region, so slop of the
// same order as `toleranceMm` does not suppress a real contact); an overlap
// is reported only when there is no face contact, which is the
// crossing-rails case a half-lap is cut from.
//
// `toleranceMm` is how far apart two faces may be and still count as
// touching - a model is never perfect, and 0.1 mm of slop is not a gap.
//
// Refuses, with a reason, when the pieces do not meet, when a piece is
// missing, when the kernel throws, and when they meet only on a CURVED
// boundary whose rectangle cannot be measured - a flat board on a round leg
// touches along a line, and ok == true with a zero-size contact would be a
// refusal surfacing as a success.
ContactResult findContact(const TopoDS_Shape& a, const TopoDS_Shape& b,
                          double toleranceMm = 0.1);

}  // namespace Joinery
