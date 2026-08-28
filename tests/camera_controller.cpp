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
        checkNear(cam.state().elevationDeg, 88.0, 1e-9, "elevation clamps at +88");
        cam.orbit(0.0, -500.0);
        checkNear(cam.state().elevationDeg, -88.0, 1e-9, "elevation clamps at -88");
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

    // --- setState clamps ------------------------------------------------------
    {
        CameraController cam;
        CameraState s;
        s.elevationDeg = 200.0; s.distance = 0.0001;
        cam.setState(s);
        checkNear(cam.state().elevationDeg, 88.0, 1e-9, "setState clamps elevation");
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

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
