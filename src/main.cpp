#include "IconSet.h"
#include "MainWindow.h"
#include "SelectorWindow.h"
#include "Theme.h"

#include <QApplication>

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

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FurnifyMe"));
    QApplication::setOrganizationName(QStringLiteral("FurnifyMe"));

    Theme::apply(app);
    // AFTER Theme::apply(): the mark is painted from panel(), border() and
    // accent(), so it has to be asked for once those exist. The application's
    // icon rather than only the window's, so anything else this app ever puts
    // on screen - a native file dialog, most of all - carries it too.
    QApplication::setWindowIcon(IconSet::appIcon());

    // Milestone 4: two top-level windows, not one. MainWindow is constructed
    // but never shown here - showInitScreen() already ran inside its own
    // constructor (see MainWindow.h/.cpp), which established the "nothing
    // open" data state and hid the window; it is only ever show()n as part
    // of the handoff below, never pre-shown or pre-realized by any other
    // means (CLAUDE.md's lazy-`initializeViewer()` pitfall - forcing the GL
    // widget to realize before it is genuinely needed broke startup
    // determinism once already).
    MainWindow window;
    SelectorWindow selector(window.furnitureStore());

    // The whole handoff, owned here rather than by either window - neither
    // class knows the other exists. A card chosen (an existing one, or a
    // freshly created one - SelectorWindow::createRequested() already routes
    // through furnitureChosen() for the id it just made, see its header)
    // hides the selector, shows the editor - the ONE place this app ever
    // shows MainWindow - and only THEN calls openFurniture(), preserving the
    // invariant every other caller of that function already relied on: the
    // window is on screen, and its viewer has had its first real
    // showEvent()/paintEvent(), before anything asks it to display shapes.
    QObject::connect(&selector, &SelectorWindow::furnitureChosen, &window,
                     [&window, &selector](const QString& id) {
                         selector.hide();
                         window.show();
                         window.raise();
                         window.activateWindow();
                         window.openFurniture(id);
                     });
    // The other direction: the editor hands control back (Close furniture,
    // the native X, a failed openFurniture()) - refresh the selector's
    // gallery (a rename/delete/create may have happened since it was last
    // shown) and show it again.
    QObject::connect(&window, &MainWindow::returnedToSelector, &selector,
                     [&selector] {
                         selector.refresh();
                         selector.show();
                         selector.raise();
                         selector.activateWindow();
                     });

    selector.show();

    return app.exec();
}
