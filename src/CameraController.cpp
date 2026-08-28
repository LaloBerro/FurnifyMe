#include "CameraController.h"

#include <algorithm>
#include <cmath>
#include <Bnd_Box.hxx>

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

void CameraController::setState(const CameraState& s)
{
    myState = s;
    myState.elevationDeg = std::clamp(myState.elevationDeg, kMinElevation, kMaxElevation);
    myState.distance = std::clamp(myState.distance, kMinDistance, kMaxDistance);
}

void CameraController::orbit(double dAzimuthDeg, double dElevationDeg)
{
    myState.azimuthDeg += dAzimuthDeg;
    myState.elevationDeg =
        std::clamp(myState.elevationDeg + dElevationDeg, kMinElevation, kMaxElevation);
}

gp_Pnt CameraController::eyePosition() const
{
    const double az = myState.azimuthDeg * kDegToRad;
    const double el = myState.elevationDeg * kDegToRad;
    const double horizontal = myState.distance * std::cos(el);
    // Azimuth 0 places the eye on +Y (looking along -Y); positive azimuth
    // rotates counterclockwise seen from above (+Z).
    return gp_Pnt(myState.target.X() - horizontal * std::sin(az),
                  myState.target.Y() + horizontal * std::cos(az),
                  myState.target.Z() + myState.distance * std::sin(el));
}

gp_Dir CameraController::viewDirection() const
{
    const gp_Pnt eye = eyePosition();
    return gp_Dir(myState.target.X() - eye.X(),
                  myState.target.Y() - eye.Y(),
                  myState.target.Z() - eye.Z());
}

gp_Dir CameraController::upVector() const
{
    // Project +Z onto the plane perpendicular to the view direction. Because
    // elevation is clamped short of the poles, this never degenerates.
    const gp_Dir view = viewDirection();
    const double dot = view.Z();   // view . (0,0,1)
    return gp_Dir(-dot * view.X(), -dot * view.Y(), 1.0 - dot * view.Z());
}

void CameraController::setPivot(const gp_Pnt& pivot)
{
    // Re-derive the spherical state around the new target, keeping the eye
    // position fixed whenever the derived elevation lies within ±88 degrees.
    // When the pivot is nearly overhead or underfoot, the derived elevation
    // exceeds the clamp and is silently clamped; the eye then moves by at most
    // distance*sin(2 deg) (~3.5% of distance) — the unavoidable price of
    // maintaining the no-roll invariant.
    const gp_Pnt eye = eyePosition();
    const double dx = eye.X() - pivot.X();
    const double dy = eye.Y() - pivot.Y();
    const double dz = eye.Z() - pivot.Z();
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < kMinDistance) return;   // pivot at the eye: nothing sensible to do

    const double horizontal = std::sqrt(dx * dx + dy * dy);
    myState.target = pivot;
    myState.distance = std::clamp(dist, kMinDistance, kMaxDistance);
    myState.elevationDeg = std::clamp(std::atan2(dz, horizontal) / kDegToRad,
                                      kMinElevation, kMaxElevation);
    if (horizontal > 1e-9) {
        myState.azimuthDeg = std::atan2(-dx, dy) / kDegToRad;
    }
    // An exactly-vertical pivot leaves azimuth undefined; keeping the previous
    // azimuth is the only sensible choice.
}

gp_Dir CameraController::rightVector() const
{
    // right = view x up; both are unit and perpendicular, so this is unit too.
    return viewDirection().Crossed(upVector());
}

void CameraController::pan(double rightUnits, double upUnits)
{
    const gp_Dir right = rightVector();
    const gp_Dir up = upVector();
    myState.target = gp_Pnt(
        myState.target.X() + right.X() * rightUnits + up.X() * upUnits,
        myState.target.Y() + right.Y() * rightUnits + up.Y() * upUnits,
        myState.target.Z() + right.Z() * rightUnits + up.Z() * upUnits);
}

void CameraController::zoom(double factor)
{
    myState.distance = std::clamp(myState.distance * factor, kMinDistance, kMaxDistance);
}

void CameraController::zoomToward(const gp_Pnt& p, double factor)
{
    // Shrink the whole eye/target frame toward p by the zoom factor: the pivot
    // stays on the same eye ray, which is what keeps it fixed on screen.
    const double clamped =
        std::clamp(myState.distance * factor, kMinDistance, kMaxDistance) / myState.distance;
    myState.target = gp_Pnt(p.X() + (myState.target.X() - p.X()) * clamped,
                            p.Y() + (myState.target.Y() - p.Y()) * clamped,
                            p.Z() + (myState.target.Z() - p.Z()) * clamped);
    myState.distance *= clamped;
}

void CameraController::frame(const Bnd_Box& box, double fovyDeg)
{
    if (box.IsVoid()) return;

    Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    myState.target = gp_Pnt((xmin + xmax) / 2.0, (ymin + ymax) / 2.0, (zmin + zmax) / 2.0);

    const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    const double radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
    const double fit = radius / std::sin(0.5 * fovyDeg * kDegToRad);
    myState.distance = std::clamp(fit * 1.1, kMinDistance, kMaxDistance);
}

double CameraController::shortestArcDelta(double fromDeg, double toDeg)
{
    double delta = std::fmod(toDeg - fromDeg, 360.0);
    if (delta > 180.0) delta -= 360.0;
    if (delta <= -180.0) delta += 360.0;
    return delta;
}
