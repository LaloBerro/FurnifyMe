#include "OcctViewWidget.h"

#include "ModelingOps.h"
#include "SketchController.h"
#include "Theme.h"

// OCCT before Qt, for the Handle() macro clash.
#include <AIS_DisplayMode.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_NameOfColor.hxx>
#include <SelectMgr_Selection.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Tool.hxx>
#include <V3d_TypeOfVisualization.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Vec.hxx>

#ifdef _WIN32
  #include <WNT_Window.hxx>
#else
  #include <Xw_Window.hxx>
#endif

#include <QEasingCurve>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
// AIS_Shape selection modes are plain integers: 0 whole shape, 2 edge, 4 face.
constexpr int kSelectionModeWholeShape = 0;
constexpr int kSelectionModeEdge       = 2;
constexpr int kSelectionModeFace       = 4;

// The transform gizmo's two extra snap steps. The translation step is not
// here: it is the viewport's own mySnapStep, the same 10 mm grid outline
// points and face pulls already land on, because a body that moved off the
// grid the outlines were drawn on would be a body nothing lines up with.
constexpr double kGizmoRotationStepDeg = 15.0;
constexpr double kGizmoScaleStep       = 0.05;

Quantity_Color toOcctColor(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One tiny point in world space, drawn as a marker whose size lives in
// screen pixels - Graphic3d_AspectMarker3d/Prs3d_PointAspect's own documented
// contract ("size does not depend on the zoom value of the views"), so a
// marker never balloons up close or vanishes far away the way a fixed
// millimetre size would. That contract held up (confirmed: these markers
// stay a constant pixel size as the camera moves). The scale argument does
// grow the rendered size, but not proportionally at the low end: 2.2
// against the ordinary dots' 1.5 measured pixel-for-pixel identical in this
// build, so the first-point ring below leans on a much larger jump (4.0)
// AND a different colour rather than trusting a small scale delta alone -
// colour is the one difference here that cannot silently fail to render,
// unlike fill and, it turns out, a modest scale bump. Same shape as
// DimensionRenderer's DimensionLines: a bespoke AIS_InteractiveObject that
// only implements Compute() and a no-op ComputeSelection(), because the
// primitive it draws (Graphic3d_ArrayOfPoints via a Graphic3d_Group) needs
// no more than that. Points are a simple enough GL primitive to trust
// without the extra shading setup that left Graphic3d_ArrayOfTriangles
// drawing nothing in this build (see DimensionRenderer's arrowhead
// comment) - confirmed by an actual snapshot before this shipped, not by
// that reasoning alone.
class SketchPointMarker : public AIS_InteractiveObject {
public:
    gp_Pnt point;
    Handle(Graphic3d_AspectMarker3d) aspect;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (aspect.IsNull()) return;
        Handle(Graphic3d_ArrayOfPoints) pts = new Graphic3d_ArrayOfPoints(1);
        pts->AddVertex(point);
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(pts);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable - feedback only, the same rule setPreview() and
        // DimensionRenderer already follow. A marker the user could select
        // would be a shape that exists in no document.
    }
};

Handle(SketchPointMarker) makeMarker(const gp_Pnt& point, Aspect_TypeOfMarker type,
                                     const Quantity_Color& colour, double scale)
{
    Handle(SketchPointMarker) marker = new SketchPointMarker();
    marker->point = point;
    marker->aspect = new Graphic3d_AspectMarker3d(type, colour, scale);
    return marker;
}
}  // namespace

OcctViewWidget::OcctViewWidget(QWidget* parent)
    : QWidget(parent)
    , mySketchPlane(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0))
{
    // Omit any of these and the viewport flickers or renders black.
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NativeWindow);
    setAutoFillBackground(false);
    setMouseTracking(true);          // hover highlight needs move events with no button down
    setFocusPolicy(Qt::StrongFocus);
    // A bare floor - this class knows nothing about the rail or any other
    // overlay content that gets pinned to it later. MainWindow::buildOverlay()
    // raises the height component once the rail exists, to whatever height
    // guarantees the rail itself fits; see that call for why 300 alone is not
    // enough to keep the rail's own buttons on screen.
    setMinimumSize(400, 300);
}

OcctViewWidget::~OcctViewWidget() = default;   // OCCT handles are refcounted; never delete them

void OcctViewWidget::initializeViewer()
{
    if (myInitialized) return;

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display);

    myViewer = new V3d_Viewer(driver);
    myViewer->SetDefaultLights();
    myViewer->SetLightOn();

    myView = myViewer->CreateView();
    myContext = new AIS_InteractiveContext(myViewer);

#ifdef _WIN32
    Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
    Handle(Xw_Window) window = new Xw_Window(display, static_cast<Aspect_Drawable>(winId()));
#endif
    myView->SetWindow(window);
    if (!window->IsMapped()) window->Map();

    const QColor bg = Theme::viewport();
    myView->SetBackgroundColor(Quantity_Color(bg.redF(), bg.greenF(), bg.blueF(),
                                              Quantity_TOC_sRGB));
    // No corner trihedron: AxisGizmo (top right) is the orientation surface,
    // and since the rail took the left edge the trihedron sat behind it with
    // one axis tip peeking out - redundant at best, a visual defect at worst.

    // OCCT's default highlight barely reads against a shaded solid. Make hover
    // and selection unmistakable - not being able to tell what is selected was
    // the single most confusing thing about the app.
    const Handle(Prs3d_Drawer) hover = myContext->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic);
    hover->SetColor(Quantity_NOC_CYAN1);
    hover->SetDisplayMode(AIS_Shaded);
    hover->SetTransparency(0.0f);

    const Handle(Prs3d_Drawer) picked = myContext->HighlightStyle(Prs3d_TypeOfHighlight_Selected);
    picked->SetColor(Quantity_NOC_ORANGE);
    picked->SetDisplayMode(AIS_Shaded);
    picked->SetTransparency(0.0f);

    // Sub-shape (face-mode) highlighting uses its own styles.
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->SetColor(Quantity_NOC_CYAN1);
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(Quantity_NOC_ORANGE);

    myGridRenderer.attach(myContext);
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane());
    myDimension.attach(myContext);
    myPullArrow.attach(myContext);
    myBevelArrow.attach(myContext);

    // Perspective projection: the turntable model is distance-based, and OCCT's
    // default orthographic camera zooms by scale, which would make
    // zoom-toward-cursor meaningless.
    myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    myView->Camera()->SetFOVy(kFovyDeg);
    applyCameraState();
    myView->MustBeResized();

    myInitialized = true;

}

void OcctViewWidget::paintEvent(QPaintEvent* /*event*/)
{
    // Lazy init: winId() is only meaningful once the widget has a native window.
    initializeViewer();
    if (!myView.IsNull()) myView->Redraw();
}

void OcctViewWidget::resizeEvent(QResizeEvent* /*event*/)
{
    if (!myView.IsNull()) myView->MustBeResized();
}

