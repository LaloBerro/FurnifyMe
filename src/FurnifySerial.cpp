#include "FurnifySerial.h"

#include <cstring>
#include <istream>
#include <ostream>

#include <BinTools.hxx>
#include <BinTools_ShapeSet.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace FurnifySerial {

namespace {

void putPlane(std::ostream& out, const gp_Pln& plane)
{
    // A plane's full placement, not just its normal: Location, main
    // Direction (the normal) and XDirection together are what gp_Ax3 needs
    // to reconstruct exactly, including the in-plane rotation that a
    // location+normal pair alone would lose.
    const gp_Ax3& position = plane.Position();
    const gp_Pnt& loc = position.Location();
    const gp_Dir& normal = position.Direction();
    const gp_Dir& xdir = position.XDirection();
    BinTools::PutReal(out, loc.X());
    BinTools::PutReal(out, loc.Y());
    BinTools::PutReal(out, loc.Z());
    BinTools::PutReal(out, normal.X());
    BinTools::PutReal(out, normal.Y());
    BinTools::PutReal(out, normal.Z());
    BinTools::PutReal(out, xdir.X());
    BinTools::PutReal(out, xdir.Y());
    BinTools::PutReal(out, xdir.Z());
}

// Throws (caught by the caller) on a zero-length direction - exactly the
// shape a corrupt or truncated stream can hand back, and exactly the case
// this must refuse rather than build a plane out of garbage.
gp_Pln getPlane(std::istream& in)
{
    double v[9];
    for (double& value : v) BinTools::GetReal(in, value);
    const gp_Pnt loc(v[0], v[1], v[2]);
    const gp_Dir normal(v[3], v[4], v[5]);
    const gp_Dir xdir(v[6], v[7], v[8]);
    return gp_Pln(gp_Ax3(loc, normal, xdir));
}

}  // namespace

SerialResult writeShapes(const SerializedDocument& doc, std::ostream& out)
{
    if (doc.outlineFaces.size() != doc.outlinePlanes.size()) {
        return {false, "outline face count does not match outline plane count"};
    }
    for (const TopoDS_Shape& s : doc.bodies) {
        if (s.IsNull()) return {false, "a body shape is null"};
    }
    for (const TopoDS_Shape& s : doc.outlineFaces) {
        if (s.IsNull()) return {false, "an outline face is null"};
    }

    try {
        // BinTools_ShapeSet::Add(S) stores S AND every sub-shape it has not
        // seen yet (shared sub-shapes are written once), and the index it
        // returns for S is assigned in whatever order the set encounters
        // things - NOT the order Add() was called in, and emphatically not
        // 1, 2, 3, ... per top-level shape. So the indices returned here
        // are recorded explicitly in the header rather than assumed, and
        // NbShapes() after Write() counts every sub-shape too, not just the
        // ones this document added at the top level - it is not a count
        // this format can cross-check against bodyCount + outlineCount.
        BinTools_ShapeSet shapeSet;
        std::vector<int> bodyIndices;
        std::vector<int> outlineIndices;
        bodyIndices.reserve(doc.bodies.size());
        outlineIndices.reserve(doc.outlineFaces.size());
        for (const TopoDS_Shape& s : doc.bodies) bodyIndices.push_back(shapeSet.Add(s));
        for (const TopoDS_Shape& s : doc.outlineFaces) outlineIndices.push_back(shapeSet.Add(s));

        out.write(kMagic, 8);
        BinTools::PutInteger(out, kFormatVersion);
        BinTools::PutInteger(out, static_cast<int>(doc.bodies.size()));
        BinTools::PutInteger(out, static_cast<int>(doc.outlineFaces.size()));
        for (int i : bodyIndices) BinTools::PutInteger(out, i);
        for (int i : outlineIndices) BinTools::PutInteger(out, i);

        shapeSet.Write(out);

        for (const gp_Pln& plane : doc.outlinePlanes) putPlane(out, plane);
    } catch (const Standard_Failure& e) {
        return {false, std::string("shape write failed: ") +
                           (e.GetMessageString() ? e.GetMessageString() : "unknown error")};
    }

    if (!out.good()) return {false, "stream write failed"};
    return {true, ""};
}

SerialResult readShapes(std::istream& in, SerializedDocument& doc)
{
    doc = SerializedDocument{};

    char magic[8] = {};
    in.read(magic, 8);
    if (!in.good() || std::memcmp(magic, kMagic, 8) != 0) {
        return {false, "not a FurnifyMe shape blob"};
    }

    int version = 0;
    BinTools::GetInteger(in, version);
    if (!in.good()) return {false, "truncated shape data"};
    if (version != kFormatVersion) {
        return {false, "unsupported shape format version " + std::to_string(version) +
                           " (this build reads version " + std::to_string(kFormatVersion) + ")"};
    }

    int bodyCount = 0;
    int outlineCount = 0;
    BinTools::GetInteger(in, bodyCount);
    BinTools::GetInteger(in, outlineCount);
    if (!in.good() || bodyCount < 0 || outlineCount < 0) {
        return {false, "truncated shape data"};
    }

    std::vector<int> bodyIndices(static_cast<std::size_t>(bodyCount));
    std::vector<int> outlineIndices(static_cast<std::size_t>(outlineCount));
    for (int& i : bodyIndices) BinTools::GetInteger(in, i);
    for (int& i : outlineIndices) BinTools::GetInteger(in, i);
    if (!in.good()) return {false, "truncated shape data"};

    try {
        BinTools_ShapeSet shapeSet;
        shapeSet.Read(in);

        if (!in.good() && !in.eof()) {
            return {false, "truncated shape data"};
        }

        // Every recorded index has to land inside the shape set that was
        // actually read back, or the file is corrupt (truncated mid-shape-
        // set, or hand-edited) rather than merely short a few bytes.
        const int nbShapes = shapeSet.NbShapes();
        for (int i : bodyIndices) {
            if (i < 1 || i > nbShapes) return {false, "a body index is out of range - file is corrupt"};
        }
        for (int i : outlineIndices) {
            if (i < 1 || i > nbShapes)
                return {false, "an outline index is out of range - file is corrupt"};
        }

        SerializedDocument result;
        result.bodies.reserve(static_cast<std::size_t>(bodyCount));
        result.outlineFaces.reserve(static_cast<std::size_t>(outlineCount));
        result.outlinePlanes.reserve(static_cast<std::size_t>(outlineCount));

        for (int i : bodyIndices) result.bodies.push_back(shapeSet.Shape(i));
        for (int i : outlineIndices) result.outlineFaces.push_back(shapeSet.Shape(i));
        for (int i = 0; i < outlineCount; ++i) result.outlinePlanes.push_back(getPlane(in));

        if (!in.good() && !in.eof()) {
            return {false, "truncated shape data"};
        }
        for (const TopoDS_Shape& s : result.bodies) {
            if (s.IsNull()) return {false, "a decoded body shape is null - file is corrupt"};
        }
        for (const TopoDS_Shape& s : result.outlineFaces) {
            if (s.IsNull() || s.ShapeType() != TopAbs_FACE) {
                return {false, "a decoded outline shape is not a face - file is corrupt"};
            }
        }

        doc = result;
    } catch (const Standard_Failure& e) {
        doc = SerializedDocument{};
        return {false, std::string("corrupt shape data: ") +
                           (e.GetMessageString() ? e.GetMessageString() : "unknown error")};
    }

    return {true, ""};
}

}  // namespace FurnifySerial
