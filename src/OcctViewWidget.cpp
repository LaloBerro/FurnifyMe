// windows.h FIRST, deliberately - the one exception to "OCCT headers before
// windows.h", and for the same reason gui_smoke.cpp takes it (see that
// file's own top-of-file comment): OCCT's own Standard_Macro.hxx includes
// windows.h itself, but with NOUSER defined first, which excludes the
// entire User32 window-management API. attachGlWindow() below needs
// WindowFromDC from that API (and wglGetCurrentDC from wingdi.h beside it),
// and windows.h's include guard means a second, unrestricted #include after
// OCCT's own restricted one is a silent no-op - the only way to get the real
// declarations is to be the FIRST includer. Handle() (OCCT's macro that
// collides with some Windows headers) does not exist yet at this point in
// the file, so there is nothing for windows.h to collide with here.
//
// It used to be SetWindowPos that needed this, for the Milestone-3 stale-HWND
// safety net in resizeEvent(). That whole hazard is gone with the native
// window it was about; what is left is the one question OpenGl_Window has to
// be able to answer - which window does the GL context Qt handed us actually
// belong to.
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
#include <BRepPrimAPI_MakeBox.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_BSDF.hxx>
#include <Graphic3d_PBRMaterial.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Texture2D.hxx>
#include <Graphic3d_TextureParams.hxx>
#include <Image_PixMap.hxx>
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
#include <Message.hxx>
#include <NCollection_HArray1.hxx>
// The GL-hosting half of the bridge. These pull in OCCT's own OpenGL enum and
// entry-point declarations, which is why every Qt header in this file stays
// below them - Qt's <qopengl.h> declares the same family, and only the first
// one seen may define the types.
#include <OpenGl_ArbFBO.hxx>
#include <OpenGl_Caps.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GlCore20.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_NameOfColor.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SortCriterion.hxx>
#include <StdSelect_BRepOwner.hxx>
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

#include <QDir>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QSet>
#include <QTemporaryFile>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
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

// The one place "which render-mode tiers drive OCCT's ray-tracing Method"
// is written down - a second, hand-written copy of this condition is
// exactly how two call sites quietly drift apart.
bool isRayTracedTier(OcctViewWidget::RenderTier tier)
{
    return tier == OcctViewWidget::RenderTier::PathTracing ||
           tier == OcctViewWidget::RenderTier::RayTracing;
}

// And the one place "which tier gets PBR shading, filmic tone mapping and
// the PBR material set" is written down. Fix round 2 scoped that to
// isRayTracedTier(); the user-feedback round narrowed it to PathTracing
// alone, for a measured reason rather than a tidier reading of the brief.
// Whitted ray tracing does not tone-map (OCCT's own header says
// ToneMappingMethod is for path tracing) and has no indirect bounce to fill
// a shadow, so a PBR floor under it rendered its cast shadow at 0.32 of the
// lit floor - the near-black the user rejected. Handed the SAME Phong
// shading model and the SAME Milestone-3-calibrated floor and body
// materials the Shadows tier has always used, the identical scene measured
// a blended floor and a 0.86 cast shadow: nothing like the reference's
// 0.70, but a real, readable shadow rather than a hole. So PBR is path
// tracing's, and every other tier is Phong.
bool usesPbrMaterials(OcctViewWidget::RenderTier tier)
{
    return tier == OcctViewWidget::RenderTier::PathTracing;
}

// How many accumulation passes a MEASURING probe lets the path tracer take
// before it dumps. Every probe that reports a NUMBER shares this, because
// an under-converged path-traced frame is not merely noisy, it is
// systematically DARK - the accumulation buffer is a running mean and the
// early samples that have not arrived yet read as zero. The first shipped
// value here was 5, and it cost a real, wrong conclusion: the same scene
// that measures a 194 floor against a 194 backdrop after this many passes
// measured 172 after five, which read as a failing seam. 24 is where the
// running mean has settled - a user at rest gets more than three times as
// many passes (kPathTracingConvergeMs), but those buy less VARIANCE, not a
// different level, so a probe that only reads levels does not have to pay
// for them in every measurement it takes.
// probePathTracingChangedImage() deliberately keeps its own smaller count -
// it asks whether two frames DIFFER, not what either one reads.
constexpr int kMeasurementSettlePasses = 24;

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
    // 1.2 is the indicator's own historical width; the Magnet guide reuses
    // this class bolder, because a guide that flashes for half a drag has to
    // read at a glance.
    double width = 1.2;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation, const Standard_Integer) override
    {
        if (segments.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect = new Graphic3d_AspectLine3d(
            colour, Aspect_TOL_SOLID, static_cast<Standard_ShortReal>(width));
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(segments);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // Never pickable - an indicator, not a body.
    }
};

// How close the RAW Move-drag value has to be to an alignment before Magnet
// takes it, in LOGICAL screen pixels - a reach the hand feels the same at
// every zoom, converted through worldPerPixel() at the moment of the
// comparison. Eight matches Auto's edge-hover promise: the two are the same
// kind of forgiveness.
constexpr double kMagnetSnapPx = 8.0;

// The wood look (Milestone 5): one mid-tone the PBR/BSDF albedo and the
// Phong diffuse both derive from, so the two pipelines disagree about
// shading, never about what colour wood is. The texture's own palette
// brackets it either side.
const Quantity_Color kWoodTone(0.55, 0.38, 0.23, Quantity_TOC_sRGB);
// What sits UNDER the grain texture: near-white, because the modulate
// pipeline MULTIPLIES the texture by this - the path tracer proved it by
// red-shifting the user's oak through kWoodTone (their own two-tier
// screenshot comparison). The texture carries the colour; this only keeps
// a hair of warmth from clipping.
const Quantity_Color kWoodUnderTexture(0.95, 0.94, 0.92, Quantity_TOC_sRGB);

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

// The framebuffer OCCT draws this app's 3D frame into, which since the
// QOpenGLWidget migration is Qt's own - QOpenGLWidget renders into an FBO and
// composites it with the rest of the widget tree, so there is no window
// backbuffer for OCCT to own any more.
//
// The subclass exists for one reason, and it is a colour-correctness reason
// rather than a plumbing one: Qt's FBO carries a GL_RGBA8 colour attachment,
// not GL_SRGB8_ALPHA8, so OCCT must be told to leave GL_FRAMEBUFFER_SRGB alone
// and apply the sRGB transfer itself. Without that flag every colour this app
// measures out of a V3d_View::Dump - and this suite measures a great many -
// would come back through the wrong curve. It is OCCT's own answer to its own
// question (occt-samples-qopenglwidget's OcctQtFrameBuffer), copied rather
// than invented.
class HostFrameBuffer : public OpenGl_FrameBuffer {
    DEFINE_STANDARD_RTTI_INLINE(HostFrameBuffer, OpenGl_FrameBuffer)
public:
    HostFrameBuffer() = default;

    void BindBuffer(const Handle(OpenGl_Context)& context) override
    {
        OpenGl_FrameBuffer::BindBuffer(context);
        context->SetFrameBufferSRGB(true, false);
    }

    void BindDrawBuffer(const Handle(OpenGl_Context)& context) override
    {
        OpenGl_FrameBuffer::BindDrawBuffer(context);
        context->SetFrameBufferSRGB(true, false);
    }

    void BindReadBuffer(const Handle(OpenGl_Context)& context) override
    {
        OpenGl_FrameBuffer::BindReadBuffer(context);
    }
};

// The OCCT-side GL context this view is rendering through, or a null handle
// before one exists. Reached the only way OCCT exposes it - down through the
// OpenGl_View and its OpenGl_Window - because everything the hosting layer has
// to do to a frame (wrap Qt's FBO, put GL state back the way Qt left it) is
// addressed to that context and not to the V3d_View.
Handle(OpenGl_Context) hostGlContext(const Handle(V3d_View)& view)
{
    if (view.IsNull()) return Handle(OpenGl_Context)();
    Handle(OpenGl_View) glView = Handle(OpenGl_View)::DownCast(view->View());
    if (glView.IsNull() || glView->GlWindow().IsNull()) return Handle(OpenGl_Context)();
    return glView->GlWindow()->GetGlContext();
}

// The native window the CURRENTLY BOUND GL context belongs to.
//
// Not this widget's own: a QOpenGLWidget deliberately has no native window,
// and asking for one through winId() would re-create exactly the native
// surface this migration exists to remove. OpenGl_Window::Init does
// GetDC(NativeHandle()) and hands the result to wglMakeCurrent alongside the
// rendering context it was given, so the handle has to be one whose device
// context is COMPATIBLE with that rendering context - which is precisely the
// window the context is already current on. OCCT's own sample answers it the
// same way (OcctGlTools::GetGlNativeWindow).
// The monotonic sequence CRITICAL 1's ordering is asserted against - see the
// tick accessors on the header. File-scope rather than per-widget because the
// thing being proved is an ORDER between two objects' lifetimes: this widget's
// teardown, and the death of the context it was rendering through.
long long ourGlTeardownTick = 0;
long long ourLastGlReleaseTick = 0;
long long ourLastGlContextDeathTick = 0;
int ourGlReleaseCount = 0;

Aspect_Drawable currentGlNativeWindow()
{
#ifdef _WIN32
    return reinterpret_cast<Aspect_Drawable>(WindowFromDC(wglGetCurrentDC()));
#else
    return 0;
#endif
}
}  // namespace

QSurfaceFormat OcctViewWidget::surfaceFormat()
{
    // FINALIZED IN PHASE 3 OF THE QOPENGLWIDGET MIGRATION, against measured
    // tier outcomes rather than against what the request looks like it should
    // buy. Each of the three lines below is a decision with a measurement
    // behind it; the two after them are decisions NOT to ask for something,
    // each with a measurement of its own (the second added by Milestone 5's
    // modeling-lag investigation).
    QSurfaceFormat format;
    // OCCT's 3D view wants both, and neither is guaranteed by Qt's default. A
    // request is not a grant, so gui_smoke reads the two back off the LIVE
    // context rather than off this object: a viewport with no depth buffer
    // renders a plausible scene with the wrong faces in front, silently.
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    // Compatibility rather than core. Phase 1 chose it on OCCT's historical
    // preference and said so; Phase 3 kept it on a measurement - on this
    // machine the tier probe reaches PathTracing through a compatibility
    // context, timing one GPU-synchronized path-traced redraw at 3 ms against
    // a 1500 ms threshold, and every calibrated pixel in the render block
    // (floor blend, both shadow ratios, the converged export) reads exactly
    // what it read through the pre-migration native window. There is nothing
    // here for a core profile to win back. initializeViewer() feeds the same
    // choice through to OpenGl_Caps::contextCompatible, so the driver and the
    // surface cannot disagree about which profile is live.
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    // AND DELIBERATELY NO setSamples(). Multisampling on the DEFAULT
    // framebuffer is the one attribute that would actively break this
    // hosting: Qt would hand the widget a multisampled FBO, OCCT would wrap
    // it as its default framebuffer, and every pixel this project treats as
    // ground truth is read back out of that buffer - V3d_View::Dump, the tier
    // probe's own shadow test, the floor blend, the shadow ratios. A
    // multisample colour attachment cannot be read without a resolve step
    // nothing here performs. Antialiasing is OCCT's to do inside the scene
    // (Graphic3d_RenderingParams::IsAntialiasingEnabled, and the ray-traced
    // tiers' own sampling), where it costs the measurements nothing.
    //
    // AND NO setSwapInterval() - a SECOND decision not to ask for something,
    // added by Milestone 5's modeling-lag investigation, which suspected the
    // present. The default of 1 paces presentation at the display's refresh.
    // Forcing 0 left the laggy orbit at 20.7 ms/frame against 20.9 with vsync
    // on - byte for byte the same cost - while the healthy path moved from
    // 4.1 ms (one 240 Hz refresh) to 0.6 ms. So the lag was never in the
    // present at all; see IconSet::appMarkPixmap() for where it actually was.
    // Tearing is not worth buying nothing.
    return format;
}

