// The HTML hub in the real app (migration Phase 2): HubWindow, MainWindow and
// SceneWindow wired by the one handoff, a seeded library in a temp dir, and
// the page driven from INSIDE - DOM events dispatched by runJavaScript, never
// OS input - with every outcome read back from the store on disk.
//
//   hub_smoke.exe [output-dir]     composited captures land in output-dir

// windows.h FIRST - see gui_smoke.cpp's own note.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

#include "DocumentModel.h"
#include "EditorSelectorHandoff.h"
#include "FurnitureStore.h"
#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "SceneModel.h"
#include "SceneWindow.h"
#include "Theme.h"
#include "ui/Themes.h"
#include "session/HubModel.h"
#include "web/HubWindow.h"
#include "web/WebHost.h"
#include "WindowChrome.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWheelEvent>

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

// A JSON value out of the page, so a list comes back as a list.
QJsonDocument jsJson(QWebEngineView* view, const QString& expression)
{
    return QJsonDocument::fromJson(
        jsString(view, QStringLiteral("JSON.stringify(%1)").arg(expression)).toUtf8());
}

QStringList cardNames(QWebEngineView* view, const QString& kind)
{
    QStringList names;
    for (const QJsonValue& v :
         jsJson(view, QStringLiteral("[...document.querySelectorAll('[data-card-kind=\"%1\"] .name')]"
                                     ".map(n => n.textContent)")
                          .arg(kind))
             .array())
        names << v.toString();
    return names;
}

QString cardSelector(const QString& name)
{
    // The cell whose name reads `name` - names are the user's, so they are
    // matched as text, never spliced into a selector.
    return QStringLiteral("[...document.querySelectorAll('[data-card-id]')].find(c => "
                          "c.querySelector('.name') && c.querySelector('.name').textContent === %1)")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact))
                 .mid(1)
                 .chopped(1));
}

void hover(QWebEngineView* view, const QString& name)
{
    // A real pointer LEAVES the card it was on before it enters another; a
    // synthetic enter alone left the previous card wearing its hover.
    js(view, QStringLiteral("document.querySelectorAll('[data-card-id]').forEach(c => "
                            "c.dispatchEvent(new MouseEvent('mouseleave')))"));
    js(view, QStringLiteral("(") + cardSelector(name) +
                 QStringLiteral(").dispatchEvent(new MouseEvent('mouseenter'))"));
    pump(80);
}

void clickButtonIn(QWebEngineView* view, const QString& name, const QString& label)
{
    js(view, QStringLiteral("[...(%1).querySelectorAll('button')].find(b => b.textContent === '%2').click()")
                 .arg(cardSelector(name), label));
    pump(80);
}

void typeAndKey(QWebEngineView* view, const QString& fieldSelector, const QString& value, const QString& key)
{
    js(view, QStringLiteral("(() => { const f = %1; f.focus(); f.value = %2; "
                            "f.dispatchEvent(new Event('input', {bubbles: true})); "
                            "f.dispatchEvent(new KeyboardEvent('keydown', {key: '%3', bubbles: true})); })()")
                 .arg(fieldSelector,
                      QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact))
                          .mid(1)
                          .chopped(1),
                      key));
    pump(150);
}

QAction* actionNamed(QWidget* window, const QString& text)
{
    for (QAction* a : window->findChildren<QAction*>())
        if (QString(a->text()).remove(QLatin1Char('&')) == text) return a;
    return nullptr;
}

#ifdef _WIN32
QImage capture(QWidget* window, const QString& path)
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
    QImage shot;
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT))
            shot = QImage(static_cast<const uchar*>(bits), w, h, QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!shot.isNull()) shot.save(path);
    return shot;
}

// gui_smoke's KeepSuiteWindowsBehind, small.
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
// What Windows itself asks the window: WM_NCHITTEST at a point of the hub's
// client area (logical px), answered by WindowChrome's filter. A message to
// our own window - not OS input.
LRESULT nativeHit(QWidget* window, const QPoint& logical)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const double dpr = window->devicePixelRatioF();
    POINT p{LONG(logical.x() * dpr), LONG(logical.y() * dpr)};
    ClientToScreen(hwnd, &p);
    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
}
#endif

