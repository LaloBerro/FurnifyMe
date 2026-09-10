#include "TransformGizmo.h"

#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Builder.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Quantity_Color.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
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

// --- the gizmo's own size and proportions, in SCREEN pixels ----------------
//
// ONE number sets the scale - how long an arm is - and the user's Gizmo size
// token multiplies it. The proportions are Unity's own gizmo language: a thin
// shaft, a slender cone tip, a small sphere at the pivot. All converted to
// world units through worldPerPixel() at build time, so the gizmo reads the
// same at any zoom.
//
// The arm length is deliberately under the manipulator's own screen cap
// (kGizmoMaxViewportFraction, 15% of the viewport's smaller side - about 116 px
// in the suite's 1100x800 window), so swapping tools with Space never makes the
// handle jump outward. The spec parks this number for Phase 3's feel-test.
constexpr double kArmPixels = 92.0;
constexpr double kShaftRadiusPx = 2.0;
constexpr double kConeLengthPx = 24.0;
constexpr double kConeRadiusPx = 6.5;
constexpr double kHubSpherePx = 7.0;
// The Rotate rings ride at the arm radius with a tube a shade thicker than a
// shaft, so a ring is as easy to see as an arm; the Scale cubes sit where the
// cones do, at a comparable visual weight.
constexpr double kRingTubePx = 2.5;
constexpr double kCubeHalfPx = 5.5;

// How much brighter a hovered handle draws. Enough to read at a glance
// without leaving the axis family - the same reason the hover is a lighter
// self rather than the app's cyan hover tint, which would cost the handle the
// one thing that names its axis.
constexpr int kHoverLighten = 155;

// Meshing, as a FRACTION of the arm so it scales with the drawing: a chord
// sagitta a quarter of a pixel is invisible, and these analytic surfaces mesh
// in well under a millisecond - which matters because a zoom step rebuilds
// (an orbit does not; see viewDependent()).
constexpr double kMeshDeflection = 0.0025;
constexpr double kMeshAngleRad = 0.3;

// The pivot sphere's neutral grey - it belongs to no axis, and colouring it
// would read as a fourth handle.
const QColor kHubColour(0xc8, 0xc8, 0xcc);

// Spelled out rather than M_PI, which MSVC does not define without
// _USE_MATH_DEFINES - the same literal every other file in this tree carries.
constexpr double kPi = 3.14159265358979323846;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// The axis token, brightened when this axis is the hovered one - the ONE
// derivation all three renderers colour their handles through.
QColor axisColour(int axis, int hoveredAxis)
{
    const QColor c = axis == 0   ? Theme::gizmoAxisX()
                     : axis == 1 ? Theme::gizmoAxisY()
                                 : Theme::gizmoAxisZ();
    return axis == hoveredAxis ? c.lighter(kHoverLighten) : c;
}

// One arrow - shaft cylinder plus cone of revolution - along `dir` from
// `origin`, sized in world units, meshed and ready to display. Built fresh at
// world size on every rebuild rather than scaled from a cached unit shape: a
// rebuild only happens on a zoom or a pivot move, and meshing these seven
// analytic faces is far cheaper than the presentation upload that follows
// either way.
TopoDS_Shape makeArrow(const gp_Pnt& origin, const gp_Dir& dir, double armLength)
{
    const double coneLength = armLength * (kConeLengthPx / kArmPixels);
    const double shaftLength = armLength - coneLength;
    const double shaftRadius = armLength * (kShaftRadiusPx / kArmPixels);
    const double coneRadius = armLength * (kConeRadiusPx / kArmPixels);

    BRep_Builder builder;
    TopoDS_Compound arrow;
    builder.MakeCompound(arrow);
    builder.Add(arrow, BRepPrimAPI_MakeCylinder(gp_Ax2(origin, dir), shaftRadius,
                                                std::max(shaftLength, 1.0e-9))
                           .Shape());
    const gp_Pnt coneBase = origin.Translated(gp_Vec(dir) * shaftLength);
    builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(coneBase, dir), coneRadius, 0.0,
                                            std::max(coneLength, 1.0e-9))
                           .Shape());
    BRepMesh_IncrementalMesh(arrow, armLength * kMeshDeflection, Standard_False,
                             kMeshAngleRad, Standard_True);
    return arrow;
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

