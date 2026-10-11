// PHASE 0 SPIKE - the seven gates, measured.
//
//   web_spike.exe [output-dir]
//
// Drives the real MainWindow with the Qt WebEngine overlay (src/web) attached,
// and prints one line per gate: PASS or FAIL, and the numbers behind it. The
// same discipline as gui_smoke: windows are kept at the BACK of the desktop
// and never activated, and every input is delivered inside this process (Qt
// events, or QTest's QWindow overloads, which go through Qt's own dispatch
// and never move the OS cursor). Run it a second time with QT_SCALE_FACTOR=1.5
// for the 150% half of gate 1.

// windows.h FIRST, as in gui_smoke: something in the Qt/OCCT include chain
// pulls it in with GDI and USER excluded, after which a later include is a
// silent no-op.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
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
#include "web/WebOverlay.h"

#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>


namespace {

int gFailures = 0;
QJsonObject gSummary;

void gate(int n, bool pass, const QString& text)
{
    if (!pass) ++gFailures;
    std::printf("[gate %d] %s  %s\n", n, pass ? "PASS" : "FAIL", qPrintable(text));
    std::fflush(stdout);
    QJsonObject g = gSummary.value(QStringLiteral("gate%1").arg(n)).toObject();
    QJsonArray lines = g.value(QStringLiteral("lines")).toArray();
    lines.append(QStringLiteral("%1  %2").arg(pass ? "PASS" : "FAIL", text));
    g.insert(QStringLiteral("lines"), lines);
    if (!pass) g.insert(QStringLiteral("failed"), true);
    gSummary.insert(QStringLiteral("gate%1").arg(n), g);
}

void info(const QString& text)
{
    std::printf("[info] %s\n", qPrintable(text));
    std::fflush(stdout);
}

// Waits until Qt is idle, up to ms - gui_smoke's settle().
void settle(int ms = 250)
{
    QElapsedTimer timer;
    timer.start();
    int quiet = 0;
    QEventLoop loop;
    while (timer.elapsed() < ms) {
        const bool worked = loop.processEvents(QEventLoop::AllEvents);
        quiet = worked ? 0 : quiet + 1;
        if (!worked) QThread::msleep(1);
        if (quiet >= 4) return;
    }
}

// Sleeps the whole time, pumping - for waits on a timer or another process.
void waitFully(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool waitFor(const std::function<bool()>& done, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

#ifdef _WIN32
// gui_smoke's KeepSuiteWindowsBehind, small: every visible top-level this
// process owns goes to the bottom of the z-order and cannot be activated.
class KeepBehind : public QObject
{
public:
    KeepBehind()
    {
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [] { push(); });
        t->start(125);
    }
    static void push()
    {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (!w->isVisible()) continue;
            HWND h = reinterpret_cast<HWND>(w->winId());
            SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
            SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
};

// The composited CLIENT area as DWM has it - the GL viewport and every
// overlay over it, which neither QWidget::grab() nor V3d_View::Dump() can show.
QImage captureClient(QWidget* window)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) return QImage();
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return QImage();
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage shot;
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT))
            shot = QImage(static_cast<const uchar*>(bits), width, height, QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return shot;
}

struct Memory {
    double selfPrivateMb = 0, selfWorkingMb = 0;
    double childPrivateMb = 0, childWorkingMb = 0;
    int children = 0;
};

void addProcess(DWORD pid, double& privateMb, double& workingMb)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        privateMb += pmc.PrivateUsage / 1048576.0;
        workingMb += pmc.WorkingSetSize / 1048576.0;
    }
    CloseHandle(h);
}

Memory measureMemory()
{
    Memory m;
    const DWORD self = GetCurrentProcessId();
    addProcess(self, m.selfPrivateMb, m.selfWorkingMb);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m;
    std::vector<std::pair<DWORD, DWORD>> all;   // pid, parent
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do all.emplace_back(pe.th32ProcessID, pe.th32ParentProcessID);
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::vector<DWORD> frontier{self};
    while (!frontier.empty()) {
        const DWORD parent = frontier.back();
        frontier.pop_back();
        for (const auto& [pid, ppid] : all) {
            if (ppid != parent || pid == self) continue;
            ++m.children;
            addProcess(pid, m.childPrivateMb, m.childWorkingMb);
            frontier.push_back(pid);
        }
    }
    return m;
}
#endif

// The capture is in device pixels from the window's client origin.
QRect toCapture(QWidget* w, const QRect& logical)
{
    const double dpr = w->window()->devicePixelRatioF();
    const QPoint tl = w->mapTo(w->window(), logical.topLeft());
    return QRect(int(std::floor(tl.x() * dpr)), int(std::floor(tl.y() * dpr)),
                 int(std::ceil(logical.width() * dpr)), int(std::ceil(logical.height() * dpr)));
}

