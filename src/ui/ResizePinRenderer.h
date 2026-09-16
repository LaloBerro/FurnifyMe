#pragma once
// The Re-Measure pin (improvements item 8) - the mark on a size's dimension
// line that says WHICH END STAYS PUT while the other moves.
//
// Three positions, in ModelingOps::ResizeAnchor's own order: the low end of
// the axis, the centre, the high end. The PIN stands on the active one; the
// other two wear a smaller open ring, because a control with two invisible
// alternatives is a control nobody finds. It starts at the CENTRE - the
// user's own change to the picked mockup - so both ends move by half until
// somebody says otherwise.
//
// Drawn IN THE SCENE, like the dimension it sits on, rather than painted over
// the viewport: the line it marks turns with the camera, and a mark that did
// not would slide off the end it names on the first orbit. Screen-SIZED
// though, through Graphic3d_TMF_ZoomRotatePers, exactly as DimensionRenderer's
// boxed number is: one local unit is one device pixel at the anchor's depth,
// so the pin reads the same size at every zoom.
//
// Like every renderer here it never redraws the viewer: show() and clear()
// return whether anything on screen changed, and OcctViewWidget asks for the
// frame.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Pnt.hxx>

#include <array>

class ResizePinRenderer {
public:
    // How near the cursor has to be, in logical pixels, for a mark to claim
    // the press - the shared gizmo grab radius, named here so the drawn size
    // and the grabbable size are derived from one number rather than agreeing
    // by coincidence.
    static constexpr double kMarkRadiusPx = 9.0;

    void attach(const Handle(AIS_InteractiveContext)& context);
    // GridRenderer::detach()'s contract: drops everything without touching
    // the viewer, for OcctViewWidget::releaseGlResources().
    void detach();
    void setZLayer(Graphic3d_ZLayerId layer);

    // The three marks at `low`, `centre` and `high`, with `active` (0, 1 or 2)
    // wearing the pin. TRUE only when something on screen actually changed -
    // an orbit that moves neither the marks nor the choice costs one
    // comparison.
    bool show(const gp_Pnt& low, const gp_Pnt& centre, const gp_Pnt& high, int active);
    bool clear();
    bool reapplyTheme();

    bool isShowing() const { return myShowing; }
    int active() const { return myActive; }
    // Where mark `index` stands, for the caller's own screen-space hit test.
    // False, `out` untouched, when nothing is showing or the index is outside
    // 0..2.
    bool markPoint(int index, gp_Pnt& out) const;

private:
    bool build();

    Handle(AIS_InteractiveContext) myContext;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    std::array<Handle(AIS_InteractiveObject), 3> myMarks;
    std::array<gp_Pnt, 3> myPoints;
    int myActive = 1;
    bool myShowing = false;
};
