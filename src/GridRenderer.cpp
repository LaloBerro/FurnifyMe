#include "GridRenderer.h"

#include "Theme.h"

#include <ElSLib.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ShaderObject.hxx>
#include <Graphic3d_ShaderProgram.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Graphic3d_Vec4.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>
#include <cmath>

namespace {

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// Two planes are the same grid frame only when they agree on all three of
// origin, normal and in-plane X direction: a plane rotated about its own
// normal draws a rotated grid, so comparing normals alone would cache a
// stale rebuild.
bool sameFrame(const gp_Pln& a, const gp_Pln& b)
{
    return a.Position().Direction().IsEqual(b.Position().Direction(), 1.0e-9) &&
           a.Position().XDirection().IsEqual(b.Position().XDirection(), 1.0e-9) &&
           a.Location().Distance(b.Location()) < 1.0e-9;
}

// --- the per-pixel grid (feedback round seven: "do it properly") ------------
//
// The grid is ONE QUAD on the work plane now, with everything else - the
// minor/major/axis line pattern, the antialiasing, the pool fade around the
// look point and the grazing fade toward the horizon - computed PER PIXEL in
// a fragment shader. Six rounds of vertex-fade artifacts (chevrons from
// per-chunk interpolation, 8-bit terracing over gentle gradients, alpha and
// colour ACCUMULATION where perspective compresses many lines into one
// pixel, staleness between rebuilds) were all consequences of approximating
// a per-pixel effect with per-vertex data; a fragment computes each of those
// quantities exactly where it is displayed, so every one of those classes is
// gone by construction, not by tuning. This is the reference apps' own
// architecture for their floors.
//
// The geometry rebuilds only when the CARPET has to move (the plane, the
// camera leaving the middle of the built square, the extent outgrowing it);
// everything visual - steps, colours, fade centres and radii - travels as
// uniforms pushed on every update(), so the pool follows the camera live
// with no rebuild at all.

const char* kGridVertexShader = R"GLSL(
THE_SHADER_OUT vec2 PlanePos;

void main()
{
  PlanePos = occTexCoord.xy;
  gl_Position = occProjectionMatrix * occWorldViewMatrix * occVertex;
}
)GLSL";

const char* kGridFragmentShader = R"GLSL(
THE_SHADER_IN vec2 PlanePos;

uniform vec2 uPoolCentre;   // plane (u,v) of the camera's look point
uniform vec2 uEyeFoot;      // plane (u,v) under the eye
uniform vec4 uRadii;        // pool start, pool end, graze start, graze end (mm)
uniform vec2 uSteps;        // minor step, major step (mm)
uniform vec4 uMinorColour;  // linear rgb + line half-width in pixels
uniform vec4 uMajorColour;
uniform vec4 uAxisXColour;
uniform vec4 uAxisYColour;

// Antialiased coverage of a line whose centre is `distMm` millimetres away,
// with `pxPerMm` pixels to the millimetre at this fragment: full ink inside
// the half-width, one smooth pixel of edge.
float lineCoverage(float distMm, float pxPerMm, float halfWidthPx)
{
  float px = distMm * pxPerMm;
  // A 0.35px AA skirt rather than a full pixel each side: wider softening
  // spread each line's ink over ~3 screen pixels, which read heavier than
  // the 1px lines this replaced (measured against the previous build's own
  // marker snapshot - grid ink under a sketch marker's antialiased edge
  // moved its blended pixels).
  return 1.0 - smoothstep(halfWidthPx - 0.35, halfWidthPx + 0.35, px);
}

