// The editor's toasts and status bar as HTML (migration Phase 3a), in the
// real editor: MainWindow in HTML-chrome mode, a seeded furniture, and the
// page driven from inside - DOM events through runJavaScript, Qt actions
// through trigger(), never OS input.
//
//   chrome_smoke.exe [output-dir]

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
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
#include "web/EditorChrome.h"
#include "web/WebHost.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
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
    return QRect(o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(), o.value("h").toInt());
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

int main(int argc, char* argv[])
{
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
        store.saveFurniture(furnitureId, doc, QImage());
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

    // --- the page's words ----------------------------------------------------------------
    {
        QString text = jsString(page, "document.body.innerText");
        text.replace(QStringLiteral("Bench"), QStringLiteral(" "));
        check(bannedIn(text).isEmpty(), "no word the page shows is banned - found: " + bannedIn(text).join(", "));
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