void OcctViewWidget::displaySolid(int id, const TopoDS_Shape& shape)
{
    initializeViewer();
    if (myContext.IsNull() || shape.IsNull()) return;

    const auto existing = mySolids.find(id);
    if (existing != mySolids.end()) {
        // The gizmo holds a handle to the presentation about to be removed, so
        // it goes first. MainWindow's predicate re-attaches it on the next
        // updateActions() if the body is still the one selected - which is how
        // a transform leaves the gizmo standing on the body it just moved.
        if (myManipulatorSolid == id) detachManipulator();
        myContext->Remove(existing->second, Standard_False);
        mySolids.erase(existing);
    }

    ModelingOps::tessellate(shape, 0.1);

    Handle(AIS_Shape) presentation = new AIS_Shape(shape);
    // Neutral grey so the cyan hover and orange selection stand out, and face
    // boundaries drawn so the shape's edges are readable when shaded.
    presentation->SetColor(Quantity_Color(Quantity_NOC_GRAY70));
    presentation->Attributes()->SetFaceBoundaryDraw(Standard_True);
    presentation->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_NOC_GRAY30, Aspect_TOL_SOLID, 1.0));
    myContext->Display(presentation, myWireframe ? AIS_WireFrame : AIS_Shaded,
                       kSelectionModeWholeShape, Standard_False);
    mySolids[id] = presentation;
    applySelectionMode(presentation);

    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::removeSolid(int id)
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || myContext.IsNull()) return;

    // Before the body goes, so the display mode it borrowed is put back on a
    // presentation that still exists - see setModelingPreview(). The arrow
    // goes with it for the same reason the dimension below does.
    clearModelingPreview();
    clearPullArrow();
    clearBevelArrow();
    // Same reason: the gizmo is attached to the presentation about to go.
    if (myManipulatorSolid == id) detachManipulator();

    myContext->Remove(it->second, Standard_False);
    mySolids.erase(it);
    // An annotation must never outlive the thing it measures: Delete and Undo
    // both come through here, and a dimension left behind hangs in empty space
    // labelling a body that is gone. Unconditional, because the only other
    // thing the renderer ever holds is the live sketch segment, and no route
    // removes a body while a sketch is in progress (Undo and Redo are disabled
    // while sketching, and the booleans need a selection sketch mode clears).
    myDimension.clear();
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearSolids()
{
    if (myContext.IsNull()) return;

    clearModelingPreview();   // same reasoning as removeSolid(), before the bodies go
    clearPullArrow();
    clearBevelArrow();
    detachManipulator();

    for (auto& entry : mySolids) myContext->Remove(entry.second, Standard_False);
    mySolids.clear();
    myDimension.clear();   // same rule as removeSolid(): nothing left to measure
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::setSolidVisible(int id, bool visible)
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || myContext.IsNull()) return;

    if (visible) {
        // Display(obj, false) would use the object's default mode - wireframe -
        // silently changing a solid's appearance when it is hidden and shown
        // again. Pass the mode the viewport is actually in.
        myContext->Display(it->second, myWireframe ? AIS_WireFrame : AIS_Shaded,
                           kSelectionModeWholeShape, Standard_False);
        applySelectionMode(it->second);
        myContext->UpdateCurrentViewer();
    } else {
        // Erase also drops it from the selection, which is what we want: acting
        // on something you cannot see would be a nasty surprise. The selection
        // can genuinely change here, unlike on the show path, so this is the
        // only branch that should tell the status bar to re-check it.
        myContext->Erase(it->second, Standard_False);
        myContext->UpdateCurrentViewer();
        emit selectionChanged();
    }
}

bool OcctViewWidget::isSolidVisible(int id) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || myContext.IsNull()) return false;
    return myContext->IsDisplayed(it->second);
}

void OcctViewWidget::setPreview(const TopoDS_Shape& shape, bool shaded)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearPreview();
    if (shape.IsNull()) return;

    if (shaded) ModelingOps::tessellate(shape, 0.1);

    myPreview = new AIS_Shape(shape);
    myPreview->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
    myPreview->SetWidth(2.0);
    // Selection mode -1: feedback only, never pickable.
    myContext->Display(myPreview, shaded ? AIS_Shaded : AIS_WireFrame, -1, Standard_False);
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearPreview()
{
    if (myContext.IsNull() || myPreview.IsNull()) return;

    myContext->Remove(myPreview, Standard_False);
    myPreview.Nullify();
    myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::hasPreview() const
{
    return !myPreview.IsNull();
}

TopoDS_Shape OcctViewWidget::previewShape() const
{
    return myPreview.IsNull() ? TopoDS_Shape() : myPreview->Shape();
}

void OcctViewWidget::setModelingPreview(const TopoDS_Shape& shape, int replacesSolidId)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearModelingPreview();
    if (shape.IsNull()) return;

    ModelingOps::tessellate(shape, 0.1);

    myModelingPreview = new AIS_Shape(shape);
    // The same yellow setPreview() uses. One rule - a preview is yellow, a
    // body is grey - rather than a second preview colour per feature. It also
    // has to differ from the pull arrow standing on top of it: both were
    // Theme::accent() at first, and the magnified capture showed an arrow
    // that was technically drawn and practically invisible against the shape
    // it was pulling.
    myModelingPreview->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
    myModelingPreview->SetWidth(2.0);
    // Selection mode -1: feedback only, never pickable - the same rule the
    // sketch preview and every marker follows. A shape the user can select
    // that exists in no document is the worst thing a preview can produce.
    myContext->Display(myModelingPreview, AIS_Shaded, -1, Standard_False);

    // The body this preview stands in for becomes a cage for the duration -
    // see the header for why, and why this is SetDisplayMode rather than
    // Erase (Erase would drop the selection the gizmo's predicate reads).
    const auto it = mySolids.find(replacesSolidId);
    if (it != mySolids.end()) {
        myContext->SetDisplayMode(it->second, AIS_WireFrame, Standard_False);
        myModelingPreviewSolid = replacesSolidId;
    }

    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearModelingPreview()
{
    if (myContext.IsNull()) return;

    bool changed = false;
    if (myModelingPreviewSolid >= 0) {
        const auto it = mySolids.find(myModelingPreviewSolid);
        if (it != mySolids.end()) {
            myContext->SetDisplayMode(it->second, myWireframe ? AIS_WireFrame : AIS_Shaded,
                                      Standard_False);
            changed = true;
        }
        // Cleared even when the body has gone (a commit replaces it), so the
        // id can never be restored onto a different body later.
        myModelingPreviewSolid = -1;
    }
    if (!myModelingPreview.IsNull()) {
        myContext->Remove(myModelingPreview, Standard_False);
        myModelingPreview.Nullify();
        changed = true;
    }
    if (changed) myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::hasModelingPreview() const
{
    return !myModelingPreview.IsNull();
}

TopoDS_Shape OcctViewWidget::modelingPreviewShape() const
{
    return myModelingPreview.IsNull() ? TopoDS_Shape() : myModelingPreview->Shape();
}

void OcctViewWidget::showPullArrow(const gp_Pnt& centre, const gp_Dir& outward)
{
    initializeViewer();
    if (myView.IsNull()) return;
    // No viewer update of its own while a camera change is being applied:
    // applyCameraState() emits cameraChanged() and then redraws, and this
    // rebuild rides along with that redraw. Forcing one here as well made
    // every orbit step pay for two vsync-bound frames instead of one.
    myPullArrow.show(centre, outward, myView->Camera()->Direction(), worldPerPixel(),
                     /*updateViewer=*/!myApplyingCamera);
}

void OcctViewWidget::clearPullArrow()
{
    myPullArrow.clear();
    myPullDrag.active = false;
}

bool OcctViewWidget::pullArrowHead(gp_Pnt& out) const
{
    if (!myPullArrow.isShowing()) return false;
    out = myPullArrow.head();
    return true;
}

void OcctViewWidget::showBevelArrow(const gp_Pnt& centre, const gp_Dir& outward)
{
    initializeViewer();
    if (myView.IsNull()) return;
    // No viewer update of its own while a camera change is being applied - the
    // same rule showPullArrow() keeps, and for the same measured reason.
    myBevelArrow.show(centre, outward, myView->Camera()->Direction(), worldPerPixel(),
                      /*updateViewer=*/!myApplyingCamera);
}

void OcctViewWidget::clearBevelArrow()
{
    myBevelArrow.clear();
    myBevelDrag.active = false;
}

bool OcctViewWidget::bevelArrowHead(gp_Pnt& out) const
{
    if (!myBevelArrow.isShowing()) return false;
    out = myBevelArrow.head();
    return true;
}

void OcctViewWidget::setEdgeDimensionSuppressed(bool suppressed)
{
    if (myEdgeDimensionSuppressed == suppressed) return;
    myEdgeDimensionSuppressed = suppressed;
    // Re-derive rather than only clear: turning it back off has to put the
    // annotation back if a hover or a selection still calls for one, which is
    // exactly what updateEdgeDimension() decides. A one-way clear here would
    // be a state that only one direction maintains - the rule this file's
    // sibling-visibility comments already record twice.
    updateEdgeDimension();
    if (!myView.IsNull()) myView->Redraw();
}

bool OcctViewWidget::arrowHit(const PullArrowRenderer& arrow, const QPoint& point) const
{
    if (!arrow.isShowing()) return false;

    QPoint tail, head;
    if (!projectToScreen(arrow.tail(), tail)) return false;
    if (!projectToScreen(arrow.head(), head)) return false;

    // Distance from the point to the projected shaft, in pixels. A generous
    // 14 px: the arrow is a hairline, and a target the user has to hit
    // exactly is one they will miss.
    const double dx = head.x() - tail.x();
    const double dy = head.y() - tail.y();
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 1.0e-9) {
        t = ((point.x() - tail.x()) * dx + (point.y() - tail.y()) * dy) / lengthSquared;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double nx = tail.x() + dx * t - point.x();
    const double ny = tail.y() + dy * t - point.y();
    return std::sqrt(nx * nx + ny * ny) <= 14.0;
}

void OcctViewWidget::beginAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at)
{
    drag.active = true;
    drag.moved = false;
    drag.value = 0.0;
    gp_Lin ray;
    drag.hasPressParam = rayThroughPixel(at.x(), at.y(), ray) &&
                         CameraController::axisParameterForRay(ray, axis, drag.pressParam);
}

bool OcctViewWidget::advanceAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at)
{
    // Where the cursor now points along the arrow's axis, minus where it
    // pointed at the press. A ray too close to parallel with the axis resolves
    // to nothing and the last value simply stands - see
    // CameraController::axisParameterForRay().
    gp_Lin ray;
    double parameter = 0.0;
    if (!rayThroughPixel(at.x(), at.y(), ray) ||
        !CameraController::axisParameterForRay(ray, axis, parameter))
        return false;

    if (!drag.hasPressParam) {
        // The press itself could not be measured (see beginAxisDrag). Anchor
        // here instead, the first moment it can be anchored at all: the drag
        // contributes nothing until the angle improves and then starts from
        // zero, rather than jumping by whatever the unmeasurable press would
        // have implied.
        drag.pressParam = parameter;
        drag.hasPressParam = true;
        return false;
    }

    double value = parameter - drag.pressParam;
    // The same grid the outline points snap to, applied to the dragged
    // distance rather than to a position.
    if (mySnapEnabled && mySnapStep > 0.0)
        value = std::round(value / mySnapStep) * mySnapStep;
    if (std::fabs(value - drag.value) <= 1.0e-9) return false;

    drag.value = value;
    if (std::fabs(value) > 1.0e-9) drag.moved = true;
    return true;
}

