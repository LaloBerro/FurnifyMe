// windows.h FIRST, deliberately - the one exception to "OCCT headers before
// windows.h", and for the same reason gui_smoke.cpp takes it (see that
// file's own top-of-file comment): OCCT's own Standard_Macro.hxx includes
// windows.h itself, but with NOUSER defined first, which excludes the
// entire User32 window-management API. resizeEvent() below needs
// SetWindowPos and its SWP_* flags from that API (see its own comment for
// why), and windows.h's include guard means a second, unrestricted
// #include after OCCT's own restricted one is a silent no-op - the only
// way to get the real declarations is to be the FIRST includer. Handle()
// (OCCT's macro that collides with some Windows headers) does not exist
// yet at this point in the file, so there is nothing for windows.h to
// collide with here.
#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#endif

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
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_CLight.hxx>
#include <Graphic3d_CView.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Image_AlienPixMap.hxx>
#include <NCollection_HArray1.hxx>
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
#include <Standard_Failure.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Tool.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_TypeOfVisualization.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Vec.hxx>

#ifdef _WIN32
  #include <WNT_Window.hxx>
#else
  #include <Xw_Window.hxx>
#endif

#include <QDir>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

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

// The one place "which render-mode tiers are ray-traced" is written down -
// applyRenderTier() and showRenderFloor() both need the identical test
// (PathTracing/RayTracing get PBR shading + materials, Shadows/Plain get
// Phong - fix round 2's scoping ruling), and a second, hand-written copy of
// this condition is exactly how the two could quietly drift apart.
bool isRayTracedTier(OcctViewWidget::RenderTier tier)
{
    return tier == OcctViewWidget::RenderTier::PathTracing ||
           tier == OcctViewWidget::RenderTier::RayTracing;
}

// The symmetry plane indicator's own presentation - GridRenderer's aspect
// idiom (GridObject in GridRenderer.cpp), one pre-built segment array drawn
// with a single Graphic3d_AspectLine3d, never pickable. A much lighter shape
// than a grid: this is an INDICATOR, not a work surface, so it draws a
// rectangle outline and a cross through the origin rather than a field of
// lines.
class SymmetryPlaneObject : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) segments;
    Quantity_Color colour;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation, const Standard_Integer) override
    {
        if (segments.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect =
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID, 1.2);
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(segments);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable - an indicator, not a body.
    }
};

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

// The first point's marker: a FILLED square, which no Aspect_TypeOfMarker
// offers. Every stock type is a dot, a ring or a stroke glyph, so the square
// has to come from Graphic3d_AspectMarker3d's bitmap constructor - a
// monochrome stamp, sized in pixels and tinted by the colour argument, so it
// stays a Theme token exactly like the dots and the ring.
//
// The bitmap is glBitmap's classic layout: one bit per pixel, rows padded to
// whole bytes. Seven pixels wide fits inside one byte per row, and every bit
// is set, so whether the driver reads the row most- or least-significant-bit
// first the result is the same solid square - which matters here, because
// this file has twice found a primitive that was "obviously" fine drawing
// nothing at all (Aspect_TOM_POINT, Graphic3d_ArrayOfTriangles). The suite
// samples the middle of this square for the fill colour rather than trusting
// that it renders.
constexpr int kStartMarkerPx = 7;

Handle(SketchPointMarker) makeFilledSquareMarker(const gp_Pnt& point,
                                                 const Quantity_Color& colour)
{
    Handle(SketchPointMarker) marker = new SketchPointMarker();
    marker->point = point;

    Handle(NCollection_HArray1<uint8_t>) bits =
        new NCollection_HArray1<uint8_t>(0, kStartMarkerPx - 1);
    for (int row = 0; row < kStartMarkerPx; ++row) bits->SetValue(row, 0xFF);

    marker->aspect =
        new Graphic3d_AspectMarker3d(colour, kStartMarkerPx, kStartMarkerPx, bits);
    return marker;
}
}  // namespace

OcctViewWidget::OcctViewWidget(QWidget* parent, bool viewerOnly)
    : QWidget(parent)
    , mySketchPlane(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0))
    , myViewerOnly(viewerOnly)
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

    // The render-mode studio key's own default azimuth (Task 7.2), computed
    // rather than hardcoded to a rounded literal so it reproduces the old
    // hardcoded gp_Dir(-0.45, 0.35, -0.82) exactly - see
    // studioKeyDirectionForAzimuth()'s own comment.
    myRenderLightAngleDeg = std::atan2(0.35, -0.45) * 180.0 / 3.14159265358979323846;
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

    // No corner trihedron: AxisGizmo (top right) is the orientation surface,
    // and since the rail took the left edge the trihedron sat behind it with
    // one axis tip peeking out - redundant at best, a visual defect at worst.
    //
    // The background and the two highlight styles are applied through the same
    // applyTheme() a live Appearance edit uses, rather than set here and set
    // again there: two copies of "what this view wears" is exactly the drift
    // the Theme spec exists to end.
    applyTheme();

    // No grid in viewer-only mode - see the header. GridRenderer::update()
    // (called from applyCameraState() and setWorkPlane() unconditionally,
    // every camera move) is already a safe no-op with no context attached,
    // so skipping the attach here is the one change this needs.
    if (!myViewerOnly) myGridRenderer.attach(myContext);
    // The third layer of the three - see sketchZLayer() in the header. It has
    // to be created AFTER the grid's, because it is positioned relative to it:
    // bodies (default) -> grid -> sketch work. If the grid renderer could not
    // make its own layer, this one goes straight after the default layer, so
    // sketch work is still drawn after the grid rather than silently losing
    // the ordering along with it.
    {
        Graphic3d_ZLayerSettings settings;
        settings.SetName("FurnifyMe sketch work");
        settings.SetEnableDepthTest(Standard_True);
        settings.SetEnableDepthWrite(Standard_True);
        // Emphatically NOT SetClearDepth(true): that is what
        // Graphic3d_ZLayerId_Topmost does, and it would let the outline draw
        // straight through a body standing in front of it.
        settings.SetClearDepth(Standard_False);
        const Graphic3d_ZLayerId after = myGridRenderer.zLayer() != Graphic3d_ZLayerId_UNKNOWN
                                             ? myGridRenderer.zLayer()
                                             : Graphic3d_ZLayerId_Default;
        Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_UNKNOWN;
        if (myViewer->InsertLayerAfter(layer, settings, after)) mySketchLayer = layer;
    }
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane(),
                          Theme::gridDensity());
    // None of these three are ever driven for a viewer-only widget - nothing
    // calls showPullArrow()/showBevelArrow()/updateEdgeDimension() on one
    // (see the header) - so attaching them would only be inert presentation
    // channels sitting in the context for no caller to ever reach. Skipped
    // outright rather than left as harmless dead weight, so "viewer-only has
    // no picking, no hover, no grid" reads as the true, complete list rather
    // than one this constructor quietly disagrees with.
    if (!myViewerOnly) {
        myDimension.attach(myContext);
        myDimension.setZLayer(mySketchLayer);
        myPullArrow.attach(myContext);
        myBevelArrow.attach(myContext);
    }

    // The field of view is fixed at kFovyDeg for ordinary modeling; render
    // mode (Task 7.2) can override it session-to-session through
    // setRenderFov()/effectiveFovyDeg(). WHICH projection is drawn with it,
    // and now the FOV itself, both move, so applyCameraState() owns both and
    // this initial call only seeds the same value it would read anyway. See
    // applyCameraState() for how the orthographic scale is kept tied to the
    // turntable's distance, which is what lets one distance-based camera
    // model serve both projections.
    myView->Camera()->SetFOVy(effectiveFovyDeg());
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
#ifdef _WIN32
    // A safety net for a real, measured defect (fix round 1, Important 1 /
    // Minor 3 on Milestone 3's Task 3): reparenting this widget's native
    // window OUT of a QSplitter and back - MainWindow's compare pane,
    // closeCompare() - left Qt's own WIDGET-LEVEL geometry correctly
    // updated (width()/height() agreed with the window) while the ACTUAL
    // underlying HWND's client rect stayed at its old, splitter-constrained
    // size. Confirmed with GetClientRect, and unmoved by resize(),
    // repaint(), hide()/show(), or even a full top-level window resize
    // round trip - none of which reach whatever is actually caching the
    // native surface's extent here. SetWindowPos, synced to Qt's own idea
    // of this widget's size on EVERY resize, closes the gap regardless of
    // what caused it - a defensive, always-on correction that costs
    // nothing when the two already agree (SetWindowPos with an unchanged
    // size is a cheap no-op) rather than a special case wired only into
    // the one call site that happened to find it.
    //
    // DEVICE pixels, not logical - toDevicePixels() is this file's one
    // conversion point (see its own comment), and a raw HWND client rect is
    // unambiguously a device-pixel quantity. Passing width()/height()
    // straight through would undersize the native surface at any scale
    // other than 100%, the exact class of bug that helper exists to close.
    const QPoint deviceSize = toDevicePixels(QPoint(width(), height()));
    SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, deviceSize.x(), deviceSize.y(),
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
#endif
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
    // No picking in viewer-only mode - see the header. Leaving the
    // presentation's selection mode deactivated is the literal version of
    // "never pickable", on the same terms an outline already is.
    if (!myViewerOnly) applySelectionMode(presentation);

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
    // Same reason again: a mirror-placement gesture describes exactly the
    // ids captured at beginMirrorPlacement(), and one of them is about to
    // stop existing - undo/redo and Delete are not gated off this gesture
    // (see canBeginMirrorPlacement()'s own header comment on why that gate
    // was left to selection mode alone), so a document change reaching here
    // mid-gesture is a real, if rare, path.
    if (myMirrorPlacement.active &&
        std::find(myMirrorPlacement.ids.begin(), myMirrorPlacement.ids.end(), id) !=
            myMirrorPlacement.ids.end()) {
        cancelMirrorPlacement();
    }

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
    cancelMirrorPlacement();   // same reasoning: every id it describes is about to go

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
        if (!myViewerOnly) applySelectionMode(it->second);
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

void OcctViewWidget::displayOutline(int id, const TopoDS_Face& face)
{
    initializeViewer();
    if (myContext.IsNull() || face.IsNull()) return;

    removeOutline(id);

    ModelingOps::tessellate(face, 0.1);

    Handle(AIS_Shape) presentation = new AIS_Shape(face);
    // The same yellow every piece of sketch work wears - setPreview() and
    // setModelingPreview() both use it. An outline is a document item, but it
    // is a FLAT one that is not a body yet, and giving it the bodies' grey
    // would say it was one.
    presentation->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
    presentation->SetWidth(2.0);
    // Above the work-plane grid it lies exactly on top of - see sketchZLayer().
    markInSketchLayer(presentation);
    // Selection mode -1: never pickable. Outlines are handled from the drawer
    // this phase, and a shape the user can select but cannot Union, Pull or
    // bevel would be a selection that makes every gizmo predicate lie.
    myContext->Display(presentation, AIS_Shaded, -1, Standard_False);
    myOutlines[id] = presentation;

    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::removeOutline(int id)
{
    const auto it = myOutlines.find(id);
    if (it == myOutlines.end() || myContext.IsNull()) return;

    myContext->Remove(it->second, Standard_False);
    myOutlines.erase(it);
    myContext->UpdateCurrentViewer();
}

void OcctViewWidget::clearOutlines()
{
    if (myContext.IsNull()) return;

    for (auto& entry : myOutlines) myContext->Remove(entry.second, Standard_False);
    myOutlines.clear();
    myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::hasOutline(int id) const
{
    return myOutlines.find(id) != myOutlines.end();
}

void OcctViewWidget::setOutlineVisible(int id, bool visible)
{
    const auto it = myOutlines.find(id);
    if (it == myOutlines.end() || myContext.IsNull()) return;

    if (visible) {
        // The same mode and the same -1 it was displayed with, not
        // Display(obj, false), which would fall back to the object's default
        // wireframe mode and silently change how a hidden-then-shown outline
        // looks.
        myContext->Display(it->second, AIS_Shaded, -1, Standard_False);
    } else {
        myContext->Erase(it->second, Standard_False);
    }
    // No selectionChanged() either way, unlike setSolidVisible(): an outline is
    // not selectable, so hiding one cannot have dropped anything from the
    // selection.
    myContext->UpdateCurrentViewer();
}

bool OcctViewWidget::isOutlineVisible(int id) const
{
    const auto it = myOutlines.find(id);
    if (it == myOutlines.end() || myContext.IsNull()) return false;
    return myContext->IsDisplayed(it->second);
}

std::vector<Graphic3d_ZLayerId> OcctViewWidget::zLayerOrder() const
{
    std::vector<Graphic3d_ZLayerId> order;
    if (myViewer.IsNull()) return order;

    NCollection_Sequence<int> layers;
    myViewer->GetAllZLayers(layers);
    for (int i = layers.Lower(); i <= layers.Upper(); ++i) order.push_back(layers.Value(i));
    return order;
}

Graphic3d_ZLayerSettings OcctViewWidget::zLayerSettings(Graphic3d_ZLayerId layer) const
{
    if (myViewer.IsNull() || layer == Graphic3d_ZLayerId_UNKNOWN)
        return Graphic3d_ZLayerSettings();
    return myViewer->ZLayerSettings(layer);
}

void OcctViewWidget::markInSketchLayer(const Handle(AIS_InteractiveObject)& object) const
{
    if (object.IsNull() || mySketchLayer == Graphic3d_ZLayerId_UNKNOWN) return;
    object->SetZLayer(mySketchLayer);
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
    // The in-progress outline and the closed face are drawn above the
    // work-plane grid they sit exactly on top of - see sketchZLayer().
    markInSketchLayer(myPreview);
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
    // In the sketch-work layer with the rest of the feedback: a pull or a
    // bevel preview carving a body sitting on the ground grid is exactly the
    // shape the grid must not paint over. Depth testing is on in that layer,
    // so it still hides behind whatever is genuinely in front of it.
    markInSketchLayer(myModelingPreview);
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
    // PLAIN WORLD SPACE. OCCT 8.0 constructs AIS_Manipulator with zoom
    // persistence ON - the header documents OptionsForAttach::AdjustSize as
    // defaulting to false and says nothing about this one, and it cost the best
    // part of a fix round to find. In that mode the manipulator's presentation
    // is anchored to the screen: its drawn size tracks SetSize() but ignores
    // the camera entirely, so every camera-derived correction below wrote a
    // number that could not reach a pixel. The measurements said so plainly
    // once they were taken from a dump rather than from the code's own
    // opinion - the painted size per unit came out 1.08 px at one zoom and
    // 1.10 px at another, across a 4.2x change in world-per-pixel.
    //
    // Turned off rather than accommodated, for three reasons: worldPerPixel()
    // is how everything else in this file relates world units to pixels and it
    // is verified in both projections; manipulatorFrame()'s size becomes an
    // honest world measurement, which is what every probe that projects a
    // point from it already assumes; and accommodating it would mean carrying
    // a screen-space constant nobody can derive from the API.
    //
    // It must be set BEFORE Attach() - the header's own warning is that
    // enabling this mode overrides transform-persistence flags and the local
    // transformation, which is exactly the machinery the drag path uses.
    myManipulator->SetZoomPersistence(Standard_False);

    // The body's own measurements, which the clamp needs and which nothing
    // else can answer for it. The SIZING itself happens at the END of this
    // function - see the comment there for why it cannot happen here.
    Bnd_Box box;
    BRepBndLib::Add(it->second->Shape(), box);
    if (!box.IsVoid()) {
        Standard_Real x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        // The longest side - SetSize() documents its argument as the side of
        // the manipulator's cubic bounding box, so a side is the like measure.
        myManipulatorNaturalSize = std::max({x1 - x0, y1 - y0, z1 - z0});
        // And where it stands, which the clamp needs in order to know how deep
        // the gizmo is. From the BODY's box rather than from
        // AIS_Manipulator::Position(), which cannot answer until AdjustPosition
        // has run inside Attach() - asked before that it says the world origin,
        // and a depth measured to the origin made the budget 2.5x too generous.
        myManipulatorCentre = gp_Pnt(0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1));
    }
    // AdjustSize stays ON as the fallback the clamp narrows a moment later: if
    // the body's box ever comes back void, a gizmo framed from the object is a
    // better answer than OCCT's bare default.
    AIS_Manipulator::OptionsForAttach options;
    options.SetAdjustPosition(Standard_True);
    options.SetAdjustSize(Standard_True);
    options.SetEnableModes(Standard_True);
    myManipulator->Attach(it->second, options);
    activateManipulatorModes();
    // The one COLOUR styling hook AIS_Manipulator actually exposes: the
    // shading aspect its parts are computed from. The per-axis HUES are
    // private (AIS_Manipulator::Axis::myColor, set in init() and reachable
    // through no public setter at ANY access level - see the corrected
    // paragraph below), and red/green/blue for X/Y/Z is the universal gizmo
    // language anyway - tinting all three to one accent would cost more than
    // it bought. What this does reach is the material, so the gizmo reads as
    // part of this app's matte surface family rather than a glossy default.
    //
    // CONFIRMED, not assumed - Task 5 (Theme::gizmoAxisX/Y/Z, the 2D
    // AxisGizmo's own restyle) went looking for a way to carry those same
    // three tokens onto THIS manipulator too, and read AIS_Manipulator.hxx
    // end to end rather than trust the paragraph above at face value.
    //
    // CORRECTED in fix round 1 (review), TWICE - once by the review, once by
    // actually building and measuring what it found. It is real and total
    // for COLOUR - AIS_Manipulator::Axis::Color() is a const getter with no
    // matching setter anywhere, and Axis::myColor is protected to Axis's OWN
    // class hierarchy, unreachable even from a subclass of AIS_Manipulator.
    // It is NOT total for API SURFACE alone: `protected Axis myAxes[3]` on
    // AIS_Manipulator IS reachable from a subclass (protected members are),
    // and Axis::SetAxisRadius()/AxisRadius() ARE public on Axis itself - the
    // review correctly caught that the first pass had stopped at "the Axis
    // objects are unreachable" without separating "unreachable" (false, for
    // a subclass) from "myColor is unreachable regardless" (true).
    //
    // But REACHABLE is not the same as USABLE, and this file went looking
    // for that difference rather than assuming the header settled it: a
    // SlimAxisManipulator subclass was built exactly as described, and its
    // effect was measured against real Dump pixels - not a Size()/
    // AxisRadius() read-back, three independent methodologies, several
    // scale factors, at points OCCT's own hover detection confirmed were
    // genuinely on the X translation arm. Every measurement moved the WRONG
    // way: the arm's rendered cross-section GREW as the radius shrank (34 px
    // stock to 40 px at a 0.3 scale to 80 px at 0.02 - reproducible and
    // monotonic, not noise), almost certainly because the shrinking shaft
    // revealed an adjacent manipulator part - the rotation ring or the hub
    // cluster - that shares the exact same uniform matte material this file
    // already applies below, so a thinner shaft did not read as "less grey"
    // anywhere the probe could isolate it. Reverted rather than shipped:
    // CLAUDE.md's zoom-persistence lesson is to trust a measured pixel over
    // a setter's own claim, and here the measurement said the setter's name
    // did not describe what actually reached the screen. SetPart(axisIndex,
    // mode, enabled) is still visibility-only, not colour, despite the name;
    // Attributes()->ShadingAspect(), the hook used below, is still the ONE
    // material for the whole object, which is why it recolours all three
    // arms uniformly and could never single out "the uniform-scale handle";
    // and SetGap() is still public with no matching getter at all, so a
    // spacing tweak through it was never attempted. The manipulator wears
    // OCCT's stock proportions AND stock per-axis hues; both boundaries are
    // real, and only one of the two was ever a matter of API surface.
    const Handle(Prs3d_ShadingAspect) gizmoAspect =
        myManipulator->Attributes()->ShadingAspect();
    if (!gizmoAspect.IsNull()) {
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);
        material.SetAmbientColor(Quantity_Color(0.35, 0.35, 0.35, Quantity_TOC_sRGB));
        material.SetDiffuseColor(Quantity_Color(0.75, 0.75, 0.75, Quantity_TOC_sRGB));
        material.SetSpecularColor(Quantity_Color(0.05, 0.05, 0.05, Quantity_TOC_sRGB));
        gizmoAspect->SetMaterial(material);
    }

    // TOPMOST, and it is the clamp below that makes this necessary. A
    // manipulator stands at its body's own centre, and once its arms are
    // capped to a share of the SCREEN they are routinely shorter than the body
    // is wide - which in the default layer means a gizmo drawn inside the solid
    // it belongs to, depth-tested away and invisible at exactly the zoom the
    // clamp exists for. A manipulator is a control rather than geometry, and
    // every CAD application draws one over the model for this reason.
    //
    // CLAUDE.md rejects this layer for the ground GRID, and that ruling stands:
    // the layer clears depth, so a grid in it would paint over every body
    // standing on it. Here painting over the body IS the requirement. Depth
    // still applies within the layer, so the gizmo's own three arms occlude
    // each other correctly.
    //
    // It DOES change picking, and saying otherwise would be convenient rather
    // than true: SelectMgr_SortCriterion::IsCloserDepth ranks ZLayerPosition
    // ahead of depth, so a manipulator part now wins picks against a body in
    // front of it that it would previously have lost. That is the right
    // outcome for a control the user can see and reach - a handle that is
    // drawn on top and picks underneath is worse than either - and it costs
    // nothing already relied on: the one place a body MUST outrank the gizmo
    // is the additive Shift pick, which Deactivates the manipulator around the
    // MoveTo/SelectDetected pair rather than trusting the ordering.
    myContext->SetZLayer(myManipulator, Graphic3d_ZLayerId_Topmost);

    myManipulatorSolid = solidId;
    // ON SCREEN FIRST, THEN SIZED, and the order is the whole fix rather than
    // fussiness. Measured three ways: with the clamp applied before or during
    // the attach, AIS_Manipulator reported the clamped size while the viewport
    // drew the bounding-diagonal one - 731 mm painted against a 256 mm budget -
    // because the presentation is computed once, inside Attach(), and a
    // Redisplay() issued before the object has been through a redraw does not
    // dislodge it. The identical call one frame later does. So the update runs
    // after this first UpdateCurrentViewer(), which is what puts the
    // manipulator on screen, and the second one carries the resized
    // presentation out.
    //
    // The cost is one extra viewer update per attach - once per selection, not
    // per frame - and the alternative is a gizmo that is the right size only
    // after the user touches the camera, which is never the frame they are
    // shown first.
    myContext->UpdateCurrentViewer();
    myManipulatorAppliedSize = 0.0;   // nothing of ours installed yet: force it
    applyCameraState();
}

