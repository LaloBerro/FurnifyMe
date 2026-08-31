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

double GridRenderer::minorStepFor(double cameraDistance)
{
    if (cameraDistance < 120.0) return 1.0;
    if (cameraDistance < 2500.0) return 10.0;
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

void GridRenderer::update(double cameraDistance, const gp_Pnt& cameraTarget,
                          const gp_Pln& plane)
{
    if (myContext.IsNull()) return;

    const double step = minorStepFor(cameraDistance);
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
    // changing by >2x.
    const bool sameLevel = (step == myBuiltStep);
    const bool samePlane = sameFrame(myBuiltPlane, plane);
    const bool centered = center.Distance(myBuiltCenter) < myBuiltExtent * 0.25;
    const bool sized = myBuiltExtent > 0.0 &&
                       extent < myBuiltExtent * 2.0 && extent > myBuiltExtent * 0.5;
    if (sameLevel && samePlane && centered && sized) return;

    rebuild(step, centerU, centerV, extent, plane);
    myBuiltStep = step;
    myBuiltCenter = center;
    myBuiltExtent = extent;
    myBuiltPlane = plane;
}

void GridRenderer::rebuild(double minorStep, double centerU, double centerV,
                           double extent, const gp_Pln& plane)
{
    const double major = minorStep * 10.0;
    const QColor background = Theme::viewport();

    // Everything below is laid out in the plane's own (u, v) coordinates and
    // mapped into the world here. That single indirection is the whole of
    // this class's plane support - the band maths never sees a world axis.
    auto at = [&plane](double u, double v) { return ElSLib::Value(u, v, plane); };

    // Three concentric bands; outer bands blend toward the background so the
    // grid has no visible boundary.
    struct BandSpec { double inner, outer, fade; };
    const BandSpec specs[3] = {{0.0, 0.5, 0.0}, {0.5, 0.75, 0.55}, {0.75, 1.0, 0.85}};

    Handle(GridObject) grid = new GridObject();

    auto addLines = [&](bool isMajor, const BandSpec& spec) {
        const double step = isMajor ? major : minorStep;
        const QColor base = isMajor ? Theme::gridMajor() : Theme::gridMinor();
        const QColor colour = lerp(base, background, spec.fade);

        // Collect segments for lines whose |coordinate| lies in the band ring.
        std::vector<gp_Pnt> points;
        const double lo = extent * spec.inner, hi = extent * spec.outer;
        // Positions are absolute multiples of the step, snapped outward from
        // the band's clip range - never anchored at the band edge, which is
        // not in general a multiple of the step.
        const double first = firstLineAtOrBelow(hi, step);
        for (double offset = first; offset <= hi + step * 0.5; offset += step) {
            if (!isMajor && std::fmod(std::fabs(offset) + step * 0.25, major) < step * 0.5)
                continue;   // skip positions covered by a major line
            const double a = std::fabs(offset);
            // Lines fully inside an inner band are drawn by that band already;
            // draw the full length in the innermost band and only the ring
            // extension in outer bands.
            if (spec.inner == 0.0) {
                if (a > hi) continue;
                points.push_back(at(centerU + offset, centerV - hi));
                points.push_back(at(centerU + offset, centerV + hi));
                points.push_back(at(centerU - hi, centerV + offset));
                points.push_back(at(centerU + hi, centerV + offset));
            } else {
                if (a > hi) continue;
                // Ring: two segments per line (the parts outside the inner square),
                // plus full-length lines whose offset itself is in the ring.
                if (a >= lo) {
                    points.push_back(at(centerU + offset, centerV - hi));
                    points.push_back(at(centerU + offset, centerV + hi));
                    points.push_back(at(centerU - hi, centerV + offset));
                    points.push_back(at(centerU + hi, centerV + offset));
                } else {
                    points.push_back(at(centerU + offset, centerV - hi));
                    points.push_back(at(centerU + offset, centerV - lo));
                    points.push_back(at(centerU + offset, centerV + lo));
                    points.push_back(at(centerU + offset, centerV + hi));
                    points.push_back(at(centerU - hi, centerV + offset));
                    points.push_back(at(centerU - lo, centerV + offset));
                    points.push_back(at(centerU + lo, centerV + offset));
                    points.push_back(at(centerU + hi, centerV + offset));
                }
            }
        }
        if (points.empty()) return;

        Handle(Graphic3d_ArrayOfSegments) array =
            new Graphic3d_ArrayOfSegments(static_cast<Standard_Integer>(points.size()));
        for (const gp_Pnt& p : points) array->AddVertex(p);

        GridObject::Band band;
        band.segments = array;
        band.colour = toOcct(colour);
        band.width = isMajor ? 1.4 : 1.0;
        grid->bands.push_back(band);
    };

    for (const BandSpec& spec : specs) {
        addLines(false, spec);
        addLines(true, spec);
    }

    // The plane's own two axes through its origin, if that origin is inside
    // the built area. On the ground plane these are the world X and Y axes,
    // which is what they have always been; on a locked face they are the
    // face's own local axes, which is what the grid is measured in.
    if (std::fabs(centerU) < extent && std::fabs(centerV) < extent) {
        auto axis = [&](const QColor& colour, bool isU) {
            Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(2);
            if (isU) {
                array->AddVertex(at(centerU - extent, 0.0));
                array->AddVertex(at(centerU + extent, 0.0));
            } else {
                array->AddVertex(at(0.0, centerV - extent));
                array->AddVertex(at(0.0, centerV + extent));
            }
            GridObject::Band band;
            band.segments = array;
            band.colour = toOcct(colour);
            band.width = 1.8;
            grid->bands.push_back(band);
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
    myContext->Display(myGrid, 0, -1, Standard_False);   // mode -1: not selectable
    myContext->UpdateCurrentViewer();
}
