#include "DimensionRenderer.h"

#include "Measure.h"
#include "Theme.h"

#include <AIS_TextLabel.hxx>
#include <Aspect_TypeOfDisplayText.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// The extension lines, the dimension line and the two arrowheads - one
// object for all of it, never pickable, same shape as GridRenderer's
// GridObject: the renderer computes the geometry, this just draws it.
class DimensionLines : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Handle(Graphic3d_ArrayOfTriangles) arrows;
    Quantity_Color colour;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (!lines.IsNull()) {
            Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectLine3d) aspect =
                new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID, 1.4);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(lines);
        }
        if (!arrows.IsNull()) {
            Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
            aspect->SetInteriorColor(colour);
            // Flat colour regardless of scene lighting, and visible from
            // either side - an arrowhead facing away from the light should
            // never read as a different shade than its twin.
            aspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
            aspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(arrows);
        }
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable - an annotation must not steal a click meant for
        // the geometry underneath it.
    }
};

}  // namespace

void DimensionRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void DimensionRenderer::clear()
{
    if (!myContext.IsNull() && !myObjects.empty()) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
        myContext->UpdateCurrentViewer();
    }
    myObjects.clear();
    myLabelText.clear();
}

void DimensionRenderer::show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normalIn)
{
    if (myContext.IsNull()) return;

    const double length = from.Distance(to);
    if (length < 1.0e-4) {   // shorter than a hair - never divide by this
        clear();
        return;
    }

    clear();   // drop whatever was drawn before, same as GridRenderer's rebuild

    const gp_Dir dir(gp_Vec(from, to));

    // Project the caller's normal onto the plane perpendicular to the
    // segment, so a direction that is only roughly sideways still produces
    // straight extension lines. Falls back to an arbitrary perpendicular if
    // it turned out parallel to the segment instead.
    gp_Vec extVec = gp_Vec(normalIn) - gp_Vec(dir) * gp_Vec(normalIn).Dot(gp_Vec(dir));
    if (extVec.Magnitude() < 1.0e-7) {
        extVec = gp_Vec(dir).Crossed(gp_Vec(0.0, 0.0, 1.0));
        if (extVec.Magnitude() < 1.0e-7) extVec = gp_Vec(dir).Crossed(gp_Vec(1.0, 0.0, 0.0));
    }
    const gp_Vec ext = gp_Vec(gp_Dir(extVec));
    const gp_Vec along = gp_Vec(dir);

    // Sizes scale gently with length: a short in-progress segment gets a
    // proportionally smaller annotation instead of one that swamps it, and a
    // long one never gets a vanishingly small arrowhead.
    const double gap = std::min(4.0, length * 0.1);
    const double offset = std::clamp(length * 0.15, 6.0, 30.0);
    const double beyond = 4.0;
    const double arrowLen = std::min(8.0, length * 0.25);
    const double arrowWidth = arrowLen * 0.4;

    const gp_Pnt dimStart = from.Translated(ext * offset);
    const gp_Pnt dimEnd = to.Translated(ext * offset);

    Handle(Graphic3d_ArrayOfSegments) segs = new Graphic3d_ArrayOfSegments(6);
    auto addSeg = [&](const gp_Pnt& a, const gp_Pnt& b) {
        segs->AddVertex(a);
        segs->AddVertex(b);
    };
    // Extension lines: a small gap off each endpoint, out past the
    // dimension line so the arrowheads have somewhere to land.
    addSeg(from.Translated(ext * gap), from.Translated(ext * (offset + beyond)));
    addSeg(to.Translated(ext * gap), to.Translated(ext * (offset + beyond)));
    // The dimension line itself.
    addSeg(dimStart, dimEnd);

    Handle(Graphic3d_ArrayOfTriangles) arrows = new Graphic3d_ArrayOfTriangles(6);
    // Tip at the very end of the dimension line, flaring back toward the
    // centre - the usual "<---->" look, arrows pointing outward.
    auto addArrow = [&](const gp_Pnt& tip, const gp_Vec& inward) {
        const gp_Pnt base = tip.Translated(inward * arrowLen);
        arrows->AddVertex(tip);
        arrows->AddVertex(base.Translated(ext * (arrowWidth * 0.5)));
        arrows->AddVertex(base.Translated(ext * (-arrowWidth * 0.5)));
    };
    addArrow(dimStart, along);
    addArrow(dimEnd, -along);

    Handle(DimensionLines) linesObj = new DimensionLines();
    linesObj->lines = segs;
    linesObj->arrows = arrows;
    linesObj->colour = toOcct(Theme::accent());
    myContext->Display(linesObj, 0, -1, Standard_False);   // mode -1: feedback only, never pickable
    myObjects.push_back(linesObj);

    // Boxed label, centred above the dimension line's midpoint. The text
    // itself is Measure::formatLength() - never a local format call - so it
    // reads in whatever unit the user has chosen.
    myLabelText = Measure::formatLength(length);
    const gp_Pnt mid(0.5 * (dimStart.X() + dimEnd.X()), 0.5 * (dimStart.Y() + dimEnd.Y()),
                     0.5 * (dimStart.Z() + dimEnd.Z()));
    // Clear of the dimension line, with the anchor on the BOTTOM of the text
    // rather than its centre - centring it on the anchor would let the box's
    // own height straddle back down onto the line it is meant to sit above.
    // The gap itself is generous rather than tuned to AIS_TextLabel's exact
    // on-screen text height, which this class has no way to query back.
    const gp_Pnt labelPos = mid.Translated(ext * 16.0);

    Handle(AIS_TextLabel) label = new AIS_TextLabel();
    label->SetText(TCollection_ExtendedString(myLabelText.c_str(), Standard_True));
    label->SetPosition(labelPos);
    label->SetHJustification(Graphic3d_HTA_CENTER);
    label->SetVJustification(Graphic3d_VTA_BOTTOM);
    label->SetHeight(16.0);
    label->SetColor(toOcct(Theme::text()));
    // TODT_SUBTITLE paints a filled rectangle behind the text - the "boxed
    // label" the brief calls for, at no extra geometry.
    label->SetDisplayType(Aspect_TODT_SUBTITLE);
    label->SetColorSubTitle(toOcct(Theme::panel()));
    myContext->Display(label, 0, -1, Standard_False);
    myObjects.push_back(label);

    myContext->UpdateCurrentViewer();
}