void OcctViewWidget::updateManipulatorSize()
{
    if (myManipulator.IsNull() || myView.IsNull()) return;
    // Never mid-gesture: AIS_Manipulator's drag maths is anchored on the arm
    // the press landed on, and resizing that arm under the cursor moves the
    // handle away from the hand holding it.
    if (myGizmoDragActive) return;
    if (myManipulatorNaturalSize <= 0.0) return;

    // worldPerPixel() is ONE formula for both projections here (see its
    // definition - the orthographic scale is set to exactly the perspective
    // frustum's height at the target), so this needs no branch on the
    // projection and is correct the moment the Persp/Ortho toggle flips.
    //
    // The smaller viewport dimension, because a gizmo that fits a wide
    // viewport's width can still run off the top and bottom of a short one.
    // Both are logical pixels, which is what worldPerPixel() divides by.
    const double smallerSide = std::min(std::max(1, width()), std::max(1, height()));

    // Two things separate a screen budget from a world size, and both of them
    // are perspective. Neither exists in a parallel projection, where a world
    // length projects to the same pixels at every depth - so both fall out to
    // 1 there by construction rather than by a branch that could go stale.
    //
    // FIRST, DEPTH. worldPerPixel() answers for the camera TARGET's plane,
    // which is the right question for the ground grid and for a dimension the
    // user is looking straight at. A gizmo stands wherever its body stands, and
    // a body nearer than the target projects larger than that number says.
    //
    // SECOND, THE ARM ITSELF. An arm pointing towards the eye ends nearer than
    // it starts, so its tip projects further out than a flat depth-scaled
    // estimate - measured at 9% over budget at 175% zoomed in, which is not a
    // rounding error and is not fixed by the depth term alone. Requiring the
    // NEAREST point of the gizmo to fit rather than its centre means solving
    //     side / (k * (depth - side)) <= limitPixels,  k = worldPerPixel/depth
    // for side, which is the closed form below - no iteration, and it collapses
    // to the flat budget whenever the gizmo is small against its own depth.
    double depthRatio = 1.0;
    double gizmoDepth = 0.0;
    const bool perspective = !myCamera.effectiveOrtho();
    if (perspective) {
        const gp_Pnt eye = myCamera.eyePosition();
        const gp_Dir viewDir = myCamera.viewDirection();
        // Along the view axis, never the straight-line distance: it is the
        // depth that scales a perspective projection, and an off-centre gizmo
        // is further away without being any deeper.
        gizmoDepth = gp_Vec(eye, myManipulatorCentre).Dot(gp_Vec(viewDir));
        const double targetDepth = myCamera.state().distance;
        if (gizmoDepth > 1.0e-6 && targetDepth > 1.0e-6) depthRatio = gizmoDepth / targetDepth;
    }

    const double flatBudget =
        kGizmoMaxViewportFraction * smallerSide * worldPerPixel() * depthRatio;
    const double maxWorld = (perspective && gizmoDepth > 1.0e-6)
                                ? flatBudget / (1.0 + flatBudget / gizmoDepth)
                                : flatBudget;
    if (maxWorld <= 0.0) return;

    const double wanted = std::min(myManipulatorNaturalSize, maxWorld);
    // The equal-guard, in the shape the view label's is: this runs on every
    // frame of an orbit and a pan, and SetSize() recomputes all seven of the
    // manipulator's presentations. Relative, not absolute, because the same
    // gizmo is legitimately 3 mm on a drawer front and 3 m on a wardrobe.
    //
    // It keys on `wanted`, which is a pure function of the camera and the
    // body - NOT on what the manipulator reports afterwards - so a call that
    // does not return early always performs exactly the same work below, and
    // the correction cannot ratchet across frames.
    if (std::fabs(wanted - myManipulatorAppliedSize) <= 1.0e-3 * std::fabs(wanted)) return;

    myManipulatorAppliedSize = wanted;

    // Installed, then CORRECTED, because Size() is not SetSize()'s own unit
    // (see the attach): what it reports is the assembly's outer reach, and that
    // is the number which actually has to fit inside the fraction. Scaling the
    // side length by the overshoot very nearly lands it but not exactly - the
    // relation is affine with an offset, since the gap between the parts does
    // not always scale with the whole - so it is applied until the reach is
    // inside budget rather than assumed to converge in one. Two passes is the
    // observed worst case; the bound is a bound, not a schedule, and the
    // equal-guard above means none of this runs again until the camera or the
    // body actually moves.
    double side = wanted;
    myManipulator->SetSize(static_cast<float>(side));
    for (int pass = 0; pass < 4; ++pass) {
        const double reported = myManipulator->Size();
        if (reported <= maxWorld || reported <= 1.0e-9) break;
        side *= maxWorld / reported;
        myManipulator->SetSize(static_cast<float>(side));
    }

    // AND THEN REDRAWN, which is the whole difference between a number and a
    // gizmo. SetSize() writes the axes' parameters and marks the object
    // ToBeUpdated; it does NOT recompute the presentation, so without this the
    // manipulator kept drawing at whatever AdjustSize() gave it on attach while
    // Size() cheerfully reported the clamped value. Everything above was
    // arithmetically correct and reached no pixel: the first capture of this
    // work shows a gizmo 538 px across a 110 px budget, taken from a build
    // whose own probe read 103 px, because that probe asked the code for the
    // number the code had just written.
    // Only once it is on screen. Before the attach there is no presentation to
    // rebuild - the size set above is simply the one the first Compute will
    // use - and asking the context to redisplay an object it does not yet hold
    // is at best a no-op.
    if (!myContext.IsNull() && myContext->IsDisplayed(myManipulator))
        myContext->Redisplay(myManipulator, Standard_False);

    // OCCT's own SetZoomPersistence(true) would hold a FIXED screen size
    // instead, and was rejected rather than missed: it overrides the local
    // transformation and the transform-persistence flags of every
    // sub-presentation, which is precisely the machinery the drag path here
    // already leans on (see endGizmoDrag and the presentation reset it
    // performs). A cap that is re-derived from the camera keeps the drag maths
    // untouched, and it also lets a small body keep a small gizmo instead of
    // giving every body the same one.
}

void OcctViewWidget::setSymmetryIndicator(bool on, const gp_Pln& plane)
{
    mySymmetryIndicatorOn = on;
    mySymmetryIndicatorPlane = plane;

    // NEVER forces initializeViewer() - GridRenderer::update()'s own rule,
    // one call site over (see its header): a no-op until a context already
    // exists. This is reached from resyncView() on every undo/redo/open/
    // restore, symmetry off or on, and an unconditional initializeViewer()
    // here forced winId()/native-window realization far earlier than this
    // widget's lazy-init contract intends - measured as a real regression
    // (fix round 1): it moved that realization inside the constructor's own
    // showInitScreen() path, ahead of the window's first show(), and that
    // reordering broke camera-state and focus determinism in gui_smoke
    // ("startup distance is 700mm", "keyboard focus is visible on a chip" -
    // both failed 4/4 on a clean parent-commit A/B, neither is the P7 wheel
    // flake). By the time symmetry is genuinely turned on by a user or a
    // test, the viewport has always already painted once, so myContext is
    // never null there in practice - see setSymmetryEnabled()'s own comment.
    if (myContext.IsNull()) return;

    if (!on) {
        if (!mySymmetryIndicator.IsNull()) {
            myContext->Remove(mySymmetryIndicator, Standard_False);
            myContext->UpdateCurrentViewer();
        }
        mySymmetryIndicator.Nullify();
        mySymmetryIndicatorBuiltHalfSpan = 0.0;
        return;
    }

    // Force a rebuild: the plane may have changed even if the half-span
    // (which is all the equal-guard inside updateSymmetryIndicator() checks)
    // has not.
    mySymmetryIndicatorBuiltHalfSpan = 0.0;
    updateSymmetryIndicator();
}

void OcctViewWidget::updateSymmetryIndicator()
{
    // Render mode (Milestone 3, item 5): "the viewport is the furniture
    // alone" is not just the grid and the gizmos - the symmetry plane is
    // scene decoration too. This single guard is what keeps it hidden
    // across every camera move while render mode is active, since
    // applyCameraState() calls this function on every one of them; without
    // it, orbiting during render mode would silently rebuild and redisplay
    // the plane the moment its screen-sized half-span crossed the equal-
    // guard below. setRenderMode() handles the two edges - erasing it
    // immediately on entry if it was already up, and forcing this function
    // to rebuild and redisplay it on exit if symmetry is still on.
    if (!mySymmetryIndicatorOn || myContext.IsNull() || myRenderModeActive) return;

    // Screen-sized - DimensionRenderer's own idiom, one call site up: a
    // constant APPARENT extent rather than a fixed number of millimetres
    // that shrinks to nothing as the camera pulls back. ~220 px half-span
    // reads as a generous plane without swallowing a small body.
    const double halfSpan = worldPerPixel() * 220.0;
    // The equal-guard updateManipulatorSize() uses, one call site over: this
    // runs on every frame of an orbit, and a rebuild is a real allocation.
    if (mySymmetryIndicatorBuiltHalfSpan > 0.0 &&
        halfSpan < mySymmetryIndicatorBuiltHalfSpan * 1.1 &&
        halfSpan > mySymmetryIndicatorBuiltHalfSpan * 0.9) {
        return;
    }

    const gp_Ax3 frame = mySymmetryIndicatorPlane.Position();
    const gp_Pnt origin = mySymmetryIndicatorPlane.Location();
    const gp_Dir u = frame.XDirection();
    const gp_Dir v = frame.YDirection();
    const auto at = [&](double du, double dv) {
        return origin.Translated(gp_Vec(u) * du + gp_Vec(v) * dv);
    };

    // A rectangle outline plus a cross through the origin - enough to read
    // as a PLANE rather than a single line, without the density of a work
    // grid; this is an indicator, not a surface to click on.
    Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(10);
    array->AddVertex(at(-halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, halfSpan));
    array->AddVertex(at(halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, -halfSpan));
    array->AddVertex(at(0.0, -halfSpan));
    array->AddVertex(at(0.0, halfSpan));

    Handle(SymmetryPlaneObject) indicator = new SymmetryPlaneObject();
    indicator->segments = array;
    // A fixed, faint, untokenised colour - the same scope ruling CLAUDE.md
    // already makes for the gizmo's axis hues and the OCCT body/preview
    // materials: this is scene decoration on the OCCT side of the bridge,
    // not a Theme surface.
    indicator->colour = Quantity_Color(0.55, 0.55, 0.65, Quantity_TOC_sRGB);
    markInSketchLayer(indicator);

    if (!mySymmetryIndicator.IsNull()) myContext->Remove(mySymmetryIndicator, Standard_False);
    mySymmetryIndicator = indicator;
    // Selection mode -1: an indicator, never pickable.
    myContext->Display(mySymmetryIndicator, 0, -1, Standard_False);
    myContext->UpdateCurrentViewer();
    mySymmetryIndicatorBuiltHalfSpan = halfSpan;
}

