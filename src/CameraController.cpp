#include "CameraController.h"

#include <algorithm>
#include <cmath>

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
