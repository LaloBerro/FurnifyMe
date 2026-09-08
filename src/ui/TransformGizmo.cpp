#include "TransformGizmo.h"

#include "AxisGizmo.h"
#include "DimensionRenderer.h"
#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <AIS_TextLabel.hxx>
#include <Aspect_TypeOfDisplayText.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Font_FontAspect.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_HorizontalTextAlignment.hxx>
#include <Graphic3d_VerticalTextAlignment.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPoint>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

// --- the chip's own card metrics (PullArrow's, one gizmo over) --------------
constexpr int kPad = 10;
constexpr int kMinWidth = 128;
constexpr int kLabelHeight = 18;
constexpr int kValueHeight = 22;
constexpr int kHintGap = 4;
constexpr int kHintHeight = 14;
constexpr int kChipGap = 18;
constexpr int kEdgeInset = 8;

// --- the gizmo's own size, in SCREEN pixels --------------------------------
//
// ONE number is chosen here: how long an arm is. Everything else - the stroke,
// the cone, the hollow ball, the hub, the letter and where each of them sits -
// is AxisCard's own number multiplied by kCardScale, so this drawing IS the
// card's drawing at another size rather than a second drawing that resembles
// it. See AxisGizmo.h.
//
// The arm length is deliberately under the manipulator's own screen cap
// (kGizmoMaxViewportFraction, 15% of the viewport's smaller side - about 116 px
// in the suite's 1100x800 window), so swapping tools with Space never makes the
// handle jump outward. The spec parks this number for Phase 3's feel-test.
constexpr double kArmPixels = 92.0;
constexpr double kCardScale = kArmPixels / AxisCard::kArmPx;

// How many segments a cone's base ring, a ball and the hub disc are drawn
// with. Thirty-two leaves no visible facet at these sizes, and the whole gizmo
// is still under four hundred line segments.
//
// It is also what makes the measurement honest rather than approximately
// honest: a ring's frame below is built so that a VERTEX lands on each screen
// silhouette extreme, and a 32-gon's inscribed width is 0.5% under its
// circumscribed one - so what a pixel probe measures across a ring is the
// radius the code asked for, not the radius minus however far the nearest
// vertex happened to fall from the edge.
constexpr int kRingSegments = 32;
// How many spokes fill the hub disc. The card FILLS its hub; nothing in this
// build fills anything (see addStrokes()), so the disc is a rim plus spokes,
// and the spoke count only has to be high enough that the arc between two of
// them at the rim is shorter than a stroke is wide. At the hub's own
// proportions that threshold is thirteen; this is comfortably past it.
constexpr int kHubSpokes = 32;
// And how many lines fill a cone. Same reasoning, one element over: the card
// fills a flat triangle, so this one is a fan from the apex across the base,
// and at the cone's own proportions the base spacing here is under a third of
// a stroke width.
constexpr int kConeFillLines = 16;

// The x/y/z the card paints past each cone, in its own lowercase.
constexpr char kAxisLetter[3] = {'x', 'y', 'z'};

// Spelled out rather than M_PI, which MSVC does not define without
// _USE_MATH_DEFINES - the same literal every other file in this tree carries.
constexpr double kPi = 3.14159265358979323846;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One AIS object per colour, never pickable - the same shape as
// PullArrowLines, DimensionRenderer's DimensionLines and OcctViewWidget's
// SketchPointMarker.
//
// Never pickable is the whole reason this gizmo needs none of AIS_Manipulator's
// workaround pile. An AIS object with a real ComputeSelection joins the
// hover/selection pipeline, and AIS_ManipulatorOwner outranks a shape's own
// owner - which is why the manipulator has to be Deactivate()d around every
// additive pick, and why an arm crossing a second body used to swallow the
// click that meant to add it. This gizmo is hit-tested in SCREEN SPACE by
// OcctViewWidget (moveGizmoAxisAt()), so it never competes for a pick at all:
// it takes a press outright, before the picker runs, or it takes nothing.
class GizmoLines : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Quantity_Color colour;
    double width = 2.0;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (lines.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect =
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID,
                                       static_cast<Standard_ShortReal>(width));
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(lines);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // See the class comment: picked in screen space by the viewport, not
        // through AIS.
    }
};