// --- Mirror plane placement (Milestone 4, Phase 3) --------------------------

gp_Dir OcctViewWidget::mirrorPlacementNormalFor(int axis)
{
    switch (axis) {
        case 1: return gp_Dir(0.0, 1.0, 0.0);
        case 2: return gp_Dir(0.0, 0.0, 1.0);
        default: return gp_Dir(1.0, 0.0, 0.0);   // 0, and anything else out of range
    }
}

void OcctViewWidget::beginMirrorPlacement(const std::vector<int>& ids)
{
    initializeViewer();
    if (myContext.IsNull() || ids.empty()) return;

    // The combined centre of every selected body's own bounding box -
    // fitAll()'s own accumulation, one call site over.
    Bnd_Box box;
    for (int id : ids) {
        const auto it = mySolids.find(id);
        if (it == mySolids.end()) continue;
        Bnd_Box b;
        BRepBndLib::Add(it->second->Shape(), b);
        box.Add(b);
    }
    gp_Pnt centre(0.0, 0.0, 0.0);
    if (!box.IsVoid()) {
        Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        centre = gp_Pnt((xmin + xmax) * 0.5, (ymin + ymax) * 0.5, (zmin + zmax) * 0.5);
    }

    myMirrorPlacement.active = true;
    myMirrorPlacement.ids = ids;
    myMirrorPlacement.centre = centre;
    // World X, normal-along-YZ-through-the-centre - DocumentModel's own
    // constructed default (see its header), so a gesture that never drags or
    // reorients reproduces exactly the plane the old direct toggle started
    // at.
    myMirrorPlacement.axis = 0;
    myMirrorPlacement.offset = 0.0;

    updateMirrorPlacementIndicator();
}

void OcctViewWidget::cancelMirrorPlacement()
{
    if (!myMirrorPlacement.active) return;

    if (!myContext.IsNull()) {
        if (!myMirrorPlacementPlaneObject.IsNull()) {
            myContext->Remove(myMirrorPlacementPlaneObject, Standard_False);
            myMirrorPlacementPlaneObject.Nullify();
        }
        if (!myMirrorPlacementHandleObject.IsNull()) {
            myContext->Remove(myMirrorPlacementHandleObject, Standard_False);
            myMirrorPlacementHandleObject.Nullify();
        }
        myContext->UpdateCurrentViewer();
    }
    // The twin ghost preview lives on the dedicated modeling-preview channel,
    // never replacing a body (replacesSolidId is -1 throughout this gesture -
    // see updateMirrorPlacementIndicator()), so clearing it here cannot leave
    // any body stuck in wireframe.
    clearModelingPreview();

    myMirrorPlacement = MirrorPlacement();
    myMirrorDrag = AxisDrag();
}

void OcctViewWidget::endMirrorPlacement()
{
    // The viewport-side teardown is identical either way - see the header
    // comment on why this stays a distinct name from cancelMirrorPlacement()
    // regardless.
    cancelMirrorPlacement();
}

gp_Pln OcctViewWidget::mirrorPlacementPlane() const
{
    if (!myMirrorPlacement.active) return gp_Pln();
    const gp_Dir normal = mirrorPlacementNormalFor(myMirrorPlacement.axis);
    const gp_Pnt location =
        myMirrorPlacement.centre.Translated(gp_Vec(normal) * myMirrorPlacement.offset);
    return gp_Pln(location, normal);
}

bool OcctViewWidget::mirrorPlacementHandle(gp_Pnt& out) const
{
    if (!myMirrorPlacement.active) return false;
    out = mirrorPlacementPlane().Location();
    return true;
}

void OcctViewWidget::setMirrorPlacementAxis(int axis)
{
    if (!myMirrorPlacement.active) return;
    axis = std::clamp(axis, 0, 2);
    if (axis == myMirrorPlacement.axis) return;
    myMirrorPlacement.axis = axis;
    // An offset measured along the OLD normal has no honest meaning against a
    // different one - see the header comment.
    myMirrorPlacement.offset = 0.0;
    updateMirrorPlacementIndicator();
}

gp_Lin OcctViewWidget::mirrorPlacementAxisLine() const
{
    return gp_Lin(myMirrorPlacement.centre, mirrorPlacementNormalFor(myMirrorPlacement.axis));
}

bool OcctViewWidget::mirrorHandleHit(const QPoint& point) const
{
    if (!myMirrorPlacement.active) return false;
    gp_Pnt handle;
    if (!mirrorPlacementHandle(handle)) return false;
    QPoint screen;
    if (!projectToScreen(handle, screen)) return false;
    const QPoint d = point - screen;
    // The same generous 14 px radius arrowHit() grabs a whole shaft with -
    // this is a single point, so a target the user has to hit exactly would
    // be one they miss even more often.
    return std::sqrt(static_cast<double>(d.x() * d.x() + d.y() * d.y())) <= 14.0;
}

void OcctViewWidget::updateMirrorPlacementIndicator()
{
    if (!myMirrorPlacement.active || myContext.IsNull()) return;

    const gp_Pln plane = mirrorPlacementPlane();
    // Screen-sized, updateSymmetryIndicator()'s own idiom - a constant
    // APPARENT extent rather than a fixed number of millimetres that shrinks
    // to nothing as the camera pulls back.
    const double halfSpan = worldPerPixel() * 220.0;
    const gp_Pnt origin0 = plane.Location();
    const gp_Dir normal0 = plane.Axis().Direction();
    // The equal-guard: a camera orbit with no drag or orientation change in
    // progress touches neither the half-span (no zoom) nor the location/
    // normal (nothing moved it), so applyCameraState()'s own per-tick call
    // costs nothing beyond this check - updateSymmetryIndicator()'s own
    // reasoning, widened past screen size to the two things a drag or a
    // flip changes that a plain orbit never does.
    if (!myMirrorPlacementPlaneObject.IsNull() && myMirrorPlacementBuiltHalfSpan > 0.0 &&
        halfSpan < myMirrorPlacementBuiltHalfSpan * 1.1 &&
        halfSpan > myMirrorPlacementBuiltHalfSpan * 0.9 &&
        origin0.IsEqual(myMirrorPlacementBuiltOrigin, 1.0e-6) &&
        normal0.IsEqual(myMirrorPlacementBuiltNormal, 1.0e-9)) {
        return;
    }

    const gp_Ax3 frame = plane.Position();
    const gp_Pnt origin = plane.Location();
    const gp_Dir u = frame.XDirection();
    const gp_Dir v = frame.YDirection();
    const auto at = [&](double du, double dv) {
        return origin.Translated(gp_Vec(u) * du + gp_Vec(v) * dv);
    };

    Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(10);
    array->AddVertex(at(-halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, -halfSpan));
    array->AddVertex(at(halfSpan, halfSpan));
    array->AddVertex(at(halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, halfSpan));
    array->AddVertex(at(-halfSpan, -halfSpan));
    array->AddVertex(at(0.0, -halfSpan));
    array->AddVertex(at(0.0, halfSpan));

    Handle(SymmetryPlaneObject) plane3d = new SymmetryPlaneObject();
    plane3d->segments = array;
    // Accent-coloured, per the picked mockup - unlike the passive symmetry
    // indicator this is a live control the user is actively placing, and
    // Theme::accent() is what every other live gizmo (the arrows, the
    // markers) already wears.
    plane3d->colour = toOcctColor(Theme::accent());
    markInSketchLayer(plane3d);
    if (!myMirrorPlacementPlaneObject.IsNull())
        myContext->Remove(myMirrorPlacementPlaneObject, Standard_False);
    myMirrorPlacementPlaneObject = plane3d;
    myContext->Display(myMirrorPlacementPlaneObject, 0, -1, Standard_False);

    Handle(SketchPointMarker) handle =
        makeMarker(origin, Aspect_TOM_BALL, toOcctColor(Theme::accent()), 2.2);
    markInSketchLayer(handle);
    if (!myMirrorPlacementHandleObject.IsNull())
        myContext->Remove(myMirrorPlacementHandleObject, Standard_False);
    myMirrorPlacementHandleObject = handle;
    myContext->Display(myMirrorPlacementHandleObject, 0, -1, Standard_False);

    myContext->UpdateCurrentViewer();
    myMirrorPlacementBuiltHalfSpan = halfSpan;
    myMirrorPlacementBuiltOrigin = origin0;
    myMirrorPlacementBuiltNormal = normal0;
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
    myManipulatorNaturalSize = 0.0;
    myManipulatorAppliedSize = 0.0;
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
    // HasDetected() FIRST, and it is not defensive padding - it is the whole
    // reason this function does not crash the app.
    //
    // AIS_InteractiveContext::DetectedInteractive() is an inline that reads
    // `myLastPicked->Selectable()` with no null check of its own, and a MoveTo
    // that detects nothing sets myLastPicked to null. HasDetected() is
    // literally `!myLastPicked.IsNull()`, so this line is the guard OCCT's own
    // accessor does not carry.
    //
    // The reachable trigger was one click: select a body (which attaches the
    // manipulator), then click empty viewport to deselect. The press handler
    // MoveTo's, detects nothing, and asks this - null deref, process gone. It
    // is a hover away too, through mouseMoveEvent's hover-highlight branch.
    // Every other DetectedInteractive() call in this file already sits behind
    // an explicit HasDetected() (see mouseDoubleClickEvent, which MoveTo's and
    // returns early on !HasDetected() before it asks anything); this one
    // function was the exception, and 850 green checks never went near it
    // because nothing in the suite clicked empty space with a gizmo up.
    if (!myContext->HasDetected()) return false;
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
        markInSketchLayer(dot);
        myContext->Display(dot, 0, -1, Standard_False);
        myPlacedMarkers.push_back(dot);
    }

    // The first point additionally gets a small FILLED SQUARE on top of its
    // dot, because clicking it back is what closes the outline and that has
    // to be visibly true, not just structurally true.
    //
    // The square replaced an amber ring, and the change is a shape change as
    // much as a colour one - which is the point. The three sketch marks are
    // now told apart by SHAPE first: a square starts the outline, a dot is a
    // placed point, a ring is where the cursor is. That matters because this
    // file has already been burned once by leaning on size alone (scale 2.2
    // against 1.5 pixel-sampled IDENTICAL on this driver), and once more by
    // assuming a primitive draws at all. Theme::accent() is the app's own
    // "this is the interactive thing" colour and no other sketch mark wears
    // it, so colour still carries the distinction independently.
    Handle(SketchPointMarker) square =
        makeFilledSquareMarker(points.front(), toOcctColor(Theme::accent()));
    markInSketchLayer(square);
    myContext->Display(square, 0, -1, Standard_False);
    myFirstPointMarker = square;

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
    // A RING, and a big one - the third of the three shapes, against the
    // first point's filled square and the placed points' dots. Its violet is
    // Theme::sketchPointMarker(), the palette's own sketch hue, which the
    // small placed dots also wear: the cursor is told apart from them by
    // being an open ring four times the size, the one size delta this file
    // has actually measured to be visible (1.5 against 4.0 - see
    // setSketchPointMarkers()). Sharing the hue is deliberate rather than
    // conceded: the live cursor is the same KIND of thing as the points it is
    // about to become, while the square that closes the outline is not, and
    // that is the distinction accent() is spent on.
    Handle(SketchPointMarker) cursor =
        makeMarker(point, Aspect_TOM_RING1, toOcctColor(Theme::sketchPointMarker()), 4.0);
    markInSketchLayer(cursor);
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

void OcctViewWidget::setSketchStraightAnchor(const gp_Pnt& prev)
{
    myStraightPrev = prev;
    myHasStraightAnchor = true;
}

void OcctViewWidget::clearSketchStraightAnchor()
{
    myHasStraightAnchor = false;
}

void OcctViewWidget::setSketchCloseTarget(const gp_Pnt& first)
{
    myCloseTarget = first;
    myHasCloseTarget = true;
}

void OcctViewWidget::clearSketchCloseTarget()
{
    myHasCloseTarget = false;
}

double OcctViewWidget::sketchCloseTolerance() const
{
    // Half a grid step is forgiving but unambiguous - it cannot reach the
    // next grid intersection - and 5 mm is the free-hand equivalent.
    return mySnapEnabled && mySnapStep > 0.0 ? mySnapStep * 0.5 : 5.0;
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
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane(),
                          Theme::gridDensity());
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

double OcctViewWidget::cameraViewHeightAtTarget() const
{
    if (myView.IsNull()) return 0.0;
    // gp_XYZ of (width, height, depth) at the focal distance. Y is the height,
    // which is the one worldPerPixel() divides by the viewport's own height.
    return myView->Camera()->ViewDimensions().Y();
}

gp_Dir OcctViewWidget::liveCameraDirection() const
{
    if (myView.IsNull()) return gp_Dir(0.0, 0.0, 1.0);
    return myView->Camera()->Direction();
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
    // And the same rule for the two sketch constraints: each names a point in
    // the sketch that is ending or has not started yet.
    clearSketchStraightAnchor();
    clearSketchCloseTarget();
}

QString OcctViewWidget::faceOnOrthoDirection() const
{
    // The eligibility half of gridPlane()'s own priority (see there),
    // factored out to a single place so a caller that needs the DIRECTION
    // rather than the plane - the status label's cue - reads the same
    // answer rather than re-deriving "which views count" a second time.
    // Never true outside an EFFECTIVELY orthographic look: Top/Bottom
    // already see the ground grid face-on, and Persp has no "squared onto
    // a world axis" to speak of.
    if (!myCamera.effectiveOrtho()) return QString();
    const QString direction = viewDirectionName();
    if (direction == QStringLiteral("Front") || direction == QStringLiteral("Back") ||
        direction == QStringLiteral("Right") || direction == QStringLiteral("Left"))
        return direction;
    return QString();
}

gp_Pln OcctViewWidget::faceOnOrthoPlane() const
{
    // The UNLOCKED half of gridPlane()'s priority, on its own: the vertical
    // world plane a face-on Front/Back/Left/Right orthographic look is
    // squared onto, or the ground plane when the current view is not
    // eligible (faceOnOrthoDirection() above answers that). Callers outside
    // this class - MainWindow::onStartSketch(), specifically - use this to
    // derive the ACTUAL plane a new outline lands on, on exactly the terms
    // the grid is already drawn on; gridPlane() itself calls this too now,
    // so there is one construction of these two planes, not two that could
    // drift apart.
    const QString direction = faceOnOrthoDirection();
    if (direction == QStringLiteral("Front") || direction == QStringLiteral("Back")) {
        // World XZ, normal +Y - Front and Back share it: both look straight
        // along the world Y axis, so the plane their view is squared onto is
        // identical either way.
        return gp_Pln(gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0),
                             gp_Dir(1.0, 0.0, 0.0)));
    }
    if (direction == QStringLiteral("Right") || direction == QStringLiteral("Left")) {
        // World YZ, normal +X - the same sharing, along X instead of Y.
        return gp_Pln(gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0),
                             gp_Dir(0.0, 1.0, 0.0)));
    }
    return gp_Pln(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
}