struct Diff {
    double mean = 0;
    double changedPct = 0;   // pixels whose largest channel delta exceeds 6
    int nearBlackA = 0, nearBlackB = 0;
    int compared = 0;
};

// A against B over `area`, skipping `skip` - both in capture pixels.
Diff compare(const QImage& a, const QImage& b, const QRect& area, const QRegion& skip)
{
    Diff d;
    if (a.isNull() || b.isNull() || a.size() != b.size()) return d;
    const QRect r = area.intersected(a.rect());
    double sum = 0;
    int changed = 0;
    for (int y = r.top(); y <= r.bottom(); y += 2) {
        for (int x = r.left(); x <= r.right(); x += 2) {
            if (skip.contains(QPoint(x, y))) continue;
            const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
            const int dr = std::abs(qRed(pa) - qRed(pb)), dg = std::abs(qGreen(pa) - qGreen(pb)),
                      db = std::abs(qBlue(pa) - qBlue(pb));
            sum += (dr + dg + db) / 3.0;
            if (std::max({dr, dg, db}) > 6) ++changed;
            if (qRed(pa) < 8 && qGreen(pa) < 8 && qBlue(pa) < 8) ++d.nearBlackA;
            if (qRed(pb) < 8 && qGreen(pb) < 8 && qBlue(pb) < 8) ++d.nearBlackB;
            ++d.compared;
        }
    }
    if (d.compared > 0) {
        d.mean = sum / d.compared;
        d.changedPct = 100.0 * changed / d.compared;
    }
    return d;
}

QColor medianColour(const QImage& img, const QRect& r)
{
    std::vector<int> rs, gs, bs;
    const QRect c = r.intersected(img.rect());
    for (int y = c.top(); y <= c.bottom(); ++y)
        for (int x = c.left(); x <= c.right(); ++x) {
            const QRgb p = img.pixel(x, y);
            rs.push_back(qRed(p));
            gs.push_back(qGreen(p));
            bs.push_back(qBlue(p));
        }
    if (rs.empty()) return QColor();
    auto med = [](std::vector<int>& v) {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    return QColor(med(rs), med(gs), med(bs));
}

int colourDistance(const QColor& a, const QColor& b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

struct Run {
    int moves = 0;
    int paints = 0;
    double pumpMs = 0;      // per move: paint + composite + present
    double intervalMs = 0;  // mean OCCT frame-to-frame
    double lagMeanRevs = 0, lagMaxRevs = 0;
    double errMeanPx = 0, errMaxPx = 0;
};

// One synthetic drag of `moves` steps with the event loop pumped between
// steps, so each step gets its own frame - gui_smoke's orbitRun(), with a
// button: Right orbits, Middle pans.
Run dragRun(OcctViewWidget* view, Qt::MouseButton button, int moves, WebOverlay* overlay,
            const std::map<int, QPointF>* anchorAt, const int* latestShown,
            const std::function<void()>& afterPress = {})
{
    Run r;
    r.moves = moves;
    const QPointF from(view->width() * 0.45, view->height() * 0.5);
    QMouseEvent press(QEvent::MouseButtonPress, from, view->mapToGlobal(from), button, button,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(view, &press);
    if (afterPress) afterPress();
    settle(150);
    view->clearPaintSamples();
    const int paintsBefore = view->totalPaintCount();
    qint64 pumpNs = 0;
    double lagSum = 0, errSum = 0;
    int lagN = 0;
    for (int i = 0; i < moves; ++i) {
        const QPointF at = button == Qt::RightButton
                               ? from + QPointF((i % 2) ? -5.0 : 5.0, (i % 2) ? -2.0 : 2.0)
                               : from + QPointF(4.0 * i, 1.5 * i);   // a pan that travels
        QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &move);
        QElapsedTimer step;
        step.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        pumpNs += step.nsecsElapsed();
        if (overlay && anchorAt && latestShown && *latestShown > 0) {
            const int sent = overlay->anchorRevision();
            const double behind = sent - *latestShown;
            lagSum += behind;
            r.lagMaxRevs = std::max(r.lagMaxRevs, behind);
            const auto now = anchorAt->find(sent);
            const auto shown = anchorAt->find(*latestShown);
            if (now != anchorAt->end() && shown != anchorAt->end()) {
                const QPointF d = now->second - shown->second;
                const double err = std::hypot(d.x(), d.y());
                errSum += err;
                r.errMaxPx = std::max(r.errMaxPx, err);
            }
            ++lagN;
        }
    }
    r.paints = view->totalPaintCount() - paintsBefore;
    r.pumpMs = pumpNs / 1.0e6 / moves;
    const auto samples = view->recentPaints();
    if (samples.size() >= 2)
        r.intervalMs = double(samples.back().beginUs - samples.front().beginUs) / 1000.0 /
                       double(samples.size() - 1);
    if (lagN > 0) {
        r.lagMeanRevs = lagSum / lagN;
        r.errMeanPx = errSum / lagN;
    }
    QMouseEvent release(QEvent::MouseButtonRelease, from, view->mapToGlobal(from), button,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &release);
    settle(200);
    return r;
}

QString runText(const char* label, const Run& r)
{
    return QStringLiteral("%1: %2 paints / %3 moves (%4 per move), pump %5 ms/move, frame interval %6 ms")
        .arg(QLatin1String(label))
        .arg(r.paints)
        .arg(r.moves)
        .arg(r.moves ? double(r.paints) / r.moves : 0.0, 0, 'f', 2)
        .arg(r.pumpMs, 0, 'f', 2)
        .arg(r.intervalMs, 0, 'f', 2);
}

class Counter : public QObject
{
public:
    int presses = 0, moves = 0, releases = 0;
    void reset() { presses = moves = releases = 0; }
    int total() const { return presses + moves + releases; }

protected:
    bool eventFilter(QObject*, QEvent* e) override
    {
        switch (e->type()) {
        case QEvent::MouseButtonPress: ++presses; break;
        case QEvent::MouseMove: ++moves; break;
        case QEvent::MouseButtonRelease: ++releases; break;
        default: break;
        }
        return false;
    }
};

QRectF jsRect(QWebEngineView* view, const QString& selector)
{
    QRectF out;
    bool done = false;
    view->page()->runJavaScript(
        QStringLiteral("(() => { const e = document.querySelector('%1'); if (!e) return null;"
                       " const r = e.getBoundingClientRect(); return [r.x, r.y, r.width, r.height]; })()")
            .arg(selector),
        [&](const QVariant& v) {
            const QVariantList l = v.toList();
            if (l.size() == 4) out = QRectF(l[0].toDouble(), l[1].toDouble(), l[2].toDouble(), l[3].toDouble());
            done = true;
        });
    waitFor([&] { return done; }, 3000);
    return out;
}

QString jsString(QWebEngineView* view, const QString& expr)
{
    QString out;
    bool done = false;
    view->page()->runJavaScript(expr, [&](const QVariant& v) {
        out = v.toString();
        done = true;
    });
    waitFor([&] { return done; }, 3000);
    return out;
}

QAccessibleInterface* findAccessible(QAccessibleInterface* node, QAccessible::Role role,
                                     const QString& name, int depth = 0)
{
    if (!node || depth > 40) return nullptr;
    if (node->role() == role && node->text(QAccessible::Name) == name) return node;
    for (int i = 0; i < node->childCount(); ++i)
        if (QAccessibleInterface* hit = findAccessible(node->child(i), role, name, depth + 1))
            return hit;
    return nullptr;
}

QAccessible::UpdateHandler gPreviousHandler = nullptr;
std::vector<QAccessible::Event> gAccessEvents;
void recordAccessEvent(QAccessibleEvent* event)
{
    gAccessEvents.push_back(event->type());
    if (gPreviousHandler) gPreviousHandler(event);
}

qint64 dirSize(const QString& path, const std::function<bool(const QString&)>& keep)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(path).relativeFilePath(f);
        if (keep(rel)) total += QFileInfo(f).size();
    }
    return total;
}

} // namespace