// gui_smoke's usesBannedWord(), ported: case-insensitive; "round" and
// "flatten" only with no letter or digit before them; "Joint" passes.
QStringList bannedIn(const QString& text)
{
    static const QStringList words = {"Fuse",  "Solid", "OCCT",     "mm3",    "(s)",   "Merge", "Join",
                                      "bevel", "round", "flatten",  "symmetry", "kernel", "engine"};
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
                                    : QDir::current().filePath(QStringLiteral("hub-smoke-out"));
    QDir().mkpath(outDir);
#ifdef _WIN32
    QTimer behind;
    QObject::connect(&behind, &QTimer::timeout, [] { keepBehind(); });
    behind.start(125);
#endif

    // --- a library: three furniture (one with a picture), one scene ---------------
    QTemporaryDir library;
    if (!library.isValid()) return 2;
    QString benchId, armchairId, stoolId;
    {
        FurnitureStore store(library.path());
        benchId = store.createFurniture(QStringLiteral("Bench"));
        armchairId = store.createFurniture(QStringLiteral("Armchair"));
        stoolId = store.createFurniture(QStringLiteral("Zebra stool"));
        DocumentModel doc;
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 600, 300, 18));
        QImage pic(320, 200, QImage::Format_RGB32);
        pic.fill(QColor(0x8f, 0x6a, 0x45));   // NOT-A-UI-COLOUR: a stand-in thumbnail
        store.saveFurniture(benchId, doc, pic);
        store.saveFurniture(armchairId, doc, QImage());
        const QString sceneId = store.createScene(QStringLiteral("Dining"));
        SceneModel scene;
        scene.addPiece(benchId.toStdString(), "Bench");
        store.saveScene(sceneId, scene, QImage());
    }

    MainWindow window(nullptr, /*persistProgress=*/false, library.path());
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    HubWindow hub(window.furnitureStore());
    hub.setAttribute(Qt::WA_ShowWithoutActivating);
    SceneWindow sceneWindow(&window.furnitureStore());
    sceneWindow.setAttribute(Qt::WA_ShowWithoutActivating);
    EditorSelectorHandoff::wire(window, hub, &sceneWindow);

    // Before the page has said where its title strip is - or if it never
    // loads - the window must still be movable and closable.
    check(hub.hitTest(QPoint(10, 10)) == WindowChrome::Hit::Caption &&
              hub.hitTest(QPoint(hub.width() - 66, 10)) == WindowChrome::Hit::MaxButton &&
              hub.hitTest(QPoint(10, 100)) == WindowChrome::Hit::Client,
          "before the page reports, a 32 px strip is caption and its maximize slot answers MaxButton");
    check(WebHost::pageAvailable(QStringLiteral("hub")) &&
              !WebHost::pageAvailable(QStringLiteral("no-such-page")),
          "the app can tell a deployed page from a missing one (main.cpp falls back to the Qt hub)");
    hub.show();

    QWebEngineView* page = hub.host()->view();
    const bool loaded = waitFor([&] { return hub.host()->loaded(); }, 20000);
    check(loaded && hub.host()->loadOk(), "the hub page loads from the deployed build");
    waitFor([&] { return cardNames(page, "furniture").size() == 3; }, 5000);
    pump(500);

    // --- what the page shows -----------------------------------------------------
    check(cardNames(page, "furniture") == QStringList({"Armchair", "Bench", "Zebra stool"}),
          "three furniture, most recent first, never-saved last - got " + cardNames(page, "furniture").join(", "));
    check(cardNames(page, "scene") == QStringList({"Dining"}), "and the scene in its own section");
    check(jsString(page, "getComputedStyle(document.documentElement).getPropertyValue('--panel').trim()") ==
              Theme::panel().name(),
          "the live Theme reaches the page's :root (--panel " + Theme::panel().name() + ")");
    waitFor([&] {
        return js(page, "(() => { const i = document.querySelector('[data-card-id] img'); "
                        "return !!i && i.complete && i.naturalWidth > 0; })()").toBool();
    }, 3000);
    check(js(page, "(() => { const i = document.querySelector('[data-card-id] img'); "
                   "return !!i && i.complete && i.naturalWidth > 0; })()").toBool(),
          "the saved thumbnail LOADS (complete, with pixels) - not merely an <img> on the page");
    check(js(page, "(() => { const s = document.querySelector('.scroll'); "
                   "return s.scrollHeight > s.clientHeight && s.offsetWidth - s.clientWidth > 0; })()")
              .toBool(),
          "a library taller than the window shows a scrollbar - " +
              jsString(page, "(() => { const s = document.querySelector('.scroll'); return "
                             "`scroll ${s.scrollHeight} client ${s.clientHeight} offsetW ${s.offsetWidth} "
                             "clientW ${s.clientWidth}`; })()"));
    check(!page->acceptDrops(), "the hub's view takes no drops - a dropped file cannot replace the page");