void OcctViewWidget::attachManipulator(int solidId)
{
    initializeViewer();
    if (myContext.IsNull()) return;
    // Idempotent per body. The predicate that drives this runs on every
    // appStateChanged, and a fresh AIS_Manipulator on each of those would
    // re-derive its position from the bounding box every time - including in
    // the middle of a gesture, which is how a gizmo ends up snapping back to
    // the body while the user is still holding it.
    if (!myManipulator.IsNull() && myManipulatorSolid == solidId) return;

    detachManipulator();
    const auto it = mySolids.find(solidId);
    if (it == mySolids.end() || !myContext->IsDisplayed(it->second)) return;

    myManipulator = new AIS_Manipulator();
    // Modes arm on DETECTION, not on selection. The alternative - OCCT's
    // default - activates a mode when a manipulator part is SELECTED, and
    // selecting a part replaces the body selection that raised the gizmo in
    // the first place: the gizmo would vanish under the hand reaching for it.
    myManipulator->SetModeActivationOnDetection(Standard_True);
    // Sized and placed from the body it serves, so a 40 mm shelf and a 2 m
    // wardrobe both get a gizmo you can actually grab.
    AIS_Manipulator::OptionsForAttach options;
    options.SetAdjustPosition(Standard_True);
    options.SetAdjustSize(Standard_True);
    options.SetEnableModes(Standard_True);
    myManipulator->Attach(it->second, options);
    activateManipulatorModes();
    // The one styling hook AIS_Manipulator actually exposes: the shading
    // aspect its parts are computed from. The per-axis HUES are private
    // (AIS_Manipulator::Axis::myColor, set in init() and reachable through no
    // public setter), and red/green/blue for X/Y/Z is the universal gizmo
    // language anyway - tinting all three to one accent would cost more than
    // it bought. What this does reach is the material, so the gizmo reads as
    // part of this app's matte surface family rather than a glossy default.
    const Handle(Prs3d_ShadingAspect) gizmoAspect =
        myManipulator->Attributes()->ShadingAspect();
    if (!gizmoAspect.IsNull()) {
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);
        material.SetAmbientColor(Quantity_Color(0.35, 0.35, 0.35, Quantity_TOC_sRGB));
        material.SetDiffuseColor(Quantity_Color(0.75, 0.75, 0.75, Quantity_TOC_sRGB));
        material.SetSpecularColor(Quantity_Color(0.05, 0.05, 0.05, Quantity_TOC_sRGB));
        gizmoAspect->SetMaterial(material);
    }

    myManipulatorSolid = solidId;
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::activateManipulatorModes()
{
    if (myManipulator.IsNull()) return;
    // Move along an axis, Move in a plane, Rotate, Scale - every mode the API
    // offers. gp_Trsf cannot express a per-axis scale, so the scale cubes are
    // uniform whichever one is grabbed; see MainWindow's bake for the clamp
    // that keeps a uniform scale to something that is still furniture.
    myManipulator->EnableMode(AIS_MM_Translation);
    myManipulator->EnableMode(AIS_MM_TranslationPlane);
    myManipulator->EnableMode(AIS_MM_Rotation);
    myManipulator->EnableMode(AIS_MM_Scaling);
}

void OcctViewWidget::detachManipulator()
{
    if (myManipulator.IsNull()) {
        myManipulatorSolid = -1;
        return;
    }

    // A gesture cannot outlive the gizmo it was made on - and OCCT's Detach()
    // does NOT put back the local transformations a live drag has written, so
    // dropping the flag alone would leave the body frozen at the pose the drag
    // reached while the document still said something else. Cancelling it is
    // the reset. Unreachable today, because every route in here runs between
    // gestures rather than during one; that is exactly what makes the line
    // cheap to have, and it is the difference between a future mid-drag detach
    // being harmless and being a body stuck where nothing put it.
    if (myManipulator->HasActiveTransformation())
        myManipulator->StopTransform(Standard_False);
    myGizmoDragActive = false;
    myGizmoDelta = gp_Trsf();
    // Detach() erases it from the context as well as letting go of the body.
    myManipulator->Detach();
    if (!myContext.IsNull()) {
        myContext->Remove(myManipulator, Standard_False);
        myContext->UpdateCurrentViewer();
    }
    myManipulator.Nullify();
    myManipulatorSolid = -1;
}