int main(int argc, char* argv[])
{
    QElapsedTimer processClock;
    processClock.start();
    QSurfaceFormat::setDefaultFormat(OcctViewWidget::surfaceFormat());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    WebOverlay::prepareApplication();
    QApplication app(argc, argv);
    Theme::apply(app);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QDir::current().filePath(QStringLiteral("web-spike-out"));
    QDir().mkpath(outDir);
#ifdef _WIN32
    KeepBehind keepBehind;
#endif
    info(QStringLiteral("scale factor %1, output %2")
             .arg(qEnvironmentVariable("QT_SCALE_FACTOR", QStringLiteral("1")), outDir));

    // --- a furniture to look at -----------------------------------------------
    QTemporaryDir library;
    if (!library.isValid()) {
        std::fprintf(stderr, "FATAL: no temp directory\n");
        return 2;
    }
    QString furnitureId;
    {
        FurnitureStore store(library.path());
        furnitureId = store.createFurniture(QStringLiteral("Spike"));
        DocumentModel doc;
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 600, 300, 18));
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 18), 18, 300, 400));
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(582, 0, 18), 18, 300, 400));
        store.saveFurniture(furnitureId, doc, QImage());
    }

    // --- the app, first without the overlay -------------------------------------
    QElapsedTimer startup;
    startup.start();
    MainWindow window(nullptr, /*persistProgress=*/false, library.path());
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1280, 820);
    window.show();
    OcctViewWidget* view = window.view();
    view->setAnimationsEnabled(false);
    waitFor([&] { return view->totalPaintCount() > 0; }, 10000);
    const double firstFrameMs = startup.nsecsElapsed() / 1.0e6;
    if (!window.openFurniture(furnitureId)) {
        std::fprintf(stderr, "FATAL: the seeded furniture did not open\n");
        return 2;
    }
    settle(400);
    view->fitAll();
    settle(300);
