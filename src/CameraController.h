#pragma once
//
// Turntable camera: target + azimuth + elevation + distance, up always derived
// from +Z so the horizon can never roll. Pure maths, no Qt, no visualization
// toolkits - lives in furnify_geometry so the headless tests cover it.
//
#include <Bnd_Box.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>

struct CameraState {
    gp_Pnt target{0.0, 0.0, 0.0};
    double azimuthDeg = -45.0;    // 0 looks along -Y; positive is CCW from above
    double elevationDeg = 30.0;   // 0 horizontal, +90 straight down at the target
    double distance = 700.0;      // eye-to-target, millimetres
};

class CameraController {
public:
    static constexpr double kMinElevation = -88.0;
    static constexpr double kMaxElevation = 88.0;
    static constexpr double kMinDistance = 1.0;
    static constexpr double kMaxDistance = 100000.0;

    // How the scene is projected. Two pieces of state, not one, and the split
    // is the whole feature:
    //
    //   - the BASE projection is what the user chose with the bar's toggle. It
    //     persists across sessions and nothing but that toggle moves it.
    //   - TEMPORARY ortho is engaged by a gesture that puts the camera square
    //     onto something - a gizmo arm, a locked face - because a face-on view
    //     with perspective convergence is not a face-on view. It is a loan,
    //     not a mode: the first orbit hands it back and the base projection
    //     returns. So does the toggle itself (see
    //     OcctViewWidget::setBaseProjection) - a control whose whole subject
    //     is the projection must never be outvoted by a loan the user did not
    //     ask for.
    //
    // effectiveOrtho() is what the renderer follows. A user in perspective who
    // clicks an arm gets one orthographic look and their perspective back the
    // moment they orbit; a user who chose Ortho never leaves it.
    enum class Projection { Perspective, Orthographic };

    const CameraState& state() const { return myState; }
    void setState(const CameraState& s);

    void setBaseProjection(Projection p) { myBaseProjection = p; }
    Projection baseProjection() const { return myBaseProjection; }
    void setTemporaryOrtho(bool on) { myTemporaryOrtho = on; }
    bool temporaryOrtho() const { return myTemporaryOrtho; }
    bool effectiveOrtho() const
    {
        return myTemporaryOrtho || myBaseProjection == Projection::Orthographic;
    }

    // Clears the temporary flag when it actually turns the camera. Pan, zoom
    // and every setState() route (the snap flights included) deliberately do
    // NOT: panning across a face-on drawing is ordinary drafting, and a
    // fly-to that cleared its own loan on the first animation frame would
    // never be orthographic at all.
    void orbit(double dAzimuthDeg, double dElevationDeg);

    gp_Pnt eyePosition() const;
    gp_Dir viewDirection() const;   // eye -> target
    gp_Dir upVector() const;
    gp_Dir rightVector() const;

    // Puts the eye on `towardEye` as seen from the target, without moving the
    // target or changing the distance - the turntable's azimuth and elevation
    // solved backwards from a direction. This is how a face lock flies square
    // onto a face: hand it the face's OUTWARD normal and viewDirection() comes
    // back antiparallel to it.
    //
    // Elevation is clamped like everything else, so a horizontal face lands at
    // 88 degrees rather than 90 - two degrees off dead-on, the unavoidable
    // price of the no-roll invariant (the same clamp setViewTop() meets). A
    // vertical direction leaves azimuth undefined, and the previous azimuth is
    // kept, exactly as setPivot() does.
    void lookFrom(const gp_Dir& towardEye);

    void setPivot(const gp_Pnt& pivot);
    void pan(double rightUnits, double upUnits);
    void zoom(double factor);
    void zoomToward(const gp_Pnt& p, double factor);
    void frame(const Bnd_Box& box, double fovyDeg);

    // Signed shortest rotation from one angle to another, in (-180, 180].
    static double shortestArcDelta(double fromDeg, double toDeg);

    // Where along `axis` the cursor is pointing: the parameter, measured from
    // the axis's own location and signed along its direction, of the point on
    // `axis` closest to `ray`. This is the whole of the face-pull drag
    // mapping - `ray` is the unprojected cursor (the same
    // V3d_View::ConvertWithProj ray the sketch unprojection uses) and `axis`
    // is the outward normal through the pulled face's centre, so the caller
    // subtracts the parameter it recorded at the press to get a signed
    // distance to pull by.
    //
    // The two lines are generally SKEW - there is no intersection to find,
    // which is why this is a closest approach rather than a ray/plane hit -
    // and the answer is exact whenever they are not parallel.
    //
    // Returns false, leaving `out` untouched, when the ray lies within about
    // 1.8 degrees of the axis (sin^2 of the angle below 1e-3). Looking down
    // the arrow, one pixel of cursor movement means an unbounded jump in
    // distance: there is no useful answer there, so the caller keeps whatever
    // value it last had rather than the model exploding. Qt-free and in
    // furnify_geometry so tests/camera_controller.cpp covers it with no
    // window.
    static bool axisParameterForRay(const gp_Lin& ray, const gp_Lin& axis, double& out);

private:
    CameraState myState;
    Projection myBaseProjection = Projection::Perspective;
    bool myTemporaryOrtho = false;
};
