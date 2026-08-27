#include "OcctViewWidget.h"

#include "ModelingOps.h"
#include "SketchController.h"

// OCCT before Qt, for the Handle() macro clash.
#include <AIS_DisplayMode.hxx>
#include <AIS_SelectionScheme.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GridDrawMode.hxx>
#include <Aspect_GridType.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_NameOfColor.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_TypeOfVisualization.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>

#ifdef _WIN32
  #include <WNT_Window.hxx>
#else
  #include <Xw_Window.hxx>
#endif

#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

namespace {
// AIS_Shape selection modes are plain integers: 0 whole shape, 4 face.
constexpr int kSelectionModeWholeShape = 0;
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

    myView->SetBackgroundColor(Quantity_Color(Quantity_NOC_GRAY30));
    myView->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_Color(Quantity_NOC_WHITE),
                            0.08, V3d_ZBUFFER);

    // Visible grid on the XY plane (Milestone 1 acceptance criterion).
    myViewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
    myViewer->SetRectangularGridValues(0.0, 0.0, 10.0, 10.0, 0.0);
    myViewer->SetRectangularGridGraphicValues(500.0, 500.0, 0.0);

    setViewAxonometric();
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
    myContext->Display(presentation, AIS_Shaded, kSelectionModeWholeShape, Standard_False);
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

void OcctViewWidget::applySelectionMode(const Handle(AIS_Shape)& shape)
{
    if (myContext.IsNull() || shape.IsNull()) return;

    myContext->Deactivate(shape);
    myContext->Activate(shape, mySelectionMode == SelectionMode::Face ? kSelectionModeFace
                                                                     : kSelectionModeWholeShape);
}

void OcctViewWidget::setSelectionMode(SelectionMode mode)
{
    if (mode == mySelectionMode) return;

    mySelectionMode = mode;
    if (myContext.IsNull()) return;

    myContext->ClearSelected(Standard_False);
    for (auto& entry : mySolids) applySelectionMode(entry.second);
    myContext->UpdateCurrentViewer();
    emit selectionChanged();
}

void OcctViewWidget::setSketchMode(bool enabled, const gp_Pln& plane)
{
    mySketchMode = enabled;
    mySketchPlane = plane;
    if (enabled) clearSelection();
}

bool OcctViewWidget::pointOnSketchPlane(int px, int py, gp_Pnt& out) const
{
    if (myView.IsNull()) return false;

    Standard_Real x = 0.0, y = 0.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0;
    myView->ConvertWithProj(px, py, x, y, z, vx, vy, vz);

    const gp_Lin ray(gp_Pnt(x, y, z), gp_Dir(vx, vy, vz));
    return SketchController::intersectRayWithPlane(ray, mySketchPlane, out);
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

void OcctViewWidget::fitAll()
{
    if (myView.IsNull()) return;

    myView->FitAll();
    myView->ZFitAll();
    myView->Redraw();
}

void OcctViewWidget::setViewAxonometric()
{
    if (myView.IsNull()) return;

    myView->SetProj(V3d_XposYnegZpos);
    myView->Redraw();
}

void OcctViewWidget::mousePressEvent(QMouseEvent* event)
{
    initializeViewer();
    myLastPos = event->position().toPoint();

    if (event->button() == Qt::RightButton) {
        myRotating = true;
        if (!myView.IsNull()) myView->StartRotation(myLastPos.x(), myLastPos.y());
    } else if (event->button() == Qt::MiddleButton) {
        myPanning = true;
    }
}

void OcctViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)       myRotating = false;
    else if (event->button() == Qt::MiddleButton) myPanning = false;

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

    if (myRotating) {
        myView->Rotation(pos.x(), pos.y());
    } else if (myPanning) {
        myView->Pan(pos.x() - myLastPos.x(), -(pos.y() - myLastPos.y()));
    } else if (!myContext.IsNull() && !mySketchMode) {
        // Hover highlight. Suppressed while sketching so the in-progress wire
        // does not fight the highlighter for attention.
        myContext->MoveTo(pos.x(), pos.y(), myView, Standard_True);
    }

    myLastPos = pos;
}

void OcctViewWidget::wheelEvent(QWheelEvent* event)
{
    if (myView.IsNull()) return;

    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    const QPoint pos = event->position().toPoint();
    const int step = delta > 0 ? 40 : -40;

    myView->StartZoomAtPoint(pos.x(), pos.y());
    myView->ZoomAtPoint(pos.x(), pos.y(), pos.x() + step, pos.y());
    myView->Redraw();
}