#ifdef _WIN32
    waitFully(500);
    const Memory before = measureMemory();
#endif

    // --- the overlay --------------------------------------------------------------
    QElapsedTimer overlayClock;
    overlayClock.start();
    auto* overlay = new WebOverlay(view, WebOverlay::topOfFirstBody(&window.document()), &window);
    QObject::connect(overlay->bridge(), &SpikeBridge::commandReceived, overlay, [overlay](const QString& id) {
        emit overlay->bridge()->toastRequested(QStringLiteral("C++ received %1").arg(id),
                                               QStringLiteral("note"));
    });
    std::map<int, QPointF> anchorAt;
    QObject::connect(overlay->bridge(), &SpikeBridge::anchorMoved, overlay,
                     [&anchorAt](int rev, double x, double y, bool) { anchorAt[rev] = QPointF(x, y); });
    int latestShown = 0;
    QObject::connect(overlay->bridge(), &SpikeBridge::chipShown, overlay,
                     [&latestShown](int rev, double, double) { latestShown = std::max(latestShown, rev); });
    overlay->load(WebOverlay::defaultUrl());
    const bool ready = waitFor([&] { return overlay->bridge()->ready(); }, 20000);
    const double readyMs = overlayClock.nsecsElapsed() / 1.0e6;
    waitFor([&] { return !overlay->bridge()->hitRects().isEmpty(); }, 5000);
    waitFully(800);   // the card's entrance and the first chip frame
    info(QStringLiteral("page %1 (load ok %2), %3 hit rectangles, url %4")
             .arg(ready ? "ready" : "NOT READY")
             .arg(overlay->loadOk())
             .arg(overlay->bridge()->hitRects().size())
             .arg(WebOverlay::defaultUrl().toString()));
    {
        QWebEngineView* w = overlay->view();
        QWidget* fp = w->focusProxy();
        info(QStringLiteral("view geometry %1,%2 %3x%4 visible %5; render widget %6 %7x%8 visible %9; viewport %10x%11")
                 .arg(w->x()).arg(w->y()).arg(w->width()).arg(w->height()).arg(w->isVisible())
                 .arg(fp ? fp->metaObject()->className() : "none")
                 .arg(fp ? fp->width() : -1).arg(fp ? fp->height() : -1).arg(fp ? fp->isVisible() : false)
                 .arg(view->width()).arg(view->height()));
        info(QStringLiteral("page says %1").arg(jsString(w, QStringLiteral(
            "JSON.stringify({w: innerWidth, h: innerHeight, dpr: devicePixelRatio, "
            "hits: [...document.querySelectorAll('[data-hit]')].map(e => { const r = e.getBoundingClientRect();"
            " return [Math.round(r.x), Math.round(r.y), Math.round(r.width), Math.round(r.height)]; })})"))));
        w->resize(w->width() - 1, w->height());
        settle(200);
        w->resize(w->width() + 1, w->height());
        settle(200);
        waitFully(300);
        info(QStringLiteral("after a resize the page says %1").arg(jsString(w, QStringLiteral(
            "JSON.stringify({w: innerWidth, h: innerHeight, vis: document.visibilityState})"))));
        for (const QRect& r : overlay->bridge()->hitRects())
            info(QStringLiteral("hit rect %1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    }
    if (!ready) {
        gate(1, false, QStringLiteral("the page never reported ready - nothing else can be measured"));
        return 1;
    }

    // ================================================================== gate 7
#ifdef _WIN32
    const Memory after = measureMemory();
    const double addedMb = (after.selfPrivateMb - before.selfPrivateMb) + after.childPrivateMb;
    gate(7, readyMs < 2000.0,
         QStringLiteral("app first frame %1 ms; overlay created -> page ready %2 ms (bound 2000)")
             .arg(firstFrameMs, 0, 'f', 0)
             .arg(readyMs, 0, 'f', 0));
    gate(7, addedMb < 400.0,
         QStringLiteral("private memory: app %1 -> %2 MB, plus %3 WebEngine processes at %4 MB "
                        "(working set %5 MB); added %6 MB (bound 400)")
             .arg(before.selfPrivateMb, 0, 'f', 0)
             .arg(after.selfPrivateMb, 0, 'f', 0)
             .arg(after.children)
             .arg(after.childPrivateMb, 0, 'f', 0)
             .arg(after.childWorkingMb, 0, 'f', 0)
             .arg(addedMb, 0, 'f', 0));
#endif

    // ================================================================== gate 1
    QWebEngineView* web = overlay->view();
    const QRect viewportArea = toCapture(view, view->rect());
    QRegion skip;   // everything the page draws, plus a margin for shadows
    for (const QRect& r : overlay->bridge()->hitRects())
        skip += toCapture(view, r.adjusted(-40, -40, 40, 40));
    QRect card;
    for (const QRect& r : overlay->bridge()->hitRects())
        if (r.width() >= 200) card = r;

    const auto captureBoth = [&](const QString& tag, QImage& with, QImage& without) {
        settle(200);
        waitFully(300);
        with = captureClient(&window);
        web->hide();
        settle(200);
        waitFully(300);
        without = captureClient(&window);
        web->show();
        settle(200);
        waitFully(400);
        with.save(outDir + QStringLiteral("/%1-overlay.png").arg(tag));
        without.save(outDir + QStringLiteral("/%1-no-overlay.png").arg(tag));
    };
    QImage a, b;
    captureBoth(QStringLiteral("rest"), a, b);
    const Diff rest = compare(a, b, viewportArea, skip);
    gate(1, rest.compared > 1000 && rest.mean < 1.0 && rest.changedPct < 0.5,
         QStringLiteral("where the page draws nothing, the viewport shows through: mean delta %1/255, "
                        "%2% of %3 sampled pixels changed (bounds 1.0 and 0.5%)")
             .arg(rest.mean, 0, 'f', 2)
             .arg(rest.changedPct, 0, 'f', 2)
             .arg(rest.compared));
    gate(1, rest.nearBlackA <= rest.nearBlackB + rest.compared / 1000,
         QStringLiteral("no black where the page is transparent: %1 near-black pixels with the overlay, "
                        "%2 without")
             .arg(rest.nearBlackA)
             .arg(rest.nearBlackB));
    // The card's own fill, counted pixel by pixel inside it: the viewport
    // behind it is a near-identical dark grey, so a median alone could not
    // tell the two apart.
    const auto panelShare = [&](const QImage& img) {
        const QRect c = toCapture(view, card.adjusted(4, 4, -4, -4)).intersected(img.rect());
        int match = 0, all = 0;
        for (int y = c.top(); y <= c.bottom(); ++y)
            for (int x = c.left(); x <= c.right(); ++x, ++all)
                if (colourDistance(QColor(img.pixel(x, y)), QColor(0x23, 0x23, 0x26)) <= 1) ++match;
        return all ? 100.0 * match / all : 0.0;
    };
    const double shareWith = panelShare(a), shareWithout = panelShare(b);
    // What is claimed is that the PAGE adds the fill: the bare viewport there
    // is a dark grey that matches the token by chance on some pixels (13.7% at
    // 100%, 20.5% at 150%), so the bound is on the difference the page makes.
    gate(1, !card.isNull() && shareWith > 60.0 && shareWith - shareWithout > 40.0,
         QStringLiteral("the card paints its own fill: %1% of its pixels are the panel token #232326 with "
                        "the overlay, %2% without")
             .arg(shareWith, 0, 'f', 1)
             .arg(shareWithout, 0, 'f', 1));

    // Live, not a stale frame: move the camera, capture again.
    dragRun(view, Qt::RightButton, 12, nullptr, nullptr, nullptr);
    QImage a2, b2;
    captureBoth(QStringLiteral("moved"), a2, b2);
    const Diff moved = compare(a2, b2, viewportArea, skip);
    const Diff cameraMoved = compare(a, a2, viewportArea, skip);
    gate(1, moved.mean < 1.0 && moved.changedPct < 0.5 && cameraMoved.changedPct > 1.0,
         QStringLiteral("after the camera moves the page still shows the LIVE frame: %1% changed against "
                        "the bare viewport, while %2% of the view changed from the first capture")
             .arg(moved.changedPct, 0, 'f', 2)
             .arg(cameraMoved.changedPct, 0, 'f', 2));

    // ================================================================== gate 2
    view->fitAll();
    settle(300);
    constexpr int kMoves = 60;
    web->hide();
    settle(300);
    const Run base1 = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr);
    web->show();
    settle(300);
    waitFully(400);
    const Run idle = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr);
    emit overlay->bridge()->animatingChanged(true);
    waitFully(500);
    const Run animating = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr);
    // The same, with the page told the camera is still - what an animation
    // costs when nothing pauses it.
    const Run animatingRaw = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr, [&] {
        emit overlay->bridge()->cameraMoving(false);
    });
    emit overlay->bridge()->animatingChanged(false);
    waitFully(300);
    web->hide();
    settle(300);
    const Run base2 = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr);
    web->show();
    settle(300);
    waitFully(400);
    // Pan: the chip stays visible and moves with the model, so this is the
    // overlay's cost while it has real work to do.
    web->hide();
    settle(300);
    view->fitAll();
    settle(200);
    const Run panBase = dragRun(view, Qt::MiddleButton, kMoves, nullptr, nullptr, nullptr);
    web->show();
    settle(300);
    waitFully(400);
    view->fitAll();
    settle(200);
    const Run panWeb = dragRun(view, Qt::MiddleButton, kMoves, nullptr, nullptr, nullptr);
    view->fitAll();
    settle(300);
    // AFTER THE MIGRATION: the page REPLACES the Qt chrome rather than sitting
    // on top of it. Today's app (Qt cards, no page) against the page with the
    // Qt cards hidden - the comparison the decision is actually about.
    std::vector<QWidget*> qtChrome;
    for (QObject* child : view->children()) {
        auto* w = qobject_cast<QWidget*>(child);
        if (w && w != web && w->isVisible()) qtChrome.push_back(w);
    }
    for (QWidget* w : qtChrome) w->hide();
    settle(300);
    waitFully(400);
    view->fitAll();
    settle(200);
    const Run migratedIdle = dragRun(view, Qt::RightButton, kMoves, nullptr, nullptr, nullptr);
    for (QWidget* w : qtChrome) w->show();
    settle(300);
    waitFully(300);
    view->fitAll();
    settle(200);
    info(runText("page only, Qt chrome hidden (the migrated app)", migratedIdle));
    info(runText("pan, no overlay", panBase));
    info(runText("pan, overlay chip following", panWeb));
    const double basePump = (base1.pumpMs + base2.pumpMs) / 2.0;
    const double baseInterval = (base1.intervalMs + base2.intervalMs) / 2.0;
    info(runText("no overlay (1)", base1));
    info(runText("no overlay (2)", base2));
    info(runText("overlay idle", idle));
    info(runText("overlay animating, paused by the camera", animating));
    info(runText("overlay animating, NOT paused", animatingRaw));
    const auto withinBudget = [&](const Run& r) {
        return r.moves > 0 && double(r.paints) / r.moves < 1.5 &&
               r.pumpMs <= basePump * 1.10 && r.intervalMs <= baseInterval * 1.10;
    };
    gate(2, withinBudget(migratedIdle),
         QStringLiteral("THE MIGRATED APP - the page instead of the %1 Qt overlay widgets: %2 ms/move against "
                        "today's %3, frame interval %4 against %5")
             .arg(qtChrome.size())
             .arg(migratedIdle.pumpMs, 0, 'f', 2).arg(basePump, 0, 'f', 2)
             .arg(migratedIdle.intervalMs, 0, 'f', 2).arg(baseInterval, 0, 'f', 2));
    gate(2, withinBudget(idle),
         QStringLiteral("page idle: %1 ms/move against %2 without the overlay, frame interval %3 against %4 "
                        "(bound: one frame per move, within 10%)")
             .arg(idle.pumpMs, 0, 'f', 2).arg(basePump, 0, 'f', 2)
             .arg(idle.intervalMs, 0, 'f', 2).arg(baseInterval, 0, 'f', 2));
    gate(2, double(panWeb.paints) / panWeb.moves < 1.5,
         QStringLiteral("a pan with the chip following the model: %1 ms/move against %2 without the overlay, "
                        "frame interval %3 against %4 (reported; the chip is moving, so this is real work)")
             .arg(panWeb.pumpMs, 0, 'f', 2).arg(panBase.pumpMs, 0, 'f', 2)
             .arg(panWeb.intervalMs, 0, 'f', 2).arg(panBase.intervalMs, 0, 'f', 2));
    gate(2, withinBudget(animating),
         QStringLiteral("page with an animation, paused while the camera moves: %1 ms/move, frame interval "
                        "%2 ms (unpaused it costs %3 ms/move, interval %4 ms)")
             .arg(animating.pumpMs, 0, 'f', 2).arg(animating.intervalMs, 0, 'f', 2)
             .arg(animatingRaw.pumpMs, 0, 'f', 2).arg(animatingRaw.intervalMs, 0, 'f', 2));

    // ================================================================== gate 3
    QWindow* win = window.windowHandle();
    Counter onViewport, onPage;
    view->installEventFilter(&onViewport);
    QWidget* render = web->focusProxy() ? web->focusProxy() : web;
    render->installEventFilter(&onPage);
    web->installEventFilter(&onPage);
    const auto toWindow = [&](const QPointF& viewportPoint) {
        return view->mapTo(&window, viewportPoint.toPoint());
    };
    QPoint empty(int(view->width() * 0.3), int(view->height() * 0.25));
    for (int i = 0; i < 20 && overlay->hitRegion().contains(empty); ++i) empty += QPoint(0, 20);
    gate(3, window.childAt(toWindow(empty)) == view && !overlay->hitRegion().contains(empty),
         QStringLiteral("Qt's own hit test sends an empty viewport point to the viewport (%1,%2)")
             .arg(empty.x()).arg(empty.y()));
    onViewport.reset();
    onPage.reset();
    QElapsedTimer dispatch;
    dispatch.start();
    QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, toWindow(empty));
    const double pressMs = dispatch.nsecsElapsed() / 1.0e6;
    const int viewportPressed = onViewport.presses, pagePressed = onPage.presses;
    QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, toWindow(empty));
    settle(200);
    gate(3, viewportPressed == 1 && pagePressed == 0,
         QStringLiteral("a press on empty viewport reaches the viewport IN THE SAME CALL (%1 there, %2 to the "
                        "page, %3 ms including the pick)")
             .arg(viewportPressed).arg(pagePressed).arg(pressMs, 0, 'f', 2));

    const QRectF apply = jsRect(web, QStringLiteral(".primary"));
    QString commandSeen;
    QElapsedTimer commandClock;
    QObject::connect(overlay->bridge(), &SpikeBridge::commandReceived, overlay,
                     [&](const QString& id) { commandSeen = id; });
    onViewport.reset();
    onPage.reset();
    const QPoint applyPoint = apply.center().toPoint();
    gate(3, window.childAt(toWindow(applyPoint)) != view && overlay->hitRegion().contains(applyPoint),
         QStringLiteral("Qt's hit test sends the card's Apply button to the page (%1,%2)")
             .arg(applyPoint.x()).arg(applyPoint.y()));
    commandClock.start();
    QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, toWindow(applyPoint));
    QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, toWindow(applyPoint));
    const bool commandArrived = waitFor([&] { return commandSeen == QLatin1String("apply"); }, 2000);
    gate(3, commandArrived && onViewport.presses == 0,
         QStringLiteral("a press on Apply reaches the page and comes back to C++ as a command in %1 ms; "
                        "the viewport saw %2 presses")
             .arg(commandArrived ? commandClock.nsecsElapsed() / 1.0e6 : -1.0, 0, 'f', 1)
             .arg(onViewport.presses));

    // An orbit that starts on the viewport and crosses the card stays an orbit.
    const QPoint dragFrom(card.left() - 80, card.center().y());
    const QPoint dragTo = card.center();
    onViewport.reset();
    onPage.reset();
    QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, toWindow(dragFrom));
    constexpr int kDragSteps = 16;
    for (int i = 1; i <= kDragSteps; ++i) {
        const QPoint at = dragFrom + (dragTo - dragFrom) * i / kDragSteps;
        QTest::mouseMove(win, toWindow(at));
    }
    QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, toWindow(dragTo));
    settle(200);
    gate(3, onViewport.presses == 1 && onViewport.moves >= kDragSteps && onViewport.releases == 1 &&
                onPage.total() == 0,
         QStringLiteral("an orbit from the viewport across the card stays with the viewport: %1 press, "
                        "%2 moves, %3 release there; %4 events to the page")
             .arg(onViewport.presses).arg(onViewport.moves).arg(onViewport.releases).arg(onPage.total()));
    view->removeEventFilter(&onViewport);
    render->removeEventFilter(&onPage);
    web->removeEventFilter(&onPage);

    // ================================================================== gate 4
    view->fitAll();
    settle(300);
    waitFully(300);
    // Orbit: the chip fades, by decision, so lag is never on screen there.
    {
        const QPointF at(view->width() * 0.45, view->height() * 0.5);
        QMouseEvent press(QEvent::MouseButtonPress, at, view->mapToGlobal(at), Qt::RightButton,
                          Qt::RightButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &press);
        waitFully(400);
        const QString opacity =
            jsString(web, QStringLiteral("getComputedStyle(document.querySelector('.chip')).opacity"));
        const QRect chipRect = jsRect(web, QStringLiteral(".chip")).toAlignedRect();
        const bool chipOutOfHits = !chipRect.isEmpty() && !overlay->hitRegion().intersects(chipRect);
        QMouseEvent release(QEvent::MouseButtonRelease, at, view->mapToGlobal(at), Qt::RightButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &release);
        waitFully(400);
        const QString back =
            jsString(web, QStringLiteral("getComputedStyle(document.querySelector('.chip')).opacity"));
        gate(4, opacity == QLatin1String("0") && chipOutOfHits && back == QLatin1String("1"),
             QStringLiteral("while the right button is held the chip fades (opacity %1, out of the click "
                            "mask: %2) and returns on release (opacity %3)")
                 .arg(opacity, chipOutOfHits ? "yes" : "no", back));
    }
    // Pan and zoom do not fade it, so their lag is what a user can see.
    overlay->clearChipSamples();
    const Run pan = dragRun(view, Qt::MiddleButton, kMoves, overlay, &anchorAt, &latestShown);
    std::vector<double> latencies;
    for (const auto& s : overlay->chipSamples()) latencies.push_back(s.latencyMs);
    std::sort(latencies.begin(), latencies.end());
    const double p50 = latencies.empty() ? -1 : latencies[latencies.size() / 2];
    const double p95 = latencies.empty() ? -1 : latencies[std::min(latencies.size() - 1, latencies.size() * 95 / 100)];
    info(QStringLiteral("pan: %1 chip reports, send->painted latency p50 %2 ms, p95 %3 ms")
             .arg(latencies.size()).arg(p50, 0, 'f', 1).arg(p95, 0, 'f', 1));
    gate(4, pan.lagMeanRevs <= 1.5,
         QStringLiteral("during a pan the chip trails by %1 frames on average (max %2), %3 px on average "
                        "(max %4 px) at %5 px per step")
             .arg(pan.lagMeanRevs, 0, 'f', 2).arg(pan.lagMaxRevs, 0, 'f', 0)
             .arg(pan.errMeanPx, 0, 'f', 1).arg(pan.errMaxPx, 0, 'f', 1)
             .arg(std::hypot(4.0, 1.5), 0, 'f', 1));
    view->fitAll();
    settle(300);

    // ================================================================== gate 5
    const QString appDir = QCoreApplication::applicationDirPath();
    const qint64 total = dirSize(appDir, [](const QString& rel) {
        return !rel.endsWith(QLatin1String(".pdb")) && !rel.endsWith(QLatin1String(".ilk")) &&
               !rel.startsWith(QLatin1String("web-spike-out"));
    });
    const qint64 webPart = dirSize(appDir, [](const QString& rel) {
        const QString l = rel.toLower();
        return !l.endsWith(QLatin1String(".pdb")) &&
               (l.contains(QLatin1String("webengine")) || l.startsWith(QLatin1String("resources/")) ||
                l.startsWith(QLatin1String("qt6qml")) || l.startsWith(QLatin1String("qt6quick")) ||
                l.startsWith(QLatin1String("qt6webchannel")) || l.startsWith(QLatin1String("qt6positioning")) ||
                l.startsWith(QLatin1String("qml/")) || l.startsWith(QLatin1String("web/")));
    });
    gate(5, overlay->loadOk(),
         QStringLiteral("the page loads from the deployed build directory; the directory holds %1 MB without "
                        "debug symbols, of which WebEngine and what it pulls in is %2 MB")
             .arg(total / 1048576.0, 0, 'f', 0)
             .arg(webPart / 1048576.0, 0, 'f', 0));

    // ================================================================== gate 6
    gPreviousHandler = QAccessible::installUpdateHandler(recordAccessEvent);
    QAccessible::setActive(true);
    QAccessibleInterface* root = QAccessible::queryAccessibleInterface(web);
    QAccessibleInterface* applyNode = nullptr;
    QAccessibleInterface* slider = nullptr;
    waitFor([&] {
        root = QAccessible::queryAccessibleInterface(web);
        applyNode = findAccessible(root, QAccessible::Button, QStringLiteral("Apply"));
        slider = findAccessible(root, QAccessible::Slider, QStringLiteral("Width"));
        return applyNode != nullptr && slider != nullptr;
    }, 8000);
    gate(6, applyNode != nullptr && slider != nullptr,
         QStringLiteral("the page's controls are in Qt's accessibility tree by name: button Apply %1, "
                        "slider Width %2")
             .arg(applyNode ? "found" : "MISSING", slider ? "found" : "MISSING"));
    gAccessEvents.clear();
    emit overlay->bridge()->toastRequested(QStringLiteral("Announced toast"), QStringLiteral("note"));
    waitFully(1500);
    const bool anyAlert = std::any_of(gAccessEvents.begin(), gAccessEvents.end(),
                                      [](QAccessible::Event t) { return t == QAccessible::Alert; });
    QAccessibleInterface* liveText = nullptr;
    waitFor([&] {
        liveText = findAccessible(QAccessible::queryAccessibleInterface(web), QAccessible::StaticText,
                                  QStringLiteral("Announced toast"));
        if (!liveText)
            liveText = findAccessible(QAccessible::queryAccessibleInterface(web), QAccessible::Section,
                                      QStringLiteral("Announced toast"));
        return liveText != nullptr;
    }, 3000);
    gate(6, !anyAlert,
         QStringLiteral("a toast raises no QAccessible::Alert, the event Qt answers with a system sound "
                        "(%1 accessibility events while it appeared; toast text in the tree: %2)")
             .arg(gAccessEvents.size())
             .arg(liveText ? "yes" : "not found by role"));
    info(QStringLiteral("Narrator itself is not driven here: it reads the same tree, and listening to it "
                        "is a manual check"));

    // ------------------------------------------------------------------ done
    gSummary.insert(QStringLiteral("failures"), gFailures);
    gSummary.insert(QStringLiteral("scale"), qEnvironmentVariable("QT_SCALE_FACTOR", QStringLiteral("1")));
    QFile json(outDir + QStringLiteral("/summary.json"));
    if (json.open(QIODevice::WriteOnly)) json.write(QJsonDocument(gSummary).toJson());
    std::printf("\n%s - %d failing line%s\n", gFailures == 0 ? "ALL GATES PASS" : "GATES FAILING",
                gFailures, gFailures == 1 ? "" : "s");
    std::fflush(stdout);
    QAccessible::installUpdateHandler(gPreviousHandler);
    delete overlay;
    return gFailures == 0 ? 0 : 1;
}