// One closed ring of `segments` chords, centred on `at`, radius `radius`, in
// the (u, v) plane. Appended rather than returned so a caller can put a ring
// and whatever else it needs into one AIS object.
void addRing(std::vector<GizmoRenderer::Stroke>& out, const gp_Pnt& at, const gp_Vec& u,
            const gp_Vec& v, double radius, int segments)
{
    gp_Pnt previous;
    gp_Pnt first;
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * kPi * double(i) / double(segments);
        const gp_Pnt onRing =
            at.Translated(u * (radius * std::cos(angle)) + v * (radius * std::sin(angle)));
        if (i == 0)
            first = onRing;
        else
            out.push_back({previous, onRing});
        previous = onRing;
    }
    out.push_back({previous, first});
}

}  // namespace

// --- the shared presentation base -------------------------------------------

void GizmoRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void GizmoRenderer::detach()
{
    if (!myContext.IsNull()) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
    }
    myObjects.clear();
    myContext.Nullify();
}

bool GizmoRenderer::clear()
{
    const bool had = !myObjects.empty();
    if (!myContext.IsNull() && had) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
        // No UpdateCurrentViewer(): the caller owns the frame since the
        // QOpenGLWidget migration - PullArrowRenderer::clear()'s own rule.
    }
    myObjects.clear();
    return had;
}

void GizmoRenderer::reapplyTheme()
{
    if (!isShowing()) return;
    myForceRebuild = true;
    show(myPose);   // caller owns the frame
}

bool GizmoRenderer::show(const GizmoPose& pose)
{
    if (myContext.IsNull()) return false;

    // Nothing to do when nothing has actually moved. MoveTool::reposition()
    // drives this from cameraChanged AND from every appStateChanged, so the
    // great majority of calls ask for exactly the gizmo already on screen -
    // and a rebuild is emphatically not free (PullArrowRenderer::show() has
    // the measurements: 33.3 ms per rebuild before its own early-out, 0.17 ms
    // after). The tolerances are that function's, for the same reasons.
    constexpr double kHalfDegree = 0.0087266;   // radians
    if (!myForceRebuild && isShowing() && pose.pivot.IsEqual(myPose.pivot, 1.0e-9) &&
        pose.view.IsEqual(myPose.view, kHalfDegree) &&
        // The drawing is laid out on the view plane, so a ROLL about the view
        // direction moves every arm while leaving `view` untouched. Both
        // spanning vectors are part of the key for that reason - dropping
        // either one leaves a gizmo that never redraws through a Top-view
        // orbit, which is the one place this app's up vector sweeps.
        pose.right.IsEqual(myPose.right, kHalfDegree) &&
        pose.up.IsEqual(myPose.up, kHalfDegree) &&
        std::fabs(pose.worldPerPixel - myPose.worldPerPixel) <=
            std::max(myPose.worldPerPixel, 1.0e-9) * 0.01 &&
        std::fabs(pose.pixelRatio - myPose.pixelRatio) < 1.0e-6) {
        return false;
    }
    myForceRebuild = false;

    clear();

    myPose = pose;
    myPose.worldPerPixel = std::max(pose.worldPerPixel, 1.0e-9);
    myPose.pixelRatio = std::max(pose.pixelRatio, 1.0e-9);

    buildStrokes();
    return true;
}