OcctViewWidget::OcctViewWidget(QWidget* parent, bool viewerOnly)
    : QOpenGLWidget(parent)
    , mySketchPlane(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0))
    , myViewerOnly(viewerOnly)
{
    // Belt to main.cpp's braces: the application default format is what
    // actually decides the context (it is set before QApplication exists, which
    // is the only moment that can), and this makes the widget carry the same
    // answer so a third entry point cannot silently produce a viewport with no
    // depth buffer.
    setFormat(surfaceFormat());
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

OcctViewWidget::~OcctViewWidget()
{
    // NOT `= default`, and the difference is a crash. OCCT handles are
    // refcounted and must never be deleted - but the resources BEHIND them
    // live on the GPU, inside the OpenGL context Qt owns, and member-order
    // destruction would release them with no context current and in no
    // particular order. `~QOpenGLWidget` has not run yet at this point, which
    // is exactly why this has to be here: the context is still alive and
    // makeCurrent() still works. MainWindow does `delete myCompareView` on
    // every compare-pane close, so this is a routine path, not an exit-only
    // one.
    //
    // The context-loss connection goes first. Qt disconnects a QObject's
    // connections in ~QObject, which runs AFTER this body, so leaving it live
    // would let ~QOpenGLWidget's own context destruction call
    // releaseGlResources() on a half-destroyed object.
    if (!myAttachedContext.isNull()) disconnect(myAttachedContext, nullptr, this, nullptr);
    releaseGlResources();
}

long long OcctViewWidget::lastGlReleaseTick() { return ourLastGlReleaseTick; }
long long OcctViewWidget::lastGlContextDeathTick() { return ourLastGlContextDeathTick; }
int OcctViewWidget::glReleaseCount() { return ourGlReleaseCount; }

void OcctViewWidget::releaseGlResources()
{
    if (myViewer.IsNull() && myView.IsNull() && myContext.IsNull()) {
        myAttachedContext.clear();
        return;
    }

    ourLastGlReleaseTick = ++ourGlTeardownTick;
    ++ourGlReleaseCount;

    // The whole point: the resources go while the context that owns them is
    // current. GlScope would do it, but it needs a view with a window and this
    // has to work when the window is exactly what is being taken away.
    const bool took = context() != nullptr && QOpenGLContext::currentContext() != context();
    if (took) makeCurrent();

    // Nothing may be driving a frame while the viewer is torn down.
    stopCameraAnimation();
    stopPathTracingConvergence();

    // Held across the teardown deliberately: OCCT's own sample keeps the
    // display connection alive until another context is made current, to
    // dodge a crash in the X11 case. It costs one refcount on Win32.
    Handle(Aspect_DisplayConnection) display;
    if (!myViewer.IsNull() && !myViewer->Driver().IsNull())
        display = myViewer->Driver()->GetDisplayConnection();

    // The three sub-renderers hold presentations and a context handle of their
    // own; they let go first, before the context they were built against does.
    myGridRenderer.detach();
    myDimension.detach();
    myPullArrow.detach();
    myBevelArrow.detach();
    myMoveGizmo.detach();
    myRotateGizmo.detach();
    myScaleGizmo.detach();

    // OCCT's own order: remove every presentation, drop the context, destroy
    // the view, then the viewer.
    if (!myContext.IsNull()) myContext->RemoveAll(Standard_False);
    myContext.Nullify();
    if (!myView.IsNull()) myView->Remove();
    myView.Nullify();
    myViewer.Nullify();
    myHostWindow.Nullify();
    display.Nullify();

    // Every handle this class held pointed into what has just gone. Cleared
    // rather than left dangling, because on the context-loss path this object
    // keeps living and initializeViewer() will build a fresh viewer that knows
    // nothing about them.
    mySolids.clear();
    myOutlines.clear();
    myPreview.Nullify();
    myModelingPreviews.clear();
    myModelingPreviewSolids.clear();
    myPlacedMarkers.clear();
    myFirstPointMarker.Nullify();
    myCursorMarker.Nullify();
    myWoodTexture.Nullify();
    myMagnetGuide.Nullify();
    myMagnetGuideShown = false;
    mySymmetryIndicator.Nullify();
    mySymmetryIndicatorBuiltHalfSpan = 0.0;
    myMirrorPlacementPlaneObject.Nullify();
    myMirrorPlacementHandleObject.Nullify();
    myRenderFloor.Nullify();
    myRenderSavedLights.clear();
    myRenderSavedAmbients.clear();
    myRenderModeActive = false;
    mySketchLayer = Graphic3d_ZLayerId_UNKNOWN;
    // Pointed into the context that has just gone, same reasoning as every
    // other handle cleared above - a fresh viewer's first MoveTo() must
    // compare against nothing, not a stale owner from the torn-down one.
    myLastHoverOwner.Nullify();
    myInitialized = false;
    myAttachedContext.clear();
    // No view left to accumulate into, so no run to be deep in.
    myAccumulationDepth = 0;

    if (took) doneCurrent();
}

void OcctViewWidget::initializeViewer()
{
    if (myInitialized) return;

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    // FALSE: the driver must not create an OpenGL context of its own. Qt owns
    // the one this widget renders through, and a second context would be a
    // second GPU-resource namespace - the framebuffer attachGlWindow() wraps
    // does not live in it.
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, Standard_False);
    // Qt presents the frame, so OCCT must not swap buffers under it, must not
    // reach for a system backbuffer it does not own, and must treat the alpha
    // channel of the shared FBO as opaque - the three options OCCT's own
    // QOpenGLWidget sample sets, for the same three reasons.
    driver->ChangeOptions().buffersNoSwap = Standard_True;
    driver->ChangeOptions().buffersOpaqueAlpha = Standard_True;
    driver->ChangeOptions().useSystemBuffer = Standard_False;
    // Told, not guessed: the profile the surface format above actually asks
    // for is the profile OCCT's context wrapper has to be initialized with.
    driver->ChangeOptions().contextCompatible =
        surfaceFormat().profile() != QSurfaceFormat::CoreProfile;

    myViewer = new V3d_Viewer(driver);
    myViewer->SetDefaultLights();
    myViewer->SetLightOn();

    myView = myViewer->CreateView();
    myContext = new AIS_InteractiveContext(myViewer);
    // What OCCT itself starts the selector at, captured rather than assumed:
    // Auto raises this tolerance and every other mode has to be handed back
    // the exact number it was picking with before, not a constant this file
    // believes OCCT uses. kDefaultPixelTolerancePx documents the value the
    // installed version does in fact start at; this is what actually gets
    // restored.
    myDefaultPixelTolerance = myContext->MainSelector()->CustomPixelTolerance();
    // ...and the tolerance auto wants is applied HERE, on the context that
    // has just been built, rather than being left to arrive as a side effect
    // of the first resizeGL(). Benign either way in practice - Qt always
    // resizes before the first paint - but auto is the constructed default
    // now, so the raise belongs where the viewer comes up in it rather than
    // in the one other place that happens to re-derive it. Safe here: it only
    // reads mySelectionMode and writes the selector.
    applySelectionTolerance();
    // Nothing below this line touches a window or a GL context, and that is
    // the point: this function is reached from every entry point that displays
    // something, including ones that run long before the widget is first shown
    // (a furniture loaded straight into a constructed-but-unshown MainWindow).
    // The old spelling attached a WNT_Window here, which meant realizing
    // winId() - which is what made an unconditional call to this function break
    // startup determinism once, and is the pitfall CLAUDE.md records. The
    // attach now lives in initializeGL(), where Qt hands us a live context.

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
    // A FOURTH layer, above every one of them, for the transform gizmo alone.
    //
    // A gizmo is fully visible over the geometry it stands on, always - that is
    // what a gizmo IS, it is what AIS_Manipulator did, and it is the one place
    // this app wants the property CLAUDE.md's grid section rejects for the
    // GRID: a layer that clears depth paints over every body, which is a defect
    // for a ground grid and the entire requirement for a handle standing at a
    // body's own bounding-box centre.
    //
    // Graphic3d_ZLayerId_Topmost already clears depth, and the gizmo used it -
    // but it is a SHARED layer. OCCT puts dynamically highlighted presentations
    // there, this file puts AIS_Manipulator there, and anything else may join;
    // whatever does arrives INSIDE the layer, after its one depth clear, at its
    // own true depth, and everything drawn behind it loses. The user's report
    // was arcs missing from the hollow rings where a body's surface was nearer,
    // which is exactly that shape of failure. A layer of our own removes the
    // question rather than reasoning about who else is in the room.
    //
    // Depth TEST stays on inside it, deliberately: the drawing is coplanar and
    // carries its own painter's order as small depth nudges (see
    // MoveGizmoRenderer::buildStrokes()), and turning the test off would leave
    // that order to whatever sequence OCCT happens to render the objects in.
    //
    // AND IT MUST BE AN IMMEDIATE LAYER, which cost an A/B to find. A custom,
    // NON-immediate layer that clears depth also clears the SHADOW MAP: OCCT
    // renders the shadow-map pass from the normal layer list, so a depth clear
    // sitting in that list wipes the depth texture the Shadows render tier is
    // built on. Measured, not reasoned about - `render-mode`'s own
    // cast-shadow check (a Dump()-differ, the same proof the tier probe uses)
    // failed with the layer and passed at the parent commit, and
    // SetRenderInDepthPrepass(false) did NOT fix it while SetImmediate(true)
    // did. Immediate is the honest description anyway: "drawn after all normal
    // layers" is what an overlay handle is, and it is the company the hover
    // highlight already keeps.
    {
        Graphic3d_ZLayerSettings settings;
        settings.SetName("FurnifyMe gizmo");
        settings.SetClearDepth(Standard_True);
        settings.SetEnableDepthTest(Standard_True);
        settings.SetEnableDepthWrite(Standard_True);
        // Ignored for an immediate layer, per the header, and set anyway so the
        // intent survives if the immediate flag ever comes off: a handle is
        // feedback, never something for a path tracer to integrate. Render mode
        // hides the gizmo outright regardless.
        settings.SetRaytracable(Standard_False);
        settings.SetRenderInDepthPrepass(Standard_False);
        settings.SetImmediate(Standard_True);
        Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_UNKNOWN;
        if (myViewer->InsertLayerAfter(layer, settings, Graphic3d_ZLayerId_Topmost))
            myGizmoLayer = layer;
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
        myMoveGizmo.attach(myContext);
        myMoveGizmo.setZLayer(myGizmoLayer);
        myRotateGizmo.attach(myContext);
        myRotateGizmo.setZLayer(myGizmoLayer);
        myScaleGizmo.attach(myContext);
        myScaleGizmo.setZLayer(myGizmoLayer);
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

    myInitialized = true;

}

bool OcctViewWidget::attachGlWindow()
{
    if (myView.IsNull() || myViewer.IsNull()) return false;

    Handle(OpenGl_GraphicDriver) driver =
        Handle(OpenGl_GraphicDriver)::DownCast(myViewer->Driver());
    if (driver.IsNull()) return false;

    // Wrapping Qt's live context is what tells us which rendering context the
    // view has to be handed. The wrapper itself is throw-away - only
    // RenderingContext() survives the call - because SetWindow() below builds
    // the OpenGl_Window (and the OpenGl_Context the rest of this file reaches
    // through hostGlContext()) that OCCT actually keeps.
    Handle(OpenGl_Context) bound = new OpenGl_Context();
    if (!bound->Init(!driver->Options().contextCompatible)) {
        Message::SendFail() << "FurnifyMe: unable to wrap Qt's OpenGL context";
        return false;
    }

    if (myHostWindow.IsNull()) {
        myHostWindow = new Aspect_NeutralWindow();
        // Virtual: window management is this application's business, not
        // OCCT's - which is exactly what Aspect_NeutralWindow is for.
        myHostWindow->SetVirtual(Standard_True);
    }
    myHostWindow->SetNativeHandle(currentGlNativeWindow());
    const QSize device = viewportDeviceSize();
    myHostWindow->SetSize(std::max(1, device.width()), std::max(1, device.height()));

    myView->SetWindow(myHostWindow, bound->RenderingContext());
    // SetImmediateModeDrawToFront(false) IS NOT CALLED HERE, AND THAT IS A
    // SETTLED DECISION, NOT AN OMISSION (Phase 3, 2026-09-04).
    //
    // The argument for calling it is real and was acted on first.
    // Graphic3d_CView documents the flag's default as TRUE, meaning immediate
    // structures - the dynamic hover highlight, the manipulator mid-drag - are
    // drawn "directly to the front buffer", and warns that such content "will
    // be missed in image dump since it is performed from back buffer". A
    // QOpenGLWidget has no front buffer at all.
    //
    // Phase 1 measured it and parked it; Phase 3 re-measured it in the
    // finished compositing layer, on one binary with the flag behind a
    // throw-away switch, and made the parking permanent. What the flag buys:
    //
    //   - It DOES silence a real OCCT error. With the flag at its default,
    //     OCCT allocates a separate immediate-scene framebuffer and asks for
    //     its colour attachment as GL_SRGB8_ALPHA8; this driver refuses with
    //     GL_INVALID_OPERATION and OCCT logs "Immediate FBO WxH@0
    //     initialization has failed" twice per run. With the flag set, no such
    //     FBO is allocated and the log is clean. THE ERROR IS HARMLESS HERE:
    //     OCCT falls back on its own, the frame is correct, and it is logged
    //     identically at 100% and at 150% scaling - a 100% run carrying both
    //     messages passes every check in the suite.
    //
    // WHAT THE EVIDENCE COVERS, AND WHAT IT DOES NOT (added by the branch
    // review, because the paragraph above read as a general fact and it is a
    // one-GPU measurement). The reason immediate content lands where Dump can
    // read it on THIS machine is precisely that the separate immediate FBO was
    // refused: with no immediate framebuffer to draw into, OCCT's "front
    // buffer" writes fall back to the bound default framebuffer, which is the
    // one Qt composites and the one Dump reads. GL_SRGB8_ALPHA8 is
    // colour-renderable in core GL 4.x, so on hardware where that allocation
    // SUCCEEDS the immediate layer goes somewhere else entirely and the
    // 13,941-pixel hover pin is untested rather than known-good. The decision
    // does not rest on that leg: the flag's cost is Dump-side and therefore
    // driver-independent (see the two measurements below), and the hover pin
    // is a live check, so a machine where the allocation succeeds and the
    // highlight stops reaching Dump reports it rather than hiding it.
    //
    // What it costs, all measured on the same build, same scene, same machine:
    //
    //   - THE USER STOPS SEEING WHAT THE PROBES MEASURE. Composited window
    //     against V3d_View::Dump, as median colour: 2.4/255 apart without the
    //     flag, 97.6 apart with it - and the picture says the same thing the
    //     number does, the studio floor rendering a full step darker than the
    //     backdrop it is calibrated to dissolve into, seam and all, while the
    //     Dump every calibration in this file is read from stays correct.
    //   - A user-chosen background lands 118/255 from the colour they picked
    //     instead of 26. That is Phase 1's number, reproduced exactly in a
    //     codebase whose whole overlay compositing changed in between.
    //
    // PROVENANCE OF THE A/B, since the ordering against the accumulation fix
    // decides how much of one column to believe. The no-flag 2.4 is
    // unambiguously POST-fix: it is the same number Phase 3's own
    // Dump-against-screen finding reports after scheduleAccumulationFrame()
    // landed. The with-flag 97.6 is Phase 3's too, taken "in the finished
    // compositing layer" - but the report does not state in so many words
    // whether that switch was thrown before or after the fix in the same
    // session, and it matters more than it looks: the PRE-fix screen-vs-Dump
    // distance was 85.5 (#93918f against #c4c3c0), so accumulation grain alone
    // could account for most of a 97.6 if that column happened to be taken
    // first. Which is exactly why this decision does not stand on that column.
    // It stands on the user-chosen background: 118.2 against 26, measured in
    // PHASE 1 - before scheduleAccumulationFrame() existed at all - on a
    // V3d_View::Dump, which no amount of accumulation restarting can move,
    // with the rest of the frame byte-identical. That number is the evidence;
    // the Dump-vs-screen column is corroboration.
    //
    // And the harm it guards against is measured NOT to exist in this hosting
    // layer: OCCT's "front buffer" writes land in the bound default
    // framebuffer, which is exactly the one Qt composites and exactly the one
    // Dump reads. gui_smoke pins that rather than asserting it - it Dumps a
    // hovered body and finds 13,941 highlight-tinted pixels against 0 on the
    // unhovered frame, with and without the flag alike.
    //
    // So: a cosmetic log line against the render's correctness. The log line
    // loses.
    myView->MustBeResized();
    invalidateAccumulation();
    myAttachedContext = context();
    return true;
}

bool OcctViewWidget::wrapDefaultFramebuffer()
{
    const Handle(OpenGl_Context) context = hostGlContext(myView);
    if (context.IsNull() || myHostWindow.IsNull()) return false;

    Handle(HostFrameBuffer) fbo =
        Handle(HostFrameBuffer)::DownCast(context->DefaultFrameBuffer());
    if (fbo.IsNull()) fbo = new HostFrameBuffer();

    // BIND QT'S OWN FRAMEBUFFER FIRST, rather than trusting whatever happens
    // to be bound. InitWrapper is documented as initializing "from currently
    // bound FBO", and this function then treats that FBO's size as
    // authoritative - it calls SetSize() + MustBeResized() from it. A nested
    // GlScope takes no context and binds nothing (that is the whole point of
    // the nesting guard), and nesting is the NORMAL case for the measuring
    // probes, each entered straight after an OCCT Redraw() that may well have
    // left a shadow map or a ray-tracing accumulation buffer bound. Wrapping
    // one of those would silently resize the view to it, take the pixel
    // measurement this project treats as ground truth at the wrong size, and
    // then self-heal on the next paintGL so nothing ever reported it.
    // defaultFramebufferObject() is Qt's own answer to "which framebuffer is
    // this widget's", and it is the only honest input here.
    if (context->arbFBO != nullptr) {
        context->arbFBO->glBindFramebuffer(
            GL_FRAMEBUFFER, static_cast<GLuint>(defaultFramebufferObject()));
    }

    if (!fbo->InitWrapper(context)) {
        context->SetDefaultFrameBuffer(Handle(OpenGl_FrameBuffer)());
        return false;
    }
    // Cleared before the resize below and set again after - OCCT's own sample
    // does this and names it a workaround; the resize path inside
    // MustBeResized() otherwise reasons about a default framebuffer that is
    // mid-rebuild.
    context->SetDefaultFrameBuffer(Handle(OpenGl_FrameBuffer)());

    // The FBO is the authority on the size OCCT is actually drawing into.
    // resizeGL() gets there first in the ordinary case; this is what catches
    // the cases Qt does not announce - a device-pixel-ratio change when the
    // window is dragged to another monitor, and the FBO Qt silently rebuilds
    // around a reparent.
    const Graphic3d_Vec2i fboSize = fbo->GetVPSize();
    Graphic3d_Vec2i windowSize(0, 0);
    myHostWindow->Size(windowSize.x(), windowSize.y());
    if (fboSize != windowSize) {
        myHostWindow->SetSize(fboSize.x(), fboSize.y());
        myView->MustBeResized();
        invalidateAccumulation();
    }
    context->SetDefaultFrameBuffer(fbo);
    return true;
}

void OcctViewWidget::initializeGL()
{
    // The lazy contract, landing where Qt puts it. NOT "once": Qt calls this
    // again after any context reset, which is exactly the case
    // releaseGlResources() exists for - and by then that function has put this
    // widget back to its never-initialized state, so initializeViewer() below
    // rebuilds rather than reviving handles into a context that is gone.
    initializeViewer();
    if (myView.IsNull()) return;
    if (!attachGlWindow()) return;

    // The ONE hook Qt offers for releasing GPU resources while the dying
    // context is still alive. Direct connection, because the handler has to
    // run inside the emission rather than after it, and receiver `this` so it
    // dies with the widget - the destructor disconnects it explicitly first,
    // since that path releases explicitly and must not be called back into
    // half-destroyed. Re-armed on every attach, because a rebuilt context is a
    // different QObject.
    if (context() != nullptr) {
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                [this]() {
                    releaseGlResources();
                    // AND TELL THE OWNER. releaseGlResources() has just emptied
                    // every presentation map this widget holds; nothing else
                    // puts the document back, and until something does the
                    // viewport is blank and MainWindow's Render mode action is
                    // checked over a widget whose render mode was cleared under
                    // it. Emitted HERE rather than inside releaseGlResources()
                    // so the destructor's own call to that function stays
                    // silent - see the signal's own comment on the header.
                    emit glResourcesReleased();
                }, Qt::DirectConnection);
        // A pure observer, receiver-scoped to the CONTEXT rather than to this
        // widget, so it lives exactly as long as the thing it watches and can
        // still record the death after the widget has released and
        // disconnected. It is what makes the ordering ASSERTABLE: the suite
        // compares lastGlReleaseTick() against lastGlContextDeathTick() rather
        // than trusting that the destructor did the right thing.
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, context(),
                []() { ourLastGlContextDeathTick = ++ourGlTeardownTick; },
                Qt::DirectConnection);
    }

    // The camera has to be pushed again now that the view has a window to
    // measure: initializeViewer()'s own applyCameraState() ran with nothing
    // attached, and the orthographic branch derives its parallel scale from
    // this widget's height.
    //
    // QUEUED, not called here. applyCameraState() emits cameraChanged() and
    // asks for a frame, and doing either from inside Qt's own GL callback
    // re-enters arbitrary overlay slots during the paint pass. The old
    // paintEvent did the same thing through initializeViewer(); making the
    // hosting swap explicit is the moment to stop.
    QMetaObject::invokeMethod(this, [this]() {
        if (myView.IsNull()) return;
        myView->Camera()->SetFOVy(effectiveFovyDeg());
        applyCameraState();
    }, Qt::QueuedConnection);
}

void OcctViewWidget::paintGL()
{
    if (myView.IsNull()) return;

    // Qt can rebuild its OpenGL context under us, and the view would then be
    // holding a rendering context that no longer exists. Re-attaching is the
    // only recovery on offer - and it is a POOR one, honestly: SetWindow()
    // tears the old OpenGl_Window down, which releases GPU resources against
    // the dead context, which is a hard crash rather than a glitch. It was
    // measured, at exactly the compare pane's reparent.
    //
    // Qt::AA_ShareOpenGLContexts (main.cpp, before QApplication) is what keeps
    // this branch unreached: with it set Qt preserves the context across a
    // reparent, which is the only context-rebuilding event this application
    // actually performs.
    //
    // IT IS A BACKSTOP, NOT THE RECOVERY, and the comment used to claim
    // otherwise. The real recovery from a context death is
    // releaseGlResources() on QOpenGLContext::aboutToBeDestroyed followed by
    // Qt's own second initializeGL(); by the time this branch could matter,
    // that has already happened. What it catches is the case where neither
    // did - and it compares CONTEXT IDENTITY, not the native window handle it
    // used to, because a context rebuilt on the SAME top-level window leaves
    // that handle identical and would sail straight through, leaving the view
    // rendering on a dangling HGLRC.
    if (myView->Window().IsNull() ||
        myHostWindow.IsNull() ||
        myAttachedContext.isNull() ||
        myAttachedContext != context()) {
        if (!attachGlWindow()) return;
    }
    if (!wrapDefaultFramebuffer()) return;

    const Handle(OpenGl_Context) context = hostGlContext(myView);
    if (context.IsNull()) return;

    // Qt leaves GL state behind that OCCT does not reset before drawing opaque
    // geometry - a bound shader program, a bound texture, blending enabled -
    // and OCCT leaves state behind that Qt's own compositor assumes is at its
    // default, most visibly the pixel-store alignment its glyph textures are
    // uploaded with. Both directions are cleaned here rather than hoped about;
    // OCCT's own sample carries the identical pair.
    if (context->core20fwd != nullptr) context->core20fwd->glUseProgram(0);
    if (context->core11fwd != nullptr) {
        context->core11fwd->glBindTexture(GL_TEXTURE_2D, 0);
        context->core11fwd->glDisable(GL_BLEND);
    }

    // One clock for the whole process, started on first use - see PaintSample
    // on the header. Static so two views (the live one and the compare pane's)
    // report on the same timeline.
    static QElapsedTimer ourPaintClock;
    if (!ourPaintClock.isValid()) ourPaintClock.start();
    const long long beginUs = ourPaintClock.nsecsElapsed() / 1000;

    myView->Redraw();

    const long long redrawUs = ourPaintClock.nsecsElapsed() / 1000 - beginUs;
    if (static_cast<int>(myPaintSamples.size()) < kPaintSampleRing) {
        myPaintSamples.push_back({beginUs, redrawUs});
        myPaintSampleNext = static_cast<int>(myPaintSamples.size()) % kPaintSampleRing;
    } else {
        myPaintSamples[myPaintSampleNext] = {beginUs, redrawUs};
        myPaintSampleNext = (myPaintSampleNext + 1) % kPaintSampleRing;
    }

    // One more frame on the current accumulation run. Counted here, at the one
    // place a frame is actually painted, rather than at whichever scheduler
    // asked for it - see accumulationDepth().
    ++myAccumulationDepth;
    // And one more frame, full stop - see totalPaintCount() for why this
    // sibling never resets where the one above does.
    ++myTotalPaintCount;

    if (context->core11fwd != nullptr) {
        context->core11fwd->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        context->core11fwd->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    if (context->core15fwd != nullptr) context->core15fwd->glActiveTexture(GL_TEXTURE0);
}

void OcctViewWidget::resizeGL(int w, int h)
{
    if (myView.IsNull() || myHostWindow.IsNull()) return;
    // DEVICE pixels, through this file's one conversion point - the window
    // OCCT measures itself against is device-sized exactly as the WNT_Window's
    // client rect was, and Aspect_Window::DevicePixelRatio() is left at its 1.0
    // default (WNT_Window never overrode it either) so the ratio is applied
    // here and nowhere else.
    const QPoint device = toDevicePixels(QPoint(std::max(1, w), std::max(1, h)));
    myHostWindow->SetSize(std::max(1, device.x()), std::max(1, device.y()));
    myView->MustBeResized();
    // The logical->device ratio can change under a widget that never resized
    // in logical terms (a window dragged to a display at another scale sends
    // exactly this callback), and Auto's edge tolerance is a promise in
    // LOGICAL pixels. Re-derived here, so the promise survives the move.
    applySelectionTolerance();
    invalidateAccumulation();
}

void OcctViewWidget::invalidateAccumulation()
{
    if (myView.IsNull()) return;
    myView->Invalidate();
    // The accumulation run ends exactly where the Invalidate lands, so the
    // counter that names its depth is zeroed in the same breath rather than at
    // each of the four call sites - see accumulationDepth() on the header for
    // what reads it and why it exists at all.
    myAccumulationDepth = 0;
}

std::vector<OcctViewWidget::PaintSample> OcctViewWidget::recentPaints() const
{
    // Unrolled oldest-first, so a caller reading successive beginUs values as
    // present-to-present intervals never has to know where the ring wrapped.
    std::vector<PaintSample> out;
    out.reserve(myPaintSamples.size());
    if (static_cast<int>(myPaintSamples.size()) < kPaintSampleRing) {
        out = myPaintSamples;
        return out;
    }
    for (int i = 0; i < kPaintSampleRing; ++i)
        out.push_back(myPaintSamples[(myPaintSampleNext + i) % kPaintSampleRing]);
    return out;
}

void OcctViewWidget::clearPaintSamples()
{
    myPaintSamples.clear();
    myPaintSampleNext = 0;
}

void OcctViewWidget::scheduleRedraw()
{
    if (myView.IsNull()) return;
    // Invalidate first: OCCT is entitled to reuse the last main-buffer content
    // when only the immediate layer changed, and every caller of this is
    // telling us the scene itself moved.
    invalidateAccumulation();
    update();
}

void OcctViewWidget::scheduleAccumulationFrame()
{
    if (myView.IsNull()) return;
    // Deliberately NO Invalidate() - see the header. This is the one caller
    // that is not reporting a change to the scene, and the one that therefore
    // leaves accumulationDepth() standing to climb another frame.
    update();
}

QSize OcctViewWidget::hostWindowSize() const
{
    if (myHostWindow.IsNull()) return QSize();
    Standard_Integer w = 0, h = 0;
    myHostWindow->Size(w, h);
    return QSize(w, h);
}

QSize OcctViewWidget::viewportDeviceSize() const
{
    const QPoint device = toDevicePixels(QPoint(width(), height()));
    return QSize(device.x(), device.y());
}

OcctViewWidget::GlScope::GlScope(OcctViewWidget* view)
{
    if (view == nullptr || view->myView.IsNull() || view->myView->Window().IsNull()) return;
    QOpenGLContext* context = view->context();
    if (context == nullptr) return;

    myWidget = view;
    // Only take it when it is not already ours - a probe that calls another
    // probe, or anything reached from inside paintGL(), must not have the
    // context released out from under it by the inner scope's destructor.
    if (QOpenGLContext::currentContext() != context) {
        myWidget->makeCurrent();
        myTook = true;
    }
    myWidget->wrapDefaultFramebuffer();
}

OcctViewWidget::GlScope::~GlScope()
{
    if (myWidget == nullptr || !myTook) return;
    myWidget->doneCurrent();
    // Whatever was rendered inside this scope went into Qt's framebuffer
    // without Qt knowing, so nothing has composited it. Ask for the frame that
    // does - the same rule scheduleRedraw() states, applied to the synchronous
    // half of the bridge.
    myWidget->update();
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
    // Milestone 5, item 6: the boundary width is an editable token now - 0
    // means no boundary lines at all, the same rule chipStrokePx's 0 already
    // follows for a chip's own ring.
    {
        const double edgeWidth = Theme::edgeWidthPx();
        presentation->Attributes()->SetFaceBoundaryDraw(edgeWidth > 0.0);
        if (edgeWidth > 0.0) {
            presentation->Attributes()->SetFaceBoundaryAspect(
                new Prs3d_LineAspect(Quantity_NOC_GRAY30, Aspect_TOL_SOLID, edgeWidth));
        }
    }
    myContext->Display(presentation, myWireframe ? AIS_WireFrame : AIS_Shaded,
                       kSelectionModeWholeShape, Standard_False);
    mySolids[id] = presentation;
    // No picking in viewer-only mode - see the header. Leaving the
    // presentation's selection mode deactivated is the literal version of
    // "never pickable", on the same terms an outline already is.
    if (!myViewerOnly) applySelectionMode(presentation);

    scheduleRedraw();
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
    // Same reason: the body gizmos stand on the presentation about to go.
    clearBodyGizmos();
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
    scheduleRedraw();
}

void OcctViewWidget::clearSolids()
{
    if (myContext.IsNull()) return;

    clearModelingPreview();   // same reasoning as removeSolid(), before the bodies go
    clearPullArrow();
    clearBevelArrow();
    clearBodyGizmos();
    cancelMirrorPlacement();   // same reasoning: every id it describes is about to go

    for (auto& entry : mySolids) myContext->Remove(entry.second, Standard_False);
    mySolids.clear();
    myDimension.clear();   // same rule as removeSolid(): nothing left to measure
    scheduleRedraw();
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
        scheduleRedraw();
    } else {
        // Erase also drops it from the selection, which is what we want: acting
        // on something you cannot see would be a nasty surprise. The selection
        // can genuinely change here, unlike on the show path, so this is the
        // only branch that should tell the status bar to re-check it.
        myContext->Erase(it->second, Standard_False);
        scheduleRedraw();
        emit selectionChanged();
    }
}

