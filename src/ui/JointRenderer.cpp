#include "JointRenderer.h"

#include "Theme.h"

#include <AIS_DisplayMode.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

// Ghosted: a plan, not material. Transparent enough to read the wood through,
// solid enough to see the hardware's shape.
constexpr double kGhostTransparency = 0.55;
// The SELECTED joint (Task 13), at full strength against the ghosts - still
// transparent enough to read the wood behind it, far enough from 0.55 to be
// unmistakable. See JointRenderer::Drawing::selected for why the difference is
// strength rather than a second colour.
constexpr double kSelectedTransparency = 0.15;
// The smallest dimension any piece of hardware is drawn at, so a zero-depth
// or zero-width item still shows up as something rather than a degenerate
// primitive the kernel refuses.
constexpr double kMinDrawnMm = 1.0;
// Tessellation - an untessellated face draws nothing. A 6 mm dowel wants a
// fine chord; a 600 mm housing is all planes and costs nothing extra.
constexpr double kMeshDeflectionMm = 0.1;
constexpr double kMeshAngleRad = 0.35;
// The outline a housing, tenon or lap block wears, in device pixels: the
// accent at full strength over a fill that is the accent ghosted, which is
// what makes the edge read as the outline of a channel rather than as more
// fill.
constexpr double kOutlineWidthPx = 1.5;
constexpr double kPi = 3.14159265358979323846;
// The change check's equality. Two derivations of the same live shapes land on
// identical numbers; these only absorb rounding, never a real move.
constexpr double kSameMm = 1.0e-9;
constexpr double kSameRad = 1.0e-9;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// A dowel or a screw: a pin from `depthA` into piece A to `depthB` into piece
// B, crossing the contact plane at the item's centre. Tilted by the item's
// angle about the RUN - the line the row of fasteners follows - so it leans
// across the board's thickness, which is the plane a pocket-hole jig drills
// in. A straight pin would show the wrong drill direction, and the direction
// is the point of planning a pocket hole at all.
TopoDS_Shape fastenerShape(const Joinery::Item& item, const Joinery::Contact& contact)
{
    gp_Dir axis = item.axis;
    if (std::fabs(item.angleDeg) > 1.0e-9) {
        const gp_Dir run =
            contact.runsAlongU() ? contact.frame.XDirection() : contact.frame.YDirection();
        axis = item.axis.Rotated(gp_Ax1(item.centre, run), item.angleDeg * kPi / 180.0);
    }
    const double radius = std::max(item.sizeMm, kMinDrawnMm) / 2.0;
    const double length = std::max(item.depthAMm + item.depthBMm, kMinDrawnMm);
    const gp_Pnt start = item.centre.Translated(gp_Vec(axis) * -item.depthAMm);
    return BRepPrimAPI_MakeCylinder(gp_Ax2(start, axis), radius, length).Shape();
}

// A housing, a tenon or a lap: outlined blocks over the item's span, spanUMm
// along the contact's own u and spanVMm along v, centred on the item.
//
// The corner is built from the block frame's OWN X and Y rather than through
// Contact::at(), so the block stays centred on the item even if the axis ever
// points against the contact frame's Z - a block is symmetric about its centre
// in u and v, so which way Y points does not matter, only that it is the
// frame's.
void regionPieces(const Joinery::Item& item, const Joinery::Contact& contact,
                  std::vector<JointRenderer::Piece>& out)
{
    const double spanU = std::max(item.spanUMm, kMinDrawnMm);
    const double spanV = std::max(item.spanVMm, kMinDrawnMm);
    const gp_Ax2 centred(item.centre, item.axis, contact.frame.XDirection());
    // A block running `length` along the axis, starting `from` the plane.
    auto block = [&](double from, double length) {
        const gp_Pnt corner =
            item.centre.Translated(gp_Vec(centred.XDirection()) * (-spanU / 2.0) +
                                   gp_Vec(centred.YDirection()) * (-spanV / 2.0) +
                                   gp_Vec(item.axis) * from);
        JointRenderer::Piece piece;
        piece.shape = BRepPrimAPI_MakeBox(gp_Ax2(corner, item.axis, centred.XDirection()),
                                          spanU, spanV, std::max(length, kMinDrawnMm))
                          .Shape();
        piece.outlined = true;
        return piece;
    };

    if (contact.type == Joinery::Contact::Type::Overlap) {
        // A lap is NOT about the contact plane: an Overlap frame sits on the
        // lap's own minimum-depth face and the lap runs [0, lap depth] along Z
        // from it (Contact's documented convention). Centring the block on the
        // plane, as a tenon is, stood half of it outside the crossing.
        const double removedA = std::max(item.depthAMm, 0.0);
        const double removedB = std::max(item.depthBMm, 0.0);
        if (removedA + removedB < kMinDrawnMm) {
            out.push_back(block(0.0, kMinDrawnMm));
            return;
        }
        if (removedA > 0.0) out.push_back(block(0.0, removedA));
        if (removedB > 0.0) out.push_back(block(removedA, removedB));
        return;
    }
    // A housing's depthB is 0, so its block is the channel in the host alone;
    // a tenon's runs from the mortise in A through to its length in B.
    out.push_back(block(-item.depthAMm, item.depthAMm + item.depthBMm));
}

