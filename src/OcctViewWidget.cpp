#include "OcctViewWidget.h"

#include "ModelingOps.h"
#include "SketchController.h"
#include "Theme.h"

// OCCT before Qt, for the Handle() macro clash.
#include <AIS_DisplayMode.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_NameOfColor.hxx>
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
    myView->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_Color(Quantity_NOC_WHITE),
                            0.08, V3d_ZBUFFER);

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
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target,
                          mySketchPlane);
    myDimension.attach(myContext);

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

    myContext->Remove(it->second, Standard_False);
    mySolids.erase(it);
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearSolids()
{
    if (myContext.IsNull()) return;

    for (auto& entry : mySolids) myContext->Remove(entry.second, Standard_False);
    mySolids.clear();
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
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, mySketchPlane);
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

bool OcctViewWidget::projectToScreen(const gp_Pnt& world, QPoint& out) const
{
    if (myView.IsNull()) return false;

    Standard_Integer px = 0, py = 0;
    myView->Convert(world.X(), world.Y(), world.Z(), px, py);
    out = QPoint(static_cast<int>(px), static_cast<int>(py));
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
}

bool OcctViewWidget::pointOnSketchPlane(int px, int py, gp_Pnt& out) const
{
    if (myView.IsNull()) return false;

    Standard_Real x = 0.0, y = 0.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0;
    myView->ConvertWithProj(px, py, x, y, z, vx, vy, vz);

    const gp_Lin ray(gp_Pnt(x, y, z), gp_Dir(vx, vy, vz));
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
        myContext->MoveTo(px, py, myView, Standard_False);
        if (myContext->HasDetected()) {
            const Handle(StdSelect_ViewerSelector3d) selector = myContext->MainSelector();
            if (selector->NbPicked() > 0) {
                out = selector->PickedPoint(1);
                return true;
            }
        }
    }
    // Otherwise the ground plane, reusing the sketch unprojection.
    if (myView.IsNull()) return false;
    Standard_Real x, y, z, vx, vy, vz;
    myView->ConvertWithProj(px, py, x, y, z, vx, vy, vz);
    const gp_Lin ray(gp_Pnt(x, y, z), gp_Dir(vx, vy, vz));
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

void OcctViewWidget::updateHoverDimension()
{
    if (mySelectionMode != SelectionMode::Edge || myContext.IsNull() ||
        !myContext->HasDetectedShape() ||
        myContext->DetectedShape().ShapeType() != TopAbs_EDGE) {
        myDimension.clear();
        return;
    }

    const TopoDS_Edge edge = TopoDS::Edge(myContext->DetectedShape());
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
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target,
                          mySketchPlane);
    myView->Redraw();
    emit cameraChanged();
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
    } else if (event->button() == Qt::MiddleButton) {
        myPanningDrag = true;
    }
}

void OcctViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)  myOrbiting = false;
    if (event->button() == Qt::MiddleButton) myPanningDrag = false;

    if (event->button() != Qt::LeftButton || myContext.IsNull()) return;

    const QPoint pos = event->position().toPoint();

    if (mySketchMode) {
        gp_Pnt hit;
        if (pointOnSketchPlane(pos.x(), pos.y(), hit)) emit sketchPointPicked(hit);
        return;   // no selection while sketching
    }

    const bool additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    myContext->MoveTo(pos.x(), pos.y(), myView, Standard_False);
    myContext->SelectDetected(additive ? AIS_SelectionScheme_XOR
                                       : AIS_SelectionScheme_Replace);
    myView->Redraw();
    emit selectionChanged();
}

void OcctViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (myView.IsNull()) return;

    const QPoint pos = event->position().toPoint();

    if (myOrbiting) {
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
        myContext->MoveTo(pos.x(), pos.y(), myView, Standard_True);
        updateHoverDimension();
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
    myContext->MoveTo(pos.x(), pos.y(), myView, Standard_False);
    if (!myContext->HasDetected()) return;

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