bool OcctViewWidget::manipulatorFrame(gp_Ax2& position, double& size) const
{
    if (myManipulator.IsNull()) return false;
    position = myManipulator->Position();
    size = myManipulator->Size();
    return true;
}

int OcctViewWidget::manipulatorActiveMode() const
{
    return myManipulator.IsNull() ? 0 : static_cast<int>(myManipulator->ActiveMode());
}

int OcctViewWidget::manipulatorActiveAxis() const
{
    if (myManipulator.IsNull() || myManipulator->ActiveMode() == AIS_MM_None) return -1;
    return myManipulator->ActiveAxisIndex();
}

bool OcctViewWidget::solidPresentationTransform(int id, gp_Trsf& out) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || it->second.IsNull()) return false;
    out = it->second->LocalTransformation();
    return true;
}

bool OcctViewWidget::detectedIsManipulator() const
{
    if (myManipulator.IsNull() || myContext.IsNull()) return false;
    const Handle(AIS_InteractiveObject) detected = myContext->DetectedInteractive();
    return !detected.IsNull() && detected.get() == myManipulator.get();
}

void OcctViewWidget::endGizmoDrag()
{
    myGizmoDragActive = false;
    if (myManipulator.IsNull()) return;

    const int solidId = myManipulatorSolid;
    const gp_Pnt pivot = myGizmoStartPosition.Location();
    gp_Trsf delta = myGizmoDelta;
    myGizmoDelta = gp_Trsf();

    // StopTransform(false), not (true): it restores every attached object's
    // local transformation and the manipulator's own position to what they
    // were at the press. The drag moved the PRESENTATION and nothing else, and
    // the document only changes if the bake that follows succeeds - so the
    // presentation goes back first, unconditionally, and the consumer's job is
    // purely to add a change rather than to undo one it did not make. A bake
    // that is refused therefore leaves the viewport already agreeing with the
    // document instead of showing a pose that exists nowhere.
    myManipulator->StopTransform(Standard_False);
    myManipulator->DeactivateCurrentMode();
    if (!myView.IsNull()) myView->Redraw();

    if (mySnapEnabled) {
        // The same 10 mm grid outline points and face pulls land on, plus the
        // two steps this gesture adds. 15 degrees is the smallest rotation
        // anyone eyeballs; 5% is a size change you can see without measuring.
        delta = ModelingOps::snapTransform(delta, pivot, mySnapStep,
                                           kGizmoRotationStepDeg, kGizmoScaleStep);
    }

    emit gizmoReleased(solidId, delta);
}

void OcctViewWidget::setSketchPointMarkers(const std::vector<gp_Pnt>& points)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearSketchPointMarkers();
    if (points.empty()) return;

    // Aspect_TOM_POINT was tried first for the ordinary dots and is
    // documented as OCCT's smallest displayable dot, but it drew nothing at
    // all in this build - a snapshot caught that before it shipped (see
    // this class's own comment on Graphic3d_ArrayOfTriangles for the
    // earlier instance of the same lesson). Aspect_TOM_BALL, tried next,
    // does draw - but pixel-sampled, it turned out to be a small HOLLOW
    // ring rather than the filled disc its name and doc suggest, in this
    // build. It still reads clearly as "a small marker at this point",
    // which is what matters here.
    for (const gp_Pnt& p : points) {
        Handle(SketchPointMarker) dot =
            makeMarker(p, Aspect_TOM_BALL, toOcctColor(Theme::sketchPointMarker()), 1.5);
        myContext->Display(dot, 0, -1, Standard_False);
        myPlacedMarkers.push_back(dot);
    }

    // The first point additionally gets a ring around its dot - "a ring, or
    // a larger dot" - because clicking it back is what closes the outline,
    // and that has to be visibly true, not just structurally true: a first
    // pass used the same colour as the ordinary dots and only a modest
    // scale bump (2.2 against 1.5), and pixel-sampling the two side by side
    // showed IDENTICAL marker geometry - whatever this driver does with the
    // scale argument at these small deltas, it was not visible. Scale 4.0
    // against 1.5 does clear that threshold (confirmed by the same
    // pixel-sampling, and it is dramatically bigger - see the crop
    // comparison in the branch's report), but Theme::focusRing() (amber, a
    // hue no other placed-point marker or the cursor dot carries) is the
    // difference this does not have to hope survives a rendering quirk.
    Handle(SketchPointMarker) ring =
        makeMarker(points.front(), Aspect_TOM_RING1, toOcctColor(Theme::focusRing()), 4.0);
    myContext->Display(ring, 0, -1, Standard_False);
    myFirstPointMarker = ring;

    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearSketchPointMarkers()
{
    if (!myContext.IsNull()) {
        for (auto& marker : myPlacedMarkers) myContext->Remove(marker, Standard_False);
        if (!myFirstPointMarker.IsNull()) myContext->Remove(myFirstPointMarker, Standard_False);
    }
    myPlacedMarkers.clear();
    myFirstPointMarker.Nullify();
    if (!myContext.IsNull()) myContext->UpdateCurrentViewer();
}

int OcctViewWidget::sketchPointMarkerCount() const
{
    return static_cast<int>(myPlacedMarkers.size());
}

bool OcctViewWidget::hasSketchStartMarker() const
{
    return !myFirstPointMarker.IsNull();
}

void OcctViewWidget::setSketchCursorMarker(const gp_Pnt& point)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearSketchCursorMarker();
    // Same marker family as the placed dots (Aspect_TOM_BALL, proven above
    // to actually render), but larger and in Theme::accent() - already the
    // viewport's own colour for "here's the interactive thing", via
    // DimensionRenderer's annotation lines - so the live cursor is never
    // mistaken for a point already committed.
    Handle(SketchPointMarker) cursor =
        makeMarker(point, Aspect_TOM_BALL, toOcctColor(Theme::accent()), 2.0);
    myContext->Display(cursor, 0, -1, Standard_False);
    myCursorMarker = cursor;
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearSketchCursorMarker()
{
    if (!myContext.IsNull() && !myCursorMarker.IsNull()) myContext->Remove(myCursorMarker, Standard_False);
    myCursorMarker.Nullify();
    if (!myContext.IsNull()) myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::hasSketchCursorMarker() const
{
    return !myCursorMarker.IsNull();
}

void OcctViewWidget::applySelectionMode(const Handle(AIS_Shape)& shape)
{
    if (myContext.IsNull() || shape.IsNull()) return;

    const int mode = mySelectionMode == SelectionMode::Face  ? kSelectionModeFace
                    : mySelectionMode == SelectionMode::Edge ? kSelectionModeEdge
                                                              : kSelectionModeWholeShape;
    myContext->Deactivate(shape);
    myContext->Activate(shape, mode);
}

void OcctViewWidget::setSelectionMode(SelectionMode mode)
{
    if (mode == mySelectionMode) return;

    mySelectionMode = mode;
    myDimension.clear();   // a hover annotation from the old mode means nothing in the new one
    if (myContext.IsNull()) return;

    myContext->ClearSelected(Standard_False);
    for (auto& entry : mySolids) applySelectionMode(entry.second);
    myContext->UpdateCurrentViewer();
    emit selectionChanged();
}

void OcctViewWidget::setSnap(bool enabled, double step)
{
    mySnapEnabled = enabled;
    if (step > 0.0) mySnapStep = step;
}

void OcctViewWidget::setWorkPlane(const gp_Pln& plane)
{
    mySketchPlane = plane;
    // The grid is drawn on this plane, so it has to be rebuilt now rather
    // than on the next camera move: locking a face and seeing the grid still
    // lying on the ground is the whole failure this call exists to prevent.
    if (myView.IsNull()) return;
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane());
    myView->Redraw();
}