gp_Pln OcctViewWidget::gridPlane() const
{
    // Since a face can be locked, the grid is drawn exactly coplanar with a
    // shaded face, and two coplanar surfaces are a depth-buffer tie: the grid
    // stipples through the face and flickers as the camera moves. So the grid
    // is displaced a hair toward whichever side of the plane the eye is on.
    //
    // The grid's Z-layer does NOT replace this, and the two solve different
    // halves of the same picture: the layer settles draw ORDER (the grid is
    // rendered after the bodies and before the sketch work, and writes no
    // depth), while this nudge settles the DEPTH TIE that decides whether the
    // locked face or the grid drawn on it wins. Drop the nudge and the
    // locked-face grid stipples again; drop the layer and the nudge makes the
    // grid win against the outline too. See GridRenderer::zLayer().
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

    // Task 5.1's substitution, ahead of the nudge above so it applies to
    // whichever plane is actually chosen. Priority order:
    //   1. A locked face's own plane (mySketchPlane, unchanged) - the grid
    //      is a click-site preview while one is locked, never in question.
    //   2. Otherwise, in an EFFECTIVELY orthographic look square onto a world
    //      axis (Front/Back/Left/Right - never Top/Bottom, which already see
    //      the ground grid face-on, and never Persp), the vertical plane
    //      that view is actually squared up to: Front/Back share the world
    //      XZ plane (normal +Y), Left/Right share YZ (normal +X), both
    //      through the origin. Ground otherwise (Persp, or Top/Bottom).
    // Without this, the unlocked ground grid - a HORIZONTAL plane - viewed
    // face-on from Front collapses to the single line where it meets the
    // view direction, which teaches the user nothing about scale in a face-
    // on look, exactly the failure locking a face already solves for a real
    // face.
    //
    // This is a VISUAL substitution only when the grid reads it: it reads
    // mySketchPlane/myWorkPlaneLocked but never writes them, so
    // setWorkPlane() remains the one place a click's plane is decided here.
    // Fix round (2026-09-03): Start Sketch now calls faceOnOrthoPlane()
    // itself to derive the ACTUAL click plane too - see
    // MainWindow::onStartSketch() - so the two no longer disagree the way
    // the first round of this task left them.
    const gp_Pln basePlane = myWorkPlaneLocked ? mySketchPlane : faceOnOrthoPlane();

    const double distance = std::max(1.0, myCamera.state().distance);
    const double octave = std::ldexp(1.0, static_cast<int>(std::lround(std::log2(distance))));
    const double nudge = octave * 1.0e-4;
    const gp_Dir normal = basePlane.Axis().Direction();
    const gp_Vec toEye(basePlane.Location(), myCamera.eyePosition());

    gp_Pln plane = basePlane;
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

bool OcctViewWidget::pointOnSketchPlane(int px, int py, gp_Pnt& out, bool straight) const
{
    gp_Lin ray;
    if (!rayThroughPixel(px, py, ray)) return false;

    if (!SketchController::intersectRayWithPlane(ray, mySketchPlane, out)) return false;
    // A PERSPECTIVE camera has a horizon: an intersection with the sketch plane
    // can lie BEHIND the eye when the cursor is above it. Such a hit is not a
    // point the user can see - reject it.
    //
    // A parallel projection has no horizon. Every ray is the view direction, so
    // either they all meet the plane or none of them do, and "behind" is only
    // measured from wherever OCCT's near plane happens to sit - which auto
    // z-fit moves with the scene. Applying the perspective rule there would
    // refuse clicks that are perfectly visible, so the guard is skipped rather
    // than trusted to be harmless. ConvertWithProj itself needs no branch: it
    // unprojects the pixel at both depths and subtracts, which is projection
    // agnostic.
    if (!myCamera.effectiveOrtho()) {
        const gp_Vec toHit(ray.Location(), out);
        if (toHit.Dot(gp_Vec(ray.Direction())) <= 0.0) return false;
    }

    // Shift's 8-direction compass dial, and how it composes with Snap to
    // Grid.
    //
    // The dial itself: SketchController::snapToCompass() takes the raw
    // vector from the anchor (the previous placed point) to this hit and
    // snaps IT to the nearest of 8 directions, 45 degrees apart, measured
    // from the sketch plane's own +u axis. That replaced an earlier rule
    // that continued the previous SEGMENT dead straight - anchored the same
    // way, at the previous point, but with a direction fixed the moment the
    // segment before it was placed rather than read fresh off wherever the
    // cursor currently sits.
    //
    // The composition with grid snap is unchanged from that earlier rule:
    // SHIFT WINS THE DIRECTION, then the grid snaps the distance ALONG that
    // direction. Snapping to the plane grid first and projecting afterwards
    // would land off the grid; projecting first and then snapping to the
    // plane grid would land off the line. Only one of the two constraints
    // can be exact, and the direction is the one the user is holding a key
    // down to get - a segment that is 3 mm off its compass line is the
    // failure Shift exists to prevent, while a length of 47 mm instead of
    // 50 is not. Rounding the line parameter keeps both whenever the anchor
    // itself is on the grid and the direction is axis-aligned, which is the
    // ordinary case.
    //
    // One exemption, and it is not a special case so much as a precedence:
    // CLOSING THE OUTLINE OUTRANKS CONTINUING IT STRAIGHT. Clicking the first
    // point back is one of the two ways to finish a sketch, and the
    // projection moves the click off the very point it was aimed at - so with
    // Shift held that route silently stopped working, and a modifier that
    // disables a way out of the mode is worse than one that does nothing.
    // Falling through then takes the ordinary grid snap, which lands the
    // click exactly on the first point - so a Shift-click on the start point
    // behaves precisely like a plain one, rather than merely closing by a
    // different route.
    //
    // Tested on the SNAPPED plane hit when snapping is on, not the raw one -
    // myCloseTarget is itself a grid-snapped point (the first sketch point
    // was placed through this same snap), and comparing a RAW ray hit against
    // it directly is comparing two things on different footings: the raw hit
    // can sit up to half a grid cell's DIAGONAL from the corner it will snap
    // to (7.07 mm at a 10 mm step), which is already past the 5 mm tolerance
    // sketchCloseTolerance() grants - so whether hovering the first point
    // registers as a close depended on exactly where in the cell the ray
    // happened to land, and device-pixel rounding at a non-integer display
    // scale (1.25x measured) was enough to tip it into the failing corner.
    // Pre-snapping the probe first puts both sides of the comparison on the
    // grid, so the exemption fires whenever the point WOULD land on the
    // first point after the ordinary snap below - which is the only question
    // that actually matters here.
    const gp_Pnt closeProbe =
        (mySnapEnabled && mySnapStep > 0.0)
            ? SketchController::snapToPlaneGrid(out, mySketchPlane, mySnapStep)
            : out;
    const bool closing =
        myHasCloseTarget && closeProbe.Distance(myCloseTarget) <= sketchCloseTolerance();

    if (straight && myHasStraightAnchor && !closing) {
        gp_Dir dir;
        // A candidate coincident with the anchor has no angle to dial -
        // snapToCompass() reports that and `out` is left as the raw plane
        // hit, the same degenerate-input rule the previous-segment version
        // followed for two coincident points.
        if (SketchController::snapToCompass(mySketchPlane, myStraightPrev, out, dir)) {
            out = SketchController::snapToDirection(myStraightPrev, dir, out);
            if (mySnapEnabled && mySnapStep > 0.0) {
                const gp_Vec along(dir);
                const double t = gp_Vec(myStraightPrev, out).Dot(along);
                out = myStraightPrev.Translated(along * (std::round(t / mySnapStep) * mySnapStep));
            }
        }
        return true;
    }

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
    // Same rule, and the same ortho exemption, as pointOnSketchPlane() above -
    // see there. A wheel notch over the sky in ortho would otherwise fall back
    // to a plain zoom rather than zooming toward the ground under the cursor.
    if (!myCamera.effectiveOrtho()) {
        const gp_Vec toHit(ray.Location(), out);
        if (toHit.Dot(gp_Vec(ray.Direction())) <= 0.0) return false;
    }
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
    // World units per pixel at target depth - the same maths panning already
    // used inline.
    //
    // ONE formula for both projections, and that is a property of how
    // applyCameraState() builds the orthographic frustum rather than a
    // coincidence. For perspective this is the visible height at the target's
    // depth divided by the viewport's height. For orthographic the visible
    // height is the camera's parallel Scale, at every depth - and
    // applyCameraState() sets that Scale to exactly this height, so the two
    // agree by construction. If that ever stops being true, every screen-sized
    // piece of furniture in the scene (dimension arrowheads and gaps, both
    // drag arrows) is wrong in ortho, and this is the single place to branch.
    // effectiveFovyDeg(), not kFovyDeg directly - Task 7.2's render-settings
    // FOV override changes this while render mode is on, and the invariant
    // this function documents (one formula, both projections, because
    // applyCameraState() ties the orthographic Scale to exactly this height)
    // has to keep holding under a live FOV exactly as it did under a fixed
    // one, or every screen-sized thing in the scene goes wrong the moment
    // the slider moves.
    return 2.0 * myCamera.state().distance *
           std::tan(0.5 * effectiveFovyDeg() * 3.14159265358979323846 / 180.0) /
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

std::vector<TopoDS_Edge> OcctViewWidget::selectedEdges() const
{
    std::vector<TopoDS_Edge> edges;
    if (myContext.IsNull()) return edges;

    for (myContext->InitSelected(); myContext->MoreSelected(); myContext->NextSelected()) {
        if (!myContext->HasSelectedShape()) continue;
        const TopoDS_Shape shape = myContext->SelectedShape();
        if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) continue;
        edges.push_back(TopoDS::Edge(shape));
    }
    return edges;
}

TopoDS_Edge OcctViewWidget::lastSelectedEdge() const
{
    const std::vector<TopoDS_Edge> edges = selectedEdges();
    if (edges.empty()) return TopoDS_Edge();

    // Remembered, but never trusted: a Shift-click that toggled it back off,
    // or a rebuild that replaced the topology, leaves a stale edge here that
    // is no longer part of the selection. Falling back to the last entry
    // keeps the arrow on SOME selected edge rather than on none.
    if (!myLastPickedEdge.IsNull()) {
        for (const TopoDS_Edge& edge : edges) {
            if (edge.IsSame(myLastPickedEdge)) return edge;
        }
    }
    return edges.back();
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
    myLastPickedEdge.Nullify();   // nothing is selected, so nothing was picked last
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

    if (!myRenderModeActive) return myView->Dump(path.toUtf8().constData()) == Standard_True;

    // Render mode doubles the export - the view's own DEVICE-pixel size
    // (toDevicePixels()'s own conversion, so a 150% display's screenshot is
    // 2x its OWN already-scaled pixel count, not 2x the logical widget
    // size), through ToPixMap() rather than Dump(): it renders an offscreen
    // buffer of the requested target size directly, with no window resize
    // needed - Dump() has no size parameter of its own to hand it one.
    const QPoint deviceSize = toDevicePixels(QPoint(width(), height()));
    Image_AlienPixMap pixmap;
    if (!myView->ToPixMap(pixmap, deviceSize.x() * 2, deviceSize.y() * 2)) return false;
    return pixmap.Save(path.toUtf8().constData());
}

QImage OcctViewWidget::captureThumbnail()
{
    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/furnifyme-thumb-XXXXXX.png"));
    if (!temp.open()) return QImage();
    const QString path = temp.fileName();
    // Closed rather than left open: V3d_View::Dump opens the path itself and
    // "the output directory does not exist" is not the only way it can
    // refuse to write - a file handle already open on it is another.
    temp.close();

    if (!saveSnapshot(path)) {
        QFile::remove(path);
        return QImage();
    }
    const QImage image(path);
    QFile::remove(path);
    return image;
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
    // Live every call, not seeded once at initializeViewer() and left alone -
    // Task 7.2's render-settings FOV can change while render mode is on, and
    // effectiveFovyDeg() is the ONE place that decides which value is live
    // (kFovyDeg outside render mode, the override while it is on). Cheap
    // when nothing changed; OCCT does not distinguish a no-op SetFOVy() from
    // any other.
    cam->SetFOVy(effectiveFovyDeg());

    // The projection, from the ONE piece of state that decides it. There is no
    // second camera and no second turntable: the eye, the target and the up
    // vector above are the same in both modes, and only how the frustum is
    // built changes.
    //
    // The orthographic half needs its half-height set explicitly, because
    // Graphic3d_Camera keeps `Scale` and `Distance` linked only for a
    // perspective camera - switching the type alone would leave the parallel
    // scale at whatever it last was (1000 by default) and the scene would jump
    // in size. Tying it to 2*distance*tan(FOVy/2) is what makes the two modes
    // frame the target identically, which in turn is what lets worldPerPixel()
    // stay one formula for both (see there).
    //
    // ORDER MATTERS: SetScale() on a camera still marked perspective moves the
    // DISTANCE instead, so the type is set first.
    if (myCamera.effectiveOrtho()) {
        cam->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        cam->SetScale(worldPerPixel() * std::max(1, height()));
    } else {
        cam->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    }

    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane(),
                          Theme::gridDensity());
    // The transform gizmo is sized in world units and judged in screen ones,
    // so the zoom is half of its arithmetic - re-derived here, before the
    // redraw below carries it, rather than from a slot on cameraChanged()
    // that would need its own UpdateCurrentViewer(). Its own guards make this
    // free on a camera move that does not change the scale.
    updateManipulatorSize();
    // Screen-sized the same way - see its own header comment.
    updateSymmetryIndicator();
    // Screen-sized the same way, and no-ops itself the same way - see its
    // own equal-guard.
    updateMirrorPlacementIndicator();
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

    // A camera move invalidates whatever path-tracing accumulation the last
    // frame built up - see startPathTracingConvergence()'s own comment -
    // so every route that lands here (orbit, pan, zoom, an animation frame,
    // fitAll) restarts the countdown rather than leaving it running down
    // against a scene that just changed under it.
    if (myRenderModeActive && myRenderTier == RenderTier::PathTracing)
        startPathTracingConvergence();
}

void OcctViewWidget::setCameraStateNow(const CameraState& state)
{
    myCamera.setState(state);
    applyCameraState();
}

void OcctViewWidget::fitAll()
{
    if (myView.IsNull()) return;

    // Frame everything we display ourselves (the grid and view cube are
    // presentation furniture, not content).
    //
    // OUTLINES COUNT. They are document items since Phase 7, and this walked
    // mySolids alone - so a document holding only outlines fell straight to
    // the +/-250 fallback below, and an outline drawn outside that box could
    // not be brought back by the one control whose entire job is to find
    // things. Both maps hold what this widget displays; a visibility toggle
    // erases the presentation without removing the entry, so Fit All frames
    // the whole document rather than the currently-visible part of it - which
    // is the behaviour the bodies have always had, and the two should not
    // differ on the same question.
    Bnd_Box box;
    for (const auto& entry : mySolids) {
        Bnd_Box b;
        BRepBndLib::Add(entry.second->Shape(), b);
        box.Add(b);
    }
    for (const auto& entry : myOutlines) {
        Bnd_Box b;
        BRepBndLib::Add(entry.second->Shape(), b);
        box.Add(b);
    }
    if (box.IsVoid()) box.Update(-250.0, -250.0, 0.0, 250.0, 250.0, 10.0);
    CameraController scratch = myCamera;
    // effectiveFovyDeg(), not kFovyDeg - Fit All stays reachable while
    // render mode is on (CLAUDE.md's "framing a shot is not a modeling
    // gesture"), and framing against a stale 45 degrees while the live FOV
    // is something else would compute a distance that does not actually
    // fit the box in what the camera is really showing.
    scratch.frame(box, effectiveFovyDeg());
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
// Positions in viewDirectionNames(). Naming them keeps viewDirectionName()
// readable while it returns entries OF that list rather than its own copies of
// the same seven literals.
enum ViewName { NamePersp = 0, NameTop, NameBottom, NameFront, NameBack, NameRight, NameLeft };

// File-local since the app bar's button stopped reserving its width against
// these: the button shows the projection now, and the only remaining consumer
// of the names is the function immediately below them.
const QStringList& viewDirectionNames()
{
    // Built once. viewDirectionName() can run on every camera frame, so this
    // must not allocate a seven-string list per orbit step.
    static const QStringList names = {
        QStringLiteral("Persp"),  QStringLiteral("Top"),   QStringLiteral("Bottom"),
        QStringLiteral("Front"),  QStringLiteral("Back"),  QStringLiteral("Right"),
        QStringLiteral("Left")};
    return names;
}
}  // namespace

void OcctViewWidget::setBaseProjection(CameraController::Projection projection)
{
    myCamera.setBaseProjection(projection);
    // ...and the loan is handed back, so the toggle ALWAYS changes what is on
    // screen. Without this, clicking it during a borrowed orthographic look -
    // which is exactly the state a face lock or a gizmo arm leaves behind -
    // flips the label Ortho->Persp while effectiveOrtho() stays true and the
    // viewport does not move. Twice in a row, since the base was perspective
    // to begin with. A control that visibly does nothing is broken to the
    // person clicking it, whatever the state machine underneath believes; the
    // loan is a convenience for gestures that did not ask about projection,
    // and this is the one gesture that is entirely about it.
    myCamera.setTemporaryOrtho(false);
    // Straight onto the OCCT camera through the one write site, which also
    // redraws and tells every camera-following overlay.
    applyCameraState();
}

bool OcctViewWidget::viewIsOrthographic() const
{
    if (myView.IsNull()) return myCamera.effectiveOrtho();
    return myView->Camera()->ProjectionType() ==
           Graphic3d_Camera::Projection_Orthographic;
}