void GizmoRenderer::addStrokes(const std::vector<Stroke>& strokes, const QColor& colour,
                               double widthPx)
{
    if (myContext.IsNull() || strokes.empty()) return;

    Handle(Graphic3d_ArrayOfSegments) segs =
        new Graphic3d_ArrayOfSegments(static_cast<Standard_Integer>(strokes.size() * 2));
    for (const Stroke& s : strokes) {
        segs->AddVertex(s.a);
        segs->AddVertex(s.b);
    }

    Handle(GizmoLines) object = new GizmoLines();
    object->lines = segs;
    object->colour = toOcct(colour);
    object->width = widthPx;
    myContext->Display(object, 0, -1, Standard_False);   // mode -1: feedback only
    // Drawn over everything. A move gizmo stands at its body's own bounding-box
    // centre, which is INSIDE the body - in the default layer it would be
    // depth-tested away and invisible, which is the same argument
    // attachManipulator() makes for putting the manipulator here. CLAUDE.md
    // rejects this layer for the ground GRID and that ruling stands: the layer
    // clears depth, so a grid on it would paint over every body standing on it.
    // Here painting over the body IS the requirement, and depth still applies
    // WITHIN the layer, so the three arms occlude each other correctly.
    myContext->SetZLayer(object, Graphic3d_ZLayerId_Topmost);
    myObjects.push_back(object);
}

void GizmoRenderer::addLabel(const QString& text, const gp_Pnt& at, const QColor& colour,
                             double heightPx)
{
    if (myContext.IsNull() || text.isEmpty() || heightPx <= 0.0) return;

    Handle(AIS_TextLabel) label = new AIS_TextLabel();
    label->SetText(TCollection_ExtendedString(text.toUtf8().constData(), Standard_True));
    label->SetPosition(at);
    // Centred on the anchor in BOTH directions, because the card centres its
    // own letter in a box around letterPos - a bottom-justified label would
    // sit a whole cap height off, which is most of the distance being pinned.
    label->SetHJustification(Graphic3d_HTA_CENTER);
    label->SetVJustification(Graphic3d_VTA_CENTER);
    label->SetHeight(heightPx);
    label->SetColor(toOcct(colour));
    label->SetFont(DimensionRenderer::fontFamily().c_str());
    // Bold, as AxisCard::letterFont() is. It changes the ink's WIDTH rather
    // than its height, so it is not what the ratio pin is measuring - it is
    // here because the two drawings are meant to be one drawing.
    label->SetFontAspect(Font_FontAspect_Bold);
    // TODT_NORMAL, unlike the dimension label: a subtitle box behind three
    // letters would put a panel-coloured rectangle over the scene at each arm
    // tip, and the card paints no such box.
    label->SetDisplayType(Aspect_TODT_NORMAL);
    myContext->Display(label, 0, -1, Standard_False);   // mode -1: feedback only
    myContext->SetZLayer(label, Graphic3d_ZLayerId_Topmost);
    myObjects.push_back(label);
}

// --- the Move tool's presentation -------------------------------------------

gp_Dir MoveGizmoRenderer::armDirection(int axis)
{
    switch (axis) {
        case 0: return gp_Dir(1.0, 0.0, 0.0);
        case 1: return gp_Dir(0.0, 1.0, 0.0);
        default: return gp_Dir(0.0, 0.0, 1.0);
    }
}

gp_Lin MoveGizmoRenderer::armAxis(int axis) const
{
    return gp_Lin(pivot(), armDirection(axis));
}

gp_Pnt MoveGizmoRenderer::handleTip(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return pivot();
    return myTip[axis][positive ? 0 : 1];
}

gp_Pnt MoveGizmoRenderer::handleGrabStart(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return pivot();
    return myGrabStart[axis][positive ? 0 : 1];
}

bool MoveGizmoRenderer::handleDrawn(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return false;
    return myDrawn[axis][positive ? 0 : 1];
}