void main()
{
  vec2 p = PlanePos;
  vec2 fw = max(fwidth(p), vec2(1.0e-6));
  vec2 pxPerMm = 1.0 / fw;

  // Distance to the nearest minor / major line in each direction. The
  // 0.5 recentring keeps fract()'s discontinuity between lines, not on one.
  vec2 dMinor = abs(fract(p / uSteps.x + 0.5) - 0.5) * uSteps.x;
  vec2 dMajor = abs(fract(p / uSteps.y + 0.5) - 0.5) * uSteps.y;

  float covMinor = max(lineCoverage(dMinor.x, pxPerMm.x, uMinorColour.a),
                       lineCoverage(dMinor.y, pxPerMm.y, uMinorColour.a));
  float covMajor = max(lineCoverage(dMajor.x, pxPerMm.x, uMajorColour.a),
                       lineCoverage(dMajor.y, pxPerMm.y, uMajorColour.a));
  // The plane's own axes: the u-running line at v = 0 wears the X colour,
  // the v-running line at u = 0 the Y colour.
  float covAxisX = lineCoverage(abs(p.y), pxPerMm.y, uAxisXColour.a);
  float covAxisY = lineCoverage(abs(p.x), pxPerMm.x, uAxisYColour.a);

  // Density fade: where a family's cells compress under a few pixels the
  // lines would alias into moire, so the family dissolves instead - the
  // per-pixel replacement for discrete LOD popping, and what kills the
  // grazing shimmer near the horizon.
  float minorCellPx = uSteps.x * min(pxPerMm.x, pxPerMm.y);
  covMinor *= clamp((minorCellPx - 2.0) / 6.0, 0.0, 1.0);
  float majorCellPx = uSteps.y * min(pxPerMm.x, pxPerMm.y);
  covMajor *= clamp((majorCellPx - 2.0) / 6.0, 0.0, 1.0);

  // Compose: axes over majors over minors.
  vec3 colour = uMinorColour.rgb;
  float alpha = covMinor;
  colour = mix(colour, uMajorColour.rgb, covMajor);
  alpha = max(alpha, covMajor);
  float covAxis = max(covAxisX, covAxisY);
  vec3 axisColour = covAxisX >= covAxisY ? uAxisXColour.rgb : uAxisYColour.rgb;
  colour = mix(colour, axisColour, covAxis);
  alpha = max(alpha, covAxis);

  // The pool around the look point, and the grazing fade from the point the
  // eye stands over - both exact circles, both per pixel.
  float fPool = smoothstep(uRadii.x, uRadii.y, distance(p, uPoolCentre));
  float fGraze = smoothstep(uRadii.z, uRadii.w, distance(p, uEyeFoot));
  alpha *= 1.0 - max(fPool, fGraze);

  if (alpha <= 0.001) discard;
  occSetFragColor(vec4(colour, alpha));
}
)GLSL";

// A minimal interactive object whose whole presentation is the shader quad -
// GridRenderer supplies the geometry and the program.
class GridObject : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfTriangles) quad;
    Handle(Graphic3d_AspectFillArea3d) aspect;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (quad.IsNull() || aspect.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(quad);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&,
                          const Standard_Integer) override
    {
        // Never pickable.
    }
};

}  // namespace

double GridRenderer::minorStepFor(double cameraDistance, double density)
{
    // Guard rather than trust: Theme::kMinGridDensity..kMaxGridDensity keeps
    // every caller in (0, +inf), but a static function taking a bare double
    // should not divide by (or multiply toward) zero if that guarantee is
    // ever broken upstream.
    const double d = density > 0.0 ? density : 1.0;
    if (cameraDistance < 120.0 * d) return 1.0;
    if (cameraDistance < 2500.0 * d) return 10.0;
    return 100.0;
}

double GridRenderer::firstLineAtOrBelow(double limit, double step)
{
    return std::floor(-limit / step) * step;
}

void GridRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
    if (myContext.IsNull()) return;

    const Handle(V3d_Viewer) viewer = myContext->CurrentViewer();
    if (viewer.IsNull()) return;

    // The grid's own layer - see zLayer() in the header for the full argument.
    // In short: rendered AFTER the bodies so a locked face cannot paint over
    // it, writing NO depth so it can never reject the sketch work that is
    // rendered after it, and depth TESTING so a body genuinely in front of
    // the ground grid still hides it.
    //
    // A layer of ours rather than a stock one: Graphic3d_ZLayerId_Top sits in
    // the right place but its settings are shared with anything else that
    // wants an overlay, and AddZLayer()'s implicit "before Top" placement is
    // a position this file would then be relying on without saying so.
    Graphic3d_ZLayerSettings settings;
    settings.SetName("FurnifyMe work-plane grid");
    settings.SetEnableDepthTest(Standard_True);
    settings.SetEnableDepthWrite(Standard_False);
    settings.SetClearDepth(Standard_False);
    Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_UNKNOWN;
    if (viewer->InsertLayerAfter(layer, settings, Graphic3d_ZLayerId_Default))
        myLayer = layer;
}

