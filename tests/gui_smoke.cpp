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
#include "ExtrudePreview.h"
#include "GridRenderer.h"
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "MainWindow.h"
#include "Measure.h"
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
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointF>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QString>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

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

// A key press delivered the way a real one arrives: to whatever currently
// holds focus inside `scope`, not to the widget the test hopes will handle
// it. Aiming a key at a specific widget is precisely the blind spot that let
// the extrude preview ship with Enter and Escape reachable only while its own
// field kept focus - which the first click anywhere in the viewport took
// away. QWidget::focusWidget() rather than QApplication::focusWidget():
// every window in this suite carries WA_ShowWithoutActivating and so is never
// the OS-active one, which leaves the application-wide focus widget null.
void sendKeyTo(QWidget* scope, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    if (!scope) return;
    QWidget* target = scope->focusWidget();
    if (!target) target = scope;
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(target, &press);
    settle(120);
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

            // The toast's Undo used to be wired straight to onUndo(), so it
            // was a fourth entry point obeying none of the guard the menu
            // entry, the chip and Ctrl+Z all share - delete a body, start a
            // sketch inside the four-second window, click Undo, and the
            // document resynced and the selection cleared while the user was
            // still placing points.
            if (!window.document().solids().empty()) {
                const int bodies = static_cast<int>(window.document().solids().size());
                view->setSelectedSolids({window.document().solids().front().id});
                settle(100);
                trigger(window, QStringLiteral("Delete Selected"));
                settle(150);
                check(toasts->isShowing() && toasts->undoControl() &&
                          toasts->undoControl()->isVisible(),
                      "a delete raises a toast whose Undo is offered");

                trigger(window, QStringLiteral("Start Sketch"));
                settle(150);
                QAction* undoAction = action(window, QStringLiteral("Undo"));
                check(undoAction != nullptr && !undoAction->isEnabled(),
                      "the Undo action is disabled mid-sketch");
                QWidget* pill = toasts->undoControl();
                check(pill != nullptr && !pill->isVisible(),
                      "and the toast's Undo control is unusable while it is");
                const int afterGuard = static_cast<int>(window.document().solids().size());
                if (pill) {
                    // Straight at the control, the most generous thing a user
                    // could manage: even reached, it must do nothing.
                    clickAt(pill, QPointF(pill->width() / 2.0, pill->height() / 2.0));
                    settle(150);
                }
                check(static_cast<int>(window.document().solids().size()) == afterGuard,
                      "clicking the toast's Undo mid-sketch undoes nothing");
                check(window.isSketching(),
                      "and leaves the sketch the user was placing points in alone");
                trigger(window, QStringLiteral("Cancel Sketch"));
                settle(120);
                trigger(window, QStringLiteral("Undo"));
                settle(150);
                check(static_cast<int>(window.document().solids().size()) == bodies,
                      "Undo by hand restores the body the guarded pill would not");

                // A toast that names one operation must not outlive it. Delete
                // a body - the toast says so and offers Undo - then press
                // Ctrl+Z by hand. The toast used to stay up, still armed, and
                // its pill then popped the checkpoint BEFORE the one it named:
                // the label described one change and the control performed
                // another. A fresh delete here, deliberately, so the toast
                // under test is one nothing has already dismissed.
                view->setSelectedSolids({window.document().solids().front().id});
                settle(100);
                trigger(window, QStringLiteral("Delete Selected"));
                settle(150);
                check(toasts->isShowing() && toasts->undoControl() &&
                          toasts->undoControl()->isVisible(),
                      "the fresh delete raises an armed toast");
                trigger(window, QStringLiteral("Undo"));
                settle(200);
                check(static_cast<int>(window.document().solids().size()) == bodies,
                      "the hand Undo restored that body too");
                check(!toasts->isShowing(),
                      "and the toast that offered to undo that same delete is gone, "
                      "rather than left describing one change while armed to "
                      "perform another");
            }
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

    // --- extrude asks for a height without stopping the user ------------------
    {
        // Draw an outline and close it, so a face is pending.
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);

        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr, "extrude opens a preview rather than a dialog");
        check(preview != nullptr && preview->isVisible(), "the preview is visible");
        check(window.findChildren<QDialog*>().isEmpty(),
              "extrude never constructs a dialog");
        check(preview != nullptr && preview->hasPreview(),
              "a preview shape is shown before the user commits anything");
        // Cross-checked against the viewport's own state, not just the
        // panel's bare flag - fix round 1, Minor 4: hasPreview() alone would
        // pass for a panel that sets the flag and displays nothing.
        check(view->hasPreview(),
              "the viewport itself actually holds the preview shape");
        check(static_cast<int>(window.document().solids().size()) == before,
              "previewing creates no body");

        if (preview && preview->field()) {
            preview->field()->setText(QStringLiteral("25"));
            settle(150);
            check(preview->hasPreview(), "editing the height keeps a live preview");
            check(view->hasPreview(), "the viewport reflects the edited height too");

            // Garbage must not clear the preview or flicker the viewport.
            preview->field()->setText(QStringLiteral("abc"));
            settle(150);
            check(preview->hasPreview(),
                  "an unparseable height leaves the last good preview alone");
            check(view->hasPreview(),
                  "the viewport still holds the last good preview, not nothing");
            check(static_cast<int>(window.document().solids().size()) == before,
                  "an unparseable height creates no body");

            preview->field()->setText(QStringLiteral("25"));
            settle(100);
            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &commit);
            settle(250);
        }

        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "Enter commits the extrude");
        ExtrudePreview* after = window.findChild<ExtrudePreview*>();
        check(after == nullptr || !after->isVisible(),
              "committing closes the preview");
        if (!window.document().solids().empty()) {
            const QString dims = QString::fromStdString(
                Measure::formatDimensions(window.document().solids().back().shape));
            // Fix round 1, Minor 4: dims.find("25") would also match a 250mm
            // width or depth component - "x <times> y <times> z mm" (see
            // Measure::formatDimensions), so check the height specifically,
            // the last of the three numbers, rather than anywhere in the
            // string.
            const QStringList parts = dims.split(QString::fromUtf8("\xC3\x97"));
            check(!parts.isEmpty() && parts.last().trimmed() == QStringLiteral("25 mm"),
                  QStringLiteral("the body's height specifically is 25 mm, not just "
                                 "some dimension that contains \"25\" (\"%1\")")
                      .arg(dims));
        }
    }

    // --- the preview owns Enter and Escape whatever holds focus ---------------
    // The whole point of a LIVE preview is that the user orbits to look at the
    // shape before committing to a height. That orbit is a press in the
    // viewport, which is Qt::StrongFocus - and every ToolChip became focusable
    // too - so focus left the panel's field, and Enter and Escape had been
    // wired only to a filter on that field. Nothing else consumed Escape
    // either (Cancel Sketch's binding is disabled while a preview can be
    // open), and the panel has no buttons: the user was stranded with a
    // preview shape and no route to commit or cancel it.
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible(),
              "a preview is open before the orbit");
        // The panel has to SAY what the two keys are - a modeless panel with
        // invisible verbs is how this went unnoticed for a whole branch.
        bool saysKeys = false;
        if (preview) {
            for (const QString& text : preview->paintedTexts()) {
                if (text.contains(QStringLiteral("Enter")) &&
                    text.contains(QStringLiteral("Esc")))
                    saysKeys = true;
            }
        }
        check(saysKeys, "the panel tells the user which keys commit and cancel");

        const int before = static_cast<int>(window.document().solids().size());

        // The orbit. clickAt()/dragButton() send events straight to a widget
        // and so never move focus the way a real press does (Qt does that in
        // QWidgetWindow, which synthetic delivery bypasses), so the focus
        // change is made explicitly - otherwise this probe would pass against
        // exactly the broken code it exists to catch.
        view->setFocus(Qt::MouseFocusReason);
        dragButton(view, QPointF(600, 400), QPointF(660, 430), Qt::RightButton);
        check(preview != nullptr && preview->field() != nullptr &&
                  window.focusWidget() != preview->field(),
              "orbiting takes focus off the height field, as a real press does");
        check(preview != nullptr && preview->isVisible(),
              "the preview survives the orbit");

        // Enter, delivered to the focus widget - the viewport, not the field.
        sendKeyTo(&window, Qt::Key_Return);
        settle(200);
        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "Enter still commits after an orbit moved focus off the field");
        ExtrudePreview* afterCommit = window.findChild<ExtrudePreview*>();
        check(afterCommit == nullptr || !afterCommit->isVisible(),
              "and the panel closes on that commit");

        // Same again for Escape.
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        const int beforeEscape = static_cast<int>(window.document().solids().size());
        view->setFocus(Qt::MouseFocusReason);
        dragButton(view, QPointF(600, 400), QPointF(650, 420), Qt::RightButton);
        sendKeyTo(&window, Qt::Key_Escape);
        settle(200);

        ExtrudePreview* afterEscape = window.findChild<ExtrudePreview*>();
        check(afterEscape == nullptr || !afterEscape->isVisible(),
              "Escape still cancels after an orbit moved focus off the field");
        check(static_cast<int>(window.document().solids().size()) == beforeEscape,
              "and that cancel created no body");
        check(window.hasPendingFace(),
              "the pending face survives a cancel from the viewport too");
        trigger(window, QStringLiteral("Start Sketch"));
        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(120);
    }

    // --- starting a new sketch closes an open extrude preview -----------------
    // Fix round 1, Important 1: onStartSketch() nulls the pending face and
    // resets the view's preview slot to empty, but that alone used to leave
    // the panel itself open and still believing it had a good preview - the
    // next keystroke redisplayed a body-shaped shape over the new outline,
    // and Enter reached commit(), where extrudePendingFace() silently failed
    // on the now-null pending face, so hide() never ran and the shape stayed
    // on screen: a body visible in the viewport that exists in no document.
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible() && preview->hasPreview() &&
                  view->hasPreview(),
              "a preview is open, with a shape in the viewport, before starting a new sketch");

        trigger(window, QStringLiteral("Start Sketch"));
        settle(150);
        ExtrudePreview* stillOpen = window.findChild<ExtrudePreview*>();
        check(stillOpen == nullptr || !stillOpen->isVisible(),
              "starting a new sketch closes the open extrude preview");
        check(!view->hasPreview(),
              "and leaves no ghost preview shape behind in the viewport");

        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(150);
    }

    // --- Escape cancels the extrude preview without touching the pending face -
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible() && preview->hasPreview(),
              "a preview is open before pressing Escape");
        check(window.hasPendingFace(), "a face is pending before pressing Escape");

        if (preview && preview->field()) {
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &escape);
            settle(150);
        }

        ExtrudePreview* afterEscape = window.findChild<ExtrudePreview*>();
        check(afterEscape == nullptr || !afterEscape->isVisible(),
              "Escape closes the preview");
        // This check used to assert the OPPOSITE - that the viewport was left
        // empty - which enshrined the bug rather than catching it.
        // MainWindow::onFinishSketch() shows the closed face through the
        // viewport's single preview slot; ExtrudePreview overwrites that same
        // slot with the body it would build. Clearing it on cancel therefore
        // erased the face, leaving an intact pending face, an enabled Extrude
        // action and a status bar still saying "Outline closed" above an
        // empty viewport - against Milestone 1's own criterion that closing an
        // outline produces a VISIBLE filled face.
        check(view->hasPreview(),
              "Escape puts the closed face back on screen rather than clearing it");
        // And it is the FACE, not the body the cancelled preview was showing:
        // hasPreview() alone cannot tell the two apart, which is how one
        // feature silently overwriting another's slot went unnoticed.
        const TopoDS_Shape restored = view->previewShape();
        check(!restored.IsNull() &&
                  !TopExp_Explorer(restored, TopAbs_SOLID).More() &&
                  TopExp_Explorer(restored, TopAbs_FACE).More(),
              "and what is on screen is the face, not the body it would have built");
        check(window.hasPendingFace(),
              "Escape leaves the pending face intact, so the user can retry");

        // Retry: the same face, extruded again, still works after a cancel.
        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);
        ExtrudePreview* retry = window.findChild<ExtrudePreview*>();
        if (retry && retry->field()) {
            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(retry->field(), &commit);
            settle(250);
        }
        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "the pending face left behind by Escape can still be extruded");
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

    // --- a fade cannot be double-clicked into a second undo -------------------
    // The suite otherwise runs with animations off, so ToastHost::dismiss()
    // is synchronous there and this exact bug is invisible to it - the toast
    // and its Undo pill vanish together in the same call that pops the undo
    // stack, leaving nothing for a second click to land on. With animations
    // on (the shipping default), a fade used to leave the pill visible and
    // clickable for the whole 160 ms, so an ordinary impatient double-click
    // undid two operations - one of them silently.
    {
        view->setAnimationsEnabled(true);

        check(!window.document().solids().empty(),
              "there is a body to delete for the double-click check");
        const int before = static_cast<int>(window.document().solids().size());
        view->setSelectedSolids({window.document().solids().front().id});
        settle(100);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(150);

        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr && toasts->isShowing(),
              "deleting a body raises a toast with animations enabled");
        QWidget* undo = toasts ? toasts->undoControl() : nullptr;
        check(undo != nullptr && undo->isVisible(), "the toast offers Undo");

        if (toasts && undo) {
            const QPoint centre = undo->mapTo(view, QPoint(undo->width() / 2, undo->height() / 2));
            check(view->childAt(centre) == undo,
                  "the Undo control is reachable before the first click");

            clickAt(undo, QPointF(undo->width() / 2.0, undo->height() / 2.0));
            // Deliberately not settled for the full fade duration - this is
            // the impatient double-click, landing mid fade-out.
            const int afterFirstClick = static_cast<int>(window.document().solids().size());
            check(afterFirstClick == before, "the first click's Undo restored the body");

            // dismiss() hides the Undo control at its own top, before the
            // fade even starts - so real hit-testing must already find
            // something other than the control here, exactly as it would
            // for a genuine second click a user fires off before the toast
            // has visibly finished fading.
            QWidget* hitDuringFade = view->childAt(centre);
            check(hitDuringFade != undo,
                  "the Undo control is not reachable by a real click during the fade");

            // The second click a real user's double-click would produce -
            // sent to whatever hit-testing actually finds there (nothing
            // claims this point once the control is hidden, so it falls
            // through to the viewport, same as clicking empty space).
            clickAt(view, QPointF(centre));
            settle(250);   // outlasts the 160 ms fade either way
            check(static_cast<int>(window.document().solids().size()) == afterFirstClick,
                  "a second click during the fade did not undo a second time");
        }

        // A resize mid-fade is the OTHER trigger for the same race:
        // ToastHost's own event filter re-derives the Undo control's
        // geometry - and, through Toast::syncUndoGeometry(), its visibility
        // - on every viewport resize for as long as the toast itself is
        // still isVisible(), which it is for the whole fade, not just until
        // dismiss() returns. A one-shot hide() at the top of dismiss() does
        // not survive that; folding "is this toast dismissing?" into the
        // same predicate syncUndoGeometry() already computes does.
        check(!window.document().solids().empty(),
              "there is a body to delete for the resize-mid-fade check");
        const int beforeResizeCheck = static_cast<int>(window.document().solids().size());
        view->setSelectedSolids({window.document().solids().front().id});
        settle(100);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(150);

        QWidget* undo2 = toasts ? toasts->undoControl() : nullptr;
        check(undo2 != nullptr && undo2->isVisible(),
              "the toast offers Undo again for the resize-mid-fade check");
        if (undo2) {
            clickAt(undo2, QPointF(undo2->width() / 2.0, undo2->height() / 2.0));
            // Deliberately not settled for the fade duration - the resize
            // below has to land while it is still running.
            check(static_cast<int>(window.document().solids().size()) == beforeResizeCheck,
                  "Undo restored the body before the resize");

            const QSize original = view->size();
            view->resize(original.width() + 40, original.height());
            settle(30);   // well inside the 160 ms fade

            const QPoint centreAfterResize =
                undo2->mapTo(view, QPoint(undo2->width() / 2, undo2->height() / 2));
            check(view->childAt(centreAfterResize) != undo2,
                  "a viewport resize mid-fade does not re-show the Undo control");

            view->resize(original);
            settle(250);   // outlasts the fade, and lets the resize settle back
        }

        view->setAnimationsEnabled(false);
        check(!view->animationsEnabled(), "animations re-disabled again after the double-click check");
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
        // or a tooltip, so it needs its own explicit sweep too. Unlike the
        // other three widgets it has no fixed set of strings to enumerate, so
        // it records every message it has been given this run and the sweep
        // covers all of them - it used to see only whichever one happened to
        // be live, which made the sweep a coin toss. HONEST LIMIT, stated on
        // Toast::paintedTexts() too: a message never actually triggered
        // during a run is not covered by this.
        Toast* sweptToast = window.findChild<Toast*>();
        check(sweptToast != nullptr && sweptToast->paintedTexts().size() > 2,
              QStringLiteral("the toast sweep covers every message shown this run, "
                             "not just the live one (%1 strings)")
                  .arg(sweptToast ? sweptToast->paintedTexts().size() : 0));
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

        // Same story for the extrude preview: its label is painted, not put
        // on an action or a tooltip, so it needs its own explicit sweep too.
        QStringList extrudePreviewOffenders;
        for (ExtrudePreview* preview : window.findChildren<ExtrudePreview*>()) {
            for (const QString& text : preview->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        extrudePreviewOffenders
                            << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(extrudePreviewOffenders.isEmpty(),
              QStringLiteral("no extrude preview text uses a banned word (%1)")
                  .arg(extrudePreviewOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : extrudePreviewOffenders.join(QStringLiteral(", "))));

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
                // Toast's UndoControl and the extrude field both carry
                // Qt::WA_NoMousePropagation and this control did not. It
                // accepts the press, which makes it the grab holder, so the
                // RELEASE lands here too - and QWidget's default release
                // handler ignores it, which propagates it to the parent. That
                // parent is the viewport, which performs a real pick on
                // release and unconditionally emits selectionChanged(), so
                // clicking "skip" on first run also selected whatever body sat
                // behind the guide. Counting that signal is the precise test:
                // an emission at all is the leak, whether or not this probe
                // has a body for the pick to land on.
                int picks = 0;
                QObject::connect(secondView, &OcctViewWidget::selectionChanged,
                                 secondView, [&picks] { ++picks; });

                const QPointF centre(hitSkip->width() / 2.0, hitSkip->height() / 2.0);
                QMouseEvent press(QEvent::MouseButtonPress, centre, centre,
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(hitSkip, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, centre, centre,
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(hitSkip, &release);
                settle(120);

                check(secondGuide->isFinished(),
                      "activating the skip control childAt() found finishes the guide");
                check(second.progress().hasLearned("walkthrough.done"),
                      "skipping records walkthrough.done, same as finishing for real");
                check(picks == 0,
                      QStringLiteral("clicking skip does not fall through to a pick in "
                                     "the viewport behind it (%1 selection changes)")
                          .arg(picks));
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

    // --- nothing in the bottom strip lands on top of anything else ------------
    // Three surfaces share the viewport's bottom edge - the guide bottom
    // right, the Snap/Select chip cluster bottom left, and the toast centred
    // between them - and all of them are z-ABOVE the toast after the next
    // relayout(). Two separate defects met here. The toast and the balloon
    // each repositioned from their own filter on the viewport's resize event,
    // which runs BEFORE ViewportOverlay has moved the guide (filters run
    // last-installed-first and the overlay installs its own first), so a
    // shrink placed them against the guide's pre-resize rectangle and the
    // guide then landed on top of them. And the toast only ever stepped
    // around the guide, never the chip cluster, so at 800x500 with the guide
    // up it was pushed left to x~86 and put a third of its message under
    // Snap/Select - reachable on a first run the moment a self-crossing
    // outline raises a failure message.
    {
        MainWindow narrow(nullptr, /*persistProgress=*/false);
        narrow.setAttribute(Qt::WA_ShowWithoutActivating);
        narrow.resize(900, 620);
        narrow.show();
        settle(300);
        narrow.view()->setAnimationsEnabled(false);

        OcctViewWidget* nv = narrow.view();
        // The defect is stated in VIEWPORT pixels, and the items panel eats a
        // couple of hundred of the window's own width, so drive the window
        // until the viewport itself is the size under test. Iterated because
        // the splitter re-proportions the panel as the window shrinks, so one
        // pass does not land it.
        auto resizeViewport = [&](int w, int h) {
            for (int i = 0; i < 5; ++i) {
                const int dw = w - nv->width();
                const int dh = h - nv->height();
                if (dw == 0 && dh == 0) break;
                narrow.resize(narrow.width() + dw, narrow.height() + dh);
                settle(250);
            }
        };
        WalkthroughPanel* guide = narrow.findChild<WalkthroughPanel*>();
        ToastHost* toasts = narrow.findChild<ToastHost*>();
        check(guide != nullptr && guide->isVisible() && toasts != nullptr,
              "the narrow probe starts with a guide up and a toast host");

        if (guide && toasts) {
            toasts->show(QStringLiteral("This outline can't close into a flat face"),
                         Toast::Kind::Failure, false);
            settle(150);
            Toast* toast = narrow.findChild<Toast*>();
            check(toast != nullptr && toast->isVisible(),
                  "a failure message is up alongside the guide");

            // The shrink, to the exact 800x500 viewport the defect names.
            resizeViewport(800, 500);
            check(nv->width() == 800 && nv->height() == 500,
                  QStringLiteral("the probe really is at the 800x500 viewport under "
                                 "test (got %1x%2)").arg(nv->width()).arg(nv->height()));

            if (toast) {
                check(!toast->geometry().intersects(guide->geometry()),
                      QStringLiteral("after a shrink the toast is clear of the guide "
                                     "(viewport %1 wide - toast %2,%3 %4x%5 - guide "
                                     "%6,%7 %8x%9)")
                          .arg(nv->width())
                          .arg(toast->x()).arg(toast->y())
                          .arg(toast->width()).arg(toast->height())
                          .arg(guide->x()).arg(guide->y())
                          .arg(guide->width()).arg(guide->height()));

                QStringList collisions;
                for (ToolCluster* cluster : nv->findChildren<ToolCluster*>()) {
                    if (cluster->isVisible() &&
                        cluster->geometry().intersects(toast->geometry()))
                        collisions << QStringLiteral("%1,%2 %3x%4")
                                          .arg(cluster->x()).arg(cluster->y())
                                          .arg(cluster->width()).arg(cluster->height());
                }
                check(collisions.isEmpty(),
                      QStringLiteral("and clear of every anchored widget too "
                                     "(viewport %1x%2, toast %3,%4 %5x%6; hits: %7)")
                          .arg(nv->width()).arg(nv->height())
                          .arg(toast->x()).arg(toast->y())
                          .arg(toast->width()).arg(toast->height())
                          .arg(collisions.isEmpty() ? QStringLiteral("none")
                                                    : collisions.join(QStringLiteral("; "))));
                check(nv->rect().contains(toast->geometry()),
                      "and still entirely inside the viewport");
            }

            // The other half: a guide that appears UNDERNEATH a toast already
            // up. HintBalloon::reconsider() handled that case for itself and
            // ToastHost did not, so Show tips again under a live toast left
            // the restored guide sitting on top of it.
            check(buildBody(narrow, 0.32, 0.32, 0.52, 0.52, 10.0),
                  "a body for the narrow probe, which completes its guide");
            settle(200);
            check(!guide->isVisible(), "the completed guide is out of the way");

            toasts->show(QStringLiteral("This outline can't close into a flat face"),
                         Toast::Kind::Failure, false);
            settle(150);
            Toast* liveToast = narrow.findChild<Toast*>();
            check(liveToast != nullptr && liveToast->isVisible(),
                  "a message is up with no guide beneath it");

            QAction* again = action(narrow, QStringLiteral("Show tips again"));
            check(again != nullptr, "the narrow probe has the reset action");
            if (again && liveToast) {
                again->trigger();
                settle(250);
                check(guide->isVisible(), "the guide comes back under the live toast");
                check(!liveToast->geometry().intersects(guide->geometry()),
                      QStringLiteral("and the toast steps aside for it rather than "
                                     "being buried (toast %1,%2 %3x%4 - guide "
                                     "%5,%6 %7x%8)")
                          .arg(liveToast->x()).arg(liveToast->y())
                          .arg(liveToast->width()).arg(liveToast->height())
                          .arg(guide->x()).arg(guide->y())
                          .arg(guide->width()).arg(guide->height()));
            }

            // ExtrudePreview has the mirror problem at the top edge: it is
            // raised once, at begin(), and relayout() then raises the
            // top-left Items/Undo/Redo cluster back over it, so its field
            // stopped being clickable on a narrow viewport. Overlap is not
            // itself the bug - being underneath it is.
            trigger(narrow, QStringLiteral("Start Sketch"));
            sketchQuad(narrow, 0.30, 0.30, 0.50, 0.50);
            trigger(narrow, QStringLiteral("Finish Sketch"));
            settle(150);
            trigger(narrow, QStringLiteral("Extrude..."));
            settle(200);
            // A resize AFTER the panel opened is what used to bury it.
            resizeViewport(500, 500);

            ExtrudePreview* panel = narrow.findChild<ExtrudePreview*>();
            check(panel != nullptr && panel->isVisible() && panel->field() != nullptr,
                  "the extrude panel is open on the narrow viewport");
            if (panel && panel->field()) {
                QWidget* field = panel->field();
                bool overlapped = false;
                for (ToolCluster* cluster : nv->findChildren<ToolCluster*>()) {
                    if (cluster->isVisible() &&
                        cluster->geometry().intersects(panel->geometry()))
                        overlapped = true;
                }
                check(overlapped,
                      QStringLiteral("the panel really does collide with a chip cluster "
                                     "at this width, so this check is exercising "
                                     "something (viewport %1 wide)").arg(nv->width()));
                const QPoint centre =
                    field->mapTo(nv, QPoint(field->width() / 2, field->height() / 2));
                QWidget* hit = nv->childAt(centre);
                check(hit == field,
                      QStringLiteral("and a real click still finds its height field, "
                                     "not a cluster raised over it (found %1)")
                          .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                                   : QStringLiteral("nothing")));
            }
            sendKeyTo(&narrow, Qt::Key_Escape);
            settle(150);
        }
        narrow.close();
    }

    // --- one type scale, and focus you can see --------------------------------
    {
        QSet<double> scale;
        for (const QFont& f : {Theme::titleFont(), Theme::bodyFont(),
                               Theme::labelFont(), Theme::badgeFont()}) {
            scale.insert(f.pointSizeF());
        }
        check(scale.size() == 4, "the type scale has four distinct sizes");

        QStringList offenders;
        for (QWidget* w : window.findChildren<QWidget*>()) {
            if (!w->isVisible()) continue;
            if (!scale.contains(w->font().pointSizeF()))
                offenders << (w->metaObject()->className() +
                              QStringLiteral(" @ %1").arg(w->font().pointSizeF()));
        }
        check(offenders.isEmpty(),
              QStringLiteral("every visible widget uses the type scale (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("all do")
                                           : offenders.join(QStringLiteral(", "))));

        // The sweep above skips anything not visible, and the toast, its Undo
        // control, the extrude panel and that panel's field are all hidden
        // whenever it runs - so they are structurally exempt from it however
        // their fonts drift. They inherit bodyFont() today; assert that
        // rather than leave it to chance.
        QStringList exempt;
        auto assertScale = [&](QWidget* w, const QString& name) {
            if (!w) { exempt << name + QStringLiteral(" (missing)"); return; }
            if (!scale.contains(w->font().pointSizeF()))
                exempt << name + QStringLiteral(" @ %1").arg(w->font().pointSizeF());
        };
        Toast* hiddenToast = window.findChild<Toast*>();
        ExtrudePreview* hiddenPreview = window.findChild<ExtrudePreview*>();
        assertScale(hiddenToast, QStringLiteral("Toast"));
        assertScale(hiddenToast ? hiddenToast->undoControl() : nullptr,
                    QStringLiteral("UndoControl"));
        assertScale(hiddenPreview, QStringLiteral("ExtrudePreview"));
        assertScale(hiddenPreview ? hiddenPreview->field() : nullptr,
                    QStringLiteral("ExtrudePreview field"));
        check(exempt.isEmpty(),
              QStringLiteral("the widgets hidden when that sweep runs use the type "
                             "scale too (%1)")
                  .arg(exempt.isEmpty() ? QStringLiteral("all do")
                                        : exempt.join(QStringLiteral(", "))));

        ToolChip* chip = window.findChild<ToolChip*>();
        check(chip != nullptr, "there is a chip to focus");
        if (chip) {
            const QImage unfocused = chip->grab().toImage();
            chip->setFocus(Qt::TabFocusReason);
            settle(80);
            const QImage focused = chip->grab().toImage();
            check(focused != unfocused, "keyboard focus is visible on a chip");

            // The check above renders whatever this harness's own window can
            // actually produce: WA_ShowWithoutActivating means window is
            // never the OS-active one (gui_smoke must never steal focus from
            // whatever else the user is doing), so window()->isActiveWindow()
            // is false throughout the whole suite and the ring painted above
            // is always the muted branch - see the comment at
            // ToolChip::paintEvent(). The active branch cannot be exercised
            // by rendering without genuinely activating a window, which this
            // suite must not do; checked at the token level instead, since
            // that is what determines whether the two branches would ever
            // look different on a window a real user is actually working in.
            check(Theme::focusRing() != Theme::focusRingMuted(),
                  "the active and muted focus-ring colours are visually distinct");
        }
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
        // The stale rect is wherever the constructor's own initial
        // syncSkipGeometry() call left the (hidden) skip control - it never
        // moved for a returning user, since the panel itself never showed.
        // Read from the control's own geometry rather than a hard-coded
        // point: sizeHint() now measures the panel's actual step strings
        // (see WalkthroughPanel::sizeHint()), so a fixed x/y here would go
        // stale the moment the wording or the type scale changed width.
        const QPoint stale = returningGuide && returningGuide->skipControl()
                                  ? returningGuide->skipControl()->geometry().center()
                                  : QPoint();
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