bool OcctViewWidget::isSolidVisible(int id) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || myContext.IsNull()) return false;
    return myContext->IsDisplayed(it->second);
}

double OcctViewWidget::solidFaceBoundaryWidth(int id) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || it->second.IsNull()) return -1.0;
    const Handle(Prs3d_Drawer)& attrs = it->second->Attributes();
    if (attrs.IsNull() || !attrs->FaceBoundaryDraw()) return -1.0;
    const Handle(Prs3d_LineAspect)& aspect = attrs->FaceBoundaryAspect();
    if (aspect.IsNull() || aspect->Aspect().IsNull()) return -1.0;
    return aspect->Aspect()->Width();
}

void OcctViewWidget::displayOutline(int id, const TopoDS_Face& face)
{
    initializeViewer();
    if (myContext.IsNull() || face.IsNull()) return;

    removeOutline(id);

    ModelingOps::tessellate(face, 0.1);

    Handle(AIS_Shape) presentation = new AIS_Shape(face);
    // Milestone 5, item 7: the same editable colour the live in-progress
    // outline wears via setPreview() below - one token for the one word
    // (outline) at two moments of its life, no longer the hardcoded OCCT
    // yellow every piece of sketch work used to share with
    // setModelingPreview() (that channel is a different, unrelated feature
    // and stays on the stock colour - see this token's own header comment).
    // An outline is a document item, but it is a FLAT one that is not a
    // body yet, and giving it the bodies' grey would say it was one.
    presentation->SetColor(toOcctColor(Theme::outlineLineColour()));
    // Milestone 5, item 6: the same editable width the live in-progress
    // outline wears via setPreview() below - one token for the one word
    // (outline) at two moments of its life.
    presentation->SetWidth(Theme::sketchLineWidthPx());
    // Above the work-plane grid it lies exactly on top of - see sketchZLayer().
    markInSketchLayer(presentation);
    // Selection mode -1: never pickable. Outlines are handled from the drawer
    // this phase, and a shape the user can select but cannot Union, Pull or
    // bevel would be a selection that makes every gizmo predicate lie.
    myContext->Display(presentation, AIS_Shaded, -1, Standard_False);
    myOutlines[id] = presentation;

    scheduleRedraw();
}

void OcctViewWidget::removeOutline(int id)
{
    const auto it = myOutlines.find(id);
    if (it == myOutlines.end() || myContext.IsNull()) return;

    myContext->Remove(it->second, Standard_False);
    myOutlines.erase(it);
    scheduleRedraw();
}

void OcctViewWidget::clearOutlines()
{
    if (myContext.IsNull()) return;

    for (auto& entry : myOutlines) myContext->Remove(entry.second, Standard_False);
    myOutlines.clear();
    scheduleRedraw();
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
    scheduleRedraw();
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
    // Milestone 5, items 6 and 7: the same editable width and colour
    // displayOutline() wears - see that function's own comment.
    myPreview->SetColor(toOcctColor(Theme::outlineLineColour()));
    myPreview->SetWidth(Theme::sketchLineWidthPx());
    // The in-progress outline and the closed face are drawn above the
    // work-plane grid they sit exactly on top of - see sketchZLayer().
    markInSketchLayer(myPreview);
    // Selection mode -1: feedback only, never pickable.
    myContext->Display(myPreview, shaded ? AIS_Shaded : AIS_WireFrame, -1, Standard_False);
    scheduleRedraw();
}

void OcctViewWidget::clearPreview()
{
    if (myContext.IsNull() || myPreview.IsNull()) return;

    myContext->Remove(myPreview, Standard_False);
    myPreview.Nullify();
    scheduleRedraw();
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
    if (shape.IsNull()) {
        clearModelingPreview();
        return;
    }
    setModelingPreviews({{replacesSolidId, shape}});
}

void OcctViewWidget::setModelingPreviews(const std::vector<std::pair<int, TopoDS_Shape>>& previews)
{
    initializeViewer();
    if (myContext.IsNull()) return;

    clearModelingPreview();
    if (previews.empty()) return;

    myModelingPreviews.reserve(previews.size());
    myModelingPreviewSolids.reserve(previews.size());
    for (const auto& [replacesSolidId, shape] : previews) {
        if (shape.IsNull()) continue;

        ModelingOps::tessellate(shape, 0.1);

        Handle(AIS_Shape) preview = new AIS_Shape(shape);
        // The same yellow setPreview() uses. One rule - a preview is yellow, a
        // body is grey - rather than a second preview colour per feature. It
        // also has to differ from the pull arrow standing on top of it: both
        // were Theme::accent() at first, and the magnified capture showed an
        // arrow that was technically drawn and practically invisible against
        // the shape it was pulling.
        preview->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
        preview->SetWidth(2.0);
        // In the sketch-work layer with the rest of the feedback: a pull or a
        // bevel preview carving a body sitting on the ground grid is exactly
        // the shape the grid must not paint over. Depth testing is on in that
        // layer, so it still hides behind whatever is genuinely in front of
        // it.
        markInSketchLayer(preview);
        // Selection mode -1: feedback only, never pickable - the same rule
        // the sketch preview and every marker follows. A shape the user can
        // select that exists in no document is the worst thing a preview can
        // produce.
        myContext->Display(preview, AIS_Shaded, -1, Standard_False);
        myModelingPreviews.push_back(preview);

        // The body this preview stands in for becomes a cage for the
        // duration - see the header for why, and why this is SetDisplayMode
        // rather than Erase (Erase would drop the selection the gizmo's
        // predicate reads).
        const auto it = mySolids.find(replacesSolidId);
        if (it != mySolids.end()) {
            myContext->SetDisplayMode(it->second, AIS_WireFrame, Standard_False);
            myModelingPreviewSolids.push_back(replacesSolidId);
        } else {
            myModelingPreviewSolids.push_back(-1);
        }
    }

    scheduleRedraw();
}

void OcctViewWidget::clearModelingPreview()
{
    if (myContext.IsNull()) return;

    bool changed = false;
    for (int solidId : myModelingPreviewSolids) {
        if (solidId < 0) continue;
        const auto it = mySolids.find(solidId);
        if (it != mySolids.end()) {
            myContext->SetDisplayMode(it->second, myWireframe ? AIS_WireFrame : AIS_Shaded,
                                      Standard_False);
            changed = true;
        }
    }
    // Cleared even when a body has gone (a commit replaces it), so an id can
    // never be restored onto a different body later.
    myModelingPreviewSolids.clear();

    for (const Handle(AIS_Shape)& preview : myModelingPreviews) {
        if (preview.IsNull()) continue;
        myContext->Remove(preview, Standard_False);
        changed = true;
    }
    myModelingPreviews.clear();

    if (changed) scheduleRedraw();
}

bool OcctViewWidget::hasModelingPreview() const
{
    return !myModelingPreviews.empty();
}

TopoDS_Shape OcctViewWidget::modelingPreviewShape() const
{
    if (myModelingPreviews.size() != 1 || myModelingPreviews.front().IsNull())
        return TopoDS_Shape();
    return myModelingPreviews.front()->Shape();
}

void OcctViewWidget::showPullArrow(const gp_Pnt& centre, const gp_Dir& outward)
{
    initializeViewer();
    if (myView.IsNull()) return;
    // No viewer update of its own while a camera change is being applied:
    // applyCameraState() emits cameraChanged() and then redraws, and this
    // rebuild rides along with that redraw. Forcing one here as well made
    // every orbit step pay for two vsync-bound frames instead of one.
    // The renderer draws; THIS asks for the frame - and only when the arrow
    // actually moved, which is its own equal-guard's answer. Skipped under
    // myApplyingCamera because applyCameraState()'s own redraw is already
    // coming, which is the measured saving PullArrowRenderer's comment records.
    const bool arrowChanged =
        myPullArrow.show(centre, outward, myView->Camera()->Direction(), worldPerPixel());
    if (arrowChanged && !myApplyingCamera) scheduleRedraw();
}

void OcctViewWidget::clearPullArrow()
{
    const bool arrowRemoved = myPullArrow.clear();
    myPullDrag.active = false;
    if (arrowRemoved) scheduleRedraw();
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
    const bool arrowChanged =
        myBevelArrow.show(centre, outward, myView->Camera()->Direction(), worldPerPixel());
    if (arrowChanged && !myApplyingCamera) scheduleRedraw();
}

void OcctViewWidget::clearBevelArrow()
{
    const bool arrowRemoved = myBevelArrow.clear();
    myBevelDrag.active = false;
    if (arrowRemoved) scheduleRedraw();
}

bool OcctViewWidget::bevelArrowHead(gp_Pnt& out) const
{
    if (!myBevelArrow.isShowing()) return false;
    out = myBevelArrow.head();
    return true;
}

void OcctViewWidget::showMoveGizmo(const gp_Pnt& pivot)
{
    initializeViewer();
    if (myView.IsNull()) return;
    // One body gizmo at a time - the same clear showRotateGizmo() and
    // showScaleGizmo() open with. Its absence HERE was the Space-cycle bug:
    // the two Phase 2 shows cleared their siblings, cycling back to Move
    // cleared nothing, and the fourth press wore Move's arrows over Scale's
    // cubes.
    bool cleared = myRotateGizmo.clear();
    cleared = myScaleGizmo.clear() || cleared;
    // The renderer draws; THIS asks for the frame - and only when the gizmo
    // actually moved, which is its own equal-guard's answer. Skipped under
    // myApplyingCamera because applyCameraState()'s own redraw is already
    // coming: showPullArrow()'s rule, for the measured reason recorded there.
    GizmoPose pose;
    pose.pivot = pivot;
    // The camera frame from CameraController, NOT from the OCCT camera: the
    // axis card lays its own drawing out along exactly these three vectors
    // (AxisCard::computeTips()), and the whole point of the scene gizmo being
    // that same drawing is that both read one frame rather than two that
    // usually agree.
    pose.right = myCamera.rightVector();
    pose.up = myCamera.upVector();
    pose.view = myCamera.viewDirection();
    pose.worldPerPixel = worldPerPixelAt(pivot);
    pose.pixelRatio = devicePixelRatioF();
    const bool changed = myMoveGizmo.show(pose) || cleared;
    if (changed && !myApplyingCamera) scheduleRedraw();
}

void OcctViewWidget::clearMoveGizmo()
{
    const bool removed = myMoveGizmo.clear();
    myMoveDrag.active = false;
    myMoveDragAxis = -1;
    if (removed) scheduleRedraw();
}

bool OcctViewWidget::moveGizmoArmTip(int axis, gp_Pnt& out) const
{
    return moveGizmoHandleTip(axis, true, out);
}

bool OcctViewWidget::moveGizmoHandleDrawn(int axis, bool positive) const
{
    return myMoveGizmo.isShowing() && myMoveGizmo.handleDrawn(axis, positive);
}

bool OcctViewWidget::moveGizmoHandleTip(int axis, bool positive, gp_Pnt& out) const
{
    if (!myMoveGizmo.isShowing() || axis < 0 || axis > 2) return false;
    out = myMoveGizmo.handleTip(axis, positive);
    return true;
}

void OcctViewWidget::cancelMoveDrag()
{
    if (!myMoveDrag.active) return;
    myMoveDrag.active = false;
    myMoveDrag.moved = false;
    myMoveDragAxis = -1;
    clearMagnetGuide();
    myMoveMagnetCandidates.clear();
    // The button is still down. Whatever release follows belongs to the
    // gesture this just ended, and letting it reach the picker would replace
    // the body selection the gizmo is standing on - which would retire the
    // gizmo the user cancelled a drag on, rather than leaving them where they
    // were.
    myMoveDragCancelled = true;
}

double OcctViewWidget::worldPerPixelAt(const gp_Pnt& at) const
{
    // worldPerPixel() answers for the camera TARGET's plane; a gizmo stands
    // wherever its body's pivot is, and in PERSPECTIVE a pivot nearer than
    // the target projects larger than that number says - the size drifted
    // with every zoom until the depth term joined. The depth ratio is the
    // one the manipulator's own size clamp carried before it died, applied
    // at the source the custom gizmos size themselves from. Along the view
    // axis, never the
    // straight-line distance: depth is what scales a projection, and an
    // off-centre pivot is further away without being any deeper. A parallel
    // projection has no depth term, so the factor is exactly 1 there by
    // construction.
    double wpp = worldPerPixel();
    if (!myCamera.effectiveOrtho()) {
        const double depth =
            gp_Vec(myCamera.eyePosition(), at).Dot(gp_Vec(myCamera.viewDirection()));
        const double targetDepth = myCamera.state().distance;
        if (depth > 1.0e-6 && targetDepth > 1.0e-6) wpp *= depth / targetDepth;
    }
    return wpp;
}

// --- the Rotate and Scale gizmos (custom gizmo, Phase 2) --------------------

void OcctViewWidget::showRotateGizmo(const gp_Pnt& pivot)
{
    initializeViewer();
    if (myView.IsNull()) return;
    // One body gizmo at a time - the split design's law, enforced where the
    // showing happens rather than trusted to every caller.
    bool changed = myMoveGizmo.clear();
    changed = myScaleGizmo.clear() || changed;
    GizmoPose pose;
    pose.pivot = pivot;
    pose.right = myCamera.rightVector();
    pose.up = myCamera.upVector();
    pose.view = myCamera.viewDirection();
    pose.worldPerPixel = worldPerPixelAt(pivot);
    pose.pixelRatio = devicePixelRatioF();
    changed = myRotateGizmo.show(pose) || changed;
    if (changed && !myApplyingCamera) scheduleRedraw();
}

void OcctViewWidget::showScaleGizmo(const gp_Pnt& pivot)
{
    initializeViewer();
    if (myView.IsNull()) return;
    bool changed = myMoveGizmo.clear();
    changed = myRotateGizmo.clear() || changed;
    GizmoPose pose;
    pose.pivot = pivot;
    pose.right = myCamera.rightVector();
    pose.up = myCamera.upVector();
    pose.view = myCamera.viewDirection();
    pose.worldPerPixel = worldPerPixelAt(pivot);
    pose.pixelRatio = devicePixelRatioF();
    changed = myScaleGizmo.show(pose) || changed;
    if (changed && !myApplyingCamera) scheduleRedraw();
}

void OcctViewWidget::clearBodyGizmos()
{
    bool removed = myMoveGizmo.clear();
    removed = myRotateGizmo.clear() || removed;
    removed = myScaleGizmo.clear() || removed;
    myMoveDrag.active = false;
    myMoveDragAxis = -1;
    myRotateDrag.active = false;
    myScaleDrag.active = false;
    // Stale hover must not survive into the next showing - the cursor may be
    // somewhere else entirely by then. Reset AFTER clear(), where it is free:
    // setHoveredAxis() only rebuilds a gizmo that is still showing.
    myMoveGizmo.setHoveredAxis(-1);
    myRotateGizmo.setHoveredAxis(-1);
    myScaleGizmo.setHoveredAxis(-1);
    clearMagnetGuide();
    myMoveMagnetCandidates.clear();
    // The grab cursor goes with the handles it was pointing at.
    unsetCursor();
    if (removed) scheduleRedraw();
}

int OcctViewWidget::rotateGizmoAxisAt(const QPoint& point) const
{
    if (!myRotateGizmo.isShowing()) return -1;
    // The drawn tori, sampled in screen space: walk each ring as a polyline
    // and keep the nearest ring within the shared grab tolerance - the same
    // nearest-wins rule the arm handles keep, for the same reason (rings
    // cross each other twice per pair).
    constexpr int kSamples = 48;
    int best = -1;
    double bestDistance = kHandleGrabPx;
    for (int axis = 0; axis < 3; ++axis) {
        gp_Pnt previous = myRotateGizmo.ringPoint(axis, 0.0);
        for (int i = 1; i <= kSamples; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * double(i) / double(kSamples);
            const gp_Pnt current = myRotateGizmo.ringPoint(axis, angle);
            const double distance = segmentPixelDistance(previous, current, point);
            previous = current;
            if (distance < 0.0 || distance > bestDistance) continue;
            best = axis;
            bestDistance = distance;
        }
    }
    return best;
}

int OcctViewWidget::scaleGizmoAxisAt(const QPoint& point) const
{
    if (!myScaleGizmo.isShowing()) return -1;
    // moveGizmoAxisAt()'s test on the scale gizmo's own handles - nearest
    // wins, dead inner third, drawn-only.
    int best = -1;
    double bestDistance = kHandleGrabPx;
    for (int axis = 0; axis < 3; ++axis) {
        if (!myScaleGizmo.handleDrawn(axis, true)) continue;
        const double distance = segmentPixelDistance(myScaleGizmo.handleGrabStart(axis, true),
                                                     myScaleGizmo.handleTip(axis, true), point);
        if (distance < 0.0 || distance > bestDistance) continue;
        best = axis;
        bestDistance = distance;
    }
    return best;
}

bool OcctViewWidget::rotateDragAnchor(gp_Pnt& out) const
{
    if (!myRotateDrag.active) return false;
    out = myRotateDrag.anchor;
    return true;
}

bool OcctViewWidget::scaleGizmoHandleTip(int axis, gp_Pnt& out) const
{
    if (!myScaleGizmo.isShowing() || axis < 0 || axis > 2) return false;
    out = myScaleGizmo.handleTip(axis, true);
    return true;
}

void OcctViewWidget::cancelBodyGizmoDrag()
{
    cancelMoveDrag();
    if (myRotateDrag.active) {
        myRotateDrag.active = false;
        myRotateDrag.moved = false;
        // cancelMoveDrag()'s own release-swallowing contract: the button is
        // still down and the coming release belongs to the cancelled gesture.
        myMoveDragCancelled = true;
    }
    if (myScaleDrag.active) {
        myScaleDrag.active = false;
        myScaleDrag.moved = false;
        myMoveDragCancelled = true;
    }
}

