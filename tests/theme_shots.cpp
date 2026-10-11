// THEME SHOTS (themes part 2): the mockups' own scene in the real app, one
// composited capture per theme, at the mockups' 1280 x 800 - so each theme is
// compared with the picture it was picked from, side by side, rather than
// judged from memory. A bookcase (Carcass folder, Shelf, Back panel), the left
// side selected so its sizes show, the Items drawer open, a Note with Undo.
//
// Drives the window in-process like chrome_smoke: no OS input, windows kept
// behind the user's own. Prints the paths it wrote; asserts nothing - the
// eye on the side-by-side is the check, and the pixel checks live in the
// suites.
//
// Usage: theme_shots <output-dir> [theme-id ...]
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "DocumentModel.h"
#include "FurnitureStore.h"
#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "ui/Themes.h"
#include "ui/HintBalloon.h"
#include "ui/WalkthroughPanel.h"
#include "CameraController.h"
#include <QMouseEvent>
#include "web/EditorChrome.h"
#include "web/WebHost.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineView>

#include <cstdio>
#include <functional>

namespace {

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

QAction* actionNamed(QWidget* window, const QString& text)
{
    for (QAction* a : window->findChildren<QAction*>())
        if (QString(a->text()).remove(QLatin1Char('&')) == text) return a;
    return nullptr;
}

#ifdef _WIN32
QImage capture(QWidget* window)
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
    QImage out;
    HBITMAP bitmap = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT))
            out = QImage(static_cast<const uchar*>(bits), w, h, QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return out;
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

}  // namespace

int main(int argc, char* argv[])
{
    QSurfaceFormat::setDefaultFormat(OcctViewWidget::surfaceFormat());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    WebHost::prepareApplication();
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    Theme::apply(app);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::current().filePath("theme-shots");
    QDir().mkpath(outDir);
    QStringList only;
    for (int i = 2; i < argc; ++i) only << QString::fromLocal8Bit(argv[i]);
#ifdef _WIN32
    QTimer behind;
    QObject::connect(&behind, &QTimer::timeout, [] { keepBehind(); });
    behind.start(125);
#endif

    // The mockups' bookcase: two 600-tall sides, top, bottom, a shelf and a
    // back panel; the four carcass parts in a folder.
    QTemporaryDir library;
    QString furnitureId;
    int sideLeft = 0;
    {
        FurnitureStore store(library.path());
        furnitureId = store.createFurniture(QStringLiteral("Hallway cabinet"));
        DocumentModel doc;
        sideLeft = doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 18, 300, 600));
        const int sideRight = doc.addSolid(ModelingOps::makeBox(gp_Pnt(818, 0, 0), 18, 300, 600));
        const int top = doc.addSolid(ModelingOps::makeBox(gp_Pnt(18, 0, 582), 800, 300, 18));
        const int bottom = doc.addSolid(ModelingOps::makeBox(gp_Pnt(18, 0, 0), 800, 300, 18));
        const int shelf = doc.addSolid(ModelingOps::makeBox(gp_Pnt(18, 0, 291), 800, 294, 18));
        const int back = doc.addSolid(ModelingOps::makeBox(gp_Pnt(18, 294, 18), 800, 6, 564));
        doc.setItemName(sideLeft, "Side left");
        doc.setItemName(sideRight, "Side right");
        doc.setItemName(top, "Top");
        doc.setItemName(bottom, "Bottom");
        doc.setItemName(shelf, "Shelf");
        doc.setItemName(back, "Back panel");
        const int carcass = doc.createGroup("Carcass");
        for (int id : {sideLeft, sideRight, top, bottom}) doc.setItemGroup(id, carcass);
        store.saveFurniture(furnitureId, doc, QImage());
    }

    MainWindow window(nullptr, /*persistProgress=*/false, library.path());
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1280, 800);
    window.useHtmlChrome();
    window.show();
    window.view()->setAnimationsEnabled(false);
    window.openFurniture(furnitureId);
    EditorChrome* chrome = window.htmlChrome();
    waitFor([&] { return chrome && chrome->isLive(); }, 20000);
    pump(500);

    if (QAction* items = actionNamed(&window, QStringLiteral("Items")); items && !items->isChecked()) items->trigger();
    // The side panel's id in the opened document (ids restart per document).
    int selectId = 0;
    for (const auto& s : window.document().solids())
        if (window.document().nameOf(s.id) == "Side left") selectId = s.id;
    window.view()->setSelectedSolids({selectId});
    pump(600);
    // The mockups' camera: from the front-left, a little above.
    {
        CameraState st = window.view()->camera().state();
        st.azimuthDeg = 135.0;
        st.elevationDeg = 24.0;
        window.view()->setCameraStateNow(st);
        window.view()->fitAll();
        st = window.view()->camera().state();
        st.distance *= 1.3;
        window.view()->setCameraStateNow(st);
        pump(300);
    }
    // One real change, so the Note's Undo is live as in the mockups.
    for (const auto& s : window.document().solids())
        if (window.document().nameOf(s.id) == "Shelf") {
            gp_Trsf nudge;
            nudge.SetTranslation(gp_Vec(0, 0, 1));
            window.transformBody(s.id, nudge);
        }
    window.view()->setSelectedSolids({selectId});
    pump(300);
    // No first-run guide and no hint: the mockups show neither.
    if (auto* guide = window.findChild<WalkthroughPanel*>()) {
        if (QWidget* skip = guide->skipControl()) {
            const QPoint c = skip->rect().center();
            QMouseEvent press(QEvent::MouseButtonPress, c, skip->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, c, skip->mapToGlobal(c), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(skip, &press);
            QCoreApplication::sendEvent(skip, &release);
        }
    }
    for (auto* hint : window.findChildren<HintBalloon*>()) hint->hide();
    pump(400);

    for (const Themes::Info& theme : Themes::all()) {
        if (!only.isEmpty() && !only.contains(theme.id)) continue;
        Themes::select(theme.id);
        pump(700);
        for (auto* hint : window.findChildren<HintBalloon*>()) hint->hide();
        chrome->showToast(QStringLiteral("Moved Shelf above Back panel"), EditorChrome::Kind::Note, true,
                          window.document().revision());
        pump(900);
#ifdef _WIN32
        const QImage shot = capture(&window);
        const QString path = outDir + "/app-" + theme.id + ".png";
        shot.save(path);
        std::printf("%s (%dx%d)\n", qPrintable(path), shot.width(), shot.height());
#endif
    }
    Themes::select(Themes::graphiteId());
    return 0;
}
