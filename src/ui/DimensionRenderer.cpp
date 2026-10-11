#include "DimensionRenderer.h"
#include "Themes.h"
#include <QHash>
#include <QGuiApplication>
#include <vector>

#include "Measure.h"
#include "Theme.h"

#include <AIS_TextLabel.hxx>
#include <Aspect_TypeOfDisplayText.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_TransformPers.hxx>
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
#include <QFont>
#include <QFontMetricsF>

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
// A bundled face (":/fonts/<file>") registered with OCCT once, by its file
// name - a theme's labels are drawn in its own typeface (themes part 2).
// Empty when the face cannot be registered: the caller keeps DM Sans.
std::string resolveBundledFont(const QString& file)
{
    static QHash<QString, std::string> known;
    if (known.contains(file)) return known.value(file);
    std::string name;
    QFile resource(QStringLiteral(":/fonts/") + file);
    if (resource.exists() && resource.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = resource.readAll();
        const QString diskPath = QDir::tempPath() + QStringLiteral("/FurnifyMe-") + file;
        if (!QFileInfo::exists(diskPath) || QFileInfo(diskPath).size() != bytes.size()) {
            QFile disk(diskPath);
            if (disk.open(QIODevice::WriteOnly)) disk.write(bytes);
        }
        const Handle(Font_FontMgr) mgr = Font_FontMgr::GetInstance();
        const Handle(Font_SystemFont) sysFont = mgr->CheckFont(diskPath.toUtf8().constData());
        if (!sysFont.IsNull() && mgr->RegisterFont(sysFont, Standard_True)) name = sysFont->FontName().ToCString();
    }
    known.insert(file, name);
    return name;
}

const std::string& resolveFontFamily()
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

// The boxed number the selection sizes wear (DimensionRenderer::Style::
// boxedLabel): a filled box, a border and the text, all in ONE presentation
// under zoom-and-rotate transformation persistence anchored on the label's
// world point. Under that persistence the local frame is the SCREEN's - x
// right, y up - and one local unit is one device pixel
// (Graphic3d_TransformPers::persistentScale() is the world size of a pixel at
// the anchor's depth), so the box is laid out in pixels around the anchor and
// stays that size at every zoom, exactly like AIS_TextLabel's own text.
//
// The three groups draw in order - fill, border, text - which is what puts
// the number over its own box: the layer this is displayed in has no depth
// test (see OcctViewWidget's sizes layer), so display order IS paint order.
class BoxedLabel : public AIS_InteractiveObject {
public:
    // A theme's shape (themes part 2): corner radius, rim width, bold text,
    // and Ultraviolet's KPI layout - a caption over a large numeral.
    double radiusPx = 0.0;
    double edgeWidth = 2.0;
    bool bold = false;
    bool kpi = false;
    std::string caption;
    std::string captionFont;
    float captionHeight = 11.0f;
    std::string number;
    std::string unit;
    float numberHeight = 32.0f;
    double padX = 12.0;
    double scale = 1.0;   // device pixels per CSS pixel, for the KPI's own offsets
    std::string text;
    double halfWidthPx = 30.0;
    double halfHeightPx = 11.0;
    Quantity_Color fill;
    Quantity_Color border;
    Quantity_Color ink;
    std::string font;
    float textHeight = 13.0f;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        const double hw = halfWidthPx, hh = halfHeightPx;
        // The outline: a rectangle, or its corners rounded (8 steps each) - a
        // convex shape, so the fill is a fan from the centre.
        std::vector<gp_Pnt> outline;
        const double r = std::min(radiusPx, std::min(hw, hh));
        if (r > 0.5) {
            const double cx[4] = {hw - r, -hw + r, -hw + r, hw - r};
            const double cy[4] = {hh - r, hh - r, -hh + r, -hh + r};
            for (int corner = 0; corner < 4; ++corner) {
                for (int step = 0; step <= 8; ++step) {
                    const double a = (corner * 90.0 + step * 90.0 / 8.0) * 3.14159265358979323846 / 180.0;
                    outline.emplace_back(cx[corner] + r * std::cos(a), cy[corner] + r * std::sin(a), 0.0);
                }
            }
        } else {
            outline = {gp_Pnt(hw, hh, 0.0), gp_Pnt(-hw, hh, 0.0), gp_Pnt(-hw, -hh, 0.0), gp_Pnt(hw, -hh, 0.0)};
        }

