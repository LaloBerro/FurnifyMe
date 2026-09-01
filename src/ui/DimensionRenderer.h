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
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

class DimensionRenderer {
public:
    void attach(const Handle(AIS_InteractiveContext)& context);

    // The Z-layer the lines and the label are displayed in. An annotation is
    // sketch work, so it belongs in the same layer as the outline and the
    // markers - above the work-plane grid, still depth-tested against the
    // bodies. Set once by OcctViewWidget after it has made that layer; left
    // at Graphic3d_ZLayerId_UNKNOWN this class displays into the default
    // layer, exactly as it did before layers existed here.
    void setZLayer(Graphic3d_ZLayerId layer) { myLayer = layer; }
    Graphic3d_ZLayerId zLayer() const { return myLayer; }

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

    // Redraws the span that is already up, from the arguments it was last
    // shown with. Nothing to do when nothing is showing. This exists because
    // the label reads through Measure and Measure's unit can change while the
    // annotation is on screen: without it a label kept saying "40 mm" over a
    // viewport that had switched to centimetres, until the next mouse move
    // happened to rebuild it. Not a QObject - this class draws, it does not
    // listen - so MainWindow drives it from appStateChanged, the same signal
    // every other unit-following surface refreshes on.
    void refresh();

    // The label's text, for the suite and the banned-word sweep. Empty when
    // nothing is shown.
    std::string labelText() const { return myLabelText; }

private:
    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    std::string myLabelText;

    // The last span shown, kept only so refresh() can rebuild it. Meaningful
    // only while something is showing.
    gp_Pnt myFrom;
    gp_Pnt myTo;
    gp_Dir myNormal;
    double myWorldPerPixel = 1.0;
};
