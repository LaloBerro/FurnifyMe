// The editor's toasts and status bar as HTML (migration Phase 3a), in the
// real editor: MainWindow in HTML-chrome mode, a seeded furniture, and the
// page driven from inside - DOM events through runJavaScript, Qt actions
// through trigger(), never OS input.
//
//   chrome_smoke.exe [output-dir]

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstring>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

#include "DocumentModel.h"
#include "FurnitureStore.h"
#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "ViewportOverlay.h"
#include "session/VersionsModel.h"
#include "session/JointsModel.h"
#include "session/ItemsModel.h"
#include "ui/GestureChip.h"
#include "ui/SettingsEdits.h"
#include "ui/Themes.h"
#include "session/ToastHost.h"
#include "web/EditorChrome.h"
#include "web/RenderPage.h"
#include "ui/RenderStudio.h"
#include "ui/SceneWindow.h"
#include "ui/LoadingCard.h"
#include "ui/ShortcutSheet.h"
#include "session/StatusLine.h"
#include "web/HubWindow.h"
#include "ui/WindowChrome.h"
#include "ui/ShapeFlyout.h"
#include "ui/MaterialCard.h"
#include "ui/HintBalloon.h"
#include "ui/WalkthroughPanel.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include "ui/ScenePiecesPanel.h"
#include "ui/AddPieceCard.h"
#include "ui/UnsavedCloseCard.h"
#include "ui/NameFurnitureCard.h"
#include "ui/WindowChrome.h"
#include "ui/AxisGizmo.h"
#include "ui/ToolChip.h"
#include "session/ActionRegistry.h"
#include "ui/RenderSettingsPanel.h"
#include "web/WebHost.h"
#include "web/ShellBridge.h"
#include "session/MenuModel.h"
#include "ui/HtmlCardSlot.h"
#include "ui/BooleanTool.h"
#include "ui/SlatsTool.h"
#include "ui/ChipFace.h"
#include "ui/ExtrudePreview.h"
#include "ui/PullArrow.h"
#include "ui/MitreTool.h"
#include "ui/ReMeasureTool.h"
#include "ui/JointChip.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColorDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QTest>

#include <TopExp_Explorer.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <cstdio>
#include <functional>

namespace {

int gFailures = 0, gChecks = 0;

void check(bool ok, const QString& what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

bool waitFor(const std::function<bool()>& done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// True once `done` has HELD for `stableMs` (T-084). A card that sizes itself
// settles through a chain - the page reports a height, the slot takes it, the
// overlay re-stacks, the page is told the new rectangle - and a wait that
// returned at the first instant the three agreed, followed by a second read
// for the check, could catch the next link of the chain in between. The check
// takes THIS answer; it never reads the state a second time.
bool waitStable(const std::function<bool()>& done, int ms, int stableMs = 250)
{
    QElapsedTimer t, held;
    t.start();
    bool holding = false;
    while (t.elapsed() <= ms) {
        if (done()) {
            if (!holding) {
                holding = true;
                held.start();
            }
            if (held.elapsed() >= stableMs) return true;
        } else {
            holding = false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return false;
}

QVariant js(QWebEngineView* view, const QString& script)
{
    QVariant out;
    bool done = false;
    view->page()->runJavaScript(script, [&](const QVariant& v) {
        out = v;
        done = true;
    });
    waitFor([&] { return done; }, 5000);
    return out;
}

QString jsString(QWebEngineView* view, const QString& script) { return js(view, script).toString(); }

QString toastText(QWebEngineView* page)
{
    return jsString(page, "(() => { const t = document.querySelector('.toast .text'); "
                          "return t ? t.textContent : ''; })()");
}

QRect toastRect(QWebEngineView* page)
{
    const QJsonObject o = QJsonDocument::fromJson(
        jsString(page, "JSON.stringify((() => { const t = document.querySelector('.toast'); if (!t) "
                       "return {}; const r = t.getBoundingClientRect(); return {x: r.left, y: r.top, "
                       "w: r.width, h: r.height}; })())").toUtf8()).object();
    // Rounded doubles, never toInt(): QJsonValue::toInt() answers 0 for a
    // non-integral number, and at 150% a toast is 51.33 px tall.
    auto n = [&](const char* key) { return qRound(o.value(QLatin1String(key)).toDouble()); };
    return QRect(n("x"), n("y"), n("w"), n("h"));
}

// The window buttons' place (the page draws them; Phase 7b deleted the Qt
// card) - and whether a press at their centre reaches buttons a user can press.
QWidget* shownWindowButtons(MainWindow& window)
{
    auto* slot = window.buttonsSlot();
    return slot && slot->isVisible() ? slot : nullptr;
}

bool windowButtonsTakePress(MainWindow& window, QWebEngineView* page)
{
    QWidget* buttons = shownWindowButtons(window);
    if (!buttons) return false;
    const QPoint centre = buttons->mapTo(window.view(), buttons->rect().center());
    QWidget* at = window.view()->childAt(centre);
    return at && (at == page || page->isAncestorOf(at)) &&
           js(page, QStringLiteral("!!document.elementFromPoint(%1, %2)?.closest('.window-buttons')").arg(centre.x()).arg(centre.y())).toBool();
}

QAction* actionNamed(QWidget* window, const QString& text)
{
    for (QAction* a : window->findChildren<QAction*>())
        if (QString(a->text()).remove(QLatin1Char('&')) == text) return a;
    return nullptr;
}

#ifdef _WIN32
// The window as the screen composites it (PrintWindow, client area, device
// pixels): the page's pixels included, which a DOM read never sees.
QImage captureImage(QWidget* window)
{
    QImage image;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT))
            image = QImage(static_cast<const uchar*>(bits), w, h, QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return image;
}

void capture(QWidget* window, const QString& path)
{
    captureImage(window).save(path);
}


void keepBehind()
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w->isVisible()) continue;
        HWND h = reinterpret_cast<HWND>(w->winId());
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
#endif

#ifdef _WIN32
// What Windows itself asks the window: WM_NCHITTEST at a point of its client
// area (logical px), answered by WindowChrome's filter - a message to our own
// window, not OS input (hub_smoke's helper).
LRESULT nativeHit(QWidget* window, const QPoint& logical)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const double dpr = window->devicePixelRatioF();
    POINT p{LONG(logical.x() * dpr), LONG(logical.y() * dpr)};
    ClientToScreen(hwnd, &p);
    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
}
#endif

QStringList bannedIn(const QString& text)
{
    static const QStringList words = {"Fuse",  "Solid", "OCCT",    "mm3",      "(s)",    "Merge", "Join",
                                      "bevel", "round", "flatten", "symmetry", "kernel", "engine"};
    QStringList found;
    for (const QString& word : words) {
        const bool boundary = word == "round" || word == "flatten";
        for (int at = text.indexOf(word, 0, Qt::CaseInsensitive); at >= 0;
             at = text.indexOf(word, at + 1, Qt::CaseInsensitive)) {
            if (word == "Join" && at + 4 < text.size() && text.at(at + 4).toLower() == 't') continue;
            if (!boundary || at == 0 || !text.at(at - 1).isLetterOrNumber()) {
                found << word;
                break;
            }
        }
    }
    return found;
}

}  // namespace

#ifdef _WIN32
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

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(reportCrash);
#endif
    // Unbuffered: a crash's last line tells where it happened.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QSurfaceFormat::setDefaultFormat(OcctViewWidget::surfaceFormat());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    WebHost::prepareApplication();
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    Theme::apply(app);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QDir::current().filePath(QStringLiteral("chrome-smoke-out"));
    QDir().mkpath(outDir);
#ifdef _WIN32
    QTimer behind;
    QObject::connect(&behind, &QTimer::timeout, [] { keepBehind(); });
    behind.start(125);
#endif

    QTemporaryDir library;
    if (!library.isValid()) return 2;
    QString furnitureId;
    {
        FurnitureStore store(library.path());
        furnitureId = store.createFurniture(QStringLiteral("Bench"));
        DocumentModel doc;
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 600, 300, 18));
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 18), 18, 300, 400));
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(582, 0, 18), 18, 300, 400));
        store.saveFurniture(furnitureId, doc, QImage());
    }
    // A side panel and a shelf in a T, with a dowel joint (Phase 3d).
    QString shelfUnitId;
    {
        FurnitureStore store(library.path());
        shelfUnitId = store.createFurniture(QStringLiteral("Shelf unit"));
        DocumentModel doc;
        const int sidePanel = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 18, 300, 800));
        const int shelf = doc.addSolid(ModelingOps::makeBox(gp_Pnt(18, 0, 400), 600, 300, 18));
        doc.addJoint(Joinery::Kind::Dowel, sidePanel, shelf, Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
        store.saveFurniture(shelfUnitId, doc, QImage());
    }

    // A cabinet for the Items card (Phase 3h): Carcass { Side left, Side
    // right, Top }, then a loose Shelf and Back.
    QString cabinetId;
    {
        FurnitureStore store(library.path());
        cabinetId = store.createFurniture(QStringLiteral("Cabinet"));
        DocumentModel doc;
        auto board = [&](const char* name, double x) {
            const int id = doc.addSolid(ModelingOps::makeBox(gp_Pnt(x, 0, 0), 18, 300, 600));
            doc.setItemName(id, name);
            return id;
        };
        const int sideL = board("Side left", 0);
        const int sideR = board("Side right", 200);
        const int top = board("Top", 400);
        board("Shelf", 600);
        board("Back", 800);
        const int carcass = doc.createGroup("Carcass");
        for (int id : {sideL, sideR, top}) doc.setItemGroup(id, carcass);
        store.saveFurniture(cabinetId, doc, QImage());
    }

    // T-064: mirroring on, a folder "Pair" reading B then A (a stored order).
    QString mirroredId;
    {
        FurnitureStore store(library.path());
        mirroredId = store.createFurniture(QStringLiteral("Mirrored"));
        DocumentModel doc;
        doc.setSymmetry(true, gp_Pln(gp_Pnt(-50, 0, 0), gp_Dir(1, 0, 0)));
        const int a = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 18, 300, 400));
        doc.setItemName(a, "A");
        const int b = doc.addSolid(ModelingOps::makeBox(gp_Pnt(100, 0, 0), 18, 300, 400));
        doc.setItemName(b, "B");
        const int pair = doc.createGroup("Pair");
        doc.setItemGroup(a, pair);
        doc.setItemGroup(b, pair);
        doc.moveItems({b}, pair, a);
        store.saveFurniture(mirroredId, doc, QImage());
    }

    MainWindow window(nullptr, /*persistProgress=*/false, library.path());
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1280, 820);
    window.useHtmlChrome();
    window.show();
    window.view()->setAnimationsEnabled(false);
    check(window.openFurniture(furnitureId), "the seeded furniture opens");
    EditorChrome* chrome = window.htmlChrome();
    check(chrome != nullptr, "the window is in HTML-chrome mode");
    if (!chrome) return 1;
    QWebEngineView* page = chrome->view();
    check(waitFor([&] { return chrome->host()->loaded(); }, 20000) && chrome->host()->loadOk(),
          "the editor's chrome page loads");
    waitFor([&] { return js(page, "!!document.querySelector('.strip')").toBool(); }, 5000);
    pump(400);

    // --- the status strip ---------------------------------------------------------
    waitFor([&] {
        return jsString(page, "document.querySelector('.strip .left').textContent") ==
               window.statusLine()->message();
    }, 3000);
    check(jsString(page, "document.querySelector('.strip .left').textContent") ==
              window.statusLine()->message(),
          "the strip's left channel is the status line's sentence - \"" + window.statusLine()->message() + "\"");
    check(jsString(page, "(document.querySelector('.strip .name') || {}).textContent || ''") == QStringLiteral("Bench"),
          "the name mark names the open furniture");
    {
        const int stripTop = js(page, "Math.round(document.querySelector('.strip').getBoundingClientRect().top)").toInt();
        bool clear = true;
        for (const QRect& r : window.findChild<ViewportOverlay*>() ? window.findChild<ViewportOverlay*>()->occupiedRects()
                                                                   : std::vector<QRect>())
            if (r.bottom() >= stripTop) clear = false;
        check(clear, "every Qt card the overlay anchored stands above the strip");
    }

    // --- a Note with Undo, and the Undo -------------------------------------------
    const int bodies = static_cast<int>(window.document().solids().size());
    window.view()->setSelectedSolids({window.document().solids().front().id});
    pump(200);
    QAction* del = actionNamed(&window, QStringLiteral("Delete Selected"));
    check(del != nullptr, "the editor has Delete Selected");
    if (del) del->trigger();
    waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
    check(toastText(page).startsWith(QStringLiteral("Deleted")) &&
              static_cast<int>(window.document().solids().size()) == bodies - 1,
          "deleting a body shows its Note on the page - \"" + toastText(page) + "\"");
    {
        const QRect t = toastRect(page);
        bool clear = !t.isEmpty();
        for (const QRect& r : window.findChild<ViewportOverlay*>()->occupiedRects())
            if (r.intersects(t)) clear = false;
        check(clear, QStringLiteral("the toast stands clear of every overlay card (%1,%2 %3x%4)")
                         .arg(t.x()).arg(t.y()).arg(t.width()).arg(t.height()));
        check(jsString(page, "document.querySelector('.toast .undo').getAttribute('aria-disabled')") ==
                  QStringLiteral("false"),
              "the toast's Undo is open the moment it arrives");
        check(window.view()->childAt(t.center()) != nullptr &&
                  window.view()->childAt(t.center()) != window.view(),
              "a press on the toast reaches the page");
    }
#ifdef _WIN32
    pump(300);   // past the 160 ms fade-in
    capture(&window, outDir + "/chrome-note.png");
#endif
    std::printf("[info] undo pill: %s; Undo action enabled: %d\n",
                qPrintable(jsString(page, "(() => { const u = document.querySelector('.toast .undo'); "
                                          "return u ? 'present, aria-disabled=' + u.getAttribute('aria-disabled') : 'absent'; })()")),
                int(actionNamed(&window, QStringLiteral("Undo"))->isEnabled()));
    js(page, "document.querySelector('.toast .undo').click()");
    waitFor([&] { return static_cast<int>(window.document().solids().size()) == bodies; }, 3000);
    check(static_cast<int>(window.document().solids().size()) == bodies, "the toast's Undo undoes the delete");
    waitFor([&] { return toastText(page).isEmpty(); }, 1500);
    check(toastText(page).isEmpty(), "and the toast that named the delete is gone once the document moved on");

    // --- Undo still works after autosave has written the furniture -------------------
    // Saving records the furniture's wood and texture, which moves the
    // document's revision with no change to the document; a toast's Undo
    // must still take back the change it names after that. (Found at 150%,
    // where the capture above outlasted the 400 ms autosave debounce.)
    {
        window.view()->setSelectedSolids({window.document().solids().front().id});
        pump(200);
        const int before = static_cast<int>(window.document().solids().size());
        if (del) del->trigger();
        waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
        const int revisionAtDelete = window.document().revision();
        pump(900);   // past the autosave debounce
        std::printf("[info] revision at the delete %d, after autosave %d\n", revisionAtDelete,
                    window.document().revision());
        js(page, "document.querySelector('.toast .undo').click()");
        waitFor([&] { return static_cast<int>(window.document().solids().size()) == before; }, 3000);
        check(static_cast<int>(window.document().solids().size()) == before,
              "the toast's Undo still works once autosave has saved the furniture");
        waitFor([&] { return toastText(page).isEmpty(); }, 1500);
    }

    // --- notifications off: no Note, the sentence on the strip ------------------------
    QAction* notes = actionNamed(&window, QStringLiteral("Show notifications"));
    check(notes != nullptr && notes->isChecked(), "Show notifications is on to start with");
    if (notes) notes->trigger();
    window.view()->setSelectedSolids({window.document().solids().front().id});
    pump(200);
    if (del) del->trigger();
    pump(600);
    check(toastText(page).isEmpty(), "with notifications off a Note is not shown");
    check(jsString(page, "document.querySelector('.strip .left').textContent").startsWith(QStringLiteral("Deleted")),
          "and the strip keeps its sentence instead");
    if (notes) notes->trigger();
    actionNamed(&window, QStringLiteral("Undo"))->trigger();
    pump(400);

    // --- a Failure: shown even then, its reason outlives it on the strip -------------------
    TopoDS_Face face;
    for (TopExp_Explorer e(window.document().solids().front().shape, TopAbs_FACE); e.More(); e.Next()) {
        face = TopoDS::Face(e.Current());
        break;
    }
    const bool pulled = window.pullFaceBy(face, -100000.0);
    waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
    const QString reason = toastText(page);
    check(!pulled && js(page, "!!document.querySelector('.toast.failure .glyph')").toBool() && !reason.isEmpty(),
          "a refused pull shows a Failure with its warning mark - \"" + reason + "\"");
#ifdef _WIN32
    pump(300);
    capture(&window, outDir + "/chrome-failure.png");
