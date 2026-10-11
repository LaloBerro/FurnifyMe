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
#include "ui/GestureChip.h"
#include "ui/ItemsPanel.h"
#include "ui/Toast.h"
#include "web/EditorChrome.h"
#include "web/WebHost.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
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
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(582, 0, 18), 18, 300, 400));
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

    // --- the page's words ----------------------------------------------------------------
    {
        QString text = jsString(page, "document.body.innerText");
        text.replace(QStringLiteral("Bench"), QStringLiteral(" "));
        check(bannedIn(text).isEmpty(), "no word the page shows is banned - found: " + bannedIn(text).join(", "));
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
#ifdef _WIN32
    {
        // A render process that dies: the same fallback, from then on.
        const qint64 pid = page->page()->renderProcessPid();
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
        waitFor([&] { return !chrome->isLive(); }, 5000);
        check(!chrome->isLive(), "a page whose render process died is no longer live");
        pump(200);
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
    }
#endif

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