TopoDS_Face OcctViewWidget::selectedFace() const
{
    if (myContext.IsNull()) return TopoDS_Face();

    TopoDS_Face found;
    int faces = 0;
    for (myContext->InitSelected(); myContext->MoreSelected(); myContext->NextSelected()) {
        const TopoDS_Shape shape = myContext->SelectedShape();
        if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) continue;
        if (++faces > 1) return TopoDS_Face();
        found = TopoDS::Face(shape);
    }
    return found;
}

QPoint OcctViewWidget::toDevicePixels(const QPoint& logical) const
{
    const double ratio = devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround(logical.x() * ratio)),
                  static_cast<int>(std::lround(logical.y() * ratio)));
}

QPoint OcctViewWidget::fromDevicePixels(int px, int py) const
{
    const double ratio = std::max(devicePixelRatioF(), 1.0e-6);
    return QPoint(static_cast<int>(std::lround(px / ratio)),
                  static_cast<int>(std::lround(py / ratio)));
}

bool OcctViewWidget::projectToScreen(const gp_Pnt& world, QPoint& out) const
{
    if (myView.IsNull()) return false;

    Standard_Integer px = 0, py = 0;
    myView->Convert(world.X(), world.Y(), world.Z(), px, py);
    // Back into the logical space every Qt caller lives in - see
    // toDevicePixels()'s comment in the header.
    out = fromDevicePixels(static_cast<int>(px), static_cast<int>(py));
    return true;
}

void OcctViewWidget::setSketchMode(bool enabled, const gp_Pln& plane)
{
    mySketchMode = enabled;
    setWorkPlane(plane);
    if (enabled) clearSelection();
    // The live segment dimension belongs to one sketch: cleared whether this
    // one just committed or was cancelled, and again on entry so a stale
    // edge-hover annotation cannot bleed into the sketch that follows it.
    myDimension.clear();
    myHasLastHoverPoint = false;
    // Same rule for the point markers: gone on commit, on cancel, and on
    // entry, so nothing from a previous sketch can survive into this one.
    clearSketchPointMarkers();
    clearSketchCursorMarker();
}

gp_Pln OcctViewWidget::gridPlane() const
{
    // Since a face can be locked, the grid is drawn exactly coplanar with a
    // shaded face, and two coplanar surfaces are a depth-buffer tie: the grid
    // stipples through the face and flickers as the camera moves. So the grid
    // is displaced a hair toward whichever side of the plane the eye is on.
    //
    // Not Graphic3d_ZLayerId_Topmost: that layer draws with the depth buffer
    // cleared, so the GROUND grid would then paint over every body standing
    // on it. And not a depth-offset ZLayer either - OCCT's
    // Graphic3d_ZLayerSettings depth offset drives glPolygonOffset in
    // Aspect_POM_Fill mode, which does nothing at all to the line primitives
    // this grid is made of.
    //
    // The displacement has to be PROPORTIONAL to the camera distance - depth
    // precision degrades with distance, so a fixed offset that clears the
    // buffer up close does not clear it far away - and it also has to be
    // QUANTIZED, because GridRenderer caches on the plane it last built and a
    // nudge that changed with every wheel notch would rebuild the whole grid
    // every frame.
    //
    // Tying it to the grid's minor step gave the quantization but not the
    // proportionality: minorStepFor() holds one value across a whole band, so
    // the ratio swung twentyfold inside it - 1.7e-4 at 120 mm down to 8e-6 at
    // 2499 mm, and ~2000 mm is this app's ordinary furniture-viewing distance,
    // the thin end of that swing. Rounding the distance itself to a power of
    // two keeps a constant ratio to within a factor of root two while still
    // changing only when the camera moves a whole octave. At 1e-4 of the
    // viewing distance the nudge is about a tenth of a pixel at any distance.
    const double distance = std::max(1.0, myCamera.state().distance);
    const double octave = std::ldexp(1.0, static_cast<int>(std::lround(std::log2(distance))));
    const double nudge = octave * 1.0e-4;
    const gp_Dir normal = mySketchPlane.Axis().Direction();
    const gp_Vec toEye(mySketchPlane.Location(), myCamera.eyePosition());

    gp_Pln plane = mySketchPlane;
    plane.Translate(gp_Vec(normal) * (toEye.Dot(gp_Vec(normal)) >= 0.0 ? nudge : -nudge));
    return plane;
}

bool OcctViewWidget::rayThroughPixel(int px, int py, gp_Lin& out) const
{
    if (myView.IsNull()) return false;

    const QPoint device = toDevicePixels(QPoint(px, py));
    Standard_Real x = 0.0, y = 0.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0;
    myView->ConvertWithProj(device.x(), device.y(), x, y, z, vx, vy, vz);
    out = gp_Lin(gp_Pnt(x, y, z), gp_Dir(vx, vy, vz));
    return true;
}

bool OcctViewWidget::pointOnSketchPlane(int px, int py, gp_Pnt& out) const
{
    gp_Lin ray;
    if (!rayThroughPixel(px, py, ray)) return false;

    if (!SketchController::intersectRayWithPlane(ray, mySketchPlane, out)) return false;
    // A perspective camera has a horizon: an intersection with the sketch plane
    // can lie BEHIND the eye when the cursor is above it. Such a hit is not a
    // point the user can see - reject it.
    const gp_Vec toHit(ray.Location(), out);
    if (toHit.Dot(gp_Vec(ray.Direction())) <= 0.0) return false;

    if (mySnapEnabled) {
        out = SketchController::snapToPlaneGrid(out, mySketchPlane, mySnapStep);
    }
    return true;
}

bool OcctViewWidget::pickWorldPoint(int px, int py, gp_Pnt& out) const
{
    // Prefer a real hit on the model: MoveTo + detection gives the picked point
    // on the surface under the cursor.
    if (!myContext.IsNull() && !myView.IsNull()) {
        const QPoint device = toDevicePixels(QPoint(px, py));
        myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
        if (myContext->HasDetected()) {
            const Handle(StdSelect_ViewerSelector3d) selector = myContext->MainSelector();
            if (selector->NbPicked() > 0) {
                out = selector->PickedPoint(1);
                return true;
            }
        }
    }
    // Otherwise the ground plane, reusing the sketch unprojection.
    gp_Lin ray;
    if (!rayThroughPixel(px, py, ray)) return false;
    const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
    if (!SketchController::intersectRayWithPlane(ray, ground, out)) return false;
    // A perspective camera has a horizon: an intersection with the ground can
    // lie BEHIND the eye when the cursor is above it. Such a hit is not a
    // point the user can see - reject it.
    const gp_Vec toHit(ray.Location(), out);
    if (toHit.Dot(gp_Vec(ray.Direction())) <= 0.0) return false;
    return true;
}

bool OcctViewWidget::lastHoverPoint(gp_Pnt& out) const
{
    if (!myHasLastHoverPoint) return false;
    out = myLastHoverPoint;
    return true;
}

double OcctViewWidget::worldPerPixel() const
{
    // World units per pixel at target depth, for a perspective camera - the
    // same maths panning already used inline.
    return 2.0 * myCamera.state().distance *
           std::tan(0.5 * kFovyDeg * 3.14159265358979323846 / 180.0) /
           std::max(1, height());
}

TopoDS_Edge OcctViewWidget::selectedEdge() const
{
    if (myContext.IsNull()) return TopoDS_Edge();

    TopoDS_Edge found;
    int edges = 0;
    for (myContext->InitSelected(); myContext->MoreSelected(); myContext->NextSelected()) {
        if (!myContext->HasSelectedShape()) continue;
        const TopoDS_Shape shape = myContext->SelectedShape();
        if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) continue;
        // Same rule as selectedFace(): "the one selected edge", never "the
        // first of several", so a dimension can never be a coin toss between
        // two highlighted edges.
        if (++edges > 1) return TopoDS_Edge();
        found = TopoDS::Edge(shape);
    }
    return found;
}