#endif
    pump(8400);
    check(toastText(page).isEmpty(), "the Failure leaves after its 8 s");
    check(jsString(page, "document.querySelector('.strip .left').textContent") ==
              MainWindow::statusLineFor(reason),
          "and its reason stays on the strip afterwards");

    // --- the hover pause -------------------------------------------------------------
    window.view()->setSelectedSolids({window.document().solids().front().id});
    pump(200);
    if (del) del->trigger();
    waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
    js(page, "document.querySelector('.toast').dispatchEvent(new MouseEvent('mouseenter'))");
    pump(5000);
    check(!toastText(page).isEmpty(), "a toast under the pointer waits past its 4 s");
    js(page, "document.querySelector('.toast').dispatchEvent(new MouseEvent('mouseleave'))");
    // It resumes from what was LEFT (about 4 s here - the pointer arrived at
    // once), never under 1.5 s.
    pump(4600);
    check(toastText(page).isEmpty(), "and goes once the pointer leaves");

    // --- a stale Undo is refused (review I1) --------------------------------------------
    // The pill's Undo names one change. A click that reaches C++ after the
    // document has moved past it - a double-click, or a Ctrl+Z racing the
    // page's documentMoved - must not pop the checkpoint BEFORE it.
    {
        window.view()->setSelectedSolids({window.document().solids().front().id});
        pump(200);
        if (del) del->trigger();
        waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
        const int staleStamp = window.document().revision();
        actionNamed(&window, QStringLiteral("Undo"))->trigger();   // Ctrl+Z
        pump(100);
        const int restored = static_cast<int>(window.document().solids().size());
        const int revision = window.document().revision();
        chrome->bridge()->undo(staleStamp);
        pump(100);
        check(static_cast<int>(window.document().solids().size()) == restored &&
                  window.document().revision() == revision,
              "an Undo naming a change the document has moved past is refused");
    }

    // --- the strip follows the state line on every route (review I4) ---------------------
    if (QAction* items = actionNamed(&window, QStringLiteral("Items")); items && !items->isChecked())
        items->trigger();
    pump(300);
    // The drawer is the page's card in this mode (Phase 3h): its drag is
    // driven there, through the page's own events.
    waitFor([&] { return js(page, "document.querySelectorAll('.items-card [data-row]').length").toInt() >= 2; }, 3000);
    if (js(page, "document.querySelectorAll('.items-card [data-row]').length").toInt() >= 2 && window.itemsModel()) {
        js(page, "(() => { const rows = document.querySelectorAll('.items-card [data-row]'); "
                 "const a = rows[0].getBoundingClientRect(), b = rows[1].getBoundingClientRect(); "
                 "const x = a.left + a.width / 3, y0 = a.top + a.height / 2, y1 = b.bottom - 3; "
                 "rows[0].dispatchEvent(new MouseEvent('mousedown', {bubbles: true, button: 0, buttons: 1, clientX: x, clientY: y0})); "
                 "for (let i = 1; i <= 4; i++) window.dispatchEvent(new MouseEvent('mousemove', "
                 "{bubbles: true, buttons: 1, clientX: x, clientY: y0 + (y1 - y0) * i / 4})); })()");
        pump(200);
        const QString line = window.itemsModel()->dragStatusText();
        const QString rightTitle = QStringLiteral("(document.querySelector('.strip .right') || {}).title || ''");
        waitFor([&] { return jsString(page, rightTitle) == line; }, 1500);
        check(!line.isEmpty() && jsString(page, rightTitle) == line,
              "a drawer drag's line reaches the strip's right channel - \"" + line + "\"");
        js(page, "window.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        pump(200);
        check(!window.itemsModel()->dragActive(), "and Escape ends the drag");
        // The press held the list's keys, as a click in the Qt list gives it
        // focus; a click on the viewport's empty space gives them back.
        check(chrome->holdsListKeys(), "the press on a row held the list's keys");
        const QPoint blank(window.view()->width() - 60, window.view()->height() / 2);
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(blank), QPointF(window.view()->mapToGlobal(blank)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window.view(), &down);
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(blank), QPointF(window.view()->mapToGlobal(blank)),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window.view(), &up);
        pump(150);
        check(!chrome->holdsListKeys(),
              QStringLiteral("and a click on the viewport gave them back (focus: %1)")
                  .arg(window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none"));
    } else {
        check(false, "the Items card lists the two seeded bodies");
    }
    {
        // A long state line is capped at half the strip, as Qt caps the label.
        const int stripW = js(page, "document.querySelector('.strip').getBoundingClientRect().width").toInt();
        js(page, "document.querySelector('.strip .right').textContent = 'x'.repeat(600)");
        pump(100);
        const int rightW = js(page, "document.querySelector('.strip .right').getBoundingClientRect().width").toInt();
        check(stripW > 0 && rightW <= stripW / 2 + 1,
              QStringLiteral("a long state line takes at most half the strip (%1 of %2)").arg(rightW).arg(stripW));
        window.view()->setSelectedSolids({});   // a sync puts the real line back
        pump(200);
    }

    // --- the inset follows the strip (review I5) -----------------------------------------
    {
        ViewportOverlay* overlay = window.findChild<ViewportOverlay*>();
        QAction* bar = actionNamed(&window, QStringLiteral("Show bottom bar"));
        check(bar != nullptr && bar->isChecked() && overlay && overlay->bottomInset() == 24,
              "with the strip up, the overlay keeps 24 px for it");
        const int minimumWithStrip = window.view()->minimumHeight();
        if (bar) bar->trigger();
        pump(200);
        check(overlay && overlay->bottomInset() == 0, "with Bottom bar off, the overlay keeps nothing back");
        check(window.view()->minimumHeight() == minimumWithStrip - 24,
              QStringLiteral("and the viewport's minimum height counts the strip only while it is up (%1 vs %2)")
                  .arg(window.view()->minimumHeight()).arg(minimumWithStrip));
        if (bar) bar->trigger();
        pump(200);
    }

    // --- a chip placed near the bottom stays above the strip (review I6) ------------------
    {
        QWidget chip(window.view());
        chip.resize(120, 40);
        const QPoint at = GestureChip::placeBeside(&chip, window.view(), QPoint(300, window.view()->height() - 2));
        check(at.y() + chip.height() <= window.view()->height() - 24 - GestureChip::kEdgeInset + 1,
              QStringLiteral("a gesture chip clamped to the bottom stands above the strip (bottom %1, strip at %2)")
                  .arg(at.y() + chip.height()).arg(window.view()->height() - 24));
        check(ViewportOverlay::usableHeight(window.view()) == window.view()->height() - 24,
              "the viewport's usable height leaves the strip's band out");
    }

    // --- the page takes no keyboard focus (review I7) --------------------------------------
    check(page->focusPolicy() == Qt::NoFocus &&
              (!page->focusProxy() || page->focusProxy()->focusPolicy() == Qt::NoFocus),
          "the chrome page and its render widget take no keyboard focus");

    // --- the Settings card's place (Phase 3b) ------------------------------------------
    QAction* settingsAction = actionNamed(&window, QStringLiteral("Settings..."));
    check(settingsAction != nullptr, "the editor has Settings...");
    if (settingsAction && !settingsAction->isChecked()) settingsAction->trigger();
    pump(400);
    QWidget* settingsSlot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_settings"));
    {
        const QJsonObject card = QJsonDocument::fromJson(
            jsString(page, "JSON.stringify((window.__chromeCards || {}).settings || {})").toUtf8()).object();
        check(settingsSlot && settingsSlot->isVisible(), "Settings shows the card's slot");
        check(settingsSlot && card.value("x").toInt() == settingsSlot->x() &&
                  card.value("y").toInt() == settingsSlot->y() && card.value("shown").toBool(),
              QStringLiteral("the page is told where the slot stands (%1,%2)")
                  .arg(card.value("x").toInt()).arg(card.value("y").toInt()));
        waitFor([&] { return js(page, "!!document.querySelector('.settings-card')").toBool(); }, 3000);
        const QRect drawn = QRect(js(page, "Math.round(document.querySelector('.settings-card').getBoundingClientRect().left)").toInt(),
                                  js(page, "Math.round(document.querySelector('.settings-card').getBoundingClientRect().top)").toInt(),
                                  js(page, "Math.round(document.querySelector('.settings-card').getBoundingClientRect().width)").toInt(),
                                  js(page, "Math.round(document.querySelector('.settings-card').getBoundingClientRect().height)").toInt());
        check(settingsSlot && drawn == settingsSlot->geometry(),
              QStringLiteral("the page draws the card exactly over its slot (%1,%2 %3x%4)")
                  .arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height()));
        pump(200);
        check(settingsSlot && chrome->hitRegion().contains(settingsSlot->geometry().center()),
              "and a press on the card reaches the page");
        // The colour list scrolls, and says so with the app's scrollbar, as
        // the Qt card's does - not a list whose last row is simply cut off.
        const int bar = js(page, "(() => { const l = document.querySelector('.settings-card .colours'); "
                                 "return l.scrollHeight > l.clientHeight ? l.offsetWidth - l.clientWidth : -1; })()").toInt();
        check(bar > 0, QStringLiteral("the colour list shows its scrollbar (%1 px)").arg(bar));
    }

    // --- typing in a field keeps every key (Review Focus 1) ----------------------------
    {
        const int before = static_cast<int>(window.document().solids().size());
        window.view()->setSelectedSolids({window.document().solids().front().id});
        pump(100);
        js(page, "document.querySelector('[data-tab=\"1\"]').click()");
        pump(150);
        js(page, "(() => { const f = document.querySelector('[data-number=\"basePt\"] input'); "
                 "f.dispatchEvent(new MouseEvent('mousedown', {bubbles: true})); f.focus(); })()");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        check(chrome->holdsKeyboard(), "pressing a typed field gives the page the keyboard");
        QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
        for (int key : {int(Qt::Key_Delete), int(Qt::Key_Backspace), int(Qt::Key_1)}) {
            QKeyEvent over(QEvent::ShortcutOverride, key, Qt::NoModifier);
            over.ignore();
            QCoreApplication::sendEvent(proxy, &over);
            check(over.isAccepted(), QStringLiteral("the page claims key %1 before any shortcut").arg(key));
        }
        QKeyEvent undo(QEvent::ShortcutOverride, Qt::Key_Z, Qt::ControlModifier);
        undo.ignore();
        QCoreApplication::sendEvent(proxy, &undo);
        check(undo.isAccepted(), "and Ctrl+Z");
        // WebEngine claims editing keys for a focused field on its own; the
        // window's OTHER shortcuts - Duplicate, Settings - are the ones only
        // the chrome's claim keeps from firing while a field is typed in.
        for (const QKeySequence& shortcut : {QKeySequence(Qt::CTRL | Qt::Key_D),
                                             QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A)}) {
            const QKeyCombination combo = shortcut[0];
            QKeyEvent over(QEvent::ShortcutOverride, combo.key(), combo.keyboardModifiers());
            over.ignore();
            QCoreApplication::sendEvent(proxy, &over);
            check(over.isAccepted(),
                  "the page claims " + shortcut.toString() + " too, so no window shortcut fires");
        }
        check(static_cast<int>(window.document().solids().size()) == before, "so nothing was deleted");
        js(page, "document.querySelector('[data-number=\"basePt\"] input').dispatchEvent("
                 "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return !chrome->holdsKeyboard(); }, 2000);
        check(!chrome->holdsKeyboard() && page->focusPolicy() == Qt::NoFocus &&
                  (!page->focusProxy() || page->focusProxy()->focusPolicy() == Qt::NoFocus),
              "Escape gives the keyboard back");
        // Keys go where focus is; afterwards it is no longer the page's. (A
        // ShortcutOverride sent straight AT the render widget is answered by
        // WebEngine itself, focused or not - not what a real key meets.)
        check(window.focusWidget() != proxy && window.focusWidget() != page,
              QStringLiteral("and afterwards keys go back to where they went before (%1; field %2, list %3)")
                  .arg(window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none")
                  .arg(chrome->holdsKeyboard())
                  .arg(chrome->holdsListKeys()));
    }

    // --- the keyboard passes from field to field, and never steals focus (review I1, I2) --
    {
        // The browser's order moving from field A to field B: B's claim
        // first, A's release after. The keyboard must stay with the page.
        chrome->bridge()->claimKeyboard();
        chrome->bridge()->claimKeyboard();
        chrome->bridge()->releaseKeyboard();
        check(chrome->holdsKeyboard(), "moving from one field to the next keeps the keyboard");
        chrome->bridge()->releaseKeyboard();
        check(!chrome->holdsKeyboard(), "and the last field out gives it back");

        // The user clicks a Qt field while typing in the page: the release
        // that follows must leave focus where the user put it.
        auto* elsewhere = new QLineEdit(&window);
        elsewhere->setGeometry(10, 10, 80, 20);
        elsewhere->show();
        window.view()->setFocus();
        chrome->bridge()->claimKeyboard();
        QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
        check(window.focusWidget() == proxy, "a claim puts focus in the page");
        elsewhere->setFocus();
        chrome->bridge()->releaseKeyboard();
        check(window.focusWidget() == elsewhere, "a release leaves focus where the user moved it");
        window.view()->setFocus();
        chrome->bridge()->claimKeyboard();
        chrome->bridge()->releaseKeyboard();
        check(window.focusWidget() == window.view(), "but hands it back when it was still the page's");
        delete elsewhere;
    }

    // --- a LIST holds only its own keys (Phase 3h, ruling R3; Review Focus 1) -------------
    {
        QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
        const auto claimed = [&](int key, Qt::KeyboardModifiers mods) {
            QKeyEvent over(QEvent::ShortcutOverride, key, mods);
            over.ignore();
            QCoreApplication::sendEvent(proxy, &over);
            return over.isAccepted();
        };
        window.view()->setFocus();
        chrome->holdListKeys(QStringLiteral("items"));
        check(chrome->holdsListKeys() && window.focusWidget() == proxy, "a list hold puts focus in the page");
        check(claimed(Qt::Key_Up, Qt::NoModifier) && claimed(Qt::Key_Down, Qt::NoModifier) &&
                  claimed(Qt::Key_Home, Qt::NoModifier) && claimed(Qt::Key_Up, Qt::AltModifier),
              "it claims the list's keys from the shortcut map - the arrows, Home, Alt+Up");
        check(!claimed(Qt::Key_Z, Qt::ControlModifier), "but not Ctrl+Z: Undo still fires while the list has focus");
        check(!claimed(Qt::Key_E, Qt::NoModifier) && !claimed(Qt::Key_D, Qt::ControlModifier) &&
                  !claimed(Qt::Key_Delete, Qt::NoModifier),
              "nor E, Ctrl+D or Delete - every other shortcut stays the window's");
        // A field in the same card (the filter) claims and lets go: the keys
        // go back to the list, not to the viewport.
        chrome->claimKeyboard(QStringLiteral("items"));
        check(claimed(Qt::Key_Z, Qt::ControlModifier), "a field claiming the keyboard takes every key");
        chrome->releaseKeyboard(QStringLiteral("items"));
        check(chrome->holdsListKeys() && window.focusWidget() == proxy && !claimed(Qt::Key_Z, Qt::ControlModifier),
              "and letting go hands the keys back to the list");
        // The window losing activation is not the user leaving the list.
        QFocusEvent away(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(proxy, &away);
        check(chrome->holdsListKeys(), "the window losing activation keeps the hold");
        // Focus moving away for any other reason ends the hold - a Qt field
        // the user clicks, in an active window.
        QFocusEvent moved(QEvent::FocusOut, Qt::MouseFocusReason);
        QCoreApplication::sendEvent(proxy, &moved);
        check(!chrome->holdsListKeys(), "focus moving elsewhere ends the hold");
        check(page->focusPolicy() == Qt::NoFocus, "and the page refuses focus again");
        // A click on the viewport ends it too, active window or not.
        window.view()->setFocus();
        chrome->holdListKeys(QStringLiteral("items"));
        const QPoint spot = window.view()->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(spot), QPointF(window.view()->mapToGlobal(spot)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window.view(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(spot), QPointF(window.view()->mapToGlobal(spot)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window.view(), &release);
        pump(100);
        check(!chrome->holdsListKeys() && window.focusWidget() == window.view(),
              "a click on the viewport gives every key back");
        chrome->holdListKeys(QStringLiteral("items"));
        chrome->releaseListKeys(QStringLiteral("items"));
        check(!chrome->holdsListKeys() && window.focusWidget() == window.view(),
              "a release hands focus back to where it was");
    }

    // --- a wheel over the card never zooms (Review Focus 2) -----------------------------
    if (settingsSlot) {
        const double distance = window.view()->camera().state().distance;
        const QPoint at = settingsSlot->geometry().topLeft() + QPoint(40, 16);   // the title
        // What WebEngine does with a wheel the page did not consume: hands it
        // to the view's parent - here, sent straight there.
        QWheelEvent bounced(QPointF(at), QPointF(window.view()->mapToGlobal(at)), QPoint(), QPoint(0, 120),
                            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window.view(), &bounced);
        pump(150);
        check(std::abs(window.view()->camera().state().distance - distance) < 1e-6,
              "a wheel coming back from the card leaves the camera alone");
        // And end to end, into the page's own widget.
        if (QWidget* target = window.view()->childAt(at)) {
            const QPoint local = target->mapFrom(window.view(), at);
            QWheelEvent wheel(QPointF(local), QPointF(target->mapToGlobal(local)), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(target, &wheel);
        }
        pump(300);
        check(std::abs(window.view()->camera().state().distance - distance) < 1e-6,
              "a wheel over the card's title leaves the camera alone");
        const QPoint outside = settingsSlot->geometry().topLeft() - QPoint(60, -60);
        QWheelEvent open(QPointF(outside), QPointF(window.view()->mapToGlobal(outside)), QPoint(), QPoint(0, 120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window.view(), &open);
        pump(150);
        check(std::abs(window.view()->camera().state().distance - distance) > 1e-6,
              "while a wheel beside the card still zooms");
    }

    // --- the drawer, end to end -----------------------------------------------------------
    {
        js(page, "document.querySelector('[data-tab=\"2\"]').click()");
        pump(200);
        QAction* gridAction = actionNamed(&window, QStringLiteral("Grid"));
        check(gridAction != nullptr, "the editor has Grid");
        if (!gridAction) return 1;
        const bool gridWas = gridAction && gridAction->isChecked();
        js(page, "document.querySelector('[data-action=\"grid\"]').click()");
        pump(300);
        check(gridAction && gridAction->isChecked() != gridWas, "the grid switch triggers the View menu's action");
        const QString switchState = QStringLiteral(
            "document.querySelector('[data-action=\"grid\"]').getAttribute('aria-checked')");
        waitFor([&] { return jsString(page, switchState) == (gridAction->isChecked() ? "true" : "false"); }, 2000);
        check(jsString(page, switchState) == (gridAction->isChecked() ? "true" : "false"),
              "and the switch reads the action back");
        gridAction->trigger();   // the menu moves the switch
        waitFor([&] { return jsString(page, switchState) == (gridAction->isChecked() ? "true" : "false"); }, 2000);
        check(jsString(page, switchState) == (gridAction->isChecked() ? "true" : "false"),
              "a change made from the menu reaches the switch");

        const double gizmoWas = Theme::spec().gizmoScale;
        js(page, "document.querySelector('[data-number=\"gizmoScale\"] .up').click()");
        pump(300);
        check(std::abs(Theme::spec().gizmoScale - std::min(2.0, gizmoWas + 0.1)) < 1e-9,
              QStringLiteral("Gizmo size steps by 0.1 into the live spec (%1)").arg(Theme::spec().gizmoScale));

        SettingsEdits::setTokenColour(QStringLiteral("accent"), QColor(200, 40, 90));   // made elsewhere
        js(page, "document.querySelector('[data-tab=\"0\"]').click()");
        const QString swatch = QStringLiteral(
            "getComputedStyle(document.querySelector('[data-swatch=\"accent\"]')).backgroundColor");
        waitFor([&] { return jsString(page, swatch) == QStringLiteral("rgb(200, 40, 90)"); }, 2000);
        check(jsString(page, swatch) == QStringLiteral("rgb(200, 40, 90)"),
              "an edit made elsewhere re-dresses the open card - \"" + jsString(page, swatch) + "\"");

        js(page, "document.querySelector('.settings-card .reset').click()");
        pump(200);
        check(Theme::spec().accent == QColor(200, 40, 90), "one click on Reset changes nothing");
        js(page, "document.querySelector('.settings-card .reset').click()");
        pump(300);
        check(Theme::spec() == Theme::defaultSpec(), "the second click resets");

        js(page, "document.querySelector('[data-tab=\"1\"]').click()");
        pump(150);
        js(page, "(() => { const f = document.querySelector('[data-number=\"basePt\"] input'); "
                 "f.dispatchEvent(new MouseEvent('mousedown', {bubbles: true})); f.focus(); })()");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        settingsAction->trigger();   // hidden mid-edit (Review Focus 4)
        pump(300);
        check(settingsSlot && !settingsSlot->isVisible() &&
                  !js(page, "!!document.querySelector('.settings-card')").toBool(),
              "Settings off takes the card away");
        check(!chrome->holdsKeyboard(), "and gives the keyboard back mid-edit");
    }
    // --- the Versions card's place (Phase 3c) ------------------------------------
    QAction* versionsAction = actionNamed(&window, QStringLiteral("Versions"));
    check(versionsAction != nullptr, "the editor has Versions");
    if (versionsAction && !versionsAction->isChecked()) versionsAction->trigger();
    pump(400);
    QWidget* versionsSlot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_versions"));
    check(versionsSlot && versionsSlot->isVisible(), "Versions shows the card's slot");
    check(window.versionsModel() && window.versionsModel()->canSave(),
          "the page's versions object is there, with + open");
    if (!versionsSlot || !versionsAction) return 1;

    {
        const QString furnitureId = window.currentFurnitureId();
        FurnitureStore& store = window.furnitureStore();
        const QString cardRect = QStringLiteral(
            "JSON.stringify((() => { const c = document.querySelector('.versions-card'); if (!c) return {}; "
            "const r = c.getBoundingClientRect(); return {x: Math.round(r.left), y: Math.round(r.top), "
            "w: Math.round(r.width), h: Math.round(r.height)}; })())");
        auto drawn = [&] {
            const QJsonObject o = QJsonDocument::fromJson(jsString(page, cardRect).toUtf8()).object();
            return QRect(o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(), o.value("h").toInt());
        };
        auto reported = [&] { return js(page, "window.__versionsHeight || 0").toInt(); };
        auto settled = [&] {
            return reported() > 0 && versionsSlot->height() == Theme::wholeDevicePixels(reported()) &&
                   drawn() == versionsSlot->geometry();
        };
        waitFor([&] { return js(page, "!!document.querySelector('.versions-card')").toBool(); }, 3000);
        const bool versionsSettled = waitStable(settled, 5000);
        check(versionsSettled, QStringLiteral("the card reports its height, the slot takes it, and the page draws "
                                        "exactly over the slot (%1x%2, reported %3)")
                             .arg(drawn().width()).arg(drawn().height()).arg(reported()));
        check(chrome->hitRegion().contains(versionsSlot->geometry().center()), "a press on the card reaches the page");
        check(jsString(page, "(document.querySelector('.versions-card .empty') || {}).textContent || ''")
                  .startsWith(QStringLiteral("No versions yet.")),
              "a furniture with no versions shows the empty state");
        const int emptyHeight = versionsSlot->height();

        // + opens a pending version, and typing in it keeps every key.
        js(page, "document.querySelector('.versions-card .add').click()");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        check(jsString(page, "(document.querySelector('.pending input') || {}).value || ''") == QStringLiteral("Version 1"),
              "+ opens a pending version named Version 1");
        check(chrome->holdsKeyboard(), "and the field holds the keyboard");
        // Another card closing leaves this one the keyboard (review I2): the
        // claims are per card, and Settings held none.
        settingsAction->trigger();
        pump(300);
        settingsAction->trigger();
        pump(300);
        check(chrome->holdsKeyboard(), "Settings opening and closing leaves the Versions field the keyboard (review I2)");
        const int bodiesBefore = static_cast<int>(window.document().solids().size());
        const std::vector<std::pair<int, Qt::KeyboardModifiers>> keys = {
            {Qt::Key_Delete, Qt::NoModifier}, {Qt::Key_Z, Qt::ControlModifier},
            {Qt::Key_1, Qt::NoModifier}, {Qt::Key_Space, Qt::NoModifier}};
        for (const auto& [key, mods] : keys) {
            QKeyEvent over(QEvent::ShortcutOverride, key, mods);
            QCoreApplication::sendEvent(chrome->view()->focusProxy(), &over);
            check(over.isAccepted(), QStringLiteral("key %1 typed into the name is the field's (Review Focus 1)").arg(key));
        }
        check(static_cast<int>(window.document().solids().size()) == bodiesBefore, "and no body was deleted");

        // Enter saves.
        const QString typeAndEnter = QStringLiteral(
            "(() => { const f = document.querySelector('.pending input'); f.value = '%1'; "
            "f.dispatchEvent(new Event('input', {bubbles: true})); "
            "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()");
        js(page, typeAndEnter.arg(QStringLiteral("Shelf added")));
        waitFor([&] { return !store.versions(furnitureId).isEmpty(); }, 3000);
        check(store.versions(furnitureId).size() == 1 &&
                  store.versions(furnitureId).front().name == QStringLiteral("Shelf added"),
              "Enter saves the version under the typed name");
        waitFor([&] { return toastText(page).endsWith(QStringLiteral("saved")); }, 2000);
        check(toastText(page) == QStringLiteral("Version \"Shelf added\" saved"),
              "and the page says so - \"" + toastText(page) + "\"");
        waitFor([&] { return !chrome->holdsKeyboard(); }, 2000);
        check(!chrome->holdsKeyboard(), "and gives the keyboard back");
        const QString row = QStringLiteral("document.querySelector('.version[data-version=\"Shelf added\"]%1')");
        waitFor([&] { return js(page, "!!" + row.arg(QString())).toBool(); }, 3000);
        const bool held1 = waitStable([&] { return versionsSlot->height() > emptyHeight && settled(); }, 5000);
        check(held1,
              QStringLiteral("the slot grows with the list and the card still covers it exactly (%1 -> %2) (Review Focus 5)")
                  .arg(emptyHeight).arg(versionsSlot->height()));
        const QRect grown = versionsSlot->geometry();
        const QString bodyInside = QStringLiteral(
            "(() => { const c = document.querySelector('.versions-card'), b = c.querySelector('.body'); "
            "return b.getBoundingClientRect().bottom <= c.getBoundingClientRect().bottom - "
            "parseFloat(getComputedStyle(c).borderBottomWidth) + 0.5; })()");
        check(js(page, bodyInside).toBool(), "the card's whole body stands inside its border - none clipped (review M3)");
        const QString thumbLoaded = "(() => { const i = " + row.arg(QStringLiteral(" img.thumb")) +
                                    "; return !!i && i.complete && i.naturalWidth > 0; })()";
        waitFor([&] { return js(page, thumbLoaded).toBool(); }, 3000);
        check(js(page, thumbLoaded).toBool(), "the saved version's picture loads");

        // A duplicate refuses, keeping the card (Review Focus 2).
        js(page, "document.querySelector('.versions-card .add').click()");
        waitFor([&] { return js(page, "!!document.querySelector('.pending input')").toBool(); }, 2000);
        pump(150);
        js(page, typeAndEnter.arg(QStringLiteral("Shelf added")));
        waitFor([&] { return toastText(page).startsWith(QStringLiteral("Couldn")); }, 3000);
        check(toastText(page).startsWith(QStringLiteral("Couldn")) && toastText(page).contains(QStringLiteral("already exists")),
              "a duplicate name is refused, saying why - \"" + toastText(page) + "\"");
        check(jsString(page, "(document.querySelector('.pending input') || {}).value || ''") == QStringLiteral("Shelf added"),
              "and the pending card stays, holding what was typed");
        check(chrome->holdsKeyboard(), "still holding the keyboard");
        js(page, "document.querySelector('.pending input').dispatchEvent("
                 "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return !js(page, "!!document.querySelector('.pending')").toBool(); }, 2000);
        check(store.versions(furnitureId).size() == 1 && !chrome->holdsKeyboard() &&
                  !js(page, "!!document.querySelector('.pending')").toBool(),
              "Escape discards it, saving nothing and giving the keyboard back");

        // File -> Save version... opens the page's pending card.
        QAction* saveVersion = actionNamed(&window, QStringLiteral("Save version..."));
        check(saveVersion != nullptr, "the editor has Save version...");
        if (saveVersion) saveVersion->trigger();
        waitFor([&] { return js(page, "!!document.querySelector('.pending input')").toBool(); }, 2000);
        check(jsString(page, "(document.querySelector('.pending input') || {}).value || ''") == QStringLiteral("Version 1"),
              "File -> Save version... opens the same pending card on the page");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        versionsAction->trigger();   // hidden mid-edit (Review Focus 4)
        // In an OS-active window the field is BLURRED as the card goes away,
        // and a blur commits (review C2); the suite's windows never are, so
        // the blur is dispatched here, after the hide, as it arrives there.
        const bool blurred = js(page, "(() => { const f = document.querySelector('.pending input'); if (!f) return false; "
                                      "f.dispatchEvent(new FocusEvent('blur')); return true; })()").toBool();
        pump(400);
        check(blurred, "the name field was still there to blur as the drawer hid");
        check(!versionsSlot->isVisible() && !js(page, "!!document.querySelector('.versions-card')").toBool(),
              "Versions off takes the card away");
        check(!chrome->holdsKeyboard() && store.versions(furnitureId).size() == 1,
              "and hiding it mid-edit gives the keyboard back and saves nothing");
        versionsAction->trigger();
        waitFor([&] { return js(page, "!!" + row.arg(QString())).toBool(); }, 3000);
        pump(200);
        check(!js(page, "!!document.querySelector('.pending')").toBool(),
              "and it comes back without the pending card the menu asked for before");

        // Restore: one checkpoint, a Note with Undo.
        window.view()->setSelectedSolids({window.document().solids().front().id});
        pump(150);
        if (del) del->trigger();
        pump(300);
        const int afterDelete = static_cast<int>(window.document().solids().size());
        js(page, row.arg(QStringLiteral(" .restore")) + ".click()");
        waitFor([&] { return static_cast<int>(window.document().solids().size()) == afterDelete + 1; }, 3000);
        check(static_cast<int>(window.document().solids().size()) == afterDelete + 1,
              "Restore puts the version's bodies back");
        waitFor([&] { return toastText(page).startsWith(QStringLiteral("Restored")); }, 2000);
        check(toastText(page).startsWith(QStringLiteral("Restored version")) &&
                  jsString(page, "(document.querySelector('.toast .undo') || {}).textContent || ''") == QStringLiteral("Undo"),
              "and its Note offers Undo - \"" + toastText(page) + "\"");

        // Compare opens the second viewport.
        js(page, row.arg(QStringLiteral(" .compare")) + ".click()");
        waitFor([&] { return window.isCompareOpen(); }, 3000);
        check(window.isCompareOpen(), "Compare opens the side-by-side");
        window.closeCompare();
        pump(400);

        // Delete: the first click arms, the second deletes.
        waitFor([&] { return js(page, "!!" + row.arg(QStringLiteral(" .delete"))).toBool(); }, 3000);
        js(page, row.arg(QStringLiteral(" .delete")) + ".click()");
        pump(200);
        check(store.versions(furnitureId).size() == 1 &&
                  jsString(page, row.arg(QStringLiteral(" .delete")) + ".textContent") ==
                      QStringLiteral("Delete — click again"),
              "one click on Delete arms it and deletes nothing");
        js(page, row.arg(QStringLiteral(" .delete")) + ".click()");
        waitFor([&] { return store.versions(furnitureId).isEmpty(); }, 3000);
        check(store.versions(furnitureId).isEmpty(), "the second click deletes the version");
        const bool held2 = waitStable([&] { return versionsSlot->height() == emptyHeight && settled(); }, 5000);
        check(held2,
              QStringLiteral("and the slot shrinks back to the empty state's height (%1) (Review Focus 5)")
                  .arg(versionsSlot->height()));
        waitFor([&] { return !chrome->hitRegion().contains(QPoint(grown.center().x(), grown.bottom() - 4)); }, 2000);
        check(!chrome->hitRegion().contains(QPoint(grown.center().x(), grown.bottom() - 4)),
              "and where the taller card stood, a press reaches the viewport again (review M5)");

        // Each theme on the drawer, with one version to look at.
        // Saved through the window, so it carries its picture.
        window.saveVersion(QStringLiteral("Shelf added"));
        waitFor([&] { return js(page, thumbLoaded).toBool() && settled(); }, 3000);
        for (const Themes::Info& theme : Themes::all()) {
            Themes::select(theme.id);
            waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == theme.id; }, 3000);
            // Delete armed, so the shot shows the actions row the pointer reveals.
            if (jsString(page, row.arg(QStringLiteral(" .delete")) + ".textContent") == QStringLiteral("Delete"))
                js(page, row.arg(QStringLiteral(" .delete")) + ".click()");
            pump(300);
#ifdef _WIN32
            capture(&window, outDir + "/chrome-versions-" + theme.id + ".png");
#endif
            check(jsString(page, "getComputedStyle(document.querySelector('.versions-card .name')).fontFamily")
                      .contains(theme.fontFamily.section(QLatin1Char(' '), 0, 0)),
                  "the Versions card is set in " + theme.name + "'s typeface");
        }
        Themes::select(Themes::graphiteId());
        pump(300);

        // The armed sentence fits its row - seen in a capture: beside Compare
        // and Restore it wrapped and leaked below the card's row.
        if (jsString(page, row.arg(QStringLiteral(" .delete")) + ".textContent") == QStringLiteral("Delete"))
            js(page, row.arg(QStringLiteral(" .delete")) + ".click()");
        // Arming is a C++ round trip, and the row lays out after it: wait for
        // the fit itself (it includes the armed words), never a fixed time.
        const bool armedFits = waitStable([&] { return js(page, "(() => { const a = " + row.arg(QStringLiteral(" .actions")) + "; const d = " +
                           row.arg(QStringLiteral(" .delete")) +
                           "; return a.scrollWidth <= a.clientWidth && d.scrollHeight <= d.clientHeight + 1 && "
                           "[...a.children].every((b) => { const r = b.getBoundingClientRect(), "
                           "o = a.getBoundingClientRect(); return b.scrollWidth <= b.clientWidth && "
                           "r.left >= o.left - 0.5 && r.right <= o.right + 0.5; }) && "
                           "d.textContent.includes('again'); })()").toBool(); }, 3000, 150);
        std::printf("[info] armed row: %s\n",
                    qPrintable(jsString(page, "JSON.stringify((() => { const a = " + row.arg(QStringLiteral(" .actions")) +
                                                  "; const d = " + row.arg(QStringLiteral(" .delete")) +
                                                  "; return {aw: a.scrollWidth, ac: a.clientWidth, dh: d.scrollHeight, "
                                                  "dc: d.clientHeight, n: a.children.length, clipped: [...a.children].filter((b) => b.scrollWidth > b.clientWidth).length, "
                                                  "right: Math.round(d.getBoundingClientRect().right), "
                                                  "card: Math.round(document.querySelector('.versions-card').getBoundingClientRect().right)}; })())")));
        check(armedFits,
              "an armed Delete's sentence fits its row on one line - every button inside the row, none cut off");

        // A long list scrolls inside its 384 px, with the app's scrollbar.
        window.saveVersion(QStringLiteral("Second"));
        window.saveVersion(QStringLiteral("Third"));
        waitFor([&] { return js(page, "document.querySelectorAll('.version').length").toInt() == 3 && settled(); }, 3000);
        const QJsonObject list = QJsonDocument::fromJson(
            jsString(page, "JSON.stringify((() => { const l = document.querySelector('.versions-card .list'); "
                           "return {client: l.clientHeight, scroll: l.scrollHeight, bar: l.offsetWidth - l.clientWidth}; })())")
                .toUtf8()).object();
        check(list.value("scroll").toInt() > list.value("client").toInt() && list.value("client").toInt() <= 384,
              QStringLiteral("a long list scrolls inside 384 px (%1 of %2)")
                  .arg(list.value("client").toInt()).arg(list.value("scroll").toInt()));
        check(list.value("bar").toInt() == 8, QStringLiteral("with the app's 8 px scrollbar (%1)").arg(list.value("bar").toInt()));
        // The card follows its slot a channel message later: read once it holds.
        check(waitStable(settled, 5000), "and the card still covers its slot exactly");
#ifdef _WIN32
        capture(&window, outDir + "/chrome-versions-long.png");
#endif
        store.deleteVersion(furnitureId, QStringLiteral("Second"));
        store.deleteVersion(furnitureId, QStringLiteral("Third"));
        store.deleteVersion(furnitureId, QStringLiteral("Shelf added"));
        window.versionsModel()->refresh();

        // The close x unchecks the action.
        js(page, "document.querySelector('.versions-card .close').click()");
        pump(300);
        check(!versionsAction->isChecked() && !versionsSlot->isVisible(), "the close x closes the drawer");
    }

    // --- the Joints card's place (Phase 3d) ---------------------------------------
    QAction* jointsAction = actionNamed(&window, QStringLiteral("Joints"));
    check(jointsAction != nullptr, "the editor has Joints");
    if (jointsAction && !jointsAction->isChecked()) jointsAction->trigger();
    pump(400);
    QWidget* jointsSlot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_joints"));
    check(jointsSlot && jointsSlot->isVisible(), "Joints shows the card's slot");
    check(window.jointsModel() && window.jointsModel()->live(), "the page's joints object is there, live");
    if (!jointsSlot || !jointsAction) return 1;
    jointsAction->trigger();
    pump(200);

    // --- each theme on the editor's chrome (T-072): a capture of each ----------------
    {
        if (!settingsSlot->isVisible()) settingsAction->trigger();
        pump(300);
        js(page, "document.querySelector('[data-tab=\"0\"]').click()");
        for (const Themes::Info& theme : Themes::all()) {
            Themes::select(theme.id);
            waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == theme.id; }, 3000);
            window.view()->setSelectedSolids({window.document().solids().front().id});
            pump(150);
            if (del) del->trigger();   // a Note with Undo to look at
            waitFor([&] { return !toastText(page).isEmpty(); }, 3000);
            pump(400);
#ifdef _WIN32
            capture(&window, outDir + "/chrome-theme-" + theme.id + ".png");
#endif
            check(jsString(page, "getComputedStyle(document.querySelector('.settings-card')).fontFamily")
                      .contains(theme.fontFamily.section(QLatin1Char(' '), 0, 0)),
                  "the card is set in " + theme.name + "'s typeface - " + theme.fontFamily);
            js(page, "document.querySelector('.toast .undo').click()");
            pump(300);
        }
        Themes::select(Themes::graphiteId());
        pump(300);
        settingsAction->trigger();
        pump(200);
    }

    // --- the theme, chosen on the card (T-072) ------------------------------------
    {
        if (!settingsSlot->isVisible()) settingsAction->trigger();
        pump(300);
        js(page, "document.querySelector('[data-tab=\"0\"]').click()");
        pump(200);
        const int options = js(page, "document.querySelectorAll('[data-row=\"Theme\"] [role=\"radio\"]').length").toInt();
        check(options == 5, QStringLiteral("the Colours tab opens with the five themes (%1)").arg(options));
        check(jsString(page, "document.querySelector('[data-action=\"graphite\"]').getAttribute('aria-checked')") ==
                  QStringLiteral("true"),
              "a window with persistence off is on Graphite");
        SettingsEdits::openColourPicker(QStringLiteral("accent"), &window);
        check(SettingsEdits::activeColourPicker() != nullptr, "a colour picker is open");
        js(page, "document.querySelector('[data-action=\"aurora-mesh-gradient\"]').click()");
        waitFor([&] { return Themes::currentId() == QStringLiteral("aurora-mesh-gradient"); }, 2000);
        check(Themes::currentId() == QStringLiteral("aurora-mesh-gradient"), "a click on Aurora puts it on");
        check(Theme::spec() == Themes::baseSpec(QStringLiteral("aurora-mesh-gradient")),
              "and the app wears its colours - the open picker's Cancel wrote nothing into it");
        pump(100);
        check(SettingsEdits::activeColourPicker() == nullptr, "the picker closed with the switch (Review Focus 1)");
        waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == QStringLiteral("aurora-mesh-gradient"); }, 2000);
        check(jsString(page, "document.documentElement.dataset.theme") == QStringLiteral("aurora-mesh-gradient"),
              "the page wears the theme's id");
        QAction* auroraAction = actionNamed(&window, QStringLiteral("Aurora / Mesh Gradient"));
        check(auroraAction && auroraAction->isChecked(), "and View -> Theme marks it");
        if (QAction* graphite = actionNamed(&window, QStringLiteral("Graphite"))) graphite->trigger();
        pump(300);
        check(Themes::currentId() == Themes::graphiteId() &&
                  jsString(page, "document.querySelector('[data-action=\"graphite\"]').getAttribute('aria-checked')") ==
                      QStringLiteral("true"),
              "the menu switches back, and the card follows");
        settingsAction->trigger();
        pump(200);
    }
#ifdef _WIN32
    for (int tab = 0; tab < 4; ++tab) {
        settingsAction->trigger();
        pump(300);
        js(page, QStringLiteral("document.querySelector('[data-tab=\"%1\"]').click()").arg(tab));
        pump(400);
        capture(&window, outDir + QStringLiteral("/settings-tab-%1.png").arg(tab));
        settingsAction->trigger();
        pump(200);
    }
#endif

    // --- the page's words ----------------------------------------------------------------
    {
        // The card's words too: open it on each tab and read everything shown.
        QString text = jsString(page, "document.body.innerText");
        if (settingsAction) {
            settingsAction->trigger();
            pump(300);
            for (int tab = 0; tab < 4; ++tab) {
                js(page, QStringLiteral("document.querySelector('[data-tab=\"%1\"]').click()").arg(tab));
                pump(150);
                text += QLatin1Char(' ') + jsString(page, "document.body.innerText");
            }
            settingsAction->trigger();
            pump(200);
        }
        text.replace(QStringLiteral("Bench"), QStringLiteral(" "));
        check(bannedIn(text).isEmpty(), "no word the page shows is banned - found: " + bannedIn(text).join(", "));
    }

    // --- a returning user keeps the theme they were given (themes review C1) --------
    // Inside a settings scope of its own (an ini under the temp dir), so no
    // window here can write the real registry.
    {
        const QString org = QCoreApplication::organizationName(), app = QCoreApplication::applicationName();
        const QSettings::Format format = QSettings::defaultFormat();
        QTemporaryDir settingsDir;
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName(QStringLiteral("FurnifyMe-chrome_smoke"));
        QCoreApplication::setApplicationName(QStringLiteral("returning-theme-probe"));
        Theme::Spec legacy = Theme::defaultSpec();
        legacy.accent = QColor(12, 200, 34);
        QSettings().setValue(QStringLiteral("appearance"), Theme::serializeSpec(legacy));   // a build before themes
        const Theme::Spec violet = Themes::baseSpec(Themes::defaultId());
        {
            MainWindow first(nullptr, /*persistProgress=*/true, library.path());
            check(Themes::currentId() == Themes::defaultId() && Theme::spec() == violet,
                  "launch 1 of a build with themes starts Violet Glass Float at its base");
        }
        {
            MainWindow second(nullptr, /*persistProgress=*/true, library.path());
            check(Themes::currentId() == Themes::defaultId() && Theme::spec() == violet,
                  "and launch 2 is still Violet Glass Float at its base - not the old colours (C1)");
            Themes::select(Themes::graphiteId());
            check(Theme::spec().accent == QColor(12, 200, 34), "the old colours are Graphite's");
            Themes::select(Themes::defaultId());
            check(Theme::spec() == violet, "and Violet stays Violet");
        }
        {
            QSettings().setValue(QStringLiteral("theme"), QStringLiteral("from-a-newer-build"));
            MainWindow third(nullptr, /*persistProgress=*/true, library.path());
            check(Themes::currentId() == Themes::defaultId() && Theme::spec() == violet,
                  "a stored theme this build does not know starts the default at its base");
        }
        QSettings().clear();
        QSettings::setDefaultFormat(format);
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        check(Themes::currentId() == Themes::graphiteId() && Theme::spec() == Theme::defaultSpec(),
              "each window gave the theme back when it closed");
    }

    // --- the Joints card, end to end (Phase 3d) ------------------------------------
    {
        check(window.openFurniture(shelfUnitId), "the shelf unit opens");
        pump(300);
        check(window.document().joints().size() == 1, "with its one dowel joint");
        const int jointId = window.document().joints().empty() ? 0 : window.document().joints().front().id;
        const int shelfId = window.document().joints().empty() ? 0 : window.document().joints().front().bodyB;
        jointsAction->trigger();
        waitFor([&] { return js(page, "!!document.querySelector('.joints-card .joint')").toBool(); }, 3000);
        const QString cardRect = QStringLiteral(
            "JSON.stringify((() => { const c = document.querySelector('.joints-card'); if (!c) return {}; "
            "const r = c.getBoundingClientRect(); return {x: Math.round(r.left), y: Math.round(r.top), "
            "w: Math.round(r.width), h: Math.round(r.height)}; })())");
        auto drawn = [&] {
            const QJsonObject o = QJsonDocument::fromJson(jsString(page, cardRect).toUtf8()).object();
            return QRect(o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(), o.value("h").toInt());
        };
        auto settled = [&] {
            const int h = js(page, "window.__jointsHeight || 0").toInt();
            return h > 0 && jointsSlot->height() == Theme::wholeDevicePixels(h) && drawn() == jointsSlot->geometry();
        };
        const bool jointsSettled = waitStable(settled, 5000);
        check(jointsSettled, QStringLiteral("the card reports its height and covers its slot exactly (%1x%2)")
                             .arg(drawn().width()).arg(drawn().height()));
        const QVariantMap modelRow = window.jointsModel()->rows().value(0).toMap();
        const QString row = QStringLiteral("document.querySelector('.joint[data-joint=\"%1\"]%2')").arg(jointId);
        check(js(page, "document.querySelectorAll('.joints-card .joint').length").toInt() == 1 &&
                  jsString(page, row.arg(QStringLiteral(" .kind")) + ".textContent") == modelRow.value("kindLine").toString(),
              "one row, saying what JointRows says - \"" + modelRow.value("kindLine").toString() + "\"");

        // A click opens it and selects the joint.
        js(page, row.arg(QString()) + ".click()");
        waitFor([&] { return js(page, "!!" + row.arg(QStringLiteral(".open .strip"))).toBool(); }, 3000);
        check(window.selectedJointId() == jointId, "a click on the row selects its joint");
        const QVariantList pieces = window.jointsModel()->rows().value(0).toMap().value("pieces").toList();
        const int dowels = pieces.value(0).toMap().value("marks").toList().size();
        check(js(page, "document.querySelectorAll('.joint.open .strip').length").toInt() == pieces.size() &&
                  js(page, "document.querySelector('.joint.open .strip').querySelectorAll('.tick').length").toInt() == dowels &&
                  dowels > 0,
              QStringLiteral("the open row draws a ruler per drilled piece, a tick per dowel (%1 pieces, %2 dowels)")
                  .arg(pieces.size()).arg(dowels));
        check(jsString(page, "(document.querySelector('.joint.open .shared') || {}).textContent || ''") ==
                  window.jointsModel()->rows().value(0).toMap().value("sharedDrawn").toString(),
              "and the shared line under it");
        {
            // Labels on one line keep apart, inside the strip (the ruler's own law).
            const bool apart = js(page, "(() => { for (const s of document.querySelectorAll('.joint.open .ruler')) { "
                                        "const r = s.getBoundingClientRect(); let last = -1e9; "
                                        "for (const l of [...s.querySelectorAll('.tick-label:not(.low)')]) { const b = l.getBoundingClientRect(); "
                                        "if (b.left < last + 2.5 || b.left < r.left - 0.5 || b.right > r.right + 0.5) return false; last = b.right; } } "
                                        "return true; })()").toBool();
            check(apart, "every tick label stands clear of the last one on its line, inside the strip");
        }
        check(waitStable(settled, 5000), "the slot grows with the open row and the card still covers it");
        // Every label was laid out with the width it is PAINTED at, and sits
        // centred on its tick unless the strip clamped it - so a font that
        // changed under the ruler shows here (Review Focus 1, review I1).
        const QString labelsTrue = QStringLiteral(
            "JSON.stringify((() => { let n = 0, bad = []; "
            "for (const ruler of document.querySelectorAll('.joint.open .ruler')) { const box = ruler.getBoundingClientRect(); "
            "for (const l of ruler.querySelectorAll('.tick-label')) { n++; const r = l.getBoundingClientRect(); "
            "const w = parseFloat(l.dataset.w), a = parseFloat(l.dataset.anchor), left = r.left - box.left; "
            "const clamped = left < 0.5 || left + w > box.width - 0.5; "
            "if (Math.abs(w - r.width) > 0.75) bad.push(l.textContent + ' w ' + w.toFixed(2) + ' painted ' + r.width.toFixed(2)); "
            "else if (!clamped && Math.abs(left + w / 2 - a) > 1) bad.push(l.textContent + ' centre ' + (left + w / 2).toFixed(2) + ' tick ' + a.toFixed(2)); } } "
            "return {n, bad: bad.join('; ')}; })())");
        auto labelsHold = [&](const QString& where) {
            const QJsonObject o = QJsonDocument::fromJson(jsString(page, labelsTrue).toUtf8()).object();
            check(o.value("n").toInt() >= dowels && o.value("bad").toString().isEmpty(),
                  QStringLiteral("%1: every tick label is laid out at its painted width, on its tick (%2 labels) %3")
                      .arg(where).arg(o.value("n").toInt()).arg(o.value("bad").toString()));
        };
        for (const Themes::Info& theme : Themes::all()) {
            Themes::select(theme.id);
            waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == theme.id; }, 3000);
            pump(400);
#ifdef _WIN32
            capture(&window, outDir + "/chrome-joints-" + theme.id + ".png");
#endif
            labelsHold(theme.name);
        }
        Themes::select(Themes::graphiteId());
        pump(300);
        {
            const double textSize = Theme::spec().basePt;
            SettingsEdits::setNumber(QStringLiteral("basePt"), textSize + 3);
            pump(500);
            labelsHold(QStringLiteral("text size %1 pt").arg(textSize + 3));
            SettingsEdits::setNumber(QStringLiteral("basePt"), textSize);
            pump(400);
        }

        // The shelf moves away: the joint breaks, keeps its row open, and shows no number.
        waitFor(settled, 2000);
        const int openHeight = jointsSlot->height();   // before the break, never after it
        gp_Trsf away;
        // Sideways, clear of the panel - a shelf lifted along it would still meet it.
        away.SetTranslation(gp_Vec(100, 0, 0));
        window.transformBody(shelfId, away);
        waitFor([&] { return js(page, "!!" + row.arg(QStringLiteral(".broken"))).toBool(); }, 3000);
        check(js(page, "!!" + row.arg(QStringLiteral(".broken.open"))).toBool(),
              "a joint whose pieces part reads broken - and its row stays open (Review Focus 3)");
        check(!js(page, "!!" + row.arg(QStringLiteral(" .strip"))).toBool() &&
                  !QRegularExpression(QStringLiteral("\\d")).match(jsString(page, row.arg(QStringLiteral(" .kind")) + ".textContent")).hasMatch(),
              "with no ruler and no number - \"" + jsString(page, row.arg(QStringLiteral(" .kind")) + ".textContent") + "\" (Review Focus 2)");
        const bool held3 = waitStable([&] { return jointsSlot->height() < openHeight && settled(); }, 5000);
        check(held3,
              QStringLiteral("and the card shrinks to the broken row, still covering its slot (%1 -> %2)").arg(openHeight).arg(jointsSlot->height()));
#ifdef _WIN32
        capture(&window, outDir + "/chrome-joints-broken.png");
#endif
        actionNamed(&window, QStringLiteral("Undo"))->trigger();
        waitFor([&] { return js(page, "!!" + row.arg(QStringLiteral(".open .strip"))).toBool(); }, 3000);
        check(js(page, "!!" + row.arg(QStringLiteral(".open .strip"))).toBool(), "Undo makes it whole again, still open");

        // Delete from the open row: one checkpoint, a Note with Undo.
        js(page, row.arg(QStringLiteral(" .delete")) + ".click()");
        waitFor([&] { return window.document().joints().empty(); }, 3000);
        check(window.document().joints().empty(), "the row's delete deletes the joint");
        waitFor([&] { return !toastText(page).isEmpty(); }, 2000);
        check(jsString(page, "(document.querySelector('.toast .undo') || {}).textContent || ''") == QStringLiteral("Undo"),
              "and its Note offers Undo - \"" + toastText(page) + "\"");
        waitFor([&] { return js(page, "!!document.querySelector('.joints-card .empty')").toBool(); }, 3000);
        check(js(page, "!!document.querySelector('.joints-card .empty')").toBool(), "the card shows its empty state");
        js(page, "document.querySelector('.toast .undo').click()");
        waitFor([&] { return window.document().joints().size() == 1; }, 3000);
        check(window.document().joints().size() == 1, "and the Note's Undo brings the joint back");

        // Joints off: the slot goes, and nothing of it takes the pointer.
        const QRect last = jointsSlot->geometry();
        jointsAction->trigger();
        pump(400);
        check(!jointsSlot->isVisible() && !js(page, "!!document.querySelector('.joints-card')").toBool(),
              "Joints off takes the card away");
        waitFor([&] { return !chrome->hitRegion().contains(last.center()); }, 2000);
        check(!chrome->hitRegion().contains(last.center()), "and where it stood, a press reaches the viewport (Review Focus 5)");
        window.openFurniture(furnitureId);
        pump(300);
    }

    // --- the Items card, end to end (Phase 3h) ------------------------------------
    {
        check(window.openFurniture(cabinetId), "the cabinet opens");
        pump(300);
        const DocumentModel& doc = window.document();
        auto idOf = [&](const char* name) {
            for (const DocumentModel::Solid& s : doc.solids())
                if (s.name == name) return s.id;
            return 0;
        };
        const int sideL = idOf("Side left"), sideR = idOf("Side right"), top = idOf("Top");
        const int shelf = idOf("Shelf"), back = idOf("Back");
        const int carcass = doc.groupOf(sideL);
        check(sideL && sideR && top && shelf && back && carcass > 0, "with its five boards and the Carcass folder");
        QAction* itemsAction = actionNamed(&window, QStringLiteral("Items"));
        if (itemsAction && !itemsAction->isChecked()) itemsAction->trigger();
        QWidget* itemsSlot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_items"));
        ItemsModel* model = window.itemsModel();
        check(itemsSlot && itemsSlot->isVisible() && model,
              "Items shows the card's slot");
        if (!itemsSlot || !model || !itemsAction) return 1;
        // Helpers: a press, a move and a release where the page's own row is.
        js(page, "window.__ic = (id) => { const r = document.querySelector(`.items-card [data-row=\"${id}\"]`); "
                 "if (!r) return null; const b = r.getBoundingClientRect(); return [b.left + b.width / 3, b.top + b.height / 2]; }; "
                 "window.__ip = (id, o = {}) => { const r = document.querySelector(`.items-card [data-row=\"${id}\"]`); "
                 "const [x, y] = window.__ic(id); r.dispatchEvent(new MouseEvent('mousedown', {bubbles: true, button: 0, "
                 "buttons: 1, clientX: x, clientY: y, ctrlKey: !!o.ctrl, shiftKey: !!o.shift, altKey: !!o.alt})); return [x, y]; }; "
                 "window.__im = (x, y) => window.dispatchEvent(new MouseEvent('mousemove', {bubbles: true, buttons: 1, clientX: x, clientY: y})); "
                 "window.__iu = (x, y) => window.dispatchEvent(new MouseEvent('mouseup', {bubbles: true, button: 0, clientX: x, clientY: y})); "
                 "window.__iclick = (id, o) => { const [x, y] = window.__ip(id, o); window.__iu(x, y); }; "
                 "window.__ikey = (key, o = {}) => document.querySelector('.items-card .list').dispatchEvent("
                 "new KeyboardEvent('keydown', {key, bubbles: true, shiftKey: !!o.shift, altKey: !!o.alt, ctrlKey: !!o.ctrl})); "
                 "window.__inames = () => [...document.querySelectorAll('.items-card [data-row]')].map((r) => "
                 "r.querySelector('.name, input.rename') ? (r.querySelector('.name') || {}).textContent : '').join('|'); true");
        const auto pageNames = [&] { return jsString(page, "window.__inames()"); };
        const auto modelNames = [&] {
            QStringList names;
            for (const QVariant& v : model->rows()) names << v.toMap().value(QStringLiteral("name")).toString();
            return names.join(QLatin1Char('|'));
        };
        const auto selection = [&] {
            std::vector<int> ids = window.view()->selectedSolidIds();
            std::sort(ids.begin(), ids.end());
            return ids;
        };
        const auto sorted = [](std::vector<int> v) {
            std::sort(v.begin(), v.end());
            return v;
        };
        waitFor([&] { return !modelNames().isEmpty() && pageNames() == modelNames(); }, 3000);
        check(pageNames() == modelNames() && !pageNames().contains(QStringLiteral("Side left")),
              "the card draws ItemsModel's rows, Carcass folded - " + pageNames());
        auto settled = [&] {
            const int h = js(page, "window.__itemsHeight || 0").toInt();
            const QRect drawn = [&] {
                const QJsonObject o = QJsonDocument::fromJson(jsString(page,
                    "JSON.stringify((() => { const r = document.querySelector('.items-card').getBoundingClientRect(); "
                    "return {x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height)}; })())")
                    .toUtf8()).object();
                return QRect(o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(), o.value("h").toInt());
            }();
            return h > 0 && itemsSlot->height() == Theme::wholeDevicePixels(h) && drawn == itemsSlot->geometry();
        };
        check(waitStable(settled, 5000), "the card reports its height and covers its slot exactly");

        // The twisty opens the folder.
        js(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"] .twisty').click()").arg(carcass));
        waitFor([&] { return pageNames().contains(QStringLiteral("Side left")); }, 3000);
        check(pageNames().contains(QStringLiteral("Side left")) && model->isFolderOpen(carcass), "the twisty opens Carcass");

        // Clicks select on release: plain, Ctrl, Shift, a folder.
        js(page, QStringLiteral("window.__iclick(%1)").arg(shelf));
        waitFor([&] { return selection() == std::vector<int>{shelf}; }, 2000);
        check(selection() == std::vector<int>{shelf}, "a click on Shelf selects it in the viewport");
        js(page, QStringLiteral("window.__iclick(%1, {ctrl: true})").arg(back));
        waitFor([&] { return selection().size() == 2; }, 2000);
        check(selection() == sorted({shelf, back}), "Ctrl adds Back");
        js(page, QStringLiteral("window.__iclick(%1)").arg(sideL));
        pump(150);
        js(page, QStringLiteral("window.__iclick(%1, {shift: true})").arg(top));
        waitFor([&] { return selection().size() == 3; }, 2000);
        check(selection() == sorted({sideL, sideR, top}), "Shift takes the run of rows between");
        js(page, QStringLiteral("window.__iclick(%1)").arg(shelf));
        pump(150);
        js(page, QStringLiteral("window.__iclick(%1)").arg(carcass));
        waitFor([&] { return selection().size() == 3; }, 2000);
        check(selection() == sorted(doc.bodiesUnderGroup(carcass)), "a folder click selects every body in it");
        waitFor([&] { return js(page, QStringLiteral("!!document.querySelector('.items-card [data-row=\"%1\"].marked')").arg(carcass)).toBool(); }, 2000);
        check(js(page, QStringLiteral("!!document.querySelector('.items-card [data-row=\"%1\"].marked')").arg(carcass)).toBool(),
              "and the folder row wears the mark");

        // The eye hides a body in the viewport; a second click shows it again.
        js(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"] .eye').click()").arg(shelf));
        waitFor([&] { return !doc.isVisible(shelf); }, 2000);
        check(!doc.isVisible(shelf) && !window.view()->isSolidVisible(shelf), "the eye hides Shelf, document and viewport");
        js(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"] .eye').click()").arg(shelf));
        waitFor([&] { return doc.isVisible(shelf); }, 2000);
        check(doc.isVisible(shelf) && window.view()->isSolidVisible(shelf), "and shows it again");

        // Rename by double-click: one checkpoint, the name written.
        js(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"]').dispatchEvent("
                                "new MouseEvent('dblclick', {bubbles: true}))").arg(shelf));
        waitFor([&] { return js(page, "!!document.querySelector('.items-card input.rename')").toBool(); }, 2000);
        check(jsString(page, "(document.querySelector('.items-card input.rename') || {}).value") == QStringLiteral("Shelf") &&
                  chrome->holdsKeyboard(),
              "a double-click opens the field over the row, holding the keyboard");
        js(page, "(() => { const f = document.querySelector('.items-card input.rename'); f.value = 'Top shelf'; "
                 "f.dispatchEvent(new Event('input', {bubbles: true})); "
                 "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()");
        waitFor([&] { return doc.nameOf(shelf) == "Top shelf"; }, 2000);
        check(doc.nameOf(shelf) == "Top shelf" && !chrome->holdsKeyboard(), "Enter renames it and gives the keyboard back");
        // F2: the Rename action opens the field over the selected body's row.
        window.view()->setSelectedSolids({back});
        pump(150);
        if (QAction* rename = actionNamed(&window, QStringLiteral("Rename"))) rename->trigger();
        waitFor([&] { return js(page, "!!document.querySelector('.items-card input.rename')").toBool(); }, 2000);
        check(jsString(page, "(document.querySelector('.items-card input.rename') || {}).value") == QStringLiteral("Back"),
              "Rename (F2) opens the field over the selected body's row");
        js(page, "document.querySelector('.items-card input.rename').dispatchEvent("
                 "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return !js(page, "!!document.querySelector('.items-card input.rename')").toBool(); }, 2000);
        check(doc.nameOf(back) == "Back", "and Escape leaves the name alone");

        // The row menu's Move into: one checkpoint, a Note.
        js(page, QStringLiteral("(() => { const [x, y] = window.__ic(%1); document.querySelector('.items-card [data-row=\"%1\"]')"
                                ".dispatchEvent(new MouseEvent('contextmenu', {bubbles: true, clientX: x, clientY: y})); })()").arg(back));
        waitFor([&] { return js(page, "!!document.querySelector('.menu [data-entry=\"into\"]')").toBool(); }, 2000);
        check(js(page, "!!document.querySelector('.menu [data-entry=\"into\"]')").toBool(), "a right press opens the row menu");
        pump(300);
#ifdef _WIN32
        capture(&window, outDir + "/items-menu.png");
#endif
        js(page, "document.querySelector('.menu [data-entry=\"into\"]').click()");
        pump(150);
        js(page, QStringLiteral("document.querySelector('.menu [data-entry=\"into:%1\"]').click()").arg(carcass));
        waitFor([&] { return doc.groupOf(back) == carcass; }, 2000);
        check(doc.groupOf(back) == carcass, "Move into -> Carcass files Back there");
        waitFor([&] { return toastText(page).contains(QStringLiteral("Moved")); }, 2000);
        check(toastText(page).contains(QStringLiteral("Moved")), "with a Note - \"" + toastText(page) + "\"");
        actionNamed(&window, QStringLiteral("Undo"))->trigger();
        waitFor([&] { return doc.groupOf(back) == 0; }, 2000);
        check(doc.groupOf(back) == 0, "one Undo takes it back");

        // The filter: rows narrow to matches, the match highlighted, Escape clears.
        js(page, "(() => { const f = document.querySelector('.items-card input.filter'); "
                 "f.dispatchEvent(new MouseEvent('mousedown', {bubbles: true})); f.focus(); f.value = 'side'; "
                 "f.dispatchEvent(new Event('input', {bubbles: true})); })()");
        waitFor([&] { return pageNames() == QStringLiteral("Carcass|Side left|Side right"); }, 2000);
        check(pageNames() == QStringLiteral("Carcass|Side left|Side right"), "the filter narrows the rows - " + pageNames());
        check(jsString(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"] .hit').textContent").arg(sideL)) ==
                  QStringLiteral("Side"),
              "and highlights the match");
        js(page, "document.querySelector('.items-card input.filter').dispatchEvent("
               "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return pageNames().contains(QStringLiteral("Back")); }, 2000);
        check(model->filterText().isEmpty() && pageNames().contains(QStringLiteral("Back")), "Escape clears it");
        js(page, "document.querySelector('.items-card input.filter').blur()");
        pump(100);

        // Reveal: a pick in the viewport opens the folder its row is in.
        waitFor(settled, 2000);
        const int openHeight = itemsSlot->height();
        js(page, QStringLiteral("document.querySelector('.items-card [data-row=\"%1\"] .twisty').click()").arg(carcass));
        waitFor([&] { return !pageNames().split(QLatin1Char('|')).contains(QStringLiteral("Top")); }, 2000);
        const bool held4 = waitStable([&] { return itemsSlot->height() < openHeight && settled(); }, 5000);
        check(held4,
              QStringLiteral("folding a folder shrinks the card, which still covers its slot (%1 -> %2)")
                  .arg(openHeight).arg(itemsSlot->height()));
        window.view()->setSelectedSolids({});
        pump(100);
        window.view()->setSelectedSolids({top});
        waitFor([&] { return pageNames().split(QLatin1Char('|')).contains(QStringLiteral("Top")); }, 2000);
        check(model->isFolderOpen(carcass) && pageNames().split(QLatin1Char('|')).contains(QStringLiteral("Top")),
              QStringLiteral("a viewport pick opens the folder its row is in (open %1, rows %2)")
                  .arg(model->isFolderOpen(carcass)).arg(pageNames()));

        // The list's keys (Review Focus 1): held from a press, Up claimed and
        // Ctrl+Z not, Down walking the rows, Alt+Up moving one with a checkpoint.
        js(page, QStringLiteral("window.__iclick(%1)").arg(shelf));
        waitFor([&] { return chrome->holdsListKeys() && selection() == std::vector<int>{shelf}; }, 2000);
        QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
        const auto claimed = [&](int key, Qt::KeyboardModifiers mods) {
            QKeyEvent over(QEvent::ShortcutOverride, key, mods);
            over.ignore();
            QCoreApplication::sendEvent(proxy, &over);
            return over.isAccepted();
        };
        check(chrome->holdsListKeys() && claimed(Qt::Key_Up, Qt::NoModifier) && claimed(Qt::Key_F2, Qt::NoModifier) &&
                  !claimed(Qt::Key_Z, Qt::ControlModifier) && !claimed(Qt::Key_E, Qt::NoModifier),
              "a click on a row holds the list's keys - Up and F2 claimed, Ctrl+Z and E not");
        js(page, "window.__ikey('ArrowDown')");
        waitFor([&] { return selection() == std::vector<int>{back}; }, 2000);
        check(selection() == std::vector<int>{back}, "Down selects the next row");
        js(page, "window.__ikey('ArrowUp')");
        waitFor([&] { return selection() == std::vector<int>{shelf}; }, 2000);
        const std::vector<int> rootBefore = doc.orderedChildren(0);
        const bool couldUndo = doc.canUndo();
        js(page, "window.__ikey('ArrowUp', {alt: true})");
        waitFor([&] { return doc.orderedChildren(0) != rootBefore; }, 2000);
        check(doc.orderedChildren(0) != rootBefore && doc.canUndo(), "Alt+Up moves Shelf up one place");
        (void)couldUndo;
        actionNamed(&window, QStringLiteral("Undo"))->trigger();
        waitFor([&] { return doc.orderedChildren(0) == rootBefore; }, 2000);
        check(doc.orderedChildren(0) == rootBefore, "and one Undo puts it back");

        // A drag into a folder: one checkpoint, the Note's words.
        // The page has the order back before the hand aims at it: the Undo
        // reaches the document first and the page a channel message later.
        waitFor([&] { return pageNames() == modelNames(); }, 2000);
        js(page, QStringLiteral("(() => { const [x, y] = window.__ip(%1); const [tx, ty] = window.__ic(%2); "
                                "for (let i = 1; i <= 5; i++) window.__im(x + (tx - x) * i / 5, y + (ty - y) * i / 5); "
                                "window.__dropAt = [tx, ty]; })()").arg(back).arg(carcass));
        waitFor([&] { return model->dragActive() && !model->dragStatusText().isEmpty(); }, 2000);
        check(model->dragActive() && model->dragStatusText().contains(QStringLiteral("Carcass")),
              "dragging Back over Carcass says where it will land - \"" + model->dragStatusText() + "\"");
        waitFor([&] { return js(page, "!!document.querySelector('.items-card .indicator.ring') && !!document.querySelector('.ghost')").toBool(); }, 2000);
        check(js(page, "!!document.querySelector('.items-card .indicator.ring') && !!document.querySelector('.ghost')").toBool(),
              QStringLiteral("with the ring around the folder row and the ghost under the hand (ring %1, line %2, ghost %3)")
                  .arg(js(page, "!!document.querySelector('.items-card .indicator.ring')").toBool())
                  .arg(js(page, "!!document.querySelector('.items-card .indicator')").toBool())
                  .arg(js(page, "!!document.querySelector('.ghost')").toBool())
              );
        {
            // The ghost is PAINTED: the view's mask clips drawing as well as
            // presses, so it must lie inside the page's region.
            const QJsonObject g = QJsonDocument::fromJson(jsString(page,
                "JSON.stringify((() => { const r = document.querySelector('.ghost').getBoundingClientRect(); "
                "return {x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2)}; })())").toUtf8()).object();
            const QPoint ghost(g.value("x").toInt(), g.value("y").toInt());
            waitFor([&] { return chrome->hitRegion().contains(ghost); }, 2000);
            check(chrome->hitRegion().contains(ghost), "and the ghost lies inside what the page paints");
        }
        pump(300);
#ifdef _WIN32
        capture(&window, outDir + "/items-drag.png");
#endif
        js(page, "window.__iu(...window.__dropAt)");
        waitFor([&] { return doc.groupOf(back) == carcass; }, 2000);
        check(doc.groupOf(back) == carcass && !model->dragActive(), "the release files Back in Carcass");
        waitFor([&] { return toastText(page).contains(QStringLiteral("Moved")); }, 2000);
        check(toastText(page).contains(QStringLiteral("Carcass")), "and the Note says so - \"" + toastText(page) + "\"");
        actionNamed(&window, QStringLiteral("Undo"))->trigger();
        waitFor([&] { return doc.groupOf(back) == 0; }, 2000);
        // Escape mid-drag moves nothing (Review Focus 3).
        waitFor([&] { return pageNames() == modelNames(); }, 2000);
        const int revision = doc.revision();
        js(page, QStringLiteral("(() => { const [x, y] = window.__ip(%1); const [tx, ty] = window.__ic(%2); "
                                "for (let i = 1; i <= 5; i++) window.__im(x + (tx - x) * i / 5, y + (ty - y) * i / 5); "
                                "window.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true})); "
                                "window.__iu(tx, ty); })()").arg(back).arg(carcass));
        pump(300);
        check(doc.groupOf(back) == 0 && doc.revision() == revision && !model->dragActive() &&
                  !js(page, "!!document.querySelector('.ghost')").toBool() && model->dragStatusText().isEmpty(),
              "Escape mid-drag moves nothing and leaves no ghost and no drag line");

        // Every theme, with a row selected.
        window.view()->setSelectedSolids({sideR});
        for (const Themes::Info& theme : Themes::all()) {
            Themes::select(theme.id);
            waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == theme.id; }, 3000);
            pump(400);
