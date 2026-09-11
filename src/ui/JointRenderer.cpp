#include "JointRenderer.h"

#include "Theme.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>

#include <algorithm>

namespace {

// Ghosted: a plan, not material. Transparent enough to read the wood through,
// solid enough to see the hardware's shape.
constexpr double kGhostTransparency = 0.55;
// The smallest dimension any piece of hardware is drawn at, so a zero-depth
// or zero-width item still shows up as something rather than a degenerate
// primitive the kernel refuses.
constexpr double kMinDrawnMm = 1.0;
// Tessellation - an untessellated face draws nothing. A 6 mm dowel wants a
// fine chord; a 600 mm housing is all planes and costs nothing extra.
constexpr double kMeshDeflectionMm = 0.1;
constexpr double kMeshAngleRad = 0.35;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// A dowel or a screw: a cylinder along the item's axis, from `depthA` into
// piece A to `depthB` into piece B.
TopoDS_Shape fastenerShape(const Joinery::Item& item)
{
    const double radius = std::max(item.sizeMm, kMinDrawnMm) / 2.0;
    const double length = std::max(item.depthAMm + item.depthBMm, kMinDrawnMm);
    const gp_Pnt start = item.centre.Translated(gp_Vec(item.axis) * -item.depthAMm);
    return BRepPrimAPI_MakeCylinder(gp_Ax2(start, item.axis), radius, length).Shape();
}

// A housing or an interlock: one block, spanUMm along the contact's own u and
// spanVMm along v, centred on the item and running the same [-depthA, +depthB]
// along the axis a fastener does (a housing's depthB is 0, so its block is the
// channel in the host alone).
//
// The corner is built from the box frame's OWN X and Y rather than through
// Contact::at(), so the block stays centred on the item even if the axis ever
// points against the contact frame's Z - a box is symmetric about its centre,
// so which way Y points does not matter, only that it is the frame's.
TopoDS_Shape regionShape(const Joinery::Item& item, const Joinery::Contact& contact)
{
    const double spanU = std::max(item.spanUMm, kMinDrawnMm);
    const double spanV = std::max(item.spanVMm, kMinDrawnMm);
    const double length = std::max(item.depthAMm + item.depthBMm, kMinDrawnMm);
    const gp_Ax2 centred(item.centre, item.axis, contact.frame.XDirection());
    const gp_Pnt corner = item.centre.Translated(gp_Vec(centred.XDirection()) * (-spanU / 2.0) +
                                                 gp_Vec(centred.YDirection()) * (-spanV / 2.0) +
                                                 gp_Vec(item.axis) * -item.depthAMm);
    return BRepPrimAPI_MakeBox(gp_Ax2(corner, item.axis, centred.XDirection()), spanU, spanV,
                               length)
        .Shape();
}

}  // namespace

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
    return had;
}

bool JointRenderer::reapplyTheme()
{
    if (myObjects.empty() || myContext.IsNull()) return false;
    for (auto& object : myObjects) myContext->Remove(object, Standard_False);
    myObjects.clear();
    build();
    return true;   // caller owns the frame
}

void JointRenderer::addSolid(const TopoDS_Shape& shape, const QColor& colour)
{
    if (myContext.IsNull() || shape.IsNull()) return;
    BRepMesh_IncrementalMesh(shape, kMeshDeflectionMm, Standard_False, kMeshAngleRad,
                             Standard_True);
    Handle(AIS_Shape) object = new AIS_Shape(shape);
    object->SetDisplayMode(AIS_Shaded);
    object->SetColor(toOcct(colour));
    object->SetTransparency(kGhostTransparency);
    // Hardware, not a drawing of it - no face-boundary ink.
    object->Attributes()->SetFaceBoundaryDraw(Standard_False);
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
    myJointsDrawn = 0;
    if (myContext.IsNull()) return;

    // Fresh on every build - never cached across a themeChanged broadcast.
    const QColor colour = Theme::accent();
    for (const Drawing& drawing : myDrawings) {
        if (!drawing.derivation.ok) continue;   // a broken joint draws nothing
        const std::size_t before = myObjects.size();
        const Joinery::Family family = Joinery::familyOf(drawing.kind);
        for (const Joinery::Item& item : drawing.derivation.items) {
            TopoDS_Shape shape;
            try {
                shape = family == Joinery::Family::Fasteners
                            ? fastenerShape(item)
                            : regionShape(item, drawing.derivation.contact);
            } catch (const Standard_Failure&) {
                // A degenerate frame (an axis parallel to the contact's X) or
                // a primitive the kernel refuses: that one item draws nothing
                // rather than taking the viewport down with it.
                continue;
            }
            addSolid(shape, colour);
        }
        if (myObjects.size() > before) ++myJointsDrawn;
    }
}

bool JointRenderer::show(const std::vector<Drawing>& drawings)
{
    const bool had = clear();
    if (myContext.IsNull()) return had;

    myDrawings = drawings;
    build();
    return had || !myObjects.empty();
}