void OcctViewWidget::updateEdgeDimension()
{
    // Suppressed while the bevel arrow's value chip is up: two annotations on
    // one edge is noise, and the chip is the more specific of the two. See
    // setEdgeDimensionSuppressed(), which MainWindow drives off the same
    // predicate that raises the arrow.
    if (myEdgeDimensionSuppressed || mySelectionMode != SelectionMode::Edge ||
        myContext.IsNull() || myView.IsNull()) {
        myDimension.clear();
        return;
    }

    // Hover first, selection second. Acceptance criterion 1 asks for both, and
    // this is the single place that decides between them: the hovered edge is
    // what the cursor is asking about right now, and a selected edge is what
    // the user asked about and has not let go of - so moving the cursor off a
    // selected edge falls back to it rather than dropping the annotation, which
    // is what used to happen.
    TopoDS_Edge edge;
    if (myContext->HasDetectedShape() &&
        myContext->DetectedShape().ShapeType() == TopAbs_EDGE) {
        edge = TopoDS::Edge(myContext->DetectedShape());
    } else {
        edge = selectedEdge();
    }
    if (edge.IsNull()) {
        myDimension.clear();
        return;
    }

    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2);
    if (v1.IsNull() || v2.IsNull()) {
        myDimension.clear();
        return;
    }

    const gp_Pnt from = BRep_Tool::Pnt(v1);
    const gp_Pnt to = BRep_Tool::Pnt(v2);
    const gp_Vec along(from, to);
    if (along.Magnitude() < 1.0e-7) {
        myDimension.clear();
        return;
    }

    // Extension lines run sideways in the screen plane - perpendicular to
    // both the edge and the direction we are looking - so they read the same
    // whichever way the camera happens to be turned.
    gp_Vec sideways = along.Crossed(gp_Vec(myView->Camera()->Direction()));
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(0.0, 0.0, 1.0).Crossed(along);
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(1.0, 0.0, 0.0);

    myDimension.show(from, to, gp_Dir(sideways), worldPerPixel());
}

std::vector<int> OcctViewWidget::selectedSolidIds() const
{
    std::vector<int> ids;
    if (myContext.IsNull()) return ids;

    for (myContext->InitSelected(); myContext->MoreSelected(); myContext->NextSelected()) {
        const Handle(AIS_InteractiveObject) picked = myContext->SelectedInteractive();
        if (picked.IsNull()) continue;

        for (const auto& entry : mySolids) {
            if (entry.second.get() != picked.get()) continue;
            if (std::find(ids.begin(), ids.end(), entry.first) == ids.end()) {
                ids.push_back(entry.first);
            }
            break;
        }
    }
    return ids;
}

void OcctViewWidget::clearSelection()
{
    if (myContext.IsNull()) return;

    myContext->ClearSelected(Standard_True);
    updateEdgeDimension();   // nothing selected, so nothing left for it to fall back to
    emit selectionChanged();
}

void OcctViewWidget::setSelectedSolids(const std::vector<int>& ids)
{
    if (myContext.IsNull()) return;

    myContext->ClearSelected(Standard_False);
    for (int id : ids) {
        const auto it = mySolids.find(id);
        if (it == mySolids.end()) continue;
        if (!myContext->IsDisplayed(it->second)) continue;   // never select the hidden
        myContext->AddOrRemoveSelected(it->second, Standard_False);
    }
    updateEdgeDimension();
    myContext->UpdateCurrentViewer();
    emit selectionChanged();
}

bool OcctViewWidget::saveSnapshot(const QString& path)
{
    if (myView.IsNull()) return false;

    myView->Redraw();
    return myView->Dump(path.toUtf8().constData()) == Standard_True;
}

void OcctViewWidget::applyCameraState()
{
    if (myView.IsNull()) return;

    const Handle(Graphic3d_Camera) cam = myView->Camera();
    const gp_Pnt eye = myCamera.eyePosition();
    const gp_Pnt& at = myCamera.state().target;
    const gp_Dir up = myCamera.upVector();
    cam->SetEye(eye);
    cam->SetCenter(at);
    cam->SetUp(up);
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane());
    // Slots FIRST, redraw second. A slot on cameraChanged() that changes the
    // scene - PullArrow rebuilds its 3D arrow, which is sized in screen
    // pixels and so has to be rebuilt whenever the camera moves - was
    // otherwise both one frame stale and forced to call UpdateCurrentViewer()
    // itself, so every orbit step cost two vsync-bound redraws instead of
    // one. myApplyingCamera is how showPullArrow() knows the redraw below is
    // coming; nothing else reads it.
    myApplyingCamera = true;
    emit cameraChanged();
    myApplyingCamera = false;
    myView->Redraw();
}

void OcctViewWidget::fitAll()
{
    if (myView.IsNull()) return;

    // Frame everything we display ourselves (the grid and view cube are
    // presentation furniture, not content).
    Bnd_Box box;
    for (const auto& entry : mySolids) {
        Bnd_Box b;
        BRepBndLib::Add(entry.second->Shape(), b);
        box.Add(b);
    }
    if (box.IsVoid()) box.Update(-250.0, -250.0, 0.0, 250.0, 250.0, 10.0);
    CameraController scratch = myCamera;
    scratch.frame(box, kFovyDeg);
    animateTo(scratch.state());
}

void OcctViewWidget::stopCameraAnimation()
{
    if (myCameraAnimation) {
        myCameraAnimation->stop();   // leaves the camera wherever it got to
        myCameraAnimation->deleteLater();
        myCameraAnimation = nullptr;
    }
}

void OcctViewWidget::animateTo(const CameraState& goal)
{
    stopCameraAnimation();
    if (!myAnimationsEnabled) {
        myCamera.setState(goal);
        applyCameraState();
        return;
    }

    const CameraState from = myCamera.state();
    // Interpolate azimuth along the shortest arc so 350 -> 10 turns 20 degrees.
    const double azDelta = CameraController::shortestArcDelta(from.azimuthDeg, goal.azimuthDeg);

    auto* animation = new QVariantAnimation(this);
    // Deliberately its own constant, not Theme::motionMs(): a camera move is
    // not a UI transition, and reading well at the same speed as a chip
    // hover would be a coincidence, not a rule. Keep this at 250 ms even if
    // Theme::motionMs() (160 ms) ever changes.
    animation->setDuration(250);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, from, goal, azDelta](const QVariant& value) {
                const double t = value.toDouble();
                CameraState s;
                s.azimuthDeg = from.azimuthDeg + azDelta * t;
                s.elevationDeg = from.elevationDeg + (goal.elevationDeg - from.elevationDeg) * t;
                s.distance = from.distance + (goal.distance - from.distance) * t;
                s.target = gp_Pnt(from.target.X() + (goal.target.X() - from.target.X()) * t,
                                  from.target.Y() + (goal.target.Y() - from.target.Y()) * t,
                                  from.target.Z() + (goal.target.Z() - from.target.Z()) * t);
                myCamera.setState(s);
                applyCameraState();
            });
    connect(animation, &QVariantAnimation::finished, this, [this, goal] {
        myCamera.setState(goal);
        applyCameraState();
        myCameraAnimation = nullptr;
    });
    myCameraAnimation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

namespace {
// Positions in OcctViewWidget::viewLabelNames(). Naming them keeps
// viewLabelText() readable while it returns entries OF that list rather than
// its own copies of the same seven literals.
enum ViewName { NamePersp = 0, NameTop, NameBottom, NameFront, NameBack, NameRight, NameLeft };
}  // namespace