QString OcctViewWidget::viewDirectionName() const
{
    const QStringList& names = viewDirectionNames();
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
    // A true 90, not one short of it (Task 6.2's fix) - CameraController::
    // upVector() no longer degenerates there. Azimuth going along for the
    // ride unused is fine: eyePosition() is insensitive to it exactly
    // overhead, but upVector() still reads it, so the view rotates about
    // its own axis exactly as an orbit approaching the pole would.
    s.elevationDeg = 90.0;
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

void OcctViewWidget::applyTheme()
{
    if (myView.IsNull() || myContext.IsNull()) return;

    // The flat colour outside render mode, the studio gradient while it is
    // on - one function so a theme edit re-derives whichever is live rather
    // than always repainting the flat one underneath an active gradient.
    // This is what makes render mode's backdrop "re-derived on themeChanged
    // while active" true: this call already runs on every theme edit
    // (onThemeChanged() below), so render mode needed no second broadcast.
    applyBackgroundForMode();

    // OCCT's default highlight barely reads against a shaded body. Make hover
    // and selection unmistakable - not being able to tell what is selected was
    // the single most confusing thing about the app.
    const Quantity_Color hoverColour = toOcctColor(Theme::highlightHover());
    const Quantity_Color pickedColour = toOcctColor(Theme::highlightSelected());

    const Handle(Prs3d_Drawer) hover = myContext->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic);
    hover->SetColor(hoverColour);
    hover->SetDisplayMode(AIS_Shaded);
    hover->SetTransparency(0.0f);

    const Handle(Prs3d_Drawer) picked = myContext->HighlightStyle(Prs3d_TypeOfHighlight_Selected);
    picked->SetColor(pickedColour);
    picked->SetDisplayMode(AIS_Shaded);
    picked->SetTransparency(0.0f);

    // Sub-shape (face-mode) highlighting uses its own styles.
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->SetColor(hoverColour);
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(pickedColour);

    // The grid's colours are baked into its vertices, so a repaint is not
    // enough - it has to be built again. invalidate() only drops the cache;
    // the update() below is what actually rebuilds it, exactly once. During
    // initializeViewer() this runs before attach(), where update() is a no-op
    // and the attach that follows does the first real build.
    myGridRenderer.invalidate();
    myGridRenderer.update(myCamera.state().distance, myCamera.state().target, gridPlane(),
                          Theme::gridDensity());

    // The same problem one presentation over: both drag arrows bake
    // Theme::accent() into the AIS object at build time and their show()
    // early-outs on an unchanged pose, so a live arrow kept the old accent.
    // Both are no-ops when nothing is showing. The edge-length annotation
    // needs no call here - MainWindow already drives refreshDimension() from
    // appStateChanged, which onThemeChanged() ends by emitting.
    myPullArrow.reapplyTheme();
    myBevelArrow.reapplyTheme();

    myContext->UpdateCurrentViewer();
    update();
}

QColor OcctViewWidget::renderBackdropColourImpl() const
{
    // Task 7.2's background swatch overrides this outright, once set - the
    // ONE derivation both the clear colour (applyBackgroundForMode()) and
    // the floor material (applyRenderFloorMaterialForTier()) read, so the
    // two can never independently drift the way two separate overrides
    // could.
    if (myRenderBackgroundOverride.isValid()) return myRenderBackgroundOverride;

    // A light warm grey - the user's own reference shot, not a taste call -
    // blended 4:1 toward the viewport token so an Appearance edit still
    // shifts it while the resting look stays a studio neutral. A gradient
    // was tried first and rejected by the user: a studio shot's floor has
    // to dissolve into its background, and only a flat colour shared with
    // the floor can make that seam actually invisible.
    const QColor base = Theme::viewport();
    constexpr int kWarmR = 226, kWarmG = 222, kWarmB = 214;
    return QColor((kWarmR * 4 + base.red()) / 5,
                  (kWarmG * 4 + base.green()) / 5,
                  (kWarmB * 4 + base.blue()) / 5);
}

void OcctViewWidget::applyBackgroundForMode()
{
    if (myView.IsNull()) return;

    if (!myRenderModeActive) {
        myView->SetBackgroundColor(toOcctColor(Theme::viewport()));
        return;
    }

    myView->SetBackgroundColor(toOcctColor(renderBackdropColourImpl()));
    // The floor wears the same colour, so a theme edit landing here while
    // render mode is up has to re-dress it too - rebuilt outright, the same
    // way the grid is rebuilt on a theme edit, because its colour is baked
    // into the displayed material rather than read live.
    if (!myRenderFloor.IsNull()) showRenderFloor();
}

void OcctViewWidget::showRenderFloor()
{
    hideRenderFloor();
    if (myContext.IsNull()) return;

    // The floor stands under what is actually on screen - a hidden body must
    // not stretch it, and must not decide where "under" is.
    Bnd_Box box;
    for (const auto& entry : mySolids) {
        if (!myContext->IsDisplayed(entry.second)) continue;
        Bnd_Box b;
        BRepBndLib::Add(entry.second->Shape(), b);
        box.Add(b);
    }
    if (box.IsVoid()) return;

    Standard_Real xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    // Big enough that its edge stays out of frame at any orbit that looks
    // down on the furniture at all - only a near-horizontal camera ever sees
    // a horizon, and a studio shot is not taken lying on the floor.
    const double half = std::max({xmax - xmin, ymax - ymin, 100.0}) * 4.0;
    const gp_Pnt centre((xmin + xmax) / 2.0, (ymin + ymax) / 2.0,
                        // A hair below the lowest body, not exactly at it: a
                        // body resting at Z = 0 would otherwise be coplanar
                        // with the floor across its whole underside, and a
                        // depth tie is a coin toss per pixel.
                        zmin - 0.1);
    const TopoDS_Face face =
        BRepBuilderAPI_MakeFace(gp_Pln(centre, gp_Dir(0.0, 0.0, 1.0)),
                                -half, half, -half, half)
            .Face();

    myRenderFloor = new AIS_Shape(face);
    // The material half is per-tier - fix round 2's scoping ruling. Reads
    // myRenderTier as it stands right now: before the first-ever probe that
    // is RenderTier::Plain, this enum's own zero-cost default, which is
    // correctly the Phong branch - exactly the material the Shadows-tier
    // candidacy probe (probeShadowPixelsDiffer(), called from inside
    // probeRenderTier()) needs on screen to judge shadow contrast against.
    applyRenderFloorMaterialForTier(isRayTracedTier(myRenderTier));
    // Selection mode -1, the previews' own never-pickable path - hover can
    // never highlight it and no pick can ever land on it, which also keeps
    // it out of the exit-click's way.
    myContext->Display(myRenderFloor, AIS_Shaded, -1, Standard_False);
}

void OcctViewWidget::hideRenderFloor()
{
    if (myRenderFloor.IsNull()) return;
    if (!myContext.IsNull()) myContext->Remove(myRenderFloor, Standard_False);
    myRenderFloor.Nullify();
}

void OcctViewWidget::applyRenderFloorMaterialForTier(bool pbrTier)
{
    if (myRenderFloor.IsNull()) return;

    const QColor floorColour = renderBackdropColourImpl();
    Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);

    if (!pbrTier) {
        // The ORIGINAL Milestone-3 Phong calibration, restored VERBATIM -
        // the controller's explicit fix-round-2 ruling: "that shadow IS the
        // tier." The floor has to render AT the backdrop colour or the seam
        // between the two reads as a horizon - and measured pixels showed
        // the default light rig is too weak for any lit material to reach
        // that tone (a full-white diffuse floor topped out well short of
        // it). So the tone is carried by EMISSIVE, which no light can dim,
        // at 87.5% of the backdrop; the white diffuse layer on top adds the
        // remaining brightness under the doubled key light and is exactly
        // what the shadow map subtracts, giving the shadow its ~25%
        // contrast against a floor that still blends into the background.
        // Ambient and specular are off outright - ambient would double-
        // count the tone, and a glossy floor is a second light source. All
        // four factors were calibrated to 3/255 against sampled Dump()
        // pixels under Phong shading, which is exactly the shading model
        // this branch puts back (applyRenderTier() sets ShadingModel =
        // Phong for Shadows/Plain) - so this material means the same thing
        // now that it did in Milestone 3, unlike fix round 1's PBR attempt,
        // which read these same four calls as unconditional linear PBR
        // terms with no floor to land on.
        material.SetAmbientColor(toOcctColor(QColor(0, 0, 0)));
        material.SetDiffuseColor(toOcctColor(QColor(118, 118, 118)));
        material.SetSpecularColor(toOcctColor(QColor(0, 0, 0)));
        material.SetEmissiveColor(
            toOcctColor(QColor(static_cast<int>(floorColour.red() * 0.875),
                               static_cast<int>(floorColour.green() * 0.875),
                               static_cast<int>(floorColour.blue() * 0.875))));
    } else {
        // The ray-traced tiers' own material - fix round 3's correction of
        // fix round 1's pure-Emission attempt, which measured as
        // functionally BLACK under PathTracing specifically (delta
        // ~190/255 from the backdrop, see the task report): OCCT's path
        // tracer builds a physical BSDF from the material, and a
        // zero-albedo/pure-Emission surface apparently does not carry that
        // emission through GI's own light-transport the way a rasterized
        // or plain-ray-traced Emissive term does - confirmed working for
        // plain RayTracing in the original diagnostic, but PathTracing's
        // own GI pass is a different code path inside OCCT and was never
        // separately verified.
        //
        // The controller's direction: stop relying on Emission for the
        // ray-traced tiers at all. A plain diffuse floor - albedo AT the
        // backdrop colour, zero emission, roughness high, metallic 0 - is
        // what a physically based renderer expects a studio floor to be;
        // under real global illumination plus filmic tone mapping (both on
        // for these two tiers - see applyRenderTier()), a GI-lit diffuse
        // surface naturally grounds the shot and the shadow comes for free,
        // the same physical mechanism the Shadows tier's shadow map
        // approximates by hand.
        //
        // MEASURED RESULT (fix round 3's own calibration Dump, PathTracing
        // tier): essentially UNCHANGED from the pure-Emission attempt -
        // delta still ~190/255, the floor point still reads as functionally
        // black. Two materials built on opposite theories producing the
        // same near-zero output means the PathTracing GI pass is not
        // lighting this floor geometry at all, not that either material's
        // calibration missed - and Color() is already at the backdrop's own
        // channel value (near the 1.0 ceiling), so there was no meaningful
        // headroom left to try boosting it further. Kept as the materially
        // more correct choice (this is genuinely what a physical floor
        // should be, and RayTracing - no GI - is expected to read it
        // correctly the same way it read the old Emission material), with
        // the open PathTracing-GI defect ledgered in the task report rather
        // than chased with a third material theory. See gui_smoke's own
        // PathTracing-tier floor-blend check for the recorded number.
        Graphic3d_PBRMaterial floorPbr;
        floorPbr.SetMetallic(0.0f);
        floorPbr.SetRoughness(0.95f);
        floorPbr.SetColor(Quantity_Color(floorColour.redF(), floorColour.greenF(),
                                         floorColour.blueF(), Quantity_TOC_RGB));
        floorPbr.SetEmission(NCollection_Vec3<float>(0.0f, 0.0f, 0.0f));
        material.SetPBRMaterial(floorPbr);
    }

    myRenderFloor->SetMaterial(material);
    // Redisplay(), on applyRenderBodyMaterials()'s own newly-measured terms:
    // SetMaterial() on an object already Display()ed does not by itself
    // guarantee the next redraw picks it up. showRenderFloor()'s own
    // Display() call (immediately after this function returns, on first
    // build) already forces a fresh presentation regardless, so this only
    // matters on the OTHER caller - applyRenderTier()'s tier-switch and
    // Task 7.2's live background-override path - where the floor is
    // already on screen and only its material is changing.
    if (!myContext.IsNull()) myContext->Redisplay(myRenderFloor, Standard_False);
}

gp_Dir OcctViewWidget::studioKeyDirectionForAzimuth(double azimuthDeg) const
{
    // The Milestone-3 calibrated studio key was the hardcoded
    // gp_Dir(-0.45, 0.35, -0.82) - this reproduces it exactly at this
    // class's own default azimuth (set in the constructor via std::atan2)
    // and holds the elevation fixed while azimuth sweeps around the
    // vertical axis. The vertical component is what keeps the shadow
    // beside the furniture rather than under it or off the floor's far
    // edge, which is why "Light angle" is azimuth-only per the mockup
    // rather than a second elevation control.
    constexpr double kHorizontalMag = 0.5700701699971242;   // std::hypot(-0.45, 0.35)
    constexpr double kVertical = -0.82;
    const double az = azimuthDeg * 3.14159265358979323846 / 180.0;
    return gp_Dir(kHorizontalMag * std::cos(az), kHorizontalMag * std::sin(az), kVertical);
}

void OcctViewWidget::applyRenderLightAngleAndStrength()
{
    if (myRenderSavedLights.empty()) return;   // render mode is off - nothing to move
    const gp_Dir direction = studioKeyDirectionForAzimuth(myRenderLightAngleDeg);
    for (auto& saved : myRenderSavedLights) {
        saved.light->SetDirection(direction);
        saved.light->SetIntensity(
            static_cast<Standard_ShortReal>(saved.intensity * myRenderLightStrength));
    }
}

void OcctViewWidget::redrawRenderModeLive()
{
    if (myView.IsNull()) return;
    // MEASURED FINDING, recorded here because every setter that calls this
    // function documents it and this is the one place the actual evidence
    // belongs. gui_smoke's own per-control Dump checks (Task 7.2) pinned
    // that on this build's OCCT 8.0.1 / GPU / driver combination, a live
    // SetMaterial() (roughness/metallic) or Graphic3d_CLight::SetIntensity()
    // edit on an object already ray-traced once does NOT reach the next
    // Dump()/ToPixMap(), while renderSurfaceRoughness()/renderMetal()/
    // renderLightStrength() themselves correctly read back the new value
    // throughout - the DATA is right, only the RENDER on this session is
    // not picking it up. Six genuinely distinct mechanisms were tried, in
    // this order, each measured against real Dump() pixels rather than
    // trusted by name (CLAUDE.md's zoom-persistence lesson): a plain
    // Redisplay() on the changed object; a settle loop of several
    // Redraw() calls (probeRenderFloorBlend()'s own kSettlePasses shape,
    // reused here); Graphic3d_CView::InvalidateBVHData() (the one public
    // hook OpenGl_View.hxx exposes over its own ray-trace BVH cache); an
    // unconditional camera-state poke (applyCameraState(), since an ORBIT
    // reliably re-renders a ray-traced scene - that IS render mode's own
    // frame-a-shot gesture); the round trip below, forcing a genuine
    // Graphic3d_RenderingMode transition through rasterization and back,
    // which is the one thing this class already KNOWS rebuilds the
    // ray-traced scene correctly (applyRenderTier()'s own path at render-
    // mode ENTRY, why the FIRST frame after entering always shows the
    // right material); and, fix round 1's own follow-up on a code
    // reviewer's specific suggestion, a full Remove()+Display() cycle on
    // every displayed body (Standard_False update, AIS_Shaded, selection
    // mode -1 to keep render mode's own picking-suppressed invariant, face
    // boundary draw reasserted off afterward) plus the floor's own
    // showRenderFloor() (already the most drastic recreation this class
    // has - a brand new AIS_Shape and a fresh Display() every time),
    // called for Surface/Metal/Light-strength/Background specifically
    // because a genuine structure teardown-and-rebuild is a materially
    // different code path from Redisplay()'s in-place update, and OCCT's
    // ray-trace layer has a documented history of picking up fresh
    // material only on structure (re)creation. Measured the same way as
    // the other five: it did not move a pixel either, on this GPU/driver -
    // reverted rather than shipped as dead weight, on the AIS_Manipulator
    // styling wall's own precedent (a finding recorded here, not a
    // subclass kept in the tree unused). NONE of the six moved a single
    // sampled pixel.
    //
    // A light's DIRECTION is the one exception - SetDirection() on the
    // SAME light object reliably reaches the render every time, because it
    // moves WHICH PIXELS fall in shadow, a per-pixel geometric query OCCT
    // must recompute every redraw regardless of any material/intensity
    // cache. That asymmetry is what rules out "the redraw path is broken
    // generally" and narrows this to a genuine, environment-specific
    // caching limitation on UNIFORM material/intensity properties
    // specifically - ledgered rather than chased further, the treatment
    // this file already gives the PathTracing GI floor defect. The round
    // trip below stays as the implementation regardless: it is the
    // textbook-correct way to force a ray-trace scene rebuild, on the off
    // chance a different OCCT version, GPU or driver responds to it even
    // though this one measured does not.
    if (isRayTracedTier(myRenderTier) && !myView.IsNull()) {
        Graphic3d_RenderingParams& params = myView->ChangeRenderingParams();
        const Graphic3d_RenderingMode wasMethod = params.Method;
        params.Method = Graphic3d_RM_RASTERIZATION;
        myView->Redraw();
        params.Method = wasMethod;
    }
    if (!myContext.IsNull()) myContext->UpdateCurrentViewer();
    myView->Redraw();
}