void MoveGizmoRenderer::buildStrokes()
{
    // THE CARD'S DRAWING, at this viewport's scale. There are exactly two
    // conversions below and every single size goes through one of them:
    //
    //   `w`  turns a card pixel into WORLD units, for anything this file
    //        builds as geometry - arms, cones, balls, the hub, the letters'
    //        anchors. Screen pixels converted at the point of use is
    //        DimensionRenderer's rule, and it is why this gizmo needs none of
    //        OCCT's zoom-persistence machinery.
    //   `p`  turns a card pixel into DEVICE pixels, for the two things OCCT
    //        sizes for us rather than from our geometry: a line's width and a
    //        label's height. worldPerPixel() answers per LOGICAL pixel, so
    //        this is the one place the display scale enters.
    //
    // Nothing here is a number of its own. The only choice this file makes is
    // kArmPixels, and kCardScale is that choice divided by the card's.
    const double w = kCardScale * worldPerPixel();
    const double p = kCardScale * pixelRatio();

    const double armLength = AxisCard::kArmPx * w;
    const double coneLength = AxisCard::kConePx * w;
    const double coneApex = AxisCard::kConePx * AxisCard::kConeApexFactor * w;
    const double coneHalf = AxisCard::kConePx * AxisCard::kConeHalfWidthFactor * w;
    const double ballRadius = AxisCard::kBallPx * w;
    const double hubRadius = AxisCard::kHubPx * w;
    const double letterOffset = AxisCard::kLetterOffsetPx * w;
    const double armStroke = AxisCard::kArmStrokePx * p;
    const double negativeStroke = AxisCard::kNegativeStrokePx * p;
    const double letterHeight = AxisCard::letterEmPx() * p;

    // INK, not path - and this is the one correction the copy needs.
    //
    // The card FILLS its cone and its hub with QPainter and no pen at all, so
    // their edges land exactly on the radius. Nothing in this build fills
    // anything (see addStrokes()), so both are drawn as stroked line-art
    // instead, and a stroke's ink runs half a line width PAST the path it is
    // centred on. Inset the path by that half width and the ink lands where
    // the card's fill edge does. Note the display scale cancels out of it: the
    // stroke is scaled by `p` and converted back by dividing by the same
    // ratio, so this is a card number times `w` like everything else.
    //
    // The BALL is not inset, because the card strokes its ball too - both
    // drawings put their ink half a stroke outside the same radius, which is
    // agreement rather than a matched error.
    const double halfArmInk = 0.5 * AxisCard::kArmStrokePx * w;

    // The view plane's own two axes, and the one direction off it.
    const gp_Vec R(pose().right);
    const gp_Vec U(pose().up);
    const gp_Vec V(pose().view);

    // A COPLANAR drawing has no draw order of its own, and the card's has one:
    // it sorts its six tips far-to-near and paints, with the hub last over
    // everything. A depth buffer is the only sorter available here, so each
    // tip's geometry is nudged ALONG the view direction in proportion to its
    // own depth and the hub is nudged in front of all of them - the card's
    // painter's algorithm, expressed as the thing this scene actually obeys.
    // The nudge is a fiftieth of an arm against a camera distance of hundreds
    // of them, so what it costs the projection is a fraction of a per cent.
    constexpr double kDepthSort = 0.02;
    const double depthStep = kDepthSort * armLength;

    AxisCard::Tip tips[6];
    AxisCard::computeTips(pose().right, pose().up, pose().view, tips);

    for (const AxisCard::Tip& tip : tips) {
        const int axis = tip.axis;
        const int slot = tip.positive ? 0 : 1;
        const QColor colour = axis == 0   ? Theme::gizmoAxisX()
                              : axis == 1 ? Theme::gizmoAxisY()
                                          : Theme::gizmoAxisZ();

        // The tip's offset from the hub, per unit arm length, laid out on the
        // view plane - the card's own (right, up) pair, as world vectors.
        const gp_Vec offset = R * tip.right + U * tip.up;
        const double reach = offset.Magnitude();   // this axis's foreshortening
        const gp_Pnt hub = pivot().Translated(V * (tip.depth * depthStep));
        const double drawn = armLength * reach;

        myDrawn[axis][slot] = drawn > hubRadius;
        if (reach < 1.0e-9) {
            // The axis points straight at the eye. The card draws a tip
            // sitting on the hub and its hub covers it; there is nothing to
            // lay out and nothing to grab - see handleDrawn().
            myTip[axis][slot] = hub;
            myGrabStart[axis][slot] = hub;
            continue;
        }

        const gp_Vec unit = offset / reach;      // the tip's direction ON SCREEN
        const gp_Vec across = V.Crossed(unit);   // ...and the perpendicular to it,
                                                 // still on the plane, still unit
        const gp_Pnt at = hub.Translated(unit * drawn);
        myTip[axis][slot] = at;
        myGrabStart[axis][slot] = hub.Translated(unit * (drawn * kGrabStartFraction));

        // The card refuses to draw an arm shorter than a pixel rather than let
        // a cone base land behind its own hub; so does this.
        if (drawn <= worldPerPixel()) continue;

        if (tip.positive) {
            // --- shaft, FILLED cone, letter -----------------------------
            const gp_Pnt base = at.Translated(unit * -coneLength);
            const gp_Pnt apex = at.Translated(unit * coneApex);
            const gp_Vec half = across * (coneHalf - halfArmInk);

            std::vector<Stroke> arm;
            arm.reserve(2 + kConeFillLines);
            arm.push_back({hub, base});
            // A flat triangle, filled the only way this build can fill
            // anything: a fan of lines from the apex across the base. The
            // spacing at the base is well under a stroke width so it closes
            // solid, and the two extreme lines ARE the triangle's own edges.
            for (int f = 0; f <= kConeFillLines; ++f) {
                const double t = 2.0 * double(f) / double(kConeFillLines) - 1.0;
                arm.push_back({base.Translated(half * t), apex});
            }
            addStrokes(arm, colour, armStroke);

            addLabel(QString(QLatin1Char(kAxisLetter[axis])), at.Translated(unit * letterOffset),
                     colour.lighter(AxisCard::kLetterLighten), letterHeight);
        } else {
            // --- a thinner stub and a HOLLOW ball ------------------------
            //
            // Hollow means hollow. The card fills its ball with its own panel
            // colour because a card is opaque and there is nothing behind it;
            // here there IS something behind it, and a ring is what the card's
            // fill was standing in for.
            std::vector<Stroke> negative;
            negative.reserve(1 + kRingSegments);
            negative.push_back({hub.Translated(unit * (drawn * AxisCard::kNegativeStubStart)), at});
            // Built on (across, unit) rather than an arbitrary pair, so a
            // vertex lands on each silhouette extreme instead of falling
            // between two - which is what lets a pixel probe read the radius
            // back rather than the radius times cos(pi/segments).
            addRing(negative, at, across, unit, ballRadius, kRingSegments);
            addStrokes(negative, colour, negativeStroke);
        }
    }

    // --- the hub: one filled neutral disc, in front of everything ----------
    //
    // The card draws a filled circle LAST, over the whole drawing, which is
    // what lets its arms start at the dead centre and still look like they
    // start at the hub's edge. Nudged a step in front of the nearest tip here
    // for exactly that, since a depth buffer and not a painter decides. A disc
    // made of a rim and spokes is a fill without a fill: the arc between two
    // spokes at the rim is shorter than a stroke is wide, and the rim gives it
    // a clean edge. Neutral on purpose - it belongs to no axis, and colouring
    // it would read as a fourth handle.
    {
        const gp_Pnt centre = pivot().Translated(V * (-1.5 * depthStep));
        const double rim = std::max(hubRadius - halfArmInk, 1.0e-9);
        std::vector<Stroke> hub;
        hub.reserve(kRingSegments + kHubSpokes);
        addRing(hub, centre, R, U, rim, kRingSegments);
        for (int i = 0; i < kHubSpokes; ++i) {
            const double angle = 2.0 * kPi * double(i) / double(kHubSpokes);
            hub.push_back({centre, centre.Translated(R * (rim * std::cos(angle)) +
                                                     U * (rim * std::sin(angle)))});
        }
        addStrokes(hub, AxisCard::hubColour(), armStroke);
    }
}

