#pragma once
//
// Turntable camera: target + azimuth + elevation + distance, up always derived
// from +Z so the horizon can never roll. Pure maths, no Qt, no visualization
// toolkits - lives in furnify_geometry so the headless tests cover it.
//
#include <Bnd_Box.hxx>
#include <gp_Dir.hxx>
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

private:
    CameraState myState;
};
