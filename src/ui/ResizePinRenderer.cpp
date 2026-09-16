#include "ResizePinRenderer.h"

#include "Theme.h"

#include <Aspect_InteriorStyle.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSegments = 24;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One mark: a filled disc, a ring around it and - on the active one - a solid
// centre dot, all in ONE presentation under zoom-rotate persistence, so the
// whole mark is laid out in device pixels around its world anchor exactly as
// DimensionRenderer's boxed number is. The three groups draw in order (fill,
// ring, dot), which IS the paint order in a layer with no depth test.
class PinMark : public AIS_InteractiveObject {
public:
    double radiusPx = ResizePinRenderer::kMarkRadiusPx;
    bool pinned = false;
    Quantity_Color fill;
    Quantity_Color ink;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        const double r = radiusPx;

        Handle(Graphic3d_Group) fillGroup = presentation->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) fillAspect = new Graphic3d_AspectFillArea3d();
        fillAspect->SetInteriorStyle(Aspect_IS_SOLID);
        fillAspect->SetInteriorColor(fill);
        fillAspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        fillAspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
        fillAspect->SetEdgeOff();
        fillGroup->SetGroupPrimitivesAspect(fillAspect);
        fillGroup->AddPrimitiveArray(disc(r));

        Handle(Graphic3d_Group) ringGroup = presentation->NewGroup();
        ringGroup->SetGroupPrimitivesAspect(
            new Graphic3d_AspectLine3d(ink, Aspect_TOL_SOLID, pinned ? 2.5f : 1.5f));
        Handle(Graphic3d_ArrayOfSegments) ring = new Graphic3d_ArrayOfSegments(kSegments * 2);
        for (int i = 0; i < kSegments; ++i) {
            ring->AddVertex(at(r, i));
            ring->AddVertex(at(r, (i + 1) % kSegments));
        }
        ringGroup->AddPrimitiveArray(ring);

        if (!pinned) return;
        // The pin's own head: a solid dot in the same ink, so the anchored end
        // reads as chosen rather than merely outlined.
        Handle(Graphic3d_Group) dotGroup = presentation->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) dotAspect = new Graphic3d_AspectFillArea3d();
        dotAspect->SetInteriorStyle(Aspect_IS_SOLID);
        dotAspect->SetInteriorColor(ink);
        dotAspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        dotAspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
        dotAspect->SetEdgeOff();
        dotGroup->SetGroupPrimitivesAspect(dotAspect);
        dotGroup->AddPrimitiveArray(disc(r * 0.45));
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable - the press is claimed in screen space by
        // OcctViewWidget, the same way every other handle in this app is.
    }

private:
    static gp_Pnt at(double r, int i)
    {
        const double a = 2.0 * kPi * i / kSegments;
        return gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0);
    }

    static Handle(Graphic3d_ArrayOfTriangles) disc(double r)
    {
        Handle(Graphic3d_ArrayOfTriangles) tris =
            new Graphic3d_ArrayOfTriangles(kSegments + 1, kSegments * 3);
        tris->AddVertex(gp_Pnt(0.0, 0.0, 0.0));
        for (int i = 0; i < kSegments; ++i) tris->AddVertex(at(r, i));
        for (int i = 0; i < kSegments; ++i)
            tris->AddEdges(1, 2 + i, 2 + (i + 1) % kSegments);
        return tris;
    }
};

}  // namespace

void ResizePinRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void ResizePinRenderer::detach()
{
    if (!myContext.IsNull()) {
        for (Handle(AIS_InteractiveObject) & mark : myMarks) {
            if (!mark.IsNull()) myContext->Remove(mark, Standard_False);
        }
    }
    for (Handle(AIS_InteractiveObject) & mark : myMarks) mark.Nullify();
    myContext.Nullify();
    myLayer = Graphic3d_ZLayerId_UNKNOWN;
    myShowing = false;
}

void ResizePinRenderer::setZLayer(Graphic3d_ZLayerId layer)
{
    myLayer = layer;
}

bool ResizePinRenderer::markPoint(int index, gp_Pnt& out) const
{
    if (!myShowing || index < 0 || index > 2) return false;
    out = myPoints[index];
    return true;
}

bool ResizePinRenderer::clear()
{
    if (!myShowing && myMarks[0].IsNull()) return false;
    if (!myContext.IsNull()) {
        for (Handle(AIS_InteractiveObject) & mark : myMarks) {
            if (!mark.IsNull()) myContext->Remove(mark, Standard_False);
        }
    }
    for (Handle(AIS_InteractiveObject) & mark : myMarks) mark.Nullify();
    myShowing = false;
    return true;
}

bool ResizePinRenderer::show(const gp_Pnt& low, const gp_Pnt& centre, const gp_Pnt& high,
                             int active)
{
    if (myContext.IsNull()) return false;
    const int wanted = (active < 0 || active > 2) ? 1 : active;

    // The equal-guard: an orbit that moves neither the marks nor the choice
    // rebuilds nothing at all - DimensionRenderer's own rule.
    if (myShowing && wanted == myActive && myPoints[0].Distance(low) < 1.0e-7 &&
        myPoints[1].Distance(centre) < 1.0e-7 && myPoints[2].Distance(high) < 1.0e-7)
        return false;

    clear();
    myPoints[0] = low;
    myPoints[1] = centre;
    myPoints[2] = high;
    myActive = wanted;
    return build();
}

bool ResizePinRenderer::reapplyTheme()
{
    if (!myShowing) return false;
    const std::array<gp_Pnt, 3> points = myPoints;
    const int active = myActive;
    clear();
    myPoints = points;
    myActive = active;
    return build();
}

bool ResizePinRenderer::build()
{
    if (myContext.IsNull()) return false;
    for (int i = 0; i < 3; ++i) {
        Handle(PinMark) mark = new PinMark();
        mark->pinned = (i == myActive);
        mark->fill = toOcct(Theme::panel());
        // The pin wears the accent; the two alternatives wear the size lines'
        // own token, so they read as part of the dimension rather than as two
        // more controls.
        mark->ink = toOcct(mark->pinned ? Theme::accent() : Theme::sizesOneBody());
        mark->radiusPx = mark->pinned ? kMarkRadiusPx : kMarkRadiusPx * 0.7;
        mark->SetTransformPersistence(
            new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, myPoints[i]));
        if (myLayer != Graphic3d_ZLayerId_UNKNOWN) mark->SetZLayer(myLayer);
        myContext->Display(mark, 0, -1, Standard_False);
        myContext->SetDisplayPriority(mark, Graphic3d_DisplayPriority_Above);
        myMarks[i] = mark;
    }
    myShowing = true;
    return true;
}