void GridRenderer::detach()
{
    if (!myContext.IsNull() && !myGrid.IsNull()) myContext->Remove(myGrid, Standard_False);
    myGrid.Nullify();
    myProgram.Nullify();
    myContext.Nullify();
    myLayer = Graphic3d_ZLayerId_UNKNOWN;
    invalidate();
}

void GridRenderer::invalidate()
{
    // `myBuiltExtent = 0.0` makes every geometry term of update()'s early-out
    // false at once, so no combination of camera arguments can be judged a
    // hit against a build that no longer exists.
    myBuiltExtent = 0.0;
}

bool GridRenderer::update(double cameraDistance, const gp_Pnt& cameraTarget,
                          const gp_Pnt& cameraEye, const gp_Pln& plane, double density)
{
    if (myContext.IsNull()) return false;

    const double step = minorStepFor(cameraDistance, density);
    const double major = step * 10.0;
    // Extent: comfortably beyond what a camera at this distance can see of the
    // work plane, snapped to the major step so the carpet does not crawl on
    // rebuild.
    double extent = std::clamp(cameraDistance * 6.0, 500.0, 200000.0);
    extent = std::ceil(extent / major) * major;

    // The camera's two anchor points in the PLANE's own coordinates, so the
    // grid follows the camera across a locked vertical face exactly as it
    // does across the ground.
    Standard_Real u = 0.0, v = 0.0;
    ElSLib::Parameters(plane, cameraTarget, u, v);
    Standard_Real eyeU = 0.0, eyeV = 0.0;
    ElSLib::Parameters(plane, cameraEye, eyeU, eyeV);
    const double eyeHeight = std::fabs(plane.Distance(cameraEye));

    // Everything VISUAL travels as uniforms, refreshed on every camera move
    // and every theme edit - the pool and the grazing fade follow the camera
    // live, with no geometry rebuild and no staleness ever. (OCCT re-applies
    // a program's pushed variables when it binds the program, which is every
    // frame this quad draws.)
    if (myProgram.IsNull()) {
        myProgram = new Graphic3d_ShaderProgram();
        myProgram->AttachShader(Graphic3d_ShaderObject::CreateFromSource(
            Graphic3d_TOS_VERTEX, kGridVertexShader));
        myProgram->AttachShader(Graphic3d_ShaderObject::CreateFromSource(
            Graphic3d_TOS_FRAGMENT, kGridFragmentShader));
    }
    const double poolEnd = std::min(cameraDistance * 2.4, extent);
    const double poolStart = std::min(cameraDistance * 0.8, poolEnd * 0.4);
    myProgram->PushVariableVec2("uPoolCentre",
                                Graphic3d_Vec2(float(u), float(v)));
    myProgram->PushVariableVec2("uEyeFoot",
                                Graphic3d_Vec2(float(eyeU), float(eyeV)));
    myProgram->PushVariableVec4("uRadii",
                                Graphic3d_Vec4(float(poolStart), float(poolEnd),
                                               float(eyeHeight * 5.0),
                                               float(eyeHeight * 9.0)));
    myProgram->PushVariableVec2("uSteps", Graphic3d_Vec2(float(step), float(major)));
    // Linear-space colours (the shader writes linear; the sRGB framebuffer
    // encodes on the way out) with each family's line HALF-width in pixels
    // riding in the alpha slot: minors 1px, majors 1.4px, axes 1.8px wide.
    auto pushColour = [this](const char* name, const QColor& token, double halfWidthPx) {
        const Quantity_Color linear = toOcct(token);
        myProgram->PushVariableVec4(
            name, Graphic3d_Vec4(float(linear.Red()), float(linear.Green()),
                                 float(linear.Blue()), float(halfWidthPx)));
    };
    pushColour("uMinorColour", Theme::gridMinor(), 0.4);
    pushColour("uMajorColour", Theme::gridMajor(), 0.6);
    pushColour("uAxisXColour", Theme::axisX(), 0.8);
    pushColour("uAxisYColour", Theme::axisY(), 0.8);

    // GEOMETRY staleness only - the carpet, not the picture on it: the
    // plane, the camera leaving the middle half of the built square, or the
    // extent outgrowing/undershooting it by 2x.
    const double centerU = std::round(u / major) * major;
    const double centerV = std::round(v / major) * major;
    const gp_Pnt center = ElSLib::Value(centerU, centerV, plane);
    const bool samePlane = sameFrame(myBuiltPlane, plane);
    const bool centered = center.Distance(myBuiltCenter) < myBuiltExtent * 0.25;
    const bool sized = myBuiltExtent > 0.0 &&
                       extent < myBuiltExtent * 2.0 && extent > myBuiltExtent * 0.5;
    if (samePlane && centered && sized) return false;

    rebuild(centerU, centerV, extent, plane);
    myBuiltCenter = center;
    myBuiltExtent = extent;
    myBuiltPlane = plane;
    return true;
}

