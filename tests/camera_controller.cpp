//
// Headless tests for the turntable camera maths. No window, no GPU, no Qt -
// CameraController lives in furnify_geometry precisely so this file can exist.
//
#include "CameraController.h"

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

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
