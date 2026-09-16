#include "SelectionSizesRenderer.h"

#include "Theme.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// The group's dashed outline: the twelve edges of the box, never pickable -
// DimensionLines' shape, one file over.
class DashedBox : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Quantity_Color colour;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (lines.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        group->SetGroupPrimitivesAspect(
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_DASH, 1.5));
        group->AddPrimitiveArray(lines);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}
};

bool sameBox(const ModelingOps::MeasuredBox& a, const ModelingOps::MeasuredBox& b)
{
    const double eps = 1.0e-7;
    return a.centre.Distance(b.centre) < eps && std::abs(a.width - b.width) < eps &&
           std::abs(a.depth - b.depth) < eps && std::abs(a.height - b.height) < eps &&
           a.widthAxis.IsEqual(b.widthAxis, eps) && a.depthAxis.IsEqual(b.depthAxis, eps) &&
           a.heightAxis.IsEqual(b.heightAxis, eps);
}

}  // namespace

void SelectionSizesRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
    for (DimensionRenderer& d : myDims) d.attach(context);
    applyStyle();
}

void SelectionSizesRenderer::detach()
{
    for (DimensionRenderer& d : myDims) d.detach();
    if (!myContext.IsNull() && !myOutline.IsNull()) myContext->Remove(myOutline, Standard_False);
    myOutline.Nullify();
    myContext.Nullify();
    myLayer = Graphic3d_ZLayerId_UNKNOWN;
    myShowing = false;
}

void SelectionSizesRenderer::setZLayer(Graphic3d_ZLayerId layer)
{
    myLayer = layer;
    for (DimensionRenderer& d : myDims) d.setZLayer(layer);
}

void SelectionSizesRenderer::applyStyle()
{
    DimensionRenderer::Style style;
    style.lineColour = myKind == Kind::Group ? &Theme::sizesGroup : &Theme::sizesOneBody;
    style.boxedLabel = true;
    for (DimensionRenderer& d : myDims) d.setStyle(style);
}

std::array<std::string, 3> SelectionSizesRenderer::labelTexts() const
{
    return {myDims[0].labelText(), myDims[1].labelText(), myDims[2].labelText()};
}

bool SelectionSizesRenderer::labelBox(int index, gp_Pnt& anchor, double& halfWidthPx,
                                      double& halfHeightPx) const
{
    if (index < 0 || index > 2 || !myShowing) return false;
    return myDims[index].labelBox(anchor, halfWidthPx, halfHeightPx);
}

bool SelectionSizesRenderer::dimensionLine(int index, gp_Pnt& start, gp_Pnt& end,
                                           gp_Dir& outward) const
{
    if (index < 0 || index > 2 || !myShowing) return false;
    return myDims[index].dimensionLine(start, end, outward);
}

bool SelectionSizesRenderer::clear()
{
    bool changed = false;
    for (DimensionRenderer& d : myDims) changed = d.clear() || changed;
    changed = clearOutline() || changed;
    myShowing = false;
    myHaveLayout = false;
    return changed;
}

bool SelectionSizesRenderer::clearOutline()
{
    if (myOutline.IsNull()) return false;
    if (!myContext.IsNull()) myContext->Remove(myOutline, Standard_False);
    myOutline.Nullify();
    return true;
}

bool SelectionSizesRenderer::reapplyTheme()
{
    if (!myShowing) return false;
    bool changed = false;
    for (DimensionRenderer& d : myDims) changed = d.refresh() || changed;
    if (!myOutline.IsNull()) {
        clearOutline();
        changed = showOutline(myOutlineBox) || changed;
    }
    if (changed) ++myBuilds;
    return changed;
}

bool SelectionSizesRenderer::showOutline(const ModelingOps::MeasuredBox& box)
{
    if (!myOutline.IsNull() && sameBox(box, myOutlineBox)) return false;
    clearOutline();
    myOutlineBox = box;

    Handle(Graphic3d_ArrayOfSegments) segs = new Graphic3d_ArrayOfSegments(24);
    // The twelve edges: for each axis, its four parallel edges.
    for (int a = 0; a < 3; ++a) {
        for (int s1 : {-1, 1}) {
            for (int s2 : {-1, 1}) {
                int sign[3];
                sign[a] = -1;
                sign[(a + 1) % 3] = s1;
                sign[(a + 2) % 3] = s2;
                segs->AddVertex(box.corner(sign[0], sign[1], sign[2]));
                sign[a] = 1;
                segs->AddVertex(box.corner(sign[0], sign[1], sign[2]));
            }
        }
    }
    Handle(DashedBox) obj = new DashedBox();
    obj->lines = segs;
    obj->colour = toOcct(Theme::sizesGroup());
    if (myLayer != Graphic3d_ZLayerId_UNKNOWN) obj->SetZLayer(myLayer);
    myContext->Display(obj, 0, -1, Standard_False);
    // Under the dimension lines and their boxed numbers - see BoxedLabel's
    // own priority in DimensionRenderer.cpp.
    myContext->SetDisplayPriority(obj, Graphic3d_DisplayPriority_Below);
    myOutline = obj;
    return true;
}