void OcctViewWidget::updateBodyGizmoHover(const QPoint& logical)
{
    bool changed = false;
    if (myMoveGizmo.isShowing() && !myMoveDrag.active)
        changed = myMoveGizmo.setHoveredAxis(moveGizmoAxisAt(logical)) || changed;
    if (myRotateGizmo.isShowing() && !myRotateDrag.active)
        changed = myRotateGizmo.setHoveredAxis(rotateGizmoAxisAt(logical)) || changed;
    if (myScaleGizmo.isShowing() && !myScaleDrag.active)
        changed = myScaleGizmo.setHoveredAxis(scaleGizmoAxisAt(logical)) || changed;
    if (changed) scheduleRedraw();

    // The cursor says "grabbable" over a handle - the same PointingHand every
    // chip and card in this app already wears, DERIVED here from the same
    // hover answer the brightening reads rather than toggled by whichever
    // event ran last. unsetCursor(), never an explicit arrow: the widget has
    // no cursor of its own to restore.
    const bool overHandle = myMoveGizmo.hoveredAxis() >= 0 ||
                            myRotateGizmo.hoveredAxis() >= 0 ||
                            myScaleGizmo.hoveredAxis() >= 0;
    if (overHandle)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
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
    scheduleRedraw();
}

double OcctViewWidget::segmentPixelDistance(const gp_Pnt& a, const gp_Pnt& b,
                                            const QPoint& point) const
{
    QPoint tail, head;
    if (!projectToScreen(a, tail)) return -1.0;
    if (!projectToScreen(b, head)) return -1.0;

    // Distance from the point to the projected segment, in logical pixels.
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
    return std::sqrt(nx * nx + ny * ny);
}

bool OcctViewWidget::arrowHit(const PullArrowRenderer& arrow, const QPoint& point) const
{
    if (!arrow.isShowing()) return false;
    const double distance = segmentPixelDistance(arrow.tail(), arrow.head(), point);
    return distance >= 0.0 && distance <= kHandleGrabPx;
}

int OcctViewWidget::moveGizmoAxisAt(const QPoint& point, bool* positive) const
{
    if (positive) *positive = true;
    if (!myMoveGizmo.isShowing()) return -1;

    // SIX handles, not three: the drawing puts a cone on each positive tip and
    // a hollow ball on each negative one, and a ball a user can see and cannot
    // grab is a control that lies about itself. Both ends of an arm resolve to
    // the same axis and the same world line - only the sign of the resulting
    // distance differs - so this stays one span per direction rather than a
    // second gesture.
    //
    // NEAREST handle wins, not the first one within tolerance: all six meet at
    // the hub, so on any camera several of them cross near the middle of the
    // screen and a first-match rule would hand the user whichever happens to
    // be checked first.
    //
    // The tested span starts a third of the way out (handleGrabStart()) for the
    // other half of the same problem: close to the hub every handle is within
    // tolerance of every pixel, and "nearest" there is decided by sub-pixel
    // noise. The inner third is dead, which is what makes the answer stable.
    int best = -1;
    bool bestPositive = true;
    double bestDistance = kHandleGrabPx;
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const bool plus = side == 0;
            // A handle whose tip falls inside the hub is not drawn and is not
            // grabbable - see MoveGizmoRenderer::handleDrawn(). Without this
            // an axis pointing at the eye collapses onto the hub and all six
            // handles claim every press on it.
            if (!myMoveGizmo.handleDrawn(axis, plus)) continue;
            const double distance =
                segmentPixelDistance(myMoveGizmo.handleGrabStart(axis, plus),
                                     myMoveGizmo.handleTip(axis, plus), point);
            if (distance < 0.0 || distance > bestDistance) continue;
            best = axis;
            bestPositive = plus;
            bestDistance = distance;
        }
    }
    if (positive) *positive = bestPositive;
    return best;
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

bool OcctViewWidget::measureAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at,
                                     double& raw)
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

    raw = parameter - drag.pressParam;
    return true;
}

bool OcctViewWidget::advanceAxisDrag(AxisDrag& drag, const gp_Lin& axis, const QPoint& at)
{
    double value = 0.0;
    if (!measureAxisDrag(drag, axis, at, value)) return false;
    // The same grid the outline points snap to, applied to the dragged
    // distance rather than to a position.
    if (mySnapEnabled && mySnapStep > 0.0)
        value = std::round(value / mySnapStep) * mySnapStep;
    if (std::fabs(value - drag.value) <= 1.0e-9) return false;

    drag.value = value;
    if (std::fabs(value) > 1.0e-9) drag.moved = true;
    return true;
}

// --- Magnet (Milestone 5) ---------------------------------------------------

void OcctViewWidget::setMagnetEnabled(bool on)
{
    myMagnetEnabled = on;
    // Mid-drag, honesty over continuity: candidates captured at the press
    // stop being consulted the moment the option goes, and the guide goes
    // with them.
    if (!on) {
        myMoveMagnetCandidates.clear();
        clearMagnetGuide();
    }
}

void OcctViewWidget::collectMagnetCandidates(int axis)
{
    myMoveMagnetCandidates.clear();
    myMoveMagnetAxis = axis;
    if (!myMagnetEnabled || axis < 0 || axis > 2 || myContext.IsNull()) return;

    // The Move gizmo stands on exactly one whole selected body - its
    // predicate says so - and that body is the one being dragged.
    const std::vector<int> selected = selectedSolidIds();
    if (selected.size() != 1) return;
    const int movingId = selected.front();

    struct Box {
        double lo[3];
        double hi[3];
        gp_Pnt centre;
    };
    auto boxOf = [](const TopoDS_Shape& shape, Box& out) {
        if (shape.IsNull()) return false;
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (box.IsVoid()) return false;
        Standard_Real x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        out.lo[0] = x0; out.lo[1] = y0; out.lo[2] = z0;
        out.hi[0] = x1; out.hi[1] = y1; out.hi[2] = z1;
        out.centre = gp_Pnt(0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1));
        return true;
    };

    const auto movingIt = mySolids.find(movingId);
    Box moving;
    if (movingIt == mySolids.end() || movingIt->second.IsNull() ||
        !boxOf(movingIt->second->Shape(), moving))
        return;
    const double movingFeatures[3] = {moving.lo[axis], 0.5 * (moving.lo[axis] + moving.hi[axis]),
                                      moving.hi[axis]};

    for (const auto& entry : mySolids) {
        if (entry.first == movingId || entry.second.IsNull()) continue;
        // A hidden body offers no alignment - the user cannot see what the
        // drag would be sticking to.
        if (!myContext->IsDisplayed(entry.second)) continue;
        Box other;
        if (!boxOf(entry.second->Shape(), other)) continue;
        const double targets[3] = {other.lo[axis], 0.5 * (other.lo[axis] + other.hi[axis]),
                                   other.hi[axis]};
        for (double target : targets) {
            for (double feature : movingFeatures) {
                MagnetCandidate candidate;
                candidate.value = target - feature;
                candidate.target = target;
                candidate.movingCentre = moving.centre;
                candidate.targetCentre = other.centre;
                myMoveMagnetCandidates.push_back(candidate);
            }
        }
    }
}

bool OcctViewWidget::magnetSnap(double raw, double& value, gp_Pnt& guideA, gp_Pnt& guideB) const
{
    if (!myMagnetEnabled || myMoveMagnetCandidates.empty()) return false;
    const double tolerance = kMagnetSnapPx * worldPerPixel();

    const MagnetCandidate* best = nullptr;
    double bestDistance = tolerance;
    for (const MagnetCandidate& candidate : myMoveMagnetCandidates) {
        const double distance = std::fabs(raw - candidate.value);
        // <= the tolerance so an alignment exactly at the reach still takes;
        // NEAREST wins among those inside it, first-seen on an exact tie, so
        // two alignments in reach cannot flicker between moves.
        if (distance <= tolerance && (!best || distance < bestDistance)) {
            best = &candidate;
            bestDistance = distance;
        }
    }
    if (!best) return false;

    value = best->value;
    // The guide runs through both bodies IN the alignment plane: both bbox
    // centres, their drag-axis coordinate replaced by the aligned one. The
    // moving centre's other two coordinates never change during an axis
    // drag, so the press-time capture is still where the body is.
    guideA = best->movingCentre;
    guideB = best->targetCentre;
    switch (myMoveMagnetAxis) {
        case 0: guideA.SetX(best->target); guideB.SetX(best->target); break;
        case 1: guideA.SetY(best->target); guideB.SetY(best->target); break;
        default: guideA.SetZ(best->target); guideB.SetZ(best->target); break;
    }
    // Two concentric bodies leave no line to draw; showMagnetGuide() clears
    // rather than inventing a direction, and the snap itself still holds.
    return true;
}

void OcctViewWidget::showMagnetGuide(const gp_Pnt& a, const gp_Pnt& b)
{
    if (myContext.IsNull()) return;
    if (a.Distance(b) < 1.0e-6) {
        clearMagnetGuide();
        return;
    }
    // Equal-guard: the guide only ever moves between alignments, so most
    // drag steps re-ask for the line already on screen.
    if (myMagnetGuideShown && myMagnetGuideA.IsEqual(a, 1.0e-9) &&
        myMagnetGuideB.IsEqual(b, 1.0e-9))
        return;

    clearMagnetGuide();
    // Overshoot past both centres so the line reads as a guide crossing the
    // bodies, not a connector between them - Photoshop's own look.
    const gp_Vec along(a, b);
    const gp_Vec overshoot = along.Normalized() * (0.25 * along.Magnitude());
    Handle(Graphic3d_ArrayOfSegments) segments = new Graphic3d_ArrayOfSegments(2);
    segments->AddVertex(a.Translated(-overshoot));
    segments->AddVertex(b.Translated(overshoot));

    Handle(SymmetryPlaneObject) guide = new SymmetryPlaneObject();
    guide->segments = segments;
    guide->colour = toOcctColor(Theme::accent());
    guide->width = 2.0;
    myContext->Display(guide, 0, -1, Standard_False);   // never pickable
    // The gizmo's own depth-cleared immediate layer, so the guide is visible
    // through the bodies it aligns - a guide the nearer body hides is no
    // guide during exactly the drags it exists for.
    myContext->SetZLayer(guide, myGizmoLayer != Graphic3d_ZLayerId_UNKNOWN
                                    ? myGizmoLayer
                                    : Graphic3d_ZLayerId_Topmost);
    myMagnetGuide = guide;
    myMagnetGuideShown = true;
    myMagnetGuideA = a;
    myMagnetGuideB = b;
    scheduleRedraw();
}

void OcctViewWidget::clearMagnetGuide()
{
    if (!myMagnetGuideShown && myMagnetGuide.IsNull()) return;
    if (!myContext.IsNull() && !myMagnetGuide.IsNull())
        myContext->Remove(myMagnetGuide, Standard_False);
    myMagnetGuide.Nullify();
    myMagnetGuideShown = false;
    scheduleRedraw();
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
            scheduleRedraw();
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
    // An equal-guard, because this runs on every frame of an orbit and a
    // rebuild is a real allocation.
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
    scheduleRedraw();
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
    gp_XYZ halfExtent(0.0, 0.0, 0.0);
    if (!box.IsVoid()) {
        Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        centre = gp_Pnt((xmin + xmax) * 0.5, (ymin + ymax) * 0.5, (zmin + zmax) * 0.5);
        halfExtent = gp_XYZ((xmax - xmin) * 0.5, (ymax - ymin) * 0.5, (zmax - zmin) * 0.5);
    }

    myMirrorPlacement.active = true;
    myMirrorPlacement.ids = ids;
    myMirrorPlacement.centre = centre;
    myMirrorPlacement.halfExtent = halfExtent;
    // World X, the same axis DocumentModel's own constructed symmetry plane
    // starts on (see its header) - but TANGENT to the selection rather than
    // through its centre. The centre default cut a lone body in half, which
    // pairWithMirror()'s straddle rule then skipped, so the gesture's own
    // headline flow could not confirm without a drag nothing told the user
    // to make. See beginMirrorPlacement()'s header comment.
    myMirrorPlacement.axis = 0;
    myMirrorPlacement.offset = mirrorPlacementTangentOffset(0);

    updateMirrorPlacementIndicator();
}

double OcctViewWidget::mirrorPlacementTangentOffset(int axis) const
{
    if (!myMirrorPlacement.active) return 0.0;
    switch (std::clamp(axis, 0, 2)) {
        case 1: return myMirrorPlacement.halfExtent.Y();
        case 2: return myMirrorPlacement.halfExtent.Z();
        default: return myMirrorPlacement.halfExtent.X();
    }
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
        scheduleRedraw();
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
    // different one - see the header comment. Re-placed TANGENT on the new
    // axis rather than zeroed, so the spawn rule holds on every axis a flip
    // can land on and a lone body never straddles its own flipped plane.
    myMirrorPlacement.offset = mirrorPlacementTangentOffset(axis);
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

    scheduleRedraw();
    myMirrorPlacementBuiltHalfSpan = halfSpan;
    myMirrorPlacementBuiltOrigin = origin0;
    myMirrorPlacementBuiltNormal = normal0;
}

bool OcctViewWidget::solidPresentationTransform(int id, gp_Trsf& out) const
{
    const auto it = mySolids.find(id);
    if (it == mySolids.end() || it->second.IsNull()) return false;
    out = it->second->LocalTransformation();
    return true;
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

    scheduleRedraw();
}

void OcctViewWidget::clearSketchPointMarkers()
{
    if (!myContext.IsNull()) {
        for (auto& marker : myPlacedMarkers) myContext->Remove(marker, Standard_False);
        if (!myFirstPointMarker.IsNull()) myContext->Remove(myFirstPointMarker, Standard_False);
    }
    myPlacedMarkers.clear();
    myFirstPointMarker.Nullify();
    scheduleRedraw();
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
    scheduleRedraw();
}

void OcctViewWidget::clearSketchCursorMarker()
{
    if (!myContext.IsNull() && !myCursorMarker.IsNull()) myContext->Remove(myCursorMarker, Standard_False);
    myCursorMarker.Nullify();
    scheduleRedraw();
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

    myContext->Deactivate(shape);

    // Auto is the only mode that activates TWO, and that is the whole
    // mechanism behind "the cursor decides": with edges and faces both in the
    // pick candidates, OCCT arbitrates between them on every MoveTo, and both
    // the hover and the click ask through that same MoveTo. See
    // kAutoEdgeTolerancePx on the header for why the edge wins the tie and
    // why no priority is set here.
    if (mySelectionMode == SelectionMode::Auto) {
        myContext->Activate(shape, kSelectionModeEdge);
        myContext->Activate(shape, kSelectionModeFace);
        return;
    }

    const int mode = mySelectionMode == SelectionMode::Face  ? kSelectionModeFace
                    : mySelectionMode == SelectionMode::Edge ? kSelectionModeEdge
                                                              : kSelectionModeWholeShape;
    myContext->Activate(shape, mode);
}

void OcctViewWidget::applySelectionTolerance()
{
    if (myContext.IsNull()) return;

    // Raised in Auto, OCCT's default in the seam modes. The manipulator-era
    // STAND-DOWN is gone with the manipulator itself (Phase 2 cleanup): the
    // custom gizmos register no selectable entities at all, so there is
    // nothing left for the raised tolerance to blur and the full 8-logical-
    // pixel edge reach holds with a gizmo up - the consequence the custom
    // gizmo was built to buy. The measured account of what the stand-down
    // was and what it cost lives in CLAUDE.md's selection section.
    //
    // Device pixels: the selector measures in the window's own pixel space,
    // which is what toDevicePixels() converts into, and at 150% an 8-logical-
    // pixel promise is 12 device pixels or it is not that promise.
    const int wanted =
        mySelectionMode == SelectionMode::Auto
            ? std::max(1, toDevicePixels(QPoint(kAutoCandidateTolerancePx, 0)).x())
            : myDefaultPixelTolerance;
    if (selectionPixelTolerance() == wanted) return;
    myContext->SetPixelTolerance(wanted);
}

void OcctViewWidget::setSelectionMode(SelectionMode mode)
{
    if (mode == mySelectionMode) return;

    mySelectionMode = mode;
    myDimension.clear();   // a hover annotation from the old mode means nothing in the new one
    // Everything a half-finished gesture remembers, in the one function that
    // owns that list - a mode change is exactly as much of a discontinuity as
    // a document swap, and keeping two copies of "what to forget" is how one
    // of them goes stale.
    resetPickGesture();
    if (myContext.IsNull()) return;

    // The tolerance belongs to the mode, so it moves with it - raised on the
    // way into Auto and put back on the way out. A tolerance left raised
    // behind Auto would make the three classic modes pick differently from
    // how they always have, which is the one thing this phase may not do.
    applySelectionTolerance();
    myContext->ClearSelected(Standard_False);
    for (auto& entry : mySolids) applySelectionMode(entry.second);
    scheduleRedraw();
    emit selectionChanged();
}

void OcctViewWidget::resetPickGesture()
{
    // See the header for the call sites and for why the kind lock itself is
    // absent from this list: it is derived from the live selection, so it
    // resets itself the moment the selection does.
    myAutoKindBeforeClick = PickKind::None;
    myAutoBodyPickTaken = false;
    myAutoRefusal.clear();
    myLastPickedEdge.Nullify();
    // The other swallow-the-trailing-release claim, and it belongs on the same
    // list for the same reason: a document swap between a cancelled Move drag
    // and the release that trails it would otherwise leave the flag armed to
    // eat the first click in the new document.
    myMoveDragCancelled = false;
}

int OcctViewWidget::selectionPixelTolerance() const
{
    if (myContext.IsNull() || myContext->MainSelector().IsNull()) return -1;
    // CustomPixelTolerance(), not PixelTolerance(). The latter is the
    // EFFECTIVE number OCCT picks with - the custom tolerance plus the
    // largest sensitivity any registered entity carries - so it moves when a
    // body is displayed and cannot answer "what did this class ask for". The
    // custom tolerance is the one thing this class writes, so it is the one
    // thing worth reading back.
    return myContext->MainSelector()->CustomPixelTolerance();
}

OcctViewWidget::PickKind OcctViewWidget::kindOfShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return PickKind::None;
    switch (shape.ShapeType()) {
        case TopAbs_EDGE: return PickKind::Edge;
        case TopAbs_FACE: return PickKind::Face;
        default:          return PickKind::Body;
    }
}

OcctViewWidget::PickKind OcctViewWidget::hoveredKind() const
{
    if (myContext.IsNull()) return PickKind::None;
    // HasDetected() first, always - DetectedInteractive()/DetectedShape()
    // dereference a null unguarded, and one such read crashed the app in a
    // single click (see CLAUDE.md's pitfalls).
    if (!myContext->HasDetected()) return PickKind::None;
    if (!myContext->HasDetectedShape()) return PickKind::Body;
    return kindOfShape(myContext->DetectedShape());
}

TopoDS_Shape OcctViewWidget::hoveredShape() const
{
    if (myContext.IsNull() || !myContext->HasDetected()) return TopoDS_Shape();
    if (!myContext->HasDetectedShape()) return TopoDS_Shape();
    return myContext->DetectedShape();
}

OcctViewWidget::PickKind OcctViewWidget::selectionKind() const
{
    if (myContext.IsNull()) return PickKind::None;

    // DERIVED, never stored. A remembered kind would be a second source of
    // truth over the selection, and it would have to be put back by hand on
    // every undo, delete, mode switch, context loss and programmatic
    // setSelectedSolids() - the drift CLAUDE.md's "derive state; never store
    // a cursor" rule is about. Asking the selection costs a short walk and
    // cannot be wrong.
    for (myContext->InitSelected(); myContext->MoreSelected(); myContext->NextSelected()) {
        if (!myContext->HasSelectedShape()) return PickKind::Body;
        const PickKind kind = kindOfShape(myContext->SelectedShape());
        if (kind != PickKind::None) return kind;
    }
    return PickKind::None;
}

QString OcctViewWidget::autoKindRefusalText(PickKind held, PickKind asked)
{
    // Names BOTH halves - what this selection is holding and what the click
    // asked for - because "that did nothing" is not a reason. Swept for the
    // banned vocabulary like any other copy this app writes: a face is a
    // face, an edge is an edge, and a whole one is a BODY, never a solid.
    const auto plural = [](PickKind kind) {
        switch (kind) {
            case PickKind::Edge: return tr("edges");
            case PickKind::Face: return tr("faces");
            case PickKind::Body: return tr("bodies");
            default:             return tr("nothing");
        }
    };
    if (held == PickKind::Body) {
        // The one asymmetric case, and it is asymmetric in the gesture rather
        // than in the rule: a body is taken by a double-click, so that is
        // what adds one too.
        return tr("Shift adds bodies to this selection — double-click a body to add it, "
                  "or click without Shift to pick %1 instead")
            .arg(plural(asked));
    }
    return tr("Shift adds %1 to this selection — click without Shift to pick %2 instead")
        .arg(plural(held), plural(asked));
}