const QStringList& OcctViewWidget::viewLabelNames()
{
    // Built once. viewLabelText() runs on every camera frame, so this must not
    // allocate a seven-string list per orbit step.
    static const QStringList names = {
        QStringLiteral("Persp"),  QStringLiteral("Top"),   QStringLiteral("Bottom"),
        QStringLiteral("Front"),  QStringLiteral("Back"),  QStringLiteral("Right"),
        QStringLiteral("Left")};
    return names;
}

QString OcctViewWidget::viewLabelText() const
{
    const QStringList& names = viewLabelNames();
    const CameraState& state = myCamera.state();
    const double el = state.elevationDeg;
    // Azimuth normalized to (-180, 180] for comparison.
    double az = std::fmod(state.azimuthDeg, 360.0);
    if (az > 180.0) az -= 360.0;
    if (az <= -180.0) az += 360.0;

    const double tolerance = 0.5;
    if (el >= 87.5) return names.at(NameTop);
    if (el <= -87.5) return names.at(NameBottom);
    if (std::fabs(el) < tolerance) {
        if (std::fabs(az) < tolerance) return names.at(NameFront);
        if (std::fabs(std::fabs(az) - 180.0) < tolerance) return names.at(NameBack);
        if (std::fabs(az + 90.0) < tolerance) return names.at(NameRight);
        if (std::fabs(az - 90.0) < tolerance) return names.at(NameLeft);
    }
    return names.at(NamePersp);
}

void OcctViewWidget::setViewAxonometric()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = -45.0; s.elevationDeg = 30.0;
    animateTo(s);
}

void OcctViewWidget::setViewTop()
{
    CameraState s = myCamera.state();
    s.elevationDeg = 89.0;   // inside the clamp: a true 90 makes azimuth degenerate
    animateTo(s);
}

void OcctViewWidget::setViewFront()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = 0.0; s.elevationDeg = 0.0;
    animateTo(s);
}

void OcctViewWidget::setViewRight()
{
    CameraState s = myCamera.state();
    s.azimuthDeg = -90.0; s.elevationDeg = 0.0;
    animateTo(s);
}

void OcctViewWidget::setWireframe(bool wireframe)
{
    if (myWireframe == wireframe) return;
    myWireframe = wireframe;

    if (myContext.IsNull()) return;   // state kept; re-applied once the viewer initialises

    const Standard_Integer mode = wireframe ? AIS_WireFrame : AIS_Shaded;
    for (auto& entry : mySolids) myContext->SetDisplayMode(entry.second, mode, Standard_False);
    myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::isSolidWireframe(int id) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || myContext.IsNull()) return false;
    return myContext->IsDisplayed(it->second, AIS_WireFrame);
}

void OcctViewWidget::mousePressEvent(QMouseEvent* event)
{
    stopCameraAnimation();
    initializeViewer();
    myLastPos = event->position().toPoint();

    // Unity-style mapping, per the user's preference: RMB orbits around the
    // current view target (which moves only when you pan or frame something -
    // no cursor-anchored re-pivoting), MMB pans.
    if (event->button() == Qt::RightButton) {
        myOrbiting = true;
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        myPanningDrag = true;
        return;
    }

    // The pull arrow owns LEFT drags that start on it, and nothing else -
    // RMB orbit and MMB pan pass straight through above, so grabbing the
    // arrow never costs the user the camera.
    if (event->button() == Qt::LeftButton && !mySketchMode && arrowHit(myPullArrow, myLastPos)) {
        // The press CLAIMS the gesture whether or not the drag maths can
        // measure it yet. It used to claim it only when
        // axisParameterForRay() resolved - so with the arrow near edge-on to
        // the camera (looking straight down it, which is exactly when a user
        // reaches for a top face from above) the press did nothing, the
        // release fell through to an ordinary pick, and the face the user had
        // just grabbed was silently deselected and its arrow dismissed. A
        // grab has to be a grab; an unmeasurable angle is a reason to
        // contribute nothing, not a reason to hand the gesture back.
        beginAxisDrag(myPullDrag, myPullArrow.axis(), myLastPos);
        return;
    }

    // The bevel arrow, on exactly the same terms - including claiming the
    // gesture at an angle the maths refuses. The two arrows are never up at
    // once (face mode against edge mode), so the order of these two blocks is
    // not load-bearing.
    if (event->button() == Qt::LeftButton && !mySketchMode && arrowHit(myBevelArrow, myLastPos)) {
        beginAxisDrag(myBevelDrag, myBevelArrow.axis(), myLastPos);
        return;
    }

    // The transform gizmo owns LEFT drags that start on one of its parts, and
    // only those. RMB orbit and MMB pan returned above; a Shift-click is the
    // "add this body to the selection" gesture and must reach the picker even
    // when it lands on an arm of the gizmo standing on the first body.
    const bool additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (event->button() == Qt::LeftButton && !mySketchMode && !additive &&
        !myManipulator.IsNull() && !myContext.IsNull() && !myView.IsNull()) {
        // Detection is what arms a mode (SetModeActivationOnDetection), so the
        // press asks for it at its own pixel rather than trusting whatever the
        // last hover happened to leave behind.
        const QPoint device = toDevicePixels(myLastPos);
        myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
        if (detectedIsManipulator()) {
            // The press CLAIMS the gesture whether or not a mode armed - the
            // pull arrow's lesson one gizmo over. If it fell through, the
            // release would run an ordinary pick, select a manipulator part,
            // and drop the body selection that raised the gizmo in the first
            // place: the thing the user just grabbed would deselect itself.
            myGizmoDragActive = true;
            myGizmoDelta = gp_Trsf();
            myGizmoStartPosition = myManipulator->Position();
            if (myManipulator->HasActiveMode())
                myManipulator->StartTransform(device.x(), device.y(), myView);
            return;
        }
        // Nothing in OCCT disarms a mode when the cursor leaves the part that
        // armed it, so a press that missed says so explicitly.
        myManipulator->DeactivateCurrentMode();
    }
}

void OcctViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)  myOrbiting = false;
    if (event->button() == Qt::MiddleButton) myPanningDrag = false;

    // The end of a pull. This widget picks NOTHING on this release: the press
    // that started the drag was aimed at the arrow, and re-picking here would
    // replace the face selection that raised the arrow in the first place -
    // the same class of bug WA_NoMousePropagation closes for the Qt overlays,
    // one layer down, where the culprit is this widget's own handler rather
    // than a propagating child event.
    if (myPullDrag.active && event->button() == Qt::LeftButton) {
        myPullDrag.active = false;
        emit pullReleased(myPullDrag.moved);
        return;
    }

    // The end of a bevel drag, swallowed for exactly the same reason: the
    // press was aimed at the arrow, and re-picking here would replace the edge
    // selection that raised it.
    if (myBevelDrag.active && event->button() == Qt::LeftButton) {
        myBevelDrag.active = false;
        emit bevelReleased(myBevelDrag.moved);
        return;
    }

    // The end of a gizmo drag, swallowed for exactly the same reason.
    if (myGizmoDragActive && event->button() == Qt::LeftButton) {
        endGizmoDrag();
        return;
    }

    if (event->button() != Qt::LeftButton || myContext.IsNull()) return;

    const QPoint pos = event->position().toPoint();

    if (mySketchMode) {
        gp_Pnt hit;
        if (pointOnSketchPlane(pos.x(), pos.y(), hit)) emit sketchPointPicked(hit);
        return;   // no selection while sketching
    }

    const bool additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    // A Shift-click means "add this body to the selection", and the gizmo
    // standing on the FIRST body must not be what the pick lands on.
    // AIS_ManipulatorOwner carries a higher selection priority than a shape's
    // owner, so an arm or a ring crossing the second body wins the pick
    // outright and the click selects nothing at all - which is how a 100%
    // display found this and a 150% one did not: at the smaller scale the
    // second body sat under a ring, at the larger it did not.
    //
    // Deactivate(), not a detach: the pick only needs the manipulator's owners
    // out of the CANDIDATES, and that is exactly what deactivating its modes
    // does. Destroying and re-attaching it would do the same by demolition -
    // an Attach and four EnableMode calls and two viewer updates on every
    // additive click, even one nowhere near an arm - and would have to be put
    // back indirectly, by relying on the selectionChanged() below to reach
    // MainWindow's predicate. This restores itself, locally and
    // unconditionally, a few lines down.
    const bool hideGizmoFromPick = additive && !myManipulator.IsNull();
    if (hideGizmoFromPick) myContext->Deactivate(myManipulator);

    const QPoint device = toDevicePixels(pos);
    myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
    // A click that landed on the gizmo but never became a drag - a press the
    // gizmo declined, or a cursor that wandered onto it between press and
    // release - must not select a manipulator part: SelectDetected would
    // replace the body selection with an owner that belongs to no document,
    // and the gizmo would erase itself. It cannot be detected at all on the
    // additive path above, which is the point of that branch.
    const bool onGizmo = detectedIsManipulator();
    if (!onGizmo) {
        myContext->SelectDetected(additive ? AIS_SelectionScheme_XOR
                                           : AIS_SelectionScheme_Replace);
    }

    // Straight back into the picker, before anything can return. Unconditional
    // on purpose: a restore that some path can skip is a gizmo that silently
    // stops being grabbable.
    if (hideGizmoFromPick) activateManipulatorModes();

    if (onGizmo) {
        myManipulator->DeactivateCurrentMode();
        return;
    }
    // The selection just changed, and in edge mode the dimension follows it as
    // well as the hover - selecting a second edge has to stop the annotation
    // claiming to measure the one before it.
    updateEdgeDimension();
    myView->Redraw();
    emit selectionChanged();
}

void OcctViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (myView.IsNull()) return;

    const QPoint pos = event->position().toPoint();

    if (myGizmoDragActive) {
        // AIS_Manipulator does the drag maths. ObjectTransformation() answers
        // "given this cursor position, what transform does the armed part
        // mean" WITHOUT applying it, and returns false for a position it
        // cannot resolve - which is why it is used in place of the
        // Transform(x, y, view) convenience, whose return value is an identity
        // transform in exactly that case and would clobber the accumulated
        // delta with nothing.
        //
        // The transform it hands back is measured from the ORIGINAL press, not
        // from the previous move, so the last one is the whole gesture.
        if (!myManipulator.IsNull() && myManipulator->HasActiveTransformation()) {
            const QPoint device = toDevicePixels(pos);
            gp_Trsf trsf;
            if (myManipulator->ObjectTransformation(device.x(), device.y(), myView, trsf)) {
                myManipulator->Transform(trsf);
                myGizmoDelta = trsf;
                myView->Redraw();
            }
        }
    } else if (myPullDrag.active) {
        if (advanceAxisDrag(myPullDrag, myPullArrow.axis(), pos))
            emit pullDragged(myPullDrag.value);
    } else if (myBevelDrag.active) {
        if (advanceAxisDrag(myBevelDrag, myBevelArrow.axis(), pos))
            emit bevelDragged(myBevelDrag.value);
    } else if (myOrbiting) {
        const QPoint delta = pos - myLastPos;
        // Dragging right swings the scene right: azimuth decreases; dragging up
        // raises the eye. 0.4 deg/px and 0.3 deg/px feel close to Fusion.
        myCamera.orbit(-delta.x() * 0.4, delta.y() * 0.3);
        applyCameraState();
    } else if (myPanningDrag) {
        const QPoint delta = pos - myLastPos;
        const double wpp = worldPerPixel();
        myCamera.pan(-delta.x() * wpp, delta.y() * wpp);
        applyCameraState();
    } else if (mySketchMode) {
        // Report where the next point would land, so the rubber band and the
        // coordinate readout track the cursor before anything is committed.
        gp_Pnt onPlane;
        if (pointOnSketchPlane(pos.x(), pos.y(), onPlane)) {
            myLastHoverPoint = onPlane;
            myHasLastHoverPoint = true;
            emit sketchCursorMoved(onPlane);
        }
    } else if (!myContext.IsNull()) {
        // Hover highlight. Suppressed while sketching so the in-progress wire
        // does not fight the highlighter for attention.
        const QPoint device = toDevicePixels(pos);
        myContext->MoveTo(device.x(), device.y(), myView, Standard_True);
        // The manipulator arms a manipulation mode when one of its parts is
        // DETECTED, and OCCT disarms it for nobody - so a hover that once
        // brushed an arrow would leave every later press anywhere in the
        // viewport claiming a gizmo drag. Answered here, at the detection that
        // would otherwise have armed it, rather than guessed at later.
        if (!myManipulator.IsNull() && !detectedIsManipulator())
            myManipulator->DeactivateCurrentMode();
        updateEdgeDimension();
    }

    myLastPos = pos;
}

void OcctViewWidget::wheelEvent(QWheelEvent* event)
{
    if (myView.IsNull()) return;

    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    const QPoint pos = event->position().toPoint();
    gp_Pnt pivot;
    const bool havePivot = pickWorldPoint(pos.x(), pos.y(), pivot);

    // One wheel notch (delta 120) zooms ~12%; exponential so every notch feels
    // the same at any scale.
    const double factor = std::exp(-double(delta) / 120.0 * 0.12);
    if (havePivot) myCamera.zoomToward(pivot, factor);
    else           myCamera.zoom(factor);
    applyCameraState();
}

void OcctViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || mySketchMode || myContext.IsNull()) return;

    const QPoint pos = event->position().toPoint();
    // A second click on either arrow is another drag, not a request to frame
    // the body or lock the face underneath it.
    if (arrowHit(myPullArrow, pos) || arrowHit(myBevelArrow, pos)) return;
    const QPoint device = toDevicePixels(pos);
    myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
    if (!myContext->HasDetected()) return;
    // A second click on a gizmo handle is another grab, not a request to frame
    // the body underneath it - the same rule the pull arrow keeps above.
    if (detectedIsManipulator()) return;

    // In face mode a double-click means "sketch on this" - the second route
    // to Lock to Face, alongside the action. Framing the body instead would
    // be the one gesture that takes the camera away from the face the user
    // just chose to work on. The refusal for a non-planar face lives in
    // MainWindow, which owns the toast, not here.
    if (mySelectionMode == SelectionMode::Face && myContext->HasDetectedShape() &&
        myContext->DetectedShape().ShapeType() == TopAbs_FACE) {
        // Select it too, so the actions agree with what was just locked.
        myContext->SelectDetected(AIS_SelectionScheme_Replace);
        myView->Redraw();
        emit selectionChanged();
        emit faceDoubleClicked(TopoDS::Face(myContext->DetectedShape()));
        return;
    }

    const Handle(AIS_InteractiveObject) hit = myContext->DetectedInteractive();
    for (const auto& entry : mySolids) {
        if (entry.second.get() != hit.get()) continue;
        Bnd_Box box;
        BRepBndLib::Add(entry.second->Shape(), box);
        CameraController scratch = myCamera;
        scratch.frame(box, kFovyDeg);
        animateTo(scratch.state());
        return;
    }
}
