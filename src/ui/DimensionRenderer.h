#pragma once
// A CAD-style length annotation - extension lines, arrowheads and a boxed
// label reading the true distance between two points. Endpoints are always
// millimetres; the label goes through Measure::formatLength so it follows
// the user's unit choice for free. App-layer only, like GridRenderer: it
// builds OCCT presentation objects rather than painting over the viewport,
// so the annotation stays correct as the camera moves.
//
// One renderer serves both call sites - the live sketch segment and a
// hovered or selected edge - and holds no opinion about which is asking. If
// it ever needs one, that is the signal the two cases have diverged and want
// separate renderers.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

class DimensionRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);

    // Draws the dimension for the segment from `from` to `to`, replacing
    // whatever was drawn before. `normal` orients the extension lines out of
    // the segment - it need not be exactly perpendicular to the segment
    // already, it is projected to be. `worldPerPixel` is world units per
    // screen pixel at the segment's depth (OcctViewWidget::worldPerPixel());
    // the furniture - extension gaps, arrowheads, the label's offset - is
    // sized from it so it reads the same number of pixels at any zoom,
    // rather than shrinking to nothing as the camera pulls back. Only the
    // measured span itself (from to to) stays true to world scale, which is
    // the whole point of a dimension. A segment shorter than a hair draws
    // nothing and leaves isShowing() false.
    void show(const gp_Pnt& from, const gp_Pnt& to, const gp_Dir& normal, double worldPerPixel);
    void clear();
    bool isShowing() const { return !myObjects.empty(); }

    // The label's text, for the suite and the banned-word sweep. Empty when
    // nothing is shown.
    std::string labelText() const { return myLabelText; }

private:
    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    std::string myLabelText;
};