#ifdef _WIN32
            capture(&window, outDir + "/chrome-items-" + theme.id + ".png");
#endif
        }
        Themes::select(Themes::graphiteId());
        pump(300);

        // The page's words, swept.
        {
            QString text = jsString(page, "document.querySelector('.items-card').innerText");
            for (const char* name : {"Carcass", "Side left", "Side right", "Top shelf", "Top", "Shelf", "Back"})
                text.replace(QString::fromUtf8(name), QStringLiteral(" "));
            check(bannedIn(text).isEmpty(), "no word the card shows is banned - found: " + bannedIn(text).join(", "));
        }

        // Items off: the slot goes, and nothing of it takes the pointer.
        const QRect last = itemsSlot->geometry();
        itemsAction->trigger();
        pump(400);
        check(!itemsSlot->isVisible() && !js(page, "!!document.querySelector('.items-card')").toBool(),
              "Items off takes the card away");
        waitFor([&] { return !chrome->hitRegion().contains(last.center()); }, 2000);
        check(!chrome->hitRegion().contains(last.center()) && !chrome->holdsListKeys(),
              "and where it stood a press reaches the viewport, and the list gave its keys back");
        itemsAction->trigger();
        pump(300);
        window.openFurniture(furnitureId);
        pump(300);
    }

    // --- the rail, the view cluster, the pill and the menus (Phase 3i) -------------
    {
        auto slotNamed = [&](const char* name) {
            return window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_") + QLatin1String(name));
        };
        HtmlCardSlot* pillSlot = slotNamed("pill");
        HtmlCardSlot* railSlot = slotNamed("rail");
        HtmlCardSlot* viewSlot = slotNamed("view");
        check(pillSlot && railSlot && viewSlot, "the pill, the rail and the view cluster each have a slot");
        if (pillSlot && railSlot && viewSlot) {
            check(pillSlot->isVisible() && railSlot->isVisible() && viewSlot->isVisible(),
                  "the pill, the rail and the view cluster stand in their places");
            check(railSlot->geometry().top() > pillSlot->geometry().bottom() &&
                      railSlot->geometry().left() == pillSlot->geometry().left(),
                  "the rail stands under the pill, in one column");
            ShellBridge* shell = window.shellBridge();
            check(shell != nullptr, "the page's `shell` object exists");

            // R5: shortcuts fire with the Qt menu bar hidden. Through Qt's own
            // shortcut map (QTest's key click runs it), with Qt's record of the
            // active window moved for the length of the press - never the OS's.
            auto withActive = [&](const std::function<void()>& run) {
                QT_WARNING_PUSH
                QT_WARNING_DISABLE_DEPRECATED
                QWidget* before = QApplication::activeWindow();
                QApplication::setActiveWindow(&window);
                run();
                QApplication::setActiveWindow(before);
                QT_WARNING_POP
            };
            const int bodiesNow = static_cast<int>(window.document().solids().size());
            window.view()->setSelectedSolids({window.document().solids().front().id});
            pump(100);
            if (del) del->trigger();
            pump(200);
            check(static_cast<int>(window.document().solids().size()) == bodiesNow - 1, "a body was deleted to undo");
            withActive([&] { QTest::keyClick(window.view(), Qt::Key_Z, Qt::ControlModifier); });
            pump(200);
            check(static_cast<int>(window.document().solids().size()) == bodiesNow,
                  "Ctrl+Z fires through the shortcut map while the HTML pill stands in for the Qt one (R5)");

#ifdef _WIN32
            // R10: the pill's ground drags the window; the slot's corner is ground.
            const QPoint ground = window.view()->mapTo(&window, pillSlot->geometry().topLeft() + QPoint(6, pillSlot->height() / 2));
            const LRESULT groundHit = nativeHit(&window, ground);
            check(groundHit == HTCAPTION, QStringLiteral("the pill's ground answers Caption (R10) - got %1 at %2,%3")
                                              .arg(groundHit).arg(ground.x()).arg(ground.y()));
            const QPoint viewport = window.view()->mapTo(&window, window.view()->rect().center());
            capture(&window, outDir + "/shell.png");
            const LRESULT viewportHit = nativeHit(&window, viewport);
            check(viewportHit == HTCLIENT, QStringLiteral("the viewport answers Client - got %1").arg(viewportHit));
#endif

            // --- the page draws them ---
            const auto chipCount = [&](const char* cluster) {
                return js(page, QStringLiteral("document.querySelectorAll('[data-cluster=\"%1\"] [data-chip]').length")
                                    .arg(QLatin1String(cluster))).toInt();
            };
            check(waitFor([&] { return chipCount("rail") == 10 && chipCount("view") == 3; }, 5000),
                  QStringLiteral("the page draws the rail's 10 chips and the view cluster's 3 (got %1 and %2)")
                      .arg(chipCount("rail")).arg(chipCount("view")));
#ifdef _WIN32
            {
                // THE PAGE IS SEEN, not only present in its DOM (T-083): a
                // Chromium switch once left every page invisible while every
                // DOM check here stayed green. The window composited with the
                // page, then with the page's view hidden: where the pill and the
                // rail stand, the two must differ. Nothing else moves between
                // the two captures, and Graphite's pill fill sits only a few
                // levels from the viewport behind it, so a small step counts -
                // and only its words and border move (about 12% of it), where
                // the rail's chips move about half of it. A page that is not
                // drawn moves neither.
                pump(300);
                const QImage withPage = captureImage(&window);
                page->hide();
                pump(300);
                const QImage without = captureImage(&window);
                page->show();
                pump(300);
                withPage.save(outDir + "/seen-with-page.png");
                without.save(outDir + "/seen-without-page.png");
                const double dpr = window.devicePixelRatioF();
                auto changed = [&](const QRect& logical) {
                    const QRect r(qRound(logical.x() * dpr), qRound(logical.y() * dpr), qRound(logical.width() * dpr),
                                  qRound(logical.height() * dpr));
                    int moved = 0, all = 0;
                    for (int y = r.top(); y <= r.bottom(); ++y)
                        for (int x = r.left(); x <= r.right(); ++x) {
                            if (!withPage.rect().contains(x, y) || !without.rect().contains(x, y)) continue;
                            const QColor p1 = withPage.pixelColor(x, y), p2 = without.pixelColor(x, y);
                            ++all;
                            if (std::abs(p1.red() - p2.red()) + std::abs(p1.green() - p2.green()) +
                                    std::abs(p1.blue() - p2.blue()) > 9)
                                ++moved;
                        }
                    return all ? double(moved) / all : 0.0;
                };
                const QRect pillAt(window.view()->mapTo(&window, pillSlot->geometry().topLeft()), pillSlot->size());
                const QRect railAt(window.view()->mapTo(&window, railSlot->geometry().topLeft()), railSlot->size());
                const double pillMoved = changed(pillAt), railMoved = changed(railAt);
                check(pillMoved > 0.05 && railMoved > 0.3,
                      QStringLiteral("the composited window SHOWS the page's pill and rail (%1% and %2% of their pixels "
                                     "change when the page is hidden)")
                          .arg(pillMoved * 100, 0, 'f', 1).arg(railMoved * 100, 0, 'f', 1));
            }
#endif
            QAction* itemsAction = actionNamed(&window, QStringLiteral("Items"));
            QAction* undoAction = actionNamed(&window, QStringLiteral("Undo"));
            QAction* orthoAction = actionNamed(&window, QStringLiteral("Orthographic"));
            QAction* fitAction = actionNamed(&window, QStringLiteral("Fit All"));
            const auto chipAttr = [&](const QString& id, const char* attr) {
                return jsString(page, QStringLiteral("(document.querySelector('[data-chip=\"%1\"]') || {getAttribute: () => ''}).getAttribute('%2') || ''")
                                          .arg(id, QLatin1String(attr)));
            };
            const auto idOf = [](QAction* a) { return a ? ActionRegistry::idFor(a->text()) : QString(); };
            check(itemsAction && chipAttr(idOf(itemsAction), "aria-label") == QStringLiteral("Items"),
                  "each chip is named by its action - " + chipAttr(idOf(itemsAction), "aria-label"));
            check(undoAction && chipAttr(idOf(undoAction), "title") == ToolChip::iconOnlyTip(undoAction),
                  "and carries the Qt chip's own composed tooltip");
            {
                const bool wasOpen = itemsAction && itemsAction->isChecked();
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(itemsAction)));
                check(waitFor([&] { return itemsAction->isChecked() != wasOpen; }, 3000),
                      "a click on the Items chip toggles the Items action (Review Focus 5)");
                check(waitFor([&] { return chipAttr(idOf(itemsAction), "aria-pressed") ==
                                           (itemsAction->isChecked() ? QStringLiteral("true") : QStringLiteral("false")); }, 3000),
                      "and the chip shows the action's new state");
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(itemsAction)));
                waitFor([&] { return itemsAction->isChecked() == wasOpen; }, 3000);
            }
            {
                // Undo follows the undo stack, live.
                window.view()->setSelectedSolids({window.document().solids().front().id});
                pump(100);
                if (del) del->trigger();
                check(waitFor([&] { return chipAttr(idOf(undoAction), "aria-disabled") == QStringLiteral("false"); }, 3000),
                      "Undo's chip enables the moment there is something to undo");
                const int before = static_cast<int>(window.document().solids().size());
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(undoAction)));
                check(waitFor([&] { return static_cast<int>(window.document().solids().size()) == before + 1; }, 3000),
                      "and a click on it undoes");
            }
            {
                // A disabled chip runs nothing (Review Focus 3).
                window.view()->setSelectedSolids({});
                pump(100);
                QAction* extrude = actionNamed(&window, QStringLiteral("Union"));
                int extrudes = 0;
                QMetaObject::Connection c;
                if (extrude) c = QObject::connect(extrude, &QAction::triggered, [&] { ++extrudes; });
                check(extrude && !extrude->isEnabled() && chipAttr(idOf(extrude), "aria-disabled") == QStringLiteral("true"),
                      "Union is disabled with nothing selected, and its chip says so");
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(extrude)));
                check(!window.shellBridge()->trigger(idOf(extrude)), "and the bridge refuses it too");
                pump(200);
                check(extrudes == 0, "a click on a disabled chip runs nothing");
                QObject::disconnect(c);
            }
            check(!window.shellBridge()->trigger(QStringLiteral("delete-everything")) &&
                      !window.shellBridge()->trigger(QStringLiteral("export-step")),
                  "the bridge triggers only its own chips' actions");
            {
                const bool ortho = orthoAction && orthoAction->isChecked();
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(orthoAction)));
                check(waitFor([&] { return orthoAction->isChecked() != ortho; }, 3000),
                      "the view cluster's projection chip flips the projection");
                js(page, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(idOf(orthoAction)));
                waitFor([&] { return orthoAction->isChecked() == ortho; }, 3000);
            }

            // --- the pill and its menus ---
            MenuModel* menu = shell->menu();
            check(waitFor([&] { return js(page, "document.querySelectorAll('[data-pill] [data-title]').length").toInt() == menu->menuCount(); }, 3000),
                  QStringLiteral("the pill draws every menu title (%1)").arg(menu->menuCount()));
            {
                const auto pageWidth = [&] { return js(page, "Math.ceil(document.querySelector('[data-pill]').getBoundingClientRect().width)").toInt(); };
                check(waitFor([&] { return pillSlot->width() == Theme::wholeDevicePixels(QSize(pageWidth(), 1)).width(); }, 3000),
                      QStringLiteral("the pill's slot takes the width the page measured (slot %1, page %2) (R9)").arg(pillSlot->width()).arg(pageWidth()));
            }
            js(page, "document.querySelector('[data-pill] [data-title=\"0\"]').dispatchEvent(new MouseEvent('mousedown', {bubbles: true, button: 0}))");
            check(waitFor([&] { return menu->isOpen() && menu->openIndex() == 0; }, 3000), "a press on File opens File");
            check(waitFor([&] { return js(page, "!!document.querySelector('[data-menu-level=\"0\"]')").toBool(); }, 3000),
                  "and the page draws it");
            check(chrome->menuOpen() && chrome->hitRegion().contains(window.view()->rect().center()),
                  "an open menu takes the whole viewport's pointer (R7)");
            {
                QWidget* at = window.view()->childAt(window.view()->rect().center());
                check(at && (at == page || page->isAncestorOf(at)), "a press on the viewport lands on the page's outside layer");
            }