bool SelectionSizesRenderer::show(const ModelingOps::MeasuredBox& box, Kind kind,
                                  const gp_Dir& viewDirection, const gp_Dir& viewUp,
                                  double worldPerPixel)
{
    if (myContext.IsNull() || !box.ok) return clear();

    bool changed = false;
    if (myShowing && kind != myKind) changed = clear();
    myKind = kind;
    applyStyle();

    // A unit switch changes every label's text under geometry that did not
    // move, which each line's equal-guard would refuse as "nothing moved" -
    // so the lines already up are refreshed first, DimensionRenderer's own
    // refresh() route.
    if (Measure::displayUnit() != myUnit) {
        myUnit = Measure::displayUnit();
        for (DimensionRenderer& d : myDims) changed = d.refresh() || changed;
    }

    const gp_Vec dir(viewDirection);
    const gp_Vec up(viewUp);
    const gp_Vec right = dir.Crossed(up);

    const gp_Vec axis[3] = {gp_Vec(box.widthAxis), gp_Vec(box.depthAxis), gp_Vec(box.heightAxis)};
    const double half[3] = {0.5 * box.width, 0.5 * box.depth, 0.5 * box.height};

    // Screen directions each dimension is pushed toward: width and depth share
    // bottom and left between them by which runs more across the screen;
    // height goes right.
    //
    // WITH HYSTERESIS, because the app's own default view is the tie: the
    // axonometric camera looks down the diagonal at azimuth -45, where a
    // square-to-the-world box's width and depth run exactly equally across
    // the screen, and the orbit-pacing probe measured the two swapping on
    // every step of a +-5 px wiggle - 60 rebuilds in 60 moves. The previous
    // assignment stands until the other axis runs clearly more across.
    const double widthAcrossness = std::abs(axis[0].Dot(right));
    const double depthAcrossness = std::abs(axis[1].Dot(right));
    constexpr double kSwapMargin = 0.1;
    bool widthAcross = widthAcrossness >= depthAcrossness;
    if (myHaveLayout) {
        widthAcross = myWidthAcross ? widthAcrossness + kSwapMargin >= depthAcrossness
                                    : widthAcrossness > depthAcrossness + kSwapMargin;
    }
    myWidthAcross = widthAcross;
    const gp_Vec push[3] = {widthAcross ? -up : -right, widthAcross ? -right : -up, right};
    // 0 bottom, 1 left, 2 right - what the held edge choice below is keyed on,
    // since `push` itself turns a little with every orbit step.
    const int role[3] = {widthAcross ? 0 : 1, widthAcross ? 1 : 0, 2};

    const double wpp = std::max(worldPerPixel, 1.0e-9);
    for (int a = 0; a < 3; ++a) {
        const int b = (a + 1) % 3, c = (a + 2) % 3;

        // How long the span reads on screen - the component across the view.
        const double across = axis[a].Crossed(dir).Magnitude();
        if (2.0 * half[a] * across / wpp < 4.0) {
            changed = myDims[a].clear() || changed;
            continue;
        }

        // The edge of the four furthest along `push`, ties to the nearer one -
        // and the edge chosen last time keeps a small head start (a few
        // percent of the box), for the same reason the assignment above does:
        // a symmetric view puts two edges level, and a line hopping between
        // them on every orbit step is a rebuild per frame and a flicker.
        gp_Vec bestOffset;
        gp_Vec bestNormal;
        int bestSb = 1, bestSc = 1;
        double bestScore = -1.0e300, bestDepth = 1.0e300;
        const double size = half[0] + half[1] + half[2] + 1.0;
        const double tieEps = 1.0e-6 * size;
        for (int sb : {-1, 1}) {
            for (int sc : {-1, 1}) {
                const gp_Vec offset = axis[b] * (sb * half[b]) + axis[c] * (sc * half[c]);
                double score = offset.Dot(push[a]);
                if (myHaveLayout && myEdge[a][0] == sb && myEdge[a][1] == sc &&
                    myRole[a] == role[a])
                    score += 0.03 * size;
                const double depth = offset.Dot(dir);
                if (score > bestScore + tieEps ||
                    (std::abs(score - bestScore) <= tieEps && depth < bestDepth)) {
                    bestScore = score;
                    bestDepth = depth;
                    bestOffset = offset;
                    bestSb = sb;
                    bestSc = sc;
                }
            }
        }
        {
            // Of the chosen edge's two outward faces, the one pointing furthest
            // along `push` on screen - held the same way when the two are level.
            const gp_Vec nb = axis[b] * double(bestSb);
            const gp_Vec nc = axis[c] * double(bestSc);
            double sbScore = nb.Dot(push[a]), scScore = nc.Dot(push[a]);
            if (myHaveLayout && myEdge[a][0] == bestSb && myEdge[a][1] == bestSc)
                (myNormalAlongB[a] ? sbScore : scScore) += 0.1;
            myNormalAlongB[a] = sbScore >= scScore;
            bestNormal = myNormalAlongB[a] ? nb : nc;
        }
        myEdge[a][0] = bestSb;
        myEdge[a][1] = bestSc;
        myRole[a] = role[a];
        const gp_Pnt mid = box.centre.Translated(bestOffset);
        const gp_Pnt from = mid.Translated(axis[a] * -half[a]);
        const gp_Pnt to = mid.Translated(axis[a] * half[a]);
        changed = myDims[a].show(from, to, gp_Dir(bestNormal), wpp) || changed;
    }

    if (kind == Kind::Group) {
        changed = showOutline(box) || changed;
    } else {
        changed = clearOutline() || changed;
    }

    myShowing = true;
    myHaveLayout = true;
    if (changed) ++myBuilds;
    return changed;
}
