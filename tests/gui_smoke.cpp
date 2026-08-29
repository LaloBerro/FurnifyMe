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
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "SketchController.h"
#include "AxisGizmo.h"
#include "ShortcutSheet.h"
#include "Theme.h"
#include "Toast.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "UserProgress.h"
#include "ViewportOverlay.h"
#include "WalkthroughPanel.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPointF>
#include <QSettings>
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

// RAII for the one probe below that needs to seed QSettings before
// constructing a persistProgress=true MainWindow: saves the real
// organization/application name and QSettings::defaultFormat(), then
// switches to a dedicated, file-backed identity that cannot collide with
// whatever the developer's own use of the real app has recorded - and
// restores everything in the destructor, so the restore happens even if
// something between construction and the end of the scope were ever changed
// to throw or return early, rather than relying on sequential code reaching
// a restore line at the bottom.
//
// IniFormat plus a temp-directory path keeps the whole probe out of the
// registry entirely, rather than merely under a distinctly named key inside
// it - the file is left on disk afterward (temp directories are routinely
// cleared by the OS; a registry key is not), but it never touches the real
// app's actual settings location either way.
class ScopedTestSettings {
public:
    ScopedTestSettings()
        : myOrg(QCoreApplication::organizationName())
        , myApp(QCoreApplication::applicationName())
        , myFormat(QSettings::defaultFormat())
    {
        const QString path = QDir::tempPath() + QStringLiteral("/furnifyme-gui_smoke-settings");
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName(QStringLiteral("FurnifyMe-gui_smoke"));
        QCoreApplication::setApplicationName(QStringLiteral("returning-user-probe"));
    }

    ~ScopedTestSettings()
    {
        QSettings().clear();   // this probe's own entries, wherever they landed
        QSettings::setDefaultFormat(myFormat);
        QCoreApplication::setOrganizationName(myOrg);
        QCoreApplication::setApplicationName(myApp);
    }

    ScopedTestSettings(const ScopedTestSettings&) = delete;
    ScopedTestSettings& operator=(const ScopedTestSettings&) = delete;

private:
    QString myOrg;
    QString myApp;
    QSettings::Format myFormat;
};

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

// Sketch-quad-then-extrude, for probes that only care about ending up with a
// given number of bodies and would otherwise repeat this boilerplate inline.
// The one copy of the banned list. Two blocks sweep with it now - the
// vocabulary block and the shortcut sheet's own painted copy - and a second
// literal list would be a vocabulary that drifts from itself.
QStringList bannedWords()
{
    return {QStringLiteral("Fuse"),  QStringLiteral("Solid"),
            QStringLiteral("OCCT"),  QStringLiteral("mm3"),
            QStringLiteral("(s)"),   QStringLiteral("Merge"),
            QStringLiteral("Join")};
}