void OcctViewWidget::preferDetectedEdge(const QPoint& cursor)
{
    if (mySelectionMode != SelectionMode::Auto || myContext.IsNull() || myView.IsNull())
        return;
    if (!myContext->HasDetected()) return;

    // NO EARLY RETURN ON "AN EDGE IS ALREADY DETECTED", and that line's
    // removal is Phase 2's other arbitration fix. Phase 1 stopped here
    // whenever OCCT had already chosen an edge, on the reasoning that an edge
    // is what this function exists to produce. But OCCT chooses among edges by
    // DEPTH, and on a body only a few tens of pixels across all four edges of
    // a face are candidates at once - so it routinely handed back an edge five
    // or six pixels from the cursor while the one the cursor was sitting
    // exactly on waited at a lower rank, and this function agreed because it
    // never looked. Measured: every click aimed at a small body's own
    // projected edge midpoint took a neighbouring edge instead, on four
    // consecutive candidates.
    //
    // Letting the loop below run in that case costs one walk of an already-
    // built candidate list and settles the question in the space the spec
    // states it in. When OCCT's own choice IS the nearest on screen - the
    // common case - the loop picks it again and the cycling loop at the end
    // returns on its first comparison, so nothing moves.

    // THE ARBITRATION, and it is ours rather than OCCT's for a MEASURED
    // reason. Raising the selector tolerance does put the edge into the
    // candidate list - that half works - but which candidate OCCT then
    // HIGHLIGHTS is decided by SelectMgr_SortCriterion::IsCloserDepth(),
    // which leads with depth, and a face sampled just inside its own boundary
    // is genuinely nearer than the edge bounding it whenever the face is
    // tilted away from the camera. Measured on a box's top face at a custom
    // tolerance of 8 device pixels: the edge sat at depth 1431.5 all the way
    // across, while the face ran 1429.6 at one pixel in to 1410.4 at six -
    // about 3.8 depth units per pixel on that obliquity - so OCCT handed the
    // hover to the face from ONE pixel out. The edge itself stayed a
    // candidate to six pixels and vanished at seven, so the selector's own
    // effective radius is about 0.75x the custom tolerance rather than equal
    // to it.
    //
    // Neither of the two obvious levers is the answer, and both were checked
    // against the same run rather than guessed at. SetPickClosest(false)
    // swaps the WHOLE selector to priority-first ordering (the picked list
    // above shows the priorities OCCT assigns: edge 7, face 5), which would
    // make an edge beat a face at any depth, on any body, across the whole
    // scene - x-ray picking. And the owners' own SetPriority() changes
    // nothing under the default ordering, because priority is only consulted
    // after the depth comparisons have already answered.
    //
    // A depth-based guard was written first and measured wrong for a reason
    // worth keeping: "the same depth within OCCT's own tolerances" put the
    // flip at 3 px on this face and would put it somewhere else on every
    // other obliquity, so the tolerance would not be a number at all. The
    // gate below is the spec's own sentence instead - "within a small PIXEL
    // tolerance of an edge" - measured in the space the sentence is about.
    // SelectMgr_SortCriterion::Point is the point on the entity the pick
    // resolved to, so projecting it gives the edge's own nearest pixel, and
    // the distance from the cursor to it is the tolerance exactly.
    //
    // The choice is handed back to OCCT through its own detected-cycling API,
    // which is what keeps "the highlight is the contract" true:
    // HilightNextDetected() moves myLastPicked, and the highlight,
    // HasDetectedShape(), DetectedShape() and SelectDetected() all read that
    // one field. There is no second path for a click to disagree with what is
    // glowing.
    const Handle(StdSelect_ViewerSelector3d)& selector = myContext->MainSelector();
    if (selector.IsNull()) return;
    const int picked = selector->NbPicked();
    if (picked < 2) return;

    // The body the cursor is actually on. An edge is only preferred over a
    // face of the SAME object, which is what keeps this from reaching through
    // one body to grab an edge of another standing behind it - the one piece
    // of the x-ray hazard that is worth spending a comparison on, since a
    // nearer body's edge is already the frontmost candidate and needs no
    // preference at all. An edge of the same body hidden BEHIND it can still
    // be preferred, and deliberately so: that is exactly what edge mode has
    // always done, and this phase is not the place to change it.
    const Handle(SelectMgr_EntityOwner) current = myContext->DetectedOwner();
    if (current.IsNull()) return;
    const Handle(SelectMgr_SelectableObject) onBody = current->Selectable();

    // THE NEAREST EDGE ON SCREEN WINS, not the first one within tolerance.
    //
    // Phase 1 took the first candidate in RANK order - which is depth order -
    // that fell inside the tolerance, on the reasoning that depth is what
    // edge mode has always sorted by. Phase 2 made this the app's only pick
    // and that reasoning stopped being enough: with several edges of one body
    // inside 8 px of the cursor at once, depth chose between them, and it
    // routinely chose an edge SIX pixels away over the one the cursor was
    // sitting exactly on. Measured on two small bodies whose top faces are a
    // few tens of pixels across: every click aimed at an edge's own projected
    // midpoint took a neighbouring edge instead.
    //
    // The spec's sentence decides it - "within a small PIXEL tolerance of an
    // edge, the edge glows" - and a rule stated in pixels should be resolved
    // in pixels. Ties keep the old behaviour exactly: the comparison is
    // strict, so among candidates at equal screen distance the first in rank
    // order still wins, which is the case that matters for one edge hidden
    // directly behind another (both project to the same pixel, and the nearer
    // one is still taken).
    Handle(SelectMgr_EntityOwner) target;
    double bestDistance = 0.0;
    for (int rank = 1; rank <= picked; ++rank) {
        const Handle(SelectMgr_EntityOwner)& owner = selector->Picked(rank);
        if (owner.IsNull() || owner->Selectable() != onBody) continue;
        const Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        if (brep.IsNull() || !brep->HasShape()) continue;
        if (brep->Shape().ShapeType() != TopAbs_EDGE) continue;
        QPoint onScreen;
        if (!projectToScreen(selector->PickedData(rank).Point, onScreen)) continue;
        const double dx = onScreen.x() - cursor.x();
        const double dy = onScreen.y() - cursor.y();
        const double distance = std::hypot(dx, dy);
        if (distance > double(kAutoEdgeTolerancePx)) continue;
        if (!target.IsNull() && distance >= bestDistance) continue;
        target = owner;
        bestDistance = distance;
    }
    if (target.IsNull()) return;

    // Bounded: HilightNextDetected() loops, and a target that is somehow not
    // in the detected sequence at all must leave OCCT's own choice standing
    // rather than spin. Standard_False - the frame is this class's to ask
    // for, and the caller asks for it once the owner has settled.
    for (int guard = 0; guard <= picked; ++guard) {
        if (myContext->DetectedOwner() == target) return;
        myContext->HilightNextDetected(myView, Standard_False);
    }
}

bool OcctViewWidget::selectDetectedBody(bool additive)
{
    if (myContext.IsNull() || !myContext->HasDetected()) return false;

    const Handle(AIS_InteractiveObject) hit = myContext->DetectedInteractive();
    if (hit.IsNull()) return false;
    for (const auto& entry : mySolids) {
        if (entry.second.get() != hit.get()) continue;
        // Auto activates edges and faces, never mode 0, so there is no
        // whole-shape owner among the pick candidates to SelectDetected. The
        // object's own global owner is the route that does not need one, and
        // it is the same call setSelectedSolids() has always made from inside
        // face and edge mode.
        if (!additive) myContext->ClearSelected(Standard_False);
        myContext->AddOrRemoveSelected(entry.second, Standard_False);
        return true;
    }
    return false;
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
    scheduleRedraw();
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

gp_Dir OcctViewWidget::liveCameraUp() const
{
    if (myView.IsNull()) return gp_Dir(0.0, 1.0, 0.0);
    return myView->Camera()->Up();
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
    //
    // And priority 1.5, this fix wave: while a SKETCH is in progress, the
    // pinned plane outranks the live camera too. onStartSketch() captures
    // faceOnOrthoPlane() BY VALUE into SketchController and pushes it to
    // setWorkPlane(), so clicks keep landing on it however the camera moves
    // - but this line asked myWorkPlaneLocked, which is true only for a
    // locked FACE, so orbiting away from Front mid-sketch dropped the drawn
    // grid back to the ground while the outline was still being built on
    // XZ. The status label already handles exactly this case and says so in
    // its own comment; the grid has to follow the same rule or it stops
    // showing where the next click lands, which is its entire job. Task
    // 5.1's fix round unified the two planes' DERIVATION; this unifies
    // their LIFETIME.
    const gp_Pln basePlane =
        (myWorkPlaneLocked || mySketchMode) ? mySketchPlane : faceOnOrthoPlane();

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
    // A FRAME IS ONLY OWED WHEN THE ANNOTATION ACTUALLY CHANGED, and this
    // function is the hottest caller of scheduleRedraw() in the file: it runs
    // from the hover branch of every mouse move, so an unconditional
    // Invalidate()+update() here means idle cursor motion over the viewport
    // discards and rebuilds a whole OCCT frame per mouse event - in Solid and
    // Face mode, where the very first branch below is a clear() over an
    // already-empty annotation and does nothing at all. The first cut of this
    // migration did exactly that (a PresentOnReturn guard on every exit) and
    // it was wrong.
    //
    // DimensionRenderer::show()/clear() now answer "did anything move" - the
    // equal-guard compares the span, the extension normal and worldPerPixel,
    // because all three are built into the annotation - and every branch here
    // simply forwards that answer. One exit shape, no unconditional frame.
    const auto present = [this](bool changed) {
        if (changed) scheduleRedraw();
    };

    // Suppressed while the bevel arrow's value chip is up: two annotations on
    // one edge is noise, and the chip is the more specific of the two. See
    // setEdgeDimensionSuppressed(), which MainWindow drives off the same
    // predicate that raises the arrow.
    // Auto joins Edge here rather than getting an annotation rule of its own:
    // in Auto the hover already resolves to an edge or to a face, so the two
    // reads below (the detected edge, then the single selected one) answer
    // "is an edge what this is about right now" exactly as they do in edge
    // mode, and a hovered face simply produces no edge to measure. The three
    // classic modes are untouched - Solid and Face still clear.
    const bool edgesAreMeasurable = mySelectionMode == SelectionMode::Edge ||
                                    mySelectionMode == SelectionMode::Auto;
    if (myEdgeDimensionSuppressed || !edgesAreMeasurable ||
        myContext.IsNull() || myView.IsNull()) {
        present(myDimension.clear());
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
        present(myDimension.clear());
        return;
    }

    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2);
    if (v1.IsNull() || v2.IsNull()) {
        present(myDimension.clear());
        return;
    }

    const gp_Pnt from = BRep_Tool::Pnt(v1);
    const gp_Pnt to = BRep_Tool::Pnt(v2);
    const gp_Vec along(from, to);
    if (along.Magnitude() < 1.0e-7) {
        present(myDimension.clear());
        return;
    }

    // Extension lines run sideways in the screen plane - perpendicular to
    // both the edge and the direction we are looking - so they read the same
    // whichever way the camera happens to be turned.
    gp_Vec sideways = along.Crossed(gp_Vec(myView->Camera()->Direction()));
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(0.0, 0.0, 1.0).Crossed(along);
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(1.0, 0.0, 0.0);

    present(myDimension.show(from, to, gp_Dir(sideways), worldPerPixel()));
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
    scheduleRedraw();
    emit selectionChanged();
}

bool OcctViewWidget::saveSnapshot(const QString& path)
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
    if (myView.IsNull()) return false;

    myView->Redraw();

    if (!myRenderModeActive) return myView->Dump(path.toUtf8().constData()) == Standard_True;

    // Render mode doubles the export - the view's own DEVICE-pixel size
    // (toDevicePixels()'s own conversion, so a 150% display's screenshot is
    // 2x its OWN already-scaled pixel count, not 2x the logical widget
    // size), through ToPixMap() rather than Dump(): it renders an offscreen
    // buffer of the requested target size directly, with no window resize
    // needed - Dump() has no size parameter of its own to hand it one.
    // The path-traced tier exports the ON-SCREEN accumulation buffer, at
    // 1x, rather than the 2x offscreen render every other tier gets - and
    // that trade was forced by measurement, not chosen.
    //
    // ToPixMap() renders an offscreen buffer of the requested size, and for
    // a path-traced view it renders exactly ONE sample per pixel and then
    // stops: six successive calls came back with the identical floor pixel,
    // bit for bit, so it neither accumulates across calls nor varies within
    // one. It is not an under-sampled frame that more calls would improve;
    // it is a deterministic single sample. `SamplesPerPixel` and
    // `AdaptiveScreenSampling` were both swept against it (0/4/16/64/128 x
    // on/off, ten combinations) and every one produced the same pixel, so
    // nothing in Graphic3d_RenderingParams reaches it either. Measured on
    // this scene: the offscreen export read 140 where the on-screen buffer
    // at rest reads 194 - the export was a third darker than the picture
    // the user was looking at when they asked for it.
    //
    // Dump() reads the on-screen framebuffer, which DOES accumulate - it is
    // the same buffer every measuring probe in this file already relies on -
    // so awaitPathTracingConvergence() drives it and Dump() exports what the
    // user actually sees. Half the linear resolution of the other tiers, and
    // the only alternative on offer was twice the resolution of the wrong
    // image.
    if (myRenderTier == RenderTier::PathTracing) {
        awaitPathTracingConvergence();
        const bool ok = myView->Dump(path.toUtf8().constData()) == Standard_True;
        // READING THE BUFFER EMPTIES IT, and the user is still looking at it.
        // awaitPathTracingConvergence()'s own comment already records that a
        // Dump() restarts OCCT's accumulation - that is why the export drives a
        // fixed pass count instead of sampling until it converges. The
        // consequence nobody had followed through: the frame left ON SCREEN
        // after an export is a single sample, so the picture visibly goes to
        // grain the moment a screenshot is taken. Measured on the composited
        // window straight after an export - median (147,145,144) against the
        // (196,195,192) it had been, grain 6 against 1.
        //
        // Restarting the convergence is the whole repair: the burst window
        // polishes it back within a couple of seconds and the idle refinement
        // carries on from there. Without it the recovery would depend on
        // whether any budget happened to be left, and an export taken from a
        // long, settled rest - exactly when a user takes one - is the case
        // where none is.
        myAccumulationDepth = 0;
        if (myRenderModeActive) startPathTracingConvergence();
        return ok;
    }

    const QPoint deviceSize = toDevicePixels(QPoint(width(), height()));
    Image_AlienPixMap pixmap;
    if (!myView->ToPixMap(pixmap, deviceSize.x() * 2, deviceSize.y() * 2)) return false;
    return pixmap.Save(path.toUtf8().constData());
}

void OcctViewWidget::awaitPathTracingConvergence()
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
    if (myView.IsNull() || !myRenderModeActive || myRenderTier != RenderTier::PathTracing)
        return;

    // This drives a fixed number of accumulation passes under a hard time
    // cap rather than watching two successive samples converge, and that is
    // a measured decision, not a shortcut.
    //
    // There is no cheap signal to watch. OpenGl_View::myAccumFrames is
    // exactly the number this wants and it is `protected` with no accessor,
    // reachable only by subclassing a class the graphic driver constructs;
    // Graphic3d_RenderingParams::SamplesPerPixel is an INPUT, and sweeping it
    // 0/4/16/64/128 changed no pixel of the export at all.
    //
    // Which leaves reading pixels - and **reading the pixels resets the
    // buffer being read**. A sampling loop was built first and measured: four
    // redraws, Dump, compare, repeat, with a 0.5/255 epsilon over a
    // whole-frame strided mean. It exported at 136 against an at-rest 194 -
    // barely better than the 140 it replaced - because every Dump() restarted
    // the accumulation, so each iteration measured a fresh four-sample frame,
    // every iteration agreed with the last to well inside the epsilon, and
    // the loop "converged" on the second pass. The tell is in this file's own
    // probes: probeRenderShadowRatio() reads 194 doing kMeasurementSettlePasses
    // redraws and ONE Dump, and would not if a Dump were free.
    //
    // So the pass count is the criterion. kMeasurementSettlePasses is where
    // the running mean was measured to settle (that is what its own comment
    // records); doubling it costs a fraction of a second on any GPU that
    // reached this tier at all and buys the variance reduction a still image
    // is judged on. The time cap is what a pathological scene hits instead of
    // hanging on a dialog the user already dismissed.
    QElapsedTimer timer;
    timer.start();
    constexpr int kExportAccumulationPasses = kMeasurementSettlePasses * 2;
    for (int i = 0; i < kExportAccumulationPasses; ++i) {
        if (timer.elapsed() >= kExportConvergenceCapMs) break;
        myView->Redraw();
    }
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
    // Screen-sized things follow the zoom here, before the redraw below
    // carries them. (The body gizmos need no call of their own: MoveTool
    // drives their rebuild from cameraChanged, and their pose cache no-ops
    // any move that does not change what they draw.)
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
    scheduleRedraw();

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
    // upVector() no longer degenerates there. eyePosition() is insensitive
    // to azimuth exactly overhead, but upVector() still reads it, so
    // leaving azimuth at whatever it happened to be (as this used to)
    // rotated the view about its own axis - the same tilt an orbit
    // approaching the pole would produce, except landed on arbitrarily
    // depending on where the camera was before "Top" was pressed. Milestone
    // 5 item 4's fix: force the squared azimuth so Top always shows world
    // +X right, +Y up, the same every time - see
    // CameraController::kTopBottomSquaredAzimuthDeg's own comment.
    s.azimuthDeg = CameraController::kTopBottomSquaredAzimuthDeg;
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

    // Milestone 5, item 6: the body boundary lines' width is a live token
    // too - re-applied and Redisplay'd here exactly like the highlight
    // styles above. Skipped while render mode is active: entry already
    // forced every body's boundary off (setRenderModeEnabled()) and a theme
    // edit made mid-render must not undo that - the exit path there is what
    // restores this state, at whatever width is live at that moment.
    if (!myRenderModeActive) {
        const double edgeWidth = Theme::edgeWidthPx();
        for (auto& entry : mySolids) {
            if (entry.second.IsNull()) continue;
            entry.second->Attributes()->SetFaceBoundaryDraw(edgeWidth > 0.0);
            if (edgeWidth > 0.0) {
                entry.second->Attributes()->SetFaceBoundaryAspect(
                    new Prs3d_LineAspect(Quantity_NOC_GRAY30, Aspect_TOL_SOLID, edgeWidth));
            }
            myContext->Redisplay(entry.second, Standard_False);
        }
    }

    // The outline's own line width AND colour, live too - both display
    // sites (displayOutline(), setPreview()) share Theme::sketchLineWidthPx()
    // and Theme::outlineLineColour(), so a committed outline item still on
    // screen follows an edit to either the same way a body's boundary does
    // above.
    {
        const double sketchWidth = Theme::sketchLineWidthPx();
        const Quantity_Color outlineColour = toOcctColor(Theme::outlineLineColour());
        for (auto& entry : myOutlines) {
            if (entry.second.IsNull()) continue;
            entry.second->SetWidth(sketchWidth);
            entry.second->SetColor(outlineColour);
            myContext->Redisplay(entry.second, Standard_False);
        }
    }

    // The same problem one presentation over: both drag arrows bake
    // Theme::accent() into the AIS object at build time and their show()
    // early-outs on an unchanged pose, so a live arrow kept the old accent.
    // Both are no-ops when nothing is showing. The edge-length annotation
    // needs no call here - MainWindow already drives refreshDimension() from
    // appStateChanged, which onThemeChanged() ends by emitting.
    myPullArrow.reapplyTheme();
    myBevelArrow.reapplyTheme();
    // And the Move gizmo, which bakes the three axis tokens in the same way -
    // its forced rebuild is also what carries a Gizmo size edit onto a live
    // gizmo, since buildStrokes() reads Theme::gizmoScale().
    myMoveGizmo.reapplyTheme();
    myRotateGizmo.reapplyTheme();
    myScaleGizmo.reapplyTheme();

    scheduleRedraw();
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

    applyRenderBackgroundColourForTier(myRenderTier);
    if (!myRenderModeActive) return;
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
    applyRenderFloorMaterialForTier(usesPbrMaterials(myRenderTier));
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
        // The path-traced tier's own material. Fix rounds 1 and 3 tried a
        // pure-Emission floor and then a pure-diffuse-albedo one, measured
        // BOTH as functionally black (delta ~190/255 from the backdrop),
        // and concluded from two opposite theories giving one answer that
        // the GI pass was not lighting this geometry at all. Two theories
        // out of two were right about the material's SHAPE and wrong about
        // which field the path tracer reads: neither of them, and not the
        // classic reflectance set either. It reads Graphic3d_BSDF, which
        // nothing here had ever written, so the floor and every body
        // integrated an all-zero BSDF - see applyRenderBodyMaterials() and
        // kPathTracingEnabled for the whole finding, including the
        // reversed-face and box-slab controls that ruled the geometry out.
        //
        // The surface itself is what a physically based renderer expects a
        // studio floor to be, and now that the BSDF says so it behaves like
        // one: albedo AT the backdrop colour, no emission, rough, not
        // metallic, grounding the shot with a real integrated shadow rather
        // than a shadow map's approximation of one. MEASURED, against the
        // studio rig this round calibrated (see kPathTracingAmbientGain):
        // the lit floor reads 194 against a 194 backdrop token, and the
        // darkest point in the frame 0.65 of it.
        Graphic3d_PBRMaterial floorPbr;
        floorPbr.SetMetallic(0.0f);
        floorPbr.SetRoughness(0.95f);
        floorPbr.SetColor(Quantity_Color(floorColour.redF(), floorColour.greenF(),
                                         floorColour.blueF(), Quantity_TOC_RGB));
        floorPbr.SetEmission(NCollection_Vec3<float>(0.0f, 0.0f, 0.0f));
        material.SetPBRMaterial(floorPbr);
        // CreateDiffuse() rather than the bodies' CreateMetallicRoughness():
        // the floor is a matte Lambertian sweep with no specular lobe worth
        // carrying, and a diffuse BSDF is the exact description of that. Its
        // weight is the same channel values the PBR albedo above carries, so
        // the two descriptions of this one surface agree by construction.
        material.SetBSDF(Graphic3d_BSDF::CreateDiffuse(
            NCollection_Vec3<float>(static_cast<float>(floorColour.redF()),
                                    static_cast<float>(floorColour.greenF()),
                                    static_cast<float>(floorColour.blueF()))));
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
    applyRenderLightsForTier(myRenderTier);
}