// --- the value chip ---------------------------------------------------------

MoveTool::MoveTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and must never eat a press meant for an arm behind
    // it - there is no interactive control on this widget at all, unlike
    // PullArrow's field, so the whole subtree can be click-through.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    applySize();
    hide();

    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applySize();
        update();
    });
    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &MoveTool::refresh);
    if (myView) {
        // The gizmo lives in the scene, so a camera move changes both where
        // the chip belongs on screen and how big the arms have to be drawn to
        // keep their pixel size.
        connect(myView, &OcctViewWidget::cameraChanged, this, &MoveTool::reposition);
        connect(myView, &OcctViewWidget::moveDragged, this, &MoveTool::onDragged);
        connect(myView, &OcctViewWidget::moveReleased, this, &MoveTool::onReleased);
    }
}

MoveTool::~MoveTool()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
}

void MoveTool::refresh()
{
    if (!myWindow || !myView) return;

    // ONE predicate, shared with updateActions() and the status label. Note
    // that it requires exactly one whole BODY selected, which is what keeps
    // this disjoint from the pull arrow (a face), the bevel arrow (edges) and
    // ExtrudePreview (a pending outline) - so at most one application-wide key
    // claim can ever be installed at a time.
    const int id = myWindow->moveToolBodyId();
    if (id <= 0) {
        end();
        return;
    }

    if (id != myBodyId) {
        begin(id);
        return;
    }

    // Already up on this body. Re-derive where it stands (a commit replaces
    // the body, and the replacement's bounding box is what the handle belongs
    // on now) and repaint - the chip's value follows the display unit, so a
    // unit switch has to reach it.
    showGizmo();
    update();
}

