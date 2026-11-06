#include "EditorSelectorHandoff.h"
#include "IconSet.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "SceneWindow.h"
#include "SelectorWindow.h"
#include "Theme.h"
#ifdef FURNIFYME_WEB_UI
#include "web/HubWindow.h"
#include "web/WebHost.h"
#include "web/WebOverlay.h"
#include "web/RenderPage.h"
#endif

#include <memory>

#ifdef _WIN32
#include <cstdio>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

// A CRASH THAT SAYS WHERE. The app is a GUI-subsystem binary with no console
// and this machine has no debugger, so an access violation (one in the GL
// driver on a theme change, the first time) left nothing but a module name
// in the event log. The unhandled-exception filter runs before the stack
// unwinds, so the faulting frames are still there: symbolized through DbgHelp
// (RelWithDebInfo carries our PDBs) and written to %TEMP%\furnifyme-crash.txt.
// A file, not stderr, because this process has no stderr anyone can read.
static LONG WINAPI reportCrash(EXCEPTION_POINTERS* info)
{
    static bool reported = false;
    if (reported) return EXCEPTION_CONTINUE_SEARCH;
    reported = true;
    char path[MAX_PATH] = {};
    const DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH - 32) return EXCEPTION_CONTINUE_SEARCH;
    std::strcat(path, "furnifyme-crash.txt");
    FILE* out = std::fopen(path, "w");
    if (!out) return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(out, "exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    void* frames[62] = {};
    const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    char buffer[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;
    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 displacement = 0;
        char module[MAX_PATH] = "?";
        HMODULE handle = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &handle))
            GetModuleFileNameA(handle, module, MAX_PATH);
        const char* base = std::strrchr(module, '\\');
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                std::fprintf(out, "%02u %s!%s  %s:%lu\n", unsigned(i), base ? base + 1 : module, symbol->Name,
                             line.FileName, line.LineNumber);
            else
                std::fprintf(out, "%02u %s!%s\n", unsigned(i), base ? base + 1 : module, symbol->Name);
        } else {
            std::fprintf(out, "%02u %s+%p\n", unsigned(i), base ? base + 1 : module,
                         reinterpret_cast<void*>(address - reinterpret_cast<DWORD64>(handle)));
        }
    }
    std::fclose(out);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(reportCrash);
#endif
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
#ifdef FURNIFYME_WEB_UI
    WebHost::prepareApplication();
#endif

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
#ifdef FURNIFYME_WEB_UI
    // The editor's toasts and status bar as HTML (migration Phase 3a) unless
    // asked for the Qt ones - or unless the page is missing beside the app.
    if (!QCoreApplication::arguments().contains(QStringLiteral("--qt-chrome")) &&
        WebHost::pageAvailable(QStringLiteral("editor")))
        window.useHtmlChrome();
#endif
    // THE HUB: the HTML one (migration Phase 2) wherever the app is built with
    // the web UI, the Qt one otherwise - and `--qt-hub` asks for the Qt one
    // in a web build, for comparing the two side by side until Phase 6
    // deletes it. Both are wired by the one templated handoff below.
#ifdef FURNIFYME_WEB_UI
    // A deploy that lost the hub's page opens on the Qt hub rather than on a
    // blank first window.
    const bool pageMissing = !WebHost::pageAvailable(QStringLiteral("hub"));
    if (pageMissing) qWarning("FurnifyMe: web/hub is missing beside the app - using the Qt hub");
    const bool qtHub =
        pageMissing || QCoreApplication::arguments().contains(QStringLiteral("--qt-hub"));
#else
    const bool qtHub = true;
#endif
    std::unique_ptr<SelectorWindow> qtSelector;
#ifdef FURNIFYME_WEB_UI
    std::unique_ptr<HubWindow> htmlHub;
#endif
    if (qtHub) qtSelector = std::make_unique<SelectorWindow>(window.furnitureStore());
#ifdef FURNIFYME_WEB_UI
    else htmlHub = std::make_unique<HubWindow>(window.furnitureStore());
#endif
    // The third window, constructed here and never shown until a scene card
    // is chosen - the same contract the editor keeps above, and for the same
    // lazy-initializeViewer() reason.
    SceneWindow sceneWindow(&window.furnitureStore());
#ifdef FURNIFYME_WEB_UI
    // The scene window's render settings panel as the same page the editor's
    // is (migration Phase 3e), under the chrome's rule.
    if (!QCoreApplication::arguments().contains(QStringLiteral("--qt-chrome")) &&
        WebHost::pageAvailable(QStringLiteral("render")))
        RenderPage::attach(sceneWindow.studio(), &sceneWindow);
    // The scene window's chrome as the editor's page (migration Phase 5a),
    // under the same rule.
    if (!QCoreApplication::arguments().contains(QStringLiteral("--qt-chrome")) &&
        WebHost::pageAvailable(QStringLiteral("editor")))
        sceneWindow.useHtmlChrome();
#endif

    // The whole handoff - both directions, and the selector's own close -
    // is ONE shared implementation (EditorSelectorHandoff::wire()), also
    // used verbatim by gui_smoke's own test harness, so the suite drives
    // exactly what ships rather than a close cousin of it. See that
    // function's own comments for what each leg does and why the ORDER
    // (show the target, then hide the source) is load-bearing.
    if (qtSelector) EditorSelectorHandoff::wire(window, *qtSelector, &sceneWindow);
#ifdef FURNIFYME_WEB_UI
    if (htmlHub) EditorSelectorHandoff::wire(window, *htmlHub, &sceneWindow);
#endif

    // The shell's entrance, triggered HERE and nowhere else. The editor window
    // is shown by the handoff above, which gui_smoke drives verbatim - so an
    // entrance hung off the handoff, or off MainWindow::showEvent(), would
    // animate under the suite's startup-layout checks as well. This filter is
    // the real app's own trigger: it fires on the editor's first show, one
    // event-loop turn later so the show's own resize storm has settled and
    // ViewportOverlay has placed both cards, and then removes itself.
    class FirstShowEntrance : public QObject
    {
    public:
        explicit FirstShowEntrance(MainWindow* window)
            : QObject(window), myWindow(window)
        {
            window->installEventFilter(this);
        }

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (watched == myWindow && event->type() == QEvent::Show) {
                myWindow->removeEventFilter(this);
                QTimer::singleShot(0, myWindow, &MainWindow::playShellEntrance);
                deleteLater();
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        MainWindow* myWindow = nullptr;
    };
    new FirstShowEntrance(&window);

#ifdef FURNIFYME_WEB_UI
    // PHASE 0 SPIKE: `furnifyme --web-spike` lays the Svelte page (web/) over
    // the editor's viewport. Without the flag the app is exactly as shipped.
    if (QCoreApplication::arguments().contains(QStringLiteral("--web-spike"))) {
        auto* overlay = new WebOverlay(window.view(),
                                       WebOverlay::topOfFirstBody(&window.document()), &window);
        QObject::connect(&window, &MainWindow::documentChanged, overlay, &WebOverlay::pushAnchor);
        QObject::connect(overlay->bridge(), &SpikeBridge::commandReceived, overlay,
                         [overlay](const QString& id) {
                             emit overlay->bridge()->toastRequested(
                                 QStringLiteral("C++ received “%1” from the page").arg(id),
                                 QStringLiteral("note"));
                         });
        overlay->load(WebOverlay::defaultUrl());
    }
#endif

    if (qtSelector) qtSelector->show();
#ifdef FURNIFYME_WEB_UI
    if (htmlHub) htmlHub->show();
#endif

    return app.exec();
}
