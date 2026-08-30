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

    const CameraState& state() const { return myState; }
    void setState(const CameraState& s);

    void orbit(double dAzimuthDeg, double dElevationDeg);

    gp_Pnt eyePosition() const;
    gp_Dir viewDirection() const;   // eye -> target
    gp_Dir upVector() const;
    gp_Dir rightVector() const;

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
};