bool GizmoRenderer::setHoveredAxis(int axis)
{
    if (axis == myHoveredAxis) return false;
    myHoveredAxis = axis;
    if (!isShowing()) return false;
    // The hover tint is baked into the AIS objects at build time exactly as
    // the theme hues are, so the same forced rebuild carries it - and it only
    // runs on enter/leave of a handle, never per mouse move.
    myForceRebuild = true;
    show(myPose);   // caller owns the frame
    return true;
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
    // The camera-frame terms apply only to a view-dependent drawing - a gizmo
    // laid out along the world axes ignores them, so for it an orbit at
    // constant distance is a cache hit and rebuilds nothing.
    const bool frameUnchanged =
        !viewDependent() || (pose.view.IsEqual(myPose.view, kHalfDegree) &&
                             pose.right.IsEqual(myPose.right, kHalfDegree) &&
                             pose.up.IsEqual(myPose.up, kHalfDegree));
    if (!myForceRebuild && isShowing() && pose.pivot.IsEqual(myPose.pivot, 1.0e-9) &&
        frameUnchanged &&
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

void GizmoRenderer::addSolid(const TopoDS_Shape& shape, const QColor& colour)
{
    if (myContext.IsNull() || shape.IsNull()) return;

    Handle(AIS_Shape) object = new AIS_Shape(shape);
    object->SetDisplayMode(AIS_Shaded);

    // UNLIT, and that is the shader choice rather than an omission: a flat
    // solid-colour arrow is Unity's own gizmo look, and an unlit pixel is the
    // token pixel exactly - which a lit mesh could never promise a probe.
    Handle(Prs3d_ShadingAspect) aspect = new Prs3d_ShadingAspect();
    aspect->Aspect()->SetInteriorStyle(Aspect_IS_SOLID);
    aspect->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    aspect->Aspect()->SetInteriorColor(toOcct(colour));
    aspect->SetColor(toOcct(colour));
    object->Attributes()->SetShadingAspect(aspect);
    // A render is a solid, not a drawing of one - no face-boundary ink on a
    // handle.
    object->Attributes()->SetFaceBoundaryDraw(Standard_False);

    // Selection mode -1: feedback only, never pickable. An AIS_Shape's
    // ComputeSelection is only ever run for an ACTIVATED mode, so this object
    // never enters the pick pipeline - the same fact that spares this gizmo
    // AIS_Manipulator's whole workaround pile. Hit-testing is
    // OcctViewWidget::moveGizmoAxisAt()'s screen-space job, as it always was.
    myContext->Display(object, AIS_Shaded, -1, Standard_False);
    myContext->SetZLayer(object, drawLayer());
    myObjects.push_back(object);
}

Graphic3d_ZLayerId GizmoRenderer::drawLayer() const
{
    // The gizmo's own layer when the viewer gave us one, and Topmost when it
    // did not - which is where this used to live and is still right in every
    // way except that it is shared. See setZLayer()'s own comment.
    return myLayer != Graphic3d_ZLayerId_UNKNOWN ? myLayer : Graphic3d_ZLayerId_Topmost;
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
    // ONE conversion: screen pixels to world units at the camera target's
    // depth, through worldPerPixel() - DimensionRenderer's rule, and why this
    // gizmo needs none of OCCT's zoom-persistence machinery. The user's Gizmo
    // size token scales the whole drawing uniformly; a theme edit reaches a
    // live gizmo through reapplyTheme()'s forced rebuild. No device-pixel
    // term anywhere: everything here is 3D geometry, and geometry is sized in
    // logical pixels like every other screen-sized thing in the scene.
    const double armLength =
        std::max(kArmPixels * Theme::gizmoScale() * worldPerPixel(), 1.0e-9);

    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = armDirection(axis);
        const gp_Vec along(dir);

        // The handle caches, in WORLD coordinates - the hit test projects
        // them to the screen itself. Only the positive handles exist (user
        // ruling, 2026-09-08); a handle that is not drawn must not grab, and
        // handleDrawn() is the one gate moveGizmoAxisAt() reads.
        myTip[axis][0] = pivot().Translated(along * armLength);
        myGrabStart[axis][0] =
            pivot().Translated(along * (armLength * kGrabStartFraction));
        myDrawn[axis][0] = true;
        myTip[axis][1] = pivot();
        myGrabStart[axis][1] = pivot();
        myDrawn[axis][1] = false;

        addSolid(makeArrow(pivot(), dir, armLength), axisColour(axis, hoveredAxis()));
    }

    // The pivot sphere - Blender's own centre handle, and the reason there is
    // no flat disc left to crop: a sphere is a perfect circle from every
    // angle. Meshed here because makeArrow() only meshes what it builds.
    TopoDS_Shape hub =
        BRepPrimAPI_MakeSphere(pivot(), armLength * (kHubSpherePx / kArmPixels)).Shape();
    BRepMesh_IncrementalMesh(hub, armLength * kMeshDeflection, Standard_False, kMeshAngleRad,
                             Standard_True);
    addSolid(hub, kHubColour);
}

// --- the Rotate tool's presentation -----------------------------------------

gp_Pnt RotateGizmoRenderer::ringPoint(int axis, double angleRad) const
{
    // The ring of `axis` lies in the plane that axis is normal to, so its
    // basis is the other two world axes - X's ring sweeps (Y, Z), Y's (Z, X),
    // Z's (X, Y). Right-handed on purpose: a positive drag angle measured
    // about the axis is then a positive rotation, with no sign fix anywhere.
    const gp_Vec u(MoveGizmoRenderer::armDirection((axis + 1) % 3));
    const gp_Vec v(MoveGizmoRenderer::armDirection((axis + 2) % 3));
    return pivot().Translated(u * (myRadius * std::cos(angleRad)) +
                              v * (myRadius * std::sin(angleRad)));
}

void RotateGizmoRenderer::buildStrokes()
{
    const double armLength =
        std::max(kArmPixels * Theme::gizmoScale() * worldPerPixel(), 1.0e-9);
    myRadius = armLength;
    const double tube = armLength * (kRingTubePx / kArmPixels);

    for (int axis = 0; axis < 3; ++axis) {
        TopoDS_Shape ring =
            BRepPrimAPI_MakeTorus(gp_Ax2(pivot(), MoveGizmoRenderer::armDirection(axis)),
                                  myRadius, tube)
                .Shape();
        BRepMesh_IncrementalMesh(ring, armLength * kMeshDeflection, Standard_False,
                                 kMeshAngleRad, Standard_True);
        addSolid(ring, axisColour(axis, hoveredAxis()));
    }
}

// --- the Scale tool's presentation ------------------------------------------

gp_Pnt ScaleGizmoRenderer::handleTip(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return pivot();
    return myTip[axis][positive ? 0 : 1];
}

gp_Pnt ScaleGizmoRenderer::handleGrabStart(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return pivot();
    return myGrabStart[axis][positive ? 0 : 1];
}

bool ScaleGizmoRenderer::handleDrawn(int axis, bool positive) const
{
    if (axis < 0 || axis > 2) return false;
    return myDrawn[axis][positive ? 0 : 1];
}

void ScaleGizmoRenderer::buildStrokes()
{
    const double armLength =
        std::max(kArmPixels * Theme::gizmoScale() * worldPerPixel(), 1.0e-9);
    const double cubeHalf = armLength * (kCubeHalfPx / kArmPixels);
    const double shaftRadius = armLength * (kShaftRadiusPx / kArmPixels);

    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = MoveGizmoRenderer::armDirection(axis);
        const gp_Vec along(dir);

        // The cube's CENTRE is the handle tip, so the chip stands on the cube
        // rather than past it. Same positive-only handle set as the Move
        // gizmo, same caches, same hit-test gate.
        const gp_Pnt tip = pivot().Translated(along * armLength);
        myTip[axis][0] = tip;
        myGrabStart[axis][0] =
            pivot().Translated(along * (armLength * MoveGizmoRenderer::kGrabStartFraction));
        myDrawn[axis][0] = true;
        myTip[axis][1] = pivot();
        myGrabStart[axis][1] = pivot();
        myDrawn[axis][1] = false;

        BRep_Builder builder;
        TopoDS_Compound arm;
        builder.MakeCompound(arm);
        builder.Add(arm, BRepPrimAPI_MakeCylinder(gp_Ax2(pivot(), dir), shaftRadius,
                                                  std::max(armLength - cubeHalf, 1.0e-9))
                             .Shape());
        // Axis-aligned is arm-aligned here: the arms run along the world
        // axes, so a world-aligned box reads as a cube square on its arm -
        // Unity's own scale tip.
        builder.Add(arm, BRepPrimAPI_MakeBox(gp_Pnt(tip.X() - cubeHalf, tip.Y() - cubeHalf,
                                                    tip.Z() - cubeHalf),
                                             2.0 * cubeHalf, 2.0 * cubeHalf, 2.0 * cubeHalf)
                             .Shape());
        BRepMesh_IncrementalMesh(arm, armLength * kMeshDeflection, Standard_False,
                                 kMeshAngleRad, Standard_True);
        addSolid(arm, axisColour(axis, hoveredAxis()));
    }

    TopoDS_Shape hub =
        BRepPrimAPI_MakeSphere(pivot(), armLength * (kHubSpherePx / kArmPixels)).Shape();
    BRepMesh_IncrementalMesh(hub, armLength * kMeshDeflection, Standard_False, kMeshAngleRad,
                             Standard_True);
    addSolid(hub, kHubColour);
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
        connect(myView, &OcctViewWidget::moveDragged, this, &MoveTool::onToolDragged);
        connect(myView, &OcctViewWidget::moveReleased, this, &MoveTool::onReleased);
        // Phase 2: the same chip serves Rotate and Scale. Their releases end
        // exactly as Move's does - one implementation of the commit tail.
        connect(myView, &OcctViewWidget::rotateDragged, this, &MoveTool::onToolDragged);
        connect(myView, &OcctViewWidget::rotateReleased, this, &MoveTool::onReleased);
        connect(myView, &OcctViewWidget::scaleDragged, this, &MoveTool::onToolDragged);
        connect(myView, &OcctViewWidget::scaleReleased, this, &MoveTool::onReleased);
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

    // The tool changed under a LIVE drag (Space mid-hold): the accumulated
    // value belongs to the tool that measured it, and dragTransform() reads
    // the live tool - so the drag is cancelled, chip and ghost included,
    // before the new tool's gizmo shows. The viewport has already killed
    // its own half at the switch (MainWindow::setBodyTool() /
    // showBodyGizmo()); this is the chip's own state, which only this class
    // can retire.
    if (myShownTool >= 0 && myShownTool != static_cast<int>(myWindow->bodyTool()) &&
        myAxis >= 0) {
        cancel();
        return;   // cancel() re-shows the new tool's gizmo itself
    }

    // Already up on this body. Re-derive where it stands (a commit replaces
    // the body, and the replacement's bounding box is what the handle belongs
    // on now) and repaint - the chip's value follows the display unit, so a
    // unit switch has to reach it.
    showGizmo();
    update();
}

