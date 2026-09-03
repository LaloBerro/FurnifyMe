#include "CameraController.h"

#include <algorithm>
#include <cmath>
#include <Bnd_Box.hxx>
#include <gp_Vec.hxx>

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
    const double azBefore = myState.azimuthDeg;
    const double elBefore = myState.elevationDeg;

    myState.azimuthDeg += dAzimuthDeg;
    myState.elevationDeg =
        std::clamp(myState.elevationDeg + dElevationDeg, kMinElevation, kMaxElevation);

    // Measured against what the camera actually DID, not against the deltas it
    // was handed. A drag that only pushes elevation further into the clamp
    // turns nothing, and neither does a zero-delta move event - and a
    // face-on view that snapped back to perspective because the mouse
    // twitched inside the dead band would be the same defect as one that
    // never returned at all.
    if (myState.azimuthDeg != azBefore || myState.elevationDeg != elBefore)
        myTemporaryOrtho = false;
}

void CameraController::lookFrom(const gp_Dir& towardEye)
{
    // The inverse of eyePosition(): with the eye at
    //   target + distance * (-cos(el) sin(az), cos(el) cos(az), sin(el))
    // a unit direction from the target to the eye gives elevation from its Z
    // component and azimuth from the other two - the same atan2(-dx, dy) form
    // setPivot() uses, and deliberately so: two derivations of one convention
    // is one derivation too many.
    myState.elevationDeg =
        std::clamp(std::asin(std::clamp(towardEye.Z(), -1.0, 1.0)) / kDegToRad,
                   kMinElevation, kMaxElevation);

    const double horizontal = std::sqrt(towardEye.X() * towardEye.X() +
                                        towardEye.Y() * towardEye.Y());
    if (horizontal > 1e-9)
        myState.azimuthDeg = std::atan2(-towardEye.X(), towardEye.Y()) / kDegToRad;
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
    // Task 6.2's fix. The straightforward formula - project world +Z onto
    // the plane perpendicular to the view direction - is undefined exactly
    // at the poles: there, view direction IS +-Z, the projection is the zero
    // vector, and gp_Dir's constructor would raise. That was why elevation
    // used to be clamped two degrees short of +-90 (see kMinElevation's own
    // comment) - the margin was standing in for a fix this formula needed
    // and never got.
    //
    // This closed form is the same up vector everywhere OFF the pole -
    // both are "which way the eye moves as elevation increases", just
    // derived two different ways, and they agree by construction (checked
    // headless across the whole sphere away from the poles) - but it stays
    // exactly this: differentiate eyePosition()'s spherical position
    //   distance * (-cos(el)sin(az), cos(el)cos(az), sin(el))
    // with respect to elevation and drop the distance factor (a positive
    // scalar, so it does not change the direction):
    //   (sin(el)sin(az), -sin(el)cos(az), cos(el))
    // which is unit length for every (az, el) - sin(el)^2 + cos(el)^2 = 1 -
    // including exactly at the pole, where it reduces to
    // (+-sin(az), -+cos(az), 0): a horizontal vector that still rotates
    // with azimuth, which is exactly "the turntable up vector at the pole
    // needs a defined azimuth" - the tangent direction of the very motion
    // that would carry the camera away from the pole.
    const double az = myState.azimuthDeg * kDegToRad;
    const double el = myState.elevationDeg * kDegToRad;
    return gp_Dir(std::sin(el) * std::sin(az), -std::sin(el) * std::cos(az), std::cos(el));
}

void CameraController::setPivot(const gp_Pnt& pivot)
{
    // Re-derive the spherical state around the new target, keeping the eye
    // position fixed. atan2(dz, horizontal) is mathematically bounded to
    // [-90, 90] already - horizontal is a magnitude, never negative - so the
    // clamp below can no longer actually bind here (it did when the clamp
    // sat at ±88, two degrees short of what the geometry alone can produce;
    // see kMinElevation's own comment). A pivot placed exactly overhead or
    // underfoot now derives exactly the pole, with the eye held EXACTLY
    // where it was - no forced drift, because there is no gap left for the
    // clamp to fight the geometry over.
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

bool CameraController::axisParameterForRay(const gp_Lin& ray, const gp_Lin& axis, double& out)
{
    // Closest approach of two skew lines, solved directly. With
    //   P(t) = a + t*u   on the axis      (u unit)
    //   Q(s) = b + s*v   on the ray       (v unit)
    // minimising |P - Q|^2 gives, writing w = a - b and c = u.v:
    //   t * (1 - c^2) = c * (w.v) - (w.u)
    // and 1 - c^2 is sin^2 of the angle between them, which is what makes
    // the parallel case singular rather than merely awkward.
    const gp_Vec u(axis.Direction());
    const gp_Vec v(ray.Direction());
    const double c = u.Dot(v);
    const double sinSquared = 1.0 - c * c;

    // See the header: within about 1.8 degrees of parallel there is no
    // useful answer, and `out` is deliberately left alone so the caller keeps
    // the last distance it had.
    constexpr double kMinSinSquared = 1.0e-3;
    if (sinSquared < kMinSinSquared) return false;

    const gp_Vec w(ray.Location(), axis.Location());
    out = (c * w.Dot(v) - w.Dot(u)) / sinSquared;
    return true;
}
