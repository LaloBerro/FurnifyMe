//
// Drives the real MainWindow with synthetic Qt events delivered straight to the
// widgets. Nothing goes through the OS input queue, so this never moves the
// cursor, never steals focus, and the machine stays usable while it runs.
//
// A window does still appear: OCCT's V3d_View needs a real native window with a
// GL surface, so `-platform offscreen` is not an option. It is shown without
// activating and parked in a corner.
//
// Not part of ctest: it needs a GPU and a window server. The headless tests stay
// the CI gate; this covers the wiring they cannot reach - that clicks become
// sketch points, that picking returns the right solids, that the document and
// the viewport stay in agreement.
//
#include "CameraController.h"
#include "DocumentModel.h"
#include "GridRenderer.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "SketchController.h"
#include "AxisGizmo.h"
#include "Theme.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "ViewportOverlay.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QPointF>
#include <QStatusBar>
#include <QString>

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const QString& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", qPrintable(what));
    if (!condition) ++g_failures;
}

// Lets the event loop breathe so Qt delivers exposure/resize and OCCT redraws.
void settle(int ms = 250)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void clickAt(QWidget* target, const QPointF& pos,
             Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPointF global = target->mapToGlobal(pos);

    QMouseEvent press(QEvent::MouseButtonPress, pos, global,
                      Qt::LeftButton, Qt::LeftButton, mods);
    QCoreApplication::sendEvent(target, &press);

    QMouseEvent release(QEvent::MouseButtonRelease, pos, global,
                        Qt::LeftButton, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &release);

    settle(80);
}

// Button drag delivered as press/move/release, for camera tests.
void dragButton(QWidget* target, const QPointF& from, const QPointF& to,
                Qt::MouseButton button, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, target->mapToGlobal(from),
                      button, button, mods);
    QCoreApplication::sendEvent(target, &press);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        QMouseEvent move(QEvent::MouseMove, p, target->mapToGlobal(p),
                         Qt::NoButton, button, mods);
        QCoreApplication::sendEvent(target, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, to, target->mapToGlobal(to),
                        button, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &release);
    settle(120);
}

// Actions are looked up by their visible text, minus the mnemonic marker.
QAction* action(MainWindow& window, const QString& label)
{
    for (QAction* candidate : window.findChildren<QAction*>()) {
        if (candidate->text().remove(QLatin1Char('&')) == label) return candidate;
    }
    return nullptr;
}

bool trigger(MainWindow& window, const QString& label)
{
    QAction* found = action(window, label);
    if (!found) {
        std::printf("[FAIL] no action named '%s'\n", qPrintable(label));
        ++g_failures;
        return false;
    }
    found->trigger();
    settle(120);
    return true;
}

// Draws a quad by clicking four points given as fractions of the viewport, so
// the test does not depend on a particular window size.
void sketchQuad(MainWindow& window, double x0, double y0, double x1, double y1)
{
    OcctViewWidget* view = window.view();
    const double w = view->width();
    const double h = view->height();

    clickAt(view, QPointF(x0 * w, y0 * h));
    clickAt(view, QPointF(x1 * w, y0 * h));
    clickAt(view, QPointF(x1 * w, y1 * h));
    clickAt(view, QPointF(x0 * w, y1 * h));
}

}  // namespace

