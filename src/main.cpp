#include "IconSet.h"
#include "MainWindow.h"
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

    MainWindow window;
    window.show();

    return app.exec();
}