bool sameMm(double a, double b) { return std::fabs(a - b) <= kSameMm; }
bool samePnt(const gp_Pnt& a, const gp_Pnt& b) { return a.Distance(b) <= kSameMm; }
bool sameDir(const gp_Dir& a, const gp_Dir& b) { return a.IsEqual(b, kSameRad); }

bool sameItem(const Joinery::Item& a, const Joinery::Item& b)
{
    return samePnt(a.centre, b.centre) && sameDir(a.axis, b.axis) && sameMm(a.sizeMm, b.sizeMm) &&
           sameMm(a.depthAMm, b.depthAMm) && sameMm(a.depthBMm, b.depthBMm) &&
           sameMm(a.spanUMm, b.spanUMm) && sameMm(a.spanVMm, b.spanVMm) &&
           std::fabs(a.angleDeg - b.angleDeg) <= 1.0e-9;
}

// Only what the pieces are built from: the type picks the lap's extent, the
// frame orients a block, and which way the run lies tilts a pin.
bool sameContact(const Joinery::Contact& a, const Joinery::Contact& b)
{
    return a.type == b.type && samePnt(a.frame.Location(), b.frame.Location()) &&
           sameDir(a.frame.Direction(), b.frame.Direction()) &&
           sameDir(a.frame.XDirection(), b.frame.XDirection()) &&
           sameDir(a.frame.YDirection(), b.frame.YDirection()) &&
           a.runsAlongU() == b.runsAlongU();
}

bool sameDrawing(const JointRenderer::Drawing& a, const JointRenderer::Drawing& b)
{
    // `selected` decides a drawn pixel, so it belongs in the change check: a
    // joint that has just become the selected one must be REBUILT, not skipped
    // as unchanged.
    if (a.kind != b.kind || a.derivation.ok != b.derivation.ok || a.selected != b.selected)
        return false;
    if (!a.derivation.ok) return true;   // a broken joint draws nothing either way
    if (!sameContact(a.derivation.contact, b.derivation.contact)) return false;
    if (a.derivation.items.size() != b.derivation.items.size()) return false;
    for (std::size_t i = 0; i < a.derivation.items.size(); ++i) {
        if (!sameItem(a.derivation.items[i], b.derivation.items[i])) return false;
    }
    return true;
}

bool sameDrawings(const std::vector<JointRenderer::Drawing>& a,
                  const std::vector<JointRenderer::Drawing>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameDrawing(a[i], b[i])) return false;
    }
    return true;
}

}  // namespace

std::vector<JointRenderer::Piece> JointRenderer::piecesFor(const Drawing& drawing)
{
    std::vector<Piece> pieces;
    if (!drawing.derivation.ok) return pieces;   // a broken joint draws nothing
    const Joinery::Contact& contact = drawing.derivation.contact;
    const bool fastener = Joinery::familyOf(drawing.kind) == Joinery::Family::Fasteners;
    for (const Joinery::Item& item : drawing.derivation.items) {
        try {
            if (fastener) {
                Piece pin;
                pin.shape = fastenerShape(item, contact);
                pin.outlined = false;   // a dowel is a cylinder, not a drawing of one
                pieces.push_back(pin);
            } else {
                regionPieces(item, contact, pieces);
            }
        } catch (const Standard_Failure&) {
            // A degenerate frame (an axis parallel to the contact's X) or a
            // primitive the kernel refuses: that one item draws nothing
            // rather than taking the viewport down with it.
            continue;
        }
    }
    return pieces;
}

void JointRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void JointRenderer::detach()
{
    if (!myContext.IsNull()) {
        for (auto& object : myObjects) myContext->Remove(object, Standard_False);
    }
    myObjects.clear();
    myDrawings.clear();
    myJointsDrawn = 0;
    myJointsHighlighted = 0;
    myContext.Nullify();
}

