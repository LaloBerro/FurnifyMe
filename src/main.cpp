#include "EditorSelectorHandoff.h"
#include "IconSet.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "SelectorWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
#ifndef _WIN32
    // Wayland does not hand out a native window handle OCCT can attach a V3d_View
    // to. Force XCB unless the user has deliberately chosen a platform plugin.
    // This must happen before QApplication is constructed.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // BEFORE QApplication, which is the only moment it can be set: the first
    // OpenGL context this process creates reads the application default
    // format, and the 3D viewport needs a depth and a stencil buffer that Qt's
    // own default does not promise. One derivation, in OcctViewWidget itself,
    // because the widget's driver options have to agree with the profile this
    // asks for - see OcctViewWidget::surfaceFormat().
    QSurfaceFormat::setDefaultFormat(OcctViewWidget::surfaceFormat());
    // Also before QApplication, and load-bearing rather than an optimisation.
    // Without it Qt DESTROYS a QOpenGLWidget's OpenGL context whenever the
    // widget is reparented (QOpenGLWidget::event, QEvent::WindowChangeInternal)
    // and builds a fresh one - and this application reparents its viewport for
    // real: opening the compare pane moves the live view into a QSplitter and
    // closing it moves it back. OCCT holds GPU resources against that context
    // and is given no chance to release them, so the re-attach on the next
    // frame tears down an OpenGl_Window whose context no longer exists, which
    // is a hard crash rather than a glitch (measured, at exactly that reparent).
    // With the attribute set Qt keeps the context across the reparent, and the
    // two viewports share GPU resources besides.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FurnifyMe"));
    QApplication::setOrganizationName(QStringLiteral("FurnifyMe"));

    // Milestone 4 fix round 1 (the CRITICAL quit-trap finding - see
    // EditorSelectorHandoff.h for the full story): belt one. Two unparented
    // top-level windows and a handoff that (by ordering mistake, now or
    // later) ever leaves both hidden for even one statement would otherwise
    // let Qt's default "quit when no window is visible" behaviour end the
    // whole application mid-handoff. Quitting is wired explicitly instead -
    // see EditorSelectorHandoff::wire()'s Hooks::quit, connected to the
    // selector's own close below - so this stays false regardless of
    // whatever ordering the handoff wiring does or does not get right.
    QApplication::setQuitOnLastWindowClosed(false);

    Theme::apply(app);
    // AFTER Theme::apply(): the mark is painted from panel(), border() and
    // accent(), so it has to be asked for once those exist. The application's
    // icon rather than only the window's, so anything else this app ever puts
    // on screen - a native file dialog, most of all - carries it too.
    QApplication::setWindowIcon(IconSet::appIcon());

    // Milestone 4: two top-level windows, not one, and deliberately
    // UNPARENTED - the real relationship EditorSelectorHandoff.h's own
    // review-round finding says a test harness must reproduce to testify
    // about this wiring at all (a Qt::Window-flagged widget with a QWidget
    // parent gets a transientParent(), which changes how Qt's
    // last-window-closed bookkeeping treats it). MainWindow is constructed
    // but never shown here - showInitScreen() already ran inside its own
    // constructor (see MainWindow.h/.cpp), which established the "nothing
    // open" data state; the window is only ever show()n as part of the
    // handoff below, never pre-shown or pre-realized by any other means
    // (CLAUDE.md's lazy-`initializeViewer()` pitfall - forcing the GL widget
    // to realize before it is genuinely needed broke startup determinism
    // once already).
    MainWindow window;
    SelectorWindow selector(window.furnitureStore());

    // The whole handoff - both directions, and the selector's own close -
    // is ONE shared implementation (EditorSelectorHandoff::wire()), also
    // used verbatim by gui_smoke's own test harness, so the suite drives
    // exactly what ships rather than a close cousin of it. See that
    // function's own comments for what each leg does and why the ORDER
    // (show the target, then hide the source) is load-bearing.
    EditorSelectorHandoff::wire(window, selector);

    selector.show();

    return app.exec();
}
