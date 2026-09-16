#pragma once
// The selection sizes (improvements item 5): width, depth and height drawn
// around what is selected, like a technical drawing. One body gets its own
// three dimension lines in Theme::sizesOneBody(); a group gets ONE dashed
// outline around everything selected and the group's overall three, in
// Theme::sizesGroup() - the picked mockup's option B, and no per-piece
// numbers.
//
// A COMPOSITION over DimensionRenderer rather than a sibling of it: the three
// lines are exactly the annotation the edge dimension already draws, asked
// for with a different Style (a token and the boxed number). DimensionRenderer
// still holds no opinion about which case is asking - the style says how it
// looks, never what it measures.
//
// This class never measures anything. The box (ModelingOps::measuredBox) is
// computed by MainWindow, cached on the selected ids and the document
// revision, and handed in; what happens here on every camera move is only the
// LAYOUT - which of each extent's four parallel box edges carries its line -
// and that is a handful of dot products. An orbit never reaches Bnd_OBB.
//
// Like every renderer here it never redraws the viewer: show() and clear()
// return whether anything on screen changed, and OcctViewWidget asks for the
// frame.
#include "DimensionRenderer.h"
#include "Measure.h"
#include "ModelingOps.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Dir.hxx>

#include <array>
#include <string>

class SelectionSizesRenderer {
public:
    enum class Kind { OneBody, Group };

    void attach(const Handle(AIS_InteractiveContext)& context);
    // GridRenderer::detach()'s contract: drops everything without touching
    // the viewer, for OcctViewWidget::releaseGlResources().
    void detach();
    void setZLayer(Graphic3d_ZLayerId layer);

    // Lays the three dimensions (and, for a group, the dashed outline) out
    // around `box` for a camera looking along `viewDirection` with `viewUp`
    // up. Which edges carry the lines, chosen so they face the reader:
    //   - the extent running most ACROSS the screen of width and depth goes
    //     along the box's bottom-most edge of that direction, pushed further
    //     down; the other goes along the left-most, pushed further left;
    //   - height goes along the right-most vertical-ish edge, pushed right;
    //   - a tie between two edges goes to the one nearer the eye.
    // Each line is pushed out along whichever of its edge's two box faces
    // points furthest that way on screen. A dimension whose span is under a
    // few pixels on screen (height, looked at straight down) is not drawn.
    // `worldPerPixel` sizes every non-measured piece, DimensionRenderer's own
    // rule. Returns TRUE only when something on screen changed - each line's
    // own equal-guard, plus the outline's - so an orbit that does not cross
    // an edge choice costs no rebuild at all.
    bool show(const ModelingOps::MeasuredBox& box, Kind kind, const gp_Dir& viewDirection,
              const gp_Dir& viewUp, double worldPerPixel);
    bool clear();
    // Rebuilds what is up with the current tokens - a theme edit.
    bool reapplyTheme();

    bool isShowing() const { return myShowing; }
    Kind kind() const { return myKind; }
    bool outlineShowing() const { return !myOutline.IsNull(); }
    // The three labels actually on screen, WIDTH, DEPTH, HEIGHT in that
    // order - an empty string for one not drawn. For the suite and the
    // banned-word sweep; the strings are Measure::formatLength's, never a
    // local format.
    std::array<std::string, 3> labelTexts() const;

    // --- where each dimension IS, for Re-Measure's hit tests -----------------
    //
    // Index is WIDTH, DEPTH, HEIGHT - labelTexts()'s own order, which is also
    // ModelingOps::MeasuredBox's widthAxis/depthAxis/heightAxis order, so a
    // caller that hit-tests index 1 knows without a lookup that it is holding
    // the depth and its axis. Both answers come straight out of
    // DimensionRenderer, which laid the annotation out - see its own header
    // for why the layout is not re-derived by whoever is testing against it.
    //
    // `start` is the end of the drawn line on the LOW side of that axis and
    // `end` the high side, which is what makes the pin's three positions
    // (low end, centre, high end) read the same way as
    // ModelingOps::ResizeAnchor's own Low/Centre/High.
    //
    // False, outputs untouched, for an index outside 0..2 or a dimension this
    // camera is not drawing (height looked at straight down).
    bool labelBox(int index, gp_Pnt& anchor, double& halfWidthPx, double& halfHeightPx) const;
    bool dimensionLine(int index, gp_Pnt& start, gp_Pnt& end, gp_Dir& outward) const;
    // How many times anything was actually rebuilt - the suite's count.
    int buildCount() const { return myBuilds; }

private:
    bool showOutline(const ModelingOps::MeasuredBox& box);
    bool clearOutline();
    void applyStyle();

    Handle(AIS_InteractiveContext) myContext;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    DimensionRenderer myDims[3];
    Handle(AIS_InteractiveObject) myOutline;
    ModelingOps::MeasuredBox myOutlineBox;
    Kind myKind = Kind::OneBody;
    bool myShowing = false;
    Measure::Unit myUnit = Measure::Unit::Millimetres;
    int myBuilds = 0;
    // The last layout's choices, held against a tie - see show().
    bool myHaveLayout = false;
    bool myWidthAcross = true;
    int myEdge[3][2] = {{1, 1}, {1, 1}, {1, 1}};
    int myRole[3] = {0, 1, 2};
    bool myNormalAlongB[3] = {true, true, true};
};