void OcctViewWidget::setRenderSurfaceRoughness(double roughness01)
{
    myRenderRoughness = std::clamp(roughness01, 0.0, 1.0);
    // No-op on Shadows/Plain by design - see this setter's own header
    // comment. isRayTracedTier() is the one written-down copy of "which
    // tiers are PBR", reused rather than re-tested here.
    if (myRenderModeActive && isRayTracedTier(myRenderTier) && !myContext.IsNull()) {
        applyRenderBodyMaterials();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderMetal(double metallic01)
{
    myRenderMetallic = std::clamp(metallic01, 0.0, 1.0);
    if (myRenderModeActive && isRayTracedTier(myRenderTier) && !myContext.IsNull()) {
        applyRenderBodyMaterials();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderLightAngleDeg(double azimuthDeg)
{
    // Wrapped rather than clamped - an azimuth is a compass heading, not a
    // bounded quantity, and a slider that refused to cross 359->0 would
    // read as broken.
    myRenderLightAngleDeg = std::fmod(azimuthDeg, 360.0);
    if (myRenderLightAngleDeg < 0.0) myRenderLightAngleDeg += 360.0;
    if (myRenderModeActive) {
        applyRenderLightAngleAndStrength();
        if (!myViewer.IsNull()) myViewer->UpdateLights();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderLightStrength(double multiplier)
{
    // 0.2x..4x - the same "usable photographic range" reasoning
    // kMinRenderFovDeg/kMaxRenderFovDeg apply to the FOV slider: a light
    // that could be dragged to zero or to a blown-out multiple is not a
    // control, it is a way to lose the shot.
    myRenderLightStrength = std::clamp(multiplier, 0.2, 4.0);
    if (myRenderModeActive) {
        applyRenderLightAngleAndStrength();
        if (!myViewer.IsNull()) myViewer->UpdateLights();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderBackgroundOverride(const QColor& colour)
{
    if (!colour.isValid()) return;
    myRenderBackgroundOverride = colour;
    // applyBackgroundForMode() re-reads renderBackdropColour() - which this
    // override now answers for - and, per its own comment, rebuilds the
    // floor too when one is on screen, so the two never drift apart.
    if (myRenderModeActive) {
        applyBackgroundForMode();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::clearRenderBackgroundOverride()
{
    if (!myRenderBackgroundOverride.isValid()) return;
    myRenderBackgroundOverride = QColor();
    if (myRenderModeActive) {
        applyBackgroundForMode();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderFov(double fovyDeg)
{
    myRenderFovyDeg = std::clamp(fovyDeg, kMinRenderFovDeg, kMaxRenderFovDeg);
    // Off while render mode is off: effectiveFovyDeg() would ignore the
    // stored value anyway, and calling applyCameraState() on a Null myView
    // (a fresh widget the viewer has never initialized) is a no-op there
    // too - this guard just skips the pointless work.
    if (myRenderModeActive && !myView.IsNull()) applyCameraState();
}

void OcctViewWidget::setLightsCastShadows(bool cast)
{
    if (myViewer.IsNull()) return;
    // Every directional light this viewer's SetDefaultLights() gave it -
    // Graphic3d_CLight::SetCastShadows() is what OCCT 8.0 actually offers
    // for a shadow-mapped RASTERIZATION light (checked against the real
    // header under vcpkg's opencascade include tree; V3d_DirectionalLight
    // itself carries no shadow API of its own, it inherits this one). An
    // ambient light (also part of SetDefaultLights()) is not a
    // V3d_DirectionalLight and DownCast() simply skips it.
    for (const Handle(Graphic3d_CLight)& light : myViewer->ActiveLights()) {
        const Handle(V3d_DirectionalLight) directional =
            Handle(V3d_DirectionalLight)::DownCast(light);
        if (!directional.IsNull()) directional->SetCastShadows(cast);
    }
}

void OcctViewWidget::applyRenderTier(RenderTier tier)
{
    if (myView.IsNull()) return;
    Graphic3d_RenderingParams& params = myView->ChangeRenderingParams();

    // Both ray-traced tiers drive the same OCCT pipeline (Method); what
    // separates them is global illumination and adaptive screen sampling
    // below, PathTracing's own look, not a second rendering mode.
    // isRayTracedTier() is the one written-down copy of this test - see its
    // own comment.
    const bool rayTraced = isRayTracedTier(tier);
    const bool pathTracing = tier == RenderTier::PathTracing;
    params.Method = rayTraced ? Graphic3d_RM_RAYTRACING : Graphic3d_RM_RASTERIZATION;
    // Fix round 2's own measured finding: this is NOT a ray-traced-only
    // flag despite living under the header's "Ray-Tracing/Path-Tracing
    // parameters" section - it also gates the RASTERIZED shadow-map
    // feature Shadows relies on. The pre-Task-7.1 code never touched this
    // field for Shadows/Plain at all, so it stayed at its Graphic3d_
    // RenderingParams constructor default (true); this task's original
    // `= rayTraced` (false for Shadows/Plain) silently disabled shadow
    // rendering there entirely, which the resurrected shadow-contrast probe
    // (probeRenderFloorShadowContrast(), fix round 2) caught: the M3
    // material's blend measured perfectly (delta 1-3/255) but the light's
    // SetCastShadows() toggle produced no pixel difference at all, because
    // this flag - not the per-light one - was the one actually silencing
    // it. Always true now, matching every tier's real pre-task behaviour
    // rather than an unmeasured assumption about which tiers need it.
    params.IsShadowEnabled = true;

    // Path tracing's own parameters, explicitly set for EVERY tier rather
    // than only switched on for PathTracing - a probe that tries PathTracing
    // first and falls back to RayTracing or Shadows calls this function
    // again on the same live view, and a field this branch left untouched
    // would carry PathTracing's value into a tier that never asked for it.
    // AdaptiveScreenSampling is what makes the image progressively refine
    // across repeated Redraw() calls at rest instead of computing one fixed
    // sample count per frame - see startPathTracingConvergence() for the
    // paint loop that actually asks for those repeated redraws.
    params.IsGlobalIlluminationEnabled = pathTracing;
    params.AdaptiveScreenSampling = pathTracing;
    params.IsAntialiasingEnabled = pathTracing;
    // Measured, not assumed: AdaptiveScreenSampling caps how many screen
    // TILES (RayTracingTileSize, 32 px square by default) a single Redraw()
    // renders - NbRayTracingTiles, 256 by default. A 1200x760 viewport is
    // ~900 tiles, so at the default cap a redraw covers well under a third
    // of the screen, and this task's first calibration Dump showed exactly
    // that: real Dump() pixels came back almost entirely (0,0,0), not merely
    // noisy, because most of the frame had genuinely never been rendered
    // yet, only the OpenGl clear colour. -1 ("no limit," the header's own
    // words) renders every tile every frame - unlimited per THIS field's own
    // name, not literally uncapped work, since kPathTracingProbeThresholdMs
    // still refuses a GPU too slow to do that in one frame. Every other tier
    // puts the field back at its constructor default (256) - irrelevant
    // there (AdaptiveScreenSampling is off), but explicit for the same
    // "every branch writes every field" reason the rest of this function
    // does it.
    params.NbRayTracingTiles = pathTracing ? -1 : 256;

    // Fix round 2's scoping ruling, overriding this task's original "PBR
    // whichever tier render mode is on" reading of the brief: PBR shading
    // and filmic tone mapping apply ONLY to the two ray-traced tiers, where
    // `rayTraced` is already exactly the right test - `ToneMappingMethod`
    // genuinely only applies there (its own OCCT header comment: "for path
    // tracing"), and fix round 1 measured, the hard way, that the
    // RASTERIZED Pbr shader has nothing to tame its linear output the way
    // the ray-traced pipeline (or a real tone-mapping curve) does: the lit
    // Color() lobe alone saturated the Shadows-tier floor regardless of
    // Emission. Shadows and Plain get the Phong shading model back -
    // "exactly as before this task," the controller's own words - which is
    // what lets applyRenderFloorMaterialForTier()/clearRenderBodyMaterials()
    // below put the ORIGINAL, Milestone-3-calibrated Phong floor and body
    // materials back for those two tiers, shadow contrast included.
    params.ShadingModel =
        rayTraced ? Graphic3d_TypeOfShadingModel_Pbr : Graphic3d_TypeOfShadingModel_Phong;
    params.ToneMappingMethod =
        rayTraced ? Graphic3d_ToneMappingMethod_Filmic : Graphic3d_ToneMappingMethod_Disabled;

    // The material half of the same scoping - see each function's own
    // comment. Every tier switch (including the temporary ones the
    // measurement probes below make) re-applies the right pair, so a probe
    // that tries PathTracing then falls back to Shadows leaves both the
    // floor and the bodies in the material that tier actually needs.
    if (rayTraced) {
        applyRenderBodyMaterials();
    } else {
        clearRenderBodyMaterials();
    }
    applyRenderFloorMaterialForTier(rayTraced);

    // 4x the 1024 default while shadow-mapping, put back for every other
    // tier. At 1024 the shadow's edge on the floor is visibly blocky - the
    // map is stretched across the whole scene including the floor, so the
    // floor is exactly what made the default resolution stop being enough.
    params.ShadowMapResolution = (tier == RenderTier::Shadows) ? 4096 : 1024;
    setLightsCastShadows(tier == RenderTier::Shadows);
}

void OcctViewWidget::saveRenderParams()
{
    if (myView.IsNull()) return;
    const Graphic3d_RenderingParams& params = myView->RenderingParams();
    myRenderSavedParams.method = params.Method;
    myRenderSavedParams.shadingModel = params.ShadingModel;
    myRenderSavedParams.toneMappingMethod = params.ToneMappingMethod;
    myRenderSavedParams.isGlobalIlluminationEnabled = params.IsGlobalIlluminationEnabled;
    myRenderSavedParams.adaptiveScreenSampling = params.AdaptiveScreenSampling;
    myRenderSavedParams.isAntialiasingEnabled = params.IsAntialiasingEnabled;
    myRenderSavedParams.isShadowEnabled = params.IsShadowEnabled;
    myRenderSavedParams.shadowMapResolution = params.ShadowMapResolution;
    myRenderSavedParams.nbRayTracingTiles = params.NbRayTracingTiles;
}

void OcctViewWidget::restoreRenderParams()
{
    if (myView.IsNull()) return;
    Graphic3d_RenderingParams& params = myView->ChangeRenderingParams();
    params.Method = myRenderSavedParams.method;
    params.ShadingModel = myRenderSavedParams.shadingModel;
    params.ToneMappingMethod = myRenderSavedParams.toneMappingMethod;
    params.IsGlobalIlluminationEnabled = myRenderSavedParams.isGlobalIlluminationEnabled;
    params.AdaptiveScreenSampling = myRenderSavedParams.adaptiveScreenSampling;
    params.IsAntialiasingEnabled = myRenderSavedParams.isAntialiasingEnabled;
    params.IsShadowEnabled = myRenderSavedParams.isShadowEnabled;
    params.ShadowMapResolution = myRenderSavedParams.shadowMapResolution;
    params.NbRayTracingTiles = myRenderSavedParams.nbRayTracingTiles;
    // Shadow-casting lights are not part of Graphic3d_RenderingParams and so
    // are not in the snapshot above, but this class has only ever turned
    // them on for the Shadows tier - always off before render mode ever
    // ran - so putting that back is unconditional, applyRenderTier(Plain)'s
    // own old behaviour, kept here rather than resurrected through a call to
    // that function (which would also fight this function over ShadingModel
    // and the rest, see applyRenderTier()'s own comment on why it now
    // always writes them).
    setLightsCastShadows(false);
}

OcctViewWidget::RenderParamsProbe OcctViewWidget::renderParamsProbe() const
{
    RenderParamsProbe probe;
    if (myView.IsNull()) return probe;
    const Graphic3d_RenderingParams& params = myView->RenderingParams();
    probe.method = static_cast<int>(params.Method);
    probe.shadingModel = static_cast<int>(params.ShadingModel);
    probe.toneMappingMethod = static_cast<int>(params.ToneMappingMethod);
    probe.isGlobalIlluminationEnabled = params.IsGlobalIlluminationEnabled;
    probe.adaptiveScreenSampling = params.AdaptiveScreenSampling;
    probe.isAntialiasingEnabled = params.IsAntialiasingEnabled;
    probe.isShadowEnabled = params.IsShadowEnabled;
    probe.shadowMapResolution = params.ShadowMapResolution;
    probe.nbRayTracingTiles = params.NbRayTracingTiles;
    return probe;
}

void OcctViewWidget::applyRenderBodyMaterials()
{
    if (myContext.IsNull()) return;
    // A light, matte, non-metallic "furniture" material - measured against
    // real Dump() pixels rather than guessed from the setter names,
    // CLAUDE.md's own rule for this class of change. Called ONLY for the two
    // ray-traced tiers as of fix round 2 - see applyRenderTier(). The Phong
    // SetColor() stays at the ordinary GRAY70 body tone (0.70), unused while
    // this material is active (ShadingModel = Pbr there) but harmless to
    // keep populated; the PBR albedo below is deliberately DARKER (0.55, not
    // 0.70) - the rasterized-PBR-clips-white finding applyRenderFloorMaterial-
    // ForTier() documents does not apply to the ray-traced pipeline this
    // material is now scoped to, but the darker albedo was measured against
    // that pipeline directly and left as-is rather than re-guessed. Roughness
    // 0.55 is a middling matte, not glossy enough to add a hot specular
    // highlight on top.
    // Roughness/metallic now come from Task 7.2's Surface/Metal controls
    // (myRenderRoughness/myRenderMetallic) rather than the hardcoded
    // 0.55/0.0 this used to carry - their defaults reproduce those two
    // literals exactly, so a session that never opens the render settings
    // card gets the identical look this always shipped.
    Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);
    material.SetColor(Quantity_Color(0.70, 0.70, 0.68, Quantity_TOC_RGB));
    Graphic3d_PBRMaterial pbr;
    pbr.SetColor(Quantity_Color(0.55, 0.55, 0.53, Quantity_TOC_RGB));
    pbr.SetMetallic(static_cast<float>(myRenderMetallic));
    pbr.SetRoughness(static_cast<float>(myRenderRoughness));
    material.SetPBRMaterial(pbr);
    for (auto& entry : mySolids) {
        entry.second->SetMaterial(material);
        // Every OTHER place in this file that changes a displayed
        // AIS_Shape's attributes follows it with exactly this call - see
        // setRenderMode(true)'s own SetFaceBoundaryDraw() loop - so this
        // does too, the textbook-correct sequence for a live presentation
        // change. It is NOT, on its own, what makes a live Surface/Metal
        // drag reach this session's ray-traced Dump - see
        // redrawRenderModeLive()'s own comment (called by both setters
        // right after this function returns) for the measured finding on
        // what does and does not.
        myContext->Redisplay(entry.second, Standard_False);
    }
}

void OcctViewWidget::clearRenderBodyMaterials()
{
    if (myContext.IsNull()) return;
    // The Shadows/Plain half of the pair - fix round 2's scoping. A plain
    // UnsetMaterial() per solid is a complete revert to whatever stood
    // before render mode touched it, on the exact terms
    // applyRenderBodyMaterials()'s own header comment already establishes:
    // no code path outside these two ever calls SetMaterial() on a body.
    // Redisplay() for the same reason applyRenderBodyMaterials() now
    // carries one on its own SetMaterial() call - a presentation change
    // that is not followed by one is not guaranteed to reach the next
    // redraw.
    for (auto& entry : mySolids) {
        entry.second->UnsetMaterial();
        myContext->Redisplay(entry.second, Standard_False);
    }
}

void OcctViewWidget::startPathTracingConvergence()
{
    if (myView.IsNull()) return;
    if (myPathTracingRefineTimer == nullptr) {
        myPathTracingRefineTimer = new QTimer(this);
        constexpr int kIntervalMs = 50;
        myPathTracingRefineTimer->setInterval(kIntervalMs);
        connect(myPathTracingRefineTimer, &QTimer::timeout, this, [this]() {
            // Re-checked on every tick rather than trusted from whenever the
            // timer was started - render mode can exit or the tier can only
            // ever have been cached once, but this guards the same way every
            // other render-mode teardown in this file does, defensively
            // rather than because a real path to it missing stopCall was
            // found.
            if (!myRenderModeActive || myRenderTier != RenderTier::PathTracing ||
                myView.IsNull() || myPathTracingRefineTicksLeft <= 0) {
                stopPathTracingConvergence();
                return;
            }
            --myPathTracingRefineTicksLeft;
            myView->Redraw();
        });
    }
    // kPathTracingConvergeMs / kIntervalMs ticks - restarted, not merely
    // topped up, so a camera move mid-convergence gets the full window
    // again, matching the path tracer's own accumulation buffer starting
    // over the instant the view actually changes.
    myPathTracingRefineTicksLeft = kPathTracingConvergeMs / myPathTracingRefineTimer->interval();
    myPathTracingRefineTimer->start();
}

void OcctViewWidget::stopPathTracingConvergence()
{
    if (myPathTracingRefineTimer != nullptr) myPathTracingRefineTimer->stop();
    myPathTracingRefineTicksLeft = 0;
}

bool OcctViewWidget::probeShadowPixelsDiffer()
{
    // Real Dump() pixels, not the setter's own claim - CLAUDE.md's
    // zoom-persistence lesson, restated for this task: SetCastShadows(true)
    // returning does not mean a shadow actually reached the screen, and a
    // driver that silently ignores the flag is a real possibility this probe
    // exists to catch. Two full dumps, shadows off then on, compared pixel
    // by pixel; any real difference is accepted as proof the effect rendered.
    if (myView.IsNull()) return false;

    QTemporaryFile beforeFile(QDir::tempPath() +
                              QStringLiteral("/furnifyme-shadow-before-XXXXXX.png"));
    QTemporaryFile afterFile(QDir::tempPath() +
                             QStringLiteral("/furnifyme-shadow-after-XXXXXX.png"));
    if (!beforeFile.open() || !afterFile.open()) return false;
    const QString beforePath = beforeFile.fileName();
    const QString afterPath = afterFile.fileName();
    // Closed rather than left open - same reasoning captureThumbnail() gives:
    // Dump() opens the path itself, and a handle already open on it is
    // another way for that to fail besides a missing directory.
    beforeFile.close();
    afterFile.close();

    setLightsCastShadows(false);
    myView->Redraw();
    const bool dumpedBefore = myView->Dump(beforePath.toUtf8().constData()) == Standard_True;

    setLightsCastShadows(true);
    myView->Redraw();
    const bool dumpedAfter = myView->Dump(afterPath.toUtf8().constData()) == Standard_True;

    bool differ = false;
    if (dumpedBefore && dumpedAfter) {
        const QImage before(beforePath);
        const QImage after(afterPath);
        if (!before.isNull() && !after.isNull() && before.size() == after.size()) {
            // Sampled, not exhaustive - this runs once per session, but a
            // full-resolution nested loop over a live viewport is still real
            // work for what is fundamentally a yes/no question.
            constexpr int kStride = 4;
            for (int y = 0; y < before.height() && !differ; y += kStride) {
                for (int x = 0; x < before.width(); x += kStride) {
                    if (before.pixel(x, y) != after.pixel(x, y)) { differ = true; break; }
                }
            }
        }
    }

    QFile::remove(beforePath);
    QFile::remove(afterPath);
    return differ;
}

bool OcctViewWidget::probePathTracingChangedImage()
{
    if (!myRenderModeActive || myRenderTier != RenderTier::PathTracing || myView.IsNull())
        return false;

    QTemporaryFile ptFile(QDir::tempPath() +
                         QStringLiteral("/furnifyme-pathtracing-XXXXXX.png"));
    QTemporaryFile shadowsFile(QDir::tempPath() +
                              QStringLiteral("/furnifyme-shadows-compare-XXXXXX.png"));
    if (!ptFile.open() || !shadowsFile.open()) return false;
    const QString ptPath = ptFile.fileName();
    const QString shadowsPath = shadowsFile.fileName();
    ptFile.close();
    shadowsFile.close();

    // A few extra accumulation passes before the PathTracing capture - the
    // comparison only has to show "different from Shadows," not "fully
    // converged," but a single first frame is still the noisiest one this
    // tier ever draws and this probe wants a representative frame, not the
    // most-likely-to-look-different one.
    constexpr int kSettlePasses = 5;
    for (int i = 0; i < kSettlePasses; ++i) myView->Redraw();
    const bool dumpedPt = myView->Dump(ptPath.toUtf8().constData()) == Standard_True;

    // Shadows, temporarily - never cached, never re-probed, and restored
    // below before this function returns. This is the one legitimate reason
    // to call applyRenderTier() with something other than myRenderTier while
    // render mode is on: a same-scene comparison frame, not a real tier
    // change.
    applyRenderTier(RenderTier::Shadows);
    myView->Redraw();
    const bool dumpedShadows = myView->Dump(shadowsPath.toUtf8().constData()) == Standard_True;

    // Restored - this probe must never leave the session actually rendering
    // a tier other than the one it already cached and reported in the entry
    // toast.
    applyRenderTier(RenderTier::PathTracing);
    myView->Redraw();

    bool differ = false;
    if (dumpedPt && dumpedShadows) {
        const QImage pt(ptPath);
        const QImage shadows(shadowsPath);
        if (!pt.isNull() && !shadows.isNull() && pt.size() == shadows.size()) {
            constexpr int kStride = 4;
            for (int y = 0; y < pt.height() && !differ; y += kStride) {
                for (int x = 0; x < pt.width(); x += kStride) {
                    if (pt.pixel(x, y) != shadows.pixel(x, y)) { differ = true; break; }
                }
            }
        }
    }

    QFile::remove(ptPath);
    QFile::remove(shadowsPath);
    return differ;
}

OcctViewWidget::FloorBlendProbe OcctViewWidget::probeRenderFloorBlend(
    RenderTier forTier, const QPoint& floorPointLogical)
{
    FloorBlendProbe result;
    if (!myRenderModeActive || myView.IsNull() || myRenderFloor.IsNull()) return result;

    // Forced, temporarily - never cached, never re-probed, restored to the
    // session's real tier before ANY return below (including the failure
    // paths), on probePathTracingChangedImage()'s own rule: a measurement
    // probe must never leave the session actually rendering a tier other
    // than the one it already cached and reported.
    const RenderTier cachedTier = myRenderTier;
    applyRenderTier(forTier);
    // A few extra accumulation passes before dumping -
    // probePathTracingChangedImage()'s own kSettlePasses, needed here for
    // the identical reason: PathTracing's first Redraw() after a tier
    // switch is genuinely its noisiest frame (fix round 2's own measured
    // finding - a single-redraw Dump here came back with the floor point
    // nearly BLACK, delta ~184/255 from the backdrop, not the real
    // pure-Emission material's actual look). Harmless for Shadows/Plain -
    // rasterization has nothing to accumulate, so the extra redraws just
    // repaint the identical frame.
    constexpr int kSettlePasses = 5;
    for (int i = 0; i < kSettlePasses; ++i) myView->Redraw();

    QTemporaryFile file(QDir::tempPath() +
                        QStringLiteral("/furnifyme-floorblend-XXXXXX.png"));
    if (!file.open()) {
        applyRenderTier(cachedTier);
        myView->Redraw();
        return result;
    }
    const QString path = file.fileName();
    file.close();
    const bool dumped = myView->Dump(path.toUtf8().constData()) == Standard_True;

    applyRenderTier(cachedTier);
    myView->Redraw();

    if (dumped) {
        const QImage shot(path);
        const QPoint floorDevice = toDevicePixels(floorPointLogical);
        if (!shot.isNull() && shot.rect().contains(floorDevice)) {
            const QColor floorColour = shot.pixelColor(floorDevice);
            const QColor target = renderBackdropColourImpl();

            // Scan the top of the frame for the pixel closest to the TRUE
            // backdrop colour, rather than trusting one hardcoded corner to
            // sit above the floor's horizon regardless of camera framing -
            // the grid sweep's own "known grid line" technique, one probe
            // over. The top-most fifth of the dumped rows is always sky in
            // this app's axonometric render-mode framing (a horizontal
            // floor plane below a camera looking generally downward can
            // never reach the top of the frame), so this is a real scan,
            // not a single guess dressed up as one.
            bool foundBackdrop = false;
            qint64 bestDist = std::numeric_limits<qint64>::max();
            QColor backdropColour = target;
            const int scanRows = std::max(1, shot.height() / 5);
            for (int y = 0; y < scanRows; y += 2) {
                for (int x = 0; x < shot.width(); x += 8) {
                    const QColor c = shot.pixelColor(x, y);
                    const qint64 dr = c.red() - target.red();
                    const qint64 dg = c.green() - target.green();
                    const qint64 db = c.blue() - target.blue();
                    const qint64 dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist) {
                        bestDist = dist;
                        backdropColour = c;
                        foundBackdrop = true;
                    }
                }
            }

            if (foundBackdrop) {
                result.measured = true;
                result.deltaR = qAbs(floorColour.red() - backdropColour.red());
                result.deltaG = qAbs(floorColour.green() - backdropColour.green());
                result.deltaB = qAbs(floorColour.blue() - backdropColour.blue());
            }
        }
    }

    QFile::remove(path);
    return result;
}

bool OcctViewWidget::probeRenderFloorShadowContrast()
{
    if (!myRenderModeActive || myView.IsNull()) return false;

    // Forced, temporarily, on probeRenderFloorBlend()'s own rule - restored
    // to the session's real cached tier before returning, including via the
    // early return above (checked first, before anything is touched).
    const RenderTier cachedTier = myRenderTier;
    applyRenderTier(RenderTier::Shadows);
    myView->Redraw();

    // probeShadowPixelsDiffer() IS the tier probe's own acceptance test for
    // this exact tier - reused rather than re-implemented, so a regression
    // here is a regression in the same mechanism that decides whether the
    // Shadows tier is ever offered at all, not a second, possibly-drifted
    // copy of it.
    const bool differs = probeShadowPixelsDiffer();

    applyRenderTier(cachedTier);
    myView->Redraw();
    return differs;
}

OcctViewWidget::RenderTier OcctViewWidget::probeRenderTier()
{
    if (myView.IsNull()) return RenderTier::Plain;

    // Tier 0: path tracing - global illumination and adaptive screen
    // sampling on top of GPU ray tracing, timed against a single redraw on
    // its own, more lenient threshold (see kPathTracingProbeThresholdMs's
    // own comment for why: that first frame pays for shader compilation, a
    // one-time cost the plain tier-2 threshold below was never calibrated
    // to absorb). Wrapped in try/catch on the same ruling tier 2 already
    // follows: a driver that cannot do this is expected to REFUSE cleanly,
    // and OCCT reports that refusal as a Standard_Failure here rather than a
    // bool return.
    bool pathTracingFast = false;
    // PARKED (controller ruling, Milestone 4 Task 7.1 fix round 3): the
    // probe does not offer PathTracing until the GI floor defect is solved.
    // Two structurally opposite floor materials - pure emission and pure
    // diffuse albedo near the 1.0 ceiling - both measured functionally BLACK
    // under the GI pass (delta ~192/255 against the backdrop) while the
    // same scene renders correctly under plain RayTracing, so the best tier
    // the probe can HONESTLY hand a user today is RayTracing, which still
    // carries the PBR shading and tone mapping. Everything PathTracing
    // needs stays built and tested behind kPathTracingEnabled so the fix,
    // when it lands, is one constant away - and the suite's PT checks
    // already skip-by-environment on a machine whose probe lands elsewhere.
    if (kPathTracingEnabled) {
        try {
            applyRenderTier(RenderTier::PathTracing);
            QElapsedTimer timer;
            timer.start();
            myView->Redraw();
            pathTracingFast = timer.elapsed() <= kPathTracingProbeThresholdMs;
        } catch (const Standard_Failure&) {
            pathTracingFast = false;
        }
    }
    if (pathTracingFast) return RenderTier::PathTracing;

    // Tier 1: GPU ray tracing with shadows, timed against a single redraw -
    // the brief's own method, not an average over several frames (a warm-up
    // redraw would hide exactly the shader-compilation cost a slow GPU also
    // pays on every later one). Wrapped in try/catch per this task's own
    // ruling: a driver that cannot do this is expected to REFUSE cleanly
    // rather than take the app down, and OCCT reports that refusal as a
    // Standard_Failure here rather than a bool return.
    bool rayTracingFast = false;
    try {
        applyRenderTier(RenderTier::RayTracing);
        QElapsedTimer timer;
        timer.start();
        myView->Redraw();
        rayTracingFast = timer.elapsed() <= kRenderTierProbeThresholdMs;
    } catch (const Standard_Failure&) {
        rayTracingFast = false;
    }
    if (rayTracingFast) return RenderTier::RayTracing;

    // Neither ray-traced tier held up - rasterization from here down. Tier
    // 2: a shadow-mapped directional light, accepted only once a real
    // Dump() shows a pixel actually moved.
    applyRenderTier(RenderTier::Shadows);
    if (probeShadowPixelsDiffer()) return RenderTier::Shadows;

    // Nothing held up - stand plain, and undo the shadow flag
    // probeShadowPixelsDiffer() may have left set.
    applyRenderTier(RenderTier::Plain);
    return RenderTier::Plain;
}

void OcctViewWidget::setRenderMode(bool on)
{
    // Never on the compare pane - it is read-only furniture from a saved
    // version, not a scene anyone renders a shot of, and this widget's own
    // header says so. A no-op when already in the requested state, so a
    // caller need not guard the call itself.
    if (myViewerOnly || on == myRenderModeActive) return;
    initializeViewer();
    if (myContext.IsNull() || myView.IsNull()) return;

    myRenderModeActive = on;

    if (on) {
        // Every Graphic3d_RenderingParams field this class is about to
        // touch, captured from the live view BEFORE any of them change - see
        // saveRenderParams()'s own comment. First thing in this branch,
        // deliberately: everything below (the floor, the tier probe/apply,
        // the PBR materials) writes into the same live params.
        saveRenderParams();

        // Suppress hover and selection highlight - a REAL ClearSelected(),
        // reusing clearSelection() rather than a second copy of its body, so
        // the edge-length dimension it clears and the selectionChanged() it
        // emits stay the one implementation. Every solid's own selection
        // modes then come OUT of the context's pick candidates -
        // Deactivate() is what actually suppresses both a future hover
        // highlight and a future pick, not merely today's selection; a
        // plain LEFT press exits render mode instead of picking anything
        // (see mousePressEvent()), and reactivating them on the way out
        // below is what makes picking work again once it does.
        clearSelection();
        for (auto& entry : mySolids) myContext->Deactivate(entry.second);

        // The mirror-placement gesture (Milestone 4, Phase 3), on the same
        // terms as every other live gizmo CLAUDE.md's render-mode section
        // names: "the viewport is the furniture alone" reaches it too, and
        // unlike the symmetry indicator a few lines down this is a genuinely
        // MODAL gesture with nothing to resume - canBeginMirrorPlacement()
        // already refuses to begin one while render mode is on, so ending an
        // active one here keeps entry and begin symmetric rather than
        // leaving a plane and a chip alive over a scene that is supposed to
        // be the furniture alone.
        cancelMirrorPlacement();

        myGridRenderer.setVisible(false);
        // The symmetry plane indicator, on the same terms as the grid - it
        // may already be up (symmetry was on before render mode was
        // entered), and updateSymmetryIndicator()'s own new guard only stops
        // it being REBUILT while active, not the presentation already on
        // screen. Erased outright rather than merely marked, because Erase
        // is what a Dump actually stops drawing; mySymmetryIndicatorOn
        // itself is untouched, so it is still the one source of truth
        // updateSymmetryIndicator() reads once render mode lets it run again.
        if (!mySymmetryIndicator.IsNull()) myContext->Erase(mySymmetryIndicator, Standard_False);

        // A render is never a wireframe, and it wears no edge ink either.
        // The bodies are forced shaded for the duration AND their face
        // boundary lines - the GRAY30 edges displaySolid() draws so shape
        // edges stay readable while modeling - are switched off, which is
        // what the user actually noticed as "still seeing the wireframe"
        // on an already-shaded body. myWireframe itself is untouched, and
        // the exit path below re-applies whatever it says - so the user's
        // toggle survives a round trip through render mode exactly as they
        // left it. Redisplay is what makes a drawer change take effect; it
        // recomputes an erased (hidden) body's presentation without showing
        // it, so the visibility toggles are respected for free.
        for (auto& entry : mySolids) {
            entry.second->Attributes()->SetFaceBoundaryDraw(Standard_False);
            if (myWireframe)
                myContext->SetDisplayMode(entry.second, AIS_Shaded, Standard_False);
            myContext->Redisplay(entry.second, Standard_False);
        }
        // Body materials are no longer set here directly - fix round 2
        // scoped them per tier (applyRenderBodyMaterials() for PathTracing/
        // RayTracing, clearRenderBodyMaterials() for Shadows/Plain), and
        // applyRenderTier() (called below by the probe/apply sequence) is
        // the one place that now decides which. Before the first probe has
        // even run, bodies simply keep whatever displaySolid() already gave
        // them - correct, since Plain/Shadows is the ShadingModel = Phong
        // default this enum's own zero-cost value maps to.

        // The studio key light: every directional light is angled off the
        // vertical so the shadow falls BESIDE the furniture - the default
        // straight-down light hides the entire shadow underneath the body
        // it belongs to, which on a floor reads as no shadow at all. Saved
        // first and restored on exit, because the modeling look outside
        // render mode is not this feature's to change.
        myRenderSavedLights.clear();
        if (!myViewer.IsNull()) {
            for (const Handle(Graphic3d_CLight)& light : myViewer->ActiveLights()) {
                if (light->Type() != Graphic3d_TypeOfLightSource_Directional) continue;
                myRenderSavedLights.push_back(
                    {light, light->Direction(), light->Intensity(), light->IsHeadlight()});
                // World-space, or the studio direction is silently read in
                // VIEW space and the "key light" follows the camera - the
                // first calibration round's top face stayed dark through a
                // doubled intensity precisely because of this flag.
                light->SetHeadlight(false);
            }
            // Direction and intensity themselves now go through the SAME
            // live application Task 7.2's setRenderLightAngleDeg()/
            // setRenderLightStrength() use - the studio key is "the Light
            // angle/Light strength controls" now, not a second copy of it,
            // so a session that never opens the render settings card still
            // gets exactly the old calibrated look: myRenderLightAngleDeg's
            // own default reproduces gp_Dir(-0.45, 0.35, -0.82) exactly (see
            // the constructor), and myRenderLightStrength defaults to 2.0,
            // the old hardcoded doubling.
            applyRenderLightAngleAndStrength();
        }

        // Before the tier probe, deliberately: the floor is the surface the
        // shadow lands on, so the tier-2 pixel probe measures the scene the
        // user will actually be shown - with no floor a straight-down shadow
        // could touch no pixel and the probe would fall to Plain on hardware
        // that shadow-maps fine.
        showRenderFloor();

        // Cache the tier for the session - the brief's own words. The first
        // activation pays for the probe (a timed redraw, and possibly two
        // full Dump()s); every later one just reapplies what was already
        // found.
        if (!myRenderTierProbed) {
            myRenderTier = probeRenderTier();
            myRenderTierProbed = true;
        } else {
            applyRenderTier(myRenderTier);
        }

        // The progressive-refine loop only means anything for the
        // PathTracing tier - every other tier's Method is a fixed-cost
        // rasterization or single-pass ray trace that Redraw() already
        // renders in full each time, so asking Qt to repaint on a timer for
        // one of those would just burn a GPU for no visible gain.
        if (myRenderTier == RenderTier::PathTracing)
            startPathTracingConvergence();
        else
            stopPathTracingConvergence();
    } else {
        stopPathTracingConvergence();
        for (auto& entry : mySolids) applySelectionMode(entry.second);
        hideRenderFloor();
        // The two restorations that mirror the entry edits above: the lights
        // back to the directions they stood at, the bodies back to whatever
        // the wireframe toggle says - which may have been flipped WHILE
        // render mode was up (setWireframe() records but does not repaint
        // then), so this is applied unconditionally rather than only when
        // entry forced a change.
        for (auto& saved : myRenderSavedLights) {
            saved.light->SetHeadlight(saved.headlight);
            saved.light->SetDirection(saved.direction);
            saved.light->SetIntensity(saved.intensity);
        }
        myRenderSavedLights.clear();
        const Standard_Integer mode = myWireframe ? AIS_WireFrame : AIS_Shaded;
        for (auto& entry : mySolids) {
            entry.second->Attributes()->SetFaceBoundaryDraw(Standard_True);
            // The render-mode PBR material off, back to whatever stood
            // before applyRenderBodyMaterials() ran - see that function's
            // own comment on why UnsetMaterial() is a complete restore here.
            entry.second->UnsetMaterial();
            myContext->SetDisplayMode(entry.second, mode, Standard_False);
            myContext->Redisplay(entry.second, Standard_False);
        }
        myGridRenderer.setVisible(true);
        // Restored from the one piece of state that says whether it should
        // be up at all (mySymmetryIndicatorOn) - derived, not a remembered
        // "it was showing" flag. Forcing the half-span guard to miss is what
        // makes updateSymmetryIndicator() actually rebuild and redisplay
        // rather than trust a cached size that may itself be stale after
        // however long render mode was up.
        mySymmetryIndicatorBuiltHalfSpan = 0.0;
        updateSymmetryIndicator();
        // Ordinary modeling never ray-traces, shadow-maps or shades PBR -
        // all three would be an interactivity hazard or a look mid-edit was
        // never meant to have. restoreRenderParams() puts every touched
        // Graphic3d_RenderingParams field back to what saveRenderParams()
        // read at entry, rather than the bare applyRenderTier(Plain) this
        // used to be - see restoreRenderParams()'s own comment on why that
        // used to be silently incomplete once PathTracing existed.
        restoreRenderParams();
    }

    // The camera's live FOV, on both edges - myRenderModeActive already
    // reads as the NEW state above, so effectiveFovyDeg() now answers
    // correctly (the render setting on entry, kFovyDeg on exit) and this is
    // what actually PUSHES it into the OCCT camera immediately. Without
    // this, exiting render mode left the camera's own Graphic3d_Camera
    // sitting at whatever FOV the settings card was last dragged to until
    // some UNRELATED camera move (an orbit, a Fit All) happened to call
    // applyCameraState() again - measured: cameraViewHeightAtTarget()
    // stayed at the render-mode value straight through an exit with no
    // camera move in between.
    applyCameraState();

    applyBackgroundForMode();
    myContext->UpdateCurrentViewer();
    myView->Redraw();
}

void OcctViewWidget::setWireframe(bool wireframe)
{
    if (myWireframe == wireframe) return;
    myWireframe = wireframe;

    if (myContext.IsNull()) return;   // state kept; re-applied once the viewer initialises
    // Render mode forces the bodies shaded regardless of this toggle - the
    // flag is recorded (just above) and setRenderMode(false) applies it
    // unconditionally on the way out, so the flip is honoured the moment
    // it can be seen rather than tearing a wireframe through a studio shot.
    if (myRenderModeActive) return;

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
    // Render mode's own exit gesture - "a pick press in the viewport" in
    // CLAUDE.md's words. Checked FIRST and unconditionally for a LEFT press:
    // every gizmo this widget could otherwise grab is already cleared or
    // detached while render mode is active (see setRenderMode()), so there
    // is nothing here for the rest of this function to do differently - the
    // press is swallowed outright rather than falling through to an ordinary
    // pick, so the click that exits render mode never also selects whatever
    // happens to be underneath it. RMB orbit and MMB pan are NOT gated here:
    // the brief is explicit that framing a shot does not exit.
    if (myRenderModeActive && event->button() == Qt::LeftButton) {
        emit renderModeExitRequested();
        return;
    }

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
    // Ctrl in FACE mode is the lock gesture and is not a grab, scoped exactly as
    // mouseDoubleClickEvent() scopes the same exemption. Without it the lock's
    // first press armed a pull drag on the way past: the release ended a drag
    // that had moved nothing, which fell through to an ordinary pick and
    // deselected the very face the gesture was aimed at. The exemption names
    // face mode rather than the modifier alone, so the bevel arrow one branch
    // down - where Ctrl means nothing - keeps its guard whole.
    const bool lockGesture = (event->modifiers() & Qt::ControlModifier) &&
                             mySelectionMode == SelectionMode::Face;
    if (event->button() == Qt::LeftButton && !mySketchMode && !lockGesture &&
        arrowHit(myPullArrow, myLastPos)) {
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

    // The mirror-placement handle, on the same terms as the two arrows below
    // it - including claiming the gesture at an angle the maths refuses.
    // Checked ahead of them rather than after: a mirror gesture requires body
    // selection mode, which the pull and bevel arrows are never shown in
    // (face mode, edge mode respectively), so the ordering is not
    // load-bearing either, but this keeps the three "grab a screen-space
    // handle" branches together.
    if (event->button() == Qt::LeftButton && !mySketchMode && myMirrorPlacement.active &&
        mirrorHandleHit(myLastPos)) {
        beginAxisDrag(myMirrorDrag, mirrorPlacementAxisLine(), myLastPos);
        myMirrorDragOffsetStart = myMirrorPlacement.offset;
        return;
    }

    // The bevel arrow, on exactly the same terms - including claiming the
    // gesture at an angle the maths refuses. The two arrows are never up at
    // once (face mode against edge mode), so the order of these two blocks is
    // not load-bearing.
    //
    // Shift is excluded, and that exclusion is the arrow's half of multi-edge
    // selection. arrowHit() is a 14 px SCREEN-SPACE test, so the arrow does
    // not compete for the pick the way AIS_ManipulatorOwner does - it does
    // something worse: it silently swallows the press before the picker ever
    // sees it. The arrow stands on the last edge picked and a second edge is
    // usually right beside it, so without this a Shift-click meant to
    // accumulate would start a drag on the edge already chosen instead. Same
    // hazard the transform gizmo's Deactivate() closes below, one layer up.
    const bool additivePress = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress &&
        arrowHit(myBevelArrow, myLastPos)) {
        beginAxisDrag(myBevelDrag, myBevelArrow.axis(), myLastPos);
        return;
    }

    // The transform gizmo owns LEFT drags that start on one of its parts, and
    // only those. RMB orbit and MMB pan returned above; a Shift-click is the
    // "add this body to the selection" gesture and must reach the picker even
    // when it lands on an arm of the gizmo standing on the first body.
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress &&
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

    // No picking in viewer-only mode - see the header. Orbit and pan are
    // both handled above (unconditionally, since neither depends on the
    // picker), so a left click in the compare pane simply does nothing
    // rather than selecting whatever is under it. None of the drag flags
    // below can be true here either: nothing ever calls showPullArrow(),
    // showBevelArrow() or attachManipulator() on a viewer-only widget, so
    // mousePressEvent() never arms one in the first place.
    if (myViewerOnly) return;

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

    // The end of a mirror-placement drag, swallowed for the same reason: the
    // press was aimed at the handle, and re-picking here would change the
    // body selection that has nothing to do with this gesture (the ids it
    // pairs were captured at beginMirrorPlacement() and never re-read).
    if (myMirrorDrag.active && event->button() == Qt::LeftButton) {
        myMirrorDrag.active = false;
        emit mirrorPlaneReleased(myMirrorDrag.moved);
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
        // Shift here means "snap this segment to the nearest of the 8
        // compass directions from its start" (Milestone 4, Task 6.1), not
        // the additive-selection Shift below: nothing is selectable while
        // sketching, so the two can never be asked for at once.
        const bool straight = (event->modifiers() & Qt::ShiftModifier) != 0;
        gp_Pnt hit;
        if (pointOnSketchPlane(pos.x(), pos.y(), hit, straight)) emit sketchPointPicked(hit);
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
    // Read BEFORE SelectDetected, because a Replace clears the detection's
    // relationship to the selection and an XOR may have just removed it.
    TopoDS_Edge justPicked;
    if (!onGizmo && myContext->HasDetectedShape() &&
        myContext->DetectedShape().ShapeType() == TopAbs_EDGE) {
        justPicked = TopoDS::Edge(myContext->DetectedShape());
    }
    if (!onGizmo) {
        myContext->SelectDetected(additive ? AIS_SelectionScheme_XOR
                                           : AIS_SelectionScheme_Replace);
        // "The edge you picked last" - remembered here because it cannot be
        // read back out of the selection afterwards (see lastSelectedEdge()).
        // A click on nothing clears it, so the arrow cannot linger on an edge
        // the user has just dropped.
        myLastPickedEdge = justPicked;
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
    } else if (myMirrorDrag.active) {
        if (advanceAxisDrag(myMirrorDrag, mirrorPlacementAxisLine(), pos)) {
            // ABSOLUTE offset, not the raw delta advanceAxisDrag() returns -
            // see mirrorPlaneDragged()'s own comment on the header for why a
            // mirror-placement drag is not a fresh-each-gesture distance the
            // way a pull or a bevel is.
            myMirrorPlacement.offset = myMirrorDragOffsetStart + myMirrorDrag.value;
            updateMirrorPlacementIndicator();
            emit mirrorPlaneDragged(myMirrorPlacement.offset);
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
        // The SAME straight-continuation rule the click uses, from the same
        // function, so the marker cannot promise one point and the click
        // place another.
        const bool straight = (event->modifiers() & Qt::ShiftModifier) != 0;
        gp_Pnt onPlane;
        if (pointOnSketchPlane(pos.x(), pos.y(), onPlane, straight)) {
            myLastHoverPoint = onPlane;
            myHasLastHoverPoint = true;
            emit sketchCursorMoved(onPlane);
        }
    } else if (!myViewerOnly && !myContext.IsNull()) {
        // Hover highlight. Suppressed while sketching so the in-progress wire
        // does not fight the highlighter for attention. Suppressed
        // altogether in viewer-only mode - see the header: no picking means
        // no hover highlight either.
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
    if (event->button() != Qt::LeftButton || mySketchMode || myContext.IsNull() || myViewerOnly)
        return;

    const QPoint pos = event->position().toPoint();
    const bool onArrow = arrowHit(myPullArrow, pos) || arrowHit(myBevelArrow, pos);

    // A second click on either arrow belongs to the arrow, not to whatever the
    // double-click would otherwise mean - the same rule every control over the
    // viewport follows. It matters most for the two gestures that would move
    // something the user is not looking at: framing the body, and selecting the
    // whole body out from under a face or an edge they are mid-drag on.
    //
    // ONE gesture is exempt, and only one: Ctrl+double-click in FACE mode, the
    // lock. An arrow's tail sits at the centre of the face it belongs to, which
    // is exactly where a user aims when they want to draw on that face - so
    // with no exemption the lock was unreachable at the most obvious pixel on
    // its own target, and reaching it meant aiming at a corner. That gesture is
    // also the one thing on this arrow that no sequence of drags can produce by
    // accident, which is what the guard is protecting against.
    //
    // The exemption is deliberately NOT "Ctrl held": it was written that way
    // first, and it let Ctrl+double-click in EDGE mode fall through to the
    // whole-body route below, which switches the selection mode out from under
    // a live bevel arrow - precisely the case the guard exists for, reachable
    // by holding a key the edge-mode gesture does not even use. The exemption
    // names the branch it exists for, so nothing else can inherit it.
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0;
    const bool lockGesture = ctrlHeld && mySelectionMode == SelectionMode::Face;
    if (onArrow && !lockGesture) return;
    const QPoint device = toDevicePixels(pos);
    myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
    if (!myContext->HasDetected()) return;
    // A second click on a gizmo handle is another grab, not a request to frame
    // the body underneath it - the same rule the pull arrow keeps above.
    if (detectedIsManipulator()) return;

    // CTRL+double-click in face mode means "sketch on this" - the second route
    // to Lock to Face, alongside the action and its L shortcut. It carried no
    // modifier until this phase, and it had to give the plain gesture up: a
    // user working on a face or an edge who wants the whole body back reaches
    // for a double-click first, and locking the sketch plane instead is a mode
    // change they did not ask for. Framing the body would be no better - it is
    // the one gesture that takes the camera away from the face just chosen -
    // which is why locking keeps the gesture and only gains the modifier.
    //
    // The refusal for a non-planar face, and the pending-outline guard, both
    // live in MainWindow, which owns the toast, not here.
    if (lockGesture && myContext->HasDetectedShape() &&
        myContext->DetectedShape().ShapeType() == TopAbs_FACE) {
        // Select it too, so the actions agree with what was just locked.
        myContext->SelectDetected(AIS_SelectionScheme_Replace);
        myView->Redraw();
        emit selectionChanged();
        emit faceDoubleClicked(TopoDS::Face(myContext->DetectedShape()));
        return;
    }

    // Past the one exempt branch, an arrow hit is an arrow hit again. A
    // Ctrl+double-click that got this far is one whose detection was not a
    // face after all - a body, an edge, the ground - and there is no reason
    // the modifier should buy it the whole-body route the guard would refuse
    // to an unmodified click on the same pixel.
    if (onArrow) return;

    const Handle(AIS_InteractiveObject) hit = myContext->DetectedInteractive();

    // A PLAIN double-click on a body while picking its parts means "give me
    // the whole thing" - one gesture that both changes the selection mode and
    // selects the body, so a user who drilled into faces or edges gets back
    // out without going to the rail for it.
    //
    // Announced rather than performed: the selection MODE is a QAction's
    // checked state and updateActions() is the single place that decides what
    // is available, so a viewport that switched its own mode would leave the
    // rail chip, the menu entry and the status label all describing the mode
    // the user just left. MainWindow answers this by triggering the same
    // action a click on the chip does.
    if (mySelectionMode != SelectionMode::Solid) {
        for (const auto& entry : mySolids) {
            if (entry.second.get() != hit.get()) continue;
            emit bodyDoubleClicked(entry.first);
            return;
        }
        return;   // nothing of ours under the cursor
    }

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
