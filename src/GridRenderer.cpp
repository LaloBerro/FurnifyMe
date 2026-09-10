#include "GridRenderer.h"

#include "Theme.h"

#include <ElSLib.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <V3d_Viewer.hxx>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// Two planes are the same grid frame only when they agree on all three of
// origin, normal and in-plane X direction: a plane rotated about its own
// normal draws a rotated grid, so comparing normals alone would cache a
// stale rebuild.
bool sameFrame(const gp_Pln& a, const gp_Pln& b)
{
    return a.Position().Direction().IsEqual(b.Position().Direction(), 1.0e-9) &&
           a.Position().XDirection().IsEqual(b.Position().XDirection(), 1.0e-9) &&
           a.Location().Distance(b.Location()) < 1.0e-9;
}

QColor lerp(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// A minimal interactive object whose whole presentation is provided by the
// renderer through a callback-free rebuild: GridRenderer computes the segment
// arrays and this object draws them.
class GridObject : public AIS_InteractiveObject {
public:
    struct Band {
        Handle(Graphic3d_ArrayOfSegments) segments;
        Quantity_Color colour;
        double width = 1.0;
    };
    std::vector<Band> bands;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        for (const Band& band : bands) {
            if (band.segments.IsNull()) continue;
            Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectLine3d) aspect =
                new Graphic3d_AspectLine3d(band.colour, Aspect_TOL_SOLID, band.width);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(band.segments);
        }
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&,
                          const Standard_Integer) override
    {
        // Never pickable.
    }
};

}  // namespace

double GridRenderer::minorStepFor(double cameraDistance, double density)
{
    // Guard rather than trust: Theme::kMinGridDensity..kMaxGridDensity keeps
    // every caller in (0, +inf), but a static function taking a bare double
    // should not divide by (or multiply toward) zero if that guarantee is
    // ever broken upstream.
    const double d = density > 0.0 ? density : 1.0;
    if (cameraDistance < 120.0 * d) return 1.0;
    if (cameraDistance < 2500.0 * d) return 10.0;
    return 100.0;
}

double GridRenderer::firstLineAtOrBelow(double limit, double step)
{
    return std::floor(-limit / step) * step;
}

void GridRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
    if (myContext.IsNull()) return;

    const Handle(V3d_Viewer) viewer = myContext->CurrentViewer();
    if (viewer.IsNull()) return;

    // The grid's own layer - see zLayer() in the header for the full argument.
    // In short: rendered AFTER the bodies so a locked face cannot paint over
    // it, writing NO depth so it can never reject the sketch work that is
    // rendered after it, and depth TESTING so a body genuinely in front of
    // the ground grid still hides it.
    //
    // A layer of ours rather than a stock one: Graphic3d_ZLayerId_Top sits in
    // the right place but its settings are shared with anything else that
    // wants an overlay, and AddZLayer()'s implicit "before Top" placement is
    // a position this file would then be relying on without saying so.
    Graphic3d_ZLayerSettings settings;
    settings.SetName("FurnifyMe work-plane grid");
    settings.SetEnableDepthTest(Standard_True);
    settings.SetEnableDepthWrite(Standard_False);
    settings.SetClearDepth(Standard_False);
    Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_UNKNOWN;
    if (viewer->InsertLayerAfter(layer, settings, Graphic3d_ZLayerId_Default))
        myLayer = layer;
}

void GridRenderer::detach()
{
    if (!myContext.IsNull() && !myGrid.IsNull()) myContext->Remove(myGrid, Standard_False);
    myGrid.Nullify();
    myContext.Nullify();
    myLayer = Graphic3d_ZLayerId_UNKNOWN;
    invalidate();
}

void GridRenderer::invalidate()
{
    // Both halves, not just the step: update()'s early-out is a conjunction,
    // so any single term going false is enough - but `myBuiltExtent = 0.0`
    // also makes `sized` false, and clearing exactly the two fields the guard
    // reads as "nothing has been built" leaves no combination of camera
    // arguments that could still be judged a hit.
    myBuiltStep = 0.0;
    myBuiltExtent = 0.0;
}