bool buildBody(MainWindow& window, double x0, double y0, double x1, double y1, double height)
{
    trigger(window, QStringLiteral("Start Sketch"));
    sketchQuad(window, x0, y0, x1, y1);
    trigger(window, QStringLiteral("Finish Sketch"));
    return window.extrudePendingFace(height);
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

    // Never persist: a suite whose behaviour depends on how often the developer
    // ran the real app is not a suite.
    MainWindow window(nullptr, /*persistProgress=*/false);
    // Show without taking focus: the point of this harness is that the user can
    // keep working while it runs.
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1200, 800);
    window.move(40, 40);
    window.show();
    settle(900);
    window.view()->setAnimationsEnabled(false);   // deterministic camera for the suite

    // --- the walkthrough appears for a newcomer -------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr, "a new user gets the guided first build");
        check(guide != nullptr && guide->isVisible(), "the guide is visible on first run");
        check(guide != nullptr && guide->completedSteps() == 0,
              "no steps are complete before the user does anything");
    }

    // --- the hint balloon has nothing to say before any body exists -----------
    // Visibility is asserted directly (isVisible()), not inferred from
    // currentHint() alone: a stub that sets myText without ever calling
    // show()/hide() would pass a text-only check just as well.
    {
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the window has a hint balloon");
        check(hint != nullptr && !hint->isVisible() && hint->currentHint().isEmpty(),
              "no hint is up before any body exists");
    }

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

    // --- outcomes are reported without stopping the user ----------------------
    {
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr, "the window has a toast host");

        // A modal would hang this suite rather than fail it, so assert the
        // absence of one directly: nothing in the app may create a dialog.
        check(window.findChildren<QDialog*>().isEmpty(),
              "no dialog is ever constructed for an outcome");

        if (toasts) {
            const int before = static_cast<int>(window.document().solids().size());
            check(before > 0, "there is a body to delete");
            view->setSelectedSolids({window.document().solids().front().id});
            settle(100);
            trigger(window, QStringLiteral("Delete Selected"));
            settle(150);

            check(toasts->isShowing(), "deleting a body raises a toast");
            check(toasts->toast() != nullptr && toasts->toast()->isVisible(),
                  "the toast is actually visible");
            check(!toasts->currentText().isEmpty(),
                  QStringLiteral("the toast says what happened (\"%1\")")
                      .arg(toasts->currentText()));

            // The Undo control must be reachable by a real click, not merely
            // present: a transparent overlay hides its whole subtree from
            // hit-testing, which is how Phase 2 shipped an unclickable control.
            QWidget* undo = toasts->undoControl();
            check(undo != nullptr && undo->isVisible(), "the toast offers Undo");
            if (undo) {
                const QPoint centre =
                    undo->mapTo(view, QPoint(undo->width() / 2, undo->height() / 2));
                check(view->childAt(centre) == undo,
                      "the Undo control is reachable by a real click");
                clickAt(undo, QPointF(undo->width() / 2.0, undo->height() / 2.0));
                settle(200);
                check(static_cast<int>(window.document().solids().size()) == before,
                      "using the toast's Undo restores the body");
                check(!toasts->isShowing(), "using Undo dismisses the toast");
            }

            // A second message replaces the first; a stack of toasts is a
            // dialog with extra steps.
            toasts->show(QStringLiteral("First"), Toast::Kind::Note, false);
            settle(50);
            toasts->show(QStringLiteral("Second"), Toast::Kind::Note, false);
            settle(50);
            check(window.findChildren<Toast*>().size() == 1,
                  "a second message replaces the first rather than stacking");
            check(toasts->currentText() == QStringLiteral("Second"),
                  "the newest message is the one showing");

            // The 4000/8000 ms split is the contract - a Failure carries a
            // sentence the user must read and act on, which is the whole
            // reason it outlives a Note. Asserted against the armed timer
            // rather than by actually waiting 4-8 real seconds for each one
            // to elapse, which would meaningfully slow this suite for a
            // property that a single read of the timer proves just as well.
            toasts->show(QStringLiteral("Note lifetime check"), Toast::Kind::Note, false);
            const int noteMs = toasts->remainingMs();
            check(noteMs > 3500 && noteMs <= 4000,
                  QStringLiteral("a Note toast is timed for 4000 ms (got %1)").arg(noteMs));

            toasts->show(QStringLiteral("Failure lifetime check"), Toast::Kind::Failure, false);
            const int failureMs = toasts->remainingMs();
            check(failureMs > 7500 && failureMs <= 8000,
                  QStringLiteral("a Failure outlives a Note - timed for 8000 ms (got %1)")
                      .arg(failureMs));
        }
    }

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

    // --- a hint appears the first time two bodies are selected ----------------
    {
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the window has a hint balloon");
        check(hint != nullptr && !hint->currentHint().isEmpty(),
              QStringLiteral("selecting two bodies raises a hint (\"%1\")")
                  .arg(hint ? hint->currentHint() : QString()));
        check(hint != nullptr && hint->currentHint().contains(QStringLiteral("Union")),
              "the hint names the operations now available");
    }

    // --- teach this window every hint, and re-select for the Cut below --------
    // The "a learned hint never appears again" assertion that used to sit
    // here was tautological: `window` had already shown the boolean hint a
    // few lines above, so myShownThisSession alone made isDue() false and the
    // check passed with the threshold logic deleted entirely. The real
    // version needs a window that has never shown the hint, which is the
    // dedicated probe immediately below. What survives here is what the rest
    // of the suite actually needs from this block: the two bodies selected
    // again for the Cut, and a `window` that has learned everything so no
    // stray hint appears over later checks.
    {
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            window.progress().record("boolean.completed");
            window.progress().record("faceMode.used");
            window.progress().record("view.changed");
        }
        view->clearSelection();
        settle(150);
        const int firstId = window.document().solids().front().id;
        const int secondId = window.document().solids().back().id;
        check(firstId != secondId, "the document holds two distinct bodies");
        view->setSelectedSolids({firstId, secondId});
        settle(200);
    }

    // --- a learned hint never appears again ------------------------------------
    {
        // A window that has never shown a hint, so the session flag cannot be
        // what silences one: every governing event is pushed past the
        // threshold BEFORE anything raises a balloon, and then the exact
        // conditions that raise all three are reproduced from scratch. Delete
        // the hasLearned() term from isDue() and this fails; that was not true
        // of the version this replaces.
        MainWindow learned(nullptr, /*persistProgress=*/false);
        learned.setAttribute(Qt::WA_ShowWithoutActivating);
        learned.resize(900, 600);
        learned.show();
        settle(300);
        learned.view()->setAnimationsEnabled(false);

        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            learned.progress().record("boolean.completed");
            learned.progress().record("faceMode.used");
            learned.progress().record("view.changed");
            learned.progress().record("walkthrough.done");
        }

        HintBalloon* learnedHint = learned.findChild<HintBalloon*>();
        check(learnedHint != nullptr, "the learned-user probe has a hint balloon");
        if (learnedHint) {
            check(buildBody(learned, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "first body for the learned-user probe");
            check(buildBody(learned, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "second body for the learned-user probe");
            check(learnedHint->currentHint().isEmpty() && !learnedHint->isVisible(),
                  "a user past every threshold is taught nothing by making bodies");

            const int idA = learned.document().solids().front().id;
            const int idB = learned.document().solids().back().id;
            check(idA != idB, "the learned-user probe holds two distinct bodies");
            learned.view()->setSelectedSolids({idA, idB});
            settle(200);
            check(learnedHint->currentHint().isEmpty() && !learnedHint->isVisible(),
                  "a user who has run three booleans is not told about them again");
        }
        learned.close();
    }

    // --- hint balloon: visibility, real hit-testing, all three dismissal ------
    // triggers, and preemption, pinned down in one tightly controlled scenario.
    //
    // A dedicated window rather than reusing `window` above: by this point in
    // the suite, `window`'s own delete/undo/redo traffic and status-bar
    // updates have already dismissed and consumed each hint's one showing per
    // session in ways that depend on exactly how earlier checks happen to be
    // ordered (deleting the only body, for instance, makes both the
    // face-selection and view hints' conditions go false well before this
    // point runs). Asserting anything precise against that would be asserting
    // an accident of ordering, not the balloon's actual contract - so this
    // scenario is built from scratch, deterministically, to pin the contract
    // down instead.
    {
        MainWindow probe(nullptr, /*persistProgress=*/false);
        probe.setAttribute(Qt::WA_ShowWithoutActivating);
        probe.resize(900, 600);
        probe.show();
        settle(300);
        OcctViewWidget* probeView = probe.view();
        probeView->setAnimationsEnabled(false);

        HintBalloon* hint = probe.findChild<HintBalloon*>();
        check(hint != nullptr, "the probe window has a hint balloon");
        check(hint != nullptr && !hint->isVisible() && hint->currentHint().isEmpty(),
              "no hint is up before any body exists");

        if (hint) {
            check(buildBody(probe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "first body for the hint-balloon probe");

            const QString firstHint = hint->currentHint();
            check(hint->isVisible() && !firstHint.isEmpty() &&
                  !firstHint.contains(QStringLiteral("Union")),
                  QStringLiteral("a lower-priority hint is up with one body (\"%1\")")
                      .arg(firstHint));

            // --- real hit-testing, not a synthetic event sent straight to a
            // widget we merely hope is reachable - see the Task 4 skip-control
            // regression this mirrors: childAt() is the actual mechanism a
            // real click uses, and this checks identity against it directly.
            const QPoint centre = hint->geometry().center();
            QWidget* hitBalloon = probeView->childAt(centre);
            check(hitBalloon == hint,
                  "childAt() at the balloon's centre finds the balloon itself, "
                  "the way a real click would");

            // --- trigger 1: "got it" genuinely dismisses it, not just myText --
            if (hitBalloon == hint) {
                clickAt(hint, QPointF(hint->width() / 2.0, hint->height() / 2.0));
            }
            check(hint->currentHint().isEmpty() && !hint->isVisible(),
                  "\"got it\" dismisses the balloon for real - hidden, not just "
                  "text-empty");

            // --- the session flag holds: building the second body re-derives
            // state with the dismissed hint's own condition still true (a body
            // exists, face selection still untried) - it must not come back.
            check(buildBody(probe, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "second body for the hint-balloon probe");
            const QString secondHint = hint->currentHint();
            check(!secondHint.contains(QStringLiteral("Select Faces")),
                  "the hint dismissed with \"got it\" does not return this session");
            check(hint->isVisible() && !secondHint.isEmpty() &&
                  secondHint.contains(QStringLiteral("gizmo")),
                  QStringLiteral("a second, different lower-priority hint is up "
                                 "instead (\"%1\")").arg(secondHint));

            // --- preemption: the boolean hint displaces one already on screen -
            const int idA = probe.document().solids().front().id;
            const int idB = probe.document().solids().back().id;
            check(idA != idB, "the probe window holds two distinct bodies");
            probeView->setSelectedSolids({idA, idB});
            settle(200);
            const QString thirdHint = hint->currentHint();
            check(thirdHint.contains(QStringLiteral("Union")) && thirdHint != secondHint,
                  "selecting two bodies preempts the hint that was already up");
            check(hint->isVisible(), "the boolean hint is genuinely visible");

            // --- trigger 2: the condition going away clears it, not just a
            // click and not just the learned threshold. Both other hints
            // already had their one showing this session (above), so nothing
            // else is due to take the freed slot - the balloon goes fully
            // quiet, not merely off-topic.
            probeView->setSelectedSolids({idA});
            settle(200);
            check(hint->currentHint().isEmpty(),
                  "dropping the selection to one body clears the boolean hint");
        }
    }

    // --- hint balloon: the two dismissal edges that do not run through -------
    // ordinary appStateChanged traffic. HintBalloon::reconsider() is driven
    // solely by MainWindow::appStateChanged, so a live predicate that flips
    // for a reason nothing already wired to that signal notices would
    // linger regardless of how correct conditionHolds() itself is -
    // MainWindow::onSelectionModeChanged() now calls updateActions()
    // explicitly, and OcctViewWidget::cameraChanged is now routed to
    // HintBalloon::onCameraChanged(). Each gets its own probe because both
    // the face-selection and the view hint can only show once per session,
    // and each hint's one showing in the probes above is already spent
    // proving a different trigger.
    {
        MainWindow modeProbe(nullptr, /*persistProgress=*/false);
        modeProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        modeProbe.resize(900, 600);
        modeProbe.show();
        settle(300);
        OcctViewWidget* modeProbeView = modeProbe.view();
        modeProbeView->setAnimationsEnabled(false);

        HintBalloon* hint = modeProbe.findChild<HintBalloon*>();
        check(hint != nullptr, "the mode-transition probe has a hint balloon");
        if (hint) {
            check(buildBody(modeProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the mode-transition probe");
            check(hint->isVisible() &&
                  hint->currentHint().contains(QStringLiteral("Select Faces")),
                  QStringLiteral("the face-selection hint is up before face mode "
                                 "is tried (\"%1\")").arg(hint->currentHint()));

            // Entering face selection mode - the action the hint is teaching -
            // must clear it on its own, with no click and no unrelated action
            // to fire appStateChanged first.
            trigger(modeProbe, QStringLiteral("Select Faces"));
            // Not necessarily empty: the view hint's own condition (a body
            // exists, no named view tried yet) is already satisfied and it
            // has not had its turn this session, so reconsider()'s cascade
            // correctly raises it the instant the slot is free - the same
            // "another due hint may legitimately take the freed slot"
            // behaviour as the condition-loss probe above. What matters here
            // is that the *face-selection* hint specifically is gone.
            check(!hint->currentHint().contains(QStringLiteral("Select Faces")),
                  QStringLiteral("switching to face selection clears its own "
                                 "hint directly (now: \"%1\")").arg(hint->currentHint()));
            check(hint->isVisible() && hint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint - already due - fills the freed slot immediately");

            // The face-selection hint already had its one showing this
            // session, so a second body changes nothing about which hint is
            // up; it is still the view hint from above.
            check(buildBody(modeProbe, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "a second body for the mode-transition probe");
            check(hint->isVisible() && hint->currentHint().contains(QStringLiteral("gizmo")),
                  QStringLiteral("the view hint is still up before a named view "
                                 "is tried (\"%1\")").arg(hint->currentHint()));

            // Snapping to a named view - the action the hint is teaching -
            // must clear it on its own, driven by cameraChanged rather than
            // by appStateChanged.
            trigger(modeProbe, QStringLiteral("Front"));
            check(hint->currentHint().isEmpty(),
                  "triggering a standard view clears its own hint directly");
        }
    }

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
        const QStringList banned = bannedWords();
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

        // The walkthrough panel's text - title, skip control, and steps - is
        // all painted, not put on any action text or tooltip, so none of the
        // loops above ever see any of it. paintedTexts() is the full set, not
        // just the steps, so nothing painted there is left unswept.
        QStringList walkthroughOffenders;
        for (WalkthroughPanel* panel : window.findChildren<WalkthroughPanel*>()) {
            for (const QString& text : panel->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        walkthroughOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(walkthroughOffenders.isEmpty(),
              QStringLiteral("no walkthrough panel text uses a banned word (%1)")
                  .arg(walkthroughOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : walkthroughOffenders.join(QStringLiteral(", "))));

        // Same story for the hint balloon: its copy is painted, not put on an
        // action or a tooltip, so it needs its own explicit sweep too.
        QStringList hintOffenders;
        for (HintBalloon* hint : window.findChildren<HintBalloon*>()) {
            for (const QString& text : hint->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        hintOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(hintOffenders.isEmpty(),
              QStringLiteral("no hint balloon text uses a banned word (%1)")
                  .arg(hintOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : hintOffenders.join(QStringLiteral(", "))));

        // Same story for the toast: its copy is painted, not put on an action
        // or a tooltip, so it needs its own explicit sweep too.
        QStringList toastOffenders;
        for (Toast* toastWidget : window.findChildren<Toast*>()) {
            for (const QString& text : toastWidget->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        toastOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(toastOffenders.isEmpty(),
              QStringLiteral("no toast text uses a banned word (%1)")
                  .arg(toastOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : toastOffenders.join(QStringLiteral(", "))));

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

    // --- progress is recorded from real actions -------------------------------
    {
        // The suite has by now completed sketches, extrudes and a boolean, so
        // those events must have been counted.
        check(window.progress().count("extrude.completed") >= 2,
              QStringLiteral("extrudes were recorded (%1)")
                  .arg(window.progress().count("extrude.completed")));
        check(window.progress().count("boolean.completed") >= 1,
              "the boolean was recorded");
        check(window.progress().count("sketch.completed") >= 2,
              "closing an outline was recorded");

        const int before = window.progress().count("undo.used");
        trigger(window, QStringLiteral("Undo"));
        settle(150);
        check(window.progress().count("undo.used") == before + 1,
              "undo records exactly once");
        trigger(window, QStringLiteral("Redo"));
        settle(150);
    }

    // --- the Help menu ---------------------------------------------------------
    {
        check(action(window, QStringLiteral("Keyboard Shortcuts")) != nullptr,
              "a Keyboard Shortcuts action exists");
        QAction* reset = action(window, QStringLiteral("Show tips again"));
        check(reset != nullptr, "a Show tips again action exists");
        if (reset) {
            reset->trigger();
            settle(100);
            check(window.progress().count("extrude.completed") == 0,
                  "Show tips again clears the progress store");

            // Acceptance criterion 5: Show tips again must genuinely restore
            // the walkthrough, not just clear the counters underneath it -
            // and it must not instantly re-complete itself just because a
            // body from before the reset is still sitting in the document.
            check(window.document().count() > 0,
                  "a body from before the reset is still in the document");
            WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
            check(guide != nullptr && guide->isVisible(),
                  "Show tips again brings the guide back");
            check(guide != nullptr && !guide->isFinished(),
                  "the restored guide is not finished");
            check(guide != nullptr && guide->completedSteps() == 0,
                  "the restored guide starts over at zero steps, not "
                  "re-completed by the body already in the document");

            // Prove the restore is real, not cosmetic: walking through the
            // guide again - building one more body - completes it again.
            trigger(window, QStringLiteral("Start Sketch"));
            sketchQuad(window, 0.35, 0.35, 0.45, 0.45);
            trigger(window, QStringLiteral("Finish Sketch"));
            check(window.extrudePendingFace(5.0), "a third extrude reports success");
            settle(150);
            check(guide != nullptr && guide->isFinished(),
                  "building another body completes the restored guide");
            check(window.progress().hasLearned("walkthrough.done"),
                  "the guide records walkthrough.done again after completing for real");
        }
    }

    // --- the view hint retires on its EVENT, from either route ---------------
    // Two defects met here. The spec records view.changed when "a standard
    // view or the gizmo changes the camera", but only the View menu ever
    // recorded it - so a user who only clicked the gizmo dismissed that hint
    // every session and never crossed the threshold. And the hint's own
    // retire condition read AxisGizmo::labelText() rather than the event, so
    // pressing 0 recorded view.changed while leaving the camera at a pose the
    // gizmo labels "Persp" - the balloon sat there after the user had done
    // exactly what it taught. One probe per route, since a hint only gets one
    // showing per session.
    {
        MainWindow axoProbe(nullptr, /*persistProgress=*/false);
        axoProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        axoProbe.resize(900, 600);
        axoProbe.show();
        settle(300);
        axoProbe.view()->setAnimationsEnabled(false);
        // Learn face selection so the view hint is the one the first body
        // raises, rather than queueing behind it.
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            axoProbe.progress().record("faceMode.used");
        }

        HintBalloon* axoHint = axoProbe.findChild<HintBalloon*>();
        AxisGizmo* axoGizmo = axoProbe.findChild<AxisGizmo*>();
        check(axoHint != nullptr && axoGizmo != nullptr,
              "the axonometric probe has a hint balloon and a gizmo");
        if (axoHint && axoGizmo) {
            check(buildBody(axoProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the axonometric probe");
            check(axoHint->isVisible() &&
                      axoHint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint is up before any named view is used");

            trigger(axoProbe, QStringLiteral("Axonometric"));
            settle(200);
            check(axoProbe.progress().count("view.changed") == 1,
                  "pressing Axonometric records view.changed exactly once");
            // The pose the Axonometric view leaves behind is precisely the one
            // the gizmo calls "Persp", which is why the old pose-based
            // condition could never retire this hint from this route.
            check(axoGizmo->labelText().contains(QStringLiteral("Persp")),
                  "and the camera it leaves is still one the gizmo labels Persp");
            check(!axoHint->isVisible() && axoHint->currentHint().isEmpty(),
                  "the hint retires all the same - it reads the recorded event, "
                  "not the camera pose");
        }
        axoProbe.close();
    }

    {
        MainWindow gizmoProbe(nullptr, /*persistProgress=*/false);
        gizmoProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        gizmoProbe.resize(900, 600);
        gizmoProbe.show();
        settle(300);
        gizmoProbe.view()->setAnimationsEnabled(false);
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            gizmoProbe.progress().record("faceMode.used");
        }

        HintBalloon* gizmoHint = gizmoProbe.findChild<HintBalloon*>();
        AxisGizmo* gizmo = gizmoProbe.findChild<AxisGizmo*>();
        check(gizmoHint != nullptr && gizmo != nullptr,
              "the gizmo probe has a hint balloon and a gizmo");
        if (gizmoHint && gizmo) {
            check(buildBody(gizmoProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the gizmo probe");
            check(gizmoHint->isVisible() &&
                      gizmoHint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint is up for the gizmo probe too");
            check(gizmoProbe.progress().count("view.changed") == 0,
                  "and nothing has recorded view.changed yet");

            clickAt(gizmo, gizmo->tipCenter(2, true));
            settle(250);
            check(gizmoProbe.progress().count("view.changed") >= 1,
                  QStringLiteral("clicking an arm of the gizmo records view.changed "
                                 "(count %1)")
                      .arg(gizmoProbe.progress().count("view.changed")));
            check(!gizmoHint->isVisible() && gizmoHint->currentHint().isEmpty(),
                  "so the gizmo retires the hint that teaches it - a user who "
                  "only ever uses the gizmo now crosses the threshold");
        }
        gizmoProbe.close();
    }

    // --- Show tips again restores the hints, not only the guide ---------------
    // Acceptance criterion 5 says "the walkthrough AND all three hints". The
    // two reset checks elsewhere in this suite assert only the panel, which
    // is why a reset that left every hint silenced until a restart survived
    // two review rounds: HintBalloon::myShownThisSession is session state
    // that clearing the store cannot reach on its own. A dedicated probe,
    // because this needs a hint genuinely dismissed earlier in the SAME
    // session, with nothing else having consumed the other hints' turns.
    {
        MainWindow resetProbe(nullptr, /*persistProgress=*/false);
        resetProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        resetProbe.resize(900, 600);
        resetProbe.show();
        settle(300);
        resetProbe.view()->setAnimationsEnabled(false);

        HintBalloon* resetHint = resetProbe.findChild<HintBalloon*>();
        WalkthroughPanel* resetGuide = resetProbe.findChild<WalkthroughPanel*>();
        check(resetHint != nullptr && resetGuide != nullptr,
              "the reset probe has both a hint balloon and a guide");
        if (resetHint && resetGuide) {
            check(buildBody(resetProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the reset probe");
            const QString dismissed = resetHint->currentHint();
            check(resetHint->isVisible() && !dismissed.isEmpty(),
                  QStringLiteral("a hint is up before the reset (\"%1\")").arg(dismissed));

            clickAt(resetHint, QPointF(resetHint->width() / 2.0,
                                       resetHint->height() / 2.0));
            check(!resetHint->isVisible() && resetHint->currentHint().isEmpty(),
                  "the hint is dismissed with \"got it\" before the reset");
            check(resetGuide->isFinished() && !resetGuide->isVisible(),
                  "and the guide has completed itself on that body");

            QAction* probeReset = action(resetProbe, QStringLiteral("Show tips again"));
            check(probeReset != nullptr, "the reset probe has the reset action");
            if (probeReset) {
                probeReset->trigger();
                settle(200);
                check(resetGuide->isVisible(), "Show tips again brings the guide back");
                // The point of the whole probe: the SAME hint, dismissed by
                // hand earlier this session, is available again. Without the
                // session set being cleared, a different hint - the one whose
                // turn was still unspent - comes back instead, and the
                // dismissed one stays gone until a restart.
                check(resetHint->isVisible() && resetHint->currentHint() == dismissed,
                      QStringLiteral("the hint dismissed earlier this session is "
                                     "available again after the reset (now: \"%1\")")
                          .arg(resetHint->currentHint()));

                // Both surfaces share this viewport, and below about 800 px of
                // viewport width the centred balloon lands on top of the
                // bottom-right guide. ViewportOverlay::relayout() raises the
                // guide over it, so an overlap would leave the guide covering
                // a balloon that is still the click target.
                check(!resetHint->geometry().intersects(resetGuide->geometry()),
                      QStringLiteral("the restored balloon and guide do not overlap "
                                     "(balloon %1,%2 %3x%4 - guide %5,%6 %7x%8)")
                          .arg(resetHint->x()).arg(resetHint->y())
                          .arg(resetHint->width()).arg(resetHint->height())
                          .arg(resetGuide->x()).arg(resetGuide->y())
                          .arg(resetGuide->width()).arg(resetGuide->height()));
                check(resetProbe.view()->childAt(resetHint->geometry().center()) == resetHint,
                      "and the balloon is still what a real click at its centre finds");
            }
        }
        resetProbe.close();
    }

    // --- the shortcut sheet lists every real binding --------------------------
    {
        QAction* open = action(window, QStringLiteral("Keyboard Shortcuts"));
        check(open != nullptr, "the shortcut sheet has an action to open it");
        if (open) {
            open->trigger();
            settle(150);
            ShortcutSheet* sheet = window.findChild<ShortcutSheet*>();
            check(sheet != nullptr && sheet->isVisible(), "triggering it shows the sheet");

            // Generated, not hand written: every action carrying a shortcut must
            // appear, so the sheet cannot go stale when a binding is added.
            int expected = 0;
            for (QAction* candidate : window.findChildren<QAction*>()) {
                if (!candidate->shortcut().isEmpty()) ++expected;
            }
            check(sheet != nullptr && sheet->rowCount() == expected,
                  QStringLiteral("the sheet lists all %1 bound actions (got %2)")
                      .arg(expected)
                      .arg(sheet ? sheet->rowCount() : -1));
            check(expected > 5, "there are enough bound actions for this to mean something");

            // Cancel Sketch is also bound to plain Escape with the default
            // WindowShortcut context, so in real dispatch QShortcutMap would
            // resolve it before a key press ever reaches the focused sheet.
            // ShortcutOverride is the mechanism Qt gives a widget to claim
            // the key back; sending it directly with sendEvent bypasses
            // QShortcutMap entirely, so this only proves the sheet uses that
            // mechanism correctly for Escape (and leaves everything else
            // alone) - it is not an end-to-end proof that the real keystroke
            // reaches the sheet instead of cancelling the sketch.
            QKeyEvent escapeOverride(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &escapeOverride);
            check(sheet != nullptr && escapeOverride.isAccepted(),
                  "the sheet claims Escape back from the shortcut map");

            QKeyEvent otherOverride(QEvent::ShortcutOverride, Qt::Key_E, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &otherOverride);
            check(sheet != nullptr && !otherOverride.isAccepted(),
                  "an unrelated key is left for the shortcut map, not grabbed");

            // Both bindings the design asks for, not just the first.
            check(open->shortcuts().contains(QKeySequence(Qt::Key_Question)) &&
                      open->shortcuts().contains(QKeySequence(Qt::Key_F1)),
                  "the sheet opens on ? and on F1");

            // Rows are grouped by the menu that owns them, and the headings
            // are read off the menu bar rather than written down twice.
            if (sheet) {
                const QStringList painted = sheet->paintedTexts();
                check(painted.contains(QStringLiteral("Sketch")) &&
                          painted.contains(QStringLiteral("View")) &&
                          painted.contains(QStringLiteral("Edit")),
                      "the sheet groups its rows under the menus that own them");
                check(!painted.contains(QStringLiteral("Other")),
                      "and every bound action is reachable from a menu, so the "
                      "catch-all group is empty");

                // Painted copy is invisible to the sweeps above, exactly like
                // the guide's and the balloon's, so it gets its own.
                QStringList sheetOffenders;
                for (const QString& text : painted) {
                    for (const QString& word : bannedWords()) {
                        if (text.contains(word, Qt::CaseInsensitive))
                            sheetOffenders << (text + QStringLiteral(" [") + word +
                                               QStringLiteral("]"));
                    }
                }
                check(sheetOffenders.isEmpty(),
                      QStringLiteral("no shortcut sheet text uses a banned word (%1)")
                          .arg(sheetOffenders.isEmpty()
                                   ? QStringLiteral("none")
                                   : sheetOffenders.join(QStringLiteral(", "))));

                // Re-centres on a window resize; it used to stay wherever the
                // window happened to be when it opened. HintBalloon solved
                // this with a filter on its parent and this now does the same.
                window.resize(1100, 760);
                settle(250);
                check(sheet->x() == (window.width() - sheet->width()) / 2 &&
                          sheet->y() == (window.height() - sheet->height()) / 2,
                      QStringLiteral("the sheet re-centres when the window resizes "
                                     "(at %1,%2 in a %3x%4 window)")
                          .arg(sheet->x()).arg(sheet->y())
                          .arg(window.width()).arg(window.height()));
                window.resize(1200, 800);
                settle(250);

                // A click OUTSIDE dismisses it - and must not also fall
                // through and pick in the viewport behind an
                // apparently-modal sheet, which is what it used to do while
                // a click INSIDE was what closed it. clickAt() delivers press
                // and release straight to the viewport, so if either reached
                // it the selection below would be cleared by the pick.
                const int keptId = window.document().solids().front().id;
                view->setSelectedSolids({keptId});
                settle(150);
                check(view->selectedSolidIds().size() == 1,
                      "one body is selected before the click-outside check");
                const QPoint outside(5, view->height() - 5);
                check(sheet->rect().contains(
                          sheet->mapFromGlobal(view->mapToGlobal(outside))) == false,
                      "the click-outside point really is outside the sheet");
                clickAt(view, QPointF(outside));
                settle(200);
                check(!sheet->isVisible(), "a click outside closes the sheet");
                check(view->selectedSolidIds().size() == 1,
                      "and does not fall through to pick in the viewport behind it");

                // Re-open for the Escape check below.
                open->trigger();
                settle(150);
                check(sheet->isVisible(), "the sheet re-opens");

                // A click INSIDE is not a dismissal: you have to hold still to
                // read a list, and clicking one used to close it.
                clickAt(sheet, QPointF(sheet->width() / 2.0, sheet->height() - 8.0));
                settle(150);
                check(sheet->isVisible(), "a click inside leaves the sheet open");
            }

            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &escape);
            settle(150);
            check(sheet != nullptr && !sheet->isVisible(), "Escape closes the sheet");
        }
    }

    // --- the walkthrough completes and stays gone ------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr && guide->isFinished(),
              "building a body completes the guide");
        check(guide != nullptr && !guide->isVisible(),
              "a finished guide hides itself");
        check(window.progress().hasLearned("walkthrough.done"),
              "finishing records walkthrough.done");

        // A returning user does not see it again.
        MainWindow second(nullptr, /*persistProgress=*/false);
        // Shown (and settled) before anything below reads a widget's
        // position: ViewportOverlay lays overlay widgets out against the
        // viewport's size at the time of each addWidget() call, which
        // happens during MainWindow's constructor - before the constructor's
        // own resize(1280, 800) call near its end, let alone this resize()
        // and show(). The corrected layout for the real size only lands once
        // the resulting resize event is actually processed, which needs a
        // pump of the event loop.
        second.setAttribute(Qt::WA_ShowWithoutActivating);
        second.resize(900, 600);
        second.show();
        settle(300);

        // An earlier version of this check only asserted
        // Qt::WA_TransparentForMouseEvents on the panel and nothing more.
        // That missed that the attribute excludes a widget's ENTIRE SUBTREE
        // from hit-testing, not just the widget carrying it (verified
        // directly against this machine's Qt 6.11.1:
        // QWidgetPrivate::childAtRecursiveHelper `continue`s straight past a
        // transparent widget without descending into its children) - so the
        // skip control, then a child of the panel, was just as unreachable
        // by a real click as the panel's own painted "skip" text was meant
        // to be, even though it answered fine to an event sent straight to
        // it in a test. That is why the skip control is now a sibling of
        // the panel instead (see WalkthroughPanel.cpp), positioned over
        // skipRect() and raised above it, and why this check goes through
        // childAt() - the actual mechanism real hit-testing uses - rather
        // than an attribute flag. gui_smoke's clickAt() sends events
        // straight to a target widget, bypassing childAt() entirely, so it
        // could not have caught this either.
        WalkthroughPanel* secondGuide = second.findChild<WalkthroughPanel*>();
        check(secondGuide != nullptr, "the second window gets its own guide too");
        if (secondGuide) {
            OcctViewWidget* secondView = second.view();

            // skipRect()'s formula, in the panel's own local coordinates:
            // width() - 14 - 34, 8, 34, 18 (see WalkthroughPanel.cpp). Its
            // centre, translated into the shared parent's coordinates the
            // way syncSkipGeometry() does, is what a real click on it would
            // land on.
            const QPoint skipCentre =
                secondGuide->pos() + QPoint(secondGuide->width() - 31, 17);
            QWidget* hitSkip = secondView->childAt(skipCentre);
            // "not the panel" alone is the check that let an AxisGizmo
            // mis-hit through once already (see the fix-round report) - it
            // asserts what the bug happened not to violate, not what this
            // is actually supposed to prove. skipControl() gives the real
            // identity to compare against.
            check(hitSkip != nullptr && hitSkip == secondGuide->skipControl(),
                  "childAt() at the skip control's centre finds the skip "
                  "control itself, not some other widget");

            const QPoint insidePanelOutsideSkip =
                secondGuide->pos() + QPoint(20, secondGuide->height() - 20);
            QWidget* hitElsewhere = secondView->childAt(insidePanelOutsideSkip);
            check(hitElsewhere == nullptr,
                  "childAt() at a point inside the panel but well outside skip "
                  "finds neither the panel nor the skip control");

            if (hitSkip && hitSkip == secondGuide->skipControl()) {
                const QPointF centre(hitSkip->width() / 2.0, hitSkip->height() / 2.0);
                QMouseEvent press(QEvent::MouseButtonPress, centre, centre,
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(hitSkip, &press);
                check(secondGuide->isFinished(),
                      "activating the skip control childAt() found finishes the guide");
                check(second.progress().hasLearned("walkthrough.done"),
                      "skipping records walkthrough.done, same as finishing for real");
            }
        }

        // A check used to sit here re-recording walkthrough.done and
        // asserting the panel stayed hidden - but by this point the skip
        // click above has already finished and hidden it, so that assertion
        // passed regardless of whether the returning-user gate actually
        // works. The genuine test of that gate - a window whose progress
        // already says learned BEFORE its panel is ever built, which is the
        // state persistProgress=false can never produce here - lives in the
        // "Show tips again restores the walkthrough for a returning user
        // too" block below instead.
        second.close();
    }

    // --- Show tips again restores the walkthrough for a returning user too ---
    {
        // Every walkthrough check above uses persistProgress=false, so
        // hasLearned() is always false at the moment buildOverlay() runs -
        // none of them can exercise the actual returning-user path, where
        // MainWindow's constructor deserializes progress from QSettings
        // BEFORE buildOverlay() ever runs. That is the path Show tips again
        // exists for: someone who quit, came back, and wants the guide
        // again. ScopedTestSettings switches to a dedicated, file-backed
        // QSettings identity for this block and guarantees the restore on
        // the way out, however the scope ends.
        ScopedTestSettings scopedSettings;

        UserProgress seed;
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) seed.record("walkthrough.done");
        {
            QSettings seedSettings;
            seedSettings.setValue(QStringLiteral("progress"),
                                  QString::fromStdString(seed.serialize()));
        }

        MainWindow returning(nullptr, /*persistProgress=*/true);
        WalkthroughPanel* returningGuide = returning.findChild<WalkthroughPanel*>();
        check(returningGuide != nullptr,
              "a returning user still gets a panel built, just hidden");
        check(returningGuide != nullptr && returningGuide->isFinished(),
              "and it already knows it is finished before ever being shown");

        returning.setAttribute(Qt::WA_ShowWithoutActivating);
        returning.resize(900, 600);
        returning.show();
        settle(300);
        check(returningGuide != nullptr && !returningGuide->isVisible(),
              "the panel stays hidden even once the window is shown - progress "
              "was already learned before it was built");

        // The panel being hidden is not the whole story. Its skip control is
        // a SIBLING, kept in step through the panel's show/hide/move events -
        // and on this exact path refresh() calls hide() on a panel that was
        // never shown, for which Qt delivers no QHideEvent at all. Without an
        // explicit hidden state the control was then revealed by
        // showChildren() when the window appeared, at its stale constructor
        // geometry near the TOP-LEFT corner, where it silently ate picks for
        // every returning user. Both halves are checked: that it is genuinely
        // not visible, and - the part an isVisible() check alone would miss
        // if the geometry were ever wrong instead - that real hit-testing
        // finds nothing at that stale rectangle.
        OcctViewWidget* returningView = returning.view();
        check(returningGuide != nullptr && returningGuide->skipControl() != nullptr &&
                  !returningGuide->skipControl()->isVisible(),
              "the skip control is hidden too, not just the panel it belongs to");
        // QRect(width() - kPad - 34, 8, 34, 18) with pos() still (0, 0):
        // (212, 8, 34, 18) for a 260-wide panel, so (229, 17) is its centre.
        const QPoint stale(229, 17);
        QWidget* hitStale = returningView->childAt(stale);
        check(hitStale == nullptr,
              QStringLiteral("nothing lurks at the skip control's stale "
                             "constructor rectangle (found %1)")
                  .arg(hitStale ? QString::fromLatin1(hitStale->metaObject()->className())
                                : QStringLiteral("nothing")));

        QAction* returningReset = action(returning, QStringLiteral("Show tips again"));
        check(returningReset != nullptr, "the returning user's window has the reset action too");
        if (returningReset) {
            returningReset->trigger();
            settle(150);
            check(returningGuide != nullptr && returningGuide->isVisible(),
                  "Show tips again restores the panel for a returning user, "
                  "not just a same-session one");
            check(returningGuide != nullptr && !returningGuide->isFinished(),
                  "the restored panel starts fresh rather than staying finished");
            // And the skip control comes back with it, at the panel's real
            // position rather than the constructor-time one.
            check(returningGuide != nullptr && returningGuide->skipControl() != nullptr &&
                      returningGuide->skipControl()->isVisible(),
                  "the restored panel's skip control is visible again");
            if (returningGuide && returningGuide->skipControl()) {
                const QPoint skipCentre =
                    returningGuide->pos() + QPoint(returningGuide->width() - 31, 17);
                check(returningView->childAt(skipCentre) == returningGuide->skipControl(),
                      "and a real click at its centre finds the skip control itself");
            }
        }
        returning.close();
        // scopedSettings restores the real QSettings identity as it goes
        // out of scope here.
    }

    std::printf("\n%s (%d failure%s)  volumes: A=%.1f B=%.1f\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s", volumeA, volumeB);
    return g_failures == 0 ? 0 : 1;
}
