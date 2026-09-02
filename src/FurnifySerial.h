#pragma once
//
// Binary (de)serialization of the shapes a document owns - bodies, outline
// faces and the planes those outlines were drawn on - via OCCT's own
// BinTools_ShapeSet, which round-trips exact B-rep with no tessellation
// loss. Qt-free: this lives in furnify_geometry beside ModelingOps, so it is
// runnable from the headless suite with no window and no GPU, exactly like
// every other file in that target.
//
// This file knows nothing about names, visibility, versions or manifests -
// those are document/library metadata, not kernel geometry, and stay in
// DocumentModel and FurnitureStore respectively. That split is deliberate:
// a blob format that also carried labels could not be reused the day
// something other than a whole document needs the same round-trip (a
// version snapshot, a clipboard, a future import), and every past addition
// to ModelingOps was earned by exactly that kind of reuse.
//
#include <iosfwd>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

namespace FurnifySerial {

// Everything the kernel side of a document owns. `outlineFaces` and
// `outlinePlanes` are positionally matched - outlineFaces[i] was drawn on
// outlinePlanes[i] - and writeShapes refuses a document where the two
// vectors are not the same length rather than silently truncating either.
struct SerializedDocument {
    std::vector<TopoDS_Shape> bodies;
    std::vector<TopoDS_Shape> outlineFaces;
    std::vector<gp_Pln> outlinePlanes;
};

// Refusal contract, binding for both functions below and pinned by test:
// ok == false always leaves the "output" side empty/untouched by anything
// this call did - writeShapes leaves the stream exactly where it started
// (nothing partial is ever flushed past the point of failure mattering to a
// caller, since the caller should not use the stream at all when ok is
// false), and readShapes leaves `doc` as a default-constructed
// SerializedDocument (empty vectors), never a partially populated one. A
// half-loaded document is worse than a refused one.
struct SerialResult {
    bool ok = false;
    std::string error;
};

// The blob's own header. Exposed (not private to the .cpp) so a test can
// build a deliberately-mismatched header - a future format version, in
// particular - without hand-encoding the magic bytes, and so any future
// caller working with raw bytes can recognise a FurnifyMe shape blob before
// handing it to readShapes at all.
inline constexpr char kMagic[9] = "FRNYSHP1";
constexpr int kFormatVersion = 1;

// Writes `doc` to `out` as one self-contained binary blob: a small header
// (magic, format version, item counts), the shapes themselves via one
// BinTools_ShapeSet (bodies first, then outline faces, in order - so
// sub-shapes shared between them are written once), then each outline
// plane's full placement (origin, normal and in-plane X axis - the three
// together are what a plane needs to reconstruct exactly, not just its
// normal).
//
// Refuses (ok == false, nothing meaningful written) when
// outlineFaces.size() != outlinePlanes.size(), when any shape is null, or
// on any exception the OCCT writer raises - which BinTools_ShapeSet can do
// on a stream that stops accepting bytes partway through, mapped here to a
// refusal rather than left to propagate.
SerialResult writeShapes(const SerializedDocument& doc, std::ostream& out);

// Reads a blob written by writeShapes. `doc` is cleared to empty vectors
// first and populated only on success - see SerialResult's contract above.
//
// Refuses: a stream that is not a FurnifyMe shape blob (bad magic), a
// format version other than kFormatVersion (a FUTURE version most of all -
// this is a newer file than this build understands, and guessing at it is
// how a document silently loses data), a truncated or otherwise corrupt
// stream (checked both by the stream's own fail state and by cross-checking
// the shape count BinTools_ShapeSet actually read against the header's
// count), and any exception the OCCT reader raises on malformed geometry
// data.
SerialResult readShapes(std::istream& in, SerializedDocument& doc);

}  // namespace FurnifySerial