#ifdef _WIN32
            pump(300);
            {
                const QPoint groundNow = window.view()->mapTo(&window, pillSlot->geometry().topLeft() + QPoint(6, pillSlot->height() / 2));
                check(nativeHit(&window, groundNow) == HTCLIENT, "while a menu is open the pill's ground does not drag (R10)");
            }
            capture(&window, outDir + "/shell-menu-file.png");
#endif
            {
                // Escape closes it: keys go to the menu through its filter.
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &esc);
                check(!menu->isOpen(), "Escape closes the menu");
                check(waitFor([&] { return !js(page, "!!document.querySelector('[data-menu-level]')").toBool(); }, 3000) &&
                          !chrome->menuOpen(),
                      "the page draws no menu and gives the viewport its pointer back");
            }
#ifdef _WIN32
            {
                // A title does not drag the window; the page reported where they stand.
                waitFor([&] { return !shell->titleRects().isEmpty(); }, 2000);
                const QRect title = shell->titleRects().value(0);
                check(!title.isEmpty() && nativeHit(&window, window.view()->mapTo(&window, title.center())) == HTCLIENT,
                      "a title answers Client, not Caption (R10)");
            }
#endif
            {
                // Alt+V opens View with its first live entry current, Down
                // moves, Enter runs it - through Qt's own shortcut map.
                int fits = 0;
                const QMetaObject::Connection c = QObject::connect(fitAction, &QAction::triggered, [&] { ++fits; });
                withActive([&] { QTest::keyClick(window.view(), Qt::Key_V, Qt::AltModifier); });
                pump(100);
                const int viewIndex = menu->menuForMnemonic(QLatin1Char('v'));
                check(menu->isOpen() && menu->openIndex() == viewIndex, "Alt+V opens View (R6)");
                const QVariantList entries = menu->open().value(QStringLiteral("levels")).toList().value(0).toMap()
                                                 .value(QStringLiteral("entries")).toList();
                const int current = menu->open().value(QStringLiteral("levels")).toList().value(0).toMap()
                                        .value(QStringLiteral("current")).toInt();
                check(entries.value(current).toMap().value(QStringLiteral("text")).toString() == fitAction->text().remove(QLatin1Char('&')),
                      "on its first live entry, Fit All");
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &down);
                QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &up);
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &enter);
                check(!menu->isOpen(), "Enter closes the menu");
                pump(200);
                check(fits == 1, QStringLiteral("and runs Fit All once (%1)").arg(fits));
                QObject::disconnect(c);
                // A menu standing open swallows a window shortcut.
                const int bodiesBefore = static_cast<int>(window.document().solids().size());
                window.view()->setSelectedSolids({window.document().solids().front().id});
                pump(100);
                withActive([&] { QTest::keyClick(window.view(), Qt::Key_V, Qt::AltModifier); });
                pump(100);
                withActive([&] { QTest::keyClick(window.view(), Qt::Key_Delete, Qt::NoModifier); });
                pump(100);
                check(static_cast<int>(window.document().solids().size()) == bodiesBefore,
                      "Delete with a menu open deletes nothing - the menu holds every key");
                menu->close();
                pump(100);
                // Not vacuous: the same press with no menu open deletes.
                window.view()->setSelectedSolids({window.document().solids().front().id});
                pump(100);
                withActive([&] { QTest::keyClick(window.view(), Qt::Key_Delete, Qt::NoModifier); });
                pump(100);
                check(static_cast<int>(window.document().solids().size()) == bodiesBefore - 1,
                      "and the same Delete with no menu open deletes the body");
                if (undoAction) undoAction->trigger();
                pump(200);
            }
            {
                // View -> Theme -> a theme, by the model's own gestures (the
                // page's clicks land on these), and every chip re-inks.
                const QString inkBefore = jsString(page, QStringLiteral("getComputedStyle(document.querySelector('[data-chip=\"%1\"]')).color").arg(idOf(fitAction)));
                menu->openMenu(menu->menuForMnemonic(QLatin1Char('v')), false);
                pump(100);
                auto indexOf = [&](int level, const QString& text) {
                    const QVariantList levels = menu->open().value(QStringLiteral("levels")).toList();
                    const QVariantList e = levels.value(level).toMap().value(QStringLiteral("entries")).toList();
                    for (int i = 0; i < e.size(); ++i)
                        if (e.at(i).toMap().value(QStringLiteral("text")).toString() == text) return i;
                    return -1;
                };
                const int themeEntry = indexOf(0, QStringLiteral("Theme"));
                menu->hover(0, themeEntry);
                pump(100);
                check(menu->open().value(QStringLiteral("levels")).toList().size() == 2, "hovering Theme opens its submenu");
                check(waitFor([&] { return js(page, "!!document.querySelector('[data-menu-level=\"1\"]')").toBool(); }, 3000),
                      "and the page draws it beside");
#ifdef _WIN32
                capture(&window, outDir + "/shell-menu-theme.png");
#endif
                const int nightshade = indexOf(1, QStringLiteral("Nightshade Glass"));
                js(page, QStringLiteral("document.querySelector('[data-menu-level=\"1\"] [data-entry=\"%1\"]').click()").arg(nightshade));
                check(waitFor([&] { return Themes::currentId() == QStringLiteral("nightshade-glass"); }, 3000),
                      "a click on Nightshade Glass puts it on - " + Themes::currentId());
                check(!menu->isOpen(), "and the menu closed first");
                check(waitFor([&] { return jsString(page, QStringLiteral("getComputedStyle(document.querySelector('[data-chip=\"%1\"]')).color").arg(idOf(fitAction))) != inkBefore; }, 3000),
                      "every glyph re-inks in the theme's chip ink (Review Focus 5)");
#ifdef _WIN32
                pump(300);
                capture(&window, outDir + "/shell-nightshade.png");
#endif
                {
                    // The rail and the view cluster take the theme's chip size
                    // (review I1): the slot follows what the page measured, and
                    // every chip stands inside its card.
                    const Themes::Chip& c = Themes::shell().chip;
                    const int chipPx = c.on ? c.size : 34;
                    const int wanted = Theme::wholeDevicePixels(QSize(chipPx + 16, 1)).width();
                    check(waitFor([&] { return railSlot->width() == wanted && viewSlot->width() == wanted; }, 3000),
                          QStringLiteral("after a theme switch the rail and the view cluster are the new chips' width (%1 and %2, wanted %3)")
                              .arg(railSlot->width()).arg(viewSlot->width()).arg(wanted));
                    const bool inside = js(page, "(() => { for (const c of document.querySelectorAll('[data-cluster]')) { "
                                                 "const r = c.getBoundingClientRect(); "
                                                 "for (const chip of c.querySelectorAll('[data-chip]')) { const b = chip.getBoundingClientRect(); "
                                                 "if (b.left < r.left || b.right > r.right + 0.5 || b.top < r.top || b.bottom > r.bottom + 0.5 || "
                                                 "Math.abs(b.width - b.height) > 0.5) return false; } } return true; })()").toBool();
                    check(inside, "and every chip stands square inside its card");
                    const QRect addShape = shell->chipRect(QStringLiteral("add-shape"));
                    const int domTop = js(page, "Math.round(document.querySelector('[data-chip=\"add-shape\"]').getBoundingClientRect().top)").toInt();
                    check(std::abs(addShape.top() - domTop) <= 1,
                          QStringLiteral("and the chips' rectangles follow (Add shape at %1, drawn at %2) (review I2)").arg(addShape.top()).arg(domTop));
                }
                for (const QString& id : {QStringLiteral("violet-glass-float"), QStringLiteral("aurora-mesh-gradient"),
                                          QStringLiteral("ultraviolet-grid")}) {
                    Themes::select(id);
                    pump(400);
                    menu->openMenu(0, true);
                    pump(300);
#ifdef _WIN32
                    capture(&window, outDir + "/shell-" + id + ".png");
#endif
                    menu->close();
                }
                Themes::select(Themes::graphiteId());
                pump(300);
            }
            // Render mode taking them away is checked in the render page's
            // block below, where render mode is entered once for the run.
            {
                // WITH ANIMATIONS ON - the shipping default (review C1): the pill
                // and the rail fly in under a transform, and what the page
                // reports must be where they LAND - the mask, and the titles
                // the caption hit test reads.
                window.view()->setAnimationsEnabled(true);
                chrome->bridge()->setAnimate(true);
                pillSlot->hide();
                railSlot->hide();
                pump(300);
                pillSlot->show();
                railSlot->show();
                pump(900);
                check(chrome->hitRegion().contains(pillSlot->geometry().center()) &&
                          chrome->hitRegion().contains(railSlot->geometry().center()),
                      "after the fly-in the page's mask holds the pill and the rail where they landed");
                const QRect title = shell->titleRects().value(0);
                const int domLeft = js(page, "Math.floor(document.querySelector('[data-pill] [data-title=\"0\"]').getBoundingClientRect().left)").toInt();
                const int domTop = js(page, "Math.floor(document.querySelector('[data-pill] [data-title=\"0\"]').getBoundingClientRect().top)").toInt();
                check(!title.isEmpty() && std::abs(title.left() - domLeft) <= 1 && std::abs(title.top() - domTop) <= 1,
                      QStringLiteral("and File's rectangle is where it is drawn (%1,%2 against %3,%4)")
                          .arg(title.left()).arg(title.top()).arg(domLeft).arg(domTop));
#ifdef _WIN32
                check(!title.isEmpty() && nativeHit(&window, window.view()->mapTo(&window, title.center())) == HTCLIENT,
                      "so a press on File opens the menu rather than dragging the window");
#endif
                window.view()->setAnimationsEnabled(false);
                chrome->bridge()->setAnimate(false);
                pump(100);
            }
        }
    }

    // --- the gesture chips (Phase 4a) ----------------------------------------------------
    {
        check(window.openFurniture(furnitureId), "chips: the bench opens again");
        pump(300);
        // Where the page drew a chip, in the viewport's coordinates (the page
        // stands at the viewport's origin), and whether it is drawn at all.
        const auto pageChip = [&](const QString& name) {
            return js(page, QStringLiteral(
                "(() => { const c = document.querySelector('[data-gesture=\"%1\"]'); if (!c) return null; "
                "return {x: c.offsetLeft, y: c.offsetTop, w: c.offsetWidth, h: c.offsetHeight, "
                "shown: c.style.visibility !== 'hidden'}; })()").arg(name)).toMap();
        };
        const auto drawn = [&](const QString& name) { return pageChip(name).value(QStringLiteral("shown")).toBool(); };
        const auto standsOn = [&](const QString& name, const QWidget* qt) {
            const QVariantMap r = pageChip(name);
            const QRect g = qt->geometry();
            return std::abs(r.value("x").toInt() - g.x()) <= 1 && std::abs(r.value("y").toInt() - g.y()) <= 1 &&
                   std::abs(r.value("w").toInt() - g.width()) <= 1 && std::abs(r.value("h").toInt() - g.height()) <= 1;
        };
        const auto key = [&](QWidget* target, int k) {
            QKeyEvent over(QEvent::ShortcutOverride, k, Qt::NoModifier);
            over.ignore();
            QCoreApplication::sendEvent(target, &over);
            QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &press);
            pump(150);
        };

        // --- Boolean ----------------------------------------------------------------
        BooleanTool* booleanTool = window.booleanTool();
        QAction* subtract = actionNamed(&window, QStringLiteral("Subtract"));
        const auto& solids = window.document().solids();
        check(booleanTool && subtract && solids.size() >= 2, "chips: the editor has its boolean chip and Subtract");
        if (booleanTool && subtract && solids.size() >= 2) {
            const int first = solids[0].id;
            const int second = solids[1].id;
            window.view()->setSelectedSolids({first, second});
            pump(150);
            subtract->trigger();
            check(window.booleanActive(), "Subtract begins the boolean gesture");
            check(waitFor([&] { return drawn(QStringLiteral("boolean")); }, 3000),
                  "the page draws the boolean chip once it stands at the size the page measured (R4)");
            check(booleanTool->isVisible() && booleanTool->face()->drawnElsewhere() &&
                      booleanTool->keepToolBox()->isHidden(),
                  "the Qt chip stands, drawn elsewhere, its own checkbox hidden");
            check(standsOn(QStringLiteral("boolean"), booleanTool),
                  QStringLiteral("and the page's card stands exactly where the Qt chip does (%1,%2 %3x%4)")
                      .arg(booleanTool->x()).arg(booleanTool->y()).arg(booleanTool->width()).arg(booleanTool->height()));
            check(std::abs(booleanTool->x() + booleanTool->width() / 2 - window.view()->width() / 2) <= 2,
                  "at the bottom centre, placed by the Qt chip's own code with the page's width (R2)");
            check(jsString(page, "document.querySelector('[data-gesture=\"boolean\"] .text.title').textContent") ==
                      window.booleanOperationName(),
                  "the chip names the operation - " + window.booleanOperationName());
            // The page reports its hit rectangles a frame after it draws.
            // And nothing of Qt's stands over it - a hint balloon did, until
            // the page's view rose with the chip.
            QWidget* under = nullptr;
            const auto onPage = [&] { return under && (under == page || page->isAncestorOf(under)); };
            waitFor([&] {
                under = window.view()->childAt(booleanTool->geometry().center());
                return onPage();
            }, 2000);
            check(onPage(), QStringLiteral("a press on the chip reaches the page - not the Qt chip, a Qt card or the "
                                           "viewport (%1)").arg(under ? under->metaObject()->className() : "nothing"));
#ifdef _WIN32
            {
                // THE CHIP IS SEEN (T-083's rule): the window composited with
                // the page, then with its view hidden, must differ where the
                // chip stands - a DOM that says "drawn" is not a chip on screen.
                pump(300);
                const QImage withPage = captureImage(&window);
                withPage.save(outDir + "/chip-boolean.png");
                page->hide();
                pump(300);
                const QImage without = captureImage(&window);
                page->show();
                pump(300);
                const double dpr = window.devicePixelRatioF();
                const QRect logical(window.view()->mapTo(&window, booleanTool->pos()), booleanTool->size());
                const QRect r(qRound(logical.x() * dpr), qRound(logical.y() * dpr), qRound(logical.width() * dpr),
                              qRound(logical.height() * dpr));
                int moved = 0, all = 0;
                for (int y = r.top(); y <= r.bottom(); ++y)
                    for (int x = r.left(); x <= r.right(); ++x) {
                        if (!withPage.rect().contains(x, y) || !without.rect().contains(x, y)) continue;
                        const QColor p1 = withPage.pixelColor(x, y), p2 = without.pixelColor(x, y);
                        ++all;
                        if (std::abs(p1.red() - p2.red()) + std::abs(p1.green() - p2.green()) +
                                std::abs(p1.blue() - p2.blue()) > 9)
                            ++moved;
                    }
                const double share = all ? double(moved) / all : 0.0;
                check(share > 0.05, QStringLiteral("the composited window SHOWS the boolean chip (%1% of its pixels "
                                                   "change when the page is hidden)")
                                        .arg(share * 100, 0, 'f', 1));
            }
#endif
            const bool keepBefore = window.booleanKeepTool();
            js(page, "document.querySelector('[data-gesture=\"boolean\"] [data-part=\"keep\"]').click()");
            check(waitFor([&] { return window.booleanKeepTool() != keepBefore; }, 2000),
                  "the page's switch turns the gesture's own Keep the other bodies");
            check(waitFor([&] {
                      return jsString(page, "document.querySelector('[data-gesture=\"boolean\"] [data-part=\"keep\"]')"
                                            ".getAttribute('aria-pressed')") ==
                             (window.booleanKeepTool() ? QStringLiteral("true") : QStringLiteral("false"));
                  }, 2000),
                  "and the switch on the page follows it back");
            js(page, "document.querySelector('[data-gesture=\"boolean\"] [data-part=\"keep\"]').click()");
            waitFor([&] { return window.booleanKeepTool() == keepBefore; }, 2000);
            // Escape with the keyboard on the viewport: the Qt chip's claim.
            window.view()->setFocus();
            key(window.view(), Qt::Key_Escape);
            check(!window.booleanActive() && waitFor([&] { return pageChip(QStringLiteral("boolean")).isEmpty(); }, 2000),
                  "Escape on the viewport cancels, and the page's chip goes with the Qt one");
            const int revision = window.document().revision();
            window.view()->setSelectedSolids({first, second});
            pump(150);
            subtract->trigger();
            waitFor([&] { return drawn(QStringLiteral("boolean")); }, 3000);
            window.view()->setFocus();
            key(window.view(), Qt::Key_Return);
            check(!window.booleanActive() && window.document().revision() != revision,
                  "Enter on the viewport applies it");
            if (QAction* undo = actionNamed(&window, QStringLiteral("Undo"))) undo->trigger();
            pump(300);
        }

        // --- Slats ------------------------------------------------------------------
        SlatsTool* slatsTool = window.findChild<SlatsTool*>();
        QAction* slatsAction = actionNamed(&window, QStringLiteral("Slats..."));
        check(slatsTool && slatsAction, "chips: the editor has its slats chip and Slats...");
        // A 600 x 300 face of the slab, picked the way a hand picks it.
        const auto pickTop = [&] {
            const int slab = window.document().solids().front().id;
            const TopoDS_Shape body = window.document().shapeOf(slab);
            window.view()->clearSelection();
            pump(100);
            for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
                const TopoDS_Face face = TopoDS::Face(e.Current());
                if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane) continue;
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props);
                if (props.Mass() < 100000.0) continue;
                QPoint at;
                if (!window.view()->projectToScreen(props.CentreOfMass(), at)) continue;
                const QPointF p(at);
                const QPointF g = window.view()->mapToGlobal(p);
                QMouseEvent move(QEvent::MouseMove, p, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &move);
                pump(60);
                QMouseEvent down(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &down);
                QMouseEvent up(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &up);
                pump(120);
                if (!window.view()->selectedFace().IsNull() && window.canBeginSlats()) return true;
            }
            return false;
        };
        const auto pageField = [&](const char* id) {
            return jsString(page, QStringLiteral("(document.querySelector('[data-gesture=\"slats\"] input[data-field=\"%1\"]')"
                                                 " || {}).value || ''").arg(QLatin1String(id)));
        };
        if (slatsTool && slatsAction && pickTop()) {
            slatsAction->trigger();
            check(window.slatsActive(), "Slats... begins on the slab's face");
            check(waitFor([&] { return drawn(QStringLiteral("slats")); }, 3000), "the page draws the slats chip");
            check(standsOn(QStringLiteral("slats"), slatsTool) && slatsTool->widthField()->isHidden() &&
                      slatsTool->flipButton()->isHidden(),
                  "where the Qt chip stands, its own fields and Flip hidden");
            check(pageField("width") == slatsTool->widthField()->text() && pageField("gap") == slatsTool->gapField()->text() &&
                      pageField("depth") == slatsTool->depthField()->text(),
                  "its fields are seeded with the Qt fields' numbers - " + pageField("width") + ", " + pageField("gap") +
                      ", " + pageField("depth"));
            check(waitFor([&] { return chrome->claimsKeyboardFor(QStringLiteral("slats")); }, 2000) &&
                      jsString(page, "document.activeElement && document.activeElement.dataset.field") == QStringLiteral("width"),
                  "Width is focused and holds the keyboard for the slats chip (R5)");
            const auto countShown = [&] {
                return jsString(page, "(document.querySelector('[data-gesture=\"slats\"] .text.muted') || {}).textContent || ''");
            };
            js(page, "(() => { const f = document.querySelector('[data-gesture=\"slats\"] input[data-field=\"width\"]'); "
                     "f.value = '30'; f.dispatchEvent(new Event('input', {bubbles: true})); })()");
            check(waitFor([&] { return slatsTool->widthField()->text() == QStringLiteral("30"); }, 2000),
                  "typing 30 on the page reaches the Qt field");
            const QString expected = slatsTool->count() == 1 ? QStringLiteral("1 slat")
                                                              : QStringLiteral("%1 slats").arg(slatsTool->count());
            check(slatsTool->count() > 0 && waitFor([&] { return countShown() == expected; }, 2000),
                  "and the live count on the page is the Qt chip's - " + countShown());
#ifdef _WIN32
            capture(&window, outDir + "/chip-slats.png");
#endif
            // A page that cannot draw it: the Qt chip draws itself again,
            // holding what was typed (Review Focus 3).
            slatsTool->face()->setDrawnElsewhere(false);
            pump(100);
            check(slatsTool->widthField()->isVisible() && slatsTool->widthField()->text() == QStringLiteral("30"),
                  "drawn here again, the Qt chip shows its own fields holding what the page typed");
            slatsTool->face()->setDrawnElsewhere(true);
            pump(100);
            check(slatsTool->widthField()->isHidden(), "and hides them once the page draws it again");
            waitFor([&] { return drawn(QStringLiteral("slats")); }, 3000);
            // R3: a real Enter, typed straight after a digit, aimed at the
            // page's field - it passes the Qt chip's claim (the field holds
            // the keyboard for this chip) and the page answers it WITH 35.
            const size_t before = window.document().solids().size();
            js(page, "(() => { const f = document.querySelector('[data-gesture=\"slats\"] input[data-field=\"width\"]'); "
                     "f.dispatchEvent(new MouseEvent('mousedown', {bubbles: true})); f.focus(); "
                     "f.value = '35'; f.dispatchEvent(new Event('input', {bubbles: true})); })()");
            QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
            QKeyEvent enterOver(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
            enterOver.ignore();
            QCoreApplication::sendEvent(proxy, &enterOver);
            std::printf("[info] before the Qt Enter: page holds slats keys %d, key is the page's %d, drawn elsewhere %d, "
                        "proxy %s inside the page %d\n",
                        int(chrome->claimsKeyboardFor(QStringLiteral("slats"))),
                        int(slatsTool->face()->keyBelongsToPage(proxy)), int(slatsTool->face()->drawnElsewhere()),
                        proxy->metaObject()->className(), int(proxy == page || page->isAncestorOf(proxy)));
            QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(QChar(13)));
            QCoreApplication::sendEvent(proxy, &enterPress);
            check(window.slatsActive(), "a Qt Enter aimed at the page's field is not the Qt chip's to apply (R3)");
            QKeyEvent enterRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QString(QChar(13)));
            QCoreApplication::sendEvent(proxy, &enterRelease);
            // A key sent from here never reaches Chromium's DOM in a window
            // the OS has not activated (no block in this suite delivers one),
            // so the page's half - its keydown, typed straight after a digit
            // in the same turn - is dispatched in the page.
            js(page, "(() => { const f = document.querySelector('[data-gesture=\"slats\"] input[data-field=\"width\"]'); "
                     "f.value = '35'; f.dispatchEvent(new Event('input', {bubbles: true})); "
                     "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()");
            check(waitFor([&] { return !window.slatsActive(); }, 3000), "the page answers that Enter and applies the slats");
            bool thirtyFive = false;
            for (size_t i = before; i < window.document().solids().size(); ++i) {
                const ModelingOps::MeasuredBox box = ModelingOps::measuredBox(std::vector<TopoDS_Shape>{window.document().solids()[i].shape});
                for (double extent : {box.width, box.depth, box.height})
                    if (std::abs(extent - 35.0) < 0.01) thirtyFive = true;
            }
            check(window.document().solids().size() > before && thirtyFive,
                  QStringLiteral("and lays slats 35 wide - the width the page held at Enter (%1 new bodies)")
                      .arg(window.document().solids().size() - before));
            check(waitFor([&] { return !chrome->claimsKeyboardFor(QStringLiteral("slats")) && !chrome->holdsKeyboard(); }, 2000),
                  "the gesture ending gives the keyboard back (Review Focus 2)");
            if (QAction* undo = actionNamed(&window, QStringLiteral("Undo"))) undo->trigger();
            pump(300);
            // A second gesture re-seeds, and Escape on the page cancels it.
            if (pickTop()) {
                slatsAction->trigger();
                waitFor([&] { return drawn(QStringLiteral("slats")); }, 3000);
                check(waitFor([&] { return pageField("width") == slatsTool->widthField()->text(); }, 2000),
                      "a second gesture seeds the page's Width afresh - " + pageField("width") + " (Review Focus 5)");
                js(page, "document.querySelector('[data-gesture=\"slats\"] input[data-field=\"gap\"]').dispatchEvent("
                         "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
                check(waitFor([&] { return !window.slatsActive(); }, 2000) &&
                          window.document().solids().size() == before,
                      "Escape on the page cancels it, laying nothing");
            } else {
                check(false, "the slab's face could be picked a second time");
            }
        } else if (slatsTool && slatsAction) {
            check(false, "the slab's top face could be picked for slats");
        }

        // --- Phase 4b: Extrude and the glued chips ------------------------------------
        // A face of a body whose area lies in [minArea, maxArea], picked the
        // way a hand picks it; `ok` says whether the pick is the one wanted.
        const auto pickFaceWhere = [&](double minArea, double maxArea, const std::function<bool()>& ok) {
            window.view()->clearSelection();
            pump(100);
            for (const auto& solid : window.document().solids()) {
                for (TopExp_Explorer e(solid.shape, TopAbs_FACE); e.More(); e.Next()) {
                    const TopoDS_Face face = TopoDS::Face(e.Current());
                    if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane) continue;
                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(face, props);
                    if (props.Mass() < minArea || props.Mass() > maxArea) continue;
                    QPoint at;
                    if (!window.view()->projectToScreen(props.CentreOfMass(), at)) continue;
                    if (!window.view()->rect().adjusted(60, 60, -60, -60).contains(at)) continue;
                    const QPointF p(at);
                    const QPointF g = window.view()->mapToGlobal(p);
                    QMouseEvent move(QEvent::MouseMove, p, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window.view(), &move);
                    pump(60);
                    QMouseEvent down(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window.view(), &down);
                    QMouseEvent up(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window.view(), &up);
                    pump(120);
                    if (!window.view()->selectedFace().IsNull() && ok()) return true;
                }
            }
            return false;
        };
        const auto chipField = [&](const QString& chip, const QString& id) {
            return jsString(page, QStringLiteral("(document.querySelector('[data-gesture=\"%1\"] input[data-field=\"%2\"]')"
                                                 " || {}).value || ''").arg(chip, id));
        };
        // Types into a chip's field on the page and presses Enter there, in one
        // turn (the page's half of R3).
        const auto typeAndEnter = [&](const QString& chip, const QString& id, const QString& text) {
            js(page, QStringLiteral("(() => { const f = document.querySelector('[data-gesture=\"%1\"] input[data-field=\"%2\"]'); "
                                    "f.dispatchEvent(new MouseEvent('mousedown', {bubbles: true})); f.focus(); "
                                    "f.value = '%3'; f.dispatchEvent(new Event('input', {bubbles: true})); "
                                    "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()")
                         .arg(chip, id, text));
        };
        QAction* undo = actionNamed(&window, QStringLiteral("Undo"));

        // Every 4b chip is handed to the page, and the five glued ones are glued.
        {
            const std::vector<ChipFace*> faces = window.chipFaces();
            QStringList names;
            int glued = 0;
            bool allDrawnElsewhere = true;
            for (ChipFace* f : faces) {
                names << f->name();
                glued += f->glued() ? 1 : 0;
                allDrawnElsewhere = allDrawnElsewhere && f->drawnElsewhere();
            }
            names.sort();
            check(names.join(QLatin1Char(',')) ==
                      QStringLiteral("bevel,boolean,extrude,joint,material,mitre,move,pull,remeasure,shapes,slats") &&
                      glued == 5 && allDrawnElsewhere,
                  "every gesture chip is drawn by the page, and the five that follow the model are glued - " +
                      names.join(QStringLiteral(", ")));
        }

        // --- Extrude ----------------------------------------------------------------
        ExtrudePreview* extrude = window.findChild<ExtrudePreview*>();
        QAction* startSketch = actionNamed(&window, QStringLiteral("Start Sketch"));
        QAction* finishSketch = actionNamed(&window, QStringLiteral("Finish Sketch"));
        QAction* extrudeAction = actionNamed(&window, QStringLiteral("Extrude..."));
        check(extrude && startSketch && finishSketch && extrudeAction, "chips: the editor has its extrude panel");
        if (extrude && startSketch && finishSketch && extrudeAction) {
            window.view()->clearSelection();
            startSketch->trigger();
            pump(100);
            const double w = window.view()->width(), h = window.view()->height();
            for (const QPointF& f : {QPointF(0.62, 0.62), QPointF(0.72, 0.62), QPointF(0.72, 0.72), QPointF(0.62, 0.72)}) {
                const QPointF p(f.x() * w, f.y() * h);
                const QPointF g = window.view()->mapToGlobal(p);
                QMouseEvent move(QEvent::MouseMove, p, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &move);
                QMouseEvent down(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &down);
                QMouseEvent up(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &up);
                pump(80);
            }
            finishSketch->trigger();
            pump(150);
            check(window.hasPendingFace(), "a closed outline waits to be extruded");
            const size_t before = window.document().solids().size();
            extrudeAction->trigger();
            check(extrude->isVisible() && waitFor([&] { return drawn(QStringLiteral("extrude")); }, 3000),
                  "Extrude... opens its panel, and the page draws it");
            check(waitFor([&] { return standsOn(QStringLiteral("extrude"), extrude); }, 2000) && extrude->field()->isHidden(),
                  "where the Qt panel stands, its own field hidden");
            check(chipField(QStringLiteral("extrude"), QStringLiteral("height")) == extrude->field()->text() &&
                      waitFor([&] { return chrome->claimsKeyboardFor(QStringLiteral("extrude")); }, 2000),
                  "its height is seeded with the panel's own " + extrude->field()->text() +
                      ", focused and holding the keyboard (R5)");
            check(std::abs(extrude->x() + extrude->width() / 2 - window.view()->width() / 2) <= 2,
                  "top centre, placed by the panel's own code with the page's width");
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-extrude.png");
#endif
            typeAndEnter(QStringLiteral("extrude"), QStringLiteral("height"), QStringLiteral("25"));
            check(waitFor([&] { return window.document().solids().size() == before + 1; }, 3000),
                  "Enter on the page extrudes the outline into a body");
            if (window.document().solids().size() == before + 1) {
                const ModelingOps::MeasuredBox box =
                    ModelingOps::measuredBox(std::vector<TopoDS_Shape>{window.document().solids().back().shape});
                check(std::abs(box.height - 25.0) < 0.01,
                      QStringLiteral("25 tall - the height the page held at Enter (%1)").arg(box.height));
            }
            check(waitFor([&] { return !chrome->claimsKeyboardFor(QStringLiteral("extrude")); }, 2000),
                  "and the panel going gives the keyboard back");
            if (undo) undo->trigger();   // the body
            pump(150);
            if (undo) undo->trigger();   // the outline
            pump(250);
        }

        // --- Pull: a glued chip, and the camera moving ---------------------------
        PullArrow* pull = window.findChild<PullArrow*>();
        check(pull != nullptr, "chips: the editor has its pull arrow");
        if (pull && pickFaceWhere(100000.0, 1.0e9, [&] { return pull->isVisible(); })) {
            check(waitFor([&] { return drawn(QStringLiteral("pull")); }, 3000) && standsOn(QStringLiteral("pull"), pull) &&
                      pull->field()->isHidden(),
                  "a picked face raises the pull arrow, and the page draws its chip where the Qt chip stands");
            // An orbit: the chip leaves the page while the camera moves, and
            // comes back beside the arrow once it holds still.
            const QPointF c(window.view()->width() / 2.0, window.view()->height() / 2.0);
            const auto rmb = [&](QEvent::Type type, const QPointF& p, Qt::MouseButtons buttons) {
                QMouseEvent e(type, p, window.view()->mapToGlobal(p), Qt::RightButton, buttons, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &e);
            };
            rmb(QEvent::MouseButtonPress, c, Qt::RightButton);
            bool hiddenMidOrbit = false;
            bool movingInMap = false;
            for (int step = 1; step <= 12; ++step) {
                QMouseEvent move(QEvent::MouseMove, c + QPointF(step * 6.0, 0), window.view()->mapToGlobal(c + QPointF(step * 6.0, 0)),
                                 Qt::NoButton, Qt::RightButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window.view(), &move);
                pump(40);
                movingInMap = movingInMap ||
                              chrome->bridge()->cards().value(QStringLiteral("pull")).toMap().value(QStringLiteral("moving")).toBool();
                hiddenMidOrbit = hiddenMidOrbit || !drawn(QStringLiteral("pull"));
            }
            rmb(QEvent::MouseButtonRelease, c + QPointF(72, 0), Qt::NoButton);
            check(movingInMap && hiddenMidOrbit,
                  "while the camera orbits, the glued chip is marked moving and the page does not draw it (gate 4)");
            check(waitFor([&] { return drawn(QStringLiteral("pull")) && standsOn(QStringLiteral("pull"), pull); }, 3000),
                  "once the camera holds still it is drawn again, beside the arrow's new place");
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-pull.png");
#endif
            const int revision = window.document().revision();
            typeAndEnter(QStringLiteral("pull"), QStringLiteral("distance"), QStringLiteral("20"));
            check(waitFor([&] { return window.document().revision() != revision; }, 3000),
                  "Enter on the page pulls the face by what its field held");
            if (undo) undo->trigger();
            pump(250);
        } else if (pull) {
            check(false, "a large face could be picked to raise the pull arrow");
        }

        // --- Mitre: a field, Flip and a glued card ----------------------------------
        MitreTool* mitre = window.findChild<MitreTool*>();
        QAction* mitreAction = actionNamed(&window, QStringLiteral("Mitre end"));
        check(mitre && mitreAction, "chips: the editor has its mitre chip");
        if (mitre && mitreAction &&
            pickFaceWhere(1000.0, 10000.0, [&] { return mitreAction->isEnabled(); })) {
            mitreAction->trigger();
            check(window.mitreEndActive() && waitFor([&] { return drawn(QStringLiteral("mitre")); }, 3000) &&
                      standsOn(QStringLiteral("mitre"), mitre) && mitre->field()->isHidden() && mitre->flipButton()->isHidden(),
                  "Mitre end opens its chip on the page, where the Qt chip stands, its field and Flip hidden");
            check(chipField(QStringLiteral("mitre"), QStringLiteral("angle")) == mitre->field()->text() &&
                      waitFor([&] { return chrome->claimsKeyboardFor(QStringLiteral("mitre")); }, 2000),
                  "its angle is seeded with " + mitre->field()->text() + " and holds the keyboard");
            int flips = 0;
            const QMetaObject::Connection counted =
                QObject::connect(mitre->flipButton(), &QPushButton::clicked, [&] { ++flips; });
            js(page, "document.querySelector('[data-gesture=\"mitre\"] [data-part=\"flip\"]').click()");
            check(waitFor([&] { return flips == 1; }, 2000), "the page's Flip clicks the Qt chip's own Flip");
            QObject::disconnect(counted);
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-mitre.png");
#endif
            js(page, "document.querySelector('[data-gesture=\"mitre\"] input[data-field=\"angle\"]').dispatchEvent("
                     "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
            check(waitFor([&] { return !window.mitreEndActive(); }, 2000), "Escape on the page cancels the mitre");
        } else if (mitre && mitreAction) {
            check(false, "a board end could be picked for a mitre");
        }

        // --- Re-measure ---------------------------------------------------------------
        ReMeasureTool* remeasure = window.findChild<ReMeasureTool*>();
        check(remeasure != nullptr, "chips: the editor has its re-measure chip");
        if (remeasure) {
            const int body = window.document().solids().front().id;
            window.view()->setSelectedSolids({body});
            pump(200);
            const bool began = window.beginReMeasure(0);
            check(began && waitFor([&] { return drawn(QStringLiteral("remeasure")); }, 3000) &&
                      standsOn(QStringLiteral("remeasure"), remeasure) && remeasure->field()->isHidden(),
                  "re-measuring a size opens its chip on the page, where the Qt chip stands");
            check(chipField(QStringLiteral("remeasure"), QStringLiteral("size")) == remeasure->field()->text(),
                  "seeded with the size it retypes - " + remeasure->field()->text());
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-remeasure.png");
#endif
            const int revision = window.document().revision();
            typeAndEnter(QStringLiteral("remeasure"), QStringLiteral("size"), QStringLiteral("250"));
            check(waitFor([&] { return window.document().revision() != revision && !window.reMeasureActive(); }, 3000),
                  "Enter on the page retypes the size");
            if (undo) undo->trigger();
            pump(250);
        }

        // --- the joint card (Phase 4c): docked, not glued ---------------------------
        JointChip* jointChip = window.findChild<JointChip*>();
        check(jointChip && window.openFurniture(shelfUnitId), "chips: the shelf unit opens, and the editor has its joint card");
        pump(300);
        if (jointChip && !window.document().joints().empty()) {
            const int jointId = window.document().joints().front().id;
            const auto jointKind = [&] {
                for (const DocumentModel::Joint& j : window.document().joints())
                    if (j.id == jointId) return j.kind;
                return Joinery::Kind::Dowel;
            };
            const auto jointCount = [&] {
                for (const DocumentModel::Joint& j : window.document().joints())
                    if (j.id == jointId) return j.params.count;
                return -1;
            };
            window.setSelectedJoint(jointId);
            pump(200);
            check(window.jointChipJointId() == jointId && jointChip->isVisible() &&
                      waitFor([&] { return drawn(QStringLiteral("joint")); }, 3000),
                  "selecting a joint opens its card, and the page draws it");
            check(waitFor([&] { return standsOn(QStringLiteral("joint"), jointChip); }, 2000) &&
                      jointChip->kindButton()->isHidden() && jointChip->field()->isHidden(),
                  "where the Qt card stands in the TopRight column, its own controls hidden");
            check(chipField(QStringLiteral("joint"), QStringLiteral("count")) ==
                      jointChip->fieldFor(JointChip::Slot::Count)->text(),
                  "its count is the card's own - " + chipField(QStringLiteral("joint"), QStringLiteral("count")));
            check(jsString(page, "(document.querySelector('[data-gesture=\"joint\"] [data-part=\"kind\"]') || {}).textContent || ''")
                          .trimmed() == jointChip->kindButton()->text(),
                  "its kind button reads " + jointChip->kindButton()->text());

            js(page, "document.querySelector('[data-gesture=\"joint\"] [data-part=\"more\"]').click()");
            check(waitFor([&] { return jointChip->moreOpen() && !chipField(QStringLiteral("joint"), QStringLiteral("size")).isEmpty(); }, 3000),
                  "More opens the rest of its numbers, on the page too");
            check(waitFor([&] { return standsOn(QStringLiteral("joint"), jointChip); }, 2000),
                  "and the taller card still stands where the Qt card does");
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-joint-more.png");
#endif
            js(page, "document.querySelector('[data-gesture=\"joint\"] [data-part=\"kind\"]').click()");
            check(waitFor([&] { return jointChip->kindMenuOpen() &&
                                       js(page, "document.querySelectorAll('[data-gesture=\"joint\"] .menu-list .entry').length").toInt() == 10; }, 3000) &&
                      jointChip->kindMenu()->isHidden(),
                  "the kind button opens the menu inside the page's card, all ten kinds, the Qt menu never shown");
#ifdef _WIN32
            pump(200);
            capture(&window, outDir + "/chip-joint-menu.png");
#endif
            js(page, QStringLiteral("document.querySelector('[data-gesture=\"joint\"] [data-part=\"kind:%1\"]').click()")
                         .arg(static_cast<int>(Joinery::Kind::Screw)));
            check(waitFor([&] { return jointKind() == Joinery::Kind::Screw && !jointChip->kindMenuOpen(); }, 3000),
                  "picking Screw on the page switches the joint, and the menu closes");
            typeAndEnter(QStringLiteral("joint"), QStringLiteral("count"), QStringLiteral("3"));
            check(waitFor([&] { return jointCount() == 3; }, 3000), "Enter on the page sets the count it held - 3");
            check(waitFor([&] { return chipField(QStringLiteral("joint"), QStringLiteral("count")) == QStringLiteral("3"); }, 2000),
                  "and the page's field reads the joint's new count");
            js(page, "document.querySelector('[data-gesture=\"joint\"] input[data-field=\"count\"]').dispatchEvent("
                     "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
            check(waitFor([&] { return !jointChip->isVisible() && pageChip(QStringLiteral("joint")).isEmpty(); }, 3000),
                  "Escape with nothing typed closes the card, on the page too");
            if (undo) undo->trigger();
            pump(150);
            if (undo) undo->trigger();
            pump(250);
        }
    }


    // --- Ctrl+D on a folder with mirroring on (T-064) ---------------------------------
    {
        check(window.openFurniture(mirroredId), "the mirrored furniture opens");
        pump(200);
        const DocumentModel& doc = window.document();
        int pair = 0;
        for (const DocumentModel::Group& g : doc.groups())
            if (g.name == "Pair") pair = g.id;
        check(pair != 0 && doc.symmetryOn() && doc.hasStoredOrder(pair), "set up: mirroring on, Pair in a stored order");
        window.view()->setSelectedSolids(doc.bodiesUnderGroup(pair));
        pump(100);
        QAction* duplicate = actionNamed(&window, QStringLiteral("Duplicate"));
        if (duplicate) duplicate->trigger();
        pump(300);
        int clone = 0;
        for (const DocumentModel::Group& g : doc.groups())
            if (g.name == "Pair copy") clone = g.id;
        const std::vector<int> made = window.view()->selectedSolidIds();
        const std::vector<int> seq = clone ? doc.orderedChildren(clone) : std::vector<int>();
        QStringList read;
        for (int id : seq)
            read << QString::fromStdString(doc.nameOf(id)) +
                        (std::find(made.begin(), made.end(), id) != made.end() ? QStringLiteral("(copy)") : QStringLiteral("(twin)"));
        const bool paired = seq.size() == 4 && std::find(made.begin(), made.end(), seq[0]) != made.end() &&
                            std::find(made.begin(), made.end(), seq[2]) != made.end() &&
                            doc.twinOf(seq[0]) == seq[1] && doc.twinOf(seq[2]) == seq[3];
        check(clone != 0 && paired,
              "Ctrl+D on a mirrored folder reads each copy, then its own twin - " + read.join(QStringLiteral(", ")));
        check(seq.size() == 4 && doc.nameOf(seq[0]).empty() == false,
              "four rows in the copy (two copies, two twins)");
        window.openFurniture(furnitureId);
        pump(300);
    }

    // --- the first-run guide and the hint balloon, drawn by the page (Phase 7a) ------
    {
        // A fresh window, so the guide stands as it does on a first run.
        MainWindow fresh(nullptr, /*persistProgress=*/false, library.path());
        fresh.setAttribute(Qt::WA_ShowWithoutActivating);
        fresh.resize(1280, 820);
        fresh.useHtmlChrome();
        fresh.show();
        fresh.view()->setAnimationsEnabled(false);
        check(waitFor([&] { return fresh.htmlChrome() && fresh.htmlChrome()->isLive(); }, 20000),
              "teaching: a fresh window's page is live");
        fresh.openFurniture(furnitureId);
        pump(400);
        QWebEngineView* fp = fresh.htmlChrome() ? fresh.htmlChrome()->view() : nullptr;
        auto* guide = fresh.view()->findChild<WalkthroughPanel*>();
        check(guide && guide->isVisible() && guide->drawnElsewhere(), "teaching: the guide stands, drawn by the page");
        if (fp && guide && guide->isVisible()) {
            check(waitFor([&] { return js(fp, "!!document.querySelector('.guide')").toBool(); }, 3000) &&
                      js(fp, "document.querySelectorAll('.guide .steps li').length").toInt() == 4,
                  "teaching: the page draws the guide and its four steps");
            const QRect drawn(js(fp, "Math.round(document.querySelector('.guide').getBoundingClientRect().left)").toInt(),
                              js(fp, "Math.round(document.querySelector('.guide').getBoundingClientRect().top)").toInt(),
                              js(fp, "Math.round(document.querySelector('.guide').getBoundingClientRect().width)").toInt(),
                              js(fp, "Math.round(document.querySelector('.guide').getBoundingClientRect().height)").toInt());
            check(std::abs(drawn.x() - guide->x()) <= 1 && std::abs(drawn.y() - guide->y()) <= 1 &&
                      std::abs(drawn.width() - guide->width()) <= 1 && std::abs(drawn.height() - guide->height()) <= 1,
                  QStringLiteral("teaching: at the Qt guide's own place (%1,%2 %3x%4 against %5,%6 %7x%8)")
                      .arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())
                      .arg(guide->x()).arg(guide->y()).arg(guide->width()).arg(guide->height()));
#ifdef _WIN32
            pump(250);
            capture(&fresh, outDir + "/guide.png");
#endif
        }
        // The hint: two bodies selected raise the boolean hint.
        std::vector<int> two;
        for (const DocumentModel::Solid& sd : fresh.document().solids())
            if (two.size() < 2) two.push_back(sd.id);
        fresh.view()->setSelectedSolids(two);
        pump(400);
        auto* balloon = fresh.view()->findChild<HintBalloon*>();
        check(balloon && balloon->isVisible() && balloon->drawnElsewhere(), "teaching: two bodies selected raise a hint");
        if (fp && balloon && balloon->isVisible()) {
            check(waitFor([&] { return jsString(fp, "(document.querySelector('.hint .text') || {}).textContent || ''") ==
                                       balloon->currentHint(); }, 3000),
                  "teaching: the page draws the hint the Qt balloon chose - " + balloon->currentHint());
#ifdef _WIN32
            pump(250);
            capture(&fresh, outDir + "/hint.png");
#endif
            js(fp, "document.querySelector('.hint').click()");
            check(waitFor([&] { return !balloon->isVisible(); }, 3000) &&
                      waitFor([&] { return !js(fp, "!!document.querySelector('.hint')").toBool(); }, 3000),
                  "teaching: a press on the page's balloon is its got it - the hint retires, on the page too");
        }
        if (fp && guide && guide->isVisible()) {
            js(fp, "document.querySelector('.guide .skip').click()");
            check(waitFor([&] { return !guide->isVisible() && guide->isFinished(); }, 3000) &&
                      waitFor([&] { return !js(fp, "!!document.querySelector('.guide')").toBool(); }, 3000),
                  "teaching: the page's skip finishes the guide, and its card goes");
        }
        fresh.close();
        pump(200);
    }

    // --- the window buttons, drawn by the page (Phase 7a) -----------------------------
    {
        auto* slot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_buttons"));
        HubWindowControls* controls = window.windowControls();
        check(slot && slot->isVisible() && controls,
              "buttons: the page's window buttons stand in their place");
        check(waitFor([&] { return js(page, "document.querySelectorAll('.window-buttons .win').length").toInt() == 3; }, 3000),
              "buttons: the page draws minimize, maximize and close");
        // The maximize button's rectangle is what Windows' hit test answers
        // HTMAXBUTTON from (snap layouts) - reported, and inside the slot.
        check(controls && waitFor([&] { return !controls->maxButton().isEmpty(); }, 3000) &&
                  slot->geometry().contains(controls->maxButton().center()),
              QStringLiteral("buttons: the page reports its maximize button, inside the slot (%1,%2 in %3,%4 %5x%6)")
                  .arg(controls ? controls->maxButton().center().x() : -1).arg(controls ? controls->maxButton().center().y() : -1)
                  .arg(slot ? slot->x() : -1).arg(slot ? slot->y() : -1).arg(slot ? slot->width() : -1).arg(slot ? slot->height() : -1));
        if (controls) {
            emit controls->maxHovered(true);
            check(waitFor([&] { return js(page, "document.querySelectorAll('.window-buttons .win')[1].classList.contains('hovered')").toBool(); }, 2000),
                  "buttons: the native side's maximize hover reaches the page's button");
            emit controls->maxHovered(false);
        }
#ifdef _WIN32
        pump(200);
        capture(&window, outDir + "/window-buttons.png");
#endif
    }

    // --- the Add-shape flyout, its tiles drawn by the page (Phase 7a) -----------------
    {
        QAction* addShape = actionNamed(&window, QStringLiteral("Add shape"));
        ShapeFlyout* flyout = window.shapeFlyout();
        check(addShape && flyout && flyout->face() && flyout->face()->drawnElsewhere(),
              "shapes: the flyout is drawn by the page while it is live");
        if (addShape && flyout && addShape->isEnabled()) {
            const std::size_t before = window.document().count();
            addShape->trigger();
            const int pageTiles = waitFor([&] { return js(page, "document.querySelectorAll('[data-gesture=\"shapes\"] .tile').length").toInt() > 0; }, 3000)
                                      ? js(page, "document.querySelectorAll('[data-gesture=\"shapes\"] .tile').length").toInt()
                                      : 0;
            check(flyout->isVisible() && pageTiles >= 3,
                  QStringLiteral("shapes: Add shape opens it, and the page draws its %1 tiles").arg(pageTiles));
            check(js(page, "[...document.querySelectorAll('[data-gesture=\"shapes\"] .tile-glyph')].every(g => (g.getAttribute('style') || '').includes('data:image/png'))").toBool(),
                  "shapes: each tile wears its glyph");
#ifdef _WIN32
            pump(250);
            capture(&window, outDir + "/shape-flyout.png");
#endif
            js(page, "document.querySelector('[data-gesture=\"shapes\"] .tile').click()");
            const bool added = waitFor([&] { return window.document().count() == before + 1; }, 3000);
            check(added && waitFor([&] { return !flyout->isVisible(); }, 2000),
                  QStringLiteral("shapes: a tile on the page adds its shape and closes the flyout (%1 -> %2 bodies)")
                      .arg(before).arg(window.document().count()));
            if (added) {
                if (QAction* undo = actionNamed(&window, QStringLiteral("Undo"))) undo->trigger();
                pump(300);
            }
        }
    }

    // --- the material card, a chip face with sliders (Phase 7a) ----------------------
    {
        QAction* renderAction = actionNamed(&window, QStringLiteral("Render mode"));
        MaterialCard* mcard = window.materialCard();
        check(renderAction && mcard && mcard->face() && mcard->face()->drawnElsewhere(),
              "material: the card is drawn by the page while it is live");
        if (renderAction && mcard) {
            if (!renderAction->isChecked()) renderAction->trigger();
            pump(600);
            MaterialCard::Look look;
            look.surface = 0.45;
            // The live wood: the card closes itself on any other material.
            auto* renderPanel = window.findChild<RenderSettingsPanel*>();
            const QString liveWood = renderPanel ? renderPanel->woodSelection() : QString();
            check(!liveWood.isEmpty(), "material: a wood is live - " + liveWood);
            mcard->open(liveWood, look, /*wood=*/true);
            check(waitFor([&] { return js(page, "!!document.querySelector('[data-gesture=\"material\"] input[data-part=\"surface\"]')").toBool(); }, 3000),
                  "material: the page draws the card with its sliders");
            check(js(page, "document.querySelector('[data-gesture=\"material\"] input[data-part=\"metal\"]') === null").toBool(),
                  "material: a wood is offered no Metal, on the page as in the Qt card");
            // A drag on the page lands in the Qt card's own slider, whose own
            // logic writes the look.
            js(page, "(() => { const r = document.querySelector('[data-gesture=\"material\"] input[data-part=\"surface\"]'); "
                     "r.value = '80'; r.dispatchEvent(new Event('input', {bubbles: true})); })()");
            check(waitFor([&] { return std::abs(mcard->look().surface - 0.80) < 1e-9; }, 3000),
                  QStringLiteral("material: the page's Surface slider moves the card's look (%1)").arg(mcard->look().surface));
            QString readouts;
            // Waited for FIRST: the message must read what the wait saw, and a
            // check()'s arguments are evaluated in no fixed order.
            const bool readoutFollows = waitFor([&] {
                readouts = jsString(page, "[...document.querySelectorAll('[data-gesture=\"material\"] .readout')].map(n => n.textContent).join('|')");
                return readouts.split(QLatin1Char('|')).contains(QStringLiteral("80"));
            }, 3000);
            check(readoutFollows, "material: and the readout the page shows follows it - " + readouts);
#ifdef _WIN32
            pump(300);
            capture(&window, outDir + "/material-card.png");
#endif
            js(page, "document.querySelector('[data-gesture=\"material\"] [data-part=\"done\"]').click()");
            check(waitFor([&] { return !mcard->isOpen(); }, 3000) &&
                      waitFor([&] { return !js(page, "!!document.querySelector('[data-gesture=\"material\"]')").toBool(); }, 3000),
                  "material: the page's Done closes it");
            // Render mode's way out is the page's Back chip (Phase 7a).
            auto* backSlot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_back"));
            check(backSlot && backSlot->isVisible() &&
                      waitFor([&] { return js(page, "!!document.querySelector('[data-cluster=\"back\"] [data-chip]')").toBool(); }, 3000),
                  "back: in render mode the page draws the Back chip in its place");
            js(page, "document.querySelector('[data-cluster=\"back\"] [data-chip]').click()");
            check(waitFor([&] { return !renderAction->isChecked(); }, 3000),
                  "back: the page's Back chip leaves render mode");
            check(waitFor([&] { return !backSlot || !backSlot->isVisible(); }, 2000),
                  "back: and its place goes with render mode");
            if (renderAction->isChecked()) renderAction->trigger();
            pump(400);
        }
    }

    // --- the shortcut sheet, drawn by the page under the Qt sheet (Phase 7a) ---------
    {
        auto* qtSheet = window.findChild<ShortcutSheet*>();
        QAction* sheetAction = actionNamed(&window, QStringLiteral("Keyboard Shortcuts"));
        check(qtSheet && sheetAction && qtSheet->drawnElsewhere(), "the Qt shortcut sheet is drawn by the page while it is live");
        if (qtSheet && sheetAction) {
            sheetAction->trigger();
            check(waitFor([&] { return js(page, "!!document.querySelector('.sheet')").toBool(); }, 3000) && qtSheet->isVisible(),
                  "Keyboard Shortcuts opens the sheet, and the page draws it");
            const int pageRows = js(page, "document.querySelectorAll('.sheet .row').length").toInt();
            check(pageRows == qtSheet->rowCount() && pageRows > 20,
                  QStringLiteral("every row the Qt sheet generated is on the page (%1 of %2)").arg(pageRows).arg(qtSheet->rowCount()));
            const QRect drawn(js(page, "Math.round(document.querySelector('.sheet').getBoundingClientRect().left)").toInt(),
                              js(page, "Math.round(document.querySelector('.sheet').getBoundingClientRect().top)").toInt(),
                              js(page, "Math.round(document.querySelector('.sheet').getBoundingClientRect().width)").toInt(),
                              js(page, "Math.round(document.querySelector('.sheet').getBoundingClientRect().height)").toInt());
            const QRect qtRect(window.view()->mapFrom(&window, qtSheet->pos()), qtSheet->size());
            check(std::abs(drawn.x() - qtRect.x()) <= 1 && std::abs(drawn.y() - qtRect.y()) <= 1 &&
                      std::abs(drawn.width() - qtRect.width()) <= 1 && std::abs(drawn.height() - qtRect.height()) <= 1,
                  QStringLiteral("the card stands exactly where the Qt sheet does (%1,%2 %3x%4 against %5,%6 %7x%8)")
                      .arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())
                      .arg(qtRect.x()).arg(qtRect.y()).arg(qtRect.width()).arg(qtRect.height()));
#ifdef _WIN32
            {
                pump(300);
                const QImage withPage = captureImage(&window);
                withPage.save(outDir + "/shortcut-sheet.png");
                page->hide();
                pump(300);
                const QImage without = captureImage(&window);
                page->show();
                pump(300);
                const double dpr = window.devicePixelRatioF();
                const QRect logical(window.view()->mapTo(&window, qtRect.topLeft()), qtRect.size());
                const QRect r(qRound(logical.x() * dpr), qRound(logical.y() * dpr), qRound(logical.width() * dpr),
                              qRound(logical.height() * dpr));
                int moved = 0, all = 0;
                for (int y = r.top(); y <= r.bottom(); y += 2)
                    for (int x = r.left(); x <= r.right(); x += 2) {
                        if (!withPage.rect().contains(x, y) || !without.rect().contains(x, y)) continue;
                        const QColor p1 = withPage.pixelColor(x, y), p2 = without.pixelColor(x, y);
                        ++all;
                        if (std::abs(p1.red() - p2.red()) + std::abs(p1.green() - p2.green()) +
                                std::abs(p1.blue() - p2.blue()) > 9)
                            ++moved;
                    }
                const double share = all ? double(moved) / all : 0.0;
                check(share > 0.30, QStringLiteral("the composited window SHOWS the sheet (%1% of its pixels change "
                                                   "when the page is hidden)").arg(share * 100, 0, 'f', 1));
            }
#endif
            // Escape is the Qt sheet's, and the page's card goes with it.
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(qtSheet, &esc);
            check(!qtSheet->isVisible() &&
                      waitFor([&] { return !js(page, "!!document.querySelector('.sheet')").toBool(); }, 3000),
                  "Escape closes it, on the page too");
        }
    }

    // --- the loading card, drawn by the page mid-load (Phase 7a) ---------------------
    {
        // A furniture heavy enough to take several steps: one step per body.
        DocumentModel heavy;
        for (int i = 0; i < 12; ++i)
            heavy.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(i * 120.0, 0.0, 0.0), 100.0, 80.0, 60.0).Shape());
        const QString heavyId = window.furnitureStore().createFurniture(QStringLiteral("Heavy dresser"));
        check(!heavyId.isEmpty() && window.furnitureStore().saveFurniture(heavyId, heavy, QImage()),
              "a twelve-body furniture to open");
        auto* card = window.findChild<LoadingCard*>();
        check(card && card->drawnElsewhere(), "the Qt loading card is drawn by the page while it is live");
        bool sawMid = false, drawn = false;
        QString title, step;
        double share = -1.0;
        QMetaObject::Connection watch;
        if (card)
            watch = QObject::connect(card, &LoadingCard::stateChanged, [&] {
                if (sawMid || !card->isBusy() || card->words().value(QStringLiteral("done")).toInt() < 3) return;
                sawMid = true;
                // Mid-load: the page has this step, and it is on screen.
                // The page catches up to the step the Qt card is on - a message
                // per step, so a page that cannot keep up stays behind for good.
                drawn = waitFor([&] {
                    // One read, so the two halves come from one frame of the page.
                    const QStringList both =
                        jsString(page, "[(document.querySelector('.loading-card .title') || {}).textContent || '', "
                                       "(document.querySelector('.loading-card .step') || {}).textContent || ''].join('|')")
                            .split(QLatin1Char('|'));
                    title = both.value(0);
                    step = both.value(1);
                    return step == card->stepText() && !title.isEmpty();
                }, 3000);
#ifdef _WIN32
                pump(250);
                const QImage withPage = captureImage(&window);
                withPage.save(outDir + "/loading-card.png");
                page->hide();
                pump(250);
                const QImage without = captureImage(&window);
                page->show();
                pump(250);
                const QRect view(window.view()->mapTo(&window, QPoint(0, 0)), window.view()->size());
                const double dpr = window.devicePixelRatioF();
                const QRect r(qRound(view.x() * dpr), qRound(view.y() * dpr), qRound(view.width() * dpr),
                              qRound(view.height() * dpr));
                int moved = 0, all = 0;
                for (int y = r.top(); y <= r.bottom(); y += 2)
                    for (int x = r.left(); x <= r.right(); x += 2) {
                        if (!withPage.rect().contains(x, y) || !without.rect().contains(x, y)) continue;
                        const QColor p1 = withPage.pixelColor(x, y), p2 = without.pixelColor(x, y);
                        ++all;
                        if (std::abs(p1.red() - p2.red()) + std::abs(p1.green() - p2.green()) +
                                std::abs(p1.blue() - p2.blue()) > 9)
                            ++moved;
                    }
                share = all ? double(moved) / all : 0.0;
#endif
            });
        check(window.openFurniture(heavyId), "the heavy furniture opens");
        QObject::disconnect(watch);
        check(sawMid && drawn && title == QStringLiteral("Heavy dresser") && !step.isEmpty(),
              QStringLiteral("mid-load the page draws the card - its name and the step the load is on (\"%1\", \"%2\")").arg(title, step));