bool GridRenderer::update(double cameraDistance, const gp_Pnt& cameraTarget,
                          const gp_Pln& plane, double density)
{
    if (myContext.IsNull()) return false;

    const double step = minorStepFor(cameraDistance, density);
    // Extent: comfortably beyond what a camera at this distance can see of the
    // work plane, snapped to the major step so lines do not crawl on rebuild.
    const double major = step * 10.0;
    double extent = std::clamp(cameraDistance * 6.0, 500.0, 200000.0);
    extent = std::ceil(extent / major) * major;

    // The camera target in the PLANE's own coordinates, so the grid follows
    // the camera across a locked vertical face exactly as it does across the
    // ground. On the ground plane these are X and Y and nothing changes.
    Standard_Real u = 0.0, v = 0.0;
    ElSLib::Parameters(plane, cameraTarget, u, v);
    const double centerU = std::round(u / major) * major;
    const double centerV = std::round(v / major) * major;
    const gp_Pnt center = ElSLib::Value(centerU, centerV, plane);

    // Rebuild only when something visible changes: level, the plane itself,
    // the camera leaving the middle half of the built area, or extent
    // changing by >2x - PLUS the two terms the pool fade added (user
    // feedback round two): the fade circle is sized by the CAMERA DISTANCE
    // and centred on the exact look point, so a zoom that moves the
    // distance past ~25% or a pan that walks the look point a third of the
    // distance away has visibly moved the circle and must rebuild, where
    // the coarse extent/centre terms alone let it lag by up to 2x.
    const gp_Pnt fadeCentre = ElSLib::Value(u, v, plane);
    const bool sameLevel = (step == myBuiltStep);
    const bool samePlane = sameFrame(myBuiltPlane, plane);
    const bool centered = center.Distance(myBuiltCenter) < myBuiltExtent * 0.25;
    const bool sized = myBuiltExtent > 0.0 &&
                       extent < myBuiltExtent * 2.0 && extent > myBuiltExtent * 0.5;
    const bool zoomSteady = myBuiltDistance > 0.0 &&
                            cameraDistance < myBuiltDistance * 1.25 &&
                            cameraDistance > myBuiltDistance * 0.8;
    const bool fadeCentred =
        fadeCentre.Distance(myBuiltFadeCentre) < cameraDistance * 0.3;
    if (sameLevel && samePlane && centered && sized && zoomSteady && fadeCentred)
        return false;

    rebuild(step, centerU, centerV, extent, plane, u - centerU, v - centerV,
            cameraDistance);
    myBuiltStep = step;
    myBuiltCenter = center;
    myBuiltExtent = extent;
    myBuiltPlane = plane;
    myBuiltDistance = cameraDistance;
    myBuiltFadeCentre = fadeCentre;
    return true;
}

