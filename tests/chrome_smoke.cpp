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
#include "ui/AppearancePanel.h"
#include "ui/VersionsPanel.h"
#include "session/VersionsModel.h"
#include "session/JointsModel.h"
#include "ui/JointsPanel.h"
#include "ui/GestureChip.h"
#include "ui/SettingsEdits.h"
#include "ui/Themes.h"
#include "ui/ItemsPanel.h"
#include "ui/Toast.h"
#include "web/EditorChrome.h"
#include "web/RenderPage.h"
#include "ui/RenderStudio.h"
#include "ui/SceneWindow.h"
#include "ui/UnsavedCloseCard.h"
#include "ui/NameFurnitureCard.h"
#include "ui/WindowChrome.h"
#include "ui/ToolCluster.h"
#include "ui/RenderSettingsPanel.h"
#include "web/WebHost.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColorDialog>
#include <QKeyEvent>
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

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

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

QAction* actionNamed(QWidget* window, const QString& text)
{
    for (QAction* a : window->findChildren<QAction*>())
        if (QString(a->text()).remove(QLatin1Char('&')) == text) return a;
    return nullptr;
}

#ifdef _WIN32
void capture(QWidget* window, const QString& path)
{
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
            QImage(static_cast<const uchar*>(bits), w, h, QImage::Format_RGB32).copy().save(path);
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
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
    check(!window.statusBar()->isVisible(), "the Qt status bar is hidden - the page draws it");
    waitFor([&] {
        return jsString(page, "document.querySelector('.strip .left').textContent") ==
               window.statusBar()->currentMessage();
    }, 3000);
    check(jsString(page, "document.querySelector('.strip .left').textContent") ==
              window.statusBar()->currentMessage(),
          "the strip's left channel is the Qt bar's sentence - \"" + window.statusBar()->currentMessage() + "\"");
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
    if (ItemsPanel* drawer = window.itemsPanel(); drawer && drawer->rowCount() >= 2) {
        QWidget* row = drawer->rowWidgetAt(0);
        QWidget* target = drawer->rowWidgetAt(1);
        auto send = [&](QEvent::Type type, const QPoint& inDrawer) {
            const QPoint local = row->mapFrom(drawer, inDrawer);
            QMouseEvent e(type, QPointF(local), QPointF(row->mapToGlobal(local)),
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
            QCoreApplication::sendEvent(row, &e);
        };
        const QPoint from = row->mapTo(drawer, QPoint(row->width() / 3, row->height() / 2));
        const QPoint to = target->mapTo(drawer, QPoint(target->width() / 3, target->height() - 3));
        send(QEvent::MouseButtonPress, from);
        for (int i = 1; i <= 4; ++i) send(QEvent::MouseMove, from + (to - from) * i / 4);
        pump(200);
        const QString line = drawer->dragStatusText();
        const QString rightTitle = QStringLiteral("(document.querySelector('.strip .right') || {}).title || ''");
        waitFor([&] { return jsString(page, rightTitle) == line; }, 1500);
        check(!line.isEmpty() && jsString(page, rightTitle) == line,
              "a drawer drag's line reaches the strip's right channel - \"" + line + "\"");
        drawer->cancelDrag();
        pump(200);
    } else {
        check(false, "the Items drawer lists the two seeded bodies");
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
        check(!window.findChild<AppearancePanel*>()->isVisible(), "and the Qt drawer stays hidden");
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
              QStringLiteral("and afterwards keys go back to where they went before (%1)")
                  .arg(window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none"));
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
    check(!window.findChild<VersionsPanel*>()->isVisible(), "and the Qt drawer stays hidden");
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
        waitFor(settled, 3000);
        check(settled(), QStringLiteral("the card reports its height, the slot takes it, and the page draws "
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
        waitFor([&] { return versionsSlot->height() > emptyHeight && settled(); }, 3000);
        check(versionsSlot->height() > emptyHeight && settled(),
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
        waitFor([&] { return versionsSlot->height() == emptyHeight && settled(); }, 3000);
        check(versionsSlot->height() == emptyHeight && settled(),
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
        pump(150);
        std::printf("[info] armed row: %s\n",
                    qPrintable(jsString(page, "JSON.stringify((() => { const a = " + row.arg(QStringLiteral(" .actions")) +
                                                  "; const d = " + row.arg(QStringLiteral(" .delete")) +
                                                  "; return {aw: a.scrollWidth, ac: a.clientWidth, dh: d.scrollHeight, "
                                                  "dc: d.clientHeight, n: a.children.length, clipped: [...a.children].filter((b) => b.scrollWidth > b.clientWidth).length, "
                                                  "right: Math.round(d.getBoundingClientRect().right), "
                                                  "card: Math.round(document.querySelector('.versions-card').getBoundingClientRect().right)}; })())")));
        check(js(page, "(() => { const a = " + row.arg(QStringLiteral(" .actions")) + "; const d = " +
                           row.arg(QStringLiteral(" .delete")) +
                           "; return a.scrollWidth <= a.clientWidth && d.scrollHeight <= d.clientHeight + 1 && "
                           "[...a.children].every((b) => { const r = b.getBoundingClientRect(), "
                           "o = a.getBoundingClientRect(); return b.scrollWidth <= b.clientWidth && "
                           "r.left >= o.left - 0.5 && r.right <= o.right + 0.5; }) && "
                           "d.textContent.includes('again'); })()").toBool(),
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
        check(settled(), "and the card still covers its slot exactly");
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
    check(!window.findChild<JointsPanel*>()->isVisible(), "and the Qt drawer stays hidden");
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
        waitFor(settled, 3000);
        check(settled(), QStringLiteral("the card reports its height and covers its slot exactly (%1x%2)")
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
        waitFor(settled, 3000);
        check(settled(), "the slot grows with the open row and the card still covers it");
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
        waitFor([&] { return jointsSlot->height() < openHeight && settled(); }, 3000);
        check(jointsSlot->height() < openHeight && settled(),
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
        check(broken.statusBar()->isVisible(), "so the Qt status bar stays up");
        if (QAction* brokenSettings = actionNamed(&broken, QStringLiteral("Settings..."))) {
            brokenSettings->trigger();
            pump(200);
            check(broken.findChild<AppearancePanel*>()->isVisible(),
                  "and Settings opens the Qt drawer, not an empty slot (review I3)");
            brokenSettings->trigger();
            pump(100);
        }
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
        WindowButtons* windowButtons = window.findChild<WindowButtons*>();
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
            if (ToolCluster* rail = window.findChild<ToolCluster*>()) {
                const QPoint onRail = rail->geometry().center();
                QWidget* at = window.view()->childAt(onRail);
                check(at && (at == page || page->isAncestorOf(at)), "and so does a press on the rail");
            }
            if (WindowButtons* buttons = window.findChild<WindowButtons*>()) {
                const QPoint onButtons = window.view()->mapFrom(&window, buttons->mapTo(&window, buttons->rect().center()));
                QWidget* at = window.view()->childAt(onButtons);
                check(at == buttons || (at && buttons->isAncestorOf(at)),
                      "the window buttons stay usable over the wash (Review Focus 4)");
            }
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
                WindowButtons* buttons = window.findChild<WindowButtons*>();
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
            // The Qt cards stand over the page again once it has gone.
            if (ToolCluster* rail = window.findChild<ToolCluster*>()) {
                QWidget* at = window.view()->childAt(rail->geometry().center());
                check(at == rail || (at && rail->isAncestorOf(at)), "the rail takes its own presses again");
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
            if (ToolCluster* rail = window.findChild<ToolCluster*>()) {
                QWidget* at = window.view()->childAt(rail->geometry().center());
                check(at && (at == page || page->isAncestorOf(at)), "and so does a press on the rail");
            }
            if (WindowButtons* buttons = window.findChild<WindowButtons*>()) {
                const QPoint on = window.view()->mapFrom(&window, buttons->mapTo(&window, buttons->rect().center()));
                QWidget* at = window.view()->childAt(on);
                check(at == buttons || (at && buttons->isAncestorOf(at)), "the window buttons stay usable");
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
            if (ToolCluster* rail = window.findChild<ToolCluster*>()) {
                QWidget* at = window.view()->childAt(rail->geometry().center());
                check(at == rail || (at && rail->isAncestorOf(at)), "the rail takes its own presses again");
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
            renderMode->trigger();
            pump(600);
            check(studio->dockedPanel() == rv && rv->isVisible() && !panel->isVisible(),
                  "render mode docks the PAGE beside the viewport, not the Qt panel");
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
            // back at once, every value intact, and render mode stays on.
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
#endif
            panel->chooseQuality(RenderSettingsPanel::Quality::Deep);
            renderMode->trigger();
            pump(400);
            check(studio->dockedPanel() == nullptr && !renderMode->isChecked(), "leaving render mode undocks it");
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
        check(window.statusBar()->isVisible() && !page->isVisible(),
              "the Qt status bar comes back and the dead page leaves the viewport");
        TopoDS_Face live;
        for (TopExp_Explorer e(window.document().solids().front().shape, TopAbs_FACE); e.More(); e.Next()) {
            live = TopoDS::Face(e.Current());
            break;
        }
        window.pullFaceBy(live, -100000.0);
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts && toasts->isShowing(), "and the next Failure is shown by the Qt toast");
        if (settingsAction) {
            settingsAction->trigger();
            pump(200);
            check(window.findChild<AppearancePanel*>()->isVisible(),
                  "Settings opens the Qt drawer once the page is gone (review I3)");
            settingsAction->trigger();
            pump(100);
        }
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
    }
#endif

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