void MoveTool::begin(int bodyId)
{
    myBodyId = bodyId;
    myAxis = -1;
    myDistance = 0.0;
    if (myHasPreview) {
        myView->clearModelingPreview();
        myHasPreview = false;
    }
    showGizmo();
    updateVisibility();
}

void MoveTool::end()
{
    if (myView) {
        if (myHasPreview) myView->clearModelingPreview();
        myView->clearMoveGizmo();
    }
    myHasPreview = false;
    myBodyId = 0;
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
}

void MoveTool::showGizmo()
{
    if (!myWindow || !myView || myBodyId <= 0) return;
    gp_Pnt pivot;
    // The SAME bounding-box centre AIS_Manipulator's own AdjustPosition
    // derives, through the one shared implementation - see
    // ModelingOps::boundingBoxCentre() for why that matters when Space swaps
    // one tool for the other.
    if (!ModelingOps::boundingBoxCentre(myWindow->document().shapeOf(myBodyId), pivot)) return;
    myView->showMoveGizmo(pivot);
}

void MoveTool::cancel()
{
    if (!myView) return;
    myView->cancelMoveDrag();
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
    // The gizmo and the selection are deliberately untouched: the body is
    // still selected, the predicate still holds, and the user can simply drag
    // again without re-picking anything.
}

void MoveTool::onDragged(int axis, double millimetres)
{
    if (myBodyId <= 0) return;
    myAxis = axis;
    myDistance = millimetres;
    updatePreview();
    updateVisibility();
    reposition();
    update();
}

void MoveTool::onReleased(bool dragged)
{
    if (myBodyId <= 0) return;
    const bool committing = dragged && std::fabs(myDistance) > 1.0e-7;
    // The preview goes FIRST, and unconditionally. Unlike the face pull, a
    // move keeps the body selected, so the predicate still holds after the
    // commit and refresh() will not reach end() - which means nothing else
    // would ever take this preview down.
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    if (committing) commit();
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
}