#ifdef _WIN32
        check(share > 0.30, QStringLiteral("and the composited window SHOWS it: the wash and the card change %1% of "
                                           "the viewport when the page is hidden")
                                .arg(share * 100, 0, 'f', 1));
#endif
        check(waitFor([&] { return !js(page, "!!document.querySelector('.loading-card')").toBool(); }, 3000),
              "and when the load ends the card goes");
        check(waitFor([&] { return !chrome->hitRegion().contains(window.view()->rect().center()); }, 3000),
              QStringLiteral("and the page gives the viewport back - its middle takes no press (region %1)")
                  .arg(QString::number(chrome->hitRegion().boundingRect().width()) + QLatin1Char('x') +
                       QString::number(chrome->hitRegion().boundingRect().height())));
        window.openFurniture(furnitureId);
        pump(300);
    }

    // --- a page that is not there reports through Qt (review I3) ---------------------
    check(chrome->isLive(), "the page said it is listening");
    {
        // A page that never loads: the Qt toast and status bar carry the report.
        qputenv("FURNIFY_WEB_URL", "file:///C:/furnifyme-no-such-pages");
        MainWindow broken(nullptr, /*persistProgress=*/false, library.path());
        broken.setAttribute(Qt::WA_ShowWithoutActivating);
        broken.resize(1000, 700);
        broken.useHtmlChrome();
        broken.show();
        broken.openFurniture(furnitureId);
        qunsetenv("FURNIFY_WEB_URL");
        waitFor([&] { return broken.htmlChrome() && broken.htmlChrome()->host()->loaded(); }, 10000);
        pump(300);
        check(broken.htmlChrome() && !broken.htmlChrome()->isLive(), "a page that never loaded is not live");
        TopoDS_Face f;
        for (TopExp_Explorer e(broken.document().solids().front().shape, TopAbs_FACE); e.More(); e.Next()) {
            f = TopoDS::Face(e.Current());
            break;
        }
        broken.pullFaceBy(f, -100000.0);
        ToastHost* toasts = broken.findChild<ToastHost*>();
        check(toasts && toasts->isShowing(), "and a Failure is shown by the Qt toast, never nowhere");
        broken.close();
        pump(200);
    }
    // --- the close question as HTML (Phase 3f) -------------------------------------
    UnsavedCloseCard* closeCard = window.findChild<UnsavedCloseCard*>();
    QAction* closeFurniture = actionNamed(&window, QStringLiteral("Close furniture"));
    const auto washShown = [&] { return js(page, "!!document.querySelector('.wash')").toBool(); };
    {
        check(closeCard && closeFurniture, "the editor has its close question and Close furniture");
        // Something to lose: a body nudged, so the furniture is dirty.
        {
            gp_Trsf nudge;
            nudge.SetTranslation(gp_Vec(0, 0, 5));
            window.transformBody(window.document().solids().front().id, nudge);
            pump(200);
        }
        check(window.isFurnitureDirty(), "the furniture has unsaved changes to ask about");
        // The window buttons' brightest pixel, before and while the question
        // stands - the page's wash must leave them as crisp as the Qt card's
        // raise did (measured red first: 52 against 152).
        QWidget* windowButtons = window.buttonsSlot();
        const auto buttonsBrightness = [&](const QString& file) {
            const QImage shot(file);
            if (shot.isNull() || !windowButtons) return -1;
            const double dpr = window.devicePixelRatioF();
            const QRect r(windowButtons->mapTo(&window, QPoint(0, 0)) * dpr, windowButtons->size() * dpr);
            int best = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    if (shot.rect().contains(x, y)) best = std::max(best, qGray(shot.pixel(x, y)));
            return best;
        };
#ifdef _WIN32
        capture(&window, outDir + "/close-question-before.png");
#endif
        if (closeCard && closeFurniture) {
            closeFurniture->trigger();
            // Before the page has drawn a thing: the press is already the
            // page's, the whole viewport (review I3) - no event loop between.
            check(chrome->hitRegion().contains(window.view()->rect().center()),
                  "the moment the question is asked, the page takes every press over the viewport");
            pump(300);
            check(window.isAskingBeforeClose() && closeCard->drawnElsewhere() && !closeCard->saveButton()->isVisible(),
                  "Close furniture asks - the Qt card stands, drawn elsewhere, its own buttons hidden");
            check(waitFor(washShown, 3000) &&
                      jsString(page, "document.querySelector('.wash .title').textContent") ==
                          QStringLiteral("Save changes to Bench before closing?"),
                  "the page draws the question with the Qt card's words");
            check(jsString(page, "[...document.querySelectorAll('.answer .label')].map(n => n.textContent).join('|')") ==
                      QStringLiteral("Save and close|Close without saving|Keep editing"),
                  "its three answers, in order");
            // A press anywhere behind it reaches the page's wash - never the
            // viewport, the rail or a drawer (Review Focus 1).
            const QPoint centre = window.view()->rect().center();
            QWidget* atCentre = window.view()->childAt(centre);
            check(atCentre && (atCentre == page || page->isAncestorOf(atCentre)),
                  "a press on the viewport behind the question lands on the page");
            if (QWidget* rail = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_rail"))) {
                const QPoint onRail = rail->geometry().center();
                QWidget* at = window.view()->childAt(onRail);
                check(at && (at == page || page->isAncestorOf(at)), "and so does a press on the rail");
            }
            check(windowButtonsTakePress(window, page), "the window buttons stay usable over the wash (Review Focus 4)");
            check(js(page, "document.querySelector('.behind').inert").toBool(), "everything else on the page is inert");
#ifdef _WIN32
            capture(&window, outDir + "/close-question.png");
            {
                const int before = buttonsBrightness(outDir + "/close-question-before.png");
                const int during = buttonsBrightness(outDir + "/close-question.png");
                check(before > 0 && during >= before * 0.9,
                      QStringLiteral("the wash leaves the window buttons crisp (brightest %1 against %2 with no question)")
                          .arg(during).arg(before));
            }
#endif
            // No version can be written while it stands - a field blurred as the
            // page goes inert would otherwise commit one (review I1).
            if (VersionsModel* versionsModel = window.versionsModel()) {
                const int before = window.furnitureStore().versions(furnitureId).size();
                check(!versionsModel->canSave() && !versionsModel->save(QStringLiteral("Behind the question")) &&
                          window.furnitureStore().versions(furnitureId).size() == before,
                      "no version is saved while the question stands");
            }
            // The window moves its buttons: the wash's hole follows them (review I2).
            {
                const QSize was = window.size();
                window.resize(was.width() - 120, was.height());
                pump(400);
                QWidget* buttons = shownWindowButtons(window);
                const int x = buttons ? buttons->mapTo(window.view(), QPoint(0, 0)).x() : -1;
                const QString clip = jsString(page, "document.querySelector('.wash').style.clipPath");
                check(buttons && clip.contains(QStringLiteral("%1px").arg(x)),
                      QStringLiteral("a resize while it asks moves the hole with the window buttons (x %1)").arg(x));
                window.resize(was);
                pump(400);
            }
            // Esc still answers - the Qt card's key claim (Review Focus 2).
            {
                QWidget* target = window.focusWidget() ? window.focusWidget() : window.view();
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &esc);
                pump(300);
            }
            check(!window.isAskingBeforeClose() && !washShown() && window.isFurnitureDirty() &&
                      !js(page, "document.querySelector('.behind').inert").toBool(),
                  "Esc keeps editing: the question and its wash go, nothing is saved");
            // Ctrl+Z reaches nothing while it stands.
            closeFurniture->trigger();
            pump(300);
            const int revision = window.document().revision();
            {
                QWidget* target = window.focusWidget() ? window.focusWidget() : window.view();
                QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Z, Qt::ControlModifier);
                QCoreApplication::sendEvent(target, &over);
                QKeyEvent press(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QCoreApplication::sendEvent(target, &press);
                pump(200);
            }
            check(window.isAskingBeforeClose() && window.document().revision() == revision,
                  "Ctrl+Z reaches nothing while the question stands");
            // A click on the page's Keep editing answers through the Qt card.
            waitFor(washShown, 3000);
            js(page, "document.querySelector('.answer.keep').click()");
            check(waitFor([&] { return !window.isAskingBeforeClose(); }, 3000) && window.isFurnitureDirty(),
                  "the page's Keep editing answers - the furniture stays open and unsaved");
            check(waitFor([&] { return !washShown(); }, 3000), "and the wash goes with it");
            // The Qt cards stand over the page again once it has gone - the
            // axis card, since the rail is the page's own (Phase 3i).
            if (AxisGizmo* gizmo = window.findChild<AxisGizmo*>()) {
                QWidget* at = window.view()->childAt(gizmo->geometry().center());
                check(at == gizmo || (at && gizmo->isAncestorOf(at)), "the axis card takes its own presses again");
            }
            // A long name wraps the title rather than widening the card (Review Focus 5).
            closeFurniture->trigger();
            pump(200);
            closeCard->ask(QStringLiteral("A very long furniture name for the hallway cabinet beside the stairs"));
            pump(300);
            const int cardWidth = js(page, "Math.round(document.querySelector('.wash .card').getBoundingClientRect().width)").toInt();
            // Wrapped: the title stands taller than two of its own font sizes
            // ("normal" line height is about 1.2 of it, one line).
            const double titleRatio = js(page, "(() => { const t = document.querySelector('.wash .title'); "
                                               "return t.getBoundingClientRect().height / parseFloat(getComputedStyle(t).fontSize); })()").toDouble();
            check(cardWidth == 380 && titleRatio > 2.0,
                  QStringLiteral("a long name wraps the title in the 380 px card (%1 px, %2 font sizes tall)")
                      .arg(cardWidth).arg(titleRatio, 0, 'f', 2));
            js(page, "document.querySelector('.answer.keep').click()");
            waitFor([&] { return !window.isAskingBeforeClose(); }, 3000);
        }
    }

    // --- the name question as HTML (Phase 3g) --------------------------------------
    NameFurnitureCard* nameCard = window.nameQuestion();
    QAction* newFurniture = actionNamed(&window, QStringLiteral("New furniture"));
    QAction* saveFurniture = actionNamed(&window, QStringLiteral("Save"));
    const auto nameShown = [&] { return js(page, "!!document.querySelector('.name-question input')").toBool(); };
    const auto nameField = [&] { return jsString(page, "(document.querySelector('.name-question input') || {}).value || ''"); };
    // Types into the page's field and, in the same turn, presses `key` there.
    const auto typeThen = [&](const QString& name, const QString& key) {
        js(page, QStringLiteral("(() => { const f = document.querySelector('.name-question input'); f.value = '%1'; "
                                "f.dispatchEvent(new Event('input', {bubbles: true})); %2 })()")
                     .arg(name, key.isEmpty() ? QString()
                                              : QStringLiteral("f.dispatchEvent(new KeyboardEvent('keydown', {key: '%1', bubbles: true}));")
                                                    .arg(key)));
    };
    {
        check(nameCard && newFurniture && saveFurniture, "the editor has its name question, New furniture and Save");
        if (nameCard && newFurniture && saveFurniture) {
            const QString benchId = window.currentFurnitureId();
            // A claim for a question that is not asked holds nothing (review
            // M2): a late page claim would otherwise kill every shortcut.
            chrome->bridge()->claimKeyboardFor(QStringLiteral("question"));
            check(!chrome->holdsKeyboard(), "a keyboard claim for a name question that is not asked is refused");
            chrome->bridge()->releaseKeyboardFor(QStringLiteral("question"));
            // Unsaved changes: the close question first, then the name question,
            // each drawn by the page in turn (Review Focus 5).
            check(window.isFurnitureDirty(), "the furniture still has unsaved changes to ask about");
            newFurniture->trigger();
            pump(300);
            check(window.isAskingBeforeClose() && waitFor(washShown, 3000) &&
                      js(page, "!!document.querySelector('.answer.save')").toBool(),
                  "New furniture with unsaved changes asks the close question first, on the page");
            js(page, "document.querySelector('.answer.save').click()");
            check(waitFor([&] { return window.isAskingForName(); }, 3000) && !window.isAskingBeforeClose() &&
                      !window.isFurnitureDirty(),
                  "Save and close saves, then hands over to the name question");
            check(waitFor(nameShown, 3000) && !js(page, "!!document.querySelector('.answer.save')").toBool(),
                  "which the page draws in the close question's place");
            check(nameCard->drawnElsewhere() && !nameCard->field()->isVisible() && !nameCard->createButton()->isVisible(),
                  "the Qt card stands, drawn elsewhere, its own field and buttons hidden");
            const QString suggestion = window.furnitureStore().nextFurnitureName();
            check(jsString(page, "document.querySelector('.name-question .title').textContent") ==
                          QStringLiteral("Name this furniture") &&
                      nameField() == suggestion,
                  "with the Qt card's words and the suggested name - " + nameField());
            check(waitFor([&] { return chrome->holdsKeyboard(); }, 2000), "its field takes the keyboard");
            // The window losing activation blurs the field; the keyboard stays
            // the field's, or typing never came back (review I1).
            js(page, "document.querySelector('.name-question input').dispatchEvent(new FocusEvent('blur'))");
            pump(200);
            check(chrome->holdsKeyboard(), "a blur - the window losing activation - leaves the keyboard with the field");
            // A press anywhere behind it lands on the page; the window buttons
            // keep theirs (the close question's wash, shared).
            QWidget* atCentre = window.view()->childAt(window.view()->rect().center());
            check(atCentre && (atCentre == page || page->isAncestorOf(atCentre)),
                  "a press on the viewport behind the name question lands on the page");
            if (QWidget* rail = window.railSlot()) {
                QWidget* at = window.view()->childAt(rail->geometry().center());
                check(at && (at == page || page->isAncestorOf(at)), "and so does a press on the rail");
            }
            if (QWidget* buttons = shownWindowButtons(window)) {
                check(windowButtonsTakePress(window, page), "the window buttons stay usable");
                const int x = buttons->mapTo(window.view(), QPoint(0, 0)).x();
                check(jsString(page, "document.querySelector('.wash').style.clipPath").contains(QStringLiteral("%1px").arg(x)),
                      "and the wash leaves them clear");
            }
            check(js(page, "document.querySelector('.behind').inert").toBool(), "everything else on the page is inert");
#ifdef _WIN32
            capture(&window, outDir + "/name-question.png");
#endif
            // Every key aimed at the page goes THROUGH the Qt card's claim -
            // Enter and Escape too - while no window shortcut fires (Review
            // Focus 2). A filter on the page's render widget counts what reaches
            // it; the card's claim, an application-wide filter, runs first.
            {
                struct KeyCounter : QObject {
                    int presses = 0;
                    bool eventFilter(QObject*, QEvent* e) override
                    {
                        if (e->type() == QEvent::KeyPress) ++presses;
                        return false;
                    }
                } counter;
                // Blank first: should Chromium act on the Enter, it creates nothing.
                typeThen(QString(), QString());
                QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
                proxy->installEventFilter(&counter);
                for (int key : {int(Qt::Key_B), int(Qt::Key_Return), int(Qt::Key_Escape)}) {
                    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
                    QCoreApplication::sendEvent(proxy, &press);
                }
                proxy->removeEventFilter(&counter);
                check(counter.presses == 3,
                      QStringLiteral("keys aimed at the page's field reach it, Enter and Escape too (%1 of 3)").arg(counter.presses));
                const int solids = static_cast<int>(window.document().solids().size());
                const int revision = window.document().revision();
                for (QWidget* target : {proxy, static_cast<QWidget*>(window.view())}) {
                    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_D, Qt::ControlModifier);
                    over.ignore();
                    QCoreApplication::sendEvent(target, &over);
                    check(over.isAccepted(), "Ctrl+D is claimed before any window shortcut, wherever it is aimed");
                }
                check(static_cast<int>(window.document().solids().size()) == solids &&
                          window.document().revision() == revision,
                      "so nothing was duplicated and the document did not move");
                pump(200);
            }
            // Should a synthesized key have answered on the page, ask again.
            if (!window.isAskingForName()) {
                newFurniture->trigger();
                pump(300);
            }
            waitFor(nameShown, 3000);
            // No version can be written behind it either.
            if (VersionsModel* versionsModel = window.versionsModel()) {
                const int before = window.furnitureStore().versions(benchId).size();
                check(!versionsModel->canSave() && !versionsModel->save(QStringLiteral("Behind the name")) &&
                          window.furnitureStore().versions(benchId).size() == before,
                      "no version is saved while the name question stands");
            }
            // Each edit is mirrored into the Qt card's own field.
            typeThen(QStringLiteral("Bookcase"), QString());
            check(waitFor([&] { return nameCard->name() == QStringLiteral("Bookcase"); }, 2000),
                  "an edit on the page reaches the Qt card's field - " + nameCard->name());
            // A blank name creates nothing (Review Focus 3).
            typeThen(QStringLiteral("   "), QStringLiteral("Enter"));
            pump(300);
            check(window.isAskingForName() && window.currentFurnitureId() == benchId &&
                      js(page, "document.querySelector('.name-question .answer.create').disabled").toBool(),
                  "a blank name creates nothing - Create dims and Enter does nothing");
            // Escape on the page cancels.
            typeThen(QStringLiteral("Not this one"), QStringLiteral("Escape"));
            check(waitFor([&] { return !window.isAskingForName(); }, 3000) && window.currentFurnitureId() == benchId,
                  "Escape cancels - the furniture stays open");
            check(waitFor([&] { return !nameShown() && !chrome->holdsKeyboard(); }, 3000) &&
                      !js(page, "document.querySelector('.behind').inert").toBool(),
                  "the question goes and gives the keyboard back");
            // Asked again with nothing unsaved: straight to the name, and the
            // press is the page's before it has drawn a thing.
            newFurniture->trigger();
            check(window.isAskingForName() && chrome->hitRegion().contains(window.view()->rect().center()),
                  "asked again, the page takes every press over the viewport at once");
            // And the keyboard: letters typed before the page has drawn go to
            // the page, never to the Qt claim that swallows them (review M1).
            {
                QWidget* proxy = page->focusProxy() ? page->focusProxy() : page;
                check(chrome->holdsKeyboard() && window.focusWidget() == proxy,
                      QStringLiteral("and the keyboard at once, before the page has drawn (focus on %1)")
                          .arg(window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none"));
            }
            check(waitFor(nameShown, 3000) && nameField() == suggestion,
                  "and the page re-seeds the field with the suggestion, not what was typed before");
            js(page, "document.querySelector('.name-question .answer.cancel').click()");
            check(waitFor([&] { return !window.isAskingForName(); }, 3000), "the page's Cancel cancels");
            // The Qt cards stand over the page again once it has gone - the
            // axis card, since the rail is the page's own (Phase 3i).
            if (AxisGizmo* gizmo = window.findChild<AxisGizmo*>()) {
                QWidget* at = window.view()->childAt(gizmo->geometry().center());
                check(at == gizmo || (at && gizmo->isAncestorOf(at)), "the axis card takes its own presses again");
            }
            // Typed and Enter in one turn: the furniture is made under the name
            // the field holds (Review Focus 1).
            newFurniture->trigger();
            waitFor(nameShown, 3000);
            typeThen(QStringLiteral("Hall bench"), QStringLiteral("Enter"));
            check(waitFor([&] { return window.currentFurnitureName() == QStringLiteral("Hall bench"); }, 5000) &&
                      window.currentFurnitureId() != benchId && !window.isAskingForName(),
                  "Enter creates the furniture under the name typed and opens it - " + window.currentFurnitureName());
            check(waitFor([&] { return !chrome->holdsKeyboard() && !nameShown(); }, 3000),
                  "and the keyboard and the wash go with the question");
            // The answer's own name is the one used - never the mirror, which a
            // page's ordered messages would otherwise always have updated first
            // (review M3): a name the mirror never saw.
            newFurniture->trigger();
            waitFor(nameShown, 3000);
            typeThen(QStringLiteral("Mirrored"), QString());
            waitFor([&] { return nameCard->name() == QStringLiteral("Mirrored"); }, 2000);
            chrome->bridge()->answerNameQuestion(QStringLiteral("create"), QStringLiteral("Side table"));
            check(waitFor([&] { return window.currentFurnitureName() == QStringLiteral("Side table"); }, 5000),
                  "the answer creates under the name it carries, not the mirrored one - " + window.currentFurnitureName());
            // Back to the bench, with something unsaved again for the blocks below.
            window.openFurniture(benchId);
            pump(300);
            gp_Trsf nudge;
            nudge.SetTranslation(gp_Vec(0, 0, 5));
            window.transformBody(window.document().solids().front().id, nudge);
            pump(200);
            check(window.currentFurnitureId() == benchId && window.isFurnitureDirty(),
                  "the bench is open again, with unsaved changes");
        }
    }

    // --- the render settings panel as a page (Phase 3e) -----------------------------
    {
        RenderStudio* studio = window.findChild<RenderStudio*>();
        RenderPage* renderPage = window.findChild<RenderPage*>();
        check(studio && renderPage, "the editor carries the render page beside its studio");
        QAction* renderMode = actionNamed(&window, QStringLiteral("Render mode"));
        check(renderMode != nullptr, "the editor has Render mode");
        if (studio && renderPage && renderMode) {
            auto* rv = static_cast<QWebEngineView*>(renderPage->view());
            RenderSettingsPanel* panel = studio->panel();
            check(waitFor([&] { return renderPage->isLive(); }, 20000), "the render page loads and says it is listening");
            check(!rv->isVisible() && studio->dockedPanel() == nullptr, "and stays out of sight outside render mode");
            // T-083: a render-mode session before this one. With the pages
            // GPU-composited, the second session's theme switch below (or a
            // quality change) died in the NVIDIA driver - every time, in this
            // sequence. WebHost::prepareApplication() composites the pages in
            // software; this session keeps the sequence that crashed.
            check(qEnvironmentVariable("QTWEBENGINE_CHROMIUM_FLAGS").contains(QStringLiteral("--disable-gpu-compositing")),
                  "Chromium composites the pages in software, so no Direct3D frame is handed to OpenGL (T-083) - " +
                      qEnvironmentVariable("QTWEBENGINE_CHROMIUM_FLAGS"));
            renderMode->trigger();
            pump(300);
            renderMode->trigger();
            pump(300);
            check(!renderMode->isChecked(), "a first render-mode session has come and gone");
            renderMode->trigger();
            pump(600);
            check(studio->dockedPanel() == rv && rv->isVisible() && !panel->isVisible(),
                  "render mode docks the PAGE beside the viewport, not the Qt panel");
            {
                // The rail and the pill leave with render mode (Phase 3i) - the
                // page's, and the Qt ones stay hidden behind them.
                auto* railSlot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_rail"));
                auto* pillSlot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_pill"));
                check(railSlot && pillSlot && !railSlot->isVisible() && !pillSlot->isVisible() &&
                          waitFor([&] { return !js(page, "!!document.querySelector('[data-cluster=\"rail\"]') || !!document.querySelector('[data-pill]')").toBool(); }, 3000),
                      "render mode takes the page's rail and pill away");
            }
            check(rv->width() == panel->minimumWidth() && rv->height() > 400,
                  QStringLiteral("at the Qt panel's own width, full height (%1 x %2)").arg(rv->width()).arg(rv->height()));
            check(waitFor([&] { return js(rv, "!!document.querySelector('.panel .title')").toBool(); }, 5000) &&
                      jsString(rv, "document.querySelector('.panel .title').textContent") == QStringLiteral("Render settings"),
                  "the page draws the panel, titled as the Qt one");
            const QString headings =
                jsString(rv, "[...document.querySelectorAll('.scroll .heading')].map(h => h.textContent).join('|')");
            check(headings.startsWith(QStringLiteral("Frame|Shots|Light|")) &&
                      headings.endsWith(QStringLiteral("Studio|Camera|Quality")),
                  "its sections in the Qt panel's order - " + headings);

            // A chip click reaches the panel, and the viewport behind it.
            js(rv, "document.querySelectorAll('.qualities .chip')[0].click()");
            check(waitFor([&] { return panel->quality() == RenderSettingsPanel::Quality::Simple; }, 3000) &&
                      window.view()->renderQuality() == OcctViewWidget::RenderQuality::Simple,
                  "Simple on the page sets the panel's quality and the viewport's");
            check(waitFor([&] {
                      return js(rv, "document.querySelectorAll('.qualities .chip')[0].classList.contains('current')").toBool();
                  }, 3000),
                  "and the page marks it current");

            // A slider drag, live.
            js(rv, "(() => { const i = document.querySelector('.dial[data-dial=\"lightAngle\"] input'); i.value = '123';"
                   " i.dispatchEvent(new Event('input', {bubbles: true})); })()");
            check(waitFor([&] { return panel->dialValue(RenderSettingsPanel::Dial::LightAngle) == 123; }, 3000) &&
                      std::abs(window.view()->renderLightAngleDeg() - 123.0) < 0.5,
                  "dragging Angle on the page turns the studio light");
            check(waitFor([&] {
                      return jsString(rv, "document.querySelector('.dial[data-dial=\"lightAngle\"] .readout').textContent") ==
                             QString::fromUtf8("123\u00b0");
                  }, 3000),
                  "and its readout is the panel's own");

            // Save this view: a shot the page lists.
            const int shotsBefore = js(rv, "document.querySelectorAll('.shot').length").toInt();
            js(rv, "document.querySelector('.save-shot').click()");
            check(waitFor([&] { return js(rv, "document.querySelectorAll('.shot').length").toInt() == shotsBefore + 1; }, 3000),
                  "Save this view on the page adds a shot row to it");

            // The woods: the folder's tiles, and a click that wears one.
            const QStringList tiles = panel->materialTileNames();
            check(js(rv, "document.querySelectorAll('.tile').length").toInt() == tiles.size() && !tiles.isEmpty(),
                  QStringLiteral("the page shows every wood tile the Qt panel has (%1)").arg(tiles.size()));
            QString other;
            for (const QString& name : tiles)
                if (name != panel->woodSelection()) other = name;
            if (!other.isEmpty()) {
                js(rv, QStringLiteral("document.querySelector('.tile[data-tile=\"%1\"]').click()").arg(other));
                check(waitFor([&] { return panel->woodSelection() == other && panel->wood(); }, 3000),
                      "a tile click on the page wears that wood - " + other);
            }

            // The shutter mirrors Save Screenshot.
            if (QAction* shoot = panel->shutter() ? panel->shutter()->action() : nullptr) {
                const bool disabled = js(rv, "document.querySelector('.shutter').disabled").toBool();
                check(disabled == !shoot->isEnabled(), "the shutter is enabled exactly when Save Screenshot is");
                check(jsString(rv, "document.querySelector('.shutter span').textContent") ==
                          QString(shoot->text()).remove(QLatin1Char('&')).remove(QStringLiteral("...")),
                      "and wears its words");
            }

            // A theme switch while render mode is on (Review Focus 3).
            std::printf("[info] render tier %d (0 path tracing, 1 ray tracing, 2 shadows, 3 plain)\n",
                        int(window.view()->renderModeTier()));
            const QString before = Themes::currentId();
            Themes::select(QStringLiteral("violet-glass-float"));
            pump(700);
#ifdef _WIN32
            capture(&window, outDir + "/render-panel-violet.png");
#endif
            check(js(rv, "document.querySelectorAll('.qualities .chip.current').length").toInt() == 1 &&
                      js(rv, "document.documentElement.dataset.theme").toString() == QStringLiteral("violet-glass-float"),
                  "a theme switch re-styles the page and keeps its marks");
            Themes::select(QStringLiteral("ultraviolet-grid"));
            pump(700);
#ifdef _WIN32
            capture(&window, outDir + "/render-panel-ultraviolet.png");
#endif
            Themes::select(before);
            pump(300);

#ifdef _WIN32
            // The page dies mid-render-mode (Review Focus 1): the Qt panel docks
            // back at once, every value intact, and render mode stays on. The
            // reload (Phase 6) waits until the fallback has been read.
            renderPage->host()->setReloadOnCrash(false);
            const qint64 pid = rv->page()->renderProcessPid();
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
                TerminateProcess(h, 1);
                CloseHandle(h);
            }
            check(waitFor([&] { return !renderPage->isLive(); }, 5000), "a render page whose process died is not live");
            pump(300);
            check(studio->dockedPanel() == panel && panel->isVisible() && !rv->isVisible() && renderMode->isChecked(),
                  QStringLiteral("the Qt panel docks back at once, and render mode stays on (docked %1, shown %2, page hidden %3, on %4)")
                      .arg(studio->dockedPanel() == panel).arg(panel->isVisible()).arg(!rv->isVisible()).arg(renderMode->isChecked()));
            check(panel->quality() == RenderSettingsPanel::Quality::Simple &&
                      panel->dialValue(RenderSettingsPanel::Dial::LightAngle) == 123,
                  "with every value the page set");
            capture(&window, outDir + "/render-panel-fallback.png");
            // ...and then the page loads again and takes the dock back.
            renderPage->host()->setReloadOnCrash(true);
            check(waitFor([&] { return renderPage->isLive(); }, 20000) && renderPage->host()->crashReloads() == 1 &&
                      studio->dockedPanel() != panel && rv->isVisible(),
                  "the dead render page loads again and docks back in the Qt panel's place");
#endif
            panel->chooseQuality(RenderSettingsPanel::Quality::Deep);
            renderMode->trigger();
            pump(400);
            check(studio->dockedPanel() == nullptr && !renderMode->isChecked(), "leaving render mode undocks it");
            {
                auto* railSlot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_rail"));
                auto* pillSlot = window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_pill"));
                check(railSlot && pillSlot && railSlot->isVisible() && pillSlot->isVisible() &&
                          waitFor([&] { return js(page, "document.querySelectorAll('[data-cluster=\"rail\"] [data-chip]').length").toInt() == 10; }, 3000),
                      "leaving render mode brings the page's rail and pill back");
            }
        }
    }

    // --- the scene window's render page (Review Focus 5) --------------------------
    {
        SceneWindow scene(&window.furnitureStore());
        scene.setAttribute(Qt::WA_ShowWithoutActivating);
        scene.resize(1200, 800);
        RenderPage* scenePage = RenderPage::attach(scene.studio(), &scene);
        scene.show();
        scene.view()->setAnimationsEnabled(false);
        check(scenePage && waitFor([&] { return scenePage->isLive(); }, 20000), "the scene window's render page loads");
        QAction* sceneRender = actionNamed(&scene, QStringLiteral("Render mode"));
        if (scenePage && sceneRender) {
            sceneRender->trigger();
            pump(600);
            check(scene.studio()->dockedPanel() == scenePage->view() && scenePage->view()->isVisible() &&
                      !scene.studio()->panel()->isVisible(),
                  "the scene's render mode docks the page too");
            auto* sv = static_cast<QWebEngineView*>(scenePage->view());
            waitFor([&] { return js(sv, "!!document.querySelector('.panel .title')").toBool(); }, 5000);
            const QString headings =
                jsString(sv, "[...document.querySelectorAll('.scroll .heading')].map(h => h.textContent).join('|')");
            check(scene.studio()->panel()->materialTileNames().isEmpty() && !headings.contains(QStringLiteral("Material")) &&
                      headings.contains(QStringLiteral("Studio")),
                  "and a scene, whose pieces wear their own woods, shows no Material section - " + headings);
#ifdef _WIN32
            capture(&scene, outDir + "/render-panel-scene.png");
#endif
            sceneRender->trigger();
            pump(300);
        }
        scene.hide();
        pump(200);
    }

    // --- the scene window's chrome as the editor's page (Phase 5a) -------------------
    {
        SceneWindow scene(&window.furnitureStore());
        scene.setAttribute(Qt::WA_ShowWithoutActivating);
        scene.resize(1200, 800);
        scene.useHtmlChrome();
        scene.show();
        scene.view()->setAnimationsEnabled(false);
        EditorChrome* sceneChrome = scene.htmlChrome();
        check(sceneChrome && waitFor([&] { return sceneChrome->isLive(); }, 20000),
              "the scene window's chrome page loads and says it is listening");
        if (sceneChrome && sceneChrome->isLive()) {
            QWebEngineView* sp = sceneChrome->view();
            pump(400);
            check(waitFor([&] { return jsString(sp, "(document.querySelector('.strip .left') || {}).textContent || ''") ==
                                           scene.statusLine()->message(); }, 3000),
                  "the Qt status bar is hidden, and the page's strip says what it said - \"" +
                      scene.statusLine()->message() + "\"");
            check(jsString(sp, "(document.querySelector('.strip .right') || {}).textContent || ''") ==
                      scene.statusLine()->state(),
                  "and its right channel is the scene's next step");
            check(waitFor([&] { return js(sp, "!!document.querySelector('[data-pill]')").toBool(); }, 3000),
                  "the page draws the pill");
            check(waitFor([&] { return js(sp, "document.querySelectorAll('[data-chip]').length").toInt() == 3; }, 3000),
                  "the page draws Move, Rotate and Render mode");
            QAction* rotate = actionNamed(&scene, QStringLiteral("Rotate"));
            QAction* move = actionNamed(&scene, QStringLiteral("Move"));
            const QString rotateId = rotate ? ActionRegistry::idFor(rotate->text()) : QString();
            js(sp, QStringLiteral("document.querySelector('[data-chip=\"%1\"]').click()").arg(rotateId));
            check(rotate && waitFor([&] { return rotate->isChecked(); }, 2000), "the page's Rotate chip picks Rotate");
            if (move) move->trigger();
            pump(150);
            // The menus: the scene's own, opened on the page.
            {
                const int titles = js(sp, "document.querySelectorAll('[data-pill] [data-title]').length").toInt();
                const int menus = int(scene.menus().size());
                check(menus > 0 && titles == menus,
                      QStringLiteral("the page's pill carries the scene's own %1 menus (%2 titles)").arg(menus).arg(titles));
            }
            scene.shellBridge()->menu()->openMenu(0, false);
            check(waitFor([&] { return js(sp, "!!document.querySelector('.menu')").toBool(); }, 3000) &&
                      sceneChrome->menuOpen(),
                  "a menu opens on the page over the scene, and owns the pointer");
            scene.shellBridge()->menu()->close();
            pump(200);
            // A Failure, drawn by the page: an empty scene refuses a picture.
            QAction* shot = actionNamed(&scene, QStringLiteral("Save Screenshot"));
            if (shot) shot->trigger();
            check(shot && waitFor([&] { return toastText(sp).contains(QStringLiteral("empty")); }, 3000),
                  "an empty scene's screenshot refusal is a Failure on the page - " + toastText(sp));
            check(scene.statusLine()->message().contains(QStringLiteral("empty")),
                  "and its reason stays on the status line after it");
            // One toast since Phase 7b: the model the page draws from.
            check(scene.toasts()->isShowing() && scene.toasts()->kind() == Toast::Kind::Failure &&
                      scene.toasts()->currentText().trimmed() == toastText(sp).trimmed(),
                  "and the toast model holds the same Failure the page shows");

            // The window buttons, drawn by the scene's page too (Phase 7a).
            {
                auto* slot = scene.findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_buttons"));
                check(slot && slot->isVisible() &&
                          waitFor([&] { return js(sp, "document.querySelectorAll('.window-buttons .win').length").toInt() == 3; }, 3000),
                      "scene buttons: the scene's page draws the window buttons in their place");
            }
            // Render mode's Back chip, drawn by the scene's page too (Phase 7a).
            if (QAction* sceneRender = actionNamed(&scene, QStringLiteral("Render mode"))) {
                sceneRender->trigger();
                pump(500);
                check(sceneRender->isChecked() &&
                          waitFor([&] { return js(sp, "!!document.querySelector('[data-cluster=\"back\"] [data-chip]')").toBool(); }, 3000),
                      "scene back: in render mode the scene's page draws the Back chip");
                js(sp, "document.querySelector('[data-cluster=\"back\"] [data-chip]').click()");
                check(waitFor([&] { return !sceneRender->isChecked(); }, 3000), "scene back: and it leaves render mode");
                if (sceneRender->isChecked()) sceneRender->trigger();
                pump(300);
            }

            // --- the Pieces card and the add card (Phase 5b) ----------------------------
            check(waitFor([&] { return js(sp, "!!document.querySelector('[data-scene=\"pieces\"]')").toBool(); }, 3000) &&
                      !scene.piecesPanel()->isVisible(),
                  "the page draws the Pieces card in its slot, and the Qt card steps aside");
            check(jsString(sp, "(document.querySelector('[data-scene=\"pieces\"] .empty') || {}).textContent || ''") ==
                      ScenePiecesPanel::emptyText(),
                  "an empty scene's card says so in the Qt card's words");
            js(sp, "document.querySelector('[data-scene=\"pieces\"] .add').click()");
            check(waitFor([&] { return js(sp, "!!document.querySelector('[data-scene=\"add\"]')").toBool(); }, 3000) &&
                      !scene.addPieceCard()->isVisible(),
                  "its Add piece row opens the page's add card, never the Qt one");
            check(waitFor([&] { return sceneChrome->claimsKeyboardFor(QStringLiteral("addPiece")); }, 2000),
                  "and the add card holds the keyboard, so its Escape reaches it");
#ifdef _WIN32
            pump(250);
            capture(&scene, outDir + "/scene-add-card.png");
#endif
            js(sp, QStringLiteral("document.querySelector('[data-entry=\"%1\"]').click()").arg(furnitureId));
            check(waitFor([&] { return scene.scene().pieces().size() == 1; }, 5000) &&
                      waitFor([&] { return js(sp, "document.querySelectorAll('[data-piece]').length").toInt() == 1; }, 3000),
                  "choosing the bench puts one piece in the scene, and the card lists it");
            check(!js(sp, "!!document.querySelector('[data-scene=\"add\"]')").toBool() &&
                      !sceneChrome->claimsKeyboardFor(QStringLiteral("addPiece")),
                  "the add card closes and gives the keyboard back");
            const int pieceId = scene.scene().pieces().front().id;
            const QString row = QStringLiteral("[data-piece=\"%1\"]").arg(pieceId);
            js(sp, QStringLiteral("document.querySelector('%1 .eye').click()").arg(row));
            check(waitFor([&] {
                      const std::vector<int> bodies = scene.bodyIdsForPiece(pieceId);
                      return !bodies.empty() && !scene.view()->isSolidVisible(bodies.front()) &&
                             jsString(sp, QStringLiteral("document.querySelector('%1 .eye').getAttribute('aria-pressed')").arg(row)) ==
                                 QStringLiteral("false");
                  }, 3000),
                  "the eye hides the piece, and the card says it is hidden");
            js(sp, QStringLiteral("document.querySelector('%1 .eye').click()").arg(row));
            pump(200);
            js(sp, QStringLiteral("document.querySelector('%1 .name').dispatchEvent(new MouseEvent('dblclick', {bubbles: true, detail: 2}))").arg(row));
            pump(200);
            js(sp, QStringLiteral("(() => { const f = document.querySelector('%1 input'); f.value = 'Long bench'; "
                                  "f.dispatchEvent(new Event('input', {bubbles: true})); "
                                  "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()").arg(row));
            check(waitFor([&] { return QString::fromStdString(scene.scene().pieces().front().name) == QStringLiteral("Long bench"); }, 3000),
                  "a double-click renames the piece in place");
#ifdef _WIN32
            pump(250);
            capture(&scene, outDir + "/scene-pieces.png");
#endif
            js(sp, QStringLiteral("document.querySelector('%1 .remove').click()").arg(row));
            check(waitFor([&] { return jsString(sp, QStringLiteral("(document.querySelector('%1 .remove') || {}).textContent || ''").arg(row)).trimmed() ==
                                       ScenePiecesPanel::armedRemoveText(); }, 2000) &&
                      scene.scene().pieces().size() == 1,
                  "the first click on Remove arms it and takes nothing out");
            js(sp, QStringLiteral("document.querySelector('%1 .remove').click()").arg(row));
            check(waitFor([&] { return scene.scene().pieces().empty() &&
                                       js(sp, "document.querySelectorAll('[data-piece]').length").toInt() == 0; }, 3000),
                  "the second takes the piece out, on the page too");
#ifdef _WIN32
            {
                // THE PAGE IS SEEN (T-083's rule): where the pill stands, the
                // window composited with the page differs from it without.
                pump(300);
                const QImage withPage = captureImage(&scene);
                withPage.save(outDir + "/scene-chrome.png");
                sp->hide();
                pump(300);
                const QImage without = captureImage(&scene);
                sp->show();
                pump(300);
                const double dpr = scene.devicePixelRatioF();
                QWidget* pill = scene.findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_pill"));
                const QRect logical = pill ? QRect(scene.view()->mapTo(&scene, pill->pos()), pill->size()) : QRect();
                const QRect r(qRound(logical.x() * dpr), qRound(logical.y() * dpr), qRound(logical.width() * dpr),
                              qRound(logical.height() * dpr));
                int moved = 0, all = 0;
                for (int y = r.top(); y <= r.bottom(); ++y)
                    for (int x = r.left(); x <= r.right(); ++x) {
                        if (!withPage.rect().contains(x, y) || !without.rect().contains(x, y)) continue;
                        const QColor p1 = withPage.pixelColor(x, y), p2 = without.pixelColor(x, y);
                        ++all;
                        if (std::abs(p1.red() - p2.red()) + std::abs(p1.green() - p2.green()) +
                                std::abs(p1.blue() - p2.blue()) > 9)
                            ++moved;
                    }
                const double share = all ? double(moved) / all : 0.0;
                check(share > 0.05, QStringLiteral("the composited scene window SHOWS the page's pill (%1% of its pixels "
                                                   "change when the page is hidden)")
                                        .arg(share * 100, 0, 'f', 1));
            }
#endif
        }
        scene.hide();
        pump(200);
    }