void GridRenderer::rebuild(double minorStep, double centerU, double centerV,
                           double extent, const gp_Pln& plane, double fadeCU,
                           double fadeCV, double cameraDistance)
{
    const double major = minorStep * 10.0;
    const QColor background = Theme::viewport();

    // Everything below is laid out in the plane's own (u, v) coordinates and
    // mapped into the world here. That single indirection is the whole of
    // this class's plane support - the fade maths never sees a world axis.
    auto at = [&plane](double u, double v) { return ElSLib::Value(u, v, plane); };

    // One continuous fade, per VERTEX, in place of the three flat-colour
    // rings this function used to draw. The rings blended toward the
    // background in two steps, and both steps - plus the outer cutoff, which
    // stopped at 0.85 rather than 1.0 - were visible as concentric seams.
    // With Graphic3d_ArrayFlags_VertexColor the GPU interpolates colour along
    // each segment, so the grid dissolves into the background instead, with
    // no boundary left to see.
    //
    // A CIRCLE FROM THE CAMERA (user feedback round two - "a circle, where
    // a radius from the camera, then start fading"): the pool's radii are
    // the CAMERA DISTANCE's own multiples, not the built extent's - the
    // extent is clamped and quantized, so radii tied to it drifted off
    // proportion at the clamp ends - and the circle is centred on the EXACT
    // look point (fadeCU/fadeCV, the true target's offset from the snapped
    // build centre), so it sits precisely under the camera rather than up
    // to half a major step aside. Full ink to 0.8x the distance, dissolved
    // by 2.4x, capped inside the built square so the fade always completes
    // before the geometry ends (a fade that outran the carpet would be the
    // hard edge all over again). The corners of the built square fade out
    // entirely before their geometry ends - the acceptable cost of a round
    // pool on a square carpet. Colour only interpolates linearly between a
    // segment's own two endpoints, so each full-length line is cut into
    // chunks and the falloff bends where the function does rather than
    // averaging across the whole line.
    const double fadeEnd = std::min(cameraDistance * 2.4, extent);
    const double fadeStart = std::min(cameraDistance * 0.8, fadeEnd * 0.4);
    auto fadeAt = [&](double du, double dv) {
        const double d = std::hypot(du - fadeCU, dv - fadeCV);
        const double f =
            std::clamp((d - fadeStart) / std::max(fadeEnd - fadeStart, 1.0), 0.0, 1.0);
        return f * f * (3.0 - 2.0 * f);   // smoothstep
    };

    Handle(GridObject) grid = new GridObject();

    // 96 (feedback round three): colour interpolates linearly between a
    // chunk's two endpoints, so every line's brightness profile is a
    // polyline with knots at chunk boundaries - and the knots of
    // neighbouring parallel lines ALIGN, which drew the circle's rim as
    // chevrons at 12 and still faintly at 48. At 96 a chunk is ~2.5% of
    // the built width and the kinks drop below what the eye separates
    // from the antialiasing. The vertex count stays trivial (tens of
    // thousands) and a rebuild is occasional by the staleness guard above.
    constexpr int kChunks = 96;
    const double chunk = 2.0 * extent / kChunks;

    struct Vertex { gp_Pnt p; Quantity_Color c; };

    auto build = [&](const std::vector<Vertex>& verts, const QColor& aspect, double width) {
        if (verts.empty()) return;
        Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(
            static_cast<Standard_Integer>(verts.size()), 0,
            Graphic3d_ArrayFlags_VertexColor);
        for (const Vertex& v : verts) array->AddVertex(v.p, v.c);

        GridObject::Band band;
        band.segments = array;
        // Ignored by the renderer once vertex colours are present, but kept
        // meaningful so Compute()'s aspect never carries a garbage colour.
        band.colour = toOcct(aspect);
        band.width = width;
        grid->bands.push_back(band);
    };

    // One chunked, vertex-faded line at `offset` in each of the two grid
    // directions. A chunk both of whose ends are FULLY faded (exactly 1.0
    // - the clamp makes that exact, so no visible ink is ever truncated)
    // is skipped. The skip is not an optimization, it is CORRECTNESS
    // (feedback round three's own lesson, measured the hard way): a
    // "background-coloured" segment is only nearly background once OCCT's
    // vertex-colour path has been through its own colour curve, and at
    // grazing angles hundreds of such segments land on each horizon pixel
    // - the accumulated near-miss drew broad dark bands across the ground.
    // Beyond the rim nothing may be DRAWN at all; inside it, per-vertex
    // fade at 96-chunk sampling keeps the dissolve smooth.
    auto addLine = [&](const QColor& base, double offset, std::vector<Vertex>& out) {
        for (int i = 0; i < kChunks; ++i) {
            const double a0 = -extent + i * chunk;
            const double a1 = -extent + (i + 1) * chunk;
            const double fv0 = fadeAt(offset, a0), fv1 = fadeAt(offset, a1);
            if (fv0 < 1.0 || fv1 < 1.0) {
                out.push_back({at(centerU + offset, centerV + a0),
                               toOcct(lerp(base, background, fv0))});
                out.push_back({at(centerU + offset, centerV + a1),
                               toOcct(lerp(base, background, fv1))});
            }
            const double fu0 = fadeAt(a0, offset), fu1 = fadeAt(a1, offset);
            if (fu0 < 1.0 || fu1 < 1.0) {
                out.push_back({at(centerU + a0, centerV + offset),
                               toOcct(lerp(base, background, fu0))});
                out.push_back({at(centerU + a1, centerV + offset),
                               toOcct(lerp(base, background, fu1))});
            }
        }
    };

    // Positions are absolute multiples of the step, snapped outward from the
    // clip range - never anchored at the edge, which is not in general a
    // multiple of the step.
    std::vector<Vertex> minorVerts, majorVerts;
    const double first = firstLineAtOrBelow(extent, minorStep);
    for (double offset = first; offset <= extent + minorStep * 0.5; offset += minorStep) {
        if (std::fabs(offset) > extent) continue;
        const bool isMajor =
            std::fmod(std::fabs(offset) + minorStep * 0.25, major) < minorStep * 0.5;
        addLine(isMajor ? Theme::gridMajor() : Theme::gridMinor(), offset,
                isMajor ? majorVerts : minorVerts);
    }
    build(minorVerts, Theme::gridMinor(), 1.0);
    build(majorVerts, Theme::gridMajor(), 1.4);

    // The plane's own two axes through its origin, if that origin is inside
    // the built area. On the ground plane these are the world X and Y axes,
    // which is what they have always been; on a locked face they are the
    // face's own local axes, which is what the grid is measured in. They fade
    // by the same function as the lines around them - an axis that stayed at
    // full strength past the dissolved grid would be the edge all over again.
    if (std::fabs(centerU) < extent && std::fabs(centerV) < extent) {
        auto axis = [&](const QColor& colour, bool isU) {
            std::vector<Vertex> verts;
            for (int i = 0; i < kChunks; ++i) {
                const double a0 = -extent + i * chunk;
                const double a1 = -extent + (i + 1) * chunk;
                // The axis runs through the PLANE's origin, not the grid's
                // centre, so its cross-distance from the fade centre is the
                // centre coordinate itself.
                const double f0 = isU ? fadeAt(a0, centerV) : fadeAt(centerU, a0);
                const double f1 = isU ? fadeAt(a1, centerV) : fadeAt(centerU, a1);
                if (f0 >= 1.0 && f1 >= 1.0) continue;
                if (isU) {
                    verts.push_back({at(centerU + a0, 0.0),
                                     toOcct(lerp(colour, background, f0))});
                    verts.push_back({at(centerU + a1, 0.0),
                                     toOcct(lerp(colour, background, f1))});
                } else {
                    verts.push_back({at(0.0, centerV + a0),
                                     toOcct(lerp(colour, background, f0))});
                    verts.push_back({at(0.0, centerV + a1),
                                     toOcct(lerp(colour, background, f1))});
                }
            }
            build(verts, colour, 1.8);
        };
        axis(Theme::axisX(), true);
        axis(Theme::axisY(), false);
    }

    if (!myGrid.IsNull()) myContext->Remove(myGrid, Standard_False);
    myGrid = grid;
    // The layer is set BEFORE the display, so the presentation is never
    // computed into the default layer and moved afterwards - a rebuild happens
    // on every camera octave, and a one-frame flash of grid over the outline
    // would be exactly the defect this layer exists to remove. Setting it on
    // the object rather than through the context is what makes that possible:
    // AIS_InteractiveObject::SetZLayer stores it on the drawer, and Display
    // reads the drawer.
    if (myLayer != Graphic3d_ZLayerId_UNKNOWN) myGrid->SetZLayer(myLayer);
    // Only actually shown while myVisible - see setVisible(). A rebuild while
    // hidden (render mode moving the camera to frame a shot) still keeps the
    // cache current, so the grid is correct the moment it is shown again
    // rather than one camera move stale.
    if (myVisible) myContext->Display(myGrid, 0, -1, Standard_False);   // mode -1: not selectable
    // No UpdateCurrentViewer() here since the QOpenGLWidget migration - see
    // update()'s own comment on the header. The caller owns the frame.
}

bool GridRenderer::setVisible(bool visible)
{
    if (myVisible == visible) return false;
    myVisible = visible;
    if (myContext.IsNull() || myGrid.IsNull()) return false;   // nothing built yet - update() will honour it
    if (myVisible) myContext->Display(myGrid, 0, -1, Standard_False);
    else           myContext->Erase(myGrid, Standard_False);
    return true;
}