void OcctViewWidget::applyRenderLightsForTier(RenderTier tier)
{
    if (myRenderSavedLights.empty()) return;   // render mode is off - nothing to move
    const bool pathTracing = tier == RenderTier::PathTracing;
    // Gains, not replacements: myRenderLightStrength still opens and closes
    // the key by the factor the Light strength control has always applied,
    // and the tier gain is what stops OCCT's modeling-legibility rig from
    // clipping a physically integrated frame to white. Every number here was
    // read off sampled Dump() pixels - see the constants' own comments.
    const double keyGain = pathTracing ? kPathTracingKeyGain : 1.0;
    const double ambientGain = pathTracing ? kPathTracingAmbientGain : kRasterAmbientGain;
    const gp_Dir direction = studioKeyDirectionForAzimuth(myRenderLightAngleDeg);
    for (auto& saved : myRenderSavedLights) {
        saved.light->SetDirection(direction);
        saved.light->SetIntensity(static_cast<Standard_ShortReal>(
            saved.intensity * myRenderLightStrength * keyGain));
        // A cone angle only means anything to the path tracer, which
        // samples it as an area light and draws the penumbra that follows.
        // Every other tier gets the angle it came in with, which is the
        // hard directional light the shadow map and the Whitted pass both
        // expect.
        saved.light->SetSmoothAngle(static_cast<Standard_ShortReal>(
            pathTracing ? kPathTracingKeySmoothAngleRad : saved.smoothness));
    }
    for (auto& saved : myRenderSavedAmbients)
        saved.light->SetIntensity(static_cast<Standard_ShortReal>(saved.intensity * ambientGain));
    if (!myViewer.IsNull()) myViewer->UpdateLights();
}

void OcctViewWidget::applyRenderBackgroundColourForTier(RenderTier tier)
{
    if (myView.IsNull()) return;
    if (!myRenderModeActive) {
        myView->SetBackgroundColor(toOcctColor(Theme::viewport()));
        return;
    }
    const Quantity_Color backdrop = toOcctColor(renderBackdropColourImpl());
    if (tier != RenderTier::PathTracing) {
        myView->SetBackgroundColor(backdrop);
        return;
    }
    // Path tracing encodes its output to sRGB and applies that encode to the
    // background colour as well, so the token that rasterizes correctly
    // path-traces roughly 33/255 too light and draws a horizon across the
    // top of the shot. kPathTracingBackdropGain is the measured
    // pre-scale that lands it back on the token - see its own comment.
    myView->SetBackgroundColor(Quantity_Color(backdrop.Red() * kPathTracingBackdropGain,
                                              backdrop.Green() * kPathTracingBackdropGain,
                                              backdrop.Blue() * kPathTracingBackdropGain,
                                              Quantity_TOC_RGB));
}

void OcctViewWidget::redrawRenderModeLive()
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
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
        // The same try/catch discipline every other Redraw() on a ray-traced
        // path in this file carries, and for a sharper reason here: an
        // escape would leave params.Method stranded at RASTERIZATION under a
        // tier that claims to be ray-traced, for the rest of the session.
        // The restore below runs whether the redraw threw or not.
        try {
            myView->Redraw();
        } catch (const Standard_Failure&) {
        }
        params.Method = wasMethod;
    }
    if (!myContext.IsNull()) myContext->UpdateCurrentViewer();
    try {
        myView->Redraw();
    } catch (const Standard_Failure&) {
    }
}

bool OcctViewWidget::renderMaterialControlsApply() const
{
    return usesPbrMaterials(myRenderTier);
}

void OcctViewWidget::setRenderWood(bool on)
{
    if (myRenderWood == on) return;
    myRenderWood = on;
    if (!myRenderModeActive || myContext.IsNull()) return;
    // Re-dress the bodies for the tier that is actually up - the same pair
    // applyRenderTier() chooses between, so wood cannot invent a third
    // material path.
    if (usesPbrMaterials(myRenderTier))
        applyRenderBodyMaterials();
    else
        clearRenderBodyMaterials();
    redrawRenderModeLive();
}

void OcctViewWidget::setRenderTextureFile(const QString& path)
{
    if (myWoodTextureFile == path) return;
    myWoodTextureFile = path;
    // The cache is per-source; the next ensureWoodTexture() rebuilds.
    myWoodTexture.Nullify();
    if (myRenderModeActive && myRenderWood && !myContext.IsNull()) {
        if (usesPbrMaterials(myRenderTier))
            applyRenderBodyMaterials();
        else
            clearRenderBodyMaterials();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::ensureWoodTexture()
{
    if (!myWoodTexture.IsNull()) return;

    // The user's own image first, when one is chosen - the procedural plank
    // below is the fallback, not the point.
    if (!myWoodTextureFile.isEmpty()) {
        QImage file(myWoodTextureFile);
        if (!file.isNull()) {
            // Capped at 2048: the user's 4K oak is 16.7M pixels through the
            // per-pixel copy below and just as many texels for a texture
            // that repeats every ~300 mm - half the side keeps every visible
            // detail at this tile size and a quarter of both costs.
            if (file.width() > 2048 || file.height() > 2048)
                file = file.scaled(2048, 2048, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
            const QImage rgb = file.convertToFormat(QImage::Format_RGB888);
            Handle(Image_PixMap) filePix = new Image_PixMap();
            if (filePix->InitTrash(Image_Format_RGB, rgb.width(), rgb.height())) {
                for (int y = 0; y < rgb.height(); ++y) {
                    for (int x = 0; x < rgb.width(); ++x) {
                        const QColor c = rgb.pixelColor(x, y);
                        filePix->SetPixelColor(x, y,
                                              Quantity_ColorRGBA(float(c.redF()),
                                                                 float(c.greenF()),
                                                                 float(c.blueF()), 1.0f));
                    }
                }
                Handle(Graphic3d_Texture2D) fileTexture = new Graphic3d_Texture2D(filePix);
                fileTexture->GetParams()->SetModulate(Standard_True);
                fileTexture->GetParams()->SetRepeat(Standard_True);
                // NO params scale - the first ship carried 1/300 on a
                // millimetre-UV assumption, and the user's pixel corrected
                // it: AIS_Shape maps a texture across each face's NORMALIZED
                // parametric space (its own header: "parametrized in
                // (0,1)x(0,1)"), so that scale crushed sampling into a
                // single corner texel and every wood rendered as one flat
                // brown. At identity each face wears the image once - a
                // veneer sheet per panel, which is how furniture is actually
                // faced.
                myWoodTexture = fileTexture;
                return;
            }
        }
        // A file that fails to load falls through to the procedural grain
        // rather than to no material at all.
    }

    // A procedural plank: wavy longitudinal grain bands with fine per-pixel
    // variation, drawn once into a QImage and handed to OCCT as an
    // Image_PixMap - no asset, no file, nothing to deploy. 512 wraps
    // seamlessly enough at the repeat scale below that seams read as grain.
    constexpr int kSize = 512;
    QImage image(kSize, kSize, QImage::Format_RGB888);
    const QColor dark(0x6b, 0x48, 0x2a);
    const QColor mid(0x8f, 0x6a, 0x45);
    const QColor light(0xa8, 0x82, 0x58);
    auto mix = [](const QColor& a, const QColor& b, double t) {
        return QColor(int(a.red() + (b.red() - a.red()) * t),
                      int(a.green() + (b.green() - a.green()) * t),
                      int(a.blue() + (b.blue() - a.blue()) * t));
    };
    // Deterministic hash noise, so every session grows the same tree.
    auto noise = [](int x, int y) {
        unsigned n = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return double((n ^ (n >> 16)) & 0xffff) / 65535.0;
    };
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            // Grain runs along Y: bands are a function of X, wobbled by a
            // slow sine of Y and a touch of noise so no two lines match.
            const double wobble = 6.0 * std::sin(y * 0.024 + x * 0.01) +
                                  2.0 * std::sin(y * 0.11);
            const double band = std::sin((x + wobble) * 0.55) * 0.5 + 0.5;
            const double fine = noise(x, y) * 0.18;
            double t = band * 0.75 + fine;
            QColor c = t < 0.5 ? mix(dark, mid, t * 2.0) : mix(mid, light, (t - 0.5) * 2.0);
            // Occasional darker vessel line.
            if (noise(x / 3, y / 7) > 0.978) c = c.darker(130);
            image.setPixelColor(x, y, c);
        }
    }

    Handle(Image_PixMap) pix = new Image_PixMap();
    if (!pix->InitTrash(Image_Format_RGB, kSize, kSize)) return;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const QColor c = image.pixelColor(x, y);
            pix->SetPixelColor(x, y,
                              Quantity_ColorRGBA(float(c.redF()), float(c.greenF()),
                                                 float(c.blueF()), 1.0f));
        }
    }

    Handle(Graphic3d_Texture2D) texture = new Graphic3d_Texture2D(pix);
    // Identity scale, for the reason the file branch above records: face
    // UVs are normalized, so any down-scale samples one texel and reads as
    // flat paint. One grain image per face.
    texture->GetParams()->SetModulate(Standard_True);
    texture->GetParams()->SetRepeat(Standard_True);
    myWoodTexture = texture;
}

void OcctViewWidget::applyWoodTexture(bool on)
{
    if (myContext.IsNull()) return;
    if (on) ensureWoodTexture();
    if (on && myWoodTexture.IsNull()) return;
    for (auto& entry : mySolids) {
        if (entry.second.IsNull()) continue;
        // Own aspect first, never the drawer link's - a texture written into
        // the shared default would dress every future presentation in wood.
        entry.second->Attributes()->SetupOwnShadingAspect();
        const Handle(Graphic3d_AspectFillArea3d) aspect =
            entry.second->Attributes()->ShadingAspect()->Aspect();
        if (on) {
            aspect->SetTextureMap(myWoodTexture);
            aspect->SetTextureMapOn(true);
        } else {
            aspect->SetTextureMapOn(false);
        }
        myContext->Redisplay(entry.second, Standard_False);
    }
}

void OcctViewWidget::setRenderSurfaceRoughness(double roughness01)
{
    myRenderRoughness = std::clamp(roughness01, 0.0, 1.0);
    // No-op on Shadows/Plain by design - see this setter's own header
    // comment. isRayTracedTier() is the one written-down copy of "which
    // tiers are PBR", reused rather than re-tested here.
    if (myRenderModeActive && usesPbrMaterials(myRenderTier) && !myContext.IsNull()) {
        applyRenderBodyMaterials();
        redrawRenderModeLive();
    }
}

void OcctViewWidget::setRenderMetal(double metallic01)
{
    myRenderMetallic = std::clamp(metallic01, 0.0, 1.0);
    if (myRenderModeActive && usesPbrMaterials(myRenderTier) && !myContext.IsNull()) {
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
    const bool pbr = usesPbrMaterials(tier);
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
    //
    // The user-feedback round narrowed `rayTraced` to `pbr` here - see
    // usesPbrMaterials() for the measurement that moved it. Whitted ray
    // tracing joins the rasterized tiers on Phong and the Milestone-3
    // materials; only path tracing, which actually integrates light and
    // actually tone-maps, keeps PBR.
    params.ShadingModel =
        pbr ? Graphic3d_TypeOfShadingModel_Pbr : Graphic3d_TypeOfShadingModel_Phong;
    params.ToneMappingMethod =
        pbr ? Graphic3d_ToneMappingMethod_Filmic : Graphic3d_ToneMappingMethod_Disabled;

    // The material half of the same scoping - see each function's own
    // comment. Every tier switch (including the temporary ones the
    // measurement probes below make) re-applies the right pair, so a probe
    // that tries PathTracing then falls back to Shadows leaves both the
    // floor and the bodies in the material that tier actually needs.
    if (pbr) {
        applyRenderBodyMaterials();
    } else {
        clearRenderBodyMaterials();
    }
    applyRenderFloorMaterialForTier(pbr);

    // 4x the 1024 default while shadow-mapping, put back for every other
    // tier. At 1024 the shadow's edge on the floor is visibly blocky - the
    // map is stretched across the whole scene including the floor, so the
    // floor is exactly what made the default resolution stop being enough.
    params.ShadowMapResolution = (tier == RenderTier::Shadows) ? 4096 : 1024;
    setLightsCastShadows(tier == RenderTier::Shadows);

    // The studio rig and the clear colour are per-tier too since the
    // user-feedback round - a path-traced frame needs a fraction of the
    // ambient the rasterized ones want, a cone angle on the key, and a
    // pre-scaled backdrop (see kPathTracingBackdropGain). Both are applied
    // for the tier being SWITCHED TO rather than myRenderTier, which is
    // what makes the tier probe and every forcing measurement probe measure
    // the rig the user would actually be shown.
    applyRenderLightsForTier(tier);
    applyRenderBackgroundColourForTier(tier);
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

OcctViewWidget::LightRigProbe OcctViewWidget::lightRigProbe() const
{
    LightRigProbe probe;
    if (myViewer.IsNull()) return probe;
    bool haveKey = false;
    bool haveAmbient = false;
    for (const Handle(Graphic3d_CLight)& light : myViewer->ActiveLights()) {
        if (!haveKey && light->Type() == Graphic3d_TypeOfLightSource_Directional) {
            probe.keyIntensity = light->Intensity();
            probe.keySmoothness = light->Smoothness();
            probe.keyHeadlight = light->IsHeadlight();
            haveKey = true;
        } else if (!haveAmbient && light->Type() == Graphic3d_TypeOfLightSource_Ambient) {
            probe.ambientIntensity = light->Intensity();
            haveAmbient = true;
        }
    }
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
    material.SetColor(myRenderWood ? kWoodUnderTexture
                                   : Quantity_Color(0.70, 0.70, 0.68, Quantity_TOC_RGB));
    Graphic3d_PBRMaterial pbr;
    // Wood swaps the albedo and nothing else - roughness and metal stay the
    // user's own sliders, so a satin-varnished or a raw plank both remain
    // one drag away. NEAR-WHITE, not kWoodTone: the path tracer multiplies
    // the sampled grain by this albedo, and a brown-times-brown red-shifted
    // the user's oak (measured against the Shadows tier's correct colour).
    pbr.SetColor(myRenderWood ? kWoodUnderTexture
                              : Quantity_Color(0.55, 0.55, 0.53, Quantity_TOC_RGB));
    pbr.SetMetallic(static_cast<float>(myRenderMetallic));
    pbr.SetRoughness(static_cast<float>(myRenderRoughness));
    material.SetPBRMaterial(pbr);
    // The line whose absence made every path-traced frame black.
    // Graphic3d_MaterialAspect carries a THIRD description of a surface
    // beside the classic reflectance colours and the PBR block - a
    // Graphic3d_BSDF - and OCCT's path tracer reads that one and nothing
    // else. SetPBRMaterial() is an inline that assigns myPBRMaterial, so a
    // material built the way this function built it left myBSDF at its
    // default-constructed all-zero state, and an all-zero BSDF returns zero
    // radiance for every incoming ray. Whitted ray tracing and both
    // rasterized tiers never noticed, because they read the other two
    // descriptions - which is exactly why this looked like a floor-specific
    // GI defect for three fix rounds. CreateMetallicRoughness() is OCCT's
    // own conversion, so the BSDF cannot drift from the PBR block above it.
    material.SetBSDF(Graphic3d_BSDF::CreateMetallicRoughness(pbr));
    // The grain itself - after the material below, see applyWoodTexture().
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
    applyWoodTexture(myRenderWood);
}

void OcctViewWidget::clearRenderBodyMaterials()
{
    if (myContext.IsNull()) return;

    // The wood branch (Milestone 5): on the Phong tiers wood is a classic
    // diffuse in the same tone the PBR albedo wears, under the same grain
    // texture - so Quick and Deep disagree about shading, never about the
    // material. Only while render mode is actually up: the EXIT path runs
    // through here too on a tier switch, and the full revert below is what
    // modeling must always get back.
    if (myRenderModeActive && myRenderWood) {
        Graphic3d_MaterialAspect wood(Graphic3d_NameOfMaterial_UserDefined);
        // The same neutral the PBR path wears under the grain - the two
        // tiers must disagree about shading, never about the material.
        wood.SetColor(kWoodUnderTexture);
        for (auto& entry : mySolids) {
            entry.second->SetMaterial(wood);
            myContext->Redisplay(entry.second, Standard_False);
        }
        applyWoodTexture(true);
        return;
    }
    // The Shadows/Plain half of the pair - fix round 2's scoping. A plain
    // UnsetMaterial() per solid is a complete revert to whatever stood
    // before render mode touched it, on the exact terms
    // applyRenderBodyMaterials()'s own header comment already establishes:
    // no code path outside these two ever calls SetMaterial() on a body.
    // Redisplay() for the same reason applyRenderBodyMaterials() now
    // carries one on its own SetMaterial() call - a presentation change
    // that is not followed by one is not guaranteed to reach the next
    // redraw.
    applyWoodTexture(false);
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
        connect(myPathTracingRefineTimer, &QTimer::timeout, this, [this]() {
            // Re-checked on every tick rather than trusted from whenever the
            // timer was started - render mode can exit or the tier can only
            // ever have been cached once, but this guards the same way every
            // other render-mode teardown in this file does, defensively
            // rather than because a real path to it missing stopCall was
            // found.
            if (!myRenderModeActive || myRenderTier != RenderTier::PathTracing ||
                myView.IsNull()) {
                stopPathTracingConvergence();
                return;
            }
            if (myPathTracingRefineTicksLeft > 0) {
                --myPathTracingRefineTicksLeft;
                // Handing over to the idle rate: the burst is spent, and the
                // interval widens for the rest of the polish. Done here rather
                // than on a second timer so there is one tick, one budget and
                // one place a revert can be caught.
                if (myPathTracingRefineTicksLeft == 0)
                    myPathTracingRefineTimer->setInterval(pathTracingIdleIntervalMs());
            } else if (myPathTracingIdleTicksLeft > 0) {
                --myPathTracingIdleTicksLeft;
            } else {
                stopPathTracingConvergence();
                return;
            }
            // NOT scheduleRedraw(): its Invalidate() restarts the very
            // accumulation this tick exists to advance. See
            // scheduleAccumulationFrame()'s own comment for the measurement
            // that found it.
            scheduleAccumulationFrame();
        });
    }
    // Both budgets restarted, not merely topped up, so a camera move mid-
    // convergence gets the full window again, matching the path tracer's own
    // accumulation buffer starting over the instant the view actually changes.
    myPathTracingRefineTicksLeft = kPathTracingConvergeMs / kPathTracingBurstIntervalMs;
    myPathTracingIdleTicksLeft = kPathTracingIdlePasses;
    myPathTracingRefineTimer->setInterval(kPathTracingBurstIntervalMs);
    myPathTracingRefineTimer->start();
}

int OcctViewWidget::pathTracingIdleIntervalMs() const
{
    // Derived from what the tier probe actually MEASURED this frame costing,
    // not from a literal - the same number the probe compares against its
    // threshold. On this machine a path-traced pass costs 3 ms, so the idle
    // rate stays at the burst's own 50 ms and the GPU idles at about 6% duty;
    // on a machine that scraped into this tier at several hundred milliseconds
    // a frame, a fixed 50 ms tick would queue paints faster than they render
    // and an orbit would have to wait behind them. Twice the frame cost keeps
    // any GPU at roughly a third duty and keeps the input queue reachable,
    // which is the responsiveness half of this bargain.
    const int frameMs = myTierProbeTimings.pathTracingMs;
    if (frameMs <= 0) return kPathTracingBurstIntervalMs;
    return std::max(kPathTracingBurstIntervalMs, 2 * frameMs);
}

void OcctViewWidget::stopPathTracingConvergence()
{
    if (myPathTracingRefineTimer != nullptr) myPathTracingRefineTimer->stop();
    myPathTracingRefineTicksLeft = 0;
    myPathTracingIdleTicksLeft = 0;
}

bool OcctViewWidget::probeShadowPixelsDiffer()
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
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
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
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
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
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
    // repaint the identical frame. The count is shared with every other
    // measuring probe - see kMeasurementSettlePasses for what five of them
    // cost.
    for (int i = 0; i < kMeasurementSettlePasses; ++i) myView->Redraw();

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
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
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

OcctViewWidget::ShadowRatioProbe OcctViewWidget::probeRenderShadowRatio(
    RenderTier forTier, const QPoint& litFloorPointLogical)
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
    ShadowRatioProbe result;
    if (!myRenderModeActive || myView.IsNull() || myRenderFloor.IsNull()) return result;

    // Forced temporarily and restored before every return, including the
    // failure paths - probeRenderFloorBlend()'s rule, and the same settle
    // pass, which the path-traced tier genuinely needs and the others
    // simply repaint through.
    const RenderTier cachedTier = myRenderTier;
    applyRenderTier(forTier);
    for (int i = 0; i < kMeasurementSettlePasses; ++i) myView->Redraw();

    QTemporaryFile file(QDir::tempPath() +
                        QStringLiteral("/furnifyme-shadowratio-XXXXXX.png"));
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
        const QPoint litDevice = toDevicePixels(litFloorPointLogical);
        if (!shot.isNull() && shot.rect().contains(litDevice)) {
            // Boxes rather than single pixels throughout: a path-traced
            // frame carries real per-pixel variance, and a probe that
            // sampled one pixel of it would be reading noise.
            constexpr int kBox = 13;
            auto boxLuma = [&shot](int x0, int y0, int size) {
                qint64 total = 0;
                int count = 0;
                for (int y = y0; y < y0 + size && y < shot.height(); y += 2) {
                    for (int x = x0; x < x0 + size && x < shot.width(); x += 2) {
                        const QColor c = shot.pixelColor(x, y);
                        total += (c.red() + c.green() + c.blue()) / 3;
                        ++count;
                    }
                }
                return count > 0 ? static_cast<int>(total / count) : -1;
            };

            const int lit = boxLuma(std::max(0, litDevice.x() - kBox / 2),
                                    std::max(0, litDevice.y() - kBox / 2), kBox);
            // Everything below the top fifth, which is the only part of the
            // frame the backdrop can reach - probeRenderFloorBlend()'s own
            // reading of this camera's framing, reused rather than
            // re-derived. Skipping it matters: the path-traced backdrop is
            // not the darkest thing in the shot, but a future framing where
            // it were would turn this into a measurement of the background.
            int darkest = -1;
            const int firstRow = shot.height() / 5;
            for (int y = firstRow; y + kBox < shot.height(); y += 6) {
                for (int x = 0; x + kBox < shot.width(); x += 6) {
                    const int v = boxLuma(x, y, kBox);
                    if (v >= 0 && (darkest < 0 || v < darkest)) darkest = v;
                }
            }
            if (lit > 0 && darkest >= 0) {
                result.measured = true;
                result.lit = lit;
                result.darkest = darkest;
            }
        }
    }

    QFile::remove(path);
    return result;
}