#ifdef _WIN32
    {
        // A render process that dies: the same fallback, from then on - and
        // with the close question standing when it does (Phase 3f, Review
        // Focus 3), the Qt card paints it again at once. Under it stands the
        // name question with a name typed on the page (Phase 3g, Review Focus
        // 4): the window's X can ask the close question over a name question.
        bool namingBeforeDeath = false;
        if (nameCard && newFurniture && saveFurniture) {
            saveFurniture->trigger();
            pump(300);
            newFurniture->trigger();
            namingBeforeDeath = waitFor(nameShown, 3000);
            typeThen(QStringLiteral("Typed before"), QString());
            waitFor([&] { return nameCard->name() == QStringLiteral("Typed before"); }, 2000);
            gp_Trsf nudge;
            nudge.SetTranslation(gp_Vec(0, 0, 5));
            window.transformBody(window.document().solids().front().id, nudge);
            pump(200);
        }
        if (closeFurniture) closeFurniture->trigger();
        pump(300);
        const bool askedBeforeDeath = window.isAskingBeforeClose();
        if (namingBeforeDeath) {
            check(askedBeforeDeath && window.isAskingForName() &&
                      waitFor([&] { return js(page, "!!document.querySelector('.answer.save')").toBool(); }, 3000),
                  "the close question stands over the name question, and the page draws the close one");
            // An edit already in flight as the close question took over is
            // still the name's (review M4).
            chrome->bridge()->nameQuestionEdited(QStringLiteral("Typed before, more"));
            check(nameCard->name() == QStringLiteral("Typed before, more"),
                  "an edit arriving under the close question still reaches the name - " + nameCard->name());
        }
        // The reload (Phase 6) waits until the fallback below has been read.
        chrome->host()->setReloadOnCrash(false);
        const qint64 pid = page->page()->renderProcessPid();
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
        waitFor([&] { return !chrome->isLive(); }, 5000);
        check(!chrome->isLive(), "a page whose render process died is no longer live");
        pump(200);
        if (closeCard && askedBeforeDeath) {
            check(window.isAskingBeforeClose() && !closeCard->drawnElsewhere() && closeCard->saveButton()->isVisible(),
                  "the question still stands, and the Qt card draws it again");
            QWidget* at = window.view()->childAt(closeCard->saveButton()->geometry().center());
            check(at == closeCard->saveButton(), "its own Save and close takes the press");
#ifdef _WIN32
            capture(&window, outDir + "/close-question-fallback.png");
#endif
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(window.focusWidget() ? window.focusWidget() : window.view(), &esc);
            pump(200);
        }
        if (nameCard && namingBeforeDeath) {
            check(window.isAskingForName() && !window.isAskingBeforeClose() && !nameCard->drawnElsewhere() &&
                      nameCard->field()->isVisible() && nameCard->name() == QStringLiteral("Typed before, more"),
                  "the name question still stands under it, drawn by the Qt card, holding what was typed - " +
                      nameCard->name());
            QWidget* at = window.view()->childAt(nameCard->createButton()->geometry().center());
            check(at == nameCard->createButton(), "its own Create takes the press");
#ifdef _WIN32
            capture(&window, outDir + "/name-question-fallback.png");
#endif
            nameCard->field()->setFocus();
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(nameCard->field(), &esc);
            pump(200);
            check(!window.isAskingForName(), "and Escape cancels it there");
        }
        check(!page->isVisible(), "the dead page leaves the viewport");
        // With no page there is no chrome (Phase 7b, ruling R1): the slots
        // keep their places, painting nothing, until the page is back.
        check(!window.shellBridge()->menu()->isOpen(), "and no menu stands open");

        TopoDS_Face live;
        for (TopExp_Explorer e(window.document().solids().front().shape, TopAbs_FACE); e.More(); e.Next()) {
            live = TopoDS::Face(e.Current());
            break;
        }
        window.pullFaceBy(live, -100000.0);
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts && toasts->isShowing(), "and the next Failure is shown by the Qt toast");
        {
            // Where the page's strip was - always in its hit region while it
            // lived - a wheel zooms again.
            const double distance = window.view()->camera().state().distance;
            const QPoint at(window.view()->width() / 2, window.view()->height() - 8);
            QWheelEvent wheel(QPointF(at), QPointF(window.view()->mapToGlobal(at)), QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window.view(), &wheel);
            pump(150);
            check(std::abs(window.view()->camera().state().distance - distance) > 1e-6,
                  "and a wheel where the strip was zooms again (review I3)");
            check(chrome->hitRegion().isEmpty(),
                  "a dead page keeps no part of the viewport for itself (review I3)");
        }

        // --- and then the page comes back (Phase 6): a dead page reloads -------
        chrome->host()->setReloadOnCrash(true);
        check(waitFor([&] { return chrome->isLive(); }, 20000) && chrome->host()->crashReloads() == 1,
              "the dead page loads again and says it is listening");
        pump(300);
        check(page->isVisible(), "it stands over the viewport again");
        check(window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_rail")) &&
                  window.view()->findChild<HtmlCardSlot*>(QStringLiteral("htmlCardSlot_rail"))->isVisible() &&
                  waitFor([&] { return js(page, "document.querySelectorAll('[data-cluster=\"rail\"] [data-chip]').length").toInt() == 10; }, 5000),
              "and the page's rail is back in its place");
    }
#endif

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
