#include "BooleanBadgeRenderer.h"

#include "DimensionRenderer.h"
#include "Theme.h"

#include <Aspect_InteriorStyle.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One badge: a filled pill, its border and its word, in ONE presentation
// under zoom-rotate persistence anchored on the body it names. Structurally
// DimensionRenderer's BoxedLabel - the same three groups in the same order,
// fill then border then text, which IS the paint order in a layer that does
// not depth-test - with a rounded end rather than a square one so a badge can
// never be mistaken for the boxed NUMBER that gesture also puts on screen.
class BadgePill : public AIS_InteractiveObject {
public:
    std::string text;
    double halfWidthPx = BooleanBadgeRenderer::kHalfWidthPx;
    double halfHeightPx = BooleanBadgeRenderer::kHalfHeightPx;
    Quantity_Color fill;
    Quantity_Color border;
    Quantity_Color ink;
    std::string font;
    float textHeight = 12.0f;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        const double hw = halfWidthPx;
        const double hh = halfHeightPx;
        // A stadium: the straight run plus a half-circle on each end. The
        // radius IS the half-height, the same rule AppBar's pill follows, so
        // the fill and the curve can never disagree.
        const int arc = 10;

        std::vector<gp_Pnt> outline;
        outline.reserve(arc * 2 + 2);
        for (int i = 0; i <= arc; ++i) {
            const double a = kPiHalf - kPi * i / arc;  // right end, top to bottom
            outline.push_back(gp_Pnt(hw - hh + hh * std::cos(a), hh * std::sin(a), 0.0));
        }
        for (int i = 0; i <= arc; ++i) {
            const double a = kPiHalf + kPi * i / arc;  // left end, top to bottom
            outline.push_back(gp_Pnt(-hw + hh + hh * std::cos(a), hh * std::sin(a), 0.0));
        }

        Handle(Graphic3d_Group) fillGroup = presentation->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) fillAspect = new Graphic3d_AspectFillArea3d();
        fillAspect->SetInteriorStyle(Aspect_IS_SOLID);
        fillAspect->SetInteriorColor(fill);
        fillAspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        // Double-sided, BoxedLabel's own reason: which way a screen-aligned
        // quad's winding faces is the persistence's business, and a culled
        // badge is an invisible one.
        fillAspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
        fillAspect->SetEdgeOff();
        fillGroup->SetGroupPrimitivesAspect(fillAspect);

        const int n = static_cast<int>(outline.size());
        Handle(Graphic3d_ArrayOfTriangles) fan =
            new Graphic3d_ArrayOfTriangles(n + 1, (n - 1) * 3);
        fan->AddVertex(gp_Pnt(0.0, 0.0, 0.0));
        for (const gp_Pnt& p : outline) fan->AddVertex(p);
        for (int i = 1; i < n; ++i) fan->AddEdges(1, i + 1, i + 2);
        fillGroup->AddPrimitiveArray(fan);

        Handle(Graphic3d_Group) borderGroup = presentation->NewGroup();
        borderGroup->SetGroupPrimitivesAspect(
            new Graphic3d_AspectLine3d(border, Aspect_TOL_SOLID, 2.0f));
        Handle(Graphic3d_ArrayOfSegments) rim = new Graphic3d_ArrayOfSegments(n * 2);
        for (int i = 0; i < n; ++i) {
            rim->AddVertex(outline[i]);
            rim->AddVertex(outline[(i + 1) % n]);
        }
        borderGroup->AddPrimitiveArray(rim);

        Handle(Graphic3d_Group) textGroup = presentation->NewGroup();
        textGroup->SetGroupPrimitivesAspect(
            new Graphic3d_AspectText3d(ink, font.c_str(), 1.0, 0.0));
        Handle(Graphic3d_Text) label = new Graphic3d_Text(textHeight);
        label->SetText(text.c_str());
        label->SetPosition(gp_Pnt(0.0, 0.0, 0.0));
        label->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        label->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        textGroup->AddText(label, false);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable. The press is claimed in SCREEN space by
        // OcctViewWidget, exactly as every other handle in this app is - an
        // AIS owner here would enter the pick pipeline and start competing
        // with the bodies the gesture is about.
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kPiHalf = kPi / 2.0;
};

}  // namespace

void BooleanBadgeRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void BooleanBadgeRenderer::detach()
{
    // Drops everything WITHOUT touching the viewer - releaseGlResources()
    // runs while the context is going away, and a Remove() there would reach
    // through a half-destroyed one.
    myMarks.clear();
    myBadges.clear();
    myContext.Nullify();
}

void BooleanBadgeRenderer::setZLayer(Graphic3d_ZLayerId layer)
{
    myLayer = layer;
}

bool BooleanBadgeRenderer::show(const std::vector<Badge>& badges)
{
    // The equal-guard, and it earns its keep: this runs on every frame of an
    // orbit, and rebuilding three presentations per frame is a real
    // allocation per frame. Compared on everything drawn - where each badge
    // stands, which one is kept, and the word - because all three are built
    // into the presentation.
    if (badges.size() == myBadges.size()) {
        bool same = true;
        for (std::size_t i = 0; i < badges.size() && same; ++i) {
            same = badges[i].keep == myBadges[i].keep && badges[i].text == myBadges[i].text &&
                   badges[i].bodyId == myBadges[i].bodyId &&
                   badges[i].at.IsEqual(myBadges[i].at, 1.0e-7);
        }
        if (same) return false;
    }

    clear();
    myBadges = badges;
    if (myBadges.empty()) return true;
    return build();
}

bool BooleanBadgeRenderer::clear()
{
    if (myMarks.empty() && myBadges.empty()) return false;
    if (!myContext.IsNull()) {
        for (const Handle(AIS_InteractiveObject)& mark : myMarks) {
            if (!mark.IsNull()) myContext->Remove(mark, Standard_False);
        }
    }
    myMarks.clear();
    myBadges.clear();
    return true;
}

bool BooleanBadgeRenderer::reapplyTheme()
{
    if (myBadges.empty()) return false;
    // Rebuild rather than recolour: the colours are baked into each
    // presentation's aspects, which is the same reason DimensionRenderer
    // rebuilds on a theme change rather than reaching into its groups.
    const std::vector<Badge> kept = myBadges;
    clear();
    myBadges = kept;
    return build();
}

bool BooleanBadgeRenderer::build()
{
    if (myContext.IsNull()) return false;
    myMarks.reserve(myBadges.size());
    for (const Badge& badge : myBadges) {
        Handle(BadgePill) pill = new BadgePill();
        pill->text = badge.text;
        // The panel ground under both, so a badge reads as a control of this
        // app rather than as a coloured blob on the wood; the role's own
        // token carries the border and the word.
        pill->fill = toOcct(Theme::panel());
        const QColor role = badge.keep ? Theme::booleanStay() : Theme::booleanOut();
        pill->border = toOcct(role);
        pill->ink = toOcct(role);
        // The same family every other scene label uses, resolved once and
        // registered with OCCT by DimensionRenderer - a family
        // Font_FontMgr cannot resolve draws nothing at all.
        pill->font = DimensionRenderer::fontFamily();
        pill->SetTransformPersistence(
            new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, badge.at));
        if (myLayer != Graphic3d_ZLayerId_UNKNOWN) pill->SetZLayer(myLayer);
        myContext->Display(pill, 0, -1, Standard_False);
        myContext->SetDisplayPriority(pill, Graphic3d_DisplayPriority_Above);
        myMarks.push_back(pill);
    }
    return true;
}

bool BooleanBadgeRenderer::badgePoint(int index, gp_Pnt& out) const
{
    if (index < 0 || index >= static_cast<int>(myBadges.size())) return false;
    out = myBadges[static_cast<std::size_t>(index)].at;
    return true;
}

int BooleanBadgeRenderer::badgeBodyId(int index) const
{
    if (index < 0 || index >= static_cast<int>(myBadges.size())) return 0;
    return myBadges[static_cast<std::size_t>(index)].bodyId;
}

bool BooleanBadgeRenderer::badgeKeeps(int index) const
{
    if (index < 0 || index >= static_cast<int>(myBadges.size())) return false;
    return myBadges[static_cast<std::size_t>(index)].keep;
}

std::vector<std::string> BooleanBadgeRenderer::paintedTexts() const
{
    std::vector<std::string> texts;
    texts.reserve(myBadges.size());
    for (const Badge& badge : myBadges) texts.push_back(badge.text);
    return texts;
}