OcctViewWidget::RenderTier OcctViewWidget::probeRenderTier()
{
    // Pixels before this returns, so the GL context has to be OURS for the
    // duration - Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);
    // Every field back to its "nothing measured" state before this run writes
    // any of them, so a second probe can never leave one tier's numbers
    // standing beside another tier's answer.
    myTierProbeTimings = TierProbeTimings();
    myTierProbeTimings.probed = true;
    if (myView.IsNull()) return RenderTier::Plain;

    // A REDRAW THAT IS NOT WAITED FOR IS NOT A TIMING, and the QOpenGLWidget
    // migration is what took the wait away. This probe's whole method is "time
    // one redraw and compare it against a threshold", which only measures
    // anything if the call returns after the GPU has done the work. It used to:
    // OCCT owned the surface and ended Redraw() with a buffer swap, and a swap
    // is a synchronization point. Qt owns the frame now, the driver is
    // configured with buffersNoSwap, and GL commands are asynchronous - so the
    // timer was measuring how long it takes to SUBMIT a path-traced frame, not
    // to render one. Measured on this machine in the wrapped context: the
    // first path-traced redraw timed 1 ms against a 1500 ms threshold, which
    // is not a fast GPU, it is no measurement at all - every GPU would have
    // been handed the top tier, including the ones this probe exists to
    // protect from it.
    //
    // glFinish() rather than glFlush(): flush only guarantees the commands
    // start, and "started" is exactly the answer that was already useless
    // here. Taken once, before the timing begins as well as after each redraw,
    // so no work queued by whatever ran before this probe is charged to the
    // first tier it tries.
    //
    // THE COST, STATED: THIS BLOCKS THE UI THREAD FOR A WHOLE PATH-TRACED
    // FRAME, once per session, at the first render-mode activation. glFinish()
    // does not return until the GPU is done, and the threshold is only
    // consulted AFTER that - kPathTracingProbeThresholdMs (1500 ms) protects
    // the TIER CHOICE, not the wait, so a GPU that takes 1400 ms to draw the
    // first path-traced frame freezes the window for 1400 ms and then gets the
    // top tier anyway. Measured here at 3 ms, but that is a warm shader cache;
    // a cold first-ever compile on weak hardware is the untested case and the
    // one that could reach the threshold. Not a regression - before the
    // QOpenGLWidget migration OCCT ended Redraw() with a buffer swap, which
    // synchronised just as hard - and not restructured, because a probe that
    // does not wait measures nothing at all (see the paragraph above, where a
    // path-traced frame "timed" 1 ms). Recorded so that a user reporting a
    // one-off freeze on entering render mode has somewhere to land.
    const Handle(OpenGl_Context) glContext = hostGlContext(myView);
    myTierProbeTimings.gpuSyncAvailable =
        !glContext.IsNull() && glContext->core11fwd != nullptr;
    auto finishGpuWork = [this, &glContext]() {
        if (myTierProbeTimings.gpuSyncAvailable) glContext->core11fwd->glFinish();
    };
    finishGpuWork();

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
    // Offered again since the user-feedback round: the black frame was an
    // unwritten Graphic3d_BSDF, not a defect in the GI pass, and every
    // surface carries one now. kPathTracingEnabled stays as the one
    // constant that parks this tier if it ever has to be parked again - the
    // suite's PT checks already skip-by-environment on a machine whose
    // probe lands elsewhere, so switching it costs nothing else.
    if (kPathTracingEnabled) {
        myTierProbeTimings.pathTracingAttempted = true;
        try {
            applyRenderTier(RenderTier::PathTracing);
            QElapsedTimer timer;
            timer.start();
            myView->Redraw();
            finishGpuWork();
            myTierProbeTimings.pathTracingMs = static_cast<int>(timer.elapsed());
            // The RECORDED number, not a second, later reading of the same
            // timer - the decision and the number the suite audits it against
            // have to be one measurement.
            pathTracingFast = myTierProbeTimings.pathTracingMs <= kPathTracingProbeThresholdMs;
        } catch (const Standard_Failure&) {
            myTierProbeTimings.pathTracingRefused = true;
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
    myTierProbeTimings.rayTracingAttempted = true;
    try {
        applyRenderTier(RenderTier::RayTracing);
        QElapsedTimer timer;
        timer.start();
        myView->Redraw();
        finishGpuWork();
        myTierProbeTimings.rayTracingMs = static_cast<int>(timer.elapsed());
        rayTracingFast = myTierProbeTimings.rayTracingMs <= kRenderTierProbeThresholdMs;
    } catch (const Standard_Failure&) {
        myTierProbeTimings.rayTracingRefused = true;
        rayTracingFast = false;
    }
    if (rayTracingFast) return RenderTier::RayTracing;

    // Neither ray-traced tier held up - rasterization from here down. Tier
    // 2: a shadow-mapped directional light, accepted only once a real
    // Dump() shows a pixel actually moved.
    applyRenderTier(RenderTier::Shadows);
    myTierProbeTimings.shadowsAttempted = true;
    myTierProbeTimings.shadowsPixelsDiffered = probeShadowPixelsDiffer();
    if (myTierProbeTimings.shadowsPixelsDiffered) return RenderTier::Shadows;

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
    //
    // EVERY early-out is taken before the GlScope below, not after. A scope
    // opened first would makeCurrent/doneCurrent and ask for a frame on every
    // no-op toggle, and would open on a view that initializeViewer() has not
    // even built yet.
    if (myViewerOnly || on == myRenderModeActive) return;
    initializeViewer();
    if (myContext.IsNull() || myView.IsNull()) return;

    // Pixels before this returns - the tier probe times real redraws and the
    // measuring probes Dump - so the GL context has to be OURS for the
    // duration; Qt only guarantees a current context inside its own three GL
    // callbacks. See GlScope on the header.
    GlScope gl(this);

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
        myRenderSavedAmbients.clear();
        if (!myViewer.IsNull()) {
            for (const Handle(Graphic3d_CLight)& light : myViewer->ActiveLights()) {
                // The ambient fill is this class's to move too since the
                // user-feedback round - up for the rasterized tiers, whose
                // unlit faces read near-black without it, and down for the
                // path-traced one, which integrates its own bounce and
                // clips to white against OCCT's modeling-legibility rig.
                if (light->Type() == Graphic3d_TypeOfLightSource_Ambient) {
                    myRenderSavedAmbients.push_back({light, light->Intensity()});
                    continue;
                }
                if (light->Type() != Graphic3d_TypeOfLightSource_Directional) continue;
                myRenderSavedLights.push_back({light, light->Direction(), light->Intensity(),
                                               light->IsHeadlight(), light->Smoothness()});
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
            myRenderTierBest = probeRenderTier();
            myRenderTierProbed = true;
            myRenderTier = effectiveRenderTier();
            // The probe leaves its own best choice applied; a Quick cap has
            // to re-dress down to the tier it actually asked for.
            if (myRenderTier != myRenderTierBest) applyRenderTier(myRenderTier);
        } else {
            myRenderTier = effectiveRenderTier();
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
            saved.light->SetSmoothAngle(saved.smoothness);
        }
        for (auto& saved : myRenderSavedAmbients) saved.light->SetIntensity(saved.intensity);
        myRenderSavedLights.clear();
        myRenderSavedAmbients.clear();
        if (!myViewer.IsNull()) myViewer->UpdateLights();
        const Standard_Integer mode = myWireframe ? AIS_WireFrame : AIS_Shaded;
        // Restored AT THE TOKEN WIDTH, not unconditionally True - a theme
        // edit made WHILE render mode was up (applyTheme() above skips the
        // boundary while myRenderModeActive) lands here instead, on exit.
        // 0 means the user asked for no boundary lines at all, which this
        // exit must honour exactly like displaySolid()'s own creation path.
        const double edgeWidth = Theme::edgeWidthPx();
        for (auto& entry : mySolids) {
            entry.second->Attributes()->SetFaceBoundaryDraw(edgeWidth > 0.0);
            if (edgeWidth > 0.0) {
                entry.second->Attributes()->SetFaceBoundaryAspect(
                    new Prs3d_LineAspect(Quantity_NOC_GRAY30, Aspect_TOL_SOLID, edgeWidth));
            }
            // The render-mode PBR material off, back to whatever stood
            // before applyRenderBodyMaterials() ran - see that function's
            // own comment on why UnsetMaterial() is a complete restore here.
            // The wood grain goes with it - the aspect bit is presentation
            // state exactly as the material is.
            entry.second->Attributes()->SetupOwnShadingAspect();
            entry.second->Attributes()->ShadingAspect()->Aspect()->SetTextureMapOn(false);
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
    scheduleRedraw();
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
    scheduleRedraw();
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
    // Ctrl is the lock gesture and is not a grab, scoped exactly as
    // mouseDoubleClickEvent() scopes the same exemption. Without it the lock's
    // first press armed a pull drag on the way past: the release ended a drag
    // that had moved nothing, which fell through to an ordinary pick and
    // deselected the very face the gesture was aimed at.
    //
    // THE EXEMPTION IS THE PULL ARROW'S ALONE, and it is scoped by WHERE it is
    // written rather than by what it names. It used to be scoped by naming
    // face mode, so that the bevel arrow one branch down - where Ctrl means
    // nothing - kept its guard whole; with the modes gone that reason no
    // longer parses, and `lockGesture` is now true for Ctrl in auto whatever
    // is under the cursor. What keeps the bevel branch whole is that it does
    // not consult `lockGesture` at all: it is guarded on Shift only, so a
    // Ctrl press on a bevel arrow still claims the bevel drag exactly as it
    // always did. mouseDoubleClickEvent() carries the matching guard for the
    // double-click half - see its own comment on why Ctrl keeps the arrow
    // guard there while a plain double-click gives it up.
    const bool lockGesture = (event->modifiers() & Qt::ControlModifier) &&
                             (mySelectionMode == SelectionMode::Face ||
                              mySelectionMode == SelectionMode::Auto);
    // Declared here rather than beside the bevel branch that first needed it:
    // three screen-space handles now read it, and a modifier the FIRST of them
    // consults has to be in scope before the first of them.
    const bool additivePress = (event->modifiers() & Qt::ShiftModifier) != 0;
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
    // Checked ahead of them rather than after: a mirror gesture needs whole
    // BODIES selected to begin, and neither arrow is ever up over a body
    // selection (the pull arrow needs one face, the bevel arrow needs edges),
    // so the ordering is not load-bearing either - but this keeps the three
    // "grab a screen-space handle" branches together.
    if (event->button() == Qt::LeftButton && !mySketchMode && myMirrorPlacement.active &&
        mirrorHandleHit(myLastPos)) {
        beginAxisDrag(myMirrorDrag, mirrorPlacementAxisLine(), myLastPos);
        myMirrorDragOffsetStart = myMirrorPlacement.offset;
        return;
    }

    // A LIVE PLACEMENT OWNS EVERY LEFT PRESS IN THE VIEWPORT, and the handle
    // above is the only thing that does anything with one. The gesture is
    // already modal in its keys - it holds Enter, Escape and X/Y/Z
    // application-wide - and this is the mouse half of the same claim.
    //
    // It exists because the auto-selection switch turned a harmless miss into
    // a destroyed gesture. Under the old modes a press that missed the handle
    // fell through to an ordinary pick, which changed the selection but never
    // the MODE, so mirrorPlacementEnvironmentOk() still held and the placement
    // survived. Under auto that same press picks a face, an edge or empty
    // space, and a Body term in that predicate turned every one of them into a
    // silent self-cancel. The term moved to canBeginMirrorPlacement() where it
    // belongs (see MainWindow), and this makes the point moot in the shipped
    // app as well: while a placement is live nothing can change the selection,
    // so nothing can change what the gesture is about to pair.
    //
    // RMB orbit and MMB pan returned above, so framing the plane still works -
    // exactly the freedom render mode's own press guard leaves intact.
    if (event->button() == Qt::LeftButton && !mySketchMode && myMirrorPlacement.active)
        return;

    // The bevel arrow, on exactly the same terms - including claiming the
    // gesture at an angle the maths refuses. The two arrows are never up at
    // once - selectionKind() answers Face for one and Edge for the other, and
    // a selection holds one kind at a time - so the order of these two blocks
    // is not load-bearing.
    //
    // Shift is excluded, and that exclusion is the arrow's half of multi-edge
    // selection. arrowHit() is a 14 px SCREEN-SPACE test, so the arrow does
    // not compete for the pick the way AIS_ManipulatorOwner does - it does
    // something worse: it silently swallows the press before the picker ever
    // sees it. The arrow stands on the last edge picked and a second edge is
    // usually right beside it, so without this a Shift-click meant to
    // accumulate would start a drag on the edge already chosen instead. Same
    // hazard the transform gizmo's Deactivate() closes below, one layer up.
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress &&
        arrowHit(myBevelArrow, myLastPos)) {
        beginAxisDrag(myBevelDrag, myBevelArrow.axis(), myLastPos);
        return;
    }

    // The Move gizmo's three arms, on exactly the same terms as the two arrows
    // above - including claiming the gesture at an angle the maths refuses.
    //
    // Never up at the same time as either of them: this gizmo needs one whole
    // BODY selected and the arrows need a face and edges (see
    // MainWindow::moveToolBodyId()), so the order of these blocks is not
    // load-bearing. Shift is excluded for the bevel arrow's own reason, one
    // gizmo over: an arm lying over a second body would otherwise swallow the
    // Shift press meant to add it. Ctrl is excluded because it is the face
    // lock's gesture and never a grab - the pull arrow's own exemption, and
    // the arms of a gizmo standing at a body's centre cross that body's faces
    // exactly where a user aims to lock one.
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress && !lockGesture &&
        myMoveGizmo.isShowing()) {
        bool positive = true;
        const int axis = moveGizmoAxisAt(myLastPos, &positive);
        if (axis >= 0) {
            myMoveDragAxis = axis;
            myMoveDragPositive = positive;
            myMoveDragCancelled = false;
            // ONE line for both ends of an arm, FROZEN here - the gizmo
            // follows the drag, so its live armAxis() moves with it and
            // cannot be the ruler (see myMoveDragLine).
            myMoveDragLine = myMoveGizmo.armAxis(axis);
            beginAxisDrag(myMoveDrag, myMoveDragLine, myLastPos);
            // Magnet's alignment candidates, frozen alongside the line.
            collectMagnetCandidates(axis);
            return;
        }
    }

    // The Rotate gizmo's press claim - same guards, same outright take. The
    // drag's zero is the press's own vector from the pivot in the ring's
    // plane; a press whose ray runs too flat to the plane anchors on the
    // first move that can be measured instead, beginAxisDrag()'s own rule.
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress && !lockGesture &&
        myRotateGizmo.isShowing()) {
        const int axis = rotateGizmoAxisAt(myLastPos);
        if (axis >= 0) {
            myRotateDrag = RotateDrag();
            myRotateDrag.active = true;
            myRotateDrag.axis = axis;
            // FROZEN at the press, myMoveDragLine's reasoning: the preview
            // rotates the body, and a pivot re-read mid-gesture would follow
            // whatever the preview does.
            myRotateDrag.pivot = myRotateGizmo.pivot();
            myRotateDrag.anchor = myRotateDrag.pivot;
            myMoveDragCancelled = false;
            gp_Lin ray;
            gp_Pnt hit;
            const gp_Pln plane(myRotateDrag.pivot,
                               MoveGizmoRenderer::armDirection(axis));
            if (rayThroughPixel(myLastPos.x(), myLastPos.y(), ray) &&
                SketchController::intersectRayWithPlane(ray, plane, hit)) {
                const gp_Vec fromPivot(myRotateDrag.pivot, hit);
                if (fromPivot.Magnitude() > 1.0e-9) {
                    myRotateDrag.pressVec = fromPivot;
                    myRotateDrag.hasPress = true;
                    // The chip's anchor: where the press landed, put ON the
                    // drawn ring so the chip stands on ink rather than a few
                    // pixels off it.
                    myRotateDrag.anchor = myRotateDrag.pivot.Translated(
                        fromPivot.Normalized() * myRotateGizmo.ringRadius());
                }
            }
            return;
        }
    }

    // The Scale gizmo's press claim. The drag is the Move drag's own maths -
    // a parameter along the frozen arm line - read out as a factor of the
    // arm's length at the press instead of as millimetres.
    if (event->button() == Qt::LeftButton && !mySketchMode && !additivePress && !lockGesture &&
        myScaleGizmo.isShowing()) {
        const int axis = scaleGizmoAxisAt(myLastPos);
        if (axis >= 0) {
            myScaleDrag = ScaleDrag();
            myScaleDrag.active = true;
            myScaleDrag.axis = axis;
            myScaleDrag.line =
                gp_Lin(myScaleGizmo.pivot(), MoveGizmoRenderer::armDirection(axis));
            myScaleDrag.baseLength = std::max(
                myScaleGizmo.pivot().Distance(myScaleGizmo.handleTip(axis, true)), 1.0e-9);
            myMoveDragCancelled = false;
            gp_Lin ray;
            myScaleDrag.hasPressParam =
                rayThroughPixel(myLastPos.x(), myLastPos.y(), ray) &&
                CameraController::axisParameterForRay(ray, myScaleDrag.line,
                                                      myScaleDrag.pressParam);
            return;
        }
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
    // showBevelArrow() or any body gizmo's show on a viewer-only widget, so
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

    // The end of a Move drag, swallowed for exactly the same reason: the press
    // was aimed at an arm, and re-picking here would take the body selection
    // the gizmo is standing on and replace it with whichever face or edge the
    // cursor happens to be over.
    if (myMoveDrag.active && event->button() == Qt::LeftButton) {
        myMoveDrag.active = false;
        myMoveDragAxis = -1;
        // The guide is the drag's own feedback and goes with it; the
        // candidates were frozen at this drag's press and mean nothing to
        // the next one.
        clearMagnetGuide();
        myMoveMagnetCandidates.clear();
        emit moveReleased(myMoveDrag.moved);
        return;
    }
    // The Rotate and Scale drags end on identical terms.
    if (myRotateDrag.active && event->button() == Qt::LeftButton) {
        myRotateDrag.active = false;
        emit rotateReleased(myRotateDrag.moved);
        return;
    }
    if (myScaleDrag.active && event->button() == Qt::LeftButton) {
        myScaleDrag.active = false;
        emit scaleReleased(myScaleDrag.moved);
        return;
    }
    // ...and the release trailing a drag Escape already cancelled, swallowed
    // for the same reason one beat later. Consumed once, so a later click
    // cannot inherit it.
    if (myMoveDragCancelled && event->button() == Qt::LeftButton) {
        myMoveDragCancelled = false;
        return;
    }

    if (event->button() != Qt::LeftButton || myContext.IsNull()) return;

    // The other half of the press guard above: a live placement suspends
    // ordinary picking outright, so a left release changes neither the
    // selection nor the gesture. Both halves are needed - the press claim
    // stops a handle grab and the release claim stops the pick -
    // and it is the same "the gesture that started owns the release that ends
    // it" rule every drag in this file already keeps, widened from one drag to
    // one modal gesture.
    if (myMirrorPlacement.active) return;

    // The release that trails an Auto double-click belongs to that gesture -
    // see mouseDoubleClickEvent()'s Auto branch. Consumed here, once, so a
    // later click can never inherit it.
    if (myAutoBodyPickTaken) {
        myAutoBodyPickTaken = false;
        return;
    }

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
    // (The manipulator-era Deactivate-around-this-pick shield is gone with
    // the manipulator: the custom gizmos register nothing, so there is
    // nothing for an additive pick to land on but the scene.)

    const QPoint device = toDevicePixels(pos);
    myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
    // The same preference the hover applies, at the same point in the same
    // sequence, before anything reads the detected shape. A click must take
    // what the last hover was glowing, and the only way to guarantee that is
    // for both to arbitrate identically.
    preferDetectedEdge(pos);
    // Read BEFORE SelectDetected, because a Replace clears the detection's
    // relationship to the selection and an XOR may have just removed it.
    TopoDS_Edge justPicked;
    if (myContext->HasDetectedShape() &&
        myContext->DetectedShape().ShapeType() == TopAbs_EDGE) {
        justPicked = TopoDS::Edge(myContext->DetectedShape());
    }

    // Auto's kind lock. The FIRST pick decides what this selection is OF, and
    // Shift only ever adds more of that - edges with edges (across bodies
    // included, which is Milestone 5 item 5's own rule and needs nothing
    // extra here: XOR accumulation never cared which body an edge came from),
    // faces with faces. A Shift-click asking for anything else does NOTHING
    // AT ALL - it does not replace the selection, does not clear it, and
    // takes no checkpoint - because the alternative is a gesture aimed at
    // adding an edge silently throwing away the four the user already had.
    //
    // Quiet is not silent: a no-op with no explanation reads as a broken
    // control, so the reason is recorded and announced. The lock itself is
    // read from selectionKind(), which derives it from the live selection -
    // so an undo, a delete or a programmatic selection change moves the lock
    // with them and there is nothing to keep in step.
    if (mySelectionMode == SelectionMode::Auto) {
        // What this selection was OF before this click touched it. Qt delivers
        // a double-click as press/release/DblClick/release, so by the time
        // mouseDoubleClickEvent() runs, the gesture's OWN first click has
        // already picked a face or an edge and moved the lock - and reading
        // the lock there would refuse a Shift+double-click on the strength of
        // a sub-shape the same gesture had just added half a beat earlier.
        // Recorded here, where the pre-click answer still exists, and read
        // exactly one event later. It is gesture-local memory, the same kind
        // myLastPickedEdge is, and never a second source of truth for the
        // lock itself: selectionKind() remains the only thing that answers
        // "what is held".
        myAutoKindBeforeClick = selectionKind();
    }
    if (mySelectionMode == SelectionMode::Auto && additive) {
        const PickKind held = myAutoKindBeforeClick;
        const PickKind asked = hoveredKind();
        if (held != PickKind::None && asked != PickKind::None && asked != held) {
            myAutoRefusal = autoKindRefusalText(held, asked);
            emit autoPickRefused(myAutoRefusal);
            return;
        }
    }

    myContext->SelectDetected(additive ? AIS_SelectionScheme_XOR
                                       : AIS_SelectionScheme_Replace);
    // A pick that landed answers whatever the last refusal was asking
    // about, so the sentence goes with it - off the member AND off
    // whatever is painting it. See autoPickRefusalWithdrawn().
    if (!myAutoRefusal.isEmpty()) {
        myAutoRefusal.clear();
        emit autoPickRefusalWithdrawn();
    }
    // "The edge you picked last" - remembered here because it cannot be
    // read back out of the selection afterwards (see lastSelectedEdge()).
    // A click on nothing clears it, so the arrow cannot linger on an edge
    // the user has just dropped.
    myLastPickedEdge = justPicked;

    // The selection just changed, and in edge mode the dimension follows it as
    // well as the hover - selecting a second edge has to stop the annotation
    // claiming to measure the one before it.
    updateEdgeDimension();
    scheduleRedraw();
    emit selectionChanged();
}

void OcctViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (myView.IsNull()) return;

    const QPoint pos = event->position().toPoint();

    if (myMirrorDrag.active) {
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
    } else if (myMoveDrag.active) {
        // Measured against the line FROZEN at the press, never the
        // renderer's live armAxis(): the gizmo follows the drag now, so the
        // live line's origin carries the very offset being measured - reading
        // it back each move would subtract the current value from itself.
        //
        // RAW first, then Magnet, then the grid - in that order and
        // exclusively: Magnet compares against the true cursor distance (a
        // grid-rounded one can sit half a step from the alignment being
        // aimed at), and an alignment it takes is exact by definition, so
        // the grid never rounds it away afterwards.
        double raw = 0.0;
        if (myMoveDragAxis >= 0 && measureAxisDrag(myMoveDrag, myMoveDragLine, pos, raw)) {
            double value = raw;
            gp_Pnt guideA, guideB;
            const bool magnetHeld = magnetSnap(raw, value, guideA, guideB);
            if (magnetHeld)
                showMagnetGuide(guideA, guideB);
            else {
                clearMagnetGuide();
                if (mySnapEnabled && mySnapStep > 0.0)
                    value = std::round(value / mySnapStep) * mySnapStep;
            }
            if (std::fabs(value - myMoveDrag.value) > 1.0e-9) {
                myMoveDrag.value = value;
                if (std::fabs(value) > 1.0e-9) myMoveDrag.moved = true;
                emit moveDragged(myMoveDragAxis, value);
            }
        }
    } else if (myRotateDrag.active) {
        // The whole-gesture angle: where the cursor's ray meets the ring's
        // own plane, measured against the press vector about the frozen
        // axis. atan2 of (v0 x v1)·axis against v0·v1 gives the SIGNED angle
        // - a positive drag is a positive rotation, with no branch on which
        // side of the ring was grabbed. A ray running too flat to the plane
        // measures nothing and the last value stands, the axis drags' rule.
        gp_Lin ray;
        gp_Pnt hit;
        const gp_Pln plane(myRotateDrag.pivot,
                           MoveGizmoRenderer::armDirection(myRotateDrag.axis));
        if (rayThroughPixel(pos.x(), pos.y(), ray) &&
            SketchController::intersectRayWithPlane(ray, plane, hit)) {
            const gp_Vec fromPivot(myRotateDrag.pivot, hit);
            if (fromPivot.Magnitude() > 1.0e-9) {
                if (!myRotateDrag.hasPress) {
                    // The press could not be measured - anchor here, the
                    // late-anchor rule beginAxisDrag() records.
                    myRotateDrag.pressVec = fromPivot;
                    myRotateDrag.hasPress = true;
                } else {
                    const gp_Vec axisVec(
                        MoveGizmoRenderer::armDirection(myRotateDrag.axis));
                    double degrees =
                        std::atan2(myRotateDrag.pressVec.Crossed(fromPivot).Dot(axisVec),
                                   myRotateDrag.pressVec.Dot(fromPivot)) *
                        180.0 / 3.14159265358979323846;
                    // The Milestone 2 step, applied where the snap state
                    // lives - 15 degrees, as the grid step is millimetres.
                    if (mySnapEnabled) degrees = std::round(degrees / 15.0) * 15.0;
                    if (std::fabs(degrees - myRotateDrag.degrees) > 1.0e-9) {
                        myRotateDrag.degrees = degrees;
                        if (std::fabs(degrees) > 1.0e-9) myRotateDrag.moved = true;
                        emit rotateDragged(myRotateDrag.axis, degrees);
                    }
                }
            }
        }
    } else if (myScaleDrag.active) {
        gp_Lin ray;
        double parameter = 0.0;
        if (rayThroughPixel(pos.x(), pos.y(), ray) &&
            CameraController::axisParameterForRay(ray, myScaleDrag.line, parameter)) {
            if (!myScaleDrag.hasPressParam) {
                myScaleDrag.pressParam = parameter;
                myScaleDrag.hasPressParam = true;
            } else {
                // Outward along the arm grows, inward shrinks: the cube sits
                // at baseLength, so dragging it to twice that distance reads
                // 200%. Snapped to 5% steps when Snap to Grid is on and
                // clamped to the Milestone 2 band the commit enforces too, so
                // the chip can never show a factor the commit would refuse.
                double factor =
                    (myScaleDrag.baseLength + (parameter - myScaleDrag.pressParam)) /
                    myScaleDrag.baseLength;
                if (mySnapEnabled) factor = std::round(factor / 0.05) * 0.05;
                factor = std::clamp(factor, 0.05, 20.0);
                if (std::fabs(factor - myScaleDrag.factor) > 1.0e-9) {
                    myScaleDrag.factor = factor;
                    if (std::fabs(factor - 1.0) > 1.0e-9) myScaleDrag.moved = true;
                    emit scaleDragged(myScaleDrag.axis, factor);
                }
            }
        }
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
        // The gizmos' own hover, before OCCT's: their handles are invisible
        // to the picker, so MoveTo below can never light one - this is the
        // screen-space counterpart, and it only rebuilds on enter/leave.
        updateBodyGizmoHover(pos);
        // Hover highlight. Suppressed while sketching so the in-progress wire
        // does not fight the highlighter for attention. Suppressed
        // altogether in viewer-only mode - see the header: no picking means
        // no hover highlight either.
        const QPoint device = toDevicePixels(pos);
        myContext->MoveTo(device.x(), device.y(), myView, Standard_True);

        // Auto's edge-over-face preference, applied BEFORE the owner
        // comparison below so the repaint is asked for against the owner that
        // will actually be glowing - and applied on this path and the click's
        // in exactly the same place, which is what makes the two agree.
        preferDetectedEdge(pos);

        // MoveTo(...,Standard_True) asks OCCT for its own immediate redraw,
        // but that is a hardware-dependent shortcut, not a guarantee: on
        // hardware where OCCT's separate immediate-mode framebuffer fails to
        // allocate, the highlight falls back to landing straight in the
        // bound default framebuffer - the one Qt actually composites - so it
        // reaches the screen regardless of anything below. Where that
        // allocation succeeds, it does not, and a genuine Qt-driven repaint
        // is the only thing that puts the new hover state on screen.
        // updateEdgeDimension() used to be the sole source of one on this
        // path, and it bails out before ever looking at hover at all the
        // moment the dimension is suppressed (an edge selected, its bevel
        // arrow up) or the mode is not Edge - so a hover that changed which
        // owner is detected could go unscheduled entirely, in any selection
        // mode. Compared here instead, by owner identity, independently of
        // what the dimension does: a repaint is asked for exactly when the
        // detected owner actually changes - something now detected, something
        // no longer detected, or a different something - never on every idle
        // mouse move over the same one, which is the redraw storm the
        // migration's own fix round already removed once.
        const Handle(SelectMgr_EntityOwner) detected =
            myContext->HasDetected() ? myContext->DetectedOwner()
                                      : Handle(SelectMgr_EntityOwner)();
        if (detected != myLastHoverOwner) {
            myLastHoverOwner = detected;
            scheduleRedraw();
        }

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
    // A live mirror placement owns every left gesture in the viewport - see
    // the press handler for why, and why the handle is the only exception.
    // Taking a whole body here would change what the placement is about to
    // pair, which is the one thing a running gesture must not let a stray
    // click do.
    if (myMirrorPlacement.active) return;

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
    // Auto joins face mode here for the reason the exemption exists at all:
    // in Auto a face is one of the two things the cursor can be on, so
    // Ctrl+double-click means exactly what it means in face mode, and the
    // gesture has to survive an arrow's tail sitting at the face's centre.
    // The exemption still names the BRANCH rather than the modifier, so
    // nothing else inherits it.
    const bool lockGesture = ctrlHeld && (mySelectionMode == SelectionMode::Face ||
                                          mySelectionMode == SelectionMode::Auto);

    // IN AUTO THE GUARD HAS NOTHING LEFT TO GUARD, and Phase 1 flagged this
    // exact pixel. The pull arrow used to need face mode, so a double-click
    // could only meet one while the user was deliberately in it; in Auto a
    // plain click on a face raises the arrow AT THAT FACE'S OWN CENTRE, which
    // is precisely where the second click of a double-click aimed at the body
    // lands. The gesture then died on this line: press picks the face, the
    // arrow appears under the cursor, and the double-click that follows is
    // swallowed by an arrow the user's first click had just put there. Every
    // plain double-click on the middle of a face was a no-op.
    //
    // Dropping the guard for Auto costs nothing, because an arrow has no
    // CLICK meaning at all - it is press, drag, release - and this gesture's
    // own first press/release pair has already offered it that gesture and
    // been answered (mousePressEvent claims the drag; mouseReleaseEvent ends
    // it and picks nothing). What arrives here is the double-click, and in
    // Auto that means one thing: the whole body. The two harms the guard was
    // written against are both gone with the mode - Auto's branch below picks
    // the body rather than flying the camera to frame it, and there is no
    // selection mode left for it to yank out from under a live drag.
    if (onArrow && !lockGesture && mySelectionMode != SelectionMode::Auto) return;

    // (The manipulator-era GizmoPickShield around this MoveTo is gone with
    // the manipulator: the custom gizmos' handles are invisible to the
    // picker, so a Shift+double-click over an arm reaches the body under it
    // with no Deactivate dance at all.)
    const bool additiveDouble = (event->modifiers() & Qt::ShiftModifier) != 0;

    const QPoint device = toDevicePixels(pos);
    myContext->MoveTo(device.x(), device.y(), myView, Standard_False);
    if (!myContext->HasDetected()) return;

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
        scheduleRedraw();
        emit selectionChanged();
        emit faceDoubleClicked(TopoDS::Face(myContext->DetectedShape()));
        return;
    }

    // Past the one exempt branch, an arrow hit is an arrow hit again. A
    // Ctrl+double-click that got this far is one whose detection was not a
    // face after all - a body, an edge, the ground - and there is no reason
    // the modifier should buy it the whole-body route the guard would refuse
    // to an unmodified click on the same pixel.
    //
    // AUTO IS EXEMPT ONLY WITHOUT CTRL, and the difference is the whole point
    // of the two clauses. A PLAIN double-click means "the whole body" and has
    // to reach it through an arrow the first click of that same gesture put
    // under the cursor - that is the guard stand-down the comment above
    // argues for. A CTRL double-click means "lock this face", nothing else,
    // and if the detection was not a face it means nothing at all; letting it
    // fall through here is exactly the harm the exemption's own comment names,
    // because it takes the whole body out from under a live bevel arrow the
    // user is standing on. Ctrl keeps the pre-phase guard, in auto as in the
    // seam modes.
    if (onArrow && (lockGesture || mySelectionMode != SelectionMode::Auto)) return;

    // In Auto the whole-body pick is performed HERE. There is no selection
    // mode to come back out to and nothing to announce: this is the gesture,
    // start to finish.
    //
    // Shift accumulates bodies, which is the "bodies with bodies" half of the
    // kind lock. It is a double-click rather than a Shift-click because a
    // click in Auto lands on a face or an edge - the kind lock refuses it and
    // says so - and the gesture that takes a body is the gesture that adds
    // one.
    if (mySelectionMode == SelectionMode::Auto) {
        const bool additive = additiveDouble;
        // The lock as it stood BEFORE this gesture's own first click - see
        // myAutoKindBeforeClick's record site in mouseReleaseEvent().
        const PickKind held = additive ? myAutoKindBeforeClick : PickKind::None;
        if (additive && held != PickKind::None && held != PickKind::Body) {
            myAutoRefusal = autoKindRefusalText(held, PickKind::Body);
            emit autoPickRefused(myAutoRefusal);
            return;
        }
        // Accumulate only onto a body selection that already existed. A
        // Shift+double-click that STARTS a selection replaces instead, which
        // is what sweeps up the face or edge this same gesture's first click
        // put there - accumulating onto it would leave a mixed selection the
        // lock is supposed to make impossible.
        if (!selectDetectedBody(held == PickKind::Body))
            return;   // nothing of ours under the cursor
        // Qt delivers a double-click as press/release/DblClick/RELEASE, and
        // that trailing release runs an ordinary pick - which in Auto lands
        // on the face or the edge under the cursor and throws the body
        // selection away half a beat after this branch made it. The
        // announce-and-switch route this replaced never noticed, because it
        // only REPORTED the gesture and the mode switch that answered it
        // cleared the selection anyway; a route that performs the pick itself
        // has to claim the release too. Same rule
        // every drag in this file already keeps: the gesture that started
        // owns the release that ends it.
        myAutoBodyPickTaken = true;
        // THE WITHDRAWAL, and this is the site it exists for. This gesture's
        // OWN first release ran an ordinary additive pick with bodies held,
        // landed on a face or an edge (mode 0 is not activated in auto), and
        // the kind lock correctly refused it and said why - a sentence this
        // branch has just made false by adding the body the sentence was
        // telling the user how to add. Emitted BEFORE selectionChanged(), so
        // the ordinary "2 bodies selected" message is the last word.
        if (!myAutoRefusal.isEmpty()) {
            myAutoRefusal.clear();
            emit autoPickRefusalWithdrawn();
        }
        myLastPickedEdge.Nullify();   // a body pick is not an edge pick
        updateEdgeDimension();
        scheduleRedraw();
        emit selectionChanged();
        return;
    }

    const Handle(AIS_InteractiveObject) hit = myContext->DetectedInteractive();

    // Below here is the classic TEST SEAM only (see the SelectionMode enum):
    // no shipped path can reach it, because nothing outside gui_smoke calls
    // setSelectionMode(). The seam's Face and Edge modes take no whole-body
    // route at all - the announce-and-switch signal they used to emit died
    // with the three mode actions that answered it, and giving them the
    // camera-framing route below instead would be inventing behaviour for a
    // seam rather than preserving it.
    if (mySelectionMode != SelectionMode::Solid) return;

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