Graphic3d_ZLayerId JointRenderer::drawLayer() const
{
    return myLayer != Graphic3d_ZLayerId_UNKNOWN ? myLayer : Graphic3d_ZLayerId_Topmost;
}

bool JointRenderer::clear()
{
    const bool had = !myObjects.empty();
    if (!myContext.IsNull() && had) {
        // No UpdateCurrentViewer(): the caller owns the frame since the
        // QOpenGLWidget migration.
        for (auto& object : myObjects) myContext->Remove(object, Standard_False);
    }
    myObjects.clear();
    myDrawings.clear();
    myJointsDrawn = 0;
    myJointsHighlighted = 0;
    return had;
}

bool JointRenderer::reapplyTheme()
{
    if (myObjects.empty() || myContext.IsNull()) return false;
    const Quantity_Color colour = toOcct(Theme::accent());
    for (auto& object : myObjects) {
        // SetColor writes the existing shading aspect's material colour and
        // synchronizes it - the transparency lives in that same material and
        // is left as it is. The outline is the object's OWN line aspect
        // (addPiece() gave it one), recoloured where it stands, and
        // SynchronizeAspects() pushes both to the drawn groups. No Remove, no
        // Display, no BRepMesh: nothing about the geometry changed.
        object->SetColor(colour);
        if (object->Attributes()->FaceBoundaryDraw())
            object->Attributes()->FaceBoundaryAspect()->SetColor(colour);
        object->SynchronizeAspects();
    }
    return true;   // caller owns the frame
}

std::vector<TopoDS_Shape> JointRenderer::shapes() const
{
    std::vector<TopoDS_Shape> out;
    out.reserve(myObjects.size());
    for (const auto& object : myObjects) out.push_back(object->Shape());
    return out;
}

void JointRenderer::addPiece(const Piece& piece, const QColor& colour, bool selected)
{
    if (myContext.IsNull() || piece.shape.IsNull()) return;
    BRepMesh_IncrementalMesh(piece.shape, kMeshDeflectionMm, Standard_False, kMeshAngleRad,
                             Standard_True);
    Handle(AIS_Shape) object = new AIS_Shape(piece.shape);
    object->SetDisplayMode(AIS_Shaded);
    object->SetColor(toOcct(colour));
    object->SetTransparency(selected ? kSelectedTransparency : kGhostTransparency);
    // A housing, tenon or lap reads as an outlined channel or block over its
    // ghost fill - the spec's own picture; a pin is hardware, not a drawing of
    // it. The outline gets a line aspect of its OWN, set after SetColor, so
    // reapplyTheme() can recolour exactly this object's line in place.
    object->Attributes()->SetFaceBoundaryDraw(piece.outlined ? Standard_True : Standard_False);
    if (piece.outlined) {
        object->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(toOcct(colour), Aspect_TOL_SOLID, kOutlineWidthPx));
    }
    // Into its layer BEFORE Display, so it never spends a frame in the
    // default layer where the wood would hide it.
    object->SetZLayer(drawLayer());
    // Selection mode -1: never pickable, never hoverable. An AIS_Shape's
    // ComputeSelection only runs for an ACTIVATED mode, so this never enters
    // the pick pipeline and can never steal a click from the wood around it.
    myContext->Display(object, AIS_Shaded, -1, Standard_False);
    myObjects.push_back(object);
}

void JointRenderer::build()
{
    ++myBuildCount;
    myJointsDrawn = 0;
    myJointsHighlighted = 0;
    if (myContext.IsNull()) return;

    // Fresh on every build - never cached across a themeChanged broadcast.
    const QColor colour = Theme::accent();
    for (const Drawing& drawing : myDrawings) {
        const std::size_t before = myObjects.size();
        for (const Piece& piece : piecesFor(drawing)) addPiece(piece, colour, drawing.selected);
        if (myObjects.size() > before) {
            ++myJointsDrawn;
            if (drawing.selected) ++myJointsHighlighted;
        }
    }
}

bool JointRenderer::show(const std::vector<Drawing>& drawings)
{
    // The change check: an unchanged joint costs a compare, not a re-mesh.
    if (!myContext.IsNull() && sameDrawings(drawings, myDrawings)) return false;

    const bool had = clear();
    if (myContext.IsNull()) return had;

    myDrawings = drawings;
    build();
    return had || !myObjects.empty();
}