#ifdef _WIN32
    {
        const QRect bar = hub.controls()->titleBar();
        const QRect max = hub.controls()->maxButton();
        check(nativeHit(&hub, QPoint(bar.left() + 60, bar.center().y())) == HTCAPTION &&
                  nativeHit(&hub, max.center()) == HTMAXBUTTON &&
                  nativeHit(&hub, QPoint(bar.left() + 60, bar.bottom() + 120)) == HTCLIENT,
              "Windows' own hit test: the strip is caption, the maximize button HTMAXBUTTON, a card client");
    }
#endif
    {
        QWidget* target = page->focusProxy() ? page->focusProxy() : page;
        const QPointF at(200, 200);
        QWheelEvent wheel(at, target->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(target, &wheel);
        pump(300);
        check(qFuzzyCompare(page->zoomFactor(), 1.0),
              QStringLiteral("Ctrl+wheel does not zoom the page (zoom %1) - zoom would move the caption "
                             "away from where Windows hit-tests it")
                  .arg(page->zoomFactor()));
    }
    {
        const QString before = page->url().toString();
        // A file: URL - what a dropped file or a stray link would be. (about:blank
        // is the renderer's own and never reaches the browser side.)
        js(page, "location.href = " + QString::fromUtf8(QJsonDocument(QJsonArray{
                     QUrl::fromLocalFile(QCoreApplication::applicationDirPath()).toString()})
                     .toJson(QJsonDocument::Compact)).mid(1).chopped(1));
        pump(600);
        check(page->url().toString() == before && cardNames(page, "furniture").size() == 3,
              "the page cannot navigate away from the hub - got " + page->url().toString());
    }
    check(hub.controls()->titleBar().height() == 32 && hub.controls()->maxButton().width() == 44 &&
              hub.controls()->titleBar().contains(hub.controls()->maxButton()),
          "the page reported its title strip (32 high) and the 44 px maximize button inside it");
    {
        QString text = jsString(page, "document.body.innerText");
        for (const QString& n : {"Armchair", "Bench", "Zebra stool", "Dining"}) text.replace(n, " ");
        check(bannedIn(text).isEmpty(), "no word the page shows is banned - found: " + bannedIn(text).join(", "));
    }
#ifdef _WIN32
    capture(&hub, outDir + "/hub-rest.png");
    hover(page, "Bench");
    capture(&hub, outDir + "/hub-hover.png");
#endif

    // --- choosing a card hands over to the editor, and back ----------------------
    js(page, "(" + cardSelector("Bench") + ").querySelector('button.thumb').click()");
    waitFor([&] { return window.isVisible() && !hub.isVisible(); }, 5000);
    pump(300);
    check(window.isVisible() && !hub.isVisible() && window.currentFurnitureId() == benchId,
          "clicking Bench shows the editor, hides the hub, and opens Bench");
    QAction* closeFurniture = actionNamed(&window, QStringLiteral("Close furniture"));
    check(closeFurniture != nullptr, "the editor has Close furniture");
    if (closeFurniture) closeFurniture->trigger();
    waitFor([&] { return hub.isVisible() && !window.isVisible(); }, 5000);
    pump(300);
    check(hub.isVisible() && !window.isVisible(), "Close furniture brings the hub back");

    // --- the name question creates and opens --------------------------------------
    js(page, "[...document.querySelectorAll('button.new')][0].click()");
    waitFor([&] { return js(page, "!!document.querySelector('[role=dialog] input')").toBool(); }, 3000);
    pump(200);
    check(jsString(page, "document.querySelector('[role=dialog] input').value") ==
              window.furnitureStore().nextFurnitureName(),
          "+ New furniture asks with the store's next name");
#ifdef _WIN32
    capture(&hub, outDir + "/hub-name-card.png");
#endif
    typeAndKey(page, "document.querySelector('[role=dialog] input')", "Shoe rack", "Enter");
    waitFor([&] { return window.isVisible(); }, 5000);
    pump(300);
    check(window.isVisible() && window.currentFurnitureName() == QStringLiteral("Shoe rack"),
          "Enter creates Shoe rack and opens it - got " + window.currentFurnitureName());
    if (closeFurniture) closeFurniture->trigger();
    waitFor([&] { return hub.isVisible(); }, 5000);
    waitFor([&] { return cardNames(page, "furniture").contains("Shoe rack"); }, 3000);
    check(cardNames(page, "furniture").contains("Shoe rack"), "and the hub lists it on the way back");

    // --- rename in place --------------------------------------------------------
    hover(page, "Armchair");
    clickButtonIn(page, "Armchair", "Rename");
    typeAndKey(page, "(" + cardSelector("Armchair") + " || document).querySelector('input.rename')",
               "Lounge chair", "Enter");
    waitFor([&] { return cardNames(page, "furniture").contains("Lounge chair"); }, 3000);
    bool renamed = false;
    for (const FurnitureStore::FurnitureInfo& i : window.furnitureStore().listFurniture())
        if (i.id == armchairId) renamed = i.name == QStringLiteral("Lounge chair");
    check(renamed && cardNames(page, "furniture").contains("Lounge chair"),
          "Rename writes the new name to the library and the card shows it");

    // --- the two-click delete, and what it breaks ----------------------------------
    hover(page, "Bench");
    clickButtonIn(page, "Bench", "Delete");
    waitFor([&] { return !jsString(page, "((" + cardSelector("Bench") + ").querySelector('.used-in') || {}).textContent || ''").isEmpty(); }, 3000);
    const QString strip = jsString(page, "((" + cardSelector("Bench") + ").querySelector('.used-in') || {}).textContent || ''");
    check(strip == QStringLiteral("Used in Dining"), "the armed Delete names the scene it breaks - got \"" + strip + "\"");
#ifdef _WIN32
    capture(&hub, outDir + "/hub-armed.png");
#endif
    clickButtonIn(page, "Bench", QStringLiteral("Delete — click again"));
    waitFor([&] { return !cardNames(page, "furniture").contains("Bench"); }, 3000);
    bool benchGone = true;
    for (const FurnitureStore::FurnitureInfo& i : window.furnitureStore().listFurniture())
        if (i.id == benchId) benchGone = false;
    check(benchGone && !cardNames(page, "furniture").contains("Bench"), "the second click deletes Bench from the library");
    check(jsString(page, "(document.querySelector('.toast') || {}).textContent || ''") ==
              QStringLiteral("Deleted Bench — 1 scene now shows it as missing"),
          "and a Note says what that broke");

    // --- a refusal is never silent -------------------------------------------------
    QString stoolDir;
    for (const FurnitureStore::FurnitureInfo& i : window.furnitureStore().listFurniture())
        if (i.id == stoolId) stoolDir = i.filePath;
    QDir(stoolDir).removeRecursively();
    const QStringList beforeRefusal = cardNames(page, "furniture");
    hover(page, "Zebra stool");
    clickButtonIn(page, "Zebra stool", "Rename");
    typeAndKey(page, "(" + cardSelector("Zebra stool") + " || document).querySelector('input.rename')", "Tall stool", "Enter");
    waitFor([&] { return !jsString(page, "(document.querySelector('.banner') || {}).textContent || ''").isEmpty(); }, 3000);
    check(jsString(page, "(document.querySelector('.banner') || {}).textContent || ''") ==
              QStringLiteral("Couldn't rename this furniture to Tall stool — Check that its folder still "
                             "exists and isn't read-only"),
          "a refused rename shows the hub's own sentence in the banner");
    check(cardNames(page, "furniture") == beforeRefusal, "and leaves the grid exactly as it was");

    // --- a picture rewritten while the hub was away is the one it shows ------------
    {
        FurnitureStore& store = window.furnitureStore();
        QString rackId;
        for (const FurnitureStore::FurnitureInfo& i : store.listFurniture())
            if (i.name == QStringLiteral("Shoe rack")) rackId = i.id;
        DocumentModel doc;
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 10, 10, 10));
        QImage first(160, 100, QImage::Format_RGB32);
        first.fill(Qt::red);
        store.saveFurniture(rackId, doc, first);
        hub.refresh();
        pump(400);
        const QString src1 = jsString(page, "((" + cardSelector("Shoe rack") + ").querySelector('img') || {}).src || ''");
        QThread::msleep(1100);   // whole-second file times
        QImage second(160, 100, QImage::Format_RGB32);
        second.fill(Qt::blue);
        store.saveFurniture(rackId, doc, second);
        hub.refresh();
        waitFor([&] {
            return js(page, "(() => { const i = (" + cardSelector("Shoe rack") +
                                ").querySelector('img'); return !!i && i.complete && i.naturalWidth > 0; })()")
                .toBool();
        }, 3000);
        const QString src2 = jsString(page, "((" + cardSelector("Shoe rack") + ").querySelector('img') || {}).src || ''");
        const QString pixel = jsString(page, "(() => { const i = (" + cardSelector("Shoe rack") +
                                                 ").querySelector('img'); const c = document.createElement('canvas');"
                                                 " c.width = 4; c.height = 4; const g = c.getContext('2d');"
                                                 " g.drawImage(i, 0, 0, 4, 4); const d = g.getImageData(2, 2, 1, 1).data;"
                                                 " return d[0] + ',' + d[1] + ',' + d[2]; })()");
        check(!src1.isEmpty() && src1 != src2 && pixel.startsWith(QStringLiteral("0,0,")),
              "a picture rewritten while the hub was away is the NEW one (src changed, pixel " + pixel + ")");
    }

    // --- search and sort --------------------------------------------------------------
    js(page, "(() => { const s = document.querySelector('input[type=search]'); s.value = 'shoe'; "
             "s.dispatchEvent(new Event('input', {bubbles: true})); })()");
    pump(300);
    check(cardNames(page, "furniture") == QStringList({"Shoe rack"}), "searching 'shoe' keeps Shoe rack alone");
    js(page, "(() => { const s = document.querySelector('input[type=search]'); s.value = ''; "
             "s.dispatchEvent(new Event('input', {bubbles: true})); })()");
    pump(300);
    js(page, "[...document.querySelectorAll('button.sort')].find(b => b.textContent === 'Name').click()");
    pump(150);
    const QStringList byName = cardNames(page, "furniture");
    QStringList sorted = byName;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    check(byName == sorted && byName.size() >= 2, "the Name chip sorts by name - got " + byName.join(", "));

    // --- each theme on the hub (T-072): the page wears it, and a capture of each ---
    for (const Themes::Info& theme : Themes::all()) {
        Themes::select(theme.id);
        waitFor([&] { return jsString(page, "document.documentElement.dataset.theme") == theme.id; }, 3000);
        check(jsString(page, "document.documentElement.dataset.theme") == theme.id,
              "the hub wears " + theme.name);
        pump(400);
        capture(&hub, outDir + "/hub-theme-" + theme.id + ".png");
    }
    {
        Themes::select(QStringLiteral("ultraviolet-grid"));
        pump(300);
        const QString radius = jsString(page, "getComputedStyle(document.querySelector('.thumb')).borderTopLeftRadius");
        check(radius == QStringLiteral("0px"), "under Ultraviolet Grid a card's corner is square (" + radius + ")");
        Themes::select(QStringLiteral("violet-glass-float"));
        pump(300);
        const QString round = jsString(page, "getComputedStyle(document.querySelector('.thumb')).borderTopLeftRadius");
        check(round == QStringLiteral("26px"), "under Violet Glass Float it is 26px (" + round + ")");
        Themes::select(Themes::graphiteId());
        pump(300);
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
