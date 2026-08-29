#include "OcctViewWidget.h"

#include "ModelingOps.h"
#include "SketchController.h"
#include "Theme.h"

// OCCT before Qt, for the Handle() macro clash.
#include <AIS_DisplayMode.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
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

Quantity_Color toOcctColor(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One tiny point in world space, drawn as a marker whose size lives in
// screen pixels - Graphic3d_AspectMarker3d/Prs3d_PointAspect's own documented
// contract ("size does not depend on the zoom value of the views"), so a
// marker never balloons up close or vanishes far away the way a fixed
// millimetre size would. Same shape as
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
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane());
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

void OcctViewWidget::setSketchPointMarkers(const std::vector<gp_Pnt>& points)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearSketchPointMarkers();
    if (points.empty()) return;

    // Aspect_TOM_BALL is a small filled, shaded circle - a real "dot" for
    // every placed point. Aspect_TOM_POINT was tried first and is described
    // as OCCT's smallest displayable dot, but it drew nothing at all in this
    // build; a snapshot caught that before it shipped (see this class's own
    // comment on Graphic3d_ArrayOfTriangles for the earlier instance of the
    // same lesson).
    for (const gp_Pnt& p : points) {
        Handle(SketchPointMarker) dot =
            makeMarker(p, Aspect_TOM_BALL, toOcctColor(Theme::sketchPointMarker()), 1.5);
        myContext->Display(dot, 0, -1, Standard_False);
        myPlacedMarkers.push_back(dot);
    }

    // The first point additionally gets a ring around its dot - "a ring, or
    // a larger dot" - because clicking it back is what closes the outline.
    Handle(SketchPointMarker) ring =
        makeMarker(points.front(), Aspect_TOM_RING1, toOcctColor(Theme::sketchPointMarker()), 2.2);
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
    if (mySelectionMode != SelectionMode::Edge || myContext.IsNull() || myView.IsNull()) {
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
