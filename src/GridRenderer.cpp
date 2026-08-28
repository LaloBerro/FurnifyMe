#include "GridRenderer.h"

#include "Theme.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
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
}

void GridRenderer::update(double cameraDistance, const gp_Pnt& cameraTarget)
{
    if (myContext.IsNull()) return;

    const double step = minorStepFor(cameraDistance);
    // Extent: comfortably beyond what a camera at this distance can see of the
    // ground, snapped to the major step so lines do not crawl on rebuild.
    const double major = step * 10.0;
    double extent = std::clamp(cameraDistance * 6.0, 500.0, 200000.0);
    extent = std::ceil(extent / major) * major;
    const gp_Pnt center(std::round(cameraTarget.X() / major) * major,
                        std::round(cameraTarget.Y() / major) * major, 0.0);

    // Rebuild only when something visible changes: level, or the camera left
    // the middle half of the built area, or extent changed by >2x.
    const bool sameLevel = (step == myBuiltStep);
    const bool centered = center.Distance(myBuiltCenter) < myBuiltExtent * 0.25;
    const bool sized = myBuiltExtent > 0.0 &&
                       extent < myBuiltExtent * 2.0 && extent > myBuiltExtent * 0.5;
    if (sameLevel && centered && sized) return;

    rebuild(step, center, extent);
    myBuiltStep = step;
    myBuiltCenter = center;
    myBuiltExtent = extent;
}

void GridRenderer::rebuild(double minorStep, const gp_Pnt& center, double extent)
{
    const double major = minorStep * 10.0;
    const QColor background = Theme::viewport();

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
        for (double v = first; v <= hi + step * 0.5; v += step) {
            if (!isMajor && std::fmod(std::fabs(v) + step * 0.25, major) < step * 0.5)
                continue;   // skip positions covered by a major line
            const double a = std::fabs(v);
            // Lines fully inside an inner band are drawn by that band already;
            // draw the full length in the innermost band and only the ring
            // extension in outer bands.
            if (spec.inner == 0.0) {
                if (a > hi) continue;
                points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
            } else {
                if (a > hi) continue;
                // Ring: two segments per line (the parts outside the inner square),
                // plus full-length lines whose offset itself is in the ring.
                if (a >= lo) {
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                    points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
                } else {
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - hi, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() - lo, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + lo, 0.0));
                    points.push_back(gp_Pnt(center.X() + v, center.Y() + hi, 0.0));
                    points.push_back(gp_Pnt(center.X() - hi, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() - lo, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + lo, center.Y() + v, 0.0));
                    points.push_back(gp_Pnt(center.X() + hi, center.Y() + v, 0.0));
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

    // Axis lines through the origin, if the origin is inside the built area.
    if (std::fabs(center.X()) < extent && std::fabs(center.Y()) < extent) {
        auto axis = [&](const QColor& colour, bool isX) {
            Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(2);
            if (isX) {
                array->AddVertex(gp_Pnt(center.X() - extent, 0.0, 0.0));
                array->AddVertex(gp_Pnt(center.X() + extent, 0.0, 0.0));
            } else {
                array->AddVertex(gp_Pnt(0.0, center.Y() - extent, 0.0));
                array->AddVertex(gp_Pnt(0.0, center.Y() + extent, 0.0));
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
    myContext->Display(myGrid, 0, -1, Standard_False);   // mode -1: not selectable
    myContext->UpdateCurrentViewer();
}