void GridRenderer::rebuild(double centerU, double centerV, double extent,
                           const gp_Pln& plane)
{
    // One quad; the shader draws the grid on it. Texels carry the plane's
    // own ABSOLUTE (u, v) so the pattern and both axes are anchored to the
    // plane, not to wherever the carpet happens to be centred.
    Handle(Graphic3d_ArrayOfTriangles) quad = new Graphic3d_ArrayOfTriangles(
        4, 6, Graphic3d_ArrayFlags_VertexTexel);
    auto corner = [&](double du, double dv) {
        quad->AddVertex(ElSLib::Value(centerU + du, centerV + dv, plane),
                        gp_Pnt2d(centerU + du, centerV + dv));
    };
    corner(-extent, -extent);
    corner(extent, -extent);
    corner(extent, extent);
    corner(-extent, extent);
    quad->AddEdges(1, 2, 3);
    quad->AddEdges(1, 3, 4);

    Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
    aspect->SetInteriorStyle(Aspect_IS_SOLID);
    aspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    // The shader's alpha IS the picture - blend it, and draw the plane from
    // both sides (the user orbits below the ground plane too).
    aspect->SetAlphaMode(Graphic3d_AlphaMode_Blend);
    aspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
    aspect->SetShaderProgram(myProgram);

    Handle(GridObject) grid = new GridObject();
    grid->quad = quad;
    grid->aspect = aspect;

    if (!myGrid.IsNull()) myContext->Remove(myGrid, Standard_False);
    myGrid = grid;
    // The layer is set BEFORE the display, so the presentation is never
    // computed into the default layer and moved afterwards. Setting it on
    // the object rather than through the context is what makes that
    // possible: AIS_InteractiveObject::SetZLayer stores it on the drawer,
    // and Display reads the drawer.
    if (myLayer != Graphic3d_ZLayerId_UNKNOWN) myGrid->SetZLayer(myLayer);
    // Only actually shown while myVisible - see setVisible(). A rebuild while
    // hidden (render mode moving the camera to frame a shot) still keeps the
    // cache current, so the grid is correct the moment it is shown again
    // rather than one camera move stale.
    if (myVisible) myContext->Display(myGrid, 0, -1, Standard_False);   // mode -1: not selectable
    // No UpdateCurrentViewer() here since the QOpenGLWidget migration - see
    // update()'s own comment on the header. The caller owns the frame.
}

bool GridRenderer::setVisible(bool visible)
{
    if (myVisible == visible) return false;
    myVisible = visible;
    if (myContext.IsNull() || myGrid.IsNull()) return false;   // nothing built yet - update() will honour it
    if (myVisible) myContext->Display(myGrid, 0, -1, Standard_False);
    else           myContext->Erase(myGrid, Standard_False);
    return true;
}