void MoveTool::resetDrag()
{
    myAxis = -1;
    myDistance = 0.0;
    myFactor = 1.0;
}

void MoveTool::begin(int bodyId)
{
    myBodyId = bodyId;
    resetDrag();
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
        myView->clearBodyGizmos();
    }
    myHasPreview = false;
    myBodyId = 0;
    myShownTool = -1;
    resetDrag();
    updateVisibility();
}

void MoveTool::showGizmo()
{
    if (!myWindow || !myView || myBodyId <= 0) return;
    myShownTool = static_cast<int>(myWindow->bodyTool());
    // The SAME bounding-box centre for every tool, through the one shared
    // implementation - see ModelingOps::boundingBoxCentre() for why Space
    // must swap tools without the handle jumping a millimetre. CACHED on
    // (bodyId, document revision): reposition() re-enters here on every
    // cameraChanged, and an orbit never edits geometry, so re-walking the
    // body's whole triangulation per mouse-move was pure per-frame waste
    // on the camera path CLAUDE.md budgets at fractions of a millisecond
    // (the branch review's finding). An edit bumps revision(), which is
    // exactly when the box can genuinely have moved.
    const int revision = myWindow->document().revision();
    if (myBodyId != myPivotBodyId || revision != myPivotRevision) {
        if (!ModelingOps::boundingBoxCentre(myWindow->document().shapeOf(myBodyId),
                                            myPivotCache))
            return;
        myPivotBodyId = myBodyId;
        myPivotRevision = revision;
    }
    gp_Pnt pivot = myPivotCache;
    // WHICH gizmo is the active tool's to say - each show clears the other
    // two inside the viewport, so exactly one is ever up.
    switch (myWindow->bodyTool()) {
        case MainWindow::BodyTool::Rotate:
            myView->showRotateGizmo(pivot);
            break;
        case MainWindow::BodyTool::Scale:
            myView->showScaleGizmo(pivot);
            break;
        default:
            // The Move gizmo TRAVELS with a live drag, standing where the
            // body will land - the same offset the ghost preview shows. The
            // drag maths is untouched: the viewport measures against the line
            // it froze at the press (myMoveDragLine), never this moving
            // pivot. Rotate and Scale stay put by design - their pivot IS the
            // fixed point of the edit.
            if (myAxis >= 0 && myView->moveDragActive())
                pivot.Translate(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
            myView->showMoveGizmo(pivot);
            break;
    }
}

void MoveTool::cancel()
{
    if (!myView) return;
    myView->cancelBodyGizmoDrag();
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    resetDrag();
    updateVisibility();
    // The selection is deliberately untouched: the body is still selected,
    // the predicate still holds, and the user can simply drag again without
    // re-picking anything. The gizmo is re-shown because it travelled with
    // the cancelled drag and belongs back at the body it stands on.
    showGizmo();
}

void MoveTool::onToolDragged(int axis, double value)
{
    if (myBodyId <= 0) return;
    myAxis = axis;
    // The value's meaning is the live tool's - millimetres, degrees, or a
    // factor - stored into the field dragTransform()'s own switch reads, so
    // there is exactly one interpretation site rather than three slots
    // repeating one body.
    if (myWindow && myWindow->bodyTool() == MainWindow::BodyTool::Scale)
        myFactor = value;
    else
        myDistance = value;
    updatePreview();
    updateVisibility();
    reposition();
    update();
}

void MoveTool::onReleased(bool dragged)
{
    if (myBodyId <= 0) return;
    // "Produced a change" is per-tool: a Move or Rotate back at zero, or a
    // Scale back at 1.0, commits nothing.
    const bool committing =
        dragged && (myWindow->bodyTool() == MainWindow::BodyTool::Scale
                        ? std::fabs(myFactor - 1.0) > 1.0e-7
                        : std::fabs(myDistance) > 1.0e-7);
    // The preview goes FIRST, and unconditionally. Unlike the face pull, a
    // move keeps the body selected, so the predicate still holds after the
    // commit and refresh() will not reach end() - which means nothing else
    // would ever take this preview down.
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    if (committing) commit();
    resetDrag();
    updateVisibility();
    // Re-derive where the gizmo stands. After a commit the bounding box IS
    // the dragged position, so this is where it already sat; a drag released
    // back at zero has no commit and no refresh coming, and this is what
    // walks the travelled gizmo home.
    showGizmo();
}

bool MoveTool::dragTransform(gp_Trsf& out) const
{
    if (!myWindow || myBodyId <= 0 || myAxis < 0) return false;
    switch (myWindow->bodyTool()) {
        case MainWindow::BodyTool::Rotate: {
            if (std::fabs(myDistance) < 1.0e-7) return false;
            // About the body's OWN pivot - the same bounding-box centre the
            // rings stand on - never the world origin, which would swing the
            // body around the room instead of turning it in place.
            gp_Pnt pivot;
            if (!ModelingOps::boundingBoxCentre(myWindow->document().shapeOf(myBodyId), pivot))
                return false;
            out.SetRotation(gp_Ax1(pivot, MoveGizmoRenderer::armDirection(myAxis)),
                            myDistance * kPi / 180.0);
            return true;
        }
        case MainWindow::BodyTool::Scale: {
            // The Milestone 2 clamp, unchanged: the kernel accepts more, the
            // UI does not hand it more.
            const double factor = std::clamp(myFactor, 0.05, 20.0);
            if (std::fabs(factor - 1.0) < 1.0e-7) return false;
            gp_Pnt pivot;
            if (!ModelingOps::boundingBoxCentre(myWindow->document().shapeOf(myBodyId), pivot))
                return false;
            out.SetScale(pivot, factor);
            return true;
        }
        default:
            if (std::fabs(myDistance) < 1.0e-7) return false;
            out.SetTranslation(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
            return true;
    }
}

void MoveTool::updatePreview()
{
    if (!myWindow || !myView || myBodyId <= 0 || myAxis < 0) return;

    // The SAME transform the commit will take, from the one derivation
    // (dragTransform()), through the same ModelingOps::transformShape(). A
    // preview built by a different path is a lie, and this is the one place
    // the user judges a value by what it looks like.
    gp_Trsf delta;
    if (!dragTransform(delta)) {
        if (myHasPreview) {
            myView->clearModelingPreview();
            myHasPreview = false;
        }
        return;
    }
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
    gp_Trsf delta;
    if (!dragTransform(delta)) return;
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
    const bool wanted = myBodyId > 0 && myAxis >= 0 && myView && myView->bodyGizmoDragActive();
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
    // The chip stands where the HAND is: the cone or cube being dragged, or -
    // for a ring, which has no tip - the point on the ring the press grabbed.
    bool haveAnchor = false;
    switch (myWindow ? myWindow->bodyTool() : MainWindow::BodyTool::Move) {
        case MainWindow::BodyTool::Rotate:
            haveAnchor = myView->rotateDragAnchor(tip);
            break;
        case MainWindow::BodyTool::Scale:
            haveAnchor = myView->scaleGizmoHandleTip(myAxis, tip);
            break;
        default:
            haveAnchor = myView->moveGizmoHandleTip(myAxis, myView->moveDragPositive(), tip);
            break;
    }
    if (!haveAnchor || !myView->projectToScreen(tip, at)) return;

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
    const MainWindow::BodyTool tool =
        myWindow ? myWindow->bodyTool() : MainWindow::BodyTool::Move;
    // Scale is uniform by kernel law (gp_Trsf has no per-axis form), so
    // naming an axis on it would claim a distinction the edit does not make.
    if (tool == MainWindow::BodyTool::Scale) return tr("Scale");
    const QChar letter = myAxis == 0   ? QLatin1Char('X')
                         : myAxis == 1 ? QLatin1Char('Y')
                                       : QLatin1Char('Z');
    if (tool == MainWindow::BodyTool::Rotate)
        return myAxis < 0 ? tr("Rotate") : tr("Rotate about %1").arg(letter);
    return myAxis < 0 ? tr("Move") : tr("Move along %1").arg(letter);
}

QString MoveTool::valueText() const
{
    const MainWindow::BodyTool tool =
        myWindow ? myWindow->bodyTool() : MainWindow::BodyTool::Move;
    // Degrees and percent are not lengths, so they do not go through Measure
    // - the same ruling the Appearance panel's bare-"x" multipliers recorded.
    if (tool == MainWindow::BodyTool::Rotate)
        return QStringLiteral("%1°").arg(QString::number(myDistance, 'f', 0));
    if (tool == MainWindow::BodyTool::Scale)
        return QStringLiteral("%1%").arg(QString::number(myFactor * 100.0, 'f', 0));
    return QString::fromStdString(Measure::formatLength(myDistance));
}

QString MoveTool::hintText() const
{
    // A modeless panel with invisible verbs is how a keyboard cancel went
    // unnoticed for a whole branch on ExtrudePreview. This chip has no buttons
    // either, so its one key is on it in words.
    return tr("Drag a handle — Esc cancels");
}

QStringList MoveTool::paintedTexts() const
{
    // Every spelling of the label, not merely the one a given drag happens to
    // be showing: the sweep must cover the copy, and a run that never dragged
    // the Y arm would otherwise leave that string uncovered.
    QStringList texts{tr("Move"), tr("Rotate"), tr("Scale"), hintText()};
    for (const QChar letter : {QLatin1Char('X'), QLatin1Char('Y'), QLatin1Char('Z')}) {
        texts << tr("Move along %1").arg(letter) << tr("Rotate about %1").arg(letter);
    }
    return texts;
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
