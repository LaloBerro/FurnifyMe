#pragma once
// The adaptive ground grid. Replaces OCCT's finite ActivateGrid patch: three
// concentric bands of line segments on Z=0 whose colours blend toward the
// viewport background with distance, so there is never a visible edge.
// App-layer only - it builds OCCT presentation objects.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

class GridRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);
    void update(double cameraDistance, const gp_Pnt& cameraTarget);

    static double minorStepFor(double cameraDistance);

    // First line position at or below -limit on the absolute grid of `step`.
    static double firstLineAtOrBelow(double limit, double step);

private:
    Handle(AIS_InteractiveContext) myContext;
    Handle(AIS_InteractiveObject) myGrid;
    double myBuiltStep = 0.0;
    gp_Pnt myBuiltCenter{0.0, 0.0, 0.0};
    double myBuiltExtent = 0.0;

    void rebuild(double minorStep, const gp_Pnt& center, double extent);
};