void MoveTool::updatePreview()
{
    if (!myWindow || !myView || myBodyId <= 0 || myAxis < 0) return;

    if (std::fabs(myDistance) < 1.0e-7) {
        if (myHasPreview) {
            myView->clearModelingPreview();
            myHasPreview = false;
        }
        return;
    }

    // The SAME ModelingOps::transformShape() the commit uses, through the same
    // gp_Trsf. A preview built by a different path is a lie, and this is the
    // one place the user judges a distance by what it looks like.
    gp_Trsf delta;
    delta.SetTranslation(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
    const ModelingOps::BooleanResult result =
        ModelingOps::transformShape(myWindow->document().shapeOf(myBodyId), delta);
    if (!result.ok) return;   // the last good preview stands

    // The DEDICATED channel, never setPreview(): that slot is already shared
    // by the sketch outline and the extrude preview, and CLAUDE.md records
    // what a third writer on it cost. myBodyId goes with it so the body the
    // ghost stands in for is drawn as a cage rather than sitting solid at the
    // old position.
    myView->setModelingPreview(result.shape, myBodyId);
    myHasPreview = true;
}

void MoveTool::commit()
{
    if (!myWindow || myBodyId <= 0 || myAxis < 0) return;
    gp_Trsf delta;
    delta.SetTranslation(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
    // THE existing commit path - the checkpoint, the toast with Undo, the
    // mirror twin, the linked copies and the kernel refusal all belong to it.
    // This gesture adds no commit logic of its own whatsoever.
    myWindow->transformBody(myBodyId, delta);
}

void MoveTool::updateVisibility()
{
    // DERIVED, in one place, from the drag the viewport says is live - never
    // toggled by whichever event happened to run last. The chip has something
    // to say exactly while a drag has produced a distance, which is also
    // exactly as long as it may claim Escape.
    const bool wanted = myBodyId > 0 && myAxis >= 0 && myView && myView->moveDragActive();
    if (wanted == isVisible()) return;
    if (wanted) {
        reposition();
        show();
        raise();
    } else {
        hide();
    }
}

void MoveTool::reposition()
{
    if (!myView) return;

    // Rebuild the gizmo at the new scale first: it is drawn in world units but
    // sized in screen pixels, so a zoom changes the geometry it needs.
    if (myBodyId > 0) showGizmo();

    if (!isVisible() || myAxis < 0) return;

    gp_Pnt tip;
    QPoint at;
    // The END that was grabbed, not always the cone: a drag begun on the
    // negative ball is measured along the same line, but a chip that jumped to
    // the far side of the body would be labelling the handle the user is not
    // holding.
    if (!myView->moveGizmoHandleTip(myAxis, myView->moveDragPositive(), tip) ||
        !myView->projectToScreen(tip, at))
        return;

    // Beside the dragged handle's tip, flipped to the other side rather than
    // clamped when that would run off the right edge - PullArrow's own layout,
    // for the same reason: a value chip that walks away from the handle it
    // labels stops labelling it.
    int x = at.x() + kChipGap;
    if (x + width() > myView->width() - kEdgeInset) x = at.x() - kChipGap - width();
    x = std::clamp(x, kEdgeInset, std::max(kEdgeInset, myView->width() - width() - kEdgeInset));
    int y = at.y() - height() / 2;
    y = std::clamp(y, kEdgeInset, std::max(kEdgeInset, myView->height() - height() - kEdgeInset));

    // Whole DEVICE pixels, Theme's position rule - PullArrow's own closing
    // lines, and for the same reason: a card placed at whatever pixel a
    // projection returned lands on a fractional device row half the time.
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    x = Theme::snapToDevicePixels(x, origin.x(), dpr);
    y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    move(x, y);
}

void MoveTool::replace()
{
    reposition();
    if (isVisible()) raise();
}

QString MoveTool::labelText() const
{
    if (myAxis < 0) return tr("Move");
    const QChar letter = myAxis == 0 ? QLatin1Char('X')
                         : myAxis == 1 ? QLatin1Char('Y')
                                       : QLatin1Char('Z');
    return tr("Move along %1").arg(letter);
}

QString MoveTool::valueText() const
{
    return QString::fromStdString(Measure::formatLength(myDistance));
}

QString MoveTool::hintText() const
{
    // A modeless panel with invisible verbs is how a keyboard cancel went
    // unnoticed for a whole branch on ExtrudePreview. This chip has no buttons
    // either, so its one key is on it in words.
    return tr("Drag an arm — Esc cancels");
}

QStringList MoveTool::paintedTexts() const
{
    // Every spelling of the label, not merely the one a given drag happens to
    // be showing: the sweep must cover the copy, and a run that never dragged
    // the Y arm would otherwise leave that string uncovered.
    return {tr("Move"), tr("Move along %1").arg(QLatin1Char('X')),
            tr("Move along %1").arg(QLatin1Char('Y')),
            tr("Move along %1").arg(QLatin1Char('Z')), hintText()};
}

void MoveTool::applySize()
{
    // Measured with the fonts it paints with - a fixed guess is exactly the
    // kind of number that silently clips the day the copy changes.
    const int margin = Theme::surfaceShadowMargin();
    const QFontMetrics hintMetrics(Theme::badgeFont());
    const QFontMetrics labelMetrics(Theme::labelFont());
    const QFontMetrics bodyMetrics(Theme::bodyFont());
    const int textWidth = std::max(
        {hintMetrics.horizontalAdvance(hintText()),
         labelMetrics.horizontalAdvance(tr("Move along %1").arg(QLatin1Char('X'))),
         // A four-figure length with a separator is the widest value this chip
         // can be asked to paint before it leaves the viewport entirely.
         bodyMetrics.horizontalAdvance(
             QString::fromStdString(Measure::formatLength(-1234.5)))});
    const int cardWidth = std::max(kMinWidth, textWidth + kPad * 2);
    setFixedSize(Theme::wholeDevicePixels(
        QSize(cardWidth + margin * 2,
              kPad * 2 + kLabelHeight + kValueHeight + kHintGap + kHintHeight + margin * 2)));
}

void MoveTool::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::accent());
    painter.drawText(QRect(body.left() + kPad, body.top() + kLabelHeight,
                           body.width() - kPad * 2, kValueHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, valueText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(QRect(body.left() + kPad, body.top() + kLabelHeight + kValueHeight + kHintGap,
                           body.width() - kPad * 2, kHintHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, hintText());
}

void MoveTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Installed for exactly as long as the chip is up - ExtrudePreview's and
    // PullArrow's own lifetime rule for an application-wide filter. Here that
    // is narrower than either: the chip is up only while a drag is live, so
    // this claim cannot even coexist with a gesture that has not started.
    QCoreApplication::instance()->installEventFilter(this);
}

void MoveTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    QCoreApplication::instance()->removeEventFilter(this);
}

bool MoveTool::eventFilter(QObject* watched, QEvent* event)
{
    // Escape belongs to this chip for as long as it is VISIBLE, whatever holds
    // focus - PullArrow::eventFilter()'s own reasoning, and it is not
    // optional: the press that started this drag went to the viewport, so the
    // viewport holds focus and a filter on this widget alone would never see
    // the key.
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    // Application-wide means every window in this process - gui_smoke builds
    // several at once - so only keys headed for this chip's own window count.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window())
        return QWidget::eventFilter(watched, event);
    // Never steal a keystroke out of a focused text field - the mirror chip's
    // own structural backstop, one gesture over. Escape in a rename field
    // means "abandon the rename", and this claim must not outrank it.
    if (qobject_cast<QLineEdit*>(widget)) return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier) return QWidget::eventFilter(watched, event);
    if (keyEvent->key() != Qt::Key_Escape) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        event->accept();   // claims the key back from QShortcutMap
        return true;
    }
    cancel();
    return true;
}