        Handle(Graphic3d_Group) fillGroup = presentation->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) fillAspect = new Graphic3d_AspectFillArea3d();
        fillAspect->SetInteriorStyle(Aspect_IS_SOLID);
        fillAspect->SetInteriorColor(fill);
        fillAspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        // Double-sided: which way a screen-aligned quad's winding faces is the
        // persistence's business, and a culled box is an invisible one.
        fillAspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
        fillAspect->SetEdgeOff();
        fillGroup->SetGroupPrimitivesAspect(fillAspect);
        const int n = int(outline.size());
        Handle(Graphic3d_ArrayOfTriangles) fan = new Graphic3d_ArrayOfTriangles(n + 1, 3 * n);
        fan->AddVertex(gp_Pnt(0.0, 0.0, 0.0));
        for (const gp_Pnt& p : outline) fan->AddVertex(p);
        for (int i = 0; i < n; ++i) fan->AddEdges(1, 2 + i, 2 + (i + 1) % n);
        fillGroup->AddPrimitiveArray(fan);

        Handle(Graphic3d_Group) borderGroup = presentation->NewGroup();
        borderGroup->SetGroupPrimitivesAspect(
            new Graphic3d_AspectLine3d(border, Aspect_TOL_SOLID, edgeWidth));
        Handle(Graphic3d_ArrayOfSegments) rim = new Graphic3d_ArrayOfSegments(2 * n);
        for (int i = 0; i < n; ++i) {
            rim->AddVertex(outline[size_t(i)]);
            rim->AddVertex(outline[size_t((i + 1) % n)]);
        }
        borderGroup->AddPrimitiveArray(rim);

        auto addText = [&](const std::string& s, const std::string& face, float height, const gp_Pnt& at,
                           Graphic3d_HorizontalTextAlignment h, Graphic3d_VerticalTextAlignment v, bool strong,
                           const Quantity_Color& colour) {
            Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectText3d) aspect = new Graphic3d_AspectText3d(colour, face.c_str(), 1.0, 0.0);
            if (strong) aspect->SetTextFontAspect(Font_FA_Bold);
            group->SetGroupPrimitivesAspect(aspect);
            Handle(Graphic3d_Text) t = new Graphic3d_Text(height);
            t->SetText(s.c_str());
            t->SetPosition(at);
            t->SetHorizontalAlignment(h);
            t->SetVerticalAlignment(v);
            group->AddText(t, false);
        };
        if (kpi) {
            // Ultraviolet's KPI: the caption up top, the numeral and its unit
            // below, all from the left padding.
            addText(caption, captionFont, captionHeight, gp_Pnt(-hw + padX, hh - 10.0 * scale, 0.0), Graphic3d_HTA_LEFT,
                    Graphic3d_VTA_CENTER, false, captionInk);
            addText(number, font, numberHeight, gp_Pnt(-hw + padX, -hh + 10.0 * scale, 0.0), Graphic3d_HTA_LEFT,
                    Graphic3d_VTA_BOTTOM, false, ink);
            addText(unit, captionFont, captionHeight, gp_Pnt(-hw + padX + numberWidth + 4.0 * scale, -hh + 12.0 * scale, 0.0),
                    Graphic3d_HTA_LEFT, Graphic3d_VTA_BOTTOM, false, captionInk);
        } else {
            addText(text, font, textHeight, gp_Pnt(0.0, 0.0, 0.0), Graphic3d_HTA_CENTER, Graphic3d_VTA_CENTER, bold,
                    ink);
        }
    }
    Quantity_Color captionInk;
    double numberWidth = 0.0;

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable, for DimensionLines' reason.
    }
};

}  // namespace

const std::string& DimensionRenderer::fontFamily()
{
    return resolveFontFamily();
}

void DimensionRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void DimensionRenderer::detach()
{
    if (!myContext.IsNull()) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
    }
    myObjects.clear();
    myLabelText.clear();
    myContext.Nullify();
    myLayer = Graphic3d_ZLayerId_UNKNOWN;
}

bool DimensionRenderer::clear()
{
    const bool had = !myObjects.empty();
    if (!myContext.IsNull() && had) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
        // No UpdateCurrentViewer() since the QOpenGLWidget migration: OCCT does
        // not own the surface any more, so a redraw from an ordinary Qt slot
        // has no Qt context current and nothing composites it. The caller owns
        // the frame - see OcctViewWidget::scheduleRedraw().
    }
    myObjects.clear();
    myLabelText.clear();
    return had;
}