int main(int argc, char* argv[])
{
#ifndef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    // Exercise what actually ships: main.cpp themes the app before building the
    // window, so the test must too, or it checks an app nobody runs.
    Theme::apply(app);

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

    MainWindow window;
    // Show without taking focus: the point of this harness is that the user can
    // keep working while it runs.
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1200, 800);
    window.move(40, 40);
    window.show();
    settle(900);
    window.view()->setAnimationsEnabled(false);   // deterministic camera for the suite

    OcctViewWidget* view = window.view();
    check(view != nullptr && view->width() > 100, "viewport has a usable size");

    // --- camera startup state -------------------------------------------------
    {
        const CameraState& cam = view->camera().state();
        check(std::fabs(cam.azimuthDeg - (-45.0)) < 1e-6, "startup azimuth is -45");
        check(std::fabs(cam.elevationDeg - 30.0) < 1e-6, "startup elevation is +30");
        check(std::fabs(cam.distance - 700.0) < 1e-6, "startup distance is 700mm");
    }

    // --- turntable input ------------------------------------------------------
    {
        const double az0 = view->camera().state().azimuthDeg;
        const gp_Dir up0 = view->camera().upVector();
        const gp_Pnt orbitTarget0 = view->camera().state().target;
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5 + 100.0, view->height() * 0.5),
                   Qt::RightButton);
        check(std::fabs(view->camera().state().azimuthDeg - az0) > 5.0,
              "a horizontal RMB drag orbits azimuth");
        check(view->camera().upVector().Z() > 0.0 && up0.Z() > 0.0,
              "orbiting never rolls: up keeps its +Z component");
        // Unity-style: orbiting spins around the current view target, so the
        // target itself must not move - no cursor-anchored re-pivoting.
        check(view->camera().state().target.Distance(orbitTarget0) < 1e-6,
              "orbiting leaves the view target where it was");

        const gp_Pnt target0 = view->camera().state().target;
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5 + 80.0, view->height() * 0.5 + 40.0),
                   Qt::MiddleButton);
        check(view->camera().state().target.Distance(target0) > 1.0,
              "an MMB drag pans the target");

        // Elevation clamp holds through input: a huge vertical drag stops at 88.
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5, view->height() * 0.5 + 2000.0),
                   Qt::RightButton);
        check(view->camera().state().elevationDeg >= -88.0 - 1e-6 &&
              view->camera().state().elevationDeg <= 88.0 + 1e-6,
              "elevation stays inside the clamp under wild input");

        // Restore the exact startup pose: every later check clicks at fractions
        // tuned for it, and this block has dragged the camera all over the sky.
        view->camera().setState(CameraState{});
        trigger(window, QStringLiteral("Axonometric"));
        settle(200);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3,
              "camera restored to the startup pose for the rest of the suite");
    }

    // --- axis gizmo -----------------------------------------------------------
    {
        AxisGizmo* gizmo = window.findChild<AxisGizmo*>();
        check(gizmo != nullptr, "the viewport has an axis gizmo");
        if (gizmo) {
            // Clicking the +Z cone looks down from above.
            clickAt(gizmo, gizmo->tipCenter(2, true));
            settle(150);
            check(std::fabs(view->camera().state().elevationDeg - 88.0) < 1e-3,
                  "clicking the +Z cone goes to Top");
            check(gizmo->labelText().contains(QStringLiteral("Top")),
                  "the label reads Top when aligned");

            // Clicking the -Y ball views from behind.
            clickAt(gizmo, gizmo->tipCenter(1, false));
            settle(150);
            check(std::fabs(std::fabs(view->camera().state().azimuthDeg) - 180.0) < 1e-3 &&
                  std::fabs(view->camera().state().elevationDeg) < 1e-3,
                  "clicking the -Y ball goes to Back");

            // Clicking the label chip returns home to the axonometric view.
            clickAt(gizmo, gizmo->labelCenter());
            settle(150);
            check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3 &&
                  std::fabs(view->camera().state().elevationDeg - 30.0) < 1e-3,
                  "clicking the label returns to the axonometric view");
            check(gizmo->labelText().contains(QStringLiteral("Persp")),
                  "the label reads Persp when not axis-aligned");
        }
    }

    // --- above-horizon clicks are rejected, not mirrored behind the eye -------
    {
        trigger(window, QStringLiteral("Front"));
        settle(400);   // elevation 0: half the viewport is above the horizon
        trigger(window, QStringLiteral("Start Sketch"));
        const int before = static_cast<int>(window.sketch().pointCount());
        // Top strip of the viewport is sky in the Front view.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.05));
        check(static_cast<int>(window.sketch().pointCount()) == before,
              "a click above the horizon adds no sketch point");
        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Axonometric"));
        settle(300);
    }

    // --- bundled font ---------------------------------------------------------
    check(!Theme::fontFamily().isEmpty(),
          QStringLiteral("the bundled font loaded (family: '%1')").arg(Theme::fontFamily()));
    check(QApplication::font().family() == Theme::fontFamily(),
          "the application font is the bundled family");
    check(Theme::fontFamily().contains(QStringLiteral("DM Sans")),
          QStringLiteral("the bundled family is DM Sans, not a fallback"));
    check(window.document().count() == 0, "document starts empty");
    check(!window.isSketching(), "not sketching at startup");

    // --- sketch -> face -> solid -------------------------------------------
    trigger(window, QStringLiteral("Start Sketch"));
    check(window.isSketching(), "Start Sketch enters sketch mode");

    sketchQuad(window, 0.35, 0.35, 0.62, 0.56);
    check(window.sketch().pointCount() == 4, "four clicks became four sketch points");

    trigger(window, QStringLiteral("Finish Sketch"));
    check(!window.isSketching(), "Finish Sketch leaves sketch mode");
    check(window.hasPendingFace(), "a face is waiting to be extruded");

    check(window.extrudePendingFace(10.0), "extrude reports success");
    check(window.document().count() == 1, "one solid in the document");

    // --- the app reports dimensions, not volume -------------------------------
    {
        const QString status = window.statusBar()->currentMessage();
        check(status.contains(QStringLiteral("Body 0")),
              QStringLiteral("the status line names the body (\"%1\")").arg(status));
        check(status.contains(QString::fromUtf8("\xC3\x97")),
              "the status line reports dimensions with a multiplication sign");
        check(!status.contains(QStringLiteral("volume")) &&
              !status.contains(QStringLiteral("mm3")),
              "the status line no longer mentions volume");
    }

    // --- items panel ----------------------------------------------------------
    check(window.itemsPanel() != nullptr, "the window has an items panel");
    check(window.itemsPanel()->rowCount() == 1, "panel shows one row for one solid");
    settle(300);

    const double volumeA = ModelingOps::volume(window.document().solids().front().shape);
    check(volumeA > 0.0, QStringLiteral("solid has positive volume (%1)").arg(volumeA, 0, 'f', 1));
    check(ModelingOps::countSolids(window.document().solids().front().shape) == 1,
          "the extrusion is a single solid");
    view->saveSnapshot(outDir + "/g1-solid.png");

    // --- picking -------------------------------------------------------------
    clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
    check(view->selectedSolidIds().size() == 1, "clicking the solid selects exactly one");

    // --- per-solid visibility -------------------------------------------------
    {
        const int id = window.document().solids().front().id;
        check(view->isSolidVisible(id), "a new solid starts visible");

        view->setSolidVisible(id, false);
        check(!view->isSolidVisible(id), "hiding reports hidden");
        check(view->selectedSolidIds().empty(), "hiding a solid drops it from the selection");

        view->setSolidVisible(id, true);
        check(view->isSolidVisible(id), "showing reports visible again");
        check(!view->isSolidVisible(9999), "an unknown id is not visible");

        // Showing does not restore the selection - production code must never
        // silently re-select something on the user's behalf. The delete/undo
        // checks below need a selection, so re-establish it the way a user
        // would, with a click.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
        check(view->selectedSolidIds().size() == 1, "the shown solid can be picked again");
    }

    {
        const int id = window.document().solids().front().id;
        view->setWireframe(false);
        view->setSolidVisible(id, false);
        view->setSolidVisible(id, true);
        settle(150);
        check(view->isSolidVisible(id), "a hidden-then-shown solid comes back visible");

        // Showing does not restore the selection (same rule as above); the
        // delete/undo checks below need one, so re-establish it with a click.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
    }

    // --- delete / undo / redo through the real actions -----------------------
    trigger(window, QStringLiteral("Delete Selected"));
    check(window.document().count() == 0, "Delete removes the solid");
    check(window.itemsPanel()->rowCount() == 0, "panel empties when the solid is deleted");

    trigger(window, QStringLiteral("Undo"));
    check(window.document().count() == 1, "Undo brings it back");

    trigger(window, QStringLiteral("Redo"));
    check(window.document().count() == 0, "Redo removes it again");

    trigger(window, QStringLiteral("Undo"));
    check(window.document().count() == 1, "Undo again, back to one solid");
    settle(200);

    // --- a second, taller solid ---------------------------------------------
    trigger(window, QStringLiteral("Start Sketch"));
    sketchQuad(window, 0.55, 0.30, 0.82, 0.50);
    trigger(window, QStringLiteral("Finish Sketch"));
    check(window.extrudePendingFace(40.0), "second extrude reports success");
    check(window.document().count() == 2, "two solids in the document");
    check(window.itemsPanel()->rowCount() == 2, "panel tracks the second solid");

    {
        const int firstId = window.document().solids().front().id;
        view->setSelectedSolids({firstId});
        settle(150);
        check(view->selectedSolidIds().size() == 1,
              "setSelectedSolids selects exactly the requested solid");
        check(view->selectedSolidIds().front() == firstId,
              "and it is the one that was asked for");

        // A hidden solid must never become selected behind the user's back.
        view->clearSelection();
        view->setSolidVisible(firstId, false);
        view->setSelectedSolids({firstId});
        settle(150);
        check(view->selectedSolidIds().empty(), "a hidden solid cannot be selected");
        view->setSolidVisible(firstId, true);
    }

    settle(300);

    const double volumeB = ModelingOps::volume(window.document().solids().back().shape);
    view->saveSnapshot(outDir + "/g2-two-solids.png");

    // --- select two and cut ---------------------------------------------------
    // Fractions re-tuned again for the perspective projection this task turns
    // on: switching from orthographic to perspective moves where the two
    // solids land on screen, and the previous 0.35/0.50 point sat right on the
    // first solid's edge (it worked under orthographic framing, not under
    // perspective). 0.45/0.55 lands solidly inside the first solid's silhouette.
    view->clearSelection();
    clickAt(view, QPointF(view->width() * 0.45, view->height() * 0.55));
    check(view->selectedSolidIds().size() == 1, "first solid picked");

    clickAt(view, QPointF(view->width() * 0.68, view->height() * 0.40), Qt::ShiftModifier);
    check(view->selectedSolidIds().size() == 2, "shift-click adds the second solid");

    if (view->selectedSolidIds().size() == 2) {
        check(window.applyBooleanToSelection(static_cast<int>(ModelingOps::BooleanKind::Cut)),
              "Cut reports success");
        check(window.document().count() == 1, "the two operands became one result");

        const double cutVolume = ModelingOps::volume(window.document().solids().front().shape);
        check(cutVolume > 0.0 && cutVolume < volumeA,
              QStringLiteral("cut removed material (%1 -> %2)")
                  .arg(volumeA, 0, 'f', 1).arg(cutVolume, 0, 'f', 1));
        settle(300);
        view->saveSnapshot(outDir + "/g3-after-cut.png");
    }

    // --- icons ----------------------------------------------------------------
    {
        const IconSet::Glyph all[] = {
            IconSet::Glyph::Sketch,      IconSet::Glyph::Extrude,
            IconSet::Glyph::Fuse,        IconSet::Glyph::Cut,
            IconSet::Glyph::Intersect,   IconSet::Glyph::Delete,
            IconSet::Glyph::Undo,        IconSet::Glyph::Redo,
            IconSet::Glyph::Items,       IconSet::Glyph::Snap,
            IconSet::Glyph::SelectSolid, IconSet::Glyph::SelectFace,
            IconSet::Glyph::DisplayMode, IconSet::Glyph::Screenshot,
            IconSet::Glyph::Fit,
        };
        bool allDrawn = true;
        for (IconSet::Glyph glyph : all) {
            const QPixmap pixmap = IconSet::icon(glyph).pixmap(16, 16);
            // A glyph that painted nothing yields a fully transparent pixmap.
            if (pixmap.isNull() || pixmap.toImage().isNull()) { allDrawn = false; break; }
            bool anyInk = false;
            const QImage image = pixmap.toImage();
            for (int y = 0; y < image.height() && !anyInk; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0) { anyInk = true; break; }
                }
            }
            if (!anyInk) { allDrawn = false; break; }
        }
        check(allDrawn, "every glyph paints something at 16x16");
    }

    // --- chips mirror their action -------------------------------------------
    {
        QAction probe(QStringLiteral("Probe"));
        probe.setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
        ToolChip chip(&probe, IconSet::Glyph::Sketch);

        probe.setEnabled(false);
        check(!chip.isEnabled(), "chip disables with its action");
        probe.setEnabled(true);
        check(chip.isEnabled(), "chip re-enables with its action");

        int fired = 0;
        QObject::connect(&probe, &QAction::triggered, [&fired] { ++fired; });
        chip.click();
        check(fired == 1, "clicking the chip triggers the action exactly once");

        probe.setCheckable(true);
        probe.setChecked(true);
        check(chip.isChecked(), "chip mirrors the checked state");

        // The action is the only thing that may change the checked state: a
        // click that does not reach the action must leave the chip alone.
        QObject::disconnect(&probe, nullptr, nullptr, nullptr);
        const bool before = chip.isChecked();
        chip.click();
        check(chip.isChecked() == before, "a chip never toggles its own checked state");
    }

    // --- overlay anchoring ----------------------------------------------------
    {
        QAction probe(QStringLiteral("Probe"));
        auto* cluster = new ToolCluster(view);
        cluster->addChip(new ToolChip(&probe, IconSet::Glyph::Fit));

        // A second, independent cluster that outlives the first - the real
        // post-condition for "relayout survives a destroyed cluster" is that
        // this one is still laid out correctly afterwards.
        QAction probe2(QStringLiteral("Probe2"));
        auto* survivor = new ToolCluster(view);
        survivor->addChip(new ToolChip(&probe2, IconSet::Glyph::Fit));

        ViewportOverlay overlay(view);
        overlay.addWidget(cluster, ViewportOverlay::Anchor::BottomLeft);
        overlay.addWidget(survivor, ViewportOverlay::Anchor::TopRight);
        overlay.relayout();

        const QRect bounds = view->rect();
        check(bounds.contains(cluster->geometry()),
              "an anchored cluster sits inside the viewport");
        const int bottomGap = bounds.bottom() - cluster->geometry().bottom();
        check(bottomGap >= 8 && bottomGap <= 32,
              QStringLiteral("bottom-anchored cluster keeps its margin (%1px)").arg(bottomGap));

        const int widthBefore = cluster->width();
        view->resize(view->width() + 120, view->height());
        settle(150);
        check(bounds.left() <= cluster->geometry().left() && cluster->width() == widthBefore,
              "cluster keeps its size and stays anchored after a resize");
        // A destroyed widget must not take the overlay down with it on the next
        // layout pass - QPointer entries go null and are skipped, and the
        // remaining, still-alive widget must still get laid out.
        delete cluster;
        overlay.relayout();
        check(view->rect().contains(survivor->geometry()),
              "the surviving cluster is still laid out after the destroyed one is skipped");
    }

    // --- view controls --------------------------------------------------------
    {
        QAction* wireframe = action(window, QStringLiteral("Wireframe"));
        check(wireframe != nullptr, "a Wireframe display-mode action exists");
        if (wireframe) {
            check(wireframe->isCheckable(), "Wireframe is a toggle");

            wireframe->trigger();
            settle(200);
            check(view->isWireframe(), "triggering Wireframe turns wireframe mode on");

            wireframe->trigger();
            settle(200);
            check(!view->isWireframe(), "triggering it again turns wireframe mode off");

            // Regression guard: displaySolid() used to hardcode AIS_Shaded, so
            // resyncView() - which Undo and Redo both run - silently reverted
            // every solid to shaded while the Wireframe action (and myWireframe
            // itself) stayed checked/true. isWireframe() alone cannot catch that
            // - it is untouched by resyncView() - so also check the solid's
            // actual live display mode via isSolidWireframe().
            wireframe->trigger();
            settle(200);
            check(view->isWireframe(), "wireframe is on going into undo/redo");
            const int solidId = window.document().solids().front().id;
            check(view->isSolidWireframe(solidId), "the solid itself renders wireframe before undo/redo");

            trigger(window, QStringLiteral("Undo"));
            trigger(window, QStringLiteral("Redo"));
            check(view->isWireframe(), "wireframe survives undo/redo (resyncView)");
            check(view->isSolidWireframe(solidId),
                  "the resynced solid still renders wireframe, not just the flag");

            // Leave the viewport shaded so later checks are unaffected.
            wireframe->trigger();
            settle(200);
            check(!view->isWireframe(), "wireframe turned back off, viewport left shaded");
        }
    }

    // --- standard views set turntable state -----------------------------------
    {
        trigger(window, QStringLiteral("Front"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg) < 1e-3,
              "Front is azimuth 0, elevation 0");

        trigger(window, QStringLiteral("Top"));
        settle(400);
        // setViewTop() requests 89 degrees, but CameraController's clamp caps
        // elevation at kMaxElevation = 88 (see CameraController.h and the
        // "setState clamps elevation" headless check) - 88 is what actually
        // lands, and it is still comfortably non-degenerate.
        check(std::fabs(view->camera().state().elevationDeg - 88.0) < 1e-3,
              "Top is elevation +88, clamped from the requested +89");

        trigger(window, QStringLiteral("Right"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-90.0)) < 1e-3,
              "Right is azimuth -90");

        trigger(window, QStringLiteral("Axonometric"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg - 30.0) < 1e-3,
              "Axonometric returns to the startup angles");
    }

    // --- animated transitions -------------------------------------------------
    {
        view->setAnimationsEnabled(true);
        CameraState goal = view->camera().state();
        goal.azimuthDeg += 90.0;
        const double azBefore = view->camera().state().azimuthDeg;
        view->animateTo(goal);
        // Mid-flight (a few event-loop turns in), the camera is between the
        // endpoints - that is what distinguishes animation from teleporting.
        settle(80);
        const double azMid = view->camera().state().azimuthDeg;
        check(std::fabs(azMid - azBefore) > 1.0 &&
              std::fabs(azMid - goal.azimuthDeg) > 1.0,
              "animateTo passes through intermediate states");
        settle(500);
        check(std::fabs(view->camera().state().azimuthDeg - goal.azimuthDeg) < 1e-3,
              "animateTo settles exactly on the goal");
        view->setAnimationsEnabled(false);
        check(!view->animationsEnabled(), "animations re-disabled for the rest of the suite");
    }

    // --- grid subdivision policy ----------------------------------------------
    {
        check(GridRenderer::minorStepFor(700.0) == 10.0,
              "default working distance uses the 10mm grid");
        check(GridRenderer::minorStepFor(50.0) == 1.0,
              "zoomed close in, the 1mm grid appears");
        check(GridRenderer::minorStepFor(8000.0) == 100.0,
              "zoomed far out, the 100mm grid takes over");
        check(GridRenderer::minorStepFor(0.0) >= 1.0 &&
              GridRenderer::minorStepFor(1e9) <= 100.0,
              "extreme distances stay inside the defined levels");
        check(GridRenderer::minorStepFor(119.9) == 1.0 &&
              GridRenderer::minorStepFor(120.0) == 10.0,
              "the 120mm threshold flips exactly once");
        check(GridRenderer::minorStepFor(2499.9) == 10.0 &&
              GridRenderer::minorStepFor(2500.0) == 100.0,
              "the 2500mm threshold flips exactly once");

        // Line positions must sit on the absolute grid regardless of band
        // extent parity - a band edge is not in general a line position.
        const double f1 = GridRenderer::firstLineAtOrBelow(2150.0, 100.0);
        check(std::fmod(f1, 100.0) == 0.0 && f1 <= -2150.0 && f1 > -2350.0,
              "band start snaps outward onto the absolute grid (odd parity)");
        const double f2 = GridRenderer::firstLineAtOrBelow(2100.0, 100.0);
        check(f2 == -2100.0,
              "an already-aligned band edge is its own first line");
    }

    // --- vocabulary is enforced, not merely documented ------------------------
    {
        // A documented vocabulary drifts the moment someone is in a hurry. An
        // asserted one cannot.
        const QStringList banned = {QStringLiteral("Fuse"), QStringLiteral("Solid"),
                                    QStringLiteral("OCCT"), QStringLiteral("mm3")};
        QStringList offenders;
        for (QAction* candidate : window.findChildren<QAction*>()) {
            const QString text = candidate->text().remove(QLatin1Char('&'));
            const QString tip = candidate->toolTip();
            for (const QString& word : banned) {
                if (text.contains(word, Qt::CaseInsensitive) ||
                    tip.contains(word, Qt::CaseInsensitive)) {
                    offenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(offenders.isEmpty(),
              QStringLiteral("no action uses a banned word (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("none")
                                           : offenders.join(QStringLiteral(", "))));

        QStringList tipOffenders;
        for (QWidget* widget : window.findChildren<QWidget*>()) {
            const QString tip = widget->toolTip();
            if (tip.isEmpty()) continue;
            for (const QString& word : banned) {
                if (tip.contains(word, Qt::CaseInsensitive))
                    tipOffenders << (tip.left(30) + QStringLiteral("…"));
            }
        }
        check(tipOffenders.isEmpty(),
              QStringLiteral("no widget tooltip uses a banned word (%1)")
                  .arg(tipOffenders.isEmpty() ? QStringLiteral("none")
                                              : tipOffenders.join(QStringLiteral(", "))));

        // The state label is the app's most-updated string; it must obey the
        // vocabulary too. It is a permanent widget on the status bar.
        QString stateText;
        for (QLabel* label : window.statusBar()->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) stateText = label->text();
        }
        check(!stateText.contains(QStringLiteral("solid"), Qt::CaseInsensitive),
              QStringLiteral("the state label says body, not solid (\"%1\")").arg(stateText));
        check(!stateText.contains(QStringLiteral("(s)")),
              "the state label writes plurals out rather than using (s)");

        check(action(window, QStringLiteral("Union")) != nullptr, "the Union action exists");
        check(action(window, QStringLiteral("Subtract")) != nullptr, "the Subtract action exists");
        check(action(window, QStringLiteral("Intersect")) != nullptr, "the Intersect action exists");
    }

    std::printf("\n%s (%d failure%s)  volumes: A=%.1f B=%.1f\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s", volumeA, volumeB);
    return g_failures == 0 ? 0 : 1;
}
