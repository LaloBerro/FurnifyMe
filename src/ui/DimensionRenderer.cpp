#include "DimensionRenderer.h"

#include "Measure.h"
#include "Theme.h"

#include <AIS_TextLabel.hxx>
#include <Aspect_TypeOfDisplayText.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Vec.hxx>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// The app's own DM Sans, registered with OCCT's font manager so the label
// matches every other piece of text instead of falling back to a serif
// face. Font_FontMgr only accepts a real file path, but the font ships
// compiled into the binary as a Qt resource (see Theme.cpp) - so the first
// call here spills it to a real file once and registers that; every later
// call reuses the same resolved family name. Falls back to a system sans
// face if the resource or the registration is ever unavailable - never
// silently back to whatever OCCT's own default happens to be, which is a
// serif face.
const std::string& dimensionFontFamily()
{
    static const std::string family = [] {
        QFile resource(QStringLiteral(":/fonts/DMSans.ttf"));
        if (resource.exists() && resource.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = resource.readAll();
            resource.close();
            const QString diskPath = QDir::tempPath() + QStringLiteral("/FurnifyMe-DMSans.ttf");
            if (!QFileInfo::exists(diskPath) || QFileInfo(diskPath).size() != bytes.size()) {
                QFile disk(diskPath);
                if (disk.open(QIODevice::WriteOnly)) {
                    disk.write(bytes);
                    disk.close();
                }
            }
            const Handle(Font_FontMgr) mgr = Font_FontMgr::GetInstance();
            const Handle(Font_SystemFont) sysFont =
                mgr->CheckFont(diskPath.toUtf8().constData());
            if (!sysFont.IsNull() && mgr->RegisterFont(sysFont, Standard_True)) {
                return std::string(sysFont->FontName().ToCString());
            }
        }
        return std::string("Segoe UI");   // sans fallback - never leave it serif
    }();
    return family;
}

// The extension lines, the dimension line and the two open, two-stroke
// arrowheads - all one segment array, one object, never pickable, same
// shape as GridRenderer's GridObject: the renderer computes the geometry,
// this just draws it. A filled triangle was tried first and rendered
// invisibly (OCCT's shading pipeline evidently needs more setup than
// SetShadingModel(Unlit) alone to light a bare fill-area aspect); an open
// arrowhead built from the same proven line aspect the rest of the
// annotation already uses sidesteps that entirely.
class DimensionLines : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Quantity_Color colour;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (lines.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect =
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID, 1.6);
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(lines);
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

void DimensionRenderer::refresh()
{
    // Only ever redraws what is already on screen: a renderer that could
    // resurrect a cleared annotation would put one back every time the app
    // state changed.
    if (myObjects.empty()) return;
    show(myFrom, myTo, myNormal, myWorldPerPixel);
}

void DimensionRenderer::show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normalIn,
                             double worldPerPixel)
{
    if (myContext.IsNull()) return;

    const double length = from.Distance(to);
    if (length < 1.0e-4) {   // shorter than a hair - never divide by this
        clear();
        return;
    }

    clear();   // drop whatever was drawn before, same as GridRenderer's rebuild

    // Remembered for refresh(), which redraws this same span when the display
    // unit changes under it. The degenerate case above has already returned,
    // so what is stored here is always a span that really is on screen.
    myFrom = from;
    myTo = to;
    myNormal = normalIn;
    myWorldPerPixel = worldPerPixel;

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

    // Every size below is a target in SCREEN PIXELS, converted through
    // worldPerPixel at the point of use - the furniture must read as the
    // same number of pixels whether the camera is close in or pulled all
    // the way back, which is the one thing a fixed millimetre size (the
    // first version of this file used one) cannot do: it either swamps a
    // close-up segment or vanishes on a distant one. Only the span between
    // `from` and `to` itself stays true to world scale.
    const double wpp = std::max(worldPerPixel, 1.0e-9);
    const double gap = 8.0 * wpp;         // clear of the endpoint before the extension line starts
    const double offset = 34.0 * wpp;     // dimension line's distance from the segment
    const double beyond = 8.0 * wpp;      // how far the extension line runs past the dimension line
    const double arrowLen = 16.0 * wpp;
    const double arrowWidth = 7.0 * wpp;
    const double labelGap = 14.0 * wpp;   // further beyond the dimension line, to the label anchor

    const gp_Pnt dimStart = from.Translated(ext * offset);
    const gp_Pnt dimEnd = to.Translated(ext * offset);

    // 3 lines (extension x2, dimension x1) + 2 arrowheads x 2 strokes each =
    // 7 segments, 14 vertices.
    Handle(Graphic3d_ArrayOfSegments) segs = new Graphic3d_ArrayOfSegments(14);
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

    // Open, two-stroke arrowheads: tip at the very end of the dimension
    // line, two strokes fanning back toward the centre - the usual
    // "<---->" look, arrows pointing outward toward the extension lines.
    auto addArrow = [&](const gp_Pnt& tip, const gp_Vec& inward) {
        const gp_Pnt base = tip.Translated(inward * arrowLen);
        addSeg(tip, base.Translated(ext * (arrowWidth * 0.5)));
        addSeg(tip, base.Translated(ext * (-arrowWidth * 0.5)));
    };
    addArrow(dimStart, along);
    addArrow(dimEnd, -along);

    Handle(DimensionLines) linesObj = new DimensionLines();
    linesObj->lines = segs;
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
    const gp_Pnt labelPos = mid.Translated(ext * labelGap);

    Handle(AIS_TextLabel) label = new AIS_TextLabel();
    label->SetText(TCollection_ExtendedString(myLabelText.c_str(), Standard_True));
    label->SetPosition(labelPos);
    label->SetHJustification(Graphic3d_HTA_CENTER);
    label->SetVJustification(Graphic3d_VTA_BOTTOM);
    // Roughly Theme::bodyFont()'s pixel size - the previous 16 read oversized
    // next to furniture this small; now that the furniture itself is a real
    // screen-space size, a smaller label sits proportionate to it.
    label->SetHeight(13.0);
    label->SetColor(toOcct(Theme::text()));
    // The app's own DM Sans if OCCT could resolve it, otherwise a sans
    // fallback - see dimensionFontFamily(). Never left at OCCT's serif
    // default, which is the one string in the app that would otherwise
    // ignore Phase 3's type scale entirely.
    label->SetFont(dimensionFontFamily().c_str());
    // TODT_SUBTITLE paints a filled rectangle behind the text - the "boxed
    // label" the brief calls for, at no extra geometry.
    label->SetDisplayType(Aspect_TODT_SUBTITLE);
    label->SetColorSubTitle(toOcct(Theme::panel()));
    myContext->Display(label, 0, -1, Standard_False);
    myObjects.push_back(label);

    myContext->UpdateCurrentViewer();
}