bool DimensionRenderer::refresh()
{
    // Only ever redraws what is already on screen: a renderer that could
    // resurrect a cleared annotation would put one back every time the app
    // state changed.
    if (myObjects.empty()) return false;
    // Past show()'s equal-guard deliberately - refresh() exists precisely to
    // rebuild the SAME span when the display unit changed under it, which the
    // guard would otherwise refuse as "nothing moved".
    myForceRebuild = true;
    return show(myFrom, myTo, myNormal, myWorldPerPixel);
}

bool DimensionRenderer::labelBox(gp_Pnt& anchor, double& halfWidthPx, double& halfHeightPx) const
{
    if (!isShowing() || !myHasLabelBox) return false;
    anchor = myLabelAnchor;
    halfWidthPx = myLabelHalfWidthPx;
    halfHeightPx = myLabelHalfHeightPx;
    return true;
}

bool DimensionRenderer::dimensionLine(gp_Pnt& start, gp_Pnt& end, gp_Dir& outward) const
{
    if (!isShowing()) return false;
    start = myDimStart;
    end = myDimEnd;
    outward = myDimOutward;
    return true;
}

bool DimensionRenderer::show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normalIn,
                             double worldPerPixel)
{
    const bool force = myForceRebuild;
    myForceRebuild = false;

    if (myContext.IsNull()) return false;

    const double length = from.Distance(to);
    if (length < 1.0e-4) {   // shorter than a hair - never divide by this
        return clear();
    }

    // The equal-guard - see the header. Everything the built annotation
    // depends on is compared, not merely the endpoints: the normal orients the
    // extension lines and worldPerPixel sizes every piece of furniture on
    // them, so a rebuild is owed if either moved. Tolerances rather than
    // equality because all four come from projected floating-point geometry
    // that a still cursor still jitters by a last bit.
    if (!force && isShowing() &&
        from.Distance(myFrom) < 1.0e-7 && to.Distance(myTo) < 1.0e-7 &&
        normalIn.IsEqual(myNormal, 1.0e-7) &&
        std::abs(worldPerPixel - myWorldPerPixel) < 1.0e-9) {
        return false;
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
    // Recorded where they are BUILT - see dimensionLine() on the header. The
    // outward direction is `ext` itself, which is the caller's normal already
    // projected perpendicular to the span (and replaced outright when it came
    // in parallel), so it is the one this annotation was really drawn along
    // rather than the one that was asked for.
    myDimStart = dimStart;
    myDimEnd = dimEnd;
    myDimOutward = gp_Dir(ext);
    myHasLabelBox = false;

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
    // Theme::dimensionLine(), not accent() (improvements item 17): a
    // measurement is a reading of the wood, and accent is what marks state.
    // The selection sizes still pass their OWN token through myStyle, which
    // is what that hook is for.
    linesObj->colour =
        toOcct(myStyle.lineColour ? myStyle.lineColour() : Theme::dimensionLine());
    if (myLayer != Graphic3d_ZLayerId_UNKNOWN) linesObj->SetZLayer(myLayer);
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

    if (myStyle.boxedLabel) {
        // Centred ON the dimension line's midpoint rather than beside it: the
        // number interrupts the line, a drawing's own convention, and stays
        // readable from any angle - "beside" along `ext` collapses onto the
        // line whenever `ext` points into the screen.
        Handle(BoxedLabel) boxed = new BoxedLabel();
        boxed->text = myLabelText;
        boxed->font = fontFamily();
        boxed->textHeight = 13.0f;
        // The box is sized from the text measured in the SAME face at the SAME
        // pixel height OCCT draws it at, so a longer number gets a longer box.
        QFont face(QString::fromStdString(fontFamily()));
        face.setPixelSize(13);
        const double textWidth =
            QFontMetricsF(face).horizontalAdvance(QString::fromStdString(myLabelText));
        boxed->halfWidthPx = 0.5 * textWidth + 8.0;
        boxed->halfHeightPx = 11.0;
        boxed->fill = toOcct(Theme::panel());
        // A THEME'S LABEL (themes part 2): the mockup's pill or box, in its
        // own face; Ultraviolet's KPI with its caption.
        if (const Themes::Label& t = Themes::shell().label; t.on) {
            // The mockup's sizes are CSS pixels, and this frame's unit is a
            // DEVICE pixel - so they are scaled by the display's ratio, or a
            // 150% display draws every label at two thirds of its size.
            const double dpr = myStyle.deviceRatio > 0.0 ? myStyle.deviceRatio : 1.0;
            std::string faceName = t.fontFile.isEmpty() ? fontFamily() : resolveBundledFont(t.fontFile);
            if (faceName.empty()) faceName = fontFamily();
            QFont measured(QString::fromStdString(faceName));
            measured.setBold(t.bold);
            boxed->font = faceName;
            boxed->bold = t.bold;
            boxed->radiusPx = t.radius * dpr;
            boxed->edgeWidth = t.edgeWidth * dpr;
            boxed->padX = t.padX * dpr;
            boxed->scale = dpr;
            if (t.fill.isValid()) boxed->fill = toOcct(t.fill);
            if (t.kpi) {
                const QString full = QString::fromStdString(myLabelText);
                const int space = full.lastIndexOf(QLatin1Char(' '));
                const QString number = space > 0 ? full.left(space) : full;
                const QString unit = space > 0 ? full.mid(space + 1).toUpper() : QString();
                std::string capFace = t.captionFontFile.isEmpty() ? faceName : resolveBundledFont(t.captionFontFile);
                if (capFace.empty()) capFace = faceName;
                boxed->kpi = true;
                boxed->number = number.toStdString();
                boxed->unit = unit.toStdString();
                boxed->caption = myStyle.caption.toUpper().toStdString();
                boxed->captionFont = capFace;
                boxed->captionHeight = float(t.captionPx * dpr);
                boxed->numberHeight = float(t.kpiTextPx * dpr);
                boxed->captionInk = toOcct(Theme::textMuted());
                measured.setPixelSize(int(std::lround(t.kpiTextPx * dpr)));
                QFont cap(QString::fromStdString(capFace));
                cap.setPixelSize(int(std::lround(t.captionPx * dpr)));
                boxed->numberWidth = QFontMetricsF(measured).horizontalAdvance(number);
                const double inner = std::max(QFontMetricsF(cap).horizontalAdvance(QString::fromStdString(boxed->caption)),
                                              boxed->numberWidth + 4.0 * dpr + QFontMetricsF(cap).horizontalAdvance(unit));
                boxed->halfWidthPx = 0.5 * inner + t.padX * dpr;
                boxed->halfHeightPx = 0.5 * t.heightPx * dpr;
            } else {
                boxed->textHeight = float(t.textPx * dpr);
                measured.setPixelSize(int(std::lround(t.textPx * dpr)));
                boxed->halfWidthPx =
                    0.5 * QFontMetricsF(measured).horizontalAdvance(QString::fromStdString(myLabelText)) + t.padX * dpr;
                boxed->halfHeightPx = 0.5 * t.heightPx * dpr;
            }
        }
        boxed->border =
            toOcct(myStyle.lineColour ? myStyle.lineColour() : Theme::dimensionLine());
        boxed->ink = toOcct(Theme::text());
        boxed->SetTransformPersistence(
            new Graphic3d_TransformPers(Graphic3d_TMF_ZoomRotatePers, mid));
        // Above every line in the layer, not merely its own: with no depth
        // test, a later-displayed line (another dimension, the group's dashed
        // outline) would otherwise strike through the number - measured on
        // the first group capture, where the outline crossed "300 mm".
        if (myLayer != Graphic3d_ZLayerId_UNKNOWN) boxed->SetZLayer(myLayer);
        myContext->Display(boxed, 0, -1, Standard_False);
        myContext->SetDisplayPriority(boxed, Graphic3d_DisplayPriority_Above);
        myObjects.push_back(boxed);
        // Same numbers the box was built from, not a second measurement of
        // the same string - see labelBox() on the header.
        myLabelAnchor = mid;
        myLabelHalfWidthPx = boxed->halfWidthPx;
        myLabelHalfHeightPx = boxed->halfHeightPx;
        myHasLabelBox = true;
        return true;
    }

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
    // fallback - see fontFamily(). Never left at OCCT's serif
    // default, which is the one string in the app that would otherwise
    // ignore Phase 3's type scale entirely.
    label->SetFont(fontFamily().c_str());
    // TODT_SUBTITLE paints a filled rectangle behind the text - the "boxed
    // label" the brief calls for, at no extra geometry.
    label->SetDisplayType(Aspect_TODT_SUBTITLE);
    label->SetColorSubTitle(toOcct(Theme::panel()));
    if (myLayer != Graphic3d_ZLayerId_UNKNOWN) label->SetZLayer(myLayer);
    myContext->Display(label, 0, -1, Standard_False);
    myObjects.push_back(label);

    // No UpdateCurrentViewer() - see clear()'s own comment. The caller owns
    // the frame, and only asks for one because this returned true.
    return true;
}
