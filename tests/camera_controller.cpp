//
// Headless tests for the turntable camera maths. No window, no GPU, no Qt -
// CameraController lives in furnify_geometry precisely so this file can exist.
//
#include "CameraController.h"

#include <Bnd_Box.hxx>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkNear(double actual, double expected, double tol, const std::string& what)
{
    const bool ok = std::fabs(actual - expected) <= tol;
    std::printf("%-6s %s (got %.6f, expected %.6f)\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual, expected);
    if (!ok) ++g_failures;
}
}  // namespace

int main()
{
    // --- defaults are the spec's startup view --------------------------------
    {
        CameraController cam;
        checkNear(cam.state().azimuthDeg, -45.0, 1e-9, "startup azimuth is -45");
        checkNear(cam.state().elevationDeg, 30.0, 1e-9, "startup elevation is +30");
        checkNear(cam.state().distance, 700.0, 1e-9, "startup distance is 700mm");
        checkNear(cam.state().target.Distance(gp_Pnt(0, 0, 0)), 0.0, 1e-9,
                  "startup target is the origin");
    }

    // --- orbit accumulates and clamps ----------------------------------------
    {
        CameraController cam;
        cam.orbit(10.0, 5.0);
        checkNear(cam.state().azimuthDeg, -35.0, 1e-9, "orbit adds azimuth");
        checkNear(cam.state().elevationDeg, 35.0, 1e-9, "orbit adds elevation");

        cam.orbit(0.0, 500.0);
        checkNear(cam.state().elevationDeg, 90.0, 1e-9, "elevation clamps at +90 - the true pole");
        cam.orbit(0.0, -500.0);
        checkNear(cam.state().elevationDeg, -90.0, 1e-9, "elevation clamps at -90");
    }

    // --- eye position geometry ------------------------------------------------
    {
        // Elevation 0, azimuth 0 looks along -Y: the eye sits at +Y.
        CameraController cam;
        CameraState s;
        s.azimuthDeg = 0.0; s.elevationDeg = 0.0; s.distance = 100.0;
        cam.setState(s);
        const gp_Pnt eye = cam.eyePosition();
        checkNear(eye.X(), 0.0, 1e-6, "az 0 el 0: eye X is 0");
        checkNear(eye.Y(), 100.0, 1e-6, "az 0 el 0: eye Y is +distance");
        checkNear(eye.Z(), 0.0, 1e-6, "az 0 el 0: eye Z is 0");

        // Elevation raises the eye: at +30 the height is d*sin(30) = 50.
        s.elevationDeg = 30.0;
        cam.setState(s);
        checkNear(cam.eyePosition().Z(), 50.0, 1e-6, "el +30: eye height is d*sin(30)");

        // Eye-to-target distance always equals state().distance.
        checkNear(cam.eyePosition().Distance(s.target), 100.0, 1e-6,
                  "eye distance equals the state distance");
    }

    // --- up vector never rolls -----------------------------------------------
    {
        CameraController cam;
        for (double az = -720.0; az <= 720.0; az += 37.0) {
            for (double el = -88.0; el <= 88.0; el += 22.0) {
                CameraState s;
                s.azimuthDeg = az; s.elevationDeg = el; s.distance = 500.0;
                cam.setState(s);
                const gp_Dir up = cam.upVector();
                // Turntable invariant: up always has a positive Z component and
                // is perpendicular to the view direction.
                if (up.Z() <= 0.0) { check(false, "up vector lost its +Z component"); goto rollDone; }
                if (std::fabs(up.Dot(cam.viewDirection())) > 1e-6) {
                    check(false, "up vector not perpendicular to view direction"); goto rollDone;
                }
            }
        }
        check(true, "up vector keeps +Z and stays perpendicular across the whole sphere");
    rollDone:;
    }

    // --- up vector at the exact pole (Task 6.2) --------------------------------
    // The formula upVector() uses away from the pole - projecting world +Z
    // onto the plane perpendicular to the view direction - is genuinely
    // undefined exactly AT elevation +-90 (view direction is parallel to
    // +Z there, so the projection is the zero vector, and gp_Dir's own
    // constructor would raise). The fix replaces it with an equivalent
    // closed form that is well-defined everywhere, including the pole. This
    // pins that directly: construction does not throw, the vector stays
    // unit and perpendicular to the view direction, and - the part that
    // matters for orbiting away from a Top view - it keeps varying with
    // azimuth exactly at the pole rather than collapsing to one fixed value.
    {
        CameraController cam;
        CameraState s;
        s.distance = 500.0;

        s.elevationDeg = 90.0;
        for (double az : {0.0, 90.0, 180.0, 270.0, -30.0}) {
            s.azimuthDeg = az;
            cam.setState(s);
            const gp_Dir up = cam.upVector();   // would throw on a zero vector
            checkNear(up.Dot(cam.viewDirection()), 0.0, 1e-9,
                      "at the pole, up stays perpendicular to the view direction");
        }
        s.azimuthDeg = 0.0;
        cam.setState(s);
        const gp_Dir upAz0 = cam.upVector();
        s.azimuthDeg = 90.0;
        cam.setState(s);
        const gp_Dir upAz90 = cam.upVector();
        check(upAz0.Angle(upAz90) > 1.0e-3,
              "and it genuinely rotates with azimuth at the pole - a defined "
              "azimuth, not a value the pole discards");

        s.elevationDeg = -90.0;
        s.azimuthDeg = 15.0;
        cam.setState(s);
        const gp_Dir upFloor = cam.upVector();   // would throw on a zero vector
        checkNear(upFloor.Dot(cam.viewDirection()), 0.0, 1e-9,
                  "the floor pole is just as well-defined as the ceiling");
    }

    // --- Milestone 5 item 4: the squared Top/Bottom azimuth --------------------
    // The bug: a user pressing "Top" got a different-looking view depending on
    // which azimuth the camera happened to hold beforehand, because
    // upVector() (correctly, per the block just above) keeps varying with
    // azimuth exactly at the pole - nothing forced that azimuth to a fixed
    // value before this task. kTopBottomSquaredAzimuthDeg is the one azimuth
    // (180 degrees - Back's own) that squares BOTH poles onto world X/Y: at
    // Top, up is exactly world +Y (the ordinary engineering top view, +X
    // screen-right); at Bottom, up is exactly world -Y (+X still screen-right,
    // +Y now screen-down - the standard convention for a view from
    // underneath). Pinned from several different starting azimuths, since the
    // whole point is that the result no longer depends on where the camera
    // started.
    {
        CameraController cam;
        CameraState s;
        s.distance = 500.0;

        for (double startAz : {-45.0, 0.0, 33.0, 90.0, 271.5}) {
            s.azimuthDeg = startAz;
            s.elevationDeg = 90.0;
            cam.setState(s);
            s.azimuthDeg = CameraController::kTopBottomSquaredAzimuthDeg;
            cam.setState(s);
            const gp_Dir up = cam.upVector();
            checkNear(up.X(), 0.0, 1e-9, "Top squared: up has no X component");
            checkNear(up.Y(), 1.0, 1e-9, "Top squared: up is exactly world +Y");
            checkNear(up.Z(), 0.0, 1e-9, "Top squared: up has no Z component (it is the pole)");

            const gp_Dir right = cam.rightVector();
            checkNear(right.X(), 1.0, 1e-9,
                      "Top squared: right is exactly world +X, the ordinary "
                      "engineering top view");

            s.elevationDeg = -90.0;
            cam.setState(s);
            const gp_Dir upBottom = cam.upVector();
            checkNear(upBottom.X(), 0.0, 1e-9, "Bottom squared: up has no X component");
            checkNear(upBottom.Y(), -1.0, 1e-9,
                      "Bottom squared: up is exactly world -Y - +Y reads screen-down, "
                      "the standard convention viewed from underneath");
            const gp_Dir rightBottom = cam.rightVector();
            checkNear(rightBottom.X(), 1.0, 1e-9,
                      "Bottom squared: right is exactly world +X too, same as Top");
        }

        // And it really is Back's own azimuth - tilting continuously up or
        // down from Back must not roll the screen at all: right stays world
        // +X the entire way, only up sweeps from +Z to +Y (or to -Y).
        s.azimuthDeg = CameraController::kTopBottomSquaredAzimuthDeg;
        s.elevationDeg = 0.0;
        cam.setState(s);
        const gp_Dir backUp = cam.upVector();
        const gp_Dir backRight = cam.rightVector();
        checkNear(backUp.Z(), 1.0, 1e-9, "Back itself: up is world +Z");
        checkNear(backRight.X(), 1.0, 1e-9, "Back itself: right is world +X");
        for (double el : {30.0, 60.0, 89.0, -30.0, -60.0, -89.0}) {
            s.elevationDeg = el;
            cam.setState(s);
            checkNear(cam.rightVector().X(), 1.0, 1e-9,
                      "right stays world +X all the way from Back to the poles - "
                      "no roll anywhere along the meridian");
        }
    }

    // --- setState clamps ------------------------------------------------------
    {
        CameraController cam;
        CameraState s;
        s.elevationDeg = 200.0; s.distance = 0.0001;
        cam.setState(s);
        checkNear(cam.state().elevationDeg, 90.0, 1e-9, "setState clamps elevation");
        checkNear(cam.state().distance, 1.0, 1e-9, "setState clamps distance to 1mm");
    }

    // --- setPivot preserves the eye ------------------------------------------
    {
        CameraController cam;
        const gp_Pnt eyeBefore = cam.eyePosition();
        cam.setPivot(gp_Pnt(200.0, -150.0, 40.0));
        const gp_Pnt eyeAfter = cam.eyePosition();
        checkNear(eyeBefore.Distance(eyeAfter), 0.0, 1e-6,
                  "setPivot leaves the eye where it was");
        checkNear(cam.state().target.Distance(gp_Pnt(200.0, -150.0, 40.0)), 0.0, 1e-6,
                  "setPivot re-targets the pivot point");
        // And orbiting afterwards keeps the pivot fixed by construction.
        cam.orbit(30.0, -10.0);
        checkNear(cam.state().target.Distance(gp_Pnt(200.0, -150.0, 40.0)), 0.0, 1e-6,
                  "orbit after setPivot keeps the pivot as target");
    }

    // --- setPivot at the pole: no clamp fighting the geometry any more --------
    // Before Task 6.2's fix, the clamp sat at +-88 - two degrees short of
    // the pole - so a pivot nearly underfoot forced the DERIVED elevation
    // (atan2(dz, horizontal), which is mathematically bounded to [-90, 90]
    // already, since horizontal is a magnitude) past that artificial
    // ceiling, and the eye had to move to keep the pose valid. Now the
    // clamp IS the true ceiling, so it can no longer bind here: a pivot
    // placed EXACTLY below the eye derives exactly the pole, and the eye
    // does not move at all to get there.
    {
        CameraController cam;
        CameraState s;
        s.azimuthDeg = 0.0; s.elevationDeg = 80.0; s.distance = 100.0;
        cam.setState(s);
        const gp_Pnt eyeBefore = cam.eyePosition();
        // A point on the ground exactly below the eye.
        const gp_Pnt pivot(eyeBefore.X(), eyeBefore.Y(), 0.0);
        cam.setPivot(pivot);
        checkNear(cam.state().elevationDeg, CameraController::kMaxElevation, 1e-9,
                  "a pivot dead below the eye derives exactly the pole, not a "
                  "clamped approximation of it");
        checkNear(cam.eyePosition().Distance(eyeBefore), 0.0, 1e-6,
                  "and the eye does not move to get there - the clamp no longer "
                  "fights the geometry");
    }

    // --- pan moves the target in the view plane -------------------------------
    {
        CameraController cam;
        CameraState s;
        s.azimuthDeg = 0.0; s.elevationDeg = 0.0; s.distance = 100.0;
        cam.setState(s);
        // Facing -Y with up +Z, right = view x up = -X: standing at +Y looking
        // south, your right hand points west. Up for this pose is +Z exactly.
        cam.pan(10.0, 5.0);
        checkNear(cam.state().target.X(), -10.0, 1e-6, "pan right moves target -X here");
        checkNear(cam.state().target.Z(), 5.0, 1e-6, "pan up moves target +Z here");
        checkNear(cam.state().target.Y(), 0.0, 1e-6, "pan does not move along the view axis");
    }

    // --- zoomToward keeps the pivot on its eye ray ----------------------------
    {
        CameraController cam;
        const gp_Pnt pivot(120.0, 80.0, 0.0);
        const gp_Pnt eye0 = cam.eyePosition();
        // Direction from eye to pivot before zooming.
        gp_Dir before(pivot.X() - eye0.X(), pivot.Y() - eye0.Y(), pivot.Z() - eye0.Z());
        cam.zoomToward(pivot, 0.5);
        const gp_Pnt eye1 = cam.eyePosition();
        gp_Dir after(pivot.X() - eye1.X(), pivot.Y() - eye1.Y(), pivot.Z() - eye1.Z());
        checkNear(before.Angle(after), 0.0, 1e-6,
                  "zoomToward keeps the pivot on the same eye ray");
        checkNear(cam.state().distance, 350.0, 1e-6, "zoomToward halves the distance");
        check(cam.eyePosition().Distance(pivot) < eye0.Distance(pivot),
              "zooming in moves the eye toward the pivot");
    }

    // --- zoom clamps ----------------------------------------------------------
    {
        CameraController cam;
        cam.zoom(1e-9);
        checkNear(cam.state().distance, 1.0, 1e-9, "zoom clamps at 1mm");
        cam.zoom(1e12);
        checkNear(cam.state().distance, 100000.0, 1e-9, "zoom clamps at 100m");
    }

    // --- frame fits a box -----------------------------------------------------
    {
        CameraController cam;
        Bnd_Box box;
        box.Update(-50.0, -50.0, 0.0, 50.0, 50.0, 100.0);
        cam.frame(box, 45.0);
        checkNear(cam.state().target.X(), 0.0, 1e-6, "frame centres X");
        checkNear(cam.state().target.Z(), 50.0, 1e-6, "frame centres Z");
        // Radius of that box is sqrt(50^2+50^2+50^2) ~ 86.6; distance must at
        // least cover radius/sin(fov/2) with the ~10% margin, and not be silly.
        const double radius = 86.6025;
        const double minimum = radius / std::sin(22.5 * 3.14159265358979323846 / 180.0);
        check(cam.state().distance >= minimum * 1.05 && cam.state().distance <= minimum * 1.3,
              "frame distance covers the bounding sphere with margin");
        // Angles are preserved - framing changes where you look, not from where.
        checkNear(cam.state().azimuthDeg, -45.0, 1e-9, "frame keeps azimuth");
    }

    // --- shortest arc ---------------------------------------------------------
    {
        checkNear(CameraController::shortestArcDelta(350.0, 10.0), 20.0, 1e-9,
                  "350 -> 10 goes +20, not -340");
        checkNear(CameraController::shortestArcDelta(10.0, 350.0), -20.0, 1e-9,
                  "10 -> 350 goes -20");
        checkNear(CameraController::shortestArcDelta(0.0, 180.0), 180.0, 1e-9,
                  "opposite angles resolve to +180");
        checkNear(CameraController::shortestArcDelta(-45.0, -45.0), 0.0, 1e-9,
                  "no movement is zero");
    }

    // --- shortestArcDelta boundaries ------------------------------------------
    {
        checkNear(CameraController::shortestArcDelta(0.0, -180.0), 180.0, 1e-9,
                  "0 -> -180 resolves to +180, the half-open boundary");
        checkNear(CameraController::shortestArcDelta(0.0, 725.0), 5.0, 1e-9,
                  "deltas beyond a full turn reduce correctly");
    }

    // --- projection: base, temporary, and what resolves ------------------------
    // Two pieces of state, and the whole feature is which gestures move which.
    {
        CameraController cam;
        check(cam.baseProjection() == CameraController::Projection::Perspective,
              "the camera starts in perspective");
        check(!cam.temporaryOrtho(), "with nothing borrowed");
        check(!cam.effectiveOrtho(), "so nothing is drawn orthographically");

        cam.setBaseProjection(CameraController::Projection::Orthographic);
        check(cam.baseProjection() == CameraController::Projection::Orthographic,
              "the base projection round-trips");
        check(cam.effectiveOrtho(), "and orthographic base resolves orthographic");

        cam.setBaseProjection(CameraController::Projection::Perspective);
        cam.setTemporaryOrtho(true);
        check(cam.baseProjection() == CameraController::Projection::Perspective,
              "a borrowed orthographic look leaves the base mode alone");
        check(cam.effectiveOrtho(), "but it is what gets drawn");
    }

    // --- an orbit hands the loan back, and nothing else does ------------------
    {
        CameraController cam;
        cam.setTemporaryOrtho(true);
        cam.pan(50.0, -20.0);
        check(cam.effectiveOrtho(),
              "panning across a face-on view keeps it - that is drafting, not orbiting");
        cam.zoom(0.5);
        check(cam.effectiveOrtho(), "and so does zooming in");
        cam.zoomToward(gp_Pnt(30.0, 10.0, 0.0), 1.4);
        check(cam.effectiveOrtho(), "and zooming toward a point");
        cam.setPivot(gp_Pnt(10.0, 10.0, 0.0));
        check(cam.effectiveOrtho(), "and re-pivoting");

        Bnd_Box box;
        box.Update(-100.0, -100.0, 0.0, 100.0, 100.0, 50.0);
        cam.frame(box, 45.0);
        check(cam.effectiveOrtho(), "and framing a box");

        CameraState pose = cam.state();
        pose.azimuthDeg = 12.0;
        cam.setState(pose);
        check(cam.effectiveOrtho(),
              "and setState - which is how every snap flight lands, so clearing "
              "there would mean no flight was ever orthographic at all");

        cam.orbit(0.0, 0.0);
        check(cam.effectiveOrtho(), "a move event that turns nothing keeps it too");

        cam.orbit(3.0, 0.0);
        check(!cam.effectiveOrtho(), "the first orbit that turns the camera hands it back");
        check(cam.baseProjection() == CameraController::Projection::Perspective,
              "leaving the base mode as it was");
    }

    // --- an orbit at the elevation clamp turns nothing, and keeps the loan -----
    {
        CameraController cam;
        CameraState pose;
        pose.elevationDeg = CameraController::kMaxElevation;
        cam.setState(pose);
        cam.setTemporaryOrtho(true);
        cam.orbit(0.0, 10.0);
        check(cam.effectiveOrtho(),
              "a drag that only pushes further into the clamp changes no angle, "
              "so it borrows nothing back");
        cam.orbit(0.0, -10.0);
        check(!cam.effectiveOrtho(), "the drag back out does");
    }

    // --- a chosen orthographic mode survives everything -----------------------
    {
        CameraController cam;
        cam.setBaseProjection(CameraController::Projection::Orthographic);
        cam.setTemporaryOrtho(true);
        cam.orbit(20.0, 10.0);
        check(cam.effectiveOrtho(),
              "orbiting clears the loan but never the mode the user chose");
    }

    // --- setBaseProjection is state; dropping the loan is the toggle's job ----
    // The split is deliberate and worth pinning. CameraController's setter
    // moves ONE field, so a caller restoring a stored preference does not have
    // to think about a loan that cannot exist yet. The user-facing toggle -
    // OcctViewWidget::setBaseProjection, which gui_smoke covers - drops the
    // loan as well, because a control whose whole subject is the projection
    // must never be outvoted by one. Both halves are tested; only their
    // composition is the shell's.
    {
        CameraController cam;
        cam.setTemporaryOrtho(true);
        cam.setBaseProjection(CameraController::Projection::Perspective);
        check(cam.temporaryOrtho(),
              "the bare setter leaves the loan alone - it is one field, not a policy");
        cam.setTemporaryOrtho(false);
        check(!cam.effectiveOrtho(),
              "and with the loan handed back, perspective is what resolves");
    }

    // --- lookFrom aims the eye down a given direction -------------------------
    // The whole of the face-lock flight: hand it a face's OUTWARD normal and
    // the camera looks straight back along it.
    {
        CameraController cam;
        cam.lookFrom(gp_Dir(1.0, 0.0, 0.0));
        checkNear(cam.state().azimuthDeg, -90.0, 1e-9, "an eye on +X is azimuth -90");
        checkNear(cam.state().elevationDeg, 0.0, 1e-9, "at elevation 0");
        checkNear(cam.viewDirection().Dot(gp_Dir(1.0, 0.0, 0.0)), -1.0, 1e-9,
                  "and the view direction is antiparallel to it");

        cam.lookFrom(gp_Dir(0.0, 1.0, 0.0));
        checkNear(cam.state().azimuthDeg, 0.0, 1e-9, "an eye on +Y is azimuth 0");
        checkNear(cam.viewDirection().Dot(gp_Dir(0.0, 1.0, 0.0)), -1.0, 1e-9,
                  "antiparallel again");

        // A slanted face - nothing special about the axes.
        const gp_Dir slanted(1.0, -2.0, 0.5);
        cam.lookFrom(slanted);
        checkNear(cam.viewDirection().Dot(slanted), -1.0, 1e-9,
                  "an off-axis normal is met just as squarely");

        // The distance and the target are the caller's business; lookFrom must
        // not touch either.
        CameraState pose;
        pose.distance = 421.0;
        pose.target = gp_Pnt(30.0, -40.0, 12.0);
        cam.setState(pose);
        cam.lookFrom(gp_Dir(0.0, -1.0, 0.0));
        checkNear(cam.state().distance, 421.0, 1e-9, "lookFrom leaves the distance alone");
        checkNear(cam.state().target.Distance(gp_Pnt(30.0, -40.0, 12.0)), 0.0, 1e-9,
                  "and the target");
    }

    // --- lookFrom at the poles: exact, and azimuth kept ------------------------
    {
        CameraController cam;
        CameraState pose;
        pose.azimuthDeg = 33.0;
        cam.setState(pose);
        cam.lookFrom(gp_Dir(0.0, 0.0, 1.0));
        checkNear(cam.state().elevationDeg, CameraController::kMaxElevation, 1e-9,
                  "a horizontal face is met exactly at the pole - the elevation "
                  "clamp IS 90 now, not two degrees short of it");
        checkNear(cam.state().azimuthDeg, 33.0, 1e-9,
                  "and a vertical direction leaves azimuth undefined, so it is kept");
        // Task 6.2's fix, pinned exactly rather than loosely: this used to be
        // "< -0.999" because the old clamp held the view direction two
        // degrees off antiparallel by construction. It is dead on now.
        checkNear(cam.viewDirection().Dot(gp_Dir(0.0, 0.0, 1.0)), -1.0, 1e-9,
                  "which is exactly antiparallel, not merely close");

        cam.lookFrom(gp_Dir(0.0, 0.0, -1.0));
        checkNear(cam.state().elevationDeg, CameraController::kMinElevation, 1e-9,
                  "and the floor clamps the same way");
        checkNear(cam.viewDirection().Dot(gp_Dir(0.0, 0.0, -1.0)), -1.0, 1e-9,
                  "exactly antiparallel there too");
    }

    // --- the face-pull drag mapping -------------------------------------------
    // The whole of PullArrow's drag maths: where along the face's outward
    // normal is the cursor pointing? Qt-free and here rather than in the
    // widget, so it is covered without a window - the same reason the
    // ray/plane unprojection lives in SketchController.
    {
        const gp_Lin zAxis(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        double t = -12345.0;

        // A ray that meets the axis square on: the answer is simply where it
        // crosses.
        check(CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(100.0, 0.0, 50.0), gp_Dir(-1.0, 0.0, 0.0)), zAxis, t),
              "a perpendicular ray has a closest-approach parameter");
        checkNear(t, 50.0, 1e-9, "and it is the height where the ray crosses the axis");

        // A skew ray never touches the axis at all - the closest point is
        // still perfectly well defined, which is the whole reason this is a
        // closest-approach rather than an intersection.
        check(CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(100.0, 10.0, 30.0), gp_Dir(0.0, -1.0, 0.0)), zAxis, t),
              "a skew ray still resolves");
        checkNear(t, 30.0, 1e-9, "to the closest point on the axis, not an intersection");

        // The parameter is measured from the axis's own origin and signed
        // along its direction: a face pull needs "how far out", and inward
        // has to come back negative.
        const gp_Lin offset(gp_Pnt(5.0, 5.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
        check(CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(105.0, 5.0, -20.0), gp_Dir(-1.0, 0.0, 0.0)), offset, t),
              "an axis away from the origin resolves too");
        checkNear(t, -20.0, 1e-9, "and behind its origin the parameter is negative");

        // Looking straight down the arrow, a pixel of cursor movement means
        // an unbounded jump in distance. Refused, so the caller keeps the
        // last value instead of the model exploding.
        double untouched = 7.0;
        check(!CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(0.0, 0.0, 100.0), gp_Dir(0.0, 0.0, -1.0)), zAxis, untouched),
              "a ray straight down the axis is refused");
        checkNear(untouched, 7.0, 1e-12, "and the caller's value is left untouched");

        // The refusal is a band, not an exact-parallel test - it has to catch
        // the nearly-parallel case that is numerically just as bad. These two
        // bracket the documented threshold.
        check(!CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(10.0, 0.0, 0.0), gp_Dir(0.03, 0.0, 1.0)), zAxis, t),
              "a ray within ~1.8 degrees of the axis is refused too");
        check(CameraController::axisParameterForRay(
                  gp_Lin(gp_Pnt(10.0, 0.0, 0.0), gp_Dir(0.05, 0.0, 1.0)), zAxis, t),
              "and one just outside that band still resolves");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
