//
// Drives the real MainWindow with synthetic Qt events delivered straight to the
// widgets. Nothing goes through the OS input queue, so this never moves the
// cursor, never steals focus, and the machine stays usable while it runs.
//
// A window does still appear: OCCT's V3d_View needs a real native window with a
// GL surface, so `-platform offscreen` is not an option. It is shown without
// activating and parked in a corner.
//
// Not part of ctest: it needs a GPU and a window server. The headless tests stay
// the CI gate; this covers the wiring they cannot reach - that clicks become
// sketch points, that picking returns the right solids, that the document and
// the viewport stay in agreement.
//
// FIRST, and deliberately so - the one file in this project that includes
// windows.h at all. CLAUDE.md's rule is "OCCT headers before <windows.h>
// where possible", because OCCT's Handle() is a macro that some Windows
// headers trip over; here it is not possible, and the other order is the one
// that works. Putting windows.h first means the macro does not exist yet
// while the Windows headers are parsed, which sidesteps the clash entirely.
// Something in the Qt/OCCT include chain otherwise pulls windows.h in with
// GDI and USER excluded, so a later include is a silent no-op and
// GetWindowRect/BITMAPINFO simply are not there. NOMINMAX keeps the min/max
// macros out of std::min/std::max's way. Only printWindowCapture() needs any
// of this.
#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #ifndef PW_RENDERFULLCONTENT
    #define PW_RENDERFULLCONTENT 0x00000002
  #endif
#endif

#include "AppearancePanel.h"
#include "CameraController.h"
#include "DimensionRenderer.h"
#include "DocumentModel.h"
#include "ExtrudePreview.h"
#include "GridRenderer.h"
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "PullArrow.h"
#include "SketchController.h"
#include "AppBar.h"
#include "AxisGizmo.h"
#include "BevelArrow.h"
#include "ShortcutSheet.h"
#include "Theme.h"
#include "Toast.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "UserProgress.h"
#include "ViewportOverlay.h"
#include "WalkthroughPanel.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QSpinBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QWheelEvent>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <ElSLib.hxx>
#include <TopAbs_Orientation.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

// Every check() EXECUTION this run, printed on the PASS line and asserted
// against kCheckFloor at the end.
//
// A suite that counts only its failures cannot tell a green run from a run
// that quietly stopped asking. Every `if (something) { check(...); }` in this
// file - and there are dozens, each of them deliberate, because a probe whose
// setup failed must not go on to dereference a null - is a place where a
// changed behaviour silently takes its checks with it. That has happened here
// before: five shadow checks went silent instead of red when the behaviour
// under them changed. The individual fix is to pin each guard with a check of
// its own; the systemic one is this counter, which notices a drop wherever it
// happens and whether or not anybody remembered to pin it.
int g_checks = 0;

// The floor a full run must reach.
//
// HOW TO UPDATE IT: add your checks, run the suite at native scale, read the
// "checks" figure off the PASS line, and put that number here. It is a FLOOR,
// not an equality, so a probe that legitimately runs a variable number of
// times (checkNoBlackLine over however many captures a run takes) cannot make
// it brittle.
//
// Never lower it to make a run pass. A count that has gone DOWN means a guard
// stopped letting its checks run, which is the one thing this constant exists
// to catch; find the guard, not a smaller number.
constexpr int kCheckFloor = 1005;

void check(bool condition, const QString& what)
{
    ++g_checks;
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", qPrintable(what));
    if (!condition) ++g_failures;
}

// RAII for the one probe below that needs to seed QSettings before
// constructing a persistProgress=true MainWindow: saves the real
// organization/application name and QSettings::defaultFormat(), then
// switches to a dedicated, file-backed identity that cannot collide with
// whatever the developer's own use of the real app has recorded - and
// restores everything in the destructor, so the restore happens even if
// something between construction and the end of the scope were ever changed
// to throw or return early, rather than relying on sequential code reaching
// a restore line at the bottom.
//
// IniFormat plus a temp-directory path keeps the whole probe out of the
// registry entirely, rather than merely under a distinctly named key inside
// it - the file is left on disk afterward (temp directories are routinely
// cleared by the OS; a registry key is not), but it never touches the real
// app's actual settings location either way.
class ScopedTestSettings {
public:
    ScopedTestSettings()
        : myOrg(QCoreApplication::organizationName())
        , myApp(QCoreApplication::applicationName())
        , myFormat(QSettings::defaultFormat())
    {
        const QString path = QDir::tempPath() + QStringLiteral("/furnifyme-gui_smoke-settings");
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName(QStringLiteral("FurnifyMe-gui_smoke"));
        QCoreApplication::setApplicationName(QStringLiteral("returning-user-probe"));
    }

    ~ScopedTestSettings()
    {
        QSettings().clear();   // this probe's own entries, wherever they landed
        QSettings::setDefaultFormat(myFormat);
        QCoreApplication::setOrganizationName(myOrg);
        QCoreApplication::setApplicationName(myApp);
    }

    ScopedTestSettings(const ScopedTestSettings&) = delete;
    ScopedTestSettings& operator=(const ScopedTestSettings&) = delete;

private:
    QString myOrg;
    QString myApp;
    QSettings::Format myFormat;
};

// Lets the event loop breathe so Qt delivers exposure/resize and OCCT redraws.
void settle(int ms = 250)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void clickAt(QWidget* target, const QPointF& pos,
             Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPointF global = target->mapToGlobal(pos);

    QMouseEvent press(QEvent::MouseButtonPress, pos, global,
                      Qt::LeftButton, Qt::LeftButton, mods);
    QCoreApplication::sendEvent(target, &press);

    QMouseEvent release(QEvent::MouseButtonRelease, pos, global,
                        Qt::LeftButton, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &release);

    settle(80);
}

// A press at one point and a release at ANOTHER, with no move event between
// them. Deliberately not a drag: it isolates what a handler does with the
// release POSITION from what it does with the movement, which is the only way
// to tell a viewport that swallows its own release-pick from one that
// re-picks and happens to land on the same thing. dragButton() cannot answer
// that question, because a drag that moves also commits.
void pressThenReleaseAt(QWidget* target, const QPointF& press, const QPointF& release,
                        Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent down(QEvent::MouseButtonPress, press, target->mapToGlobal(press),
                     button, button, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &down);
    QMouseEvent up(QEvent::MouseButtonRelease, release, target->mapToGlobal(release),
                   button, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &up);
    settle(120);
}

// A hover move with no button down - the live dimension and the hover
// highlight both key off this, not a click.
void moveTo(QWidget* target, const QPointF& pos, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPointF global = target->mapToGlobal(pos);

    QMouseEvent move(QEvent::MouseMove, pos, global, Qt::NoButton, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &move);

    settle(80);
}

// A key press delivered the way a real one arrives: to whatever currently
// holds focus inside `scope`, not to the widget the test hopes will handle
// it. Aiming a key at a specific widget is precisely the blind spot that let
// the extrude preview ship with Enter and Escape reachable only while its own
// field kept focus - which the first click anywhere in the viewport took
// away. QWidget::focusWidget() rather than QApplication::focusWidget():
// every window in this suite carries WA_ShowWithoutActivating and so is never
// the OS-active one, which leaves the application-wide focus widget null.
void sendKeyTo(QWidget* scope, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    if (!scope) return;
    QWidget* target = scope->focusWidget();
    if (!target) target = scope;
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(target, &press);
    settle(120);
}

// Button drag delivered as press/move/release, for camera tests.
void dragButton(QWidget* target, const QPointF& from, const QPointF& to,
                Qt::MouseButton button, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, target->mapToGlobal(from),
                      button, button, mods);
    QCoreApplication::sendEvent(target, &press);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        QMouseEvent move(QEvent::MouseMove, p, target->mapToGlobal(p),
                         Qt::NoButton, button, mods);
        QCoreApplication::sendEvent(target, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, to, target->mapToGlobal(to),
                        button, Qt::NoButton, mods);
    QCoreApplication::sendEvent(target, &release);
    settle(120);
}

// Actions are looked up by their visible text, minus the mnemonic marker.
QAction* action(MainWindow& window, const QString& label)
{
    for (QAction* candidate : window.findChildren<QAction*>()) {
        if (candidate->text().remove(QLatin1Char('&')) == label) return candidate;
    }
    return nullptr;
}

// The persistent right-hand readout - a permanent widget on the status bar,
// which MainWindow keeps no accessor for, so it is found the same way the
// vocabulary sweep finds it.
QString stateLabelText(MainWindow& window)
{
    QString text;
    for (QLabel* label : window.statusBar()->findChildren<QLabel*>()) {
        if (!label->text().isEmpty()) text = label->text();
    }
    return text;
}

bool trigger(MainWindow& window, const QString& label)
{
    QAction* found = action(window, label);
    if (!found) {
        std::printf("[FAIL] no action named '%s'\n", qPrintable(label));
        ++g_failures;
        return false;
    }
    found->trigger();
    settle(120);
    return true;
}

// Draws a quad by clicking four points given as fractions of the viewport, so
// the test does not depend on a particular window size.
void sketchQuad(MainWindow& window, double x0, double y0, double x1, double y1)
{
    OcctViewWidget* view = window.view();
    const double w = view->width();
    const double h = view->height();

    clickAt(view, QPointF(x0 * w, y0 * h));
    clickAt(view, QPointF(x1 * w, y0 * h));
    clickAt(view, QPointF(x1 * w, y1 * h));
    clickAt(view, QPointF(x0 * w, y1 * h));
}

// Sketch-quad-then-extrude, for probes that only care about ending up with a
// given number of bodies and would otherwise repeat this boilerplate inline.
// The one copy of the banned list. Two blocks sweep with it now - the
// vocabulary block and the shortcut sheet's own painted copy - and a second
// literal list would be a vocabulary that drifts from itself.
QStringList bannedWords()
{
    // "bevel" joined the list in Milestone 2: it is the CODE's word for the
    // pair of operations (BevelArrow, bevelAxis) and it must never reach the
    // user, who is offered a Fillet that rounds an edge or a Chamfer that
    // flattens one - two operations with two names, not one vague one.
    return {QStringLiteral("Fuse"),  QStringLiteral("Solid"),
            QStringLiteral("OCCT"),  QStringLiteral("mm3"),
            QStringLiteral("(s)"),   QStringLiteral("Merge"),
            QStringLiteral("Join"),  QStringLiteral("bevel")};
}

bool buildBody(MainWindow& window, double x0, double y0, double x1, double y1, double height)
{
    trigger(window, QStringLiteral("Start Sketch"));
    sketchQuad(window, x0, y0, x1, y1);
    trigger(window, QStringLiteral("Finish Sketch"));
    return window.extrudePendingFace(height);
}

// --- Graphite fix-round-1 pixel probes --------------------------------------
// The image-diff pattern above ("does the whole widget's pixmap change at
// all") already passed against the PRE-fix-round chip, which painted a
// checked border, a hover fill and a dimmed disabled state of its own - none
// of it in the specific shape the anatomy contract calls for. These probes
// sample specific pixels instead, so a plausible-but-wrong revert (dropping
// the always-on border, swapping the inset ring back for a border colour
// change, un-dimming just the badge) fails loudly rather than passing
// because SOMETHING else still repainted.

// Renders a widget at its own logical size, DPI-neutral - QWidget::grab()
// scales by the screen's devicePixelRatio, which would make a pixel-exact
// probe's coordinates environment-dependent. Works on a hidden widget too
// (render() does not require the widget to be shown), which the isolated
// probe chip relies on.
QImage renderExact(QWidget* widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    return image;
}

// The same render, at a FRACTIONAL device-pixel ratio, onto a sentinel so an
// unpainted pixel has a name.
//
// renderExact() above is deliberately 1:1, which is what makes its pixel
// coordinates environment-independent - and which also makes it structurally
// blind to one whole class of defect. At 1:1 a widget's logical rect and its
// backing store are the same rectangle, so a pixel outside the logical rect
// cannot exist. At 150% - what this machine actually runs - a card 93 logical
// rows tall needs 139.5 device rows, Qt allocates 140, and QPainter's logical
// fill reaches 139 of them. Over OCCT's GL surface the leftover row is not
// transparent but whatever the driver left, and it measured as an exact
// 0,0,0 line the full width of the round/flatten chip in a magnified
// PrintWindow capture. Ceil, not round: the store is never SHORTER than the
// logical rect, and a probe that cut the row off would be measuring nothing.
// The longest horizontal run of PURE BLACK inside a PrintWindow capture,
// ignoring `inset` pixels of frame on every side. Writes where it starts.
//
// Nothing this app paints is 0,0,0 - the darkest token is chrome() at
// #1b1b1d, and the viewport clears to viewport() - so a black run across the
// composited window is a run of pixels NOTHING painted: a widget's backing
// store where its own painter never reached, with OCCT's on-screen GL surface
// unable to show through a Qt child sitting on top of it. That is the exact
// failure the floating-surface family exists to prevent, and CLAUDE.md has
// already paid for it twice (the drawer's corner nubs, the rail's black band).
//
// Measured over the whole window rather than one card's footprint, which is
// both stronger and more honest: mapping a widget's logical rect into a
// PrintWindow capture needs a scale AND an offset, since Windows 11 includes
// the invisible resize frame in what it hands back, and a probe whose region
// is a few pixels out reports zero exactly as loudly as a clean window does.
// The inset drops that frame and the uncaptured strip along the bottom, both
// of which are legitimately black and belong to no widget.
//
// renderExact() is structurally blind to this class of defect: it renders one
// widget at 1:1 into an image of precisely that widget's logical size, where a
// pixel outside the logical rect cannot exist.
int longestBlackRun(const QImage& image, int inset, QPoint& startsAt)
{
    int best = 0;
    for (int y = inset; y < image.height() - inset; ++y) {
        int run = 0;
        for (int x = inset; x < image.width() - inset; ++x) {
            if (qGray(image.pixel(x, y)) == 0) {
                ++run;
                if (run > best) {
                    best = run;
                    startsAt = QPoint(x - run + 1, y);
                }
            } else {
                run = 0;
            }
        }
    }
    return best;
}

// Every gizmo capture goes through this, not just the one that found the bug.
// A class of defect covered at one call site is an instance, and the round/
// flatten chip was only the first card whose logical size and position did not
// land on whole device pixels - the same sweep run over the face-pull capture
// is what says the rest of the family is clean rather than untested.
void checkNoBlackLine(const QImage& shot, const QString& label)
{
    constexpr int kFrame = 24;     // Windows 11's invisible resize frame
    constexpr int kAllowed = 24;   // longer than any antialiasing artefact
    // Non-vacuity first: a null or tiny capture sweeps nothing and reports
    // clean exactly as loudly as a clean window does.
    check(!shot.isNull() && shot.width() > kFrame * 4 && shot.height() > kFrame * 4,
          QStringLiteral("the %1 capture is a real window to sweep (%2x%3)")
              .arg(label).arg(shot.width()).arg(shot.height()));
    if (shot.isNull()) return;

    QPoint at;
    const int run = longestBlackRun(shot, kFrame, at);
    check(run < kAllowed,
          QStringLiteral("and no widget over the 3D area leaves an unpainted black line "
                         "across the %1 capture (%2)")
              .arg(label)
              .arg(run < kAllowed
                       ? QStringLiteral("longest run %1 px").arg(run)
                       : QStringLiteral("%1 px starting at %2,%3")
                             .arg(run).arg(at.x()).arg(at.y())));
}

// The ONE capture that shows the app as the user sees it: Qt's overlay
// widgets composited over OCCT's on-screen GL surface. Neither half-measure
// can do that alone - QWidget::grab() renders the widget tree and the
// viewport paints nothing into it (paintEngine() is null, by design), while
// V3d_View::Dump() renders the 3D scene and knows nothing about the Qt cards
// floating on top. PW_RENDERFULLCONTENT asks DWM for the window's real
// composited content, which is exactly both.
//
// In-process and CAPTURE ONLY: it reads the window this suite already owns
// and injects nothing, so it does not break the no-OS-input rule the way
// SetCursorPos/mouse_event did. Being in-process is also what makes it usable
// at a precise moment - a mid-drag gizmo state cannot be caught by an
// external screenshot tool racing a millisecond-long drag.
//
// Returns the captured image (null on failure) as well as writing it, so a
// caller can crop or magnify the same pixels it just saved.
QImage printWindowCapture(QWidget* widget, const QString& path)
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(widget->window()->winId());
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return QImage();
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return QImage();

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // top-down, to match QImage's row order
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage shot;
    if (bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT)) {
            shot = QImage(static_cast<const uchar*>(bits), width, height,
                          QImage::Format_RGB32)
                       .copy();   // copy: the DIB is about to be destroyed
        }
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!shot.isNull()) shot.save(path);
    return shot;
#else
    // No PrintWindow off Windows; the widget grab at least records the
    // overlay geometry, which is what the layout checks stand on.
    const QImage shot = widget->window()->grab().toImage();
    shot.save(path);
    return shot;
#endif
}

// Straight-line RGB distance - used to say "this pixel reads as X, not Y"
// without demanding bit-exact equality, which a 1px antialiased stroke can
// never guarantee at the exact pixel a naive test would pick.
double colorDistance(const QColor& a, const QColor& b)
{
    const double dr = a.red() - b.red();
    const double dg = a.green() - b.green();
    const double db = a.blue() - b.blue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// Mean of R+G+B/3 over a rectangular region - used where the signal is
// "this region got darker", not any one pixel's exact colour, since a
// right-aligned run of glyphs is mostly background between the letterforms.
double averageLuminance(const QImage& image, const QRect& region)
{
    double sum = 0.0;
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
            const QColor c = image.pixelColor(x, y);
            sum += (c.red() + c.green() + c.blue()) / 3.0;
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

// Confirms a floating card genuinely goes through Theme::paintSurface():
// panel() fill well inside the card, and `edgeColour` - the family's plain
// border() for a card that adds no accent of its own at the sampled edge,
// or the card's own override colour (WalkthroughPanel's accent() outline,
// which replaces the family border everywhere) - measured closer at the
// edge than the interior reads.
//
// The third check used to be "a shadow is genuinely present just outside the
// card, in the margin surfaceShadowMargin() reserves". That margin is gone
// and so is the shadow: fix round 1's ruling is that NOTHING paints
// translucent pixels over the GL surface, because there is nothing behind
// them in the widget's backing store to blend with and the alpha lands on
// black. So the check is inverted rather than dropped - the card must be
// fully OPAQUE right out to its own edge, which is the property the ruling
// actually cares about and which a returning shadow would fail on its first
// row. Swept around the whole perimeter, not sampled at one point, because a
// shadow reintroduced on one side only is exactly the shape this regresses
// in.
//
// `edgePoint` and `interiorSearch` are supplied by the caller, in the
// widget's own local coordinates, rather than derived here - which edge is
// safe to sample (clear of a stripe, a pill, a skip control) is a per-card
// decision, not a general one.
void checkFamilySurface(QWidget* widget, const QPoint& edgePoint, const QRect& interiorSearch,
                        const QColor& edgeColour, const QString& label)
{
    const QImage img = renderExact(widget);
    // Asserted unconditionally rather than as an `if (bad) check(false)`
    // bail-out. In that shape it was the ONE check() in this file that never
    // executed in a run - measured, by instrumenting check() with __LINE__ and
    // differencing the executed set against every call site - so a caller
    // handing it a probe point outside the widget would have reported nothing
    // at all rather than one loud failure. Its message was inverted too: it
    // fired when the point fell OUTSIDE and said "inside". As an assertion the
    // sentence is right and it always runs.
    const bool probeInside = img.rect().contains(edgePoint);
    check(probeInside,
          QStringLiteral("%1: the edge probe point falls inside the rendered image")
              .arg(label));
    if (!probeInside) return;

    // The card's own content - a title, a message, a list of rows - can sit
    // almost anywhere in its interior, so this searches the whole region for
    // whichever pixel reads closest to panel() rather than trusting one
    // hand-picked point to have dodged every card's text. Real background
    // is common even in a text-heavy card (between glyphs, between rows), so
    // the best match found is expected to land very close to the token
    // itself, not just closer than some other colour.
    double bestPanelDist = 1e9;
    QColor bestPanelColor = Theme::panel();
    for (int y = interiorSearch.top(); y <= interiorSearch.bottom(); y += 2) {
        for (int x = interiorSearch.left(); x <= interiorSearch.right(); x += 2) {
            if (!img.rect().contains(x, y)) continue;
            const QColor c = img.pixelColor(x, y);
            const double d = colorDistance(c, Theme::panel());
            if (d < bestPanelDist) { bestPanelDist = d; bestPanelColor = c; }
        }
    }
    check(bestPanelDist < 12.0,
          QStringLiteral("%1's interior contains genuine panel() fill somewhere "
                         "clear of its own painted content").arg(label));

    const QColor edge = img.pixelColor(edgePoint);
    check(colorDistance(edge, edgeColour) < colorDistance(bestPanelColor, edgeColour),
          QStringLiteral("%1's edge reads closer to its border colour than its "
                         "panel() interior does").arg(label));

    // Fully opaque out to the edge - the WHOLE perimeter now, corners
    // included. Corners used to be excluded here: a rounded card's fill does
    // not cover the small triangle outside the rounded shape and inside the
    // widget rect at each corner, and that area used to be left unpainted.
    // Theme::paintSurface() now fills the widget's full rect with an opaque
    // ground before the rounded panel (this task's fix - see Theme.h), so
    // that triangle is covered too and the sweep no longer has to dodge it.
    int seeThrough = 0;
    int visited = 0;
    QPoint firstSeeThrough;
    auto sweep = [&](int x, int y) {
        if (!img.rect().contains(x, y)) return;
        ++visited;
        if (qAlpha(img.pixel(x, y)) == 255) return;
        if (seeThrough == 0) firstSeeThrough = QPoint(x, y);
        ++seeThrough;
    };
    for (int x = 0; x < img.width(); ++x) {
        sweep(x, 0);
        sweep(x, img.height() - 1);
    }
    for (int y = 0; y < img.height(); ++y) {
        sweep(0, y);
        sweep(img.width() - 1, y);
    }
    // Non-vacuity, and not a formality: `seeThrough == 0` is exactly as true
    // of a card whose perimeter was never sampled at all. Rendering can hand
    // back a null image (a zero-sized widget), which is one way to sweep
    // nothing; `visited > 0` catches that regardless of width or height.
    check(visited > 0,
          QStringLiteral("%1's perimeter sweep actually visited pixels, so the "
                         "opacity check below is not vacuous (%2 sampled, card "
                         "%3x%4)")
              .arg(label).arg(visited).arg(img.width()).arg(img.height()));
    check(seeThrough == 0,
          QStringLiteral("%1 is opaque right out to its own edge - no translucent "
                         "pixels over the GL surface (%2)")
              .arg(label)
              .arg(seeThrough == 0
                       ? QStringLiteral("every edge pixel solid")
                       : QStringLiteral("%1 see-through, first at %2,%3")
                             .arg(seeThrough).arg(firstSeeThrough.x()).arg(firstSeeThrough.y())));
}

}  // namespace

int main(int argc, char* argv[])
{
    // Unbuffered, deliberately. This suite drives a real GL window through a
    // kernel that can abort the process, and stdout to a pipe or a file is
    // block-buffered - so the last few kilobytes of a run that dies are simply
    // lost, and the visible tail points at a check that passed long before the
    // fault. A crash whose location you cannot read costs far more than the
    // syscall per line this gives up.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifndef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    // Exercise what actually ships: main.cpp themes the app before building the
    // window, so the test must too, or it checks an app nobody runs.
    Theme::apply(app);

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

    // Never persist: a suite whose behaviour depends on how often the developer
    // ran the real app is not a suite.
    MainWindow window(nullptr, /*persistProgress=*/false);
    // Show without taking focus: the point of this harness is that the user can
    // keep working while it runs.
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.resize(1200, 800);
    window.move(40, 40);
    window.show();
    settle(900);
    window.view()->setAnimationsEnabled(false);   // deterministic camera for the suite

    // --- the shell is laid out correctly BEFORE anybody touches it ------------
    // First thing after the window appears, ahead of every trigger(), click
    // and resize below - because the defect this catches repaired itself on
    // the first of any of them. The only relayout the application performs
    // between construction and the user's first action runs inside the
    // window's own show sequence, where QMainWindow sizes the viewport before
    // showChildren() marks the rail visible; a left-edge computation guarded
    // on isVisible() therefore saw no rail, and the drawer opened on top of
    // it with the rail's top six buttons underneath. Every check further down
    // this file was green while that was true.
    {
        OcctViewWidget* v = window.view();
        ItemsPanel* drawer = window.itemsPanel();
        ToolCluster* startupRail = v ? v->findChild<ToolCluster*>() : nullptr;
        check(drawer != nullptr && startupRail != nullptr,
              "the shell has a drawer and a rail as soon as it is on screen");
        if (drawer && startupRail) {
            check(!drawer->geometry().intersects(startupRail->geometry()),
                  QStringLiteral("and the drawer is clear of the rail on the very first "
                                 "frame, before any action has re-laid anything out "
                                 "(drawer %1,%2 %3x%4 - rail %5,%6 %7x%8)")
                      .arg(drawer->x()).arg(drawer->y())
                      .arg(drawer->width()).arg(drawer->height())
                      .arg(startupRail->x()).arg(startupRail->y())
                      .arg(startupRail->width()).arg(startupRail->height()));
            // The rail's own buttons are reachable, not buried under it -
            // the symptom a geometry check alone could miss if the two ever
            // merely touched.
            const QVector<ToolChip*> startupChips = startupRail->chips();
            QStringList buried;
            for (ToolChip* chip : startupChips) {
                const QPoint c = chip->mapTo(v, QPoint(chip->width() / 2, chip->height() / 2));
                if (v->childAt(c) != chip) buried << chip->text();
            }
            check(buried.isEmpty(),
                  QStringLiteral("and every rail button is clickable from the first "
                                 "frame (%1)")
                      .arg(buried.isEmpty() ? QStringLiteral("all are")
                                            : buried.join(QStringLiteral(", "))));
        }
    }

    // --- the walkthrough appears for a newcomer -------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr, "a new user gets the guided first build");
        check(guide != nullptr && guide->isVisible(), "the guide is visible on first run");
        check(guide != nullptr && guide->completedSteps() == 0,
              "no steps are complete before the user does anything");
    }

    // --- the hint balloon has nothing to say before any body exists -----------
    // Visibility is asserted directly (isVisible()), not inferred from
    // currentHint() alone: a stub that sets myText without ever calling
    // show()/hide() would pass a text-only check just as well.
    {
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the window has a hint balloon");
        check(hint != nullptr && !hint->isVisible() && hint->currentHint().isEmpty(),
              "no hint is up before any body exists");
    }

    OcctViewWidget* view = window.view();
    check(view != nullptr && view->width() > 100, "viewport has a usable size");

    // --- camera startup state -------------------------------------------------
    {
        const CameraState& cam = view->camera().state();
        check(std::fabs(cam.azimuthDeg - (-45.0)) < 1e-6, "startup azimuth is -45");
        check(std::fabs(cam.elevationDeg - 30.0) < 1e-6, "startup elevation is +30");
        check(std::fabs(cam.distance - 700.0) < 1e-6, "startup distance is 700mm");
    }

    // --- turntable input ------------------------------------------------------
    {
        const double az0 = view->camera().state().azimuthDeg;
        const gp_Dir up0 = view->camera().upVector();
        const gp_Pnt orbitTarget0 = view->camera().state().target;
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5 + 100.0, view->height() * 0.5),
                   Qt::RightButton);
        check(std::fabs(view->camera().state().azimuthDeg - az0) > 5.0,
              "a horizontal RMB drag orbits azimuth");
        check(view->camera().upVector().Z() > 0.0 && up0.Z() > 0.0,
              "orbiting never rolls: up keeps its +Z component");
        // Unity-style: orbiting spins around the current view target, so the
        // target itself must not move - no cursor-anchored re-pivoting.
        check(view->camera().state().target.Distance(orbitTarget0) < 1e-6,
              "orbiting leaves the view target where it was");

        const gp_Pnt target0 = view->camera().state().target;
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5 + 80.0, view->height() * 0.5 + 40.0),
                   Qt::MiddleButton);
        check(view->camera().state().target.Distance(target0) > 1.0,
              "an MMB drag pans the target");

        // Elevation clamp holds through input: a huge vertical drag stops at 88.
        dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                         QPointF(view->width() * 0.5, view->height() * 0.5 + 2000.0),
                   Qt::RightButton);
        check(view->camera().state().elevationDeg >= -88.0 - 1e-6 &&
              view->camera().state().elevationDeg <= 88.0 + 1e-6,
              "elevation stays inside the clamp under wild input");

        // Restore the exact startup pose: every later check clicks at fractions
        // tuned for it, and this block has dragged the camera all over the sky.
        view->camera().setState(CameraState{});
        trigger(window, QStringLiteral("Axonometric"));
        settle(200);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3,
              "camera restored to the startup pose for the rest of the suite");
    }

    // --- axis gizmo -----------------------------------------------------------
    // The gizmo is axes and tips only now: its painted label chip moved into
    // the app bar, which shows the projection rather than the direction. The
    // direction name has one source, OcctViewWidget::viewDirectionName(),
    // which this block reads as its oracle for "square onto a world axis".
    {
        AxisGizmo* gizmo = window.findChild<AxisGizmo*>();
        check(gizmo != nullptr, "the viewport has an axis gizmo");
        if (gizmo) {
            // Clicking the +Z cone looks down from above.
            clickAt(gizmo, gizmo->tipCenter(2, true));
            settle(150);
            check(std::fabs(view->camera().state().elevationDeg - 88.0) < 1e-3,
                  "clicking the +Z cone goes to Top");
            check(view->viewDirectionName() == QStringLiteral("Top"),
                  "the camera reads as square onto Top when aligned");

            // An axis view is a face-on view, and a face-on view with
            // perspective convergence is not one. The arm click borrows
            // orthographic; the BASE mode is untouched, which is the whole
            // distinction (see CameraController::Projection).
            check(view->viewIsOrthographic(),
                  "and clicking an arm lands the camera orthographic");
            check(view->camera().baseProjection() ==
                      CameraController::Projection::Perspective,
                  "without changing the projection the user chose");
            {
                AppBar* projBar = qobject_cast<AppBar*>(window.menuWidget());
                QAbstractButton* projButton =
                    projBar ? qobject_cast<QAbstractButton*>(projBar->projectionButton())
                            : nullptr;
                check(projButton != nullptr &&
                          projButton->text() == AppBar::projectionLabel(false),
                      QStringLiteral("so the bar's readout still says Persp (\"%1\")")
                          .arg(projButton ? projButton->text() : QStringLiteral("<none>")));
            }

            // ...and the first orbit hands it back. A right-button drag is the
            // real route - the widget's own mouseMoveEvent, not a poke at
            // CameraController - because the loan is cleared inside orbit()
            // and pushed to the OCCT camera by applyCameraState(), and only
            // the drag exercises both.
            dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                       QPointF(view->width() * 0.5 + 40.0, view->height() * 0.5),
                       Qt::RightButton);
            settle(120);
            check(!view->viewIsOrthographic(),
                  "and the first orbit puts perspective back");

            // Pan and zoom must NOT: panning across a face-on drawing is
            // ordinary drafting. Back to Top first, since the orbit above
            // spent the loan.
            clickAt(gizmo, gizmo->tipCenter(2, true));
            settle(150);
            check(view->viewIsOrthographic(), "a second arm click borrows it again");
            dragButton(view, QPointF(view->width() * 0.5, view->height() * 0.5),
                       QPointF(view->width() * 0.5 + 40.0, view->height() * 0.5),
                       Qt::MiddleButton);
            settle(120);
            check(view->viewIsOrthographic(), "and a pan keeps it");
            {
                QWheelEvent wheel(QPointF(view->width() * 0.5, view->height() * 0.5),
                                  view->mapToGlobal(QPointF(view->width() * 0.5,
                                                            view->height() * 0.5)),
                                  QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(view, &wheel);
                settle(120);
            }
            check(view->viewIsOrthographic(), "and so does a wheel notch");

            // --- the toggle always changes what you see ----------------------
            // A loan is live right now, which is the state that used to make
            // the Persp/Ortho button do visibly nothing: with the loan kept,
            // the first click moved the base to Ortho (no visual change, since
            // ortho was already on screen) and the SECOND click moved it back
            // to Persp while the loan held effectiveOrtho() true - so the user
            // clicked twice, watched the label change twice, and saw the
            // viewport change never.
            //
            // The toggle drops the loan now. Driven through the real action,
            // and read off the LIVE OCCT camera, not our own flag.
            {
                QAction* orthoNow = action(window, QStringLiteral("Orthographic"));
                check(orthoNow != nullptr && !orthoNow->isChecked() &&
                          view->camera().temporaryOrtho(),
                      "the loan is live and the chosen mode is still perspective");
                if (orthoNow) {
                    orthoNow->trigger();
                    settle(200);
                    check(orthoNow->isChecked() && !view->camera().temporaryOrtho(),
                          "one click adopts Ortho as the base AND hands the loan back");
                    check(view->viewIsOrthographic(),
                          "the view is orthographic because that is now the mode, "
                          "not because anything is borrowed");

                    orthoNow->trigger();
                    settle(200);
                    check(!view->viewIsOrthographic(),
                          "and the next click actually returns to perspective - with the "
                          "loan kept, this is the one that used to do nothing");
                    check(!orthoNow->isChecked() && !view->camera().temporaryOrtho(),
                          "leaving neither a chosen ortho nor a borrowed one");
                }
            }

            // Clicking the -Y ball views from behind.
            clickAt(gizmo, gizmo->tipCenter(1, false));
            settle(150);
            check(std::fabs(std::fabs(view->camera().state().azimuthDeg) - 180.0) < 1e-3 &&
                  std::fabs(view->camera().state().elevationDeg) < 1e-3,
                  "clicking the -Y ball goes to Back");

            // The label chip is gone, not merely hidden: the widget shrank to
            // its axes, and a click on the strip the chip used to occupy is
            // an ordinary miss now rather than a camera move. Height alone
            // would pass against a chip still painted over the axes, and the
            // click alone would pass against a widget that just grew a dead
            // 30px strip - so both.
            check(gizmo->height() <= 120,
                  QStringLiteral("the gizmo shrank to its axes (%1px tall)")
                      .arg(gizmo->height()));
            const double azBefore = view->camera().state().azimuthDeg;
            const double elBefore = view->camera().state().elevationDeg;
            clickAt(gizmo, QPointF(gizmo->width() / 2.0, gizmo->height() - 1.0));
            settle(150);
            check(std::fabs(view->camera().state().azimuthDeg - azBefore) < 1e-9 &&
                      std::fabs(view->camera().state().elevationDeg - elBefore) < 1e-9,
                  "the gizmo no longer carries a label chip to click");

            // The gizmo wears the floating-surface family's card now, in
            // place of the flat Theme::viewport() fill that read as a lighter
            // box against the real gradient. It had no check of its own while
            // it was that flat fill, which is how it could change ground
            // without anything noticing - so it joins the same sweep every
            // other card in the family carries.
            checkFamilySurface(gizmo, QPoint(0, gizmo->height() / 2),
                               gizmo->rect().adjusted(2, 2, -2, -2), Theme::border(),
                               QStringLiteral("AxisGizmo"));

            // ...and specifically its CORNERS, now that the gizmo carries the
            // family's full radius 8 rather than the radius-0 stopgap. A
            // rounded card's fill does not reach the small triangle outside
            // the rounded shape and inside the widget rect at each corner;
            // Theme::paintSurface() now covers that triangle by filling the
            // widget's FULL rect with an opaque ground - viewport() by
            // default, the ground this card genuinely sits on - before the
            // rounded panel goes on top (this task's fix, see Theme.h). So
            // the right answer at each corner is opaque viewport(), not
            // merely "opaque": the old radius-0 stopgap was opaque too, by
            // covering the corners with panel() fill instead of leaving them
            // to the driver's black, and that would still pass an alpha-only
            // check. Each corner is compared against BOTH wrong answers -
            // it must read closer to viewport() than to panel().
            const QImage gizmoImg = renderExact(gizmo);
            QStringList gizmoNubs;
            const QPoint gizmoCorners[4] = {
                QPoint(0, 0), QPoint(gizmoImg.width() - 1, 0),
                QPoint(0, gizmoImg.height() - 1),
                QPoint(gizmoImg.width() - 1, gizmoImg.height() - 1)};
            for (const QPoint& c : gizmoCorners) {
                if (!gizmoImg.rect().contains(c)) continue;
                const QRgb px = gizmoImg.pixel(c);
                const QColor colour(px);
                const bool opaque = qAlpha(px) == 255;
                const bool readsAsViewport =
                    colorDistance(colour, Theme::viewport()) <
                        colorDistance(colour, Theme::panel()) &&
                    colorDistance(colour, Theme::viewport()) < 20.0;
                if (!opaque || !readsAsViewport)
                    gizmoNubs << QStringLiteral("%1,%2 alpha %3 colour %4")
                                     .arg(c.x()).arg(c.y()).arg(qAlpha(px)).arg(colour.name());
            }
            check(!gizmoImg.isNull() && gizmoNubs.isEmpty(),
                  QStringLiteral("and its corners read as opaque viewport() ground at "
                                 "radius 8 - not black, not panel() (%1)")
                      .arg(gizmoNubs.isEmpty() ? QStringLiteral("all four solid viewport()")
                                               : gizmoNubs.join(QStringLiteral("; "))));
        }

        // Restore the exact startup pose AND the projection, for the same
        // reason the block above this one does: every later check clicks at
        // fractions tuned for the startup camera, and this block has orbited,
        // panned, zoomed and borrowed an orthographic look. The bar's view
        // button used to put the pose back as a side effect of being clicked;
        // it is the projection toggle now and snaps nothing, so the restore
        // is explicit.
        view->camera().setTemporaryOrtho(false);
        view->camera().setState(CameraState{});
        trigger(window, QStringLiteral("Axonometric"));
        settle(200);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3 &&
                  !view->viewIsOrthographic(),
              "camera and projection restored to the startup state after the gizmo block");
    }

    // --- the app bar owns the menu strip --------------------------------------
    // QMainWindow::setMenuWidget puts an arbitrary widget where the menu strip
    // was, with the window's real QMenuBar living inside it. Two traps make
    // this worth asserting by pointer identity rather than by class name.
    //
    // First, QMainWindow::menuBar() is qobject_cast<QMenuBar*>(the menu-widget
    // slot) - which now holds an AppBar, so the cast fails and menuBar()
    // CREATES a new, empty menu bar, whose setMenuBar() then deleteLater()s
    // the app bar. Nothing in this block calls window.menuBar(); it asks
    // window.menuWidget() instead, and counts the menu bars to prove no second
    // one appeared.
    //
    // Second, ShortcutSheet enumerates every binding by walking
    // parentWidget()->findChild<QMenuBar*>() from the window. Reparenting the
    // bar into the app bar must leave that walk finding the same object; the
    // sheet's own block later asserts the row count against the same
    // enumeration, and this one asserts the group titles it can only produce
    // by reaching the menus at all.
    {
        AppBar* bar = qobject_cast<AppBar*>(window.menuWidget());
        check(bar != nullptr, "the window's menu strip is the app bar");

        const QList<QMenuBar*> bars = window.findChildren<QMenuBar*>();
        check(bars.size() == 1,
              QStringLiteral("the window holds exactly one menu bar, not a second "
                             "created behind the app bar (found %1)")
                  .arg(bars.size()));
        if (bar && bars.size() == 1) {
            check(bars.first() == bar->menus(),
                  "and it is the app bar's own menu bar, by pointer identity");
            check(bar->menus()->parentWidget() == bar,
                  "the menu bar is reparented INTO the bar, not left beside it");
            check(bar->menus()->isVisible(), "and it is visible there");

            QStringList titles;
            for (QAction* top : bar->menus()->actions())
                titles << top->text().remove(QLatin1Char('&'));
            check(titles.contains(QStringLiteral("File")) &&
                      titles.contains(QStringLiteral("View")) &&
                      titles.contains(QStringLiteral("Help")),
                  QStringLiteral("the menus survived the move (%1)")
                      .arg(titles.join(QStringLiteral(", "))));
        }

        ShortcutSheet* sheet = window.findChild<ShortcutSheet*>();
        check(sheet != nullptr, "the window has a shortcut sheet to enumerate with");
        if (sheet) {
            const QStringList painted = sheet->paintedTexts();
            check(painted.contains(QStringLiteral("Sketch")) &&
                      painted.contains(QStringLiteral("View")),
                  "the shortcut sheet still finds the menu bar inside the app bar");
            check(!painted.contains(QStringLiteral("Other")),
                  "and no binding fell out of its menu group in the move");
        }

        // The seat the view-direction readout used to hold is the Persp/Ortho
        // toggle now. It no longer snaps to the axonometric pose (that lives
        // on the gizmo, keys 0-3 and the View menu) and it deliberately does
        // NOT record view.changed: a projection flip is not a look in a named
        // direction, and the hint that teaches the gizmo retires on that
        // event. Both halves of that are asserted here, because a control
        // that quietly kept either behaviour would still show the right word.
        QAbstractButton* projButton =
            bar ? qobject_cast<QAbstractButton*>(bar->projectionButton()) : nullptr;
        check(projButton != nullptr, "the bar carries a projection toggle");
        if (bar && projButton) {
            check(projButton->text() == AppBar::projectionLabel(false) &&
                      !view->viewIsOrthographic(),
                  QStringLiteral("it starts on the perspective the app ships with "
                                 "(\"%1\")")
                      .arg(projButton->text()));

            // A real hit test, not an event aimed at the widget we hope is
            // reachable: childAt() is the mechanism a user's click goes
            // through, and it is what caught an unreachable control once
            // already (see the walkthrough's skip control).
            check(bar->childAt(projButton->geometry().center()) == projButton,
                  "childAt() at the toggle's centre finds the button itself");

            const int before = window.progress().count("view.changed");
            const double azBefore = view->camera().state().azimuthDeg;
            const double elBefore = view->camera().state().elevationDeg;
            // The scale invariant, sampled in perspective and compared in
            // ortho below. Pinned on this side too, so the ortho comparison
            // below is against a figure already known to be right rather than
            // against whatever perspective happened to be doing.
            const double wppBefore = view->worldPerPixel();
            {
                const double shown = view->cameraViewHeightAtTarget();
                const double wanted = wppBefore * std::max(1, view->height());
                check(wanted > 1.0e-6 &&
                          std::fabs(shown - wanted) / wanted < 1.0e-6,
                      QStringLiteral("the perspective camera shows the height "
                                     "worldPerPixel() assumes (%1 mm against %2 mm)")
                          .arg(shown, 0, 'f', 4)
                          .arg(wanted, 0, 'f', 4));
            }

            clickAt(projButton, QPointF(projButton->width() / 2.0,
                                        projButton->height() / 2.0));
            settle(300);
            // The LIVE OCCT camera, not our own flag: the write site in
            // applyCameraState() is the thing under test, and asking
            // CameraController whether it had told OCCT something would be
            // its own oracle.
            check(view->viewIsOrthographic(),
                  "clicking it flips the camera's actual projection to orthographic");
            check(view->camera().baseProjection() ==
                      CameraController::Projection::Orthographic,
                  "and it is the BASE mode that moved, not a borrowed look");
            check(projButton->text() == AppBar::projectionLabel(true),
                  QStringLiteral("the button reads Ortho (\"%1\")")
                      .arg(projButton->text()));
            check(std::fabs(view->camera().state().azimuthDeg - azBefore) < 1e-9 &&
                      std::fabs(view->camera().state().elevationDeg - elBefore) < 1e-9,
                  "and the camera did not move - the toggle snaps to no pose");
            check(window.progress().count("view.changed") == before,
                  QStringLiteral("...and records no view.changed, so it cannot retire "
                                 "the hint that teaches the gizmo (%1 then %2)")
                      .arg(before)
                      .arg(window.progress().count("view.changed")));

            // The menu entry and the button are one action, so the menu must
            // already show it checked - a second source of truth would show
            // here first.
            QAction* orthoAction = action(window, QStringLiteral("Orthographic"));
            check(orthoAction != nullptr && orthoAction->isCheckable() &&
                      orthoAction->isChecked(),
                  "the View menu's Orthographic entry is the same state, already checked");

            // --- the parallel-Scale invariant, asserted where it protects ----
            // worldPerPixel() is ONE formula for both projections, and that
            // holds only because applyCameraState() sets the orthographic
            // camera's Scale to exactly the perspective visible height at
            // target depth. Every screen-sized thing in the scene rides on it:
            // the dimension arrowheads and gaps, and both drag arrows' mapping
            // from pixels to millimetres.
            //
            // Nothing else in the suite can catch a mis-scaled ortho camera.
            // The unprojection round trip cannot - it goes out and back
            // through the same matrix, so a camera scaled 2x agrees with
            // itself perfectly.
            //
            // And neither would comparing worldPerPixel() to itself across the
            // flip: it is computed from the turntable's own distance and never
            // reads the OCCT camera, so it is unchanged by arithmetic whatever
            // OCCT was told. The oracle has to be what OCCT was ACTUALLY told,
            // which is cameraViewHeightAtTarget() - the live
            // Graphic3d_Camera::ViewDimensions(). Drop the SetScale line, or
            // move it above the SetProjectionType that must precede it, and
            // the parallel camera sits at its 1000 default and this fails.
            const double wppAfter = view->worldPerPixel();
            const double shownAfter = view->cameraViewHeightAtTarget();
            const double wantedAfter = wppAfter * std::max(1, view->height());
            check(std::fabs(wppAfter - wppBefore) < 1.0e-9,
                  QStringLiteral("world-per-pixel is unchanged by the flip (%1 then %2 "
                                 "mm/px)")
                      .arg(wppBefore, 0, 'g', 12)
                      .arg(wppAfter, 0, 'g', 12));
            check(wantedAfter > 1.0e-6 &&
                      std::fabs(shownAfter - wantedAfter) / wantedAfter < 1.0e-6,
                  QStringLiteral("and the orthographic camera really shows that height - "
                                 "its parallel scale is tied to the perspective framing, "
                                 "not left at a default (shows %1 mm, wants %2 mm)")
                      .arg(shownAfter, 0, 'f', 4)
                      .arg(wantedAfter, 0, 'f', 4));

            // Half of a side-by-side: the SAME camera pose, drawn both ways,
            // which is the only comparison that shows what the mode does. The
            // check above has just pinned that the toggle moved nothing but
            // the projection, so the two frames differ in exactly one thing -
            // and what the pair shows is the ground grid, which converges
            // toward a horizon under perspective and is perfectly uniform
            // under a parallel projection.
            view->saveSnapshot(outDir + "/j-projection-ortho.png");

            clickAt(projButton, QPointF(projButton->width() / 2.0,
                                        projButton->height() / 2.0));
            settle(300);
            view->saveSnapshot(outDir + "/j-projection-persp.png");
            check(!view->viewIsOrthographic() &&
                      projButton->text() == AppBar::projectionLabel(false),
                  "clicking again goes back to perspective");
            check(orthoAction != nullptr && !orthoAction->isChecked(),
                  "and the menu entry follows it back");

            // --- the unprojection audit, proved rather than reasoned about ---
            // Every pixel-to-world route in this app runs through
            // OcctViewWidget::rayThroughPixel(), which asks
            // V3d_View::ConvertWithProj for a ray. Under perspective that ray
            // starts at the eye and fans out; under a parallel projection
            // every ray is the view direction and the origin is the pixel
            // itself. ConvertWithProj is documented to handle both, and this
            // is the check that says so rather than the comment that assumes
            // it - a wrong ray does not crash, it silently puts the point
            // somewhere plausible and wrong.
            //
            // The proof is a ROUND TRIP: click a pixel, take the sketch point
            // the app derived from it, project that point back, and land on
            // the pixel that was clicked. A check that only asserted Z == 0
            // would pass on any point of the ground plane, which is every
            // wrong answer this could produce.
            if (orthoAction) {
                orthoAction->trigger();
                settle(200);
                check(view->viewIsOrthographic(), "in orthographic for the unprojection probe");

                trigger(window, QStringLiteral("Start Sketch"));
                const QPointF probes[3] = {
                    QPointF(view->width() * 0.38, view->height() * 0.38),
                    QPointF(view->width() * 0.61, view->height() * 0.40),
                    QPointF(view->width() * 0.55, view->height() * 0.62)};
                bool onPlane = true;
                double worstPixel = 0.0;
                for (int p = 0; p < 3; ++p) {
                    clickAt(view, probes[p]);
                    settle(40);
                    if (window.sketch().pointCount() != static_cast<size_t>(p + 1)) {
                        onPlane = false;
                        break;
                    }
                    const gp_Pnt placed = window.sketch().points().back();
                    // On the plane it was drawn on - the ground, since nothing
                    // is locked here.
                    onPlane = onPlane && std::fabs(placed.Z()) < 1.0e-6;
                    QPoint back;
                    onPlane = onPlane && view->projectToScreen(placed, back);
                    // Snap to Grid is on by default, so the placed point is
                    // rounded off the exact ray hit before it is stored; at
                    // the startup framing one grid step is a handful of
                    // pixels, which is the tolerance here. A projection that
                    // did not understand parallel rays misses by a fraction of
                    // the viewport, not by a grid step.
                    worstPixel = std::max(
                        worstPixel,
                        std::hypot(back.x() - probes[p].x(), back.y() - probes[p].y()));
                }
                check(onPlane,
                      "every point clicked in orthographic lands exactly on the "
                      "sketch plane");
                check(onPlane && worstPixel < 24.0,
                      QStringLiteral("and projects back to the pixel it was clicked at, "
                                     "so ConvertWithProj's parallel-projection ray is "
                                     "right (worst miss %1 px)")
                          .arg(worstPixel, 0, 'f', 1));

                trigger(window, QStringLiteral("Cancel Sketch"));
                orthoAction->trigger();
                settle(200);
                check(!view->viewIsOrthographic() && !window.isSketching(),
                      "the probe leaves perspective and no sketch behind it");
            }
        }

        QAbstractButton* unitButton =
            bar ? qobject_cast<QAbstractButton*>(bar->unitButton()) : nullptr;
        check(unitButton != nullptr && unitButton->text() == QStringLiteral("mm"),
              "the bar's unit button reads the display unit");
        if (bar && unitButton) {
            check(bar->childAt(unitButton->geometry().center()) == unitButton,
                  "childAt() at the unit button's centre finds it too");
        }

        // Wireframe and Fit All left the viewport for the bar, and mirror
        // their actions rather than storing anything of their own.
        QAbstractButton* wireButton = nullptr;
        QAbstractButton* fitButton = nullptr;
        if (bar) {
            for (QAbstractButton* candidate : bar->findChildren<QAbstractButton*>()) {
                if (candidate->text() == QStringLiteral("Wireframe")) wireButton = candidate;
                if (candidate->text() == QStringLiteral("Fit All")) fitButton = candidate;
            }
        }
        check(wireButton != nullptr && fitButton != nullptr,
              "Wireframe and Fit All are buttons in the bar");
        QAction* wireAction = action(window, QStringLiteral("Wireframe"));
        if (wireButton && wireAction) {
            check(wireButton->isCheckable() && !wireButton->isChecked(),
                  "the Wireframe button is a toggle and starts off");
            wireAction->trigger();
            settle(120);
            check(wireButton->isChecked(),
                  "it mirrors its action when the action is triggered elsewhere");
            wireAction->trigger();
            settle(120);
            check(!wireButton->isChecked(), "and mirrors it back off");

            clickAt(wireButton, QPointF(wireButton->width() / 2.0,
                                        wireButton->height() / 2.0));
            settle(150);
            check(view->isWireframe() && wireButton->isChecked(),
                  "clicking the button drives the action, not a private state");
            clickAt(wireButton, QPointF(wireButton->width() / 2.0,
                                        wireButton->height() / 2.0));
            settle(150);
            check(!view->isWireframe() && !wireButton->isChecked(),
                  "and drives it back, leaving the viewport shaded");
        }
        if (fitButton) {
            check(!fitButton->isCheckable(), "Fit All is not a toggle");
            check(bar && bar->childAt(fitButton->geometry().center()) == fitButton,
                  "childAt() at Fit All's centre finds the button");

            // And it actually does something. Deliberately knock the camera
            // off-centre first, so "the camera moved" cannot be satisfied by
            // a button that does nothing to an already-fitted view. Through
            // animateTo(), which is how every other route moves this camera -
            // poking CameraController directly would leave the OCCT view
            // holding the old pose and make the fit measure the wrong thing.
            CameraState nudged = view->camera().state();
            nudged.target = gp_Pnt(nudged.target.X() + 400.0,
                                   nudged.target.Y() + 260.0, nudged.target.Z());
            nudged.distance *= 0.35;
            view->animateTo(nudged);
            settle(400);
            const CameraState before = view->camera().state();
            clickAt(fitButton, QPointF(fitButton->width() / 2.0,
                                       fitButton->height() / 2.0));
            settle(400);   // the fit animates
            const CameraState after = view->camera().state();
            const double moved =
                gp_Vec(before.target, after.target).Magnitude() +
                std::fabs(before.distance - after.distance);
            check(moved > 1.0e-6,
                  QStringLiteral("clicking Fit All reframes the viewport (camera "
                                 "moved %1)")
                      .arg(moved));
        }

        // Save Screenshot is menu-only from here on: it kept no bar button,
        // and the right-center chip cluster it shared went away with it.
        check(action(window, QStringLiteral("Save Screenshot...")) != nullptr,
              "Save Screenshot is still reachable as an action");

        // ...and the cluster really is GONE, not merely missing a chip. The
        // action existing proves nothing about the cluster, and the bar-button
        // search above runs inside the bar, so a stub that left the old
        // cluster floating over the viewport would satisfy every check above
        // this one.
        //
        // This asserted exactly three while Task 2's interim arrangement was
        // in force (top-left, left-center, bottom-left, with the right-center
        // one gone to the bar). Task 3 folded all three into ONE icon rail
        // pinned to the left edge, so the count is now one - still the exact
        // count rather than "no right-hand cluster", for the same reason:
        // that is what makes this fail loudly the next time the shell's
        // composition changes instead of silently passing against one cluster
        // or five. What that single cluster actually contains is asserted in
        // the rail block that follows.
        const QList<ToolCluster*> clusters = view->findChildren<ToolCluster*>();
        check(clusters.size() == 1,
              QStringLiteral("exactly one chip cluster floats over the viewport - "
                             "the rail, with the other three folded into it "
                             "(found %1)")
                  .arg(clusters.size()));
        QStringList onTheRight;
        for (ToolCluster* cluster : clusters) {
            if (cluster->geometry().center().x() > view->width() / 2)
                onTheRight << QStringLiteral("%1,%2")
                                  .arg(cluster->geometry().center().x())
                                  .arg(cluster->geometry().center().y());
        }
        check(onTheRight.isEmpty(),
              QStringLiteral("and none of them sits on the viewport's right half "
                             "(%1)")
                  .arg(onTheRight.isEmpty() ? QStringLiteral("none")
                                            : onTheRight.join(QStringLiteral("; "))));

        // A hit test where the cluster actually sat, for the same reason every
        // other control here is probed with childAt: a geometry assertion can
        // pass against a widget that is still there and still clickable.
        // Swept across the right margin because the exact inset is the
        // overlay's business, not this check's.
        QWidget* lurking = nullptr;
        for (int inset = 4; inset < 90 && !lurking; inset += 4) {
            QWidget* hit = view->childAt(view->width() - inset, view->height() / 2);
            for (QWidget* w = hit; w; w = w->parentWidget()) {
                if (qobject_cast<ToolCluster*>(w)) { lurking = w; break; }
                if (w == view) break;
            }
        }
        check(lurking == nullptr,
              "and nothing is clickable where the right-center cluster used to be");
    }

    // --- the rail -------------------------------------------------------------
    // One icon-only cluster pinned to the viewport's left edge, carrying every
    // command the four old floating clusters carried. The count check above
    // proves there is exactly one cluster; this block proves it is the right
    // one, in the right order, reachable, and rendering its actions' states.
    {
        const QList<ToolCluster*> found = view->findChildren<ToolCluster*>();
        ToolCluster* rail = found.isEmpty() ? nullptr : found.first();
        check(rail != nullptr && rail->isVisible(), "the rail is up over the viewport");

        if (rail) {
            // Pinned to the LEFT edge and spanning the viewport top to
            // bottom - the two halves of what Anchor::LeftEdge means. The
            // height is the part a plain corner anchor could not produce:
            // it is what puts Undo and Redo at the bottom of the viewport
            // rather than directly under Select Edges.
            check(rail->x() >= 0 && rail->x() <= 20,
                  QStringLiteral("the rail hugs the viewport's left edge (x=%1)")
                      .arg(rail->x()));
            const int bottomGap = view->height() - (rail->y() + rail->height());
            check(rail->y() >= 0 && rail->y() <= 20 && bottomGap >= -1 && bottomGap <= 20,
                  QStringLiteral("and spans it top to bottom (y=%1, %2px of viewport "
                                 "left below it, viewport %3px tall, rail %4px)")
                      .arg(rail->y()).arg(bottomGap)
                      .arg(view->height()).arg(rail->height()));

            // The exact order, by QAction POINTER. Comparing visible text
            // would pass against the right buttons in the wrong order as
            // easily as against the wrong buttons, and would break the next
            // time a label is reworded for reasons that have nothing to do
            // with the rail.
            const QVector<QString> wanted = {
                QStringLiteral("Items"),
                QStringLiteral("Start Sketch"),  QStringLiteral("Extrude..."),
                QStringLiteral("Union"),         QStringLiteral("Subtract"),
                QStringLiteral("Intersect"),     QStringLiteral("Delete Selected"),
                QStringLiteral("Snap to Grid"),  QStringLiteral("Select Bodies"),
                QStringLiteral("Select Faces"),  QStringLiteral("Select Edges"),
                QStringLiteral("Undo"),          QStringLiteral("Redo"),
            };
            const QVector<ToolChip*> chips = rail->chips();
            check(chips.size() == wanted.size(),
                  QStringLiteral("the rail carries %1 buttons (found %2)")
                      .arg(wanted.size()).arg(chips.size()));

            QStringList wrong;
            for (int i = 0; i < wanted.size() && i < chips.size(); ++i) {
                QAction* expected = action(window, wanted[i]);
                if (!expected || chips[i]->action() != expected) {
                    wrong << QStringLiteral("%1: wanted %2, got '%3'")
                                 .arg(i).arg(wanted[i])
                                 .arg(chips[i]->action()
                                          ? chips[i]->action()->text().remove(QLatin1Char('&'))
                                          : QStringLiteral("nothing"));
                }
            }
            check(wrong.isEmpty(),
                  QStringLiteral("and they are the window's own actions in the "
                                 "designed order (%1)")
                      .arg(wrong.isEmpty() ? QStringLiteral("all thirteen match")
                                           : wrong.join(QStringLiteral("; "))));

            // No labelled chip floats over the viewport any more - the old
            // clusters are gone in substance, not merely reparented. A rail
            // that had quietly kept one labelled button would satisfy the
            // cluster count and the order check above and still be wrong.
            QStringList labelled;
            for (ToolChip* chip : view->findChildren<ToolChip*>()) {
                if (chip->mode() != ToolChip::ChipMode::IconOnly)
                    labelled << chip->text();
            }
            check(labelled.isEmpty(),
                  QStringLiteral("every chip over the viewport is icon-only (%1)")
                      .arg(labelled.isEmpty() ? QStringLiteral("all are")
                                              : labelled.join(QStringLiteral(", "))));

            QStringList mis_sized;
            const int side = 34 + Theme::surfaceShadowMargin() * 2;
            for (ToolChip* chip : chips) {
                if (chip->width() != side || chip->height() != side)
                    mis_sized << QStringLiteral("%1 %2x%3")
                                     .arg(chip->text()).arg(chip->width()).arg(chip->height());
            }
            check(mis_sized.isEmpty(),
                  QStringLiteral("each is a %1x%1 square of painted card (%2)")
                      .arg(side)
                      .arg(mis_sized.isEmpty() ? QStringLiteral("all are")
                                               : mis_sized.join(QStringLiteral(", "))));

            // --- the painted gap, MEASURED --------------------------------
            // Task 1's source claimed a 3px painted gap between chip bodies
            // and a magnified crop confirming it. Neither was true:
            // QLayout::spacing() silently ignored the negative value it was
            // given and ran at the style's 6px, so the gap was 12px for two
            // whole tasks and no check could tell. This counts the actual
            // rows of card background between two adjacent buttons' painted
            // borders in a rendered image of the rail - so the number in the
            // source and the number on screen cannot diverge again.
            //
            // Deliberately two chips inside one GROUP (Union and Subtract),
            // not a pair with a separator between them: the separator's own
            // band is a different measurement.
            if (chips.size() >= 6) {
                ToolChip* first = chips[3];    // Union
                ToolChip* second = chips[4];   // Subtract
                const QImage railImg = renderExact(rail);
                const int column = first->x() + first->width() / 2;
                int background = 0;
                QStringList rowColours;
                for (int y = first->y() + first->height(); y < second->y(); ++y) {
                    const QColor c = railImg.pixelColor(column, y);
                    rowColours << c.name();
                    if (colorDistance(c, Theme::panel()) < 12.0) ++background;
                }
                check(second->y() - (first->y() + first->height()) == 3 && background == 3,
                      QStringLiteral("exactly 3 rows of card background separate two "
                                     "adjacent rail buttons - the painted gap the source "
                                     "claims, counted in pixels (%1 rows, %2 of them "
                                     "background: %3)")
                          .arg(second->y() - (first->y() + first->height()))
                          .arg(background)
                          .arg(rowColours.join(QStringLiteral(" "))));
            }

            // Three group dividers - drawer | sketch | model | select - as
            // separate widgets between the chips, not gaps that merely look
            // like dividers.
            QList<QWidget*> separators;
            for (QWidget* child :
                 rail->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (!qobject_cast<ToolChip*>(child)) separators << child;
            }
            check(separators.size() == 3,
                  QStringLiteral("three separators divide the rail's four groups "
                                 "(found %1)").arg(separators.size()));
            if (!separators.isEmpty()) {
                // The rule is really painted, in border(), on the middle row.
                // "A separator widget exists" would pass against one that
                // draws nothing at all.
                QWidget* sep = separators.first();
                const QImage img = renderExact(sep);
                const QColor rule = img.pixelColor(sep->width() / 2, sep->height() / 2);
                const QColor air = img.pixelColor(sep->width() / 2, 0);
                check(colorDistance(rule, Theme::border()) < colorDistance(air, Theme::border()),
                      QStringLiteral("and each paints a border() rule on its middle row, "
                                     "not just empty space (rule %1, air above it %2)")
                          .arg(rule.name()).arg(air.name()));
            }

            // The rail paints the floating-surface family's own card behind
            // its buttons. This is not decoration: over OCCT's on-screen GL
            // surface an unpainted region of a child widget shows whatever
            // the driver left there, and the rail - stretched to the whole
            // viewport height with slack between the select group and
            // history - is mostly such region. Sampled in the middle of that
            // slack, where nothing but the card is painted.
            if (chips.size() >= 3) {
                ToolChip* selectEdges = chips[chips.size() - 3];
                ToolChip* undo = chips[chips.size() - 2];
                const int slackY = (selectEdges->y() + selectEdges->height() + undo->y()) / 2;
                const QImage railImg = renderExact(rail);
                const QColor back = railImg.pixelColor(rail->width() / 2, slackY);
                check(colorDistance(back, Theme::panel()) < 12.0,
                      QStringLiteral("the rail paints panel() behind its buttons rather "
                                     "than leaving the GL surface showing through "
                                     "(sampled %1 at y=%2)")
                          .arg(back.name()).arg(slackY));

                // ...and the card's own 1px border() lands on ONE column,
                // not smeared at half intensity across two - the same
                // antialiasing trap the app bar's bottom rule fell into.
                // Compared against the card's fill, since an absolute match
                // would be hostage to a single antialiased pixel.
                const QColor edge = railImg.pixelColor(0, slackY);
                check(colorDistance(edge, Theme::border()) < 10.0,
                      QStringLiteral("and its outermost column is a crisp border() "
                                     "(sampled %1, wanted %2)")
                          .arg(edge.name()).arg(Theme::border().name()));

                // ...and the WHOLE perimeter, not the one pixel above. The
                // rail is the card whose unpainted slack produced the black
                // band down the viewport in the first place, and it was the
                // only member of the family still checked by a single probe -
                // a shadow, or a gap, reintroduced on the top, bottom or
                // right edge alone would have sailed through. The interior
                // sample is deliberately the slack region between Select
                // Edges and Undo: the one part of this card that is nothing
                // but card.
                const QRect slack(4, selectEdges->y() + selectEdges->height() + 4,
                                  rail->width() - 8,
                                  std::max(4, undo->y() - 4 -
                                                  (selectEdges->y() + selectEdges->height() + 4)));
                checkFamilySurface(rail, QPoint(0, slackY), slack, Theme::border(),
                                   QStringLiteral("ToolCluster (the rail)"));
            }

            // Undo and Redo are pushed to the BOTTOM by the rail's stretch,
            // not left sitting under Select Edges. Asserted against the
            // rail's own height so it holds at any viewport size.
            if (chips.size() >= 2) {
                ToolChip* redo = chips.last();
                const int fromBottom = rail->height() - (redo->y() + redo->height());
                check(fromBottom >= 0 && fromBottom <= 14,
                      QStringLiteral("the stretch puts Redo at the rail's bottom, one "
                                     "card padding up (%1px below it, rail %2px tall)")
                          .arg(fromBottom).arg(rail->height()));
                ToolChip* selectEdges = chips[chips.size() - 3];
                check(redo->y() - selectEdges->y() > 100,
                      QStringLiteral("with real slack between the select group and "
                                     "history (%1px)").arg(redo->y() - selectEdges->y()));
            }

            // Every button is a real hit-test target, compared against the
            // actual chip pointer - CLAUDE.md's rule, and the one that has
            // caught a control no user could click while every attribute
            // assertion passed.
            QStringList unreachable;
            for (ToolChip* chip : chips) {
                const QPoint centre =
                    chip->mapTo(view, QPoint(chip->width() / 2, chip->height() / 2));
                QWidget* hit = view->childAt(centre);
                if (hit != chip)
                    unreachable << QStringLiteral("%1 -> %2")
                                       .arg(chip->text())
                                       .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                                                : QStringLiteral("nothing"));
            }
            check(unreachable.isEmpty(),
                  QStringLiteral("a real click at each button's centre finds that "
                                 "button (%1)")
                      .arg(unreachable.isEmpty() ? QStringLiteral("all thirteen")
                                                 : unreachable.join(QStringLiteral("; "))));

            // ...and a real click on one drives its action, which is the
            // whole point of an action-driven shell. Select Faces is the
            // safe probe: it is a checkable mode with a sibling to switch
            // back to, and the mode block later re-exercises both.
            QAction* facesAction = action(window, QStringLiteral("Select Faces"));
            QAction* bodiesAction = action(window, QStringLiteral("Select Bodies"));
            ToolChip* facesButton = nullptr;
            for (ToolChip* chip : chips) {
                if (chip->action() == facesAction) facesButton = chip;
            }
            if (facesButton && facesAction && bodiesAction) {
                check(!facesAction->isChecked(),
                      "Select Faces is off before the rail button is clicked");
                clickAt(facesButton, QPointF(facesButton->width() / 2.0,
                                             facesButton->height() / 2.0));
                settle(120);
                check(facesAction->isChecked() && facesButton->isChecked(),
                      "clicking the rail's Select Faces button checks the action, and "
                      "the button follows the action rather than itself");
                bodiesAction->trigger();
                settle(120);
                check(bodiesAction->isChecked() && !facesButton->isChecked(),
                      "and switching back through the action alone un-checks the "
                      "button, which stores no state of its own");
            }

            // The label and the shortcut did not vanish with the text - they
            // moved into the tooltip, which is the only thing naming an
            // icon-only button. Items is the honest probe: its action's own
            // tooltip does NOT contain the word "Items", so a chip that just
            // forwarded the action's tooltip unchanged fails this.
            ToolChip* items = chips.isEmpty() ? nullptr : chips.first();
            check(items != nullptr &&
                      items->toolTip().contains(QStringLiteral("Items")) &&
                      items->toolTip().contains(QStringLiteral("Ctrl+Alt+S")),
                  QStringLiteral("an icon-only button's tooltip carries its label and "
                                 "its shortcut (\"%1\")")
                      .arg(items ? items->toolTip().replace(QLatin1Char('\n'),
                                                            QStringLiteral(" / "))
                                 : QString()));

            // ...and it is RECOMPOSED whenever the action changes, not
            // captured once at construction. Items' tooltip never varies, so
            // the check above passes against a capture-once implementation.
            // Snap to Grid's does: updateActions() rebuilds it from
            // snapTooltipText(), which formats the grid step through Measure
            // and therefore reads differently in every display unit. Driven
            // through the real Units actions - the same path the app bar's
            // unit chip uses - and put back, so the units blocks further down
            // still start from millimetres.
            ToolChip* snapButton = nullptr;
            for (ToolChip* chip : chips) {
                if (chip->action() == action(window, QStringLiteral("Snap to Grid")))
                    snapButton = chip;
            }
            QAction* toCentimetres = action(window, QStringLiteral("Centimetres"));
            QAction* toMillimetres = action(window, QStringLiteral("Millimetres"));
            if (snapButton && toCentimetres && toMillimetres) {
                const QString before = snapButton->toolTip();
                check(before.contains(QStringLiteral("Snap to Grid")) &&
                          before.contains(QStringLiteral("10 mm")),
                      QStringLiteral("the Snap button's tooltip names its command and "
                                     "the grid step in millimetres (\"%1\")")
                          .arg(QString(before).replace(QLatin1Char('\n'),
                                                       QStringLiteral(" / "))));
                toCentimetres->trigger();
                settle(150);
                const QString after = snapButton->toolTip();
                check(after != before && after.contains(QStringLiteral("1 cm")) &&
                          after.contains(QStringLiteral("Snap to Grid")),
                      QStringLiteral("and it follows the action when the unit changes "
                                     "rather than being captured once (\"%1\")")
                          .arg(QString(after).replace(QLatin1Char('\n'),
                                                      QStringLiteral(" / "))));
                toMillimetres->trigger();
                settle(150);
                check(snapButton->toolTip() == before,
                      "and back again when the unit is put back");
            }

            // --- state rendering, at the glyph -----------------------------
            // Disabled: the SAME chip, rendered either way, sampled over the
            // 16x16 glyph box alone. A whole-image compare would pass on any
            // repaint at all, and comparing two DIFFERENT rail buttons would
            // compare two glyphs with different ink coverage rather than one
            // glyph in two states.
            if (items && items->action()) {
                const int m = Theme::surfaceShadowMargin();
                const QRect body = items->rect().adjusted(m, m, -m, -m);
                const QRect glyph(body.center().x() - 7, body.center().y() - 7, 15, 15);

                QAction* itemsAction = items->action();
                const bool wasEnabled = itemsAction->isEnabled();
                const QImage enabledImg = renderExact(items);
                itemsAction->setEnabled(false);
                settle(50);
                const QImage disabledImg = renderExact(items);
                const double enabledLum = averageLuminance(enabledImg, glyph);
                const double disabledLum = averageLuminance(disabledImg, glyph);
                check(disabledLum < enabledLum - 1.0,
                      QStringLiteral("a disabled rail button's glyph itself dims "
                                     "(enabled avg %1, disabled avg %2)")
                          .arg(enabledLum, 0, 'f', 1).arg(disabledLum, 0, 'f', 1));
                itemsAction->setEnabled(wasEnabled);
                settle(50);
            }

            // Checked: the inset accent() ring, sampled 2px in from the left
            // edge of the body - clear of the glyph box - on Select Bodies
            // (checked at startup) against Select Faces (not). Two chips of
            // identical size and shape whose only difference at that pixel
            // is the ring, which is what makes the relative comparison fair.
            ToolChip* bodiesChip = nullptr;
            ToolChip* facesChip = nullptr;
            for (ToolChip* chip : chips) {
                if (chip->action() == action(window, QStringLiteral("Select Bodies")))
                    bodiesChip = chip;
                if (chip->action() == action(window, QStringLiteral("Select Faces")))
                    facesChip = chip;
            }
            check(bodiesChip != nullptr && facesChip != nullptr &&
                      bodiesChip->isChecked() && !facesChip->isChecked(),
                  "the rail's Select Bodies button is checked and Select Faces is not, "
                  "so the ring probe has both states to compare");
            if (bodiesChip && facesChip && bodiesChip->isChecked() && !facesChip->isChecked()) {
                const int m = Theme::surfaceShadowMargin();
                const QPoint inset(m + 2, bodiesChip->height() / 2);
                const QColor checkedInset = renderExact(bodiesChip).pixelColor(inset);
                const QColor uncheckedInset = renderExact(facesChip).pixelColor(inset);
                check(colorDistance(checkedInset, Theme::accent()) <
                          colorDistance(uncheckedInset, Theme::accent()),
                      QStringLiteral("and its ring-inset pixel reads far closer to "
                                     "accent() than the unchecked button's does "
                                     "(%1 vs %2)")
                          .arg(checkedInset.name()).arg(uncheckedInset.name()));
            }
        }
    }

    // --- the viewport enforces a minimum height the rail actually fits in ----
    // Regression: ViewportOverlay::relayout()'s LeftEdge case deliberately
    // keeps the rail at its natural size on a too-short viewport and lets
    // the last button run off the bottom edge - Redo first, then Undo - see
    // that function's own comment. Nothing used to stop the window from
    // actually being shrunk that far: at gui_smoke's own 800x500 probe (see
    // "nothing in the bottom strip lands on top of anything else" further
    // down) the rail measured 50x524 and Redo had 4px left on screen.
    // MainWindow::buildOverlay() now derives the viewport's own minimum
    // height from the rail's sizeHint(), so resizing a window down to ITS
    // minimum - the shortest this probe can ever legally be - must still
    // leave the whole rail, Redo included, inside the viewport and reachable
    // by a real click.
    {
        MainWindow minWin(nullptr, /*persistProgress=*/false);
        minWin.setAttribute(Qt::WA_ShowWithoutActivating);
        minWin.show();
        settle(300);
        minWin.view()->setAnimationsEnabled(false);

        // Straight to the window's own computed minimum, not an iterative
        // shrink toward a guessed target - this probe wants exactly what
        // the layout considers the smallest legal size, whatever that
        // number is today.
        minWin.resize(minWin.minimumSizeHint());
        settle(250);

        OcctViewWidget* mv = minWin.view();
        ToolCluster* minRail = mv ? mv->findChild<ToolCluster*>() : nullptr;
        check(minRail != nullptr, "the minimum-size probe still has a rail");
        if (minRail) {
            const QVector<ToolChip*> minChips = minRail->chips();
            check(!minChips.isEmpty(), "and the rail still carries its buttons");
            if (!minChips.isEmpty()) {
                // Redo is the LAST chip added in MainWindow::buildOverlay() -
                // the one the old clip claimed first - so it is what this
                // probe has to find intact, not merely "some chip or other".
                ToolChip* lastButton = minChips.last();
                check(lastButton->text() == QStringLiteral("Redo"),
                      QStringLiteral("and the rail's last button really is Redo, so this "
                                     "probes the actual regression (got '%1')")
                          .arg(lastButton->text()));

                const QRect lastInViewport(lastButton->mapTo(mv, QPoint(0, 0)),
                                           lastButton->size());
                check(mv->rect().contains(lastInViewport),
                      QStringLiteral("Redo sits fully inside the viewport at the "
                                     "window's own minimum size (viewport %1x%2, Redo "
                                     "%3,%4 %5x%6)")
                          .arg(mv->width()).arg(mv->height())
                          .arg(lastInViewport.x()).arg(lastInViewport.y())
                          .arg(lastInViewport.width()).arg(lastInViewport.height()));

                // childAt(), not a geometry check alone - CLAUDE.md's rule:
                // a control can sit at the right coordinates and still be
                // unreachable by a real click if something else is on top of
                // it, and a geometry-only assertion is exactly the kind of
                // check that stayed green while the old clip shipped.
                const QPoint centre = lastButton->mapTo(
                    mv, QPoint(lastButton->width() / 2, lastButton->height() / 2));
                check(mv->childAt(centre) == lastButton,
                      "and a real click at its centre finds Redo itself, not a "
                      "clipped edge or whatever is behind it");
            }
        }
        minWin.close();
    }

    // --- above-horizon clicks are rejected, not mirrored behind the eye -------
    {
        trigger(window, QStringLiteral("Front"));
        settle(400);   // elevation 0: half the viewport is above the horizon
        trigger(window, QStringLiteral("Start Sketch"));
        const int before = static_cast<int>(window.sketch().pointCount());
        // Top strip of the viewport is sky in the Front view.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.05));
        check(static_cast<int>(window.sketch().pointCount()) == before,
              "a click above the horizon adds no sketch point");
        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Axonometric"));
        settle(300);
    }

    // --- bundled font ---------------------------------------------------------
    check(!Theme::fontFamily().isEmpty(),
          QStringLiteral("the bundled font loaded (family: '%1')").arg(Theme::fontFamily()));
    check(QApplication::font().family() == Theme::fontFamily(),
          "the application font is the bundled family");
    check(Theme::fontFamily().contains(QStringLiteral("DM Sans")),
          QStringLiteral("the bundled family is DM Sans, not a fallback"));
    check(window.document().count() == 0, "document starts empty");
    check(!window.isSketching(), "not sketching at startup");

    // --- sketch -> face -> solid -------------------------------------------
    trigger(window, QStringLiteral("Start Sketch"));
    check(window.isSketching(), "Start Sketch enters sketch mode");

    // --- sketch point markers --------------------------------------------
    // A dot at each placed point, a ring on the first (clicking it back is
    // what closes the outline), and a dot at the live cursor - see
    // OcctViewWidget::setSketchPointMarkers()/setSketchCursorMarker().
    {
        check(view->sketchPointMarkerCount() == 0, "no point markers before any click");
        check(!view->hasSketchStartMarker(), "no start marker before any click");
        check(!view->hasSketchCursorMarker(), "no cursor marker before any hover");

        const double w = view->width();
        const double h = view->height();

        moveTo(view, QPointF(0.35 * w, 0.35 * h));
        check(view->hasSketchCursorMarker(), "the cursor marker appears on the first hover");

        clickAt(view, QPointF(0.35 * w, 0.35 * h));
        check(view->sketchPointMarkerCount() == 1, "one placed-point marker after one click");
        check(view->hasSketchStartMarker(), "the first point gets its own start marker");

        clickAt(view, QPointF(0.62 * w, 0.35 * h));
        clickAt(view, QPointF(0.62 * w, 0.56 * h));
        check(view->sketchPointMarkerCount() == 3, "the marker count tracks placed points");
        check(view->hasSketchStartMarker(), "the start marker survives later points");

        moveTo(view, QPointF(0.35 * w, 0.56 * h));
        check(view->hasSketchCursorMarker(), "the cursor marker follows the live cursor");

        // Mid-sketch, several points down and the cursor live over the
        // fourth corner - what "look at it" asks for.
        view->saveSnapshot(outDir + "/i-sketch-markers.png");

        trigger(window, QStringLiteral("Undo Last Point"));
        check(view->sketchPointMarkerCount() == 2, "undoing a point drops its marker");
        check(view->hasSketchStartMarker(), "the start marker survives an undo above it");

        // Markers are feedback, not geometry: clicking exactly on top of one
        // must place an ordinary sketch point, never select anything - the
        // sketch point count is the observable proof, since selection is
        // already disabled outright while sketching.
        const auto pointsBefore = window.sketch().pointCount();
        clickAt(view, QPointF(0.62 * w, 0.35 * h));   // exactly on the second marker
        check(window.sketch().pointCount() == pointsBefore + 1,
              "clicking on a marker's own position still places an ordinary sketch point");

        trigger(window, QStringLiteral("Cancel Sketch"));
        check(view->sketchPointMarkerCount() == 0, "Cancel Sketch clears the point markers");
        check(!view->hasSketchStartMarker(), "Cancel Sketch clears the start marker");
        check(!view->hasSketchCursorMarker(), "Cancel Sketch clears the cursor marker");

        trigger(window, QStringLiteral("Start Sketch"));
    }

    sketchQuad(window, 0.35, 0.35, 0.62, 0.56);
    check(window.sketch().pointCount() == 4, "four clicks became four sketch points");
    check(view->sketchPointMarkerCount() == 4, "sketchQuad's four clicks left four markers");

    trigger(window, QStringLiteral("Finish Sketch"));
    check(!window.isSketching(), "Finish Sketch leaves sketch mode");
    check(window.hasPendingFace(), "a face is waiting to be extruded");
    check(view->sketchPointMarkerCount() == 0, "Finish Sketch clears the point markers");
    check(!view->hasSketchStartMarker(), "Finish Sketch clears the start marker");
    check(!view->hasSketchCursorMarker(), "Finish Sketch clears the cursor marker");

    check(window.extrudePendingFace(10.0), "extrude reports success");
    check(window.document().count() == 1, "one solid in the document");

    // --- the app reports dimensions, not volume -------------------------------
    {
        const QString status = window.statusBar()->currentMessage();
        check(status.contains(QStringLiteral("Body 0")),
              QStringLiteral("the status line names the body (\"%1\")").arg(status));
        check(status.contains(QString::fromUtf8("\xC3\x97")),
              "the status line reports dimensions with a multiplication sign");
        check(!status.contains(QStringLiteral("volume")) &&
              !status.contains(QStringLiteral("mm3")),
              "the status line no longer mentions volume");
    }

    // --- items panel ----------------------------------------------------------
    check(window.itemsPanel() != nullptr, "the window has an items panel");
    check(window.itemsPanel()->rowCount() == 1, "panel shows one row for one solid");
    settle(300);

    // --- the items drawer -----------------------------------------------------
    // The panel stopped docking: it is a floating card over the viewport now,
    // beside the rail, toggled by the same Items action and Ctrl+Alt+S it has
    // always had. The first three checks assert the OLD arrangement is gone
    // rather than that the new one exists - a drawer added while the dock
    // stayed behind would satisfy every check after them.
    {
        check(window.findChildren<QDockWidget*>().isEmpty(),
              QStringLiteral("no dock widget is left in the window (found %1)")
                  .arg(window.findChildren<QDockWidget*>().size()));
        check(window.findChildren<QSplitter*>().isEmpty(),
              QStringLiteral("and no splitter either (found %1)")
                  .arg(window.findChildren<QSplitter*>().size()));
        check(window.centralWidget() == view,
              "the central widget is the viewport alone - full bleed");

        ItemsPanel* drawer = window.itemsPanel();
        check(drawer != nullptr && drawer->parentWidget() == view,
              "the drawer is a child of the viewport, not of a dock area");
        check(drawer != nullptr && drawer->isVisible(), "and it is up");

        ToolCluster* rail = view->findChild<ToolCluster*>();
        check(rail != nullptr && drawer != nullptr &&
                  drawer->x() > rail->geometry().right(),
              QStringLiteral("it is anchored BESIDE the rail rather than under it "
                             "(drawer x=%1, rail right=%2)")
                  .arg(drawer ? drawer->x() : -1)
                  .arg(rail ? rail->geometry().right() : -1));
        // Its top edge, derived rather than allowed a corridor. The overlay's
        // own margin is private to ViewportOverlay.cpp, so this reads it off
        // the widget anchored to the OPPOSITE top corner - the axis gizmo,
        // whose TopRight placement uses the very same constant. Two cards on
        // one top edge that disagree about where that edge is would be
        // visible at a glance, and a "0 to 24" corridor would not have said
        // so.
        AxisGizmo* topRight = view->findChild<AxisGizmo*>();
        check(drawer != nullptr && topRight != nullptr && drawer->y() == topRight->y(),
              QStringLiteral("and its top edge is the overlay's own margin, the same one "
                             "the gizmo hangs from across the top edge (drawer y=%1, "
                             "gizmo y=%2)")
                  .arg(drawer ? drawer->y() : -1).arg(topRight ? topRight->y() : -1));
        check(drawer != nullptr && view->rect().contains(drawer->geometry()),
              "and lies entirely inside the viewport");

        // The drawer is one of the rectangles the toast and the balloon step
        // around - which it gets for free from being an overlay entry, and
        // which nothing else in the suite would notice if it stopped being
        // one.
        ViewportOverlay* overlay = view->findChild<ViewportOverlay*>();
        bool anchored = false;
        if (overlay && drawer) {
            for (const QRect& r : overlay->occupiedRects())
                if (r == drawer->geometry()) anchored = true;
        }
        check(anchored, "the drawer is one of the overlay's occupied rectangles");

        // Rows are real hit-test targets. A docked pane never had to satisfy
        // this; a card over OCCT's GL surface does, and CLAUDE.md's rule is
        // that it is checked with childAt() against the actual control
        // pointer rather than by asserting an attribute.
        auto firstEye = [&]() -> QPushButton* {
            return drawer ? drawer->findChild<QPushButton*>() : nullptr;
        };
        QPushButton* eye = firstEye();
        check(eye != nullptr, "the row carries a visibility toggle");
        if (eye && drawer) {
            const QPoint centre =
                eye->mapTo(view, QPoint(eye->width() / 2, eye->height() / 2));
            QWidget* hit = view->childAt(centre);
            check(hit == eye,
                  QStringLiteral("a real click at its centre finds that toggle, not "
                                 "the viewport behind it (found %1)")
                      .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                               : QStringLiteral("nothing")));

            const int id = window.document().solids().front().id;
            check(view->isSolidVisible(id), "the body starts visible");
            clickAt(eye, QPointF(eye->width() / 2.0, eye->height() / 2.0));
            settle(150);
            check(!view->isSolidVisible(id),
                  "and clicking the eye really hides it - the row is wired, not "
                  "merely reachable");
            // refresh() rebuilds every row wholesale, so the button that was
            // just clicked is on its way to deleteLater(); the second click
            // has to find the NEW one.
            QPushButton* again = firstEye();
            if (again) clickAt(again, QPointF(again->width() / 2.0, again->height() / 2.0));
            settle(150);
            check(view->isSolidVisible(id), "clicking it again brings the body back");
        }

        // A rebuild leaves nothing of the previous list on screen. refresh()
        // deleteLater()s the old rows, which keeps them alive, parented and
        // VISIBLE until control reaches the event loop - so the card painted
        // the empty state's "No bodies yet" underneath the first real row,
        // and that stale text was a live hit-test target while it lasted.
        // Found in a magnified render of the drawer, not by any check that
        // existed.
        QStringList stale;
        if (drawer) {
            for (QLabel* label : drawer->findChildren<QLabel*>()) {
                if (label->isVisible() &&
                    label->text().contains(QStringLiteral("No bodies yet")))
                    stale << label->text().left(24);
            }
        }
        check(stale.isEmpty(),
              QStringLiteral("no leftover empty-state text is still showing behind the "
                             "rows (%1)")
                  .arg(stale.isEmpty() ? QStringLiteral("clean")
                                       : stale.join(QStringLiteral(" | "))));

        // The text sweep above catches the empty state and nothing else - a
        // replaced BODY row carries text that legitimately reappears on the
        // new row, so only the widget's own state can distinguish "gone" from
        // "still there". Held by pointer across a rebuild: settle() runs a
        // nested event loop, which does not deliver DeferredDelete, so the
        // old row is still alive here and its visibility is the whole
        // question.
        if (drawer && drawer->rowCount() > 0) {
            QPointer<QWidget> deadRow = drawer->findChild<QWidget*>(QStringLiteral("itemsRow"));
            check(deadRow != nullptr && deadRow->isVisible(),
                  "a live row is visible before the rebuild that replaces it");
            drawer->refresh();
            settle(60);
            check(deadRow.isNull() || !deadRow->isVisible(),
                  QStringLiteral("and the row a rebuild replaced is hidden the instant it "
                                 "is replaced, not merely on its way to deleteLater() "
                                 "(%1)")
                      .arg(deadRow.isNull() ? QStringLiteral("already destroyed")
                                            : QStringLiteral("still alive, hidden")));
            check(drawer->rowCount() == static_cast<int>(window.document().solids().size()),
                  "and the rebuild left exactly one row per body");
        }

        // Content unchanged: the row still carries name and dimensions.
        check(drawer != nullptr &&
                  drawer->rowTextAt(0).contains(QString::fromUtf8("\xC3\x97")) &&
                  drawer->rowTextAt(0).contains(QStringLiteral("mm")),
              QStringLiteral("rowTextAt(0) still carries the body's dimensions "
                             "(\"%1\")")
                  .arg(drawer ? drawer->rowTextAt(0) : QString()));

        // Toggled by the EXISTING action and its existing shortcut - no new
        // action, no new state. Both directions, and the rail's own button
        // follows the action rather than storing anything of its own.
        QAction* items = action(window, QStringLiteral("Items"));
        check(items != nullptr &&
                  items->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+S")),
              "the drawer still answers to Ctrl+Alt+S on the same action");
        ToolChip* itemsChip = nullptr;
        if (rail) {
            for (ToolChip* chip : rail->chips())
                if (chip->action() == items) itemsChip = chip;
        }
        check(itemsChip != nullptr, "the rail carries the Items button");
        if (items && drawer && itemsChip) {
            check(items->isChecked() && drawer->isVisible() && itemsChip->isChecked(),
                  "action, drawer and rail button all start in agreement");
            items->trigger();
            settle(200);
            check(!drawer->isVisible(),
                  "triggering Items closes the drawer");
            check(!items->isChecked() && !itemsChip->isChecked(),
                  "and the rail button follows the action down");
            bool stillAnchored = false;
            if (overlay) {
                for (const QRect& r : overlay->occupiedRects())
                    if (r == drawer->geometry()) stillAnchored = true;
            }
            check(!stillAnchored,
                  "a closed drawer stops being an obstacle the others avoid");

            items->trigger();
            settle(200);
            check(drawer->isVisible(), "triggering it again reopens the drawer");
            check(items->isChecked() && itemsChip->isChecked(),
                  "and the rail button comes back up with it");
        }

        // The family surface, swept the whole way round - the drawer paints
        // its ENTIRE rect opaquely, which is the property that stops an
        // unpainted slack region rendering as a black band over the GL
        // surface (see ToolCluster.cpp for the capture that found it).
        if (drawer && drawer->isVisible()) {
            checkFamilySurface(drawer, QPoint(0, drawer->height() / 2),
                               drawer->rect().adjusted(6, 6, -6, -6), Theme::border(),
                               QStringLiteral("ItemsPanel (the drawer)"));

            // ...and specifically its CORNERS - the card the nubs were worst
            // on: it floats widest of the family, and its rounded corners at
            // radius 10 used to sit squarely on the GL surface with nothing
            // behind them, reading as black. Theme::paintSurface() now fills
            // the drawer's full rect with an opaque ground - viewport() by
            // default, the ground this card genuinely sits on - before the
            // rounded panel goes on top, so the right answer at each corner
            // is opaque viewport(), not merely opaque: a corner filled with
            // panel() instead (the wrong fix - covering the triangle with the
            // card's own fill rather than the ground behind it) would still
            // pass an alpha-only check, so this is compared against both
            // wrong answers, black and panel().
            const QImage drawerImg = renderExact(drawer);
            QStringList drawerNubs;
            const QPoint drawerCorners[4] = {
                QPoint(0, 0), QPoint(drawerImg.width() - 1, 0),
                QPoint(0, drawerImg.height() - 1),
                QPoint(drawerImg.width() - 1, drawerImg.height() - 1)};
            for (const QPoint& c : drawerCorners) {
                if (!drawerImg.rect().contains(c)) continue;
                const QRgb px = drawerImg.pixel(c);
                const QColor colour(px);
                const bool opaque = qAlpha(px) == 255;
                const bool notBlack = colorDistance(colour, QColor(Qt::black)) > 20.0;
                const bool readsAsViewport =
                    colorDistance(colour, Theme::viewport()) <
                        colorDistance(colour, Theme::panel()) &&
                    colorDistance(colour, Theme::viewport()) < 20.0;
                if (!opaque || !notBlack || !readsAsViewport)
                    drawerNubs << QStringLiteral("%1,%2 alpha %3 colour %4")
                                      .arg(c.x()).arg(c.y()).arg(qAlpha(px)).arg(colour.name());
            }
            check(!drawerImg.isNull() && drawerNubs.isEmpty(),
                  QStringLiteral("and the drawer's corners - where the nubs were worst - "
                                 "read as opaque viewport() ground, not black and not "
                                 "panel() (%1)")
                      .arg(drawerNubs.isEmpty() ? QStringLiteral("all four solid viewport()")
                                                : drawerNubs.join(QStringLiteral("; "))));

            // Saved to disk for the same reason the toast is: the empty-state
            // drawer is what a PrintWindow capture of a freshly launched app
            // shows, and a populated one - rows, dimension readouts, eyes -
            // only exists once a body does. QWidget::render(), the same
            // in-process mechanism every check here uses, never OS input.
            renderExact(drawer).save(outDir + QStringLiteral("/drawer_rows.png"));
        }
    }

    const double volumeA = ModelingOps::volume(window.document().solids().front().shape);
    check(volumeA > 0.0, QStringLiteral("solid has positive volume (%1)").arg(volumeA, 0, 'f', 1));
    check(ModelingOps::countSolids(window.document().solids().front().shape) == 1,
          "the extrusion is a single solid");
    view->saveSnapshot(outDir + "/g1-solid.png");

    // --- the Qt/OCCT pixel boundary ------------------------------------------
    // Pinned before the first pick that depends on it. Qt reports mouse
    // positions and widget geometry in LOGICAL pixels; the native window
    // OCCT was handed is sized in DEVICE pixels. They coincide at 100%
    // display scaling and diverge by exactly the scale factor at any other -
    // so every fraction-of-the-widget click in this file missed by that
    // factor on a 150% display, while the projected-geometry clicks kept
    // working because they round-tripped through the same wrong space.
    // The camera's own target is by definition at the centre of the
    // viewport, which makes it the one point whose projection is known
    // without reference to any model.
    {
        QPoint targetAt;
        const bool projected = view->projectToScreen(view->camera().state().target, targetAt);
        check(projected && std::abs(targetAt.x() - view->width() / 2) <= 3 &&
                  std::abs(targetAt.y() - view->height() / 2) <= 3,
              QStringLiteral("projectToScreen answers in Qt's own logical pixels - the "
                             "camera target lands at the viewport centre (%1,%2 vs %3,%4)")
                  .arg(targetAt.x()).arg(targetAt.y())
                  .arg(view->width() / 2).arg(view->height() / 2));

        // The OTHER direction, pinned directly. The check above exercises
        // only the device -> logical half; toDevicePixels() is what carries a
        // click back the other way, and a round trip is the one thing that is
        // false unless BOTH halves agree. A point known to be on the body,
        // projected to a pixel and clicked at that pixel, must select that
        // body.
        GProp_GProps bodyProps;
        BRepGProp::VolumeProperties(window.document().solids().front().shape, bodyProps);
        QPoint bodyAt;
        const int bodyId = window.document().solids().front().id;
        check(view->projectToScreen(bodyProps.CentreOfMass(), bodyAt),
              "a point inside the one body projects to a pixel");
        view->clearSelection();
        clickAt(view, QPointF(bodyAt));
        settle(120);
        check(view->selectedSolidIds().size() == 1 &&
                  view->selectedSolidIds().front() == bodyId,
              QStringLiteral("and clicking that pixel selects that body - the projection "
                             "and the pick agree on one pixel space, in both directions"));
        view->clearSelection();
    }

    // --- picking -------------------------------------------------------------
    clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
    check(view->selectedSolidIds().size() == 1, "clicking the solid selects exactly one");

    // --- per-solid visibility -------------------------------------------------
    {
        const int id = window.document().solids().front().id;
        check(view->isSolidVisible(id), "a new solid starts visible");

        view->setSolidVisible(id, false);
        check(!view->isSolidVisible(id), "hiding reports hidden");
        check(view->selectedSolidIds().empty(), "hiding a solid drops it from the selection");

        view->setSolidVisible(id, true);
        check(view->isSolidVisible(id), "showing reports visible again");
        check(!view->isSolidVisible(9999), "an unknown id is not visible");

        // Showing does not restore the selection - production code must never
        // silently re-select something on the user's behalf. The delete/undo
        // checks below need a selection, so re-establish it the way a user
        // would, with a click.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
        check(view->selectedSolidIds().size() == 1, "the shown solid can be picked again");
    }

    {
        const int id = window.document().solids().front().id;
        view->setWireframe(false);
        view->setSolidVisible(id, false);
        view->setSolidVisible(id, true);
        settle(150);
        check(view->isSolidVisible(id), "a hidden-then-shown solid comes back visible");

        // Showing does not restore the selection (same rule as above); the
        // delete/undo checks below need one, so re-establish it with a click.
        clickAt(view, QPointF(view->width() * 0.5, view->height() * 0.5));
    }

    // --- delete / undo / redo through the real actions -----------------------
    trigger(window, QStringLiteral("Delete Selected"));
    check(window.document().count() == 0, "Delete removes the solid");
    check(window.itemsPanel()->rowCount() == 0, "panel empties when the solid is deleted");

    trigger(window, QStringLiteral("Undo"));
    check(window.document().count() == 1, "Undo brings it back");

    trigger(window, QStringLiteral("Redo"));
    check(window.document().count() == 0, "Redo removes it again");

    trigger(window, QStringLiteral("Undo"));
    check(window.document().count() == 1, "Undo again, back to one solid");
    settle(200);

    // --- outcomes are reported without stopping the user ----------------------
    {
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr, "the window has a toast host");

        // A modal would hang this suite rather than fail it, so assert the
        // absence of one directly: nothing in the app may create a dialog.
        check(window.findChildren<QDialog*>().isEmpty(),
              "no dialog is ever constructed for an outcome");

        if (toasts) {
            const int before = static_cast<int>(window.document().solids().size());
            check(before > 0, "there is a body to delete");
            view->setSelectedSolids({window.document().solids().front().id});
            settle(100);
            trigger(window, QStringLiteral("Delete Selected"));
            settle(150);

            check(toasts->isShowing(), "deleting a body raises a toast");
            check(toasts->toast() != nullptr && toasts->toast()->isVisible(),
                  "the toast is actually visible");
            check(!toasts->currentText().isEmpty(),
                  QStringLiteral("the toast says what happened (\"%1\")")
                      .arg(toasts->currentText()));

            // The Undo control must be reachable by a real click, not merely
            // present: a transparent overlay hides its whole subtree from
            // hit-testing, which is how Phase 2 shipped an unclickable control.
            QWidget* undo = toasts->undoControl();
            check(undo != nullptr && undo->isVisible(), "the toast offers Undo");
            if (undo) {
                const QPoint centre =
                    undo->mapTo(view, QPoint(undo->width() / 2, undo->height() / 2));
                check(view->childAt(centre) == undo,
                      "the Undo control is reachable by a real click");
                clickAt(undo, QPointF(undo->width() / 2.0, undo->height() / 2.0));
                settle(200);
                check(static_cast<int>(window.document().solids().size()) == before,
                      "using the toast's Undo restores the body");
                check(!toasts->isShowing(), "using Undo dismisses the toast");
            }

            // A second message replaces the first; a stack of toasts is a
            // dialog with extra steps.
            toasts->show(QStringLiteral("First"), Toast::Kind::Note, false);
            settle(50);
            toasts->show(QStringLiteral("Second"), Toast::Kind::Note, false);
            settle(50);
            check(window.findChildren<Toast*>().size() == 1,
                  "a second message replaces the first rather than stacking");
            check(toasts->currentText() == QStringLiteral("Second"),
                  "the newest message is the one showing");

            // The 4000/8000 ms split is the contract - a Failure carries a
            // sentence the user must read and act on, which is the whole
            // reason it outlives a Note. Asserted against the armed timer
            // rather than by actually waiting 4-8 real seconds for each one
            // to elapse, which would meaningfully slow this suite for a
            // property that a single read of the timer proves just as well.
            toasts->show(QStringLiteral("Note lifetime check"), Toast::Kind::Note, false);
            const int noteMs = toasts->remainingMs();
            check(noteMs > 3500 && noteMs <= 4000,
                  QStringLiteral("a Note toast is timed for 4000 ms (got %1)").arg(noteMs));

            toasts->show(QStringLiteral("Failure lifetime check"), Toast::Kind::Failure, false);
            const int failureMs = toasts->remainingMs();
            check(failureMs > 7500 && failureMs <= 8000,
                  QStringLiteral("a Failure outlives a Note - timed for 8000 ms (got %1)")
                      .arg(failureMs));

            // The toast's Undo used to be wired straight to onUndo(), so it
            // was a fourth entry point obeying none of the guard the menu
            // entry, the chip and Ctrl+Z all share - delete a body, start a
            // sketch inside the four-second window, click Undo, and the
            // document resynced and the selection cleared while the user was
            // still placing points.
            if (!window.document().solids().empty()) {
                const int bodies = static_cast<int>(window.document().solids().size());
                view->setSelectedSolids({window.document().solids().front().id});
                settle(100);
                trigger(window, QStringLiteral("Delete Selected"));
                settle(150);
                check(toasts->isShowing() && toasts->undoControl() &&
                          toasts->undoControl()->isVisible(),
                      "a delete raises a toast whose Undo is offered");

                trigger(window, QStringLiteral("Start Sketch"));
                settle(150);
                QAction* undoAction = action(window, QStringLiteral("Undo"));
                check(undoAction != nullptr && !undoAction->isEnabled(),
                      "the Undo action is disabled mid-sketch");
                QWidget* pill = toasts->undoControl();
                check(pill != nullptr && !pill->isVisible(),
                      "and the toast's Undo control is unusable while it is");
                const int afterGuard = static_cast<int>(window.document().solids().size());
                if (pill) {
                    // Straight at the control, the most generous thing a user
                    // could manage: even reached, it must do nothing.
                    clickAt(pill, QPointF(pill->width() / 2.0, pill->height() / 2.0));
                    settle(150);
                }
                check(static_cast<int>(window.document().solids().size()) == afterGuard,
                      "clicking the toast's Undo mid-sketch undoes nothing");
                check(window.isSketching(),
                      "and leaves the sketch the user was placing points in alone");
                trigger(window, QStringLiteral("Cancel Sketch"));
                settle(120);
                trigger(window, QStringLiteral("Undo"));
                settle(150);
                check(static_cast<int>(window.document().solids().size()) == bodies,
                      "Undo by hand restores the body the guarded pill would not");

                // A toast that names one operation must not outlive it. Delete
                // a body - the toast says so and offers Undo - then press
                // Ctrl+Z by hand. The toast used to stay up, still armed, and
                // its pill then popped the checkpoint BEFORE the one it named:
                // the label described one change and the control performed
                // another. A fresh delete here, deliberately, so the toast
                // under test is one nothing has already dismissed.
                view->setSelectedSolids({window.document().solids().front().id});
                settle(100);
                trigger(window, QStringLiteral("Delete Selected"));
                settle(150);
                check(toasts->isShowing() && toasts->undoControl() &&
                          toasts->undoControl()->isVisible(),
                      "the fresh delete raises an armed toast");
                trigger(window, QStringLiteral("Undo"));
                settle(200);
                check(static_cast<int>(window.document().solids().size()) == bodies,
                      "the hand Undo restored that body too");
                check(!toasts->isShowing(),
                      "and the toast that offered to undo that same delete is gone, "
                      "rather than left describing one change while armed to "
                      "perform another");
            }
        }
    }

    // --- a second, taller solid ---------------------------------------------
    trigger(window, QStringLiteral("Start Sketch"));
    sketchQuad(window, 0.55, 0.30, 0.82, 0.50);
    trigger(window, QStringLiteral("Finish Sketch"));
    check(window.extrudePendingFace(40.0), "second extrude reports success");
    check(window.document().count() == 2, "two solids in the document");
    check(window.itemsPanel()->rowCount() == 2, "panel tracks the second solid");

    {
        const int firstId = window.document().solids().front().id;
        view->setSelectedSolids({firstId});
        settle(150);
        check(view->selectedSolidIds().size() == 1,
              "setSelectedSolids selects exactly the requested solid");
        check(view->selectedSolidIds().front() == firstId,
              "and it is the one that was asked for");

        // A hidden solid must never become selected behind the user's back.
        view->clearSelection();
        view->setSolidVisible(firstId, false);
        view->setSelectedSolids({firstId});
        settle(150);
        check(view->selectedSolidIds().empty(), "a hidden solid cannot be selected");
        view->setSolidVisible(firstId, true);
    }

    settle(300);

    const double volumeB = ModelingOps::volume(window.document().solids().back().shape);
    view->saveSnapshot(outDir + "/g2-two-solids.png");

    // --- select two and cut ---------------------------------------------------
    // Fractions re-tuned again for the perspective projection this task turns
    // on: switching from orthographic to perspective moves where the two
    // solids land on screen, and the previous 0.35/0.50 point sat right on the
    // first solid's edge (it worked under orthographic framing, not under
    // perspective). 0.45/0.55 lands solidly inside the first solid's silhouette.
    view->clearSelection();
    clickAt(view, QPointF(view->width() * 0.45, view->height() * 0.55));
    check(view->selectedSolidIds().size() == 1, "first solid picked");

    clickAt(view, QPointF(view->width() * 0.68, view->height() * 0.40), Qt::ShiftModifier);
    check(view->selectedSolidIds().size() == 2, "shift-click adds the second solid");

    // --- a hint appears the first time two bodies are selected ----------------
    {
        HintBalloon* hint = window.findChild<HintBalloon*>();
        check(hint != nullptr, "the window has a hint balloon");
        check(hint != nullptr && !hint->currentHint().isEmpty(),
              QStringLiteral("selecting two bodies raises a hint (\"%1\")")
                  .arg(hint ? hint->currentHint() : QString()));
        check(hint != nullptr && hint->currentHint().contains(QStringLiteral("Union")),
              "the hint names the operations now available");
    }

    // --- teach this window every hint, and re-select for the Cut below --------
    // The "a learned hint never appears again" assertion that used to sit
    // here was tautological: `window` had already shown the boolean hint a
    // few lines above, so myShownThisSession alone made isDue() false and the
    // check passed with the threshold logic deleted entirely. The real
    // version needs a window that has never shown the hint, which is the
    // dedicated probe immediately below. What survives here is what the rest
    // of the suite actually needs from this block: the two bodies selected
    // again for the Cut, and a `window` that has learned everything so no
    // stray hint appears over later checks.
    {
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            window.progress().record("boolean.completed");
            window.progress().record("faceMode.used");
            window.progress().record("view.changed");
        }
        view->clearSelection();
        settle(150);
        const int firstId = window.document().solids().front().id;
        const int secondId = window.document().solids().back().id;
        check(firstId != secondId, "the document holds two distinct bodies");
        view->setSelectedSolids({firstId, secondId});
        settle(200);
    }

    // --- a learned hint never appears again ------------------------------------
    {
        // A window that has never shown a hint, so the session flag cannot be
        // what silences one: every governing event is pushed past the
        // threshold BEFORE anything raises a balloon, and then the exact
        // conditions that raise all three are reproduced from scratch. Delete
        // the hasLearned() term from isDue() and this fails; that was not true
        // of the version this replaces.
        MainWindow learned(nullptr, /*persistProgress=*/false);
        learned.setAttribute(Qt::WA_ShowWithoutActivating);
        learned.resize(900, 600);
        learned.show();
        settle(300);
        learned.view()->setAnimationsEnabled(false);

        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            learned.progress().record("boolean.completed");
            learned.progress().record("faceMode.used");
            learned.progress().record("view.changed");
            learned.progress().record("walkthrough.done");
        }

        HintBalloon* learnedHint = learned.findChild<HintBalloon*>();
        check(learnedHint != nullptr, "the learned-user probe has a hint balloon");
        if (learnedHint) {
            check(buildBody(learned, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "first body for the learned-user probe");
            check(buildBody(learned, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "second body for the learned-user probe");
            check(learnedHint->currentHint().isEmpty() && !learnedHint->isVisible(),
                  "a user past every threshold is taught nothing by making bodies");

            const int idA = learned.document().solids().front().id;
            const int idB = learned.document().solids().back().id;
            check(idA != idB, "the learned-user probe holds two distinct bodies");
            learned.view()->setSelectedSolids({idA, idB});
            settle(200);
            check(learnedHint->currentHint().isEmpty() && !learnedHint->isVisible(),
                  "a user who has run three booleans is not told about them again");
        }
        learned.close();
    }

    // --- hint balloon: visibility, real hit-testing, all three dismissal ------
    // triggers, and preemption, pinned down in one tightly controlled scenario.
    //
    // A dedicated window rather than reusing `window` above: by this point in
    // the suite, `window`'s own delete/undo/redo traffic and status-bar
    // updates have already dismissed and consumed each hint's one showing per
    // session in ways that depend on exactly how earlier checks happen to be
    // ordered (deleting the only body, for instance, makes both the
    // face-selection and view hints' conditions go false well before this
    // point runs). Asserting anything precise against that would be asserting
    // an accident of ordering, not the balloon's actual contract - so this
    // scenario is built from scratch, deterministically, to pin the contract
    // down instead.
    {
        MainWindow probe(nullptr, /*persistProgress=*/false);
        probe.setAttribute(Qt::WA_ShowWithoutActivating);
        probe.resize(900, 600);
        probe.show();
        settle(300);
        OcctViewWidget* probeView = probe.view();
        probeView->setAnimationsEnabled(false);

        HintBalloon* hint = probe.findChild<HintBalloon*>();
        check(hint != nullptr, "the probe window has a hint balloon");
        check(hint != nullptr && !hint->isVisible() && hint->currentHint().isEmpty(),
              "no hint is up before any body exists");

        if (hint) {
            check(buildBody(probe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "first body for the hint-balloon probe");

            const QString firstHint = hint->currentHint();
            check(hint->isVisible() && !firstHint.isEmpty() &&
                  !firstHint.contains(QStringLiteral("Union")),
                  QStringLiteral("a lower-priority hint is up with one body (\"%1\")")
                      .arg(firstHint));

            // --- real hit-testing, not a synthetic event sent straight to a
            // widget we merely hope is reachable - see the Task 4 skip-control
            // regression this mirrors: childAt() is the actual mechanism a
            // real click uses, and this checks identity against it directly.
            const QPoint centre = hint->geometry().center();
            QWidget* hitBalloon = probeView->childAt(centre);
            check(hitBalloon == hint,
                  "childAt() at the balloon's centre finds the balloon itself, "
                  "the way a real click would");

            // --- trigger 1: "got it" genuinely dismisses it, not just myText --
            if (hitBalloon == hint) {
                clickAt(hint, QPointF(hint->width() / 2.0, hint->height() / 2.0));
            }
            check(hint->currentHint().isEmpty() && !hint->isVisible(),
                  "\"got it\" dismisses the balloon for real - hidden, not just "
                  "text-empty");

            // --- the session flag holds: building the second body re-derives
            // state with the dismissed hint's own condition still true (a body
            // exists, face selection still untried) - it must not come back.
            check(buildBody(probe, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "second body for the hint-balloon probe");
            const QString secondHint = hint->currentHint();
            check(!secondHint.contains(QStringLiteral("Select Faces")),
                  "the hint dismissed with \"got it\" does not return this session");
            check(hint->isVisible() && !secondHint.isEmpty() &&
                  secondHint.contains(QStringLiteral("gizmo")),
                  QStringLiteral("a second, different lower-priority hint is up "
                                 "instead (\"%1\")").arg(secondHint));

            // --- preemption: the boolean hint displaces one already on screen -
            const int idA = probe.document().solids().front().id;
            const int idB = probe.document().solids().back().id;
            check(idA != idB, "the probe window holds two distinct bodies");
            probeView->setSelectedSolids({idA, idB});
            settle(200);
            const QString thirdHint = hint->currentHint();
            check(thirdHint.contains(QStringLiteral("Union")) && thirdHint != secondHint,
                  "selecting two bodies preempts the hint that was already up");
            check(hint->isVisible(), "the boolean hint is genuinely visible");

            // --- trigger 2: the condition going away clears it, not just a
            // click and not just the learned threshold. Both other hints
            // already had their one showing this session (above), so nothing
            // else is due to take the freed slot - the balloon goes fully
            // quiet, not merely off-topic.
            probeView->setSelectedSolids({idA});
            settle(200);
            check(hint->currentHint().isEmpty(),
                  "dropping the selection to one body clears the boolean hint");
        }
    }

    // --- hint balloon: the two dismissal edges that do not run through -------
    // ordinary appStateChanged traffic. HintBalloon::reconsider() is driven
    // solely by MainWindow::appStateChanged, so a live predicate that flips
    // for a reason nothing already wired to that signal notices would
    // linger regardless of how correct conditionHolds() itself is -
    // MainWindow::onSelectionModeChanged() now calls updateActions()
    // explicitly, and OcctViewWidget::cameraChanged is now routed to
    // HintBalloon::onCameraChanged(). Each gets its own probe because both
    // the face-selection and the view hint can only show once per session,
    // and each hint's one showing in the probes above is already spent
    // proving a different trigger.
    {
        MainWindow modeProbe(nullptr, /*persistProgress=*/false);
        modeProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        modeProbe.resize(900, 600);
        modeProbe.show();
        settle(300);
        OcctViewWidget* modeProbeView = modeProbe.view();
        modeProbeView->setAnimationsEnabled(false);

        HintBalloon* hint = modeProbe.findChild<HintBalloon*>();
        check(hint != nullptr, "the mode-transition probe has a hint balloon");
        if (hint) {
            check(buildBody(modeProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the mode-transition probe");
            check(hint->isVisible() &&
                  hint->currentHint().contains(QStringLiteral("Select Faces")),
                  QStringLiteral("the face-selection hint is up before face mode "
                                 "is tried (\"%1\")").arg(hint->currentHint()));

            // Entering face selection mode - the action the hint is teaching -
            // must clear it on its own, with no click and no unrelated action
            // to fire appStateChanged first.
            trigger(modeProbe, QStringLiteral("Select Faces"));
            // Not necessarily empty: the view hint's own condition (a body
            // exists, no named view tried yet) is already satisfied and it
            // has not had its turn this session, so reconsider()'s cascade
            // correctly raises it the instant the slot is free - the same
            // "another due hint may legitimately take the freed slot"
            // behaviour as the condition-loss probe above. What matters here
            // is that the *face-selection* hint specifically is gone.
            check(!hint->currentHint().contains(QStringLiteral("Select Faces")),
                  QStringLiteral("switching to face selection clears its own "
                                 "hint directly (now: \"%1\")").arg(hint->currentHint()));
            check(hint->isVisible() && hint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint - already due - fills the freed slot immediately");

            // The face-selection hint already had its one showing this
            // session, so a second body changes nothing about which hint is
            // up; it is still the view hint from above.
            check(buildBody(modeProbe, 0.55, 0.30, 0.75, 0.50, 10.0),
                  "a second body for the mode-transition probe");
            check(hint->isVisible() && hint->currentHint().contains(QStringLiteral("gizmo")),
                  QStringLiteral("the view hint is still up before a named view "
                                 "is tried (\"%1\")").arg(hint->currentHint()));

            // Snapping to a named view - the action the hint is teaching -
            // must clear it on its own, driven by cameraChanged rather than
            // by appStateChanged.
            trigger(modeProbe, QStringLiteral("Front"));
            check(hint->currentHint().isEmpty(),
                  "triggering a standard view clears its own hint directly");
        }
    }

    if (view->selectedSolidIds().size() == 2) {
        check(window.applyBooleanToSelection(static_cast<int>(ModelingOps::BooleanKind::Cut)),
              "Cut reports success");
        check(window.document().count() == 1, "the two operands became one result");

        const double cutVolume = ModelingOps::volume(window.document().solids().front().shape);
        check(cutVolume > 0.0 && cutVolume < volumeA,
              QStringLiteral("cut removed material (%1 -> %2)")
                  .arg(volumeA, 0, 'f', 1).arg(cutVolume, 0, 'f', 1));
        settle(300);
        view->saveSnapshot(outDir + "/g3-after-cut.png");
    }

    // --- a length you can see while you make it -------------------------------
    {
        trigger(window, QStringLiteral("Start Sketch"));
        settle(100);
        clickAt(view, QPointF(300, 300));
        moveTo(view, QPointF(420, 300));
        settle(120);

        check(view->dimension().isShowing(),
              "dragging out a segment shows its length");
        view->saveSnapshot(outDir + "/g-dimension-live.png");

        // Computed independently: the renderer must not be its own oracle.
        const gp_Pnt a = window.sketch().points().front();
        gp_Pnt b;
        check(view->lastHoverPoint(b), "the cursor's ground point is known");
        const std::string expected = Measure::formatLength(a.Distance(b));
        check(view->dimension().labelText() == expected,
              QStringLiteral("the label reads the true distance (\"%1\" vs \"%2\")")
                  .arg(QString::fromStdString(view->dimension().labelText()))
                  .arg(QString::fromStdString(expected)));

        // The one copy of the banned list (bannedWords(), below) reaches this
        // painted-not-tooltipped string too, the same way it already reaches
        // the walkthrough panel, the hint balloon and the toast.
        const QString labelText = QString::fromStdString(view->dimension().labelText());
        QStringList labelOffenders;
        for (const QString& word : bannedWords()) {
            if (labelText.contains(word, Qt::CaseInsensitive)) labelOffenders << word;
        }
        check(labelOffenders.isEmpty(),
              QStringLiteral("the dimension label uses no banned word (\"%1\"%2)")
                  .arg(labelText,
                       labelOffenders.isEmpty()
                           ? QString()
                           : QStringLiteral(" [") + labelOffenders.join(QStringLiteral(", ")) +
                                 QStringLiteral("]")));

        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(120);
        check(!view->dimension().isShowing(),
              "cancelling the outline clears the dimension");
    }

    // --- an edge's length, hovered and selected -------------------------------
    // Acceptance criterion 1 asks for both, and edge mode had no coverage at
    // all: the dimension was refreshed only from the hover branch and cleared
    // whenever nothing was detected, so moving the cursor off an edge the user
    // had SELECTED dropped its annotation. The last two checks are the other
    // half of the same rule - an annotation must not outlive the body it
    // measures.
    {
        const CameraState cameraBefore = view->camera().state();
        view->fitAll();
        settle(200);
        trigger(window, QStringLiteral("Select Edges"));
        settle(120);
        check(!view->dimension().isShowing(),
              "entering edge selection annotates nothing on its own");

        // The edge is found by clicking and then READ BACK through
        // selectedEdge(), so the expected length comes from the edge the app
        // actually picked rather than from whichever one this loop hoped it
        // would hit - several bodies exist by now and any of them can occlude
        // any other.
        TopoDS_Edge picked;
        QPoint screen;
        for (const DocumentModel::Solid& solid : window.document().solids()) {
            for (TopExp_Explorer it(solid.shape, TopAbs_EDGE); it.More(); it.Next()) {
                const TopoDS_Edge candidate = TopoDS::Edge(it.Current());
                TopoDS_Vertex v1, v2;
                TopExp::Vertices(candidate, v1, v2);
                if (v1.IsNull() || v2.IsNull()) continue;
                const gp_Pnt a = BRep_Tool::Pnt(v1);
                const gp_Pnt b = BRep_Tool::Pnt(v2);
                if (a.Distance(b) < 1.0) continue;
                const gp_Pnt mid(0.5 * (a.X() + b.X()), 0.5 * (a.Y() + b.Y()),
                                 0.5 * (a.Z() + b.Z()));
                QPoint at;
                if (!view->projectToScreen(mid, at)) continue;
                if (!view->rect().adjusted(40, 40, -40, -40).contains(at)) continue;
                clickAt(view, QPointF(at));
                settle(100);
                const TopoDS_Edge got = view->selectedEdge();
                if (got.IsNull() || !got.IsSame(candidate)) continue;
                picked = got;
                screen = at;
                break;
            }
            if (!picked.IsNull()) break;
        }
        check(!picked.IsNull(), "clicking a projected edge midpoint selects that edge");

        if (!picked.IsNull()) {
            TopoDS_Vertex v1, v2;
            TopExp::Vertices(picked, v1, v2);
            // Computed here, from the edge the app handed back: the renderer
            // must not be its own oracle.
            const std::string expected =
                Measure::formatLength(BRep_Tool::Pnt(v1).Distance(BRep_Tool::Pnt(v2)));

            // Milestone 2 Task 4 changed what SELECTING a straight edge means:
            // it now raises the round/flatten arrow, whose value chip labels
            // that same edge. Two annotations on one edge is noise, so the
            // length stands down for as long as the arrow is up. The Phase 4
            // rules this used to check here - that the annotation follows the
            // SELECTION as well as the hover, survives the cursor leaving, and
            // never outlives the body it measures - are not dropped: they moved
            // to the bevel block, onto a CURVED edge, where no arrow appears
            // and the selection half is still the only thing holding the
            // annotation up. That also covers the arrow's straight-only rule in
            // the same breath.
            check(!view->dimension().isShowing(),
                  "selecting a straight edge stands its length annotation down - the "
                  "gizmo's own value chip is the more specific of the two labels");
            check(view->edgeDimensionSuppressed(),
                  "and it is the window that stood it down, from the same predicate "
                  "that raised the arrow");
            check(view->hasBevelArrow(),
                  "which is up on that edge, so the chip really is there to replace it");

            // Non-vacuity for the three above: the annotation is not simply
            // broken in edge mode. The pick left the cursor's DETECTION on that
            // same edge, so the moment the arrow goes the hover half takes over
            // and the length is back - with no mouse movement at all, which is
            // what setEdgeDimensionSuppressed() re-deriving rather than merely
            // clearing is for.
            view->clearSelection();
            settle(120);
            check(!view->edgeDimensionSuppressed(),
                  "letting go of the edge lets the annotation come back");
            // Two assertions, two checks. Collapsed into one they report as a
            // single failure, and "the annotation is gone" and "the annotation
            // reads the wrong length" are different bugs with different causes.
            check(view->dimension().isShowing(),
                  "and the cursor still resting on it annotates it again immediately");
            check(view->dimension().labelText() == expected,
                  QStringLiteral("with the same length it had before (\"%1\" against "
                                 "\"%2\")")
                      .arg(QString::fromStdString(view->dimension().labelText()),
                           QString::fromStdString(expected)));
            moveTo(view, QPointF(8, 8));
            check(!view->dimension().isShowing(),
                  "moving the cursor off it is then what clears it");

            // The hover half, with nothing selected to fall back on - unchanged
            // by Task 4, because a hover raises no gizmo.
            moveTo(view, QPointF(screen));
            check(view->dimension().isShowing(), "hovering an edge shows its length");
            check(view->dimension().labelText() == expected,
                  "the hovered length is the same edge's true length");
            moveTo(view, QPointF(8, 8));
            check(!view->dimension().isShowing(), "and leaving it clears the dimension");

            clickAt(view, QPointF(screen));
            settle(100);
            check(view->hasBevelArrow(), "clicking it again raises the arrow again");
            check(view->selectedSolidIds().size() == 1,
                  "and selecting an edge selects the body it belongs to");
            const std::size_t bodiesBefore = window.document().count();
            trigger(window, QStringLiteral("Delete Selected"));
            settle(150);
            check(window.document().count() == bodiesBefore - 1, "deleting removes the body");
            check(!view->hasBevelArrow() && !view->dimension().isShowing(),
                  "and the arrow and the annotation both go with it rather than "
                  "floating where the body was");

            trigger(window, QStringLiteral("Undo"));
            settle(150);
            check(window.document().count() == bodiesBefore,
                  "the body comes back for the checks that follow");
        }

        // Put the world back for the blocks written against it.
        trigger(window, QStringLiteral("Select Bodies"));
        view->clearSelection();
        view->animateTo(cameraBefore);   // animations are off: this is immediate
        settle(150);
    }

    // --- a flat face can become the sketch plane ------------------------------
    // The one part of this phase that changes what the app can build: an
    // outline on the side of a body, extruding out of it rather than up.
    {
        // A body to pick a side face from, and a camera that definitely
        // frames it. Both are put back at the end of the block so the checks
        // after this one see the state they were written against.
        if (window.document().count() == 0) buildBody(window, 0.25, 0.35, 0.45, 0.6, 60.0);
        check(window.document().count() > 0, "there is a body to lock a face on");
        const CameraState cameraBefore = view->camera().state();
        view->fitAll();
        settle(200);

        trigger(window, QStringLiteral("Select Faces"));
        settle(120);

        // Pick the face by PROJECTING it, never by a hardcoded pixel: a
        // literal (360, 340) passes only for as long as the camera and
        // everything built before this block stay exactly as they are today.
        // The target is a vertical side face - locking a ground-parallel one
        // would prove nothing that Z=0 does not already.
        //
        // Projecting a centre of mass says where a face WOULD be if nothing
        // stood in front of it; several bodies exist by now and any of them
        // can occlude any other. So each candidate is clicked and the result
        // read back, and only a click that actually selected a vertical flat
        // face is accepted. A check that passed because a click happened to
        // land somewhere is a check that fails on an unrelated change three
        // tasks from now.
        // BRepAdaptor_Surface carries geometry and location only - it never
        // applies TopAbs_Orientation - so on a TopAbs_REVERSED face the
        // surface normal points INTO the body. Everything below reasons about
        // the OUTWARD normal, which is what the app stores and extrudes along.
        //
        // This lambda is also why the orientation bug did not show up here
        // first: the "is it facing the camera" test used the raw surface
        // normal, and on a REVERSED face that normal points away from the eye,
        // so every REVERSED face was silently filtered out of the candidate
        // list. The picker could only ever land on a face whose surface normal
        // already happened to be the outward one.
        auto outwardNormal = [](const TopoDS_Face& face) {
            gp_Dir normal = BRepAdaptor_Surface(face).Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            return normal;
        };
        auto isVerticalPlane = [](const TopoDS_Face& face) {
            if (face.IsNull()) return false;
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane) return false;
            return std::fabs(surface.Plane().Axis().Direction().Z()) < 0.1;
        };

        TopoDS_Face picked;
        TopoDS_Shape pickedBody;
        QPoint screen;
        // Where the outline's four corners get clicked. Built as real points ON
        // the candidate's plane and then projected - never as a pixel offset
        // from the centre. See the comment where they are computed.
        QPoint outlineCorners[4];
        // A fifth point on the plane, for the cursor-readout probe.
        QPoint hoverAt;
        // The WORLD points those pixels came from, kept so they can be
        // projected again. Locking a face now flies the camera square onto it
        // (Phase 7's item 1), so every pixel computed before the lock is aimed
        // at a camera that has since moved - and a stale pixel does not fail
        // loudly, it clicks somewhere else and takes a plausible-looking wrong
        // answer with it.
        gp_Pnt outlineCornerPoints[4];
        gp_Pnt hoverPoint;
        // Projecting a centre of mass says where a face WOULD be if nothing
        // stood in front of it; several bodies exist by now and any of them
        // can occlude any other. So each candidate is clicked and the result
        // read back, and only a click that actually selected a vertical flat
        // face is accepted. A check that passed because a click happened to
        // land somewhere is a check that fails on an unrelated change three
        // tasks from now.
        //
        // REVERSED faces are tried first, so that when the model offers one
        // the end-to-end path runs over the case the orientation fix exists
        // for rather than over the easy one. The deterministic coverage of
        // that fix is the box probe further down; this is belt and braces.
        gp_Pnt pickedCentre;
        for (int pass = 0; pass < 2 && picked.IsNull(); ++pass) {
            const bool wantReversed = (pass == 0);
            for (const DocumentModel::Solid& solid : window.document().solids()) {
                for (TopExp_Explorer it(solid.shape, TopAbs_FACE); it.More(); it.Next()) {
                    const TopoDS_Face candidate = TopoDS::Face(it.Current());
                    if (!isVerticalPlane(candidate)) continue;
                    if ((candidate.Orientation() == TopAbs_REVERSED) != wantReversed) continue;
                    // Only a face whose OUTWARD normal points back at the
                    // camera can be picked at all.
                    if (gp_Vec(outwardNormal(candidate))
                            .Dot(gp_Vec(view->camera().viewDirection())) >= 0.0)
                        continue;

                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(candidate, props);
                    QPoint at;
                    if (!view->projectToScreen(props.CentreOfMass(), at)) continue;
                    if (!view->rect().adjusted(20, 20, -20, -20).contains(at)) continue;

                    // The points this block will later click, chosen as real
                    // points ON the candidate's plane and then projected, never
                    // as a fixed pixel offset from the projected centre.
                    //
                    // A vertical face can be almost edge-on to the axonometric
                    // camera - every vertical face this model offers here is
                    // within 8 degrees of edge-on - and on such a plane a fixed
                    // pixel offset walks past the plane's own horizon. The ray
                    // then meets the plane BEHIND the eye, pointOnSketchPlane()
                    // rejects it (see the toHit.Dot(direction) <= 0 guard), and
                    // the click places no point at all. Points built on the
                    // plane round-trip through projectToScreen() by
                    // construction, at any grazing angle.
                    //
                    // That pixel offsets ever worked here was luck, not design:
                    // this model is itself built from earlier screen clicks, so
                    // any change to the viewport's size reshapes it and re-rolls
                    // that luck. Adding the app bar shortened the viewport by
                    // 8px and turned the offsets into misses.
                    const gp_Pln candidatePlane = BRepAdaptor_Surface(candidate).Plane();
                    Standard_Real cu0 = 0.0, cv0 = 0.0;
                    ElSLib::Parameters(candidatePlane, props.CentreOfMass(), cu0, cv0);
                    // Comfortably wider than the snap grid, so the four corners
                    // stay distinct once snapped.
                    const double half = 20.0;
                    const double du[4] = {-half,  half, half, -half};
                    const double dv[4] = {-half, -half, half,  half};
                    QPoint corners[4];
                    gp_Pnt cornerPoints[4];
                    bool cornersUsable = true;
                    for (int c = 0; c < 4 && cornersUsable; ++c) {
                        cornerPoints[c] =
                            ElSLib::Value(cu0 + du[c], cv0 + dv[c], candidatePlane);
                        cornersUsable =
                            view->projectToScreen(cornerPoints[c], corners[c]) &&
                            view->rect().adjusted(8, 8, -8, -8).contains(corners[c]);
                    }
                    if (!cornersUsable) continue;
                    QPoint hoverCandidate;
                    const gp_Pnt hoverOnPlane =
                        ElSLib::Value(cu0 + half * 0.5, cv0 + half * 0.5, candidatePlane);
                    if (!view->projectToScreen(hoverOnPlane, hoverCandidate) ||
                        !view->rect().adjusted(8, 8, -8, -8).contains(hoverCandidate))
                        continue;

                    clickAt(view, QPointF(at));
                    settle(120);
                    const TopoDS_Face got = view->selectedFace();
                    // The face this loop is REASONING about, not merely some
                    // vertical face that happens to be selected. Everything
                    // below - the outline corners, the plane, the outwardness
                    // assertion, the lifted click - is derived from
                    // `candidate`, so accepting a different face under
                    // occlusion would silently measure one face's plane
                    // against another face's geometry. The pull loop further
                    // down has required IsSame from the start; this one
                    // checked only the shape's kind.
                    if (got.IsNull() || !got.IsSame(candidate)) continue;
                    picked = got;
                    pickedCentre = props.CentreOfMass();
                    screen = at;
                    std::copy(corners, corners + 4, outlineCorners);
                    std::copy(cornerPoints, cornerPoints + 4, outlineCornerPoints);
                    hoverAt = hoverCandidate;
                    hoverPoint = hoverOnPlane;
                    break;
                }
                if (!picked.IsNull()) break;
            }
        }
        check(!picked.IsNull(),
              "clicking a projected face centre selects a vertical flat face");


        // The host is the body that actually CONTAINS the selected face, not
        // whichever body the loop happened to be iterating when the click
        // landed: under occlusion those are different bodies, and measuring
        // the locked plane against the wrong centroid makes the outwardness
        // assertion below either vacuous or spuriously red. Derived, so the
        // check measures what its message says it measures.
        if (!picked.IsNull()) {
            for (const DocumentModel::Solid& solid : window.document().solids()) {
                for (TopExp_Explorer it(solid.shape, TopAbs_FACE); it.More(); it.Next()) {
                    if (!it.Current().IsSame(picked)) continue;
                    pickedBody = solid.shape;
                    break;
                }
                if (!pickedBody.IsNull()) break;
            }
        }
        check(!picked.IsNull() && !pickedBody.IsNull(),
              "the body that owns the selected face is identified");

        if (!picked.IsNull() && !pickedBody.IsNull()) {
            QAction* lock = action(window, QStringLiteral("Lock to Face"));
            check(lock != nullptr, "there is an action to lock a face");
            check(lock != nullptr && lock->isEnabled(),
                  "selecting one flat face enables it");

            if (lock && lock->isEnabled()) {
                const gp_Pln facePlane = BRepAdaptor_Surface(picked).Plane();
                const gp_Dir faceOutward = outwardNormal(picked);
                // The pose every pixel above was projected at. The lock flies
                // the camera away from it (see below), and one probe further
                // down draws on the GROUND while this face is unlocked -
                // impossible from a camera squared onto a vertical face, where
                // the ground plane is exactly edge-on and no ray meets it.
                const CameraState poseBeforeLock = view->camera().state();
                lock->trigger();
                settle(200);
                check(window.isFaceLocked(), "the face is locked");

                // Phase 7 item 1: locking also FLIES the camera square onto
                // the face, orthographic, so the face reads at its true shape
                // before a single point is drawn on it. Asserted as the dot
                // between the camera's view direction and the face's own
                // outward normal - "square onto" is a direction, and a check
                // that only compared azimuths would pass on a face whose
                // normal happens to share one.
                const double squareness =
                    view->camera().viewDirection().Dot(faceOutward);
                check(squareness < -0.999,
                      QStringLiteral("the camera flies square onto the face - view "
                                     "direction antiparallel to its outward normal "
                                     "(dot %1)")
                          .arg(squareness));
                check(view->viewIsOrthographic(),
                      "and lands orthographic, so nothing converges");
                check(view->camera().baseProjection() ==
                          CameraController::Projection::Perspective,
                      "on loan - the mode the user chose is untouched");
                // The BOUNDING BOX centre, which is what flyOntoFace() aims at
                // (through CameraController::frame), not the centre of mass
                // this block picked the face by. They coincide on a rectangle
                // and separate on an L-shaped or tapered face, so deriving the
                // oracle the way the code derives the target is what stops an
                // asymmetric face reading as a targeting error - or, worse,
                // hiding one behind a tolerance widened to accommodate it.
                Bnd_Box faceBox;
                BRepBndLib::Add(picked, faceBox);
                Standard_Real bx0, by0, bz0, bx1, by1, bz1;
                faceBox.Get(bx0, by0, bz0, bx1, by1, bz1);
                const gp_Pnt faceBoxCentre((bx0 + bx1) / 2.0, (by0 + by1) / 2.0,
                                           (bz0 + bz1) / 2.0);
                check(view->camera().state().target.Distance(faceBoxCentre) < 1.0,
                      QStringLiteral("aimed at the centre of the face's own extent "
                                     "(%1 mm off)")
                          .arg(view->camera().state().target.Distance(faceBoxCentre)));

                // Every pixel this block computed before the lock was aimed at
                // the camera the lock has just moved. Projected again from the
                // world points they came from, and checked to still be inside
                // the viewport - a click that lands outside it does nothing at
                // all, and "did nothing" is not distinguishable from "did the
                // wrong thing" three checks later.
                bool reprojected = view->projectToScreen(pickedCentre, screen) &&
                                   view->rect().adjusted(8, 8, -8, -8).contains(screen);
                for (int c = 0; c < 4; ++c) {
                    reprojected = reprojected &&
                                  view->projectToScreen(outlineCornerPoints[c],
                                                        outlineCorners[c]) &&
                                  view->rect().adjusted(8, 8, -8, -8).contains(
                                      outlineCorners[c]);
                }
                reprojected = reprojected &&
                              view->projectToScreen(hoverPoint, hoverAt) &&
                              view->rect().adjusted(8, 8, -8, -8).contains(hoverAt);
                check(reprojected,
                      "and the points this block clicks re-project inside the "
                      "viewport at the camera the flight left");

                // The lock flight's own result, captured at the camera it
                // left rather than at a pose arranged for a picture. The face
                // this block locks is a vertical one on a thin slab, so seen
                // dead on it IS a thin strip - unflattering, and honest; the
                // elevation pair near the end of this file is the readable
                // demonstration.
                view->saveSnapshot(outDir + "/j-locked-face-orthographic.png");

                // The sketch plane must BE the face's plane, not merely something.
                const gp_Pln sketchPlane = window.sketch().plane();
                check(std::fabs(facePlane.Distance(sketchPlane.Location())) < 1.0e-6,
                      "the sketch plane sits on the face");
                check(std::fabs(sketchPlane.Axis().Direction().Z()) < 0.1,
                      "and the locked plane really is a vertical one, not the ground");

                // SIGNED, not IsParallel. IsParallel is true for antiparallel
                // directions too, so a plane whose normal points INTO the body
                // sails through it - and that is exactly the bug: extrude
                // sweeps along this normal, so an inward one puts the shelf
                // inside the cabinet. The centroid of the body the face came
                // from is the reference that cannot be argued with.
                GProp_GProps hostProps;
                BRepGProp::VolumeProperties(pickedBody, hostProps);
                const gp_Pnt hostCentre = hostProps.CentreOfMass();
                const double outwardness =
                    gp_Vec(hostCentre, sketchPlane.Location())
                        .Dot(gp_Vec(sketchPlane.Axis().Direction()));
                check(outwardness > 0.0,
                      QStringLiteral("the locked plane's normal points out of the body, "
                                     "not into it (%1)")
                          .arg(outwardness));

                // A locked sketch plane is a persistent mode, so it needs a
                // persistent cue: the status-bar message that announces the
                // lock scrolls away, and the grid's orientation is easy to
                // misread once the camera moves.
                check(stateLabelText(window).contains(QStringLiteral("locked face")),
                      QStringLiteral("the state label says the plane is locked (\"%1\")")
                          .arg(stateLabelText(window)));

                // The grid is drawn in the 3D view, so the viewport dump is
                // the only place it can be seen at all.
                view->saveSnapshot(outDir + "/h-locked-face-grid.png");

                // The part that makes this a feature rather than a label:
                // a point clicked now lands ON the face's plane, not on Z=0.
                //
                // Clicked 20 mm UP the face rather than at its centre, and
                // that matters. The locked plane is vertical, so +Z lies in
                // it and the lifted point is on it by construction - but the
                // ground plane and a vertical locked plane AGREE at Z = 0,
                // and the candidate this probe finds is a side face of a
                // 10 mm slab whose centre sits at Z = 5, which the 10 mm snap
                // rounds straight back to zero. The assertion below would
                // then be false however perfectly the feature worked. Lifting
                // the click puts it somewhere the two planes cannot agree,
                // which is the only place the claim can actually be tested.
                const gp_Pnt liftedOnPlane = pickedCentre.Translated(gp_Vec(0.0, 0.0, 20.0));
                QPoint liftedAt;
                const bool haveLifted =
                    view->projectToScreen(liftedOnPlane, liftedAt) &&
                    view->rect().adjusted(8, 8, -8, -8).contains(liftedAt);
                check(haveLifted,
                      "a point 20 mm up the locked face projects inside the viewport, "
                      "where the ground plane and the locked one cannot agree");
                trigger(window, QStringLiteral("Start Sketch"));
                settle(120);
                clickAt(view, QPointF(haveLifted ? liftedAt : screen));
                settle(150);
                check(window.sketch().pointCount() == 1,
                      "clicking while locked places a point");
                if (window.sketch().pointCount() == 1) {
                    const gp_Pnt placed = window.sketch().points().front();
                    check(std::fabs(facePlane.Distance(placed)) < 1.0e-6,
                          QStringLiteral("the point lands on the locked face's plane "
                                         "(%1 mm off it)")
                              .arg(facePlane.Distance(placed)));
                    check(std::fabs(placed.Z()) > 1.0e-6,
                          QStringLiteral("and not on the ground plane it would have used "
                                         "before (Z = %1)")
                              .arg(placed.Z()));
                }

                // The readout has to be the PLANE's own coordinates. On a face
                // locked at a fixed world y, running the cursor up the face
                // changes only Z - so a world X/Y readout froze one number and
                // left the other meaningless in the plane the user is drawing
                // in. ElSLib::Parameters is what snapToPlaneGrid already uses,
                // so the two agree by construction.
                moveTo(view, QPointF(hoverAt));
                settle(120);
                gp_Pnt cursor;
                check(view->lastHoverPoint(cursor),
                      "the cursor's point on the locked plane is known");
                Standard_Real cu = 0.0, cv = 0.0;
                ElSLib::Parameters(window.sketch().plane(), cursor, cu, cv);
                const QString inPlane =
                    QStringLiteral("Cursor at %1, %2")
                        .arg(QString::fromStdString(Measure::formatLength(cu)),
                             QString::fromStdString(Measure::formatLength(cv)));
                const QString inWorld =
                    QStringLiteral("Cursor at %1, %2")
                        .arg(QString::fromStdString(Measure::formatLength(cursor.X())),
                             QString::fromStdString(Measure::formatLength(cursor.Y())));
                check(window.statusBar()->currentMessage() == inPlane,
                      QStringLiteral("the readout is the locked plane's own coordinates "
                                     "(\"%1\")").arg(window.statusBar()->currentMessage()));
                check(inPlane != inWorld,
                      QStringLiteral("and on this face the two really do differ, so that "
                                     "check is not vacuous (plane \"%1\", world \"%2\")")
                          .arg(inPlane, inWorld));

                trigger(window, QStringLiteral("Cancel Sketch"));
                settle(120);

                // And now the whole point of the feature: draw an outline on
                // the locked face and extrude it. Volume alone would NOT have
                // caught the inward-sweep bug - a prism swept into the body is
                // still a valid prism of the right volume, just in the wrong
                // place - so the check that bites is where the new body's
                // centre of mass ends up relative to the face it grew from.
                trigger(window, QStringLiteral("Start Sketch"));
                settle(120);
                for (const QPoint& corner : outlineCorners)
                    clickAt(view, QPointF(corner));
                settle(120);
                check(window.sketch().pointCount() == 4,
                      "four points land on the locked face");

                // The outline's area, in the plane's own coordinates, computed
                // here so the expected volume is not read back out of the
                // thing under test.
                double area = 0.0;
                const std::vector<gp_Pnt> outline = window.sketch().points();
                for (std::size_t k = 0; k < outline.size(); ++k) {
                    Standard_Real u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
                    ElSLib::Parameters(sketchPlane, outline[k], u0, v0);
                    ElSLib::Parameters(sketchPlane, outline[(k + 1) % outline.size()], u1, v1);
                    area += u0 * v1 - u1 * v0;
                }
                area = std::fabs(area) * 0.5;
                check(area > 1.0, QStringLiteral("the outline encloses real area (%1)").arg(area));

                trigger(window, QStringLiteral("Finish Sketch"));
                settle(150);
                check(window.hasPendingFace(), "the outline on the locked face closes");

                const std::size_t bodiesBefore = window.document().count();
                const double shelfHeight = 30.0;
                check(window.extrudePendingFace(shelfHeight),
                      "and extrudes into a body");
                settle(150);
                if (window.document().count() == bodiesBefore + 1) {
                    const TopoDS_Shape shelf = window.document().solids().back().shape;
                    check(std::fabs(ModelingOps::volume(shelf) - area * shelfHeight) <
                              area * shelfHeight * 1.0e-6,
                          QStringLiteral("whose volume is the outline times the height "
                                         "(%1 against %2)")
                              .arg(ModelingOps::volume(shelf))
                              .arg(area * shelfHeight));

                    GProp_GProps shelfProps;
                    BRepGProp::VolumeProperties(shelf, shelfProps);
                    const double standsProud =
                        gp_Vec(sketchPlane.Location(), shelfProps.CentreOfMass())
                            .Dot(gp_Vec(sketchPlane.Axis().Direction()));
                    check(standsProud > 0.0,
                          QStringLiteral("and stands proud of the locked face rather than "
                                         "sinking into the body behind it (%1)")
                              .arg(standsProud));
                }

                // Extruding calls SketchController::reset(), which clears the
                // points and must NOT clear the plane - a lock that silently
                // expired on the first extrude would make the feature useless
                // for the second shelf.
                check(window.isFaceLocked() &&
                          std::fabs(facePlane.Distance(window.sketch().plane().Location())) <
                              1.0e-6,
                      "the face stays locked after extruding on it");

                // Put the document back where the rest of the suite expects it.
                trigger(window, QStringLiteral("Undo"));
                settle(150);
                check(window.document().count() == bodiesBefore,
                      "the shelf is undone, leaving the document as it was");

                // A curved face has no single plane to draw on, and the
                // refusal has to say so rather than silently doing nothing.
                // Built here rather than modelled: nothing in this document
                // is round, and the check is about the refusal, not about
                // how the cylinder got made.
                ToastHost* toasts = window.findChild<ToastHost*>();
                TopoDS_Face curved;
                const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(20.0, 40.0).Shape();
                for (TopExp_Explorer it(cylinder, TopAbs_FACE); it.More(); it.Next()) {
                    if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() ==
                        GeomAbs_Plane)
                        continue;
                    curved = TopoDS::Face(it.Current());
                    break;
                }
                check(!curved.IsNull(), "a curved face is available to refuse");
                if (!curved.IsNull() && toasts) {
                    check(!window.lockToFace(curved), "a curved face cannot be locked");
                    check(toasts->currentText() ==
                              QStringLiteral("This face isn't flat, so it can't hold an "
                                             "outline. Pick a flat face and try again."),
                          QStringLiteral("and the refusal names the cause and the fix "
                                         "(\"%1\")")
                              .arg(toasts->currentText()));
                    // IsEqual, not IsParallel: "unchanged" has to include the
                    // sense of the normal, or a refusal that quietly flipped
                    // the plane the user is drawing on would read as a pass.
                    check(window.isFaceLocked() &&
                              window.sketch().plane().Axis().Direction().IsEqual(
                                  sketchPlane.Axis().Direction(), 1.0e-9) &&
                              std::fabs(facePlane.Distance(
                                  window.sketch().plane().Location())) < 1.0e-6,
                          "a refused lock leaves the plane that was already locked alone");
                }

                QAction* unlock = action(window, QStringLiteral("Unlock Face"));
                check(unlock != nullptr && unlock->isEnabled(), "it can be unlocked");
                if (unlock) {
                    unlock->trigger();
                    settle(150);
                    check(!window.isFaceLocked(), "unlocking releases it");
                    check(std::fabs(window.sketch().plane().Location().Z()) < 1.0e-9,
                          "and returns to the ground plane");
                    check(!unlock->isEnabled(),
                          "and there is nothing left to unlock");
                    check(!stateLabelText(window).contains(QStringLiteral("locked face")),
                          QStringLiteral("and the state label stops saying so (\"%1\")")
                              .arg(stateLabelText(window)));
                    // The GROUND grid is what this picture is for, and the
                    // lock flight left the camera square onto a vertical face,
                    // where the ground is exactly edge-on. Angled for the
                    // snapshot; put back below, with everything else the box
                    // probe disturbs.
                    view->camera().setTemporaryOrtho(false);
                    view->animateTo(CameraState{});
                    settle(120);
                    view->saveSnapshot(outDir + "/h-unlocked-ground-grid.png");
                }

                // Locking is a capability the learning system can teach, so
                // it has to be recorded like every other one.
                check(window.progress().count("faceLock.used") >= 1,
                      "locking a face is recorded as something the user has done");

                // Deterministic coverage of the orientation flip, independent
                // of which face the picker above happened to reach. A plain
                // BRepPrimAPI_MakeBox has three TopAbs_REVERSED faces whose
                // surface normals point into the box; every one of the six
                // must lock to a plane whose normal points OUT.
                {
                    const TopoDS_Shape probe =
                        BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 100.0, 60.0, 40.0).Shape();
                    GProp_GProps probeProps;
                    BRepGProp::VolumeProperties(probe, probeProps);
                    const gp_Pnt probeCentre = probeProps.CentreOfMass();

                    int reversedSeen = 0, outward = 0, faces = 0;
                    for (TopExp_Explorer it(probe, TopAbs_FACE); it.More(); it.Next()) {
                        const TopoDS_Face f = TopoDS::Face(it.Current());
                        if (f.Orientation() == TopAbs_REVERSED) ++reversedSeen;
                        if (!window.lockToFace(f)) continue;
                        ++faces;
                        const gp_Pln locked = window.sketch().plane();
                        if (gp_Vec(probeCentre, locked.Location())
                                .Dot(gp_Vec(locked.Axis().Direction())) > 0.0)
                            ++outward;
                    }
                    check(faces == 6, "every face of the probe box locks");
                    check(reversedSeen > 0,
                          QStringLiteral("the probe box really does carry reversed faces "
                                         "(%1 of 6), so this check exercises the flip")
                              .arg(reversedSeen));
                    check(outward == faces,
                          QStringLiteral("every locked face stores its OUTWARD normal "
                                         "(%1 of %2)").arg(outward).arg(faces));

                    // The probe leaves the window locked to a box that is not
                    // in the document; hand the rest of the suite the ground
                    // plane it was written against.
                    window.unlockFace();
                    check(!window.isFaceLocked(), "the probe leaves nothing locked");
                }

                // A closed outline pins the plane it was drawn on. Both the
                // commit and the live preview sweep the pending face along
                // whatever the sketch plane's normal is AT THAT MOMENT, so
                // locking a different face in between sweeps a ground-plane
                // outline along a direction lying in its own plane - a body
                // with no volume that BRepPrimAPI_MakePrism calls done.
                {
                    // Back to the angled pose this block started from, and
                    // re-projected onto it. This probe draws its outline on
                    // the GROUND, which is edge-on to the face-on camera the
                    // lock flight leaves behind - no ray meets a plane it is
                    // parallel to, so not one point would land.
                    view->camera().setTemporaryOrtho(false);
                    view->animateTo(poseBeforeLock);
                    settle(120);
                    bool angledAgain =
                        view->projectToScreen(pickedCentre, screen) &&
                        view->rect().adjusted(8, 8, -8, -8).contains(screen);
                    for (int c = 0; c < 4; ++c) {
                        angledAgain = angledAgain &&
                                      view->projectToScreen(outlineCornerPoints[c],
                                                            outlineCorners[c]) &&
                                      view->rect().adjusted(8, 8, -8, -8).contains(
                                          outlineCorners[c]);
                    }
                    check(angledAgain && !view->viewIsOrthographic(),
                          "the camera is angled again, in perspective, with this "
                          "block's click points back inside the viewport");

                    // Selected again first, so "unavailable" below means the
                    // pending outline rather than merely an empty selection.
                    clickAt(view, QPointF(screen));
                    settle(120);
                    check(!view->selectedFace().IsNull(),
                          "a flat face is selected for the pending-outline probe");
                    QAction* lockAgain = action(window, QStringLiteral("Lock to Face"));
                    check(lockAgain != nullptr && lockAgain->isEnabled(),
                          "Lock to Face is available while nothing is pending");
                    const QString ordinaryTip = lockAgain ? lockAgain->toolTip() : QString();

                    trigger(window, QStringLiteral("Start Sketch"));
                    sketchQuad(window, 0.30, 0.30, 0.44, 0.44);
                    trigger(window, QStringLiteral("Finish Sketch"));
                    settle(150);
                    check(window.hasPendingFace(),
                          "an outline is closed and waiting to be extruded");
                    const gp_Pln waitingPlane = window.sketch().plane();

                    // Entering sketch mode cleared the selection, so it has to
                    // be re-established before the check below means anything:
                    // "unavailable" has to be attributable to the pending
                    // outline and not to there being no face selected at all.
                    clickAt(view, QPointF(screen));
                    settle(120);
                    check(!view->selectedFace().IsNull(),
                          "a flat face is selected again, so the next check is not vacuous");

                    check(lockAgain != nullptr && !lockAgain->isEnabled(),
                          "Lock to Face goes unavailable while that outline waits");
                    check(lockAgain != nullptr && lockAgain->toolTip() != ordinaryTip &&
                              lockAgain->toolTip().contains(QStringLiteral("outline")),
                          QStringLiteral("and says why, rather than just looking broken "
                                         "(\"%1\")")
                              .arg(lockAgain ? lockAgain->toolTip() : QString()));

                    // The double-click route never consults that enabled
                    // state, so the refusal has to live in lockToFace() too.
                    check(!window.lockToFace(picked),
                          "and the call the double-click route uses refuses as well");
                    check(toasts != nullptr &&
                              toasts->currentText().contains(QStringLiteral("outline")),
                          QStringLiteral("naming the cause and the fix (\"%1\")")
                              .arg(toasts ? toasts->currentText() : QString()));
                    check(!window.isFaceLocked() &&
                              window.sketch().plane().Axis().Direction().IsEqual(
                                  waitingPlane.Axis().Direction(), 1.0e-9),
                          "leaving the plane the waiting outline belongs to exactly alone");

                    // And the outline it protected still extrudes, on that
                    // plane, into a body with real volume.
                    const std::size_t bodiesNow = window.document().count();
                    check(window.extrudePendingFace(15.0),
                          "the protected outline still extrudes");
                    settle(150);
                    check(window.document().count() == bodiesNow + 1 &&
                              ModelingOps::volume(
                                  window.document().solids().back().shape) > 1.0,
                          "into a body with real volume, not a flat one");
                    trigger(window, QStringLiteral("Undo"));
                    settle(150);
                    check(window.document().count() == bodiesNow,
                          "which is undone again for the checks that follow");

                    // The same refusal the other way round: locked, with an
                    // outline pending on the locked face, unlocking would
                    // re-aim it back at the ground.
                    check(window.lockToFace(picked),
                          "the face locks again once nothing is pending");
                    trigger(window, QStringLiteral("Start Sketch"));
                    // The same plane-derived corners the first outline used,
                    // and for the same reason - this face is nearly edge-on,
                    // so a pixel offset from its centre is not reliably a
                    // point on it. Nothing has moved the camera since they
                    // were projected.
                    for (const QPoint& corner : outlineCorners)
                        clickAt(view, QPointF(corner));
                    trigger(window, QStringLiteral("Finish Sketch"));
                    settle(150);
                    check(window.hasPendingFace(),
                          "an outline on the locked face is waiting in turn");

                    QAction* unlockAgain = action(window, QStringLiteral("Unlock Face"));
                    check(unlockAgain != nullptr && !unlockAgain->isEnabled(),
                          "Unlock Face goes unavailable while it waits");
                    check(unlockAgain != nullptr &&
                              unlockAgain->toolTip().contains(QStringLiteral("outline")),
                          QStringLiteral("and says why too (\"%1\")")
                              .arg(unlockAgain ? unlockAgain->toolTip() : QString()));
                    window.unlockFace();
                    check(window.isFaceLocked(),
                          "and calling it directly leaves the face locked");

                    // Starting another outline is the way out, and it brings
                    // both actions back.
                    trigger(window, QStringLiteral("Start Sketch"));
                    trigger(window, QStringLiteral("Cancel Sketch"));
                    settle(120);
                    check(!window.hasPendingFace(),
                          "starting a fresh outline drops the one that was waiting");
                    check(unlockAgain != nullptr && unlockAgain->isEnabled(),
                          "and Unlock Face is available again");
                    window.unlockFace();
                    check(!window.isFaceLocked(),
                          "the pending-outline probe leaves nothing locked either");
                }
            }
        }

        // Put the world back: body selection, no lock, the camera where the
        // blocks after this one expect it.
        trigger(window, QStringLiteral("Select Bodies"));
        view->clearSelection();
        view->animateTo(cameraBefore);   // animations are off: this is immediate
        settle(150);
    }

    // --- extrude asks for a height without stopping the user ------------------
    {
        // Draw an outline and close it, so a face is pending.
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);

        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr, "extrude opens a preview rather than a dialog");
        check(preview != nullptr && preview->isVisible(), "the preview is visible");
        check(window.findChildren<QDialog*>().isEmpty(),
              "extrude never constructs a dialog");
        check(preview != nullptr && preview->hasPreview(),
              "a preview shape is shown before the user commits anything");
        // Cross-checked against the viewport's own state, not just the
        // panel's bare flag - fix round 1, Minor 4: hasPreview() alone would
        // pass for a panel that sets the flag and displays nothing.
        check(view->hasPreview(),
              "the viewport itself actually holds the preview shape");
        check(static_cast<int>(window.document().solids().size()) == before,
              "previewing creates no body");

        // Pinned before it is used as a guard. SEVEN checks hang off this
        // condition - five inside the block and the two after it, which only
        // mean anything because the Enter at the end of the block was sent -
        // and a preview that came up without a field would take all seven with
        // it in silence.
        check(preview != nullptr && preview->field() != nullptr,
              "the preview carries its height field, so the seven checks that "
              "depend on it cannot vanish quietly");
        if (preview && preview->field()) {
            preview->field()->setText(QStringLiteral("25"));
            settle(150);
            check(preview->hasPreview(), "editing the height keeps a live preview");
            check(view->hasPreview(), "the viewport reflects the edited height too");

            // Garbage must not clear the preview or flicker the viewport.
            preview->field()->setText(QStringLiteral("abc"));
            settle(150);
            check(preview->hasPreview(),
                  "an unparseable height leaves the last good preview alone");
            check(view->hasPreview(),
                  "the viewport still holds the last good preview, not nothing");
            check(static_cast<int>(window.document().solids().size()) == before,
                  "an unparseable height creates no body");

            preview->field()->setText(QStringLiteral("25"));
            settle(100);
            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &commit);
            settle(250);
        }

        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "Enter commits the extrude");
        ExtrudePreview* after = window.findChild<ExtrudePreview*>();
        check(after == nullptr || !after->isVisible(),
              "committing closes the preview");
        if (!window.document().solids().empty()) {
            const QString dims = QString::fromStdString(
                Measure::formatDimensions(window.document().solids().back().shape));
            // Fix round 1, Minor 4: dims.find("25") would also match a 250mm
            // width or depth component - "x <times> y <times> z mm" (see
            // Measure::formatDimensions), so check the height specifically,
            // the last of the three numbers, rather than anywhere in the
            // string.
            const QStringList parts = dims.split(QString::fromUtf8("\xC3\x97"));
            check(!parts.isEmpty() && parts.last().trimmed() == QStringLiteral("25 mm"),
                  QStringLiteral("the body's height specifically is 25 mm, not just "
                                 "some dimension that contains \"25\" (\"%1\")")
                      .arg(dims));
        }
    }

    // --- the preview owns Enter and Escape whatever holds focus ---------------
    // The whole point of a LIVE preview is that the user orbits to look at the
    // shape before committing to a height. That orbit is a press in the
    // viewport, which is Qt::StrongFocus - and every ToolChip became focusable
    // too - so focus left the panel's field, and Enter and Escape had been
    // wired only to a filter on that field. Nothing else consumed Escape
    // either (Cancel Sketch's binding is disabled while a preview can be
    // open), and the panel has no buttons: the user was stranded with a
    // preview shape and no route to commit or cancel it.
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible(),
              "a preview is open before the orbit");
        // The panel has to SAY what the two keys are - a modeless panel with
        // invisible verbs is how this went unnoticed for a whole branch.
        bool saysKeys = false;
        if (preview) {
            for (const QString& text : preview->paintedTexts()) {
                if (text.contains(QStringLiteral("Enter")) &&
                    text.contains(QStringLiteral("Esc")))
                    saysKeys = true;
            }
        }
        check(saysKeys, "the panel tells the user which keys commit and cancel");

        const int before = static_cast<int>(window.document().solids().size());

        // The orbit. clickAt()/dragButton() send events straight to a widget
        // and so never move focus the way a real press does (Qt does that in
        // QWidgetWindow, which synthetic delivery bypasses), so the focus
        // change is made explicitly - otherwise this probe would pass against
        // exactly the broken code it exists to catch.
        view->setFocus(Qt::MouseFocusReason);
        dragButton(view, QPointF(600, 400), QPointF(660, 430), Qt::RightButton);
        check(preview != nullptr && preview->field() != nullptr &&
                  window.focusWidget() != preview->field(),
              "orbiting takes focus off the height field, as a real press does");
        check(preview != nullptr && preview->isVisible(),
              "the preview survives the orbit");

        // Enter, delivered to the focus widget - the viewport, not the field.
        sendKeyTo(&window, Qt::Key_Return);
        settle(200);
        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "Enter still commits after an orbit moved focus off the field");
        ExtrudePreview* afterCommit = window.findChild<ExtrudePreview*>();
        check(afterCommit == nullptr || !afterCommit->isVisible(),
              "and the panel closes on that commit");

        // Same again for Escape.
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        const int beforeEscape = static_cast<int>(window.document().solids().size());
        view->setFocus(Qt::MouseFocusReason);
        dragButton(view, QPointF(600, 400), QPointF(650, 420), Qt::RightButton);
        sendKeyTo(&window, Qt::Key_Escape);
        settle(200);

        ExtrudePreview* afterEscape = window.findChild<ExtrudePreview*>();
        check(afterEscape == nullptr || !afterEscape->isVisible(),
              "Escape still cancels after an orbit moved focus off the field");
        check(static_cast<int>(window.document().solids().size()) == beforeEscape,
              "and that cancel created no body");
        check(window.hasPendingFace(),
              "the pending face survives a cancel from the viewport too");
        trigger(window, QStringLiteral("Start Sketch"));
        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(120);
    }

    // --- starting a new sketch closes an open extrude preview -----------------
    // Fix round 1, Important 1: onStartSketch() nulls the pending face and
    // resets the view's preview slot to empty, but that alone used to leave
    // the panel itself open and still believing it had a good preview - the
    // next keystroke redisplayed a body-shaped shape over the new outline,
    // and Enter reached commit(), where extrudePendingFace() silently failed
    // on the now-null pending face, so hide() never ran and the shape stayed
    // on screen: a body visible in the viewport that exists in no document.
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible() && preview->hasPreview() &&
                  view->hasPreview(),
              "a preview is open, with a shape in the viewport, before starting a new sketch");

        trigger(window, QStringLiteral("Start Sketch"));
        settle(150);
        ExtrudePreview* stillOpen = window.findChild<ExtrudePreview*>();
        check(stillOpen == nullptr || !stillOpen->isVisible(),
              "starting a new sketch closes the open extrude preview");
        check(!view->hasPreview(),
              "and leaves no ghost preview shape behind in the viewport");

        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(150);
    }

    // --- Escape cancels the extrude preview without touching the pending face -
    {
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->isVisible() && preview->hasPreview(),
              "a preview is open before pressing Escape");
        check(window.hasPendingFace(), "a face is pending before pressing Escape");

        if (preview && preview->field()) {
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &escape);
            settle(150);
        }

        ExtrudePreview* afterEscape = window.findChild<ExtrudePreview*>();
        check(afterEscape == nullptr || !afterEscape->isVisible(),
              "Escape closes the preview");
        // This check used to assert the OPPOSITE - that the viewport was left
        // empty - which enshrined the bug rather than catching it.
        // MainWindow::onFinishSketch() shows the closed face through the
        // viewport's single preview slot; ExtrudePreview overwrites that same
        // slot with the body it would build. Clearing it on cancel therefore
        // erased the face, leaving an intact pending face, an enabled Extrude
        // action and a status bar still saying "Outline closed" above an
        // empty viewport - against Milestone 1's own criterion that closing an
        // outline produces a VISIBLE filled face.
        check(view->hasPreview(),
              "Escape puts the closed face back on screen rather than clearing it");
        // And it is the FACE, not the body the cancelled preview was showing:
        // hasPreview() alone cannot tell the two apart, which is how one
        // feature silently overwriting another's slot went unnoticed.
        const TopoDS_Shape restored = view->previewShape();
        check(!restored.IsNull() &&
                  !TopExp_Explorer(restored, TopAbs_SOLID).More() &&
                  TopExp_Explorer(restored, TopAbs_FACE).More(),
              "and what is on screen is the face, not the body it would have built");
        check(window.hasPendingFace(),
              "Escape leaves the pending face intact, so the user can retry");

        // Retry: the same face, extruded again, still works after a cancel.
        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);
        ExtrudePreview* retry = window.findChild<ExtrudePreview*>();
        if (retry && retry->field()) {
            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(retry->field(), &commit);
            settle(250);
        }
        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "the pending face left behind by Escape can still be extruded");
    }

    // --- pull a face: the headline direct-modeling gesture --------------------
    // Select a face, get an arrow, drag it. Everything here is derived from
    // projected geometry rather than a hardcoded pixel, and every distance is
    // asserted as an exact volume delta (face area x distance) rather than
    // "something changed" - a pull that moved the wrong way, or by the wrong
    // amount, has to fail loudly.
    {
        const CameraState pullCameraBefore = view->camera().state();
        // Snap decides the drag step, so it is set here rather than inherited
        // from whatever an earlier block left it at.
        QAction* snap = action(window, QStringLiteral("Snap to Grid"));
        check(snap != nullptr, "there is a Snap to Grid action");
        if (snap && !snap->isChecked()) { snap->trigger(); settle(120); }

        // A fresh CONVEX body of our own: a prism grown on a planar face of a
        // convex prism adds exactly area x distance and a carve removes
        // exactly that, so the arithmetic below is an equality rather than an
        // inequality. Pulling a face of one of the boolean results further up
        // would not have that property.
        const int bodiesBefore = static_cast<int>(window.document().count());
        check(buildBody(window, 0.56, 0.30, 0.74, 0.46, 40.0),
              "a fresh body to pull a face on");
        check(static_cast<int>(window.document().count()) == bodiesBefore + 1,
              "and it reached the document");
        const int pullId = window.document().solids().empty()
                               ? -1
                               : window.document().solids().back().id;

        view->fitAll();
        settle(250);
        trigger(window, QStringLiteral("Select Faces"));
        settle(150);

        // BRepAdaptor_Surface never applies TopAbs_Orientation, so a REVERSED
        // face's plane normal points INTO the body - the Phase 4 lesson, and
        // the one this whole feature's sign convention rests on.
        auto outwardNormal = [](const TopoDS_Face& face) {
            gp_Dir n = BRepAdaptor_Surface(face).Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
            return n;
        };
        auto bodyVolume = [&window, pullId] {
            return ModelingOps::volume(window.document().shapeOf(pullId));
        };

        TopoDS_Face target;
        gp_Pnt targetCentre;
        gp_Dir targetOutward;
        double targetArea = 0.0;
        QPoint centreAt;
        // Pass 0 wants the top face specifically - it is the one guaranteed
        // thick enough behind it for the inward carve below - and pass 1 will
        // take any pickable planar face if the top one is occluded.
        for (int pass = 0; pass < 2 && target.IsNull(); ++pass) {
            for (TopExp_Explorer it(window.document().shapeOf(pullId), TopAbs_FACE);
                 it.More(); it.Next()) {
                const TopoDS_Face candidate = TopoDS::Face(it.Current());
                if (BRepAdaptor_Surface(candidate).GetType() != GeomAbs_Plane) continue;
                const gp_Dir outward = outwardNormal(candidate);
                if (pass == 0 && outward.Z() < 0.9) continue;
                const double facing =
                    gp_Vec(outward).Dot(gp_Vec(view->camera().viewDirection()));
                if (facing >= -0.2) continue;   // turned away from the camera
                // Looking down the arrow makes the drag unmeasurable - the
                // maths helper refuses that band outright - so a face nearly
                // square-on to the eye is no use as a drag probe either.
                if (facing < -0.97) continue;

                GProp_GProps props;
                BRepGProp::SurfaceProperties(candidate, props);
                QPoint at;
                if (!view->projectToScreen(props.CentreOfMass(), at)) continue;
                if (!view->rect().adjusted(60, 60, -60, -60).contains(at)) continue;

                clickAt(view, QPointF(at));
                settle(150);
                const TopoDS_Face got = view->selectedFace();
                if (got.IsNull() || !got.IsSame(candidate)) continue;   // occluded, or missed
                target = candidate;
                targetCentre = props.CentreOfMass();
                targetOutward = outward;
                targetArea = props.Mass();
                centreAt = at;
                break;
            }
        }
        check(!target.IsNull(),
              "clicking a projected face centre selects a face of the new body");

        PullArrow* arrow = window.findChild<PullArrow*>();
        check(arrow != nullptr, "the window has a pull arrow");
        check(arrow != nullptr && arrow->isVisible(),
              "one flat face selected raises it");
        check(view->hasPullArrow(),
              "and the arrow itself is drawn in the 3D scene, not painted over it");
        check(stateLabelText(window).contains(QStringLiteral("drag the arrow to pull")),
              QStringLiteral("the state label teaches the gesture (\"%1\")")
                  .arg(stateLabelText(window)));

        // The value chip's field is a real, reachable control. childAt
        // identity, not an attribute flag: asserting a flag passes against a
        // control no user can click (CLAUDE.md's rule, learned twice).
        check(arrow != nullptr && arrow->field() != nullptr,
              "the pull arrow and its value field exist before the reachability probe");
        if (arrow && arrow->field()) {
            check(view->childAt(arrow->field()->geometry().center()) == arrow->field(),
                  "a real click at the value field's centre finds the field itself");
        }

        // --- outward drag grows the body ---------------------------------
        double volumeBefore = bodyVolume();
        QPoint dragTo;
        const bool haveOut =
            !target.IsNull() &&
            view->projectToScreen(targetCentre.Translated(gp_Vec(targetOutward) * 30.0),
                                  dragTo) &&
            view->rect().contains(dragTo);
        check(haveOut, "a point 30 mm out along the normal projects into the viewport");
        if (haveOut) {
            dragButton(view, QPointF(centreAt), QPointF(dragTo), Qt::LeftButton);
            settle(300);
            const double grown = bodyVolume() - volumeBefore;
            check(std::fabs(grown - targetArea * 30.0) < std::max(1.0, targetArea * 0.02),
                  QStringLiteral("dragging the arrow out 30 mm grows the body by "
                                 "exactly the face area x 30 (%1 vs %2)")
                      .arg(grown).arg(targetArea * 30.0));

            ToastHost* toasts = window.findChild<ToastHost*>();
            check(toasts != nullptr && toasts->isShowing() && toasts->toast() != nullptr &&
                      toasts->toast()->hasUndo(),
                  "the pull is reported through a toast that offers Undo");

            trigger(window, QStringLiteral("Undo"));
            settle(250);
            check(std::fabs(bodyVolume() - volumeBefore) < 1.0,
                  "and Undo puts the body back");
        }

        // --- inward drag carves ------------------------------------------
        clickAt(view, QPointF(centreAt));
        settle(150);
        check(!view->selectedFace().IsNull() && view->hasPullArrow(),
              "the face selects again after the undo, and the arrow comes back");
        volumeBefore = bodyVolume();
        QPoint carveTo;
        const bool haveIn =
            !target.IsNull() &&
            view->projectToScreen(targetCentre.Translated(gp_Vec(targetOutward) * -20.0),
                                  carveTo) &&
            view->rect().contains(carveTo);
        check(haveIn, "a point 20 mm in along the normal projects into the viewport");
        if (haveIn) {
            dragButton(view, QPointF(centreAt), QPointF(carveTo), Qt::LeftButton);
            settle(300);
            const double carved = volumeBefore - bodyVolume();
            check(std::fabs(carved - targetArea * 20.0) < std::max(1.0, targetArea * 0.02),
                  QStringLiteral("dragging the arrow in 20 mm carves exactly the face "
                                 "area x 20 out of it (%1 vs %2)")
                      .arg(carved).arg(targetArea * 20.0));
            trigger(window, QStringLiteral("Undo"));
            settle(250);
            check(std::fabs(bodyVolume() - volumeBefore) < 1.0,
                  "and Undo puts that back too");
        }

        // The body is back to its built size here - both drags above were
        // undone - and the two probes that follow both measure against it.
        const double steadyVolumeBase = bodyVolume();

        // --- the chip must be able to read its own writing -----------------
        // A drag writes the field and the field is what previews and commits,
        // so the text the chip writes has to survive Measure::parseLength()
        // going back the other way. It did not past a thousand:
        // formatLength() inserts a thousands separator and parseLength()'s
        // grammar has no comma in it, so the chip wrote "1,410" and then
        // refused to read it - the preview froze at the last good value and
        // Enter committed nothing. Every other drag in this file is a few
        // hundred millimetres and never crosses that boundary; this one is
        // zoomed out until a few hundred pixels really is that far.
        {
            clickAt(view, QPointF(centreAt));
            settle(150);
            const CameraState nearCamera = view->camera().state();
            CameraState farCamera = nearCamera;
            farCamera.distance = 6000.0;
            view->animateTo(farCamera);
            settle(200);

            QPoint farFrom, farTo;
            const bool haveFar =
                view->projectToScreen(targetCentre, farFrom) &&
                view->projectToScreen(
                    targetCentre.Translated(gp_Vec(targetOutward) * 1500.0), farTo) &&
                view->rect().adjusted(10, 10, -10, -10).contains(farFrom) &&
                view->rect().adjusted(10, 10, -10, -10).contains(farTo);
            check(haveFar, "zoomed out, a 1,500 mm pull projects inside the viewport");

            PullArrow* farArrow = window.findChild<PullArrow*>();
            check(farArrow != nullptr && farArrow->field() != nullptr,
                  "and the chip is up to be dragged");
            if (haveFar && farArrow && farArrow->field()) {
                // Press and move but do NOT release yet - the field has to be
                // read while the drag is live.
                const QPointF pressAt(farFrom);
                QMouseEvent down(QEvent::MouseButtonPress, pressAt, view->mapToGlobal(pressAt),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(view, &down);
                for (int i = 1; i <= 8; ++i) {
                    const QPointF p =
                        pressAt + (QPointF(farTo) - pressAt) * (double(i) / 8.0);
                    QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p),
                                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(view, &move);
                }
                settle(200);

                const QString text = farArrow->field()->text();
                double readBack = 0.0;
                const bool reads = Measure::parseLength(text.toStdString(), readBack);
                check(!text.contains(QLatin1Char(',')),
                      QStringLiteral("the chip writes a value with no thousands separator "
                                     "(\"%1\")").arg(text));
                check(reads && std::fabs(readBack - farArrow->distance()) < 1.0e-6,
                      QStringLiteral("and Measure::parseLength reads it back as exactly the "
                                     "distance the preview was built from (%1 vs %2)")
                          .arg(readBack).arg(farArrow->distance()));
                check(readBack > 1000.0,
                      QStringLiteral("and that distance really is past the thousand this "
                                     "probe exists for (%1 mm)").arg(readBack));
                check(view->hasModelingPreview(),
                      "the preview is live at that distance rather than frozen at the "
                      "last value the chip could still read");

                const QPointF releaseAt(farTo);
                QMouseEvent up(QEvent::MouseButtonRelease, releaseAt,
                               view->mapToGlobal(releaseAt), Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
                QCoreApplication::sendEvent(view, &up);
                settle(300);
                check(bodyVolume() > steadyVolumeBase * 1.5,
                      "and releasing commits that big pull for real");
                trigger(window, QStringLiteral("Undo"));
                settle(250);
                check(std::fabs(bodyVolume() - steadyVolumeBase) < 1.0,
                      "which Undo puts back like any other");
            }

            view->animateTo(nearCamera);
            settle(200);
        }

        // --- a typed carve the kernel refuses -----------------------------
        clickAt(view, QPointF(centreAt));
        settle(150);
        arrow = window.findChild<PullArrow*>();
        check(arrow != nullptr && arrow->isVisible(),
              "the arrow is up again for the typed-value probe");
        const double steadyVolume = bodyVolume();
        ToastHost* toasts = window.findChild<ToastHost*>();
        // Pinned before it guards anything. `field()` is a QPointer, and an
        // `if (arrow && arrow->field())` with nothing asserting it deletes
        // every check inside without a single red line - CLAUDE.md's
        // vacuous-probe shape, the one that went silent five at a time in
        // Phase 5. Both blocks below are `check(); if ()` for that reason.
        check(arrow != nullptr && arrow->field() != nullptr,
              "the arrow's value field exists, so the eleven checks that need it "
              "cannot vanish quietly");
        if (arrow && arrow->field()) {
            arrow->field()->setText(QStringLiteral("-999"));
            settle(250);
            check(!arrow->hasPreview(),
                  "a carve deeper than the body previews nothing");
            check(!view->hasModelingPreview(),
                  "and leaves the modeling preview channel empty");

            sendKeyTo(&window, Qt::Key_Return);
            settle(300);
            check(std::fabs(bodyVolume() - steadyVolume) < 1.0e-6,
                  "committing it leaves the body byte-for-byte untouched");
            check(toasts != nullptr && toasts->isShowing() &&
                      toasts->currentText().contains(QStringLiteral("can't be pulled")),
                  QStringLiteral("and it is reported as a failure in cause-and-fix form "
                                 "(\"%1\")")
                      .arg(toasts ? toasts->currentText() : QString()));
        }

        // --- Escape clears the dedicated preview channel -------------------
        check(arrow != nullptr && arrow->field() != nullptr,
              "and it is still there for the preview-channel and capture checks");
        if (arrow && arrow->field()) {
            arrow->field()->setText(QStringLiteral("20"));
            settle(250);
            check(view->hasModelingPreview(),
                  "a valid typed distance previews through the dedicated channel");
            check(!view->hasPreview(),
                  "and never through the sketch/extrude preview slot the two "
                  "features already collided over once");
            check(arrow->hasPreview(), "the arrow agrees that it has one");

            // Step 5's evidence, taken at the one moment all three parts of
            // the gesture are live: the 3D arrow, the value chip beside it,
            // and the preview body the typed distance would build. A magnified
            // crop of the same pixels goes beside it, framed on the arrow and
            // its chip together - the anatomy the reference image is compared
            // against.
            settle(250);
            const QImage shot =
                printWindowCapture(&window, outDir + QStringLiteral("/pull-arrow.png"));
            check(!shot.isNull(), "the mid-pull capture came back with pixels");
            // The same whole-window sweep the round/flatten capture gets.
            // This chip is 176x80 and lands on whole device pixels by luck
            // rather than by rule until now, and its POSITION follows a
            // projected 3D point - which is the half no fixed size can
            // save. Covering the class, not the instance.
            checkNoBlackLine(shot, QStringLiteral("face-pull"));
            if (!shot.isNull()) {
                // The chip and the arrow are both in the VIEWPORT's coordinate
                // space (the chip is its child; projectToScreen answers in
                // viewport pixels), so the union is translated into window
                // coordinates once, at the end.
                QPoint arrowAt;
                QRect focus(arrow->geometry());
                if (view->projectToScreen(targetCentre, arrowAt))
                    focus = focus.united(QRect(arrowAt, QSize(1, 1)));
                focus.adjust(-60, -60, 60, 60);
                focus.translate(view->mapTo(&window, QPoint(0, 0)));
                const double sx = double(shot.width()) / std::max(1, window.width());
                const double sy = double(shot.height()) / std::max(1, window.height());
                const QRect scaled(int(focus.left() * sx), int(focus.top() * sy),
                                   int(focus.width() * sx), int(focus.height() * sy));
                const QImage crop = shot.copy(scaled.intersected(shot.rect()));
                if (!crop.isNull())
                    crop.scaled(crop.width() * 3, crop.height() * 3, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation)
                        .save(outDir + QStringLiteral("/pull-arrow-crop.png"));
            }
            // The 3D half on its own, straight from V3d_View::Dump - the arrow
            // and the preview with no Qt chrome over them.
            view->saveSnapshot(outDir + QStringLiteral("/pull-arrow-viewport.png"));

            sendKeyTo(&window, Qt::Key_Escape);
            settle(250);
            check(!view->hasModelingPreview(), "Escape clears the modeling preview");
            check(std::fabs(bodyVolume() - steadyVolume) < 1.0e-6,
                  "and commits nothing on the way out");
        }

        // --- the arrow owns LMB only --------------------------------------
        {
            // Where the arrow is RIGHT NOW. The camera moves inside this
            // block, so a `centreAt` captured before it goes stale - and a
            // camera check that claims to start "on the arrow" while
            // starting somewhere else is a check that has stopped meaning
            // what it says.
            auto arrowOnScreen = [&](QPoint& out) {
                return view->projectToScreen(targetCentre, out);
            };

            // The release has to land somewhere a real pick would give a
            // DIFFERENT answer, or the probe cannot tell a swallowed
            // release-pick from one that re-picked the same face and looked
            // identical. 400 mm out along the normal is well clear of the
            // arrow (which is 52 px long) and of every body.
            QPoint emptyAt;
            const bool haveEmpty =
                view->projectToScreen(targetCentre.Translated(gp_Vec(targetOutward) * 400.0),
                                      emptyAt) &&
                view->rect().adjusted(20, 20, -20, -20).contains(emptyAt) &&
                (emptyAt - centreAt).manhattanLength() > 60;
            check(haveEmpty,
                  "a release point well clear of the arrow and of every body projects "
                  "inside the viewport");

            const double before = bodyVolume();
            pressThenReleaseAt(view, QPointF(centreAt),
                               QPointF(haveEmpty ? emptyAt : centreAt));
            settle(150);
            check(std::fabs(bodyVolume() - before) < 1.0e-6,
                  "a press on the arrow released without a drag commits nothing");
            check(!view->selectedFace().IsNull() && view->selectedFace().IsSame(target),
                  "and the viewport swallows its own release-pick - the SAME face is "
                  "still selected, though the release landed in empty space");
            check(view->hasPullArrow(),
                  "so the arrow that press grabbed is still on screen");

            // Non-vacuity for the three checks above: that point really does
            // deselect when an ordinary pick happens there. Without this the
            // release check would pass just as well against a release point
            // that happened to hit the same face again - which is exactly
            // how it passed before, aimed at the face's own centre.
            clickAt(view, QPointF(haveEmpty ? emptyAt : centreAt));
            settle(150);
            check(!haveEmpty || (view->selectedFace().IsNull() && !view->hasPullArrow()),
                  "a plain click at that same empty point DOES clear the selection and "
                  "retire the arrow - so the swallowed release was a real difference");

            clickAt(view, QPointF(centreAt));
            settle(150);
            check(!view->selectedFace().IsNull() && view->hasPullArrow(),
                  "the face selects again for the camera checks");

            QPoint at = centreAt;
            const double azimuth = view->camera().state().azimuthDeg;
            if (arrowOnScreen(at))
                dragButton(view, QPointF(at), QPointF(at + QPoint(70, 0)), Qt::RightButton);
            check(std::fabs(view->camera().state().azimuthDeg - azimuth) > 5.0,
                  "an RMB drag starting on the arrow still orbits the camera");
            const gp_Pnt panTarget = view->camera().state().target;
            if (arrowOnScreen(at))
                dragButton(view, QPointF(at), QPointF(at + QPoint(50, 30)), Qt::MiddleButton);
            check(view->camera().state().target.Distance(panTarget) > 1.0,
                  "and an MMB drag starting on it still pans");
        }

        // --- a press the drag maths cannot measure still claims the grab ---
        // Looking straight DOWN the arrow is not a corner case: it is what a
        // user gets by pressing 1 for a top view and reaching for the face in
        // front of them. axisParameterForRay() refuses inside ~1.8 degrees of
        // parallel, and the press used to claim the gesture only when it
        // resolved - so in that pose the press did nothing, the release fell
        // through to an ordinary pick, and the face the user had just grabbed
        // was silently deselected.
        {
            // Put the EYE exactly above the face centre. Then the ray through
            // the face centre's own pixel is exactly vertical - exactly
            // parallel to the arrow's axis - and the refusal is by
            // construction rather than by luck.
            //
            // Aiming the camera's target at the face and steepening the
            // elevation is NOT enough, and that is worth recording: the
            // controller clamps elevation to 88 degrees (kMaxElevation), so
            // the steepest pose reachable that way leaves the ray 2.0 degrees
            // off the axis - just outside the ~1.81 degree band
            // axisParameterForRay() refuses. The first version of this probe
            // did exactly that, resolved, and read a 1,410 mm jump out of the
            // field. The eye position is what has to be arranged, not the
            // elevation.
            CameraState edgeOn = view->camera().state();
            edgeOn.elevationDeg = 88.0;
            // Where the eye sits relative to its target at this pose, from
            // the same controller the viewport uses rather than a second copy
            // of the spherical maths.
            CameraController scratchCam;
            CameraState atOrigin = edgeOn;
            atOrigin.target = gp_Pnt(0.0, 0.0, 0.0);
            scratchCam.setState(atOrigin);
            const gp_Pnt eyeOffset = scratchCam.eyePosition();
            edgeOn.target = gp_Pnt(targetCentre.X() - eyeOffset.X(),
                                   targetCentre.Y() - eyeOffset.Y(),
                                   targetCentre.Z());
            view->animateTo(edgeOn);   // animations are off: immediate
            settle(250);
            const gp_Pnt eyeNow = view->camera().eyePosition();
            check(std::hypot(eyeNow.X() - targetCentre.X(), eyeNow.Y() - targetCentre.Y()) < 1.0e-6,
                  QStringLiteral("the eye is directly above the face centre, so the ray "
                                 "through it runs straight down the arrow (off by %1 mm)")
                      .arg(std::hypot(eyeNow.X() - targetCentre.X(),
                                      eyeNow.Y() - targetCentre.Y())));

            QPoint downTheArrow;
            const bool haveDown = view->projectToScreen(targetCentre, downTheArrow);
            check(haveDown && view->hasPullArrow(),
                  "the arrow is still up with the camera looking straight down it");

            PullArrow* edgeArrow = window.findChild<PullArrow*>();
            check(edgeArrow != nullptr && edgeArrow->field() != nullptr,
                  "and its value field is there to read the drag off");
            const QString beforeText =
                (edgeArrow && edgeArrow->field()) ? edgeArrow->field()->text() : QString();

            if (haveDown && edgeArrow && edgeArrow->field()) {
                const QPointF press(downTheArrow);
                QMouseEvent down(QEvent::MouseButtonPress, press, view->mapToGlobal(press),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(view, &down);
                settle(80);
                check(view->pullDragActive(),
                      "a press on the arrow claims the gesture even at an angle the drag "
                      "maths refuses to measure");

                // A long move, at an angle that DOES resolve. The field must
                // still read what it did - which is also the non-vacuity for
                // the check above: had the press resolved, this move would
                // have produced a large distance and rewritten the field.
                const QPointF moved = press + QPointF(0.0, -200.0);
                for (int i = 1; i <= 8; ++i) {
                    const QPointF p = press + (moved - press) * (double(i) / 8.0);
                    QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p),
                                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(view, &move);
                }
                settle(150);
                check(edgeArrow->field()->text() == beforeText,
                      QStringLiteral("and an unmeasurable press contributes nothing rather "
                                     "than jumping - the value is untouched (\"%1\")")
                          .arg(edgeArrow->field()->text()));

                QMouseEvent up(QEvent::MouseButtonRelease, moved, view->mapToGlobal(moved),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(view, &up);
                settle(200);
                check(!view->selectedFace().IsNull() &&
                          view->selectedFace().IsSame(target) && view->hasPullArrow(),
                      "and that release is swallowed too, so the face the user grabbed "
                      "is still selected");
            }
        }

        // The arrow goes the moment its predicate stops holding - one
        // function decides both directions.
        trigger(window, QStringLiteral("Select Bodies"));
        settle(200);
        check(!view->hasPullArrow(),
              "leaving face selection retires the arrow");
        PullArrow* retired = window.findChild<PullArrow*>();
        check(retired == nullptr || !retired->isVisible(),
              "and its value chip goes with it");

        view->clearSelection();
        view->animateTo(pullCameraBefore);   // animations are off: immediate
        settle(200);
    }

    // --- the transform gizmo: move, rotate and scale a whole body -------------
    // Everything the gizmo is aimed at here is derived from AIS_Manipulator's
    // OWN position and size, hovered until the widget reports which part the
    // detection actually armed. A hardcoded pixel would be a probe that
    // silently stops hitting what it meant to; guessing at the arrow lengths
    // would be worse still, because AIS_Manipulator keeps them private and is
    // free to change them.
    {
        trigger(window, QStringLiteral("Select Bodies"));
        settle(150);
        view->clearSelection();
        settle(120);

        QAction* snapAction = action(window, QStringLiteral("Snap to Grid"));
        check(snapAction != nullptr, "there is a Snap to Grid action for the gizmo probes");
        if (snapAction && !snapAction->isChecked()) { snapAction->trigger(); settle(120); }

        // A fresh box of its own, so the rotation probe below can read an
        // angle back out of axis-aligned extents. Every sub-probe undoes
        // itself, which is what keeps it axis-aligned until then.
        const int gizmoBodiesBefore = static_cast<int>(window.document().count());
        check(buildBody(window, 0.30, 0.56, 0.52, 0.70, 40.0),
              "a fresh body for the transform gizmo");
        check(static_cast<int>(window.document().count()) == gizmoBodiesBefore + 1,
              "and it reached the document");
        const int gizmoId = window.document().solids().empty()
                                ? -1
                                : window.document().solids().back().id;
        view->fitAll();
        settle(250);

        auto gizmoShape = [&window, gizmoId] { return window.document().shapeOf(gizmoId); };
        auto gizmoVolume = [&] { return ModelingOps::volume(gizmoShape()); };
        auto gizmoCentre = [&] {
            GProp_GProps props;
            BRepGProp::VolumeProperties(gizmoShape(), props);
            return props.CentreOfMass();
        };
        // The invariant the whole gesture is built around: outside an active
        // drag the body's PRESENTATION carries no transformation of its own,
        // so what is on screen IS what the document holds. A gizmo drag moves
        // the presentation and nothing else until it commits, and a volume or
        // centre-of-mass check reads the document and so cannot see a
        // viewport that stayed behind - this can.
        auto presentationIsClean = [&] {
            gp_Trsf local;
            if (!view->solidPresentationTransform(gizmoId, local)) return false;
            return ModelingOps::isIdentityTransform(local, 1.0e-6, 1.0e-4);
        };
        auto gizmoExtents = [&] {
            Bnd_Box box;
            BRepBndLib::Add(gizmoShape(), box);
            Standard_Real x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            return gp_XYZ(x1 - x0, y1 - y0, z1 - z0);
        };

        // --- the predicate: exactly one body, in body mode -----------------
        check(!view->hasManipulator(),
              "nothing selected, no gizmo");
        view->setSelectedSolids({gizmoId});
        settle(200);
        check(view->hasManipulator(),
              "selecting exactly one body raises the transform gizmo");
        check(view->manipulatorSolid() == gizmoId,
              "and it stands on that body, not another");
        check(window.canTransformSelectedBody(),
              "the window's own predicate agrees");
        // It lives outside the document, so it must never be counted as one of
        // its bodies - the suite's body-count checks would notice, and so
        // would every boolean.
        check(view->selectedSolidIds().size() == 1,
              "the gizmo is not itself pickable as a body");
        check(static_cast<int>(window.document().count()) == gizmoBodiesBefore + 1,
              "and it added nothing to the document");

        if (window.document().count() >= 2) {
            const int otherId = window.document().solids().front().id;
            view->setSelectedSolids({otherId, gizmoId});
            settle(200);
            check(!view->hasManipulator(),
                  "two bodies selected retires it - there is no one body to transform");
            view->setSelectedSolids({gizmoId});
            settle(200);
            check(view->hasManipulator(), "and one body brings it back");
        }

        trigger(window, QStringLiteral("Select Faces"));
        settle(200);
        check(!view->hasManipulator(),
              "face selection retires it, so it can never fight the pull arrow");
        trigger(window, QStringLiteral("Select Bodies"));
        view->setSelectedSolids({gizmoId});
        settle(200);
        check(view->hasManipulator(), "back in body selection it returns");

        trigger(window, QStringLiteral("Start Sketch"));
        settle(200);
        check(!view->hasManipulator(), "starting a sketch retires it");
        trigger(window, QStringLiteral("Cancel Sketch"));
        view->setSelectedSolids({gizmoId});
        settle(200);
        check(view->hasManipulator(), "cancelling the sketch brings it back");

        // A hover with no settle: MoveTo runs inside the handler, so the
        // armed mode is readable the moment the event has been delivered, and
        // a search that walked ~60 candidates through settle() would cost
        // most of a minute for nothing.
        auto hover = [view](const QPoint& at) {
            const QPointF p(at);
            QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p),
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &move);
        };

        // --- a hover and a click on EMPTY space, with the gizmo up ---------
        //
        // The one-click crash 850 green checks never went near, because every
        // one of them clicked something that was there.
        //
        // AIS_InteractiveContext::DetectedInteractive() is an inline that
        // reads myLastPicked->Selectable() with no null check of its own, and
        // a MoveTo that detects nothing nulls myLastPicked.
        // detectedIsManipulator() asked it without guarding, so selecting a
        // body - which attaches the manipulator - and then clicking empty
        // viewport to deselect took the process down. That is the most
        // ordinary gesture in the app, and it is reachable from a cold start
        // in two clicks.
        //
        // SURVIVING to the next check is the assertion for the crash itself;
        // the two after it are what say the click still did its job rather
        // than being swallowed by a guard that returns early too eagerly.
        {
            check(view->hasManipulator(),
                  "the gizmo is up before the empty-space probe, so the crashing "
                  "path is actually reachable from here");

            // "Empty" is DERIVED: the union of every body's projected bounding
            // box and the gizmo's own projected reach, stepped well clear of.
            // A hardcoded pixel is a probe that stops meaning anything the
            // moment the camera or the document moves.
            QRect occupied;
            auto swallow = [&](const gp_Pnt& world) {
                QPoint at;
                if (!view->projectToScreen(world, at)) return;
                const QRect dot(at, QSize(1, 1));
                occupied = occupied.isNull() ? dot : occupied.united(dot);
            };
            for (const DocumentModel::Solid& body : window.document().solids()) {
                Bnd_Box box;
                BRepBndLib::Add(body.shape, box);
                if (box.IsVoid()) continue;
                Standard_Real x0, y0, z0, x1, y1, z1;
                box.Get(x0, y0, z0, x1, y1, z1);
                for (int corner = 0; corner < 8; ++corner)
                    swallow(gp_Pnt(corner & 1 ? x1 : x0, corner & 2 ? y1 : y0,
                                   corner & 4 ? z1 : z0));
            }
            gp_Ax2 reach;
            double reachSize = 0.0;
            if (view->manipulatorFrame(reach, reachSize)) {
                const gp_Dir axes[3] = {reach.XDirection(), reach.YDirection(),
                                        reach.Direction()};
                for (const gp_Dir& axis : axes) {
                    swallow(reach.Location().Translated(gp_Vec(axis) * reachSize));
                    swallow(reach.Location().Translated(gp_Vec(axis) * -reachSize));
                }
            }

            // 40 px clear of all of it, and inside the viewport's own edges.
            QPoint empty(-1, -1);
            const QRect keepOut = occupied.adjusted(-40, -40, 40, 40);
            for (int y = 20; y < view->height() - 20 && empty.x() < 0; y += 12) {
                for (int x = 20; x < view->width() - 20; x += 12) {
                    if (keepOut.contains(QPoint(x, y))) continue;
                    empty = QPoint(x, y);
                    break;
                }
            }
            check(empty.x() >= 0,
                  QStringLiteral("there is a pixel with nothing behind it to click "
                                 "(bodies and gizmo occupy %1,%2 %3x%4 of %5x%6)")
                      .arg(occupied.left()).arg(occupied.top())
                      .arg(occupied.width()).arg(occupied.height())
                      .arg(view->width()).arg(view->height()));

            if (empty.x() >= 0) {
                // The bare hover first: mouseMoveEvent's hover-highlight branch
                // asks detectedIsManipulator() too, and it crashes there just
                // as readily as on the press. Two separate call sites, two
                // separate ways in.
                hover(empty);
                settle(80);
                check(view->hasManipulator(),
                      "a hover over empty space with the gizmo up is survivable, and "
                      "leaves the gizmo alone");

                clickAt(view, QPointF(empty));
                settle(200);
                check(view->selectedSolidIds().empty(),
                      QStringLiteral("clicking empty space clears the selection (%1 "
                                     "still selected)")
                          .arg(view->selectedSolidIds().size()));
                check(!view->hasManipulator(),
                      "and the gizmo goes with it, from the same predicate that "
                      "raised it");
            }

            view->setSelectedSolids({gizmoId});
            settle(200);
            check(view->hasManipulator(),
                  "the gizmo is back for the probes that follow");
        }

        // Walks out from the gizmo's centre along `along`, in fractions of its
        // own size, and stops at the first point whose hover arms `wantMode`
        // (1 Move along an axis, 2 Rotate, 3 Scale) on `wantAxis` (-1 for any).
        // Returning true IS the pick assertion this block's drags depend on: a
        // drag that starts where detection never found the manipulator is a
        // drag on nothing, and would pass every "the body did not move" check
        // for the wrong reason.
        //
        // The axis matters for the rings in particular. Detection answers in
        // SCREEN pixels, so a camera that sees one ring nearly edge-on happily
        // reports a different one under the pixel a world-space walk aimed at
        // - and then the world point the walk found is not on the ring that
        // armed, so a drag computed around it turns the body by nothing. That
        // is exactly what a 100% display did to a probe written at 150%.
        // `clearOf`, when given, rejects candidates INSIDE that box. The gizmo
        // is centred on the body's bounding box, so the inner end of every arm
        // is buried in the body - fine for a drag, useless for the Shift-click
        // probe below, which has to park a second body on the arm point and
        // then click it: a point inside the first body has that body's own
        // surface in front of it, and the click finds the wrong one.
        auto findHandle = [&](int wantMode, int wantAxis, const gp_Dir& along, QPoint& out,
                              gp_Pnt& world, const Bnd_Box* clearOf = nullptr) {
            gp_Ax2 frame;
            double size = 0.0;
            if (!view->manipulatorFrame(frame, size)) return false;
            for (int percent = 8; percent <= 140; percent += 2) {
                const gp_Pnt candidate =
                    frame.Location().Translated(gp_Vec(along) * (size * percent / 100.0));
                if (clearOf && !clearOf->IsOut(candidate)) continue;
                QPoint at;
                if (!view->projectToScreen(candidate, at)) continue;
                if (!view->rect().adjusted(6, 6, -6, -6).contains(at)) continue;
                hover(at);
                if (view->manipulatorActiveMode() != wantMode) continue;
                if (wantAxis >= 0 && view->manipulatorActiveAxis() != wantAxis) continue;
                out = at;
                world = candidate;
                return true;
            }
            return false;
        };

        // Undo, but only for a gesture that actually committed. An unguarded
        // Undo after a drag that netted nothing rewinds a checkpoint some
        // EARLIER probe took - and enough of those in a row rewind past the
        // body's own creation, at which point every later probe is reading a
        // null shape. That is not a hypothetical: it crashed this suite
        // outright (BRepGProp on a null shape throws, and an OCCT exception
        // escaping a Qt handler terminates the process), turning one legible
        // failing check into no output at all.
        auto undoIfCommitted = [&](std::size_t depthBefore) {
            if (window.document().undoDepth() > depthBefore) {
                trigger(window, QStringLiteral("Undo"));
                settle(250);
            }
            view->setSelectedSolids({gizmoId});
            settle(200);
        };

        // Read first, assert second. The condition and the message are two
        // arguments to the same call and C++ leaves their evaluation order
        // unspecified, so a check() that both fills a value and formats it
        // prints whatever the value was BEFORE the call - which is how the
        // first version of this reported a healthy gizmo as "0 mm".
        gp_Ax2 gizmoFrame;
        double gizmoSize = 0.0;
        const bool haveFrame = view->manipulatorFrame(gizmoFrame, gizmoSize);
        check(haveFrame && gizmoSize > 0.0,
              QStringLiteral("the gizmo reports its own frame and size (%1 mm)")
                  .arg(gizmoSize));

        // --- Snap on: the Z arrow moves the body by a whole grid step ------
        {
            QPoint handleAt;
            gp_Pnt handleWorld;
            const bool found =
                findHandle(1, 2, gizmoFrame.Direction(), handleAt, handleWorld);
            check(found,
                  "hovering out along the gizmo's own Z axis finds the Move handle - "
                  "detection armed it, so the drag below really starts on the gizmo");
            check(!found || view->manipulatorActiveAxis() == 2,
                  "and it is the Z arm, not another");

            QPoint dragTo;
            const bool haveTarget =
                found &&
                view->projectToScreen(handleWorld.Translated(gp_Vec(0.0, 0.0, 30.0)),
                                      dragTo) &&
                view->rect().contains(dragTo);
            check(haveTarget, "a point 30 mm up the Z axis projects into the viewport");

            if (haveTarget) {
                const double volumeBefore = gizmoVolume();
                const gp_Pnt centreBefore = gizmoCentre();
                const std::size_t depthBefore = window.document().undoDepth();

                dragButton(view, QPointF(handleAt), QPointF(dragTo), Qt::LeftButton);
                settle(300);

                const gp_Pnt centreAfter = gizmoCentre();
                const double dz = centreAfter.Z() - centreBefore.Z();
                check(std::fabs(dz - std::round(dz / 10.0) * 10.0) < 1.0e-6,
                      QStringLiteral("with Snap on the body lands on a whole 10 mm step "
                                     "(moved %1 mm)").arg(dz));
                check(dz > 1.0,
                      QStringLiteral("and it really moved, upward, rather than nowhere "
                                     "(%1 mm)").arg(dz));
                check(std::fabs(dz - 30.0) < 10.001,
                      QStringLiteral("within one step of the 30 mm dragged (%1 mm)").arg(dz));
                check(std::hypot(centreAfter.X() - centreBefore.X(),
                                 centreAfter.Y() - centreBefore.Y()) < 1.0e-6,
                      "the Z arrow moves along Z only - X and Y are untouched");
                check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                      "a move changes where a body is, never how big it is");
                check(window.document().undoDepth() == depthBefore + 1,
                      "it took exactly one undo checkpoint");

                ToastHost* toasts = window.findChild<ToastHost*>();
                check(toasts != nullptr && toasts->isShowing() &&
                          toasts->toast() != nullptr && toasts->toast()->hasUndo(),
                      "and reported it through a toast that offers Undo");
                check(toasts != nullptr &&
                          toasts->currentText().contains(QStringLiteral("moved")),
                      QStringLiteral("which names the gesture (\"%1\")")
                          .arg(toasts ? toasts->currentText() : QString()));

                check(view->hasManipulator() && view->manipulatorSolid() == gizmoId,
                      "the gizmo is still standing on the body it just moved");
                check(presentationIsClean(),
                      "and the body on screen IS the body in the document - the drag's "
                      "transform was baked into the geometry, not left sitting on the "
                      "presentation");

                undoIfCommitted(depthBefore);
                check(gizmoCentre().Distance(centreBefore) < 1.0e-6,
                      "Undo puts the body back exactly where it was");
                check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                      "at exactly the size it was");
                check(presentationIsClean(),
                      "with the presentation agreeing with the restored document too");
                check(view->hasManipulator(),
                      "and the gizmo comes back with the restored body's presentation");

                // Redo is the other half of the same invariant. It is
                // structurally safe today - resyncView() rebuilds every
                // presentation from the document - which is exactly when a
                // check costs nothing and is worth having: the day a transform
                // is ever restored by re-applying a stored trsf instead of
                // redisplaying the shape, this is what notices.
                trigger(window, QStringLiteral("Redo"));
                settle(250);
                view->setSelectedSolids({gizmoId});
                settle(200);
                check(std::fabs((gizmoCentre().Z() - centreBefore.Z()) - dz) < 1.0e-6,
                      "Redo puts the move back exactly");
                check(presentationIsClean(),
                      "and the redone body on screen IS the redone body in the document");
                trigger(window, QStringLiteral("Undo"));
                settle(250);
                view->setSelectedSolids({gizmoId});
                settle(200);
                check(gizmoCentre().Distance(centreBefore) < 1.0e-6,
                      "and Undo takes it away again for the probes that follow");
            }
        }

        // --- Snap off: the body lands where it was dragged, not on the grid -
        {
            if (snapAction && snapAction->isChecked()) { snapAction->trigger(); settle(150); }
            check(snapAction != nullptr && !snapAction->isChecked(),
                  "Snap to Grid is off for the free-move probe");

            // The handle is found FIRST, at the framing every other probe
            // uses, and only then is the camera dollied in. Order matters and
            // it cost a crash to learn: findHandle() walks the arm hovering
            // candidate pixels, and hovering a manipulator that fills much of
            // the viewport takes OCCT's own detection down with an access
            // violation - reproduced on the pre-Task-4 baseline too, so it is
            // not this branch's, but it is emphatically this probe's problem.
            // Re-projecting the handle's WORLD point at the new camera needs
            // no walk at all, and one hover at that single point is enough to
            // arm the mode and to pin that it armed.
            QPoint handleAt;
            gp_Pnt handleWorld;
            const bool found =
                findHandle(1, 2, gizmoFrame.Direction(), handleAt, handleWorld);
            check(found, "the Move handle is still findable with Snap off");

            // Frame THIS body before measuring a distance in millimetres off a
            // drag measured in pixels.
            //
            // fitAll() frames the whole document - six bodies by now - which
            // left roughly 1.8 mm to the logical pixel, so the 35 mm below
            // spanned FOURTEEN pixels and one pixel of rounding was 13% of the
            // answer. The check's 2 mm tolerance was barely over one pixel, so
            // it passed on where the rounding happened to land: 34.70 mm at
            // one display scale and 32.74 mm at another, from the same code.
            // Both are the same drag; only one of them passed.
            const CameraState framedBefore = view->camera().state();
            // Dollied until the 35 mm this drag is about to travel spans a
            // healthy number of PIXELS - measured on the projection itself,
            // pass by pass, rather than inferred from worldPerPixel(). The
            // axis is oblique to the screen, so a straight-on estimate
            // overstates the span by about a quarter and the first version of
            // this landed at 1.2 mm to the pixel while aiming for under one.
            //
            // The target is deliberately NOT moved. Re-centring on the gizmo
            // pulled the arm's projection back toward the middle of the
            // screen, where the translation PLANE handles live, and the point
            // that had armed the Z arrow came back arming a plane instead -
            // detection speaks screen pixels, which is Task 3's own lesson.
            // A pure dolly moves the arm's pixel outward, away from the
            // centre, which is the direction that keeps it on the arrow.
            //
            // Bounded at 0.6 per pass over two passes, so the gizmo can grow
            // to fill more of the viewport but not without limit: hovering a
            // manipulator that fills the viewport takes OCCT's own detection
            // down with an access violation. That crash reproduces on the
            // pre-Task-4 baseline, so it is not this branch's - but an
            // unbounded zoom here would walk straight into it.
            //
            // Each pass is KEPT only while the handle still arms the Z arrow.
            // The gizmo grows on screen as the camera comes in, and past some
            // point the translation-plane and scale parts win the pixel the
            // arm used to own - mode 4 at one scale, mode 3 at another, both
            // found by running this at scales the machine does not natively
            // use. A pass that loses the handle is rolled back and the loop
            // stops, so the probe trades precision for correctness rather than
            // silently dragging the wrong part.
            auto armsTheZArm = [&](QPoint& at) {
                if (!view->projectToScreen(handleWorld, at)) return false;
                if (!view->rect().adjusted(6, 6, -6, -6).contains(at)) return false;
                hover(at);
                return view->manipulatorActiveMode() == 1 &&
                       view->manipulatorActiveAxis() == 2;
            };
            if (found) {
                for (int pass = 0; pass < 3; ++pass) {
                    QPoint from, to;
                    if (!view->projectToScreen(handleWorld, from) ||
                        !view->projectToScreen(handleWorld.Translated(gp_Vec(0.0, 0.0, 35.0)),
                                               to))
                        break;
                    const double span = std::hypot(double(to.x() - from.x()),
                                                   double(to.y() - from.y()));
                    if (span >= 44.0) break;   // 35 mm over 44 px is under 0.8 mm/px
                    const CameraState beforePass = view->camera().state();
                    CameraState closer = beforePass;
                    closer.distance *= std::max(0.6, span / 44.0);
                    view->animateTo(closer);   // animations are off: immediate
                    settle(220);
                    QPoint probe;
                    if (!armsTheZArm(probe)) {
                        view->animateTo(beforePass);
                        settle(220);
                        break;
                    }
                }
            }

            // The handle's own world point, re-projected, and armed with one
            // hover rather than a walk.
            const bool armed = found && armsTheZArm(handleAt);
            check(armed,
                  QStringLiteral("the handle still projects into the viewport and arms "
                                 "the Z move handle after the camera moved in (mode %1, "
                                 "axis %2)")
                      .arg(view->manipulatorActiveMode())
                      .arg(view->manipulatorActiveAxis()));
            const bool reprojected = armed;

            // 35 mm, deliberately: its nearest 10 mm neighbours are 5 mm away,
            // so a result within a couple of millimetres of it cannot be
            // mistaken for a snapped one - which is the whole point here.
            QPoint dragTo;
            const bool haveTarget =
                reprojected && view->manipulatorActiveMode() == 1 &&
                view->projectToScreen(handleWorld.Translated(gp_Vec(0.0, 0.0, 35.0)),
                                      dragTo) &&
                view->rect().contains(dragTo);
            check(haveTarget, "a point 35 mm up the Z axis projects into the viewport");

            if (haveTarget) {
                const gp_Pnt centreBefore = gizmoCentre();
                const double volumeBefore = gizmoVolume();
                const std::size_t depthBefore = window.document().undoDepth();
                dragButton(view, QPointF(handleAt), QPointF(dragTo), Qt::LeftButton);
                settle(300);

                // The precision the framing above was for, asserted rather
                // than assumed: how many millimetres one logical pixel is
                // worth ALONG THIS DRAG. Under a millimetre the 2 mm
                // tolerance below is two pixels of slack; at the 2.5 mm the
                // un-framed camera gave, it was less than one, and the check
                // was deciding on rounding luck. Pinned first so a framing
                // that silently failed cannot leave the tolerance check
                // looking meaningful.
                const double dragPixels = std::hypot(double(dragTo.x() - handleAt.x()),
                                                     double(dragTo.y() - handleAt.y()));
                const double mmPerPixel = dragPixels > 0.0 ? 35.0 / dragPixels : 1.0e9;
                // Two logical pixels of slack, or 2 mm, whichever is larger:
                // both of this drag's endpoints are rounded to whole logical
                // pixels, so +/-1 px is the floor on what it can resolve and a
                // tolerance under that is a coin toss dressed as a check. The
                // framing above is what keeps this at the 2 mm floor.
                const double tolerance = std::max(2.0, 2.0 * mmPerPixel);
                // And the tolerance has to leave the off-grid check below
                // something to say: 35 mm is 5 mm from its nearest step, so a
                // pixel worth more than about two millimetres could not tell a
                // free landing from a snapped one at all. Pinned, not assumed
                // - this is the assertion that fails loudly at a scale where
                // the framing could not deliver, rather than the suite quietly
                // deciding on rounding luck.
                check(tolerance < 4.0,
                      QStringLiteral("this drag can resolve a non-grid landing at all "
                                     "(%1 mm/px over %2 px, tolerance %3 mm against a "
                                     "5 mm margin)")
                          .arg(mmPerPixel).arg(dragPixels).arg(tolerance));

                const double dz = gizmoCentre().Z() - centreBefore.Z();
                check(std::fabs(dz - 35.0) < tolerance,
                      QStringLiteral("with Snap off the body lands at the 35 mm dragged, "
                                     "not at a grid step (%1 mm)").arg(dz));
                check(std::fabs(dz - std::round(dz / 10.0) * 10.0) > 2.0,
                      QStringLiteral("and that really is off the 10 mm grid (%1 mm from "
                                     "the nearest step)")
                          .arg(std::fabs(dz - std::round(dz / 10.0) * 10.0)));
                check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                      "still the same size");

                undoIfCommitted(depthBefore);
                check(gizmoCentre().Distance(centreBefore) < 1.0e-6,
                      "and Undo puts that back too");
            }
            // Back to the framing every probe after this one inherits.
            view->animateTo(framedBefore);
            settle(200);
            view->setSelectedSolids({gizmoId});
            settle(150);
            if (snapAction && !snapAction->isChecked()) { snapAction->trigger(); settle(150); }
            check(snapAction != nullptr && snapAction->isChecked(),
                  "Snap to Grid is back on for the probes that follow");
        }

        // --- the scale cube: volume by the cube of a 5% multiple -----------
        {
            // Tried FIRST at the framing every other probe inherits, and
            // re-framed only if that finds nothing.
            //
            // The gizmo has to be the right size on screen for the walk to
            // find the scale cube, and the constraint has two sides: too big
            // and the far end of the walk projects outside the viewport,
            // where findHandle() skips every candidate; too small and the
            // cube - a little box at the arrow's tip - is a couple of pixels
            // and loses detection to the arrow and to the translation planes.
            // At a forced display scale that left this window 813x565 logical
            // the walk reported plane handles the whole way out and never a
            // cube, which is the second case.
            //
            // The re-framing is a FALLBACK rather than an unconditional step,
            // because an unconditional one broke the case it was not needed
            // for: framing "correctly" by a reach-to-viewport ratio moved a
            // camera where the walk already worked, and the cube stopped
            // arming there instead. Doing nothing when nothing is wrong is
            // the only version of this that holds at every scale.
            QPoint handleAt;
            gp_Pnt handleWorld;
            bool found = findHandle(3, 0, gizmoFrame.XDirection(), handleAt, handleWorld);
            const CameraState scaleFramedBefore = view->camera().state();
            for (int pass = 0; pass < 2 && !found; ++pass) {
                const gp_Pnt reachTip = gizmoFrame.Location().Translated(
                    gp_Vec(gizmoFrame.XDirection()) * (gizmoSize * 1.4));
                QPoint centreAt, tipAt;
                if (!view->projectToScreen(gizmoFrame.Location(), centreAt) ||
                    !view->projectToScreen(reachTip, tipAt))
                    break;
                const double reach = std::hypot(double(tipAt.x() - centreAt.x()),
                                                double(tipAt.y() - centreAt.y()));
                const double wanted = 0.25 * std::min(view->width(), view->height());
                if (reach <= 0.0 || wanted <= 0.0) break;
                CameraState framed = view->camera().state();
                // Bounded to half or double the distance it started at, so a
                // recovery can never dolly far enough to put the manipulator
                // somewhere OCCT's own detection falls over.
                framed.distance *= std::clamp(reach / wanted, 0.5, 2.0);
                view->animateTo(framed);   // animations are off: immediate
                settle(200);
                found = findHandle(3, 0, gizmoFrame.XDirection(), handleAt, handleWorld);
            }

            // What the walk actually saw, when it saw no scale cube. A probe
            // that reports only "not found" has failed twice here for two
            // different reasons - candidates off-screen at a small viewport,
            // and the cube too few pixels across to win the detection - and
            // the two are indistinguishable without this.
            QString trail;
            if (!found) {
                for (int percent = 8; percent <= 140; percent += 12) {
                    const gp_Pnt candidate = gizmoFrame.Location().Translated(
                        gp_Vec(gizmoFrame.XDirection()) * (gizmoSize * percent / 100.0));
                    QPoint at;
                    if (!view->projectToScreen(candidate, at)) { trail += QStringLiteral(" ?"); continue; }
                    if (!view->rect().adjusted(6, 6, -6, -6).contains(at)) {
                        trail += QStringLiteral(" off");
                        continue;
                    }
                    hover(at);
                    trail += QStringLiteral(" %1%%:m%2a%3")
                                 .arg(percent).arg(view->manipulatorActiveMode())
                                 .arg(view->manipulatorActiveAxis());
                }
            }
            check(found,
                  QStringLiteral("walking out along X finds the Scale handle past the "
                                 "arrow (%1)")
                      .arg(found ? QStringLiteral("armed") : trail.trimmed()));

            // Further out along the same axis: AIS_Manipulator reads a scale
            // as the ratio of the cursor's distance from the gizmo centre to
            // the handle's, so a target further out is a growth.
            QPoint dragTo;
            gp_Pnt scaleTarget;
            bool haveTarget = false;
            double growth = 0.0;
            if (found) {
                // The LARGEST growth that still lands on screen, not a fixed
                // 30%. The assertions below ask for a 5 per cent multiple and
                // for something over 1.02 - they do not care which multiple -
                // so a viewport too small to show the 30% point is a reason to
                // drag less far, not a reason to fail. It was failing at one
                // display scale for exactly that, with the handle found and
                // the gesture never attempted.
                for (const double candidate : {1.30, 1.25, 1.20, 1.15, 1.10}) {
                    const gp_Pnt at = gizmoFrame.Location().Translated(
                        gp_Vec(gizmoFrame.Location(), handleWorld) * candidate);
                    QPoint projected;
                    if (!view->projectToScreen(at, projected)) continue;
                    if (!view->rect().contains(projected)) continue;
                    scaleTarget = at;
                    dragTo = projected;
                    growth = candidate;
                    haveTarget = true;
                    break;
                }
            }
            check(haveTarget,
                  QStringLiteral("and a point further out along it projects into the "
                                 "viewport to drag to (%1)")
                      .arg(haveTarget ? QStringLiteral("x%1").arg(growth)
                                      : QStringLiteral("none of 1.30 down to 1.10")));

            if (haveTarget) {
                const double volumeBefore = gizmoVolume();
                const gp_XYZ extentsBefore = gizmoExtents();
                const std::size_t depthBefore = window.document().undoDepth();
                dragButton(view, QPointF(handleAt), QPointF(dragTo), Qt::LeftButton);
                settle(300);

                const double ratio = gizmoVolume() / std::max(1.0e-9, volumeBefore);
                const double factor = std::cbrt(ratio);
                check(factor > 1.02,
                      QStringLiteral("dragging the scale cube outward grows the body "
                                     "(x%1 on each side)").arg(factor));
                check(std::fabs(factor - std::round(factor / 0.05) * 0.05) < 1.0e-4,
                      QStringLiteral("by exactly a 5 per cent multiple with Snap on (x%1)")
                          .arg(factor));
                const gp_XYZ extentsAfter = gizmoExtents();
                check(std::fabs(extentsAfter.X() / extentsBefore.X() - factor) < 1.0e-4 &&
                          std::fabs(extentsAfter.Z() / extentsBefore.Z() - factor) < 1.0e-4,
                      QStringLiteral("and it is a UNIFORM scale - every extent by the "
                                     "same factor (%1, %2)")
                          .arg(extentsAfter.X() / extentsBefore.X())
                          .arg(extentsAfter.Z() / extentsBefore.Z()));

                ToastHost* toasts = window.findChild<ToastHost*>();
                check(toasts != nullptr &&
                          toasts->currentText().contains(QStringLiteral("scaled")),
                      QStringLiteral("the toast names this one Scale (\"%1\")")
                          .arg(toasts ? toasts->currentText() : QString()));

                undoIfCommitted(depthBefore);
                check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                      "and Undo restores the body's size exactly");
            }
            // Back to the framing every probe after this one inherits.
            view->animateTo(scaleFramedBefore);
            settle(200);
            view->setSelectedSolids({gizmoId});
            settle(150);
        }

        // --- the rotation ring: extents consistent with a snapped angle ----
        {
            // Out along the bisector of X and Y - a direction the arrows and
            // the scale cubes do not lie along, so a walk out there meets a
            // ring first. WHICH ring is not something the probe gets to
            // assume: detection answers in screen pixels, and a camera that
            // sees one ring nearly edge-on will happily report a different
            // one under the same pixel. Demanding the Z ring is how the first
            // version of this failed at a display scale it was not written on
            // - so the probe reads the ring the widget says it armed and
            // measures against THAT axis instead.
            const gp_Vec bisector =
                (gp_Vec(gizmoFrame.XDirection()) + gp_Vec(gizmoFrame.YDirection()))
                    .Normalized();
            const gp_Vec antiBisector =
                (gp_Vec(gizmoFrame.XDirection()) - gp_Vec(gizmoFrame.YDirection()))
                    .Normalized();
            // Every direction tried lies IN the XY plane, which is the Z
            // ring's own plane - so wherever the walk stops with the Z ring
            // armed, the world point really is ON that ring, and the drag
            // computed around it is a drag around the thing that armed.
            // Demanding the Z ring rather than accepting whichever one
            // answered is the whole point: a 100% display reported the Y ring
            // under a pixel a 150% one gave to the Z ring, the walk's world
            // point was nowhere near the Y ring, and the drag turned the body
            // by nothing at all. Eight directions because which arc of the
            // ring is on screen and unoccluded is the camera's business.
            const gp_Vec inPlane[] = {bisector,          bisector.Reversed(),
                                      antiBisector,      antiBisector.Reversed(),
                                      gp_Vec(gizmoFrame.XDirection()),
                                      gp_Vec(gizmoFrame.XDirection()).Reversed(),
                                      gp_Vec(gizmoFrame.YDirection()),
                                      gp_Vec(gizmoFrame.YDirection()).Reversed()};
            QPoint handleAt;
            gp_Pnt handleWorld;
            bool found = false;
            for (const gp_Vec& direction : inPlane) {
                if (found) break;
                found = findHandle(2, 2, gp_Dir(direction), handleAt, handleWorld);
            }
            // The same fallback the scale cube gets, for the same reason and
            // on the same terms: a ring too few pixels across loses the
            // detection to whatever else is under the cursor, and the walk
            // then reports nothing for a reason that has nothing to do with
            // the ring. Only entered when the walk found nothing, bounded to
            // two passes and to half or double the starting distance, and
            // restored at the end of this block.
            const CameraState ringFramedBefore = view->camera().state();
            for (int pass = 0; pass < 2 && !found; ++pass) {
                QPoint centreAt, tipAt;
                const gp_Pnt reachTip = gizmoFrame.Location().Translated(
                    gp_Vec(gizmoFrame.XDirection()) * gizmoSize);
                if (!view->projectToScreen(gizmoFrame.Location(), centreAt) ||
                    !view->projectToScreen(reachTip, tipAt))
                    break;
                const double reach = std::hypot(double(tipAt.x() - centreAt.x()),
                                                double(tipAt.y() - centreAt.y()));
                const double wanted = 0.22 * std::min(view->width(), view->height());
                if (reach <= 0.0 || wanted <= 0.0) break;
                CameraState framed = view->camera().state();
                framed.distance *= std::clamp(reach / wanted, 0.5, 2.0);
                view->animateTo(framed);   // animations are off: immediate
                settle(200);
                for (const gp_Vec& direction : inPlane) {
                    if (found) break;
                    found = findHandle(2, 2, gp_Dir(direction), handleAt, handleWorld);
                }
            }
            check(found, "walking the XY plane finds the Rotate ring about Z");
            check(!found || view->manipulatorActiveAxis() == 2,
                  "and it is the ring about Z, so the drag below turns about Z");

            // The gizmo's frame RIGHT NOW - the undos above re-attached it, and
            // Attach re-derives its position from the body's bounding box.
            gp_Ax2 ringFrame;
            double ringSize = 0.0;
            const bool haveRingFrame = view->manipulatorFrame(ringFrame, ringSize);
            const gp_Dir ringDir =
                haveRingFrame ? ringFrame.Direction() : gp_Dir(0.0, 0.0, 1.0);

            // 30 degrees around that ring, in world space, then projected.
            QPoint dragTo;
            bool haveTarget = false;
            if (found && haveRingFrame) {
                gp_Trsf spin;
                spin.SetRotation(gp_Ax1(ringFrame.Location(), ringDir),
                                 30.0 * 3.14159265358979323846 / 180.0);
                haveTarget = view->projectToScreen(handleWorld.Transformed(spin), dragTo) &&
                             view->rect().contains(dragTo);
            }
            check(haveTarget, "and a point 30 degrees round it projects inside the viewport");

            if (haveTarget) {
                const double volumeBefore = gizmoVolume();
                const TopoDS_Shape shapeBefore = gizmoShape();
                const gp_XYZ before = gizmoExtents();
                const std::size_t depthBefore = window.document().undoDepth();
                dragButton(view, QPointF(handleAt), QPointF(dragTo), Qt::LeftButton);
                settle(300);

                check(std::fabs(gizmoVolume() - volumeBefore) < volumeBefore * 1.0e-6,
                      "a rotation changes which way a body faces, never its volume");

                // The oracle is the geometry itself, not a closed form for a
                // box's footprint: a sketched quad is NOT a box. The four
                // clicks are screen-space corners of a rectangle, and a
                // perspective camera unprojects those onto the ground as a
                // trapezoid - so dx.cos a + dy.sin a describes a shape this
                // body is not, and the first version of this check duly
                // failed against a perfectly correct 30 degree turn. Turning
                // the PRE-drag shape through each candidate step and comparing
                // extents makes no assumption about its footprint at all.
                // Extents are invariant to where the rotation is centred, so
                // the pivot does not have to be reproduced here.
                const gp_XYZ after = gizmoExtents();
                check(std::fabs(after.X() - before.X()) + std::fabs(after.Y() - before.Y()) +
                              std::fabs(after.Z() - before.Z()) > 1.0,
                      QStringLiteral("the body really turned (%1 x %2 x %3 became "
                                     "%4 x %5 x %6)")
                          .arg(before.X()).arg(before.Y()).arg(before.Z())
                          .arg(after.X()).arg(after.Y()).arg(after.Z()));
                int matchedStep = 0;
                for (int step = -5; step <= 5 && matchedStep == 0; ++step) {
                    if (step == 0) continue;
                    gp_Trsf spin;
                    spin.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), ringDir),
                                     step * 15.0 * 3.14159265358979323846 / 180.0);
                    const ModelingOps::BooleanResult turned =
                        ModelingOps::transformShape(shapeBefore, spin);
                    if (!turned.ok) continue;
                    Bnd_Box probe;
                    BRepBndLib::Add(turned.shape, probe);
                    Standard_Real px0, py0, pz0, px1, py1, pz1;
                    probe.Get(px0, py0, pz0, px1, py1, pz1);
                    if (std::fabs((px1 - px0) - after.X()) < 1.0 &&
                        std::fabs((py1 - py0) - after.Y()) < 1.0 &&
                        std::fabs((pz1 - pz0) - after.Z()) < 1.0) {
                        matchedStep = step;
                    }
                }
                check(matchedStep != 0,
                      QStringLiteral("and its extents are exactly those of the same body "
                                     "turned a whole 15 degree step about the ring's own "
                                     "axis (%1 deg; %2 x %3 became %4 x %5)")
                          .arg(matchedStep * 15).arg(before.X()).arg(before.Y())
                          .arg(after.X()).arg(after.Y()));

                ToastHost* toasts = window.findChild<ToastHost*>();
                check(toasts != nullptr &&
                          toasts->currentText().contains(QStringLiteral("rotated")),
                      QStringLiteral("the toast names this one Rotate (\"%1\")")
                          .arg(toasts ? toasts->currentText() : QString()));

                undoIfCommitted(depthBefore);
                const gp_XYZ restored = gizmoExtents();
                check(std::fabs(restored.X() - before.X()) < 1.0e-6 &&
                          std::fabs(restored.Y() - before.Y()) < 1.0e-6 &&
                          std::fabs(restored.Z() - before.Z()) < 1.0e-6,
                      "and Undo turns it back exactly");
            }
            // Back to the framing every probe after this one inherits.
            view->animateTo(ringFramedBefore);
            settle(200);
            view->setSelectedSolids({gizmoId});
            settle(150);
        }

        // --- a drag that nets nothing is a cancel, not an edit --------------
        {
            QPoint handleAt;
            gp_Pnt handleWorld;
            const bool found =
                findHandle(1, 2, gizmoFrame.Direction(), handleAt, handleWorld);
            check(found, "the Move handle is findable for the no-op probe");

            if (found) {
                const std::size_t depthBefore = window.document().undoDepth();
                const int revisionBefore = window.document().revision();
                const gp_Pnt centreBefore = gizmoCentre();
                ToastHost* toasts = window.findChild<ToastHost*>();
                if (toasts) { toasts->documentMovedTo(window.document().revision() + 1); settle(150); }

                // Press, move away, and come back to exactly where it started
                // before releasing. Not a bare press-and-release: that would
                // pass against an implementation which simply never reads the
                // transform, while this one has genuinely moved the
                // presentation and has to notice it came home.
                dragButton(view, QPointF(handleAt), QPointF(handleAt + QPoint(0, -60)),
                           Qt::LeftButton, Qt::NoModifier);
                settle(120);
                dragButton(view, QPointF(handleAt), QPointF(handleAt), Qt::LeftButton);
                settle(250);

                check(window.document().undoDepth() == depthBefore + 1,
                      "the first of those two drags did commit, so the probe is not "
                      "vacuous");
                undoIfCommitted(depthBefore);

                const std::size_t depthNow = window.document().undoDepth();
                const int revisionNow = window.document().revision();
                if (toasts) { toasts->documentMovedTo(window.document().revision() + 1); settle(150); }
                dragButton(view, QPointF(handleAt), QPointF(handleAt), Qt::LeftButton);
                settle(250);
                check(window.document().undoDepth() == depthNow,
                      QStringLiteral("a drag that releases where it started takes no "
                                     "checkpoint (%1 -> %2)")
                          .arg(depthNow).arg(window.document().undoDepth()));
                check(window.document().revision() == revisionNow,
                      "and moves the document not at all");
                check(toasts == nullptr || !toasts->isShowing(),
                      "and says nothing");
                check(gizmoCentre().Distance(centreBefore) < 1.0e-6,
                      "leaving the body exactly where it was");
                check(presentationIsClean(),
                      "and the presentation back where the document says it is, rather "
                      "than stuck at the pose the cancelled drag left it in");
            }
        }

        // --- a scale nobody could have meant is refused, not clamped -------
        // The kernel only refuses a factor <= 0: it will happily build a body
        // a billionth of its size, which is a body the user has lost rather
        // than an edit they can see. This band is MainWindow's, so it is
        // asserted through MainWindow's own commit path - the same one a drag
        // reaches - rather than through a gesture that would have to be
        // dragged implausibly far to get there.
        {
            const double volumeBefore = gizmoVolume();
            const std::size_t depthBefore = window.document().undoDepth();
            gp_Trsf absurd;
            absurd.SetScale(gizmoCentre(), 50.0);
            check(!window.transformBody(gizmoId, absurd),
                  "a x50 scale is refused rather than baked");
            check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                  "and the body is left byte-for-byte as it was");
            check(window.document().undoDepth() == depthBefore,
                  "with no checkpoint taken for the change that never happened");

            ToastHost* toasts = window.findChild<ToastHost*>();
            check(toasts != nullptr && toasts->isShowing() &&
                      toasts->currentText().contains(QStringLiteral("change of size")),
                  QStringLiteral("reported as a failure in cause-and-fix form (\"%1\")")
                      .arg(toasts ? toasts->currentText() : QString()));

            gp_Trsf vanishing;
            vanishing.SetScale(gizmoCentre(), 0.01);
            check(!window.transformBody(gizmoId, vanishing),
                  "and so is a shrink to a hundredth, at the other end of the band");
            check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                  "leaving the body alone that time too");
            check(presentationIsClean(),
                  "and a refused bake leaves the viewport agreeing with the document, "
                  "never showing a pose that exists nowhere");

            // Non-vacuity: a factor INSIDE the band still commits, so the two
            // refusals above are the clamp doing its job rather than
            // transformBody refusing everything.
            gp_Trsf sensible;
            sensible.SetScale(gizmoCentre(), 1.5);
            check(window.transformBody(gizmoId, sensible),
                  "while a x1.5 scale inside the band is baked as normal");
            undoIfCommitted(depthBefore);
            check(std::fabs(gizmoVolume() - volumeBefore) < 1.0e-6,
                  "and undone again for the probes that follow");
        }

        // --- the gizmo must not fight the camera ---------------------------
        {
            QPoint handleAt;
            gp_Pnt handleWorld;
            const bool found =
                findHandle(1, 2, gizmoFrame.Direction(), handleAt, handleWorld);
            check(found, "the Move handle is findable for the camera probes");

            const double azimuth = view->camera().state().azimuthDeg;
            const gp_Pnt centreBefore = gizmoCentre();
            if (found)
                dragButton(view, QPointF(handleAt), QPointF(handleAt + QPoint(70, 0)),
                           Qt::RightButton);
            check(std::fabs(view->camera().state().azimuthDeg - azimuth) > 5.0,
                  "an RMB drag starting on a gizmo handle still orbits the camera");
            check(gizmoCentre().Distance(centreBefore) < 1.0e-6,
                  "and moves the body not at all");

            const gp_Pnt panTarget = view->camera().state().target;
            QPoint again;
            gp_Pnt againWorld;
            if (findHandle(1, 2, gizmoFrame.Direction(), again, againWorld))
                dragButton(view, QPointF(again), QPointF(again + QPoint(50, 30)),
                           Qt::MiddleButton);
            check(view->camera().state().target.Distance(panTarget) > 1.0,
                  "and an MMB drag starting on it still pans");
        }

        // --- a Shift-click must reach the body under a gizmo arm -----------
        // The regression net for the bug a 100% display found and a 150% one
        // hid: AIS_ManipulatorOwner outranks a shape's owner, so an arm
        // crossing a second body wins the pick and "add this body to the
        // selection" adds nothing at all.
        //
        // Everything here goes through the REAL mouse handler. Every other
        // multi-body check in this block uses setSelectedSolids(), which
        // bypasses the exact code the fix lives in - so without this, deleting
        // the fix leaves the suite green at this machine's own scale and the
        // bug has no net under it. The pixel is derived from the manipulator's
        // own geometry and the second body is parked on it, so the arrangement
        // holds at any display scale rather than depending on where two bodies
        // happened to fall.
        {
            // A known camera, so which arm faces the eye is decided rather
            // than inherited from whatever the camera probes above left
            // behind, and a FRESH convex body to park on it. The document's
            // first body was the obvious candidate and the wrong one: it has
            // been through the booleans further up, and the centre of mass of
            // a carved body need not lie in its own material - so parking it
            // "at" the arm point put no material there at all, and the
            // non-vacuity click found the body behind instead.
            view->setViewAxonometric();
            settle(250);
            const int helperBefore = static_cast<int>(window.document().count());
            check(buildBody(window, 0.62, 0.30, 0.76, 0.42, 30.0),
                  "a second body for the Shift-click probe");
            const int helperId = window.document().solids().empty()
                                     ? -1
                                     : window.document().solids().back().id;
            check(helperId > 0 && helperId != gizmoId,
                  "and it is a different body from the one the gizmo stands on");

            view->setSelectedSolids({gizmoId});
            settle(200);
            check(view->hasManipulator(), "the gizmo is up for the Shift-click probe");

            // Prefer the arm pointing most toward the camera, so the body
            // parked on it is the NEAREST thing along that ray. Then a failure
            // means the pick chose the manipulator over the body on selection
            // priority - which is the bug - rather than choosing something in
            // front of both on depth, which would be nobody's fault.
            const gp_Dir viewDir = view->camera().viewDirection();
            auto armDirection = [&](int axis) {
                return axis == 0   ? gizmoFrame.XDirection()
                       : axis == 1 ? gizmoFrame.YDirection()
                                   : gizmoFrame.Direction();
            };
            int order[3] = {0, 1, 2};
            for (int a = 0; a < 3; ++a) {
                for (int b = a + 1; b < 3; ++b) {
                    const double facingA =
                        -gp_Vec(armDirection(order[a])).Dot(gp_Vec(viewDir));
                    const double facingB =
                        -gp_Vec(armDirection(order[b])).Dot(gp_Vec(viewDir));
                    if (facingB > facingA) std::swap(order[a], order[b]);
                }
            }

            // Clear of the body the gizmo stands on, with room to spare, so
            // the second body can be parked on the arm point in open air.
            gp_Ax2 armFrame;
            double armSize = 1.0;
            view->manipulatorFrame(armFrame, armSize);
            Bnd_Box clearOfBody;
            BRepBndLib::Add(window.document().shapeOf(gizmoId), clearOfBody);
            clearOfBody.Enlarge(0.15 * armSize);

            QPoint armAt;
            gp_Pnt armWorld;
            int armAxis = -1;
            for (int i = 0; i < 3 && armAxis < 0; ++i) {
                if (findHandle(1, order[i], armDirection(order[i]), armAt, armWorld,
                               &clearOfBody))
                    armAxis = order[i];
            }
            check(armAxis >= 0,
                  "an arm of the gizmo is findable clear of the body it stands on");

            if (armAxis >= 0 && helperId > 0 && helperId != gizmoId) {
                // Park the second body exactly on that arm point, through the
                // same commit path a drag uses.
                GProp_GProps helperProps;
                BRepGProp::VolumeProperties(window.document().shapeOf(helperId),
                                            helperProps);
                gp_Trsf park;
                park.SetTranslation(gp_Vec(helperProps.CentreOfMass(), armWorld));
                check(window.transformBody(helperId, park),
                      "the second body can be parked on that arm");

                // Non-vacuity, and it has to come first: with no gizmo in the
                // way, a plain click at that pixel really does find the parked
                // body. Without this the Shift-click check below would pass
                // just as well against a pixel with nothing behind it.
                view->clearSelection();
                settle(200);
                check(!view->hasManipulator(),
                      "the gizmo is down for the non-vacuity click");
                clickAt(view, QPointF(armAt));
                settle(150);
                std::vector<int> got = view->selectedSolidIds();
                const bool plainFoundHelper =
                    std::find(got.begin(), got.end(), helperId) != got.end();
                check(plainFoundHelper,
                      QStringLiteral("a plain click at the arm's pixel finds the parked "
                                     "body, so that pixel really is over it (%1 selected)")
                          .arg(got.size()));

                view->setSelectedSolids({gizmoId});
                settle(200);
                check(view->hasManipulator(), "the gizmo is back on the first body");
                hover(armAt);
                check(view->manipulatorActiveMode() != 0,
                      "and one of its arms genuinely crosses that same pixel - so the "
                      "Shift-click below is aimed at the collision, not beside it");

                // The gesture itself, through the mouse handler.
                clickAt(view, QPointF(armAt), Qt::ShiftModifier);
                settle(250);
                got = view->selectedSolidIds();
                const bool haveFirst =
                    std::find(got.begin(), got.end(), gizmoId) != got.end();
                const bool haveSecond =
                    std::find(got.begin(), got.end(), helperId) != got.end();
                check(haveFirst && haveSecond,
                      QStringLiteral("a Shift-click at a pixel a gizmo arm crosses still "
                                     "adds the body underneath (%1 selected)")
                          .arg(got.size()));
                check(!view->hasManipulator(),
                      "and two bodies selected retires the gizmo, as the predicate says");
            }

            // The parked body was built for this probe, so it goes rather than
            // being moved back - the block's own "leaves the document as it
            // found it" check at the end is what this is keeping true.
            if (helperId > 0) {
                view->setSelectedSolids({helperId});
                settle(150);
                trigger(window, QStringLiteral("Delete Selected"));
                settle(200);
            }
            check(static_cast<int>(window.document().count()) == helperBefore,
                  "and the probe's own second body is cleared away after it");
            view->setSelectedSolids({gizmoId});
            settle(200);
        }

        // --- Step 5's evidence: the gizmo at the body ----------------------
        {
            view->setViewAxonometric();
            settle(250);
            view->setSelectedSolids({gizmoId});
            settle(250);
            check(view->hasManipulator(), "the gizmo is up for the capture");

            const QImage shot =
                printWindowCapture(&window, outDir + QStringLiteral("/transform-gizmo.png"));
            check(!shot.isNull(), "the gizmo capture came back with pixels");
            checkNoBlackLine(shot, QStringLiteral("transform gizmo"));
            if (!shot.isNull()) {
                gp_Ax2 frame;
                double size = 0.0;
                QPoint centreAt;
                if (view->manipulatorFrame(frame, size) &&
                    view->projectToScreen(frame.Location(), centreAt)) {
                    // A box around the whole gizmo, sized from its own reach
                    // rather than a pixel guess: the outermost ring sits about
                    // one `size` from the centre, so project that and use it.
                    QPoint tipAt;
                    int reach = 160;
                    if (view->projectToScreen(
                            frame.Location().Translated(gp_Vec(frame.Direction()) * size),
                            tipAt))
                        reach = std::max(120, (tipAt - centreAt).manhattanLength() + 60);
                    QRect focus(centreAt, QSize(1, 1));
                    focus.adjust(-reach, -reach, reach, reach);
                    focus.translate(view->mapTo(&window, QPoint(0, 0)));
                    const double sx = double(shot.width()) / std::max(1, window.width());
                    const double sy = double(shot.height()) / std::max(1, window.height());
                    const QRect scaled(int(focus.left() * sx), int(focus.top() * sy),
                                       int(focus.width() * sx), int(focus.height() * sy));
                    const QImage crop = shot.copy(scaled.intersected(shot.rect()));
                    if (!crop.isNull())
                        crop.scaled(crop.width() * 3, crop.height() * 3,
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            .save(outDir + QStringLiteral("/transform-gizmo-crop.png"));
                }
            }
            view->saveSnapshot(outDir + QStringLiteral("/transform-gizmo-viewport.png"));
        }

        // The gizmo goes the moment its predicate stops holding.
        view->clearSelection();
        settle(200);
        check(!view->hasManipulator(),
              "clearing the selection retires the gizmo");
        check(view->manipulatorSolid() == -1,
              "and it lets go of the body it was standing on");

        // Leave the document as this block found it, so every later probe's
        // body counts still add up.
        view->setSelectedSolids({gizmoId});
        settle(150);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(200);
        check(static_cast<int>(window.document().count()) == gizmoBodiesBefore,
              "the gizmo probe leaves the document as it found it");
        view->clearSelection();
        settle(150);
    }

    // --- one edge, one axis, two operations -----------------------------------
    // Select an edge, drag one way to round it and the other way to flatten it.
    // Everything is derived from projected geometry, and both outcomes are
    // asserted as EXACT volume deltas from the closed forms a fillet and a
    // chamfer of a straight edge have - (1 - pi/4)r^2 L and d^2 L / 2 - rather
    // than "something got smaller", which both operations would satisfy in
    // either direction.
    {
        const CameraState bevelCameraBefore = view->camera().state();
        QAction* snap = action(window, QStringLiteral("Snap to Grid"));
        check(snap != nullptr, "there is a Snap to Grid action for the bevel probes");
        if (snap && !snap->isChecked()) { snap->trigger(); settle(120); }

        const int bevelBodiesBefore = static_cast<int>(window.document().count());
        check(buildBody(window, 0.34, 0.30, 0.72, 0.62, 40.0),
              "a fresh box to round and flatten an edge of");
        check(static_cast<int>(window.document().count()) == bevelBodiesBefore + 1,
              "and it reached the document");
        const int bevelId = window.document().solids().empty()
                                ? -1
                                : window.document().solids().back().id;
        view->fitAll();
        settle(250);
        trigger(window, QStringLiteral("Select Edges"));
        settle(150);

        auto bodyShape = [&window, bevelId] { return window.document().shapeOf(bevelId); };
        auto bodyVolume = [&] { return ModelingOps::volume(bodyShape()); };
        auto faceCount = [&] {
            int faces = 0;
            for (TopExp_Explorer it(bodyShape(), TopAbs_FACE); it.More(); it.Next()) ++faces;
            return faces;
        };
        // Press and move without releasing: every assertion about the kind, the
        // label and the live preview has to be made while the gesture is still
        // live, because committing it retires the chip that carries them.
        auto pressAndDrag = [&](const QPoint& from, const QPoint& to) {
            const QPointF start(from);
            QMouseEvent down(QEvent::MouseButtonPress, start, view->mapToGlobal(start),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &down);
            for (int i = 1; i <= 8; ++i) {
                const QPointF p = start + (QPointF(to) - start) * (double(i) / 8.0);
                QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(view, &move);
            }
            settle(200);
        };
        auto releaseAt = [&](const QPoint& at) {
            const QPointF p(at);
            QMouseEvent up(QEvent::MouseButtonRelease, p, view->mapToGlobal(p),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &up);
            settle(300);
        };

        // The dragged size, in millimetres. 20 is a multiple of the 10 mm snap
        // step (so the snap cannot round the assertion out from under it) and
        // is comfortably inside the 40 mm the box is thick, which is what the
        // neighbouring side face has to give up.
        constexpr double kSize = 20.0;

        // A LONG, TOP edge: the vertical ones are only as tall as the box, and
        // the whole point of a bevel is that it runs along something. The axis
        // is asked of ModelingOps::bevelAxis itself, so this probe aims at the
        // gizmo's own geometry rather than guessing where the arrow will be.
        TopoDS_Edge target;
        QPoint edgeAt;
        QPoint inwardAt;
        QPoint outwardAt;
        gp_Pnt edgeCentre;
        gp_Dir edgeOutward;
        double edgeLength = 0.0;
        for (TopExp_Explorer it(bodyShape(), TopAbs_EDGE); it.More() && target.IsNull();
             it.Next()) {
            const TopoDS_Edge candidate = TopoDS::Edge(it.Current());
            gp_Pnt centre;
            gp_Dir outward;
            if (!ModelingOps::bevelAxis(bodyShape(), candidate, centre, outward)) continue;
            if (outward.Z() < 0.3) continue;   // a top edge, so both its faces are reachable

            const double facing =
                gp_Vec(outward).Dot(gp_Vec(view->camera().viewDirection()));
            if (facing >= -0.25) continue;   // turned away from the camera
            if (facing < -0.95) continue;    // looking down the arrow: unmeasurable

            TopoDS_Vertex v1, v2;
            TopExp::Vertices(candidate, v1, v2);
            if (v1.IsNull() || v2.IsNull()) continue;
            const double length = BRep_Tool::Pnt(v1).Distance(BRep_Tool::Pnt(v2));
            if (length < 100.0) continue;

            QPoint at, inAt, outAt;
            if (!view->projectToScreen(centre, at)) continue;
            if (!view->projectToScreen(centre.Translated(gp_Vec(outward) * -kSize), inAt))
                continue;
            if (!view->projectToScreen(centre.Translated(gp_Vec(outward) * kSize), outAt))
                continue;
            if (!view->rect().adjusted(60, 60, -60, -60).contains(at)) continue;
            if (!view->rect().contains(inAt) || !view->rect().contains(outAt)) continue;

            clickAt(view, QPointF(at));
            settle(150);
            const TopoDS_Edge got = view->selectedEdge();
            if (got.IsNull() || !got.IsSame(candidate)) continue;   // occluded, or missed
            target = candidate;
            edgeAt = at;
            inwardAt = inAt;
            outwardAt = outAt;
            edgeCentre = centre;
            edgeOutward = outward;
            edgeLength = length;
        }
        check(!target.IsNull(),
              "clicking a projected edge midpoint selects a long top edge of the new body");

        // --- dolly until the 20 mm drag spans enough PIXELS to aim with ----
        //
        // The transform gizmo's probe already invented this (see the Move
        // handle's framing loop) and the bevel drags needed it just as badly.
        // fitAll() frames the whole document, which left about 1.8 mm to the
        // logical pixel at 100% - so the 20 mm this gesture drags spanned only
        // eleven pixels, and at 125% it spanned nine. The press point and the
        // release point are both rounded to whole pixels and the axis is
        // oblique to the screen, so the error those two roundings put into the
        // parameter along the arrow is a large fraction of a 10 mm snap step:
        // measured, the flattening drag at 125% snapped to 30 mm instead of
        // 20 and the released volume then missed its closed form by more than
        // 2x. Both were parked as "display-scale sensitivity"; neither was the
        // product's.
        //
        // A pure dolly, exactly as the gizmo probe does it, and for the same
        // reason: moving the camera TARGET would slide the edge's projection
        // across the screen and the pick could land on a different edge.
        //
        // Each pass is kept only while all three drag points still project
        // comfortably inside the viewport AND the edge still selects at the
        // new pixel - a pass that loses either is rolled back and the loop
        // stops, so this trades precision for correctness rather than zooming
        // until the probe aims at nothing. bevelCameraBefore is restored at
        // the end of this block, so nothing after it inherits the framing.
        // A pass is kept only while all three drag points still project inside
        // the viewport - a probe that zooms until it is aiming at nothing has
        // traded correctness for precision, which is the wrong way round.
        auto projectDrag = [&](QPoint& at, QPoint& inAt, QPoint& outAt) {
            return view->projectToScreen(edgeCentre, at) &&
                   view->projectToScreen(edgeCentre.Translated(gp_Vec(edgeOutward) * -kSize),
                                         inAt) &&
                   view->projectToScreen(edgeCentre.Translated(gp_Vec(edgeOutward) * kSize),
                                         outAt) &&
                   view->rect().adjusted(30, 30, -30, -30).contains(at) &&
                   view->rect().contains(inAt) && view->rect().contains(outAt);
        };
        // Called before EACH of the two drags, not once: the curved-edge probe
        // between them re-frames the whole document on purpose (its arcs live
        // at the far ends of a 1,100 mm edge and are off screen at any framing
        // tight enough to aim a 20 mm drag), so the second drag's three points
        // would otherwise be stale.
        //
        // Returns the pixel span the 20 mm drag ends up with, and how it
        // stopped, so the check that reads it can say which.
        auto aimTheDrag = [&](QString& why) {
            // 20 mm over 32 px is under 0.7 mm to the pixel: the two endpoint
            // roundings, amplified by however oblique the arrow is to the
            // screen, then stay well inside half a 10 mm snap step.
            constexpr double kWantedSpan = 32.0;
            why = QStringLiteral("already wide enough");
            for (int pass = 0; pass < 5; ++pass) {
                QPoint at, inAt, outAt;
                if (!projectDrag(at, inAt, outAt)) {
                    why = QStringLiteral("a drag point left the viewport");
                    break;
                }
                const double span = std::hypot(double(outAt.x() - at.x()),
                                               double(outAt.y() - at.y()));
                if (span >= kWantedSpan) break;
                const CameraState beforePass = view->camera().state();
                CameraState closer = beforePass;
                // Bounded per pass, the same 0.6 the gizmo probe uses, so the
                // camera walks in rather than jumping inside the body.
                closer.distance *= std::max(0.6, span / kWantedSpan);
                view->animateTo(closer);   // animations are off: immediate
                settle(180);
                QPoint a2, i2, o2;
                if (!projectDrag(a2, i2, o2)) {
                    view->animateTo(beforePass);
                    settle(180);
                    why = QStringLiteral("one pass further pushed a drag point off screen");
                    break;
                }
                why = QStringLiteral("dollied in");
            }
            // Re-projected at whatever framing the loop settled on - the three
            // points found before it are stale the moment the camera moves.
            projectDrag(edgeAt, inwardAt, outwardAt);
            return std::hypot(double(outwardAt.x() - edgeAt.x()),
                              double(outwardAt.y() - edgeAt.y()));
        };

        if (!target.IsNull()) {
            QString why;
            const double dragSpan = aimTheDrag(why);
            // The selection is re-taken at the new pixel, with the previous one
            // cleared first: with the arrow up, a press within 14 px of its
            // shaft begins a DRAG rather than a pick (OcctViewWidget::arrowHit),
            // and edgeAt is the arrow's own tail - so a click there would
            // validate nothing at all.
            view->clearSelection();
            settle(150);
            clickAt(view, QPointF(edgeAt));
            settle(150);
            check(!view->selectedEdge().IsNull() && view->selectedEdge().IsSame(target),
                  "the same edge still selects at the framing the drags are aimed from");
            // The precision the framing was for, asserted rather than assumed -
            // and the check that would have shown the two 125% failures as an
            // aiming problem instead of leaving them looking like product bugs.
            check(dragSpan >= 16.0,
                  QStringLiteral("and the %1 mm drag spans enough pixels to aim with "
                                 "(%2 px, %3 mm per pixel, %4)")
                      .arg(kSize).arg(dragSpan)
                      .arg(dragSpan > 0.0 ? kSize / dragSpan : 0.0)
                      .arg(why));
        }

        // The sign convention's GROUND TRUTH, and not optional. Every other
        // check in this block measures the drag against the very axis
        // ModelingOps::bevelAxis hands back, so a bisector built from un-flipped
        // REVERSED normals - which points INTO the body, the Phase 4 lesson -
        // would send the gesture the opposite physical way and every one of
        // them would still pass, consistently wrong. BRepClass3d_SolidClassifier
        // is the same independent oracle Phase 4 used to pin lockToFace's flip:
        // a step ALONG the axis has to leave the body and a step against it has
        // to stay inside it.
        if (!target.IsNull()) {
            BRepClass3d_SolidClassifier classifier(bodyShape());
            classifier.Perform(edgeCentre.Translated(gp_Vec(edgeOutward) * 2.0), 1.0e-7);
            const bool leavesTheBody = classifier.State() == TopAbs_OUT;
            classifier.Perform(edgeCentre.Translated(gp_Vec(edgeOutward) * -2.0), 1.0e-7);
            const bool entersTheBody = classifier.State() == TopAbs_IN;
            check(leavesTheBody && entersTheBody,
                  QStringLiteral("the drag axis really points out of the body, so "
                                 "\"inward rounds\" means inward (out: %1, in: %2)")
                      .arg(leavesTheBody).arg(entersTheBody));
        }

        BevelArrow* bevel = window.findChild<BevelArrow*>();
        check(bevel != nullptr, "the window has a round/flatten gizmo");
        check(bevel != nullptr && bevel->isVisible(),
              "one straight edge selected raises it");
        check(view->hasBevelArrow(),
              "and the arrow itself is drawn in the 3D scene, not painted over it");
        // The table's words, not "round or flatten": one operation, one name,
        // across the chip, the tooltips, this label and both refusals.
        check(stateLabelText(window).contains(
                  QStringLiteral("drag in for a Fillet, out for a Chamfer")),
              QStringLiteral("the state label teaches the gesture in the operations' "
                             "own names (\"%1\")")
                  .arg(stateLabelText(window)));
        check(!view->dimension().isShowing() && view->edgeDimensionSuppressed(),
              "and the edge-length annotation stands down while it is up");

        // The value chip's field is a real, reachable control. childAt
        // identity, not an attribute flag - CLAUDE.md's rule, learned twice.
        // Pinned non-null first, so the checks guarded by it cannot vanish
        // quietly.
        check(bevel != nullptr && bevel->field() != nullptr,
              "the gizmo and its value field exist before the reachability probe");
        if (bevel && bevel->field()) {
            check(view->childAt(bevel->field()->geometry().center()) == bevel->field(),
                  "a real click at the value field's centre finds the field itself");
            // The field is where the two operations are TAUGHT, and the
            // tooltip follows the kind rather than describing both - a
            // tooltip that named the operation the chip is not about to
            // perform would be worse than none. Checked on both sides: the
            // flattening half is asserted after the outward drag below.
            check(bevel->field()->toolTip().contains(QStringLiteral("rounds the edge")),
                  QStringLiteral("and the field teaches what the rounding half does "
                                 "(\"%1\")").arg(bevel->field()->toolTip()));
            // The em dash, by codepoint. This file is UTF-8 with no BOM, the
            // way every other source here is, and the first version of it
            // carried a BOM and a double-encoded dash that compiled, passed
            // the banned-word sweep, and rendered as mojibake. A separator
            // nothing asserts is a separator nothing notices.
            check(bevel->field()->toolTip().contains(QChar(0x2014)),
                  "joined by a real em dash rather than a mis-encoded one");
        }

        const double startVolume = bodyVolume();
        const int startFaces = faceCount();

        // --- inward rounds ------------------------------------------------
        if (bevel && !target.IsNull()) {
            pressAndDrag(edgeAt, inwardAt);
            check(bevel->isFillet(),
                  "dragging INTO the body, against the bisector, chooses the rounding half");
            check(bevel->valueText().startsWith(QLatin1Char('R')),
                  QStringLiteral("so the value reads as a radius (\"%1\")")
                      .arg(bevel->valueText()));
            check(bevel->kindText() == QStringLiteral("Fillet"),
                  QStringLiteral("and the chip names the operation in words (\"%1\")")
                      .arg(bevel->kindText()));
            check(std::fabs(bevel->size() - kSize) < 1.0e-6,
                  QStringLiteral("the size snapped to the 10 mm step at exactly the "
                                 "dragged distance (%1 vs %2)")
                      .arg(bevel->size()).arg(kSize));
            check(view->hasModelingPreview(),
                  "a live preview is up on the dedicated channel");
            check(!view->hasPreview(),
                  "and never on the sketch/extrude slot two features already collided over");

            // Step 5's evidence for the rounding half, taken at the one moment
            // the arrow, the chip and the preview are all live.
            const QImage roundShot =
                printWindowCapture(&window, outDir + QStringLiteral("/bevel-fillet.png"));
            check(!roundShot.isNull(), "the mid-drag rounding capture came back with pixels");

            // --- measured, not eyeballed --------------------------------
            // The whole composited window swept for a black run, taken at the
            // one moment three overlay widgets, an in-scene arrow and a live
            // preview are all up over the GL surface.
            //
            // It really was here, and only a measurement found it: the first
            // capture of this gesture carried a 264-device-pixel 0,0,0
            // hairline along the value chip's bottom edge - the row Qt
            // flushes for a card 93 logical rows tall at 150% scaling and the
            // widget's own logical clip stops it from painting. Both
            // magnified crops read as clean to the eye. This phase's worst
            // finding was a commit message claiming a crop confirmed a 3px
            // gap that measured 12, so the crop is evidence and the sweep is
            // the check - and it now runs on every gizmo capture, not only
            // the one that found it.
            checkNoBlackLine(roundShot, QStringLiteral("rounding"));

            if (!roundShot.isNull()) {
                QRect focus(bevel->geometry());
                focus = focus.united(QRect(edgeAt, QSize(1, 1)));
                focus.adjust(-70, -70, 70, 70);
                focus.translate(view->mapTo(&window, QPoint(0, 0)));
                const double sx = double(roundShot.width()) / std::max(1, window.width());
                const double sy = double(roundShot.height()) / std::max(1, window.height());
                const QRect scaled(int(focus.left() * sx), int(focus.top() * sy),
                                   int(focus.width() * sx), int(focus.height() * sy));
                const QImage crop = roundShot.copy(scaled.intersected(roundShot.rect()));
                if (!crop.isNull())
                    crop.scaled(crop.width() * 3, crop.height() * 3, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation)
                        .save(outDir + QStringLiteral("/bevel-fillet-crop.png"));
            }
            view->saveSnapshot(outDir + QStringLiteral("/bevel-fillet-viewport.png"));

            // The radius the chip is about to commit, read back rather than
            // assumed from kSize - see the same readback on the flattening
            // side for why a closed form must be evaluated at the size that
            // was actually committed.
            const double committedRadius = bevel->size();
            releaseAt(inwardAt);
            const double removed = startVolume - bodyVolume();
            constexpr double kPi = 3.14159265358979323846;
            const double expected =
                (1.0 - kPi / 4.0) * committedRadius * committedRadius * edgeLength;
            check(std::fabs(removed - expected) < std::max(1.0, expected * 0.02),
                  QStringLiteral("releasing rounds it by exactly (1 - pi/4) r^2 L "
                                 "(%1 vs %2 at r = %3 mm over a %4 mm edge)")
                      .arg(removed).arg(expected).arg(committedRadius).arg(edgeLength));
            check(faceCount() > startFaces,
                  QStringLiteral("and the result carries the rounded strip as a new face "
                                 "(%1 -> %2)").arg(startFaces).arg(faceCount()));

            ToastHost* toasts = window.findChild<ToastHost*>();
            check(toasts != nullptr && toasts->isShowing() && toasts->toast() != nullptr &&
                      toasts->toast()->hasUndo(),
                  "it is reported through a toast that offers Undo");

            // --- the Phase 4 annotation rules, on a CURVED edge -----------
            // Moved here from the edge-dimension block: selecting a STRAIGHT
            // edge now raises this gizmo instead of an annotation, so the
            // "follows the selection, survives the cursor leaving, never
            // outlives its body" rules are checked on the one kind of edge
            // that still has no gizmo - which is also the check that the
            // arrow's straight-only rule is real.
            //
            // Re-framed on the whole document first, and deliberately: the
            // only curved edges a filleted box has are the two end arcs of the
            // rounded strip, which sit at the far ends of an 1,100 mm edge and
            // are off screen at any framing tight enough to aim a 20 mm drag
            // (see aimTheDrag). The drags re-aim themselves, so widening here
            // costs them nothing.
            view->fitAll();
            settle(250);
            TopoDS_Edge arc;
            for (TopExp_Explorer it(bodyShape(), TopAbs_EDGE); it.More() && arc.IsNull();
                 it.Next()) {
                const TopoDS_Edge candidate = TopoDS::Edge(it.Current());
                BRepAdaptor_Curve curve(candidate);
                if (curve.GetType() == GeomAbs_Line) continue;
                const gp_Pnt mid = curve.Value(
                    0.5 * (curve.FirstParameter() + curve.LastParameter()));
                QPoint at;
                if (!view->projectToScreen(mid, at)) continue;
                if (!view->rect().adjusted(30, 30, -30, -30).contains(at)) continue;
                clickAt(view, QPointF(at));
                settle(140);
                const TopoDS_Edge got = view->selectedEdge();
                if (got.IsNull() || !got.IsSame(candidate)) continue;
                arc = got;
            }
            check(!arc.IsNull(),
                  "one of the rounded body's curved edges can be selected");
            if (!arc.IsNull()) {
                check(!view->hasBevelArrow(),
                      "a curved edge raises no arrow - one drag axis needs one "
                      "perpendicular, and a bending edge has none");
                check(view->dimension().isShowing(),
                      "so its length annotation is the one label on it, and SELECTING it "
                      "is what put it there");
                TopoDS_Vertex a1, a2;
                TopExp::Vertices(arc, a1, a2);
                const std::string arcExpected =
                    Measure::formatLength(BRep_Tool::Pnt(a1).Distance(BRep_Tool::Pnt(a2)));
                check(view->dimension().labelText() == arcExpected,
                      QStringLiteral("reading the true span between its ends (\"%1\" "
                                     "against \"%2\")")
                          .arg(QString::fromStdString(view->dimension().labelText()),
                               QString::fromStdString(arcExpected)));
                moveTo(view, QPointF(8, 8));
                check(view->dimension().isShowing(),
                      "and it survives the cursor moving off, because the edge is still "
                      "selected - Phase 4's rule, still enforced");
                check(view->dimension().labelText() == arcExpected,
                      QStringLiteral("still reading that edge's own span rather than "
                                     "some other edge's (\"%1\")")
                          .arg(QString::fromStdString(view->dimension().labelText())));

                const std::size_t bodiesHere = window.document().count();
                trigger(window, QStringLiteral("Delete Selected"));
                settle(200);
                check(window.document().count() == bodiesHere - 1,
                      "deleting the body the annotation measures removes it");
                check(!view->dimension().isShowing(),
                      "and takes the annotation with it rather than leaving it in "
                      "empty space");
                trigger(window, QStringLiteral("Undo"));
                settle(250);
                check(window.document().count() == bodiesHere,
                      "and Undo brings the rounded body back");
            }

            trigger(window, QStringLiteral("Undo"));
            settle(250);
            check(std::fabs(bodyVolume() - startVolume) < 1.0 && faceCount() == startFaces,
                  "Undo puts the sharp edge back, faces and all");
        }

        // --- outward flattens ---------------------------------------------
        if (bevel && !target.IsNull()) {
            // Re-aimed, because the curved-edge probe above widened the
            // framing back out to the whole document on purpose. Done with
            // nothing selected, so the click below is a real pick.
            view->clearSelection();
            settle(150);
            QString whyOut;
            const double outSpan = aimTheDrag(whyOut);
            clickAt(view, QPointF(edgeAt));
            settle(150);
            check(!view->selectedEdge().IsNull() && view->selectedEdge().IsSame(target) &&
                      view->hasBevelArrow(),
                  "the same edge selects again after the undo, and the arrow comes back");
            check(outSpan >= 16.0,
                  QStringLiteral("and the flattening drag is aimed just as widely "
                                 "(%1 px, %2 mm per pixel, %3)")
                      .arg(outSpan).arg(outSpan > 0.0 ? kSize / outSpan : 0.0).arg(whyOut));

            pressAndDrag(edgeAt, outwardAt);
            check(!bevel->isFillet(),
                  "dragging OUT along the bisector chooses the flattening half instead");
            check(bevel->valueText().startsWith(QLatin1Char('C')),
                  QStringLiteral("so the value reads as a chamfer size (\"%1\")")
                      .arg(bevel->valueText()));
            check(bevel->kindText() == QStringLiteral("Chamfer"),
                  QStringLiteral("and the chip renames itself in words (\"%1\")")
                      .arg(bevel->kindText()));
            check(std::fabs(bevel->size() - kSize) < 1.0e-6,
                  QStringLiteral("at the same snapped size as the drag the other way "
                                 "(%1)").arg(bevel->size()));
            // The other half of the tooltip rule pinned above: it follows the
            // kind, so crossing zero has to have rewritten it.
            check(bevel->field() != nullptr &&
                      bevel->field()->toolTip().contains(QStringLiteral("flattens the edge")),
                  QStringLiteral("and the field now teaches flattening instead (\"%1\")")
                      .arg(bevel->field() ? bevel->field()->toolTip() : QString()));
            // Its em dash by codepoint too, and not because the fillet one was
            // checked: they are two separate literals in the source, each
            // separately capable of being mis-encoded, and only one of them
            // was on screen when the mojibake was found.
            check(bevel->field() != nullptr &&
                      bevel->field()->toolTip().contains(QChar(0x2014)),
                  "joined by a real em dash on this side as well");

            const QImage flatShot =
                printWindowCapture(&window, outDir + QStringLiteral("/bevel-chamfer.png"));
            check(!flatShot.isNull(), "the mid-drag flattening capture came back with pixels");
            checkNoBlackLine(flatShot, QStringLiteral("flattening"));
            if (!flatShot.isNull()) {
                QRect focus(bevel->geometry());
                focus = focus.united(QRect(edgeAt, QSize(1, 1)));
                focus.adjust(-70, -70, 70, 70);
                focus.translate(view->mapTo(&window, QPoint(0, 0)));
                const double sx = double(flatShot.width()) / std::max(1, window.width());
                const double sy = double(flatShot.height()) / std::max(1, window.height());
                const QRect scaled(int(focus.left() * sx), int(focus.top() * sy),
                                   int(focus.width() * sx), int(focus.height() * sy));
                const QImage crop = flatShot.copy(scaled.intersected(flatShot.rect()));
                if (!crop.isNull())
                    crop.scaled(crop.width() * 3, crop.height() * 3, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation)
                        .save(outDir + QStringLiteral("/bevel-chamfer-crop.png"));
            }
            view->saveSnapshot(outDir + QStringLiteral("/bevel-chamfer-viewport.png"));

            // The size the CHIP is about to commit, read back before the
            // release, not the constant the drag aimed at. The two agree - the
            // check above says so - and when they ever stop agreeing this
            // should report the snap that missed, once, rather than reporting
            // it a second time as a closed form that does not hold. Computing
            // `expected` from kSize turned one failure into two, the second of
            // which reads as "the formula for a chamfer is wrong".
            const double committedSize = bevel->size();
            releaseAt(outwardAt);
            const double removed = startVolume - bodyVolume();
            const double expected = 0.5 * committedSize * committedSize * edgeLength;
            check(std::fabs(removed - expected) < std::max(1.0, expected * 0.02),
                  QStringLiteral("releasing flattens it by exactly d^2 L / 2 - a bigger "
                                 "bite than the same drag inward took (%1 vs %2 at "
                                 "d = %3 mm)")
                      .arg(removed).arg(expected).arg(committedSize));
            trigger(window, QStringLiteral("Undo"));
            settle(250);
            check(std::fabs(bodyVolume() - startVolume) < 1.0 && faceCount() == startFaces,
                  "which Undo puts back like any other");
        }

        // --- a typed size the kernel refuses ------------------------------
        clickAt(view, QPointF(edgeAt));
        settle(150);
        bevel = window.findChild<BevelArrow*>();
        check(bevel != nullptr && bevel->isVisible() && bevel->field() != nullptr,
              "the gizmo is up again with its field, so the six checks below cannot "
              "vanish quietly");
        ToastHost* toasts = window.findChild<ToastHost*>();
        if (bevel && bevel->isVisible() && bevel->field()) {
            const double steadyVolume = bodyVolume();
            const int steadyFaces = faceCount();
            // Far larger than the box is thick: the rounded strip would have to
            // eat the whole side face and then some. OCCT refuses this, and
            // refusing it is the contract.
            bevel->field()->setText(QStringLiteral("999"));
            settle(300);
            check(!bevel->hasPreview(),
                  "a radius the body cannot take previews nothing");
            check(!view->hasModelingPreview(),
                  "and leaves the last good preview - here, none - exactly as it was");

            sendKeyTo(&window, Qt::Key_Return);
            settle(300);
            check(std::fabs(bodyVolume() - steadyVolume) < 1.0e-6 &&
                      faceCount() == steadyFaces,
                  "committing it leaves the body untouched, volume and faces both");
            // The WHOLE sentence, character for character, not a substring of
            // it. The brief fixes this wording exactly, and a `contains` on
            // the memorable clause passes just as well over a sentence whose
            // fix half has been dropped, whose em dash has decayed, or which
            // has grown an apology in front of the cause.
            // No trailing period: the app's failure sentences end without one
            // (the pull's and the transform's always did) and this pair was
            // the exception. Pinned character for character here, which is why
            // dropping it is a test change as well as a copy change.
            const QString expectedFailure = QStringLiteral(
                "This edge can't take a fillet that big \xE2\x80\x94 the curve would eat "
                "a neighbouring face. Try a smaller size");
            check(toasts != nullptr && toasts->isShowing() &&
                      toasts->currentText() == expectedFailure,
                  QStringLiteral("and it is reported as a failure in the exact "
                                 "cause-and-fix sentence (\"%1\")")
                      .arg(toasts ? toasts->currentText() : QString()));

            // --- Escape clears the dedicated preview channel --------------
            bevel->field()->setText(QStringLiteral("10"));
            settle(300);
            check(view->hasModelingPreview() && bevel->hasPreview(),
                  "a size the body can take previews through the dedicated channel");
            sendKeyTo(&window, Qt::Key_Escape);
            settle(250);
            check(!view->hasModelingPreview(), "Escape clears the modeling preview");
            check(std::fabs(bodyVolume() - steadyVolume) < 1.0e-6,
                  "and commits nothing on the way out");
            check(view->hasBevelArrow(),
                  "leaving the arrow and the selection alone, so the user can simply "
                  "drag again");

            // --- Escape with the focus somewhere else entirely ------------
            // The defect class CLAUDE.md documents on ExtrudePreview: a filter
            // installed on the FIELD stops working the moment anything else
            // takes focus, and an orbit - the entire reason a live preview
            // exists - is a press in the viewport, which does exactly that.
            // The check above cannot see it, because sendKeyTo goes to the
            // window and the field still held focus. This moves focus to a
            // real control the user can click and then presses Escape.
            bevel->field()->setText(QStringLiteral("10"));
            settle(300);
            check(view->hasModelingPreview(),
                  "a preview is up again for the focus-elsewhere probe");
            QWidget* elsewhere = nullptr;
            for (ToolChip* chip : window.findChildren<ToolChip*>()) {
                if (chip->isVisible() && chip->isEnabled()) { elsewhere = chip; break; }
            }
            check(elsewhere != nullptr,
                  "there is a visible rail control to move the focus onto");
            if (elsewhere) {
                elsewhere->setFocus(Qt::MouseFocusReason);
                settle(120);
                check(window.focusWidget() == elsewhere,
                      QStringLiteral("the focus really left the value field (%1)")
                          .arg(window.focusWidget() ? window.focusWidget()->metaObject()->className()
                                                    : "none"));
                sendKeyTo(&window, Qt::Key_Escape);
                settle(250);
                check(!view->hasModelingPreview(),
                      "and Escape still cancels, because the claim is application-wide "
                      "rather than on the field");
                check(std::fabs(bodyVolume() - steadyVolume) < 1.0e-6,
                      "with nothing committed on that route either");
            }
        }

        // The arrow goes the moment its predicate stops holding - one function
        // decides both directions.
        trigger(window, QStringLiteral("Select Bodies"));
        settle(200);
        check(!view->hasBevelArrow(), "leaving edge selection retires the arrow");
        BevelArrow* retired = window.findChild<BevelArrow*>();
        check(retired == nullptr || !retired->isVisible(),
              "and its value chip goes with it");
        check(!view->edgeDimensionSuppressed(),
              "and the edge-length annotation is free again");

        // Leave the document as this block found it.
        view->setSelectedSolids({bevelId});
        settle(150);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(200);
        check(static_cast<int>(window.document().count()) == bevelBodiesBefore,
              "the bevel probe leaves the document as it found it");
        view->clearSelection();
        view->animateTo(bevelCameraBefore);   // animations are off: immediate
        settle(200);
    }

    // --- the whole app reads in one unit --------------------------------------
    {
        QAction* mm = action(window, QStringLiteral("Millimetres"));
        QAction* cm = action(window, QStringLiteral("Centimetres"));
        check(mm != nullptr && cm != nullptr, "both units are offered");
        check(mm != nullptr && mm->isChecked(), "millimetres is the default");

        ItemsPanel* items = window.findChild<ItemsPanel*>();
        check(items != nullptr, "the items panel is present");
        const QString beforeItems = items ? items->rowTextAt(0) : QString();
        check(beforeItems.contains(QStringLiteral("mm")),
              QStringLiteral("the panel reads in millimetres (\"%1\")").arg(beforeItems));

        // The bar's unit button is not a second unit-writing path: it triggers
        // the OTHER unit's existing QAction, so persistence, the items panel,
        // the status bar and the extrude field's label all follow the one
        // route Phase 4 built. Asserting the menu action's checked state after
        // each click is what proves that - a private toggle inside the button
        // would move the label and leave the menu behind.
        AppBar* unitBar = qobject_cast<AppBar*>(window.menuWidget());
        QAbstractButton* unitButton =
            unitBar ? qobject_cast<QAbstractButton*>(unitBar->unitButton()) : nullptr;
        check(unitButton != nullptr && unitButton->text() == QStringLiteral("mm"),
              "the bar's unit button starts on millimetres");
        if (unitButton && mm && cm) {
            clickAt(unitButton, QPointF(unitButton->width() / 2.0,
                                        unitButton->height() / 2.0));
            settle(200);
            check(unitButton->text() == QStringLiteral("cm"),
                  QStringLiteral("clicking it switches to centimetres (\"%1\")")
                      .arg(unitButton->text()));
            check(cm->isChecked() && !mm->isChecked(),
                  "and it went through the Units actions, not a private toggle");
            check(items && items->rowTextAt(0).contains(QStringLiteral("cm")),
                  QStringLiteral("the items panel follows the button (\"%1\")")
                      .arg(items ? items->rowTextAt(0) : QString()));

            clickAt(unitButton, QPointF(unitButton->width() / 2.0,
                                        unitButton->height() / 2.0));
            settle(200);
            check(unitButton->text() == QStringLiteral("mm"),
                  "clicking it again cycles back to millimetres");
            check(mm->isChecked() && !cm->isChecked(),
                  "the Units actions came back with it");
            check(items && items->rowTextAt(0).contains(QStringLiteral("mm")),
                  "and so did the items panel");
        }

        if (cm) {
            cm->trigger();
            settle(150);
            const QString afterItems = items ? items->rowTextAt(0) : QString();
            check(afterItems.contains(QStringLiteral("cm")),
                  QStringLiteral("the panel follows the unit (\"%1\")").arg(afterItems));
            check(!afterItems.contains(QStringLiteral("mm")),
                  "and no millimetre value is left behind");

            // The trap: a field that displays centimetres and reads millimetres.
            trigger(window, QStringLiteral("Start Sketch"));
            clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
            clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
            trigger(window, QStringLiteral("Finish Sketch"));
            settle(150);
            const int before = static_cast<int>(window.document().solids().size());
            trigger(window, QStringLiteral("Extrude..."));
            settle(150);
            ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
            if (preview && preview->field()) {
                preview->field()->setText(QStringLiteral("4"));
                settle(120);
                QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(preview->field(), &commit);
                settle(250);
            }
            check(static_cast<int>(window.document().solids().size()) == before + 1,
                  "the body was created");
            if (!window.document().solids().empty()) {
                const Measure::Extents e =
                    Measure::extentsOf(window.document().solids().back().shape);
                check(std::fabs(e.z - 40.0) < 1e-6,
                      QStringLiteral("4 typed in centimetres is 40 mm, not 4 (got %1)")
                          .arg(e.z));
            }

            mm->trigger();
            settle(150);
            check(items && items->rowTextAt(0).contains(QStringLiteral("mm")),
                  "switching back restores millimetres");
        }
    }

    // --- a dimension already on screen follows the unit too -------------------
    // DimensionRenderer is not a QObject and nothing rebuilt a label that was
    // already up, so it kept saying "40 mm" over a viewport that had switched
    // to centimetres, until the next mouse move happened to redraw it. The
    // spec asked for the status bar, the items panel and a dimension label
    // checked together, which is what this does - and the label is checked
    // BEFORE any mouse move, because a move would rebuild it either way.
    {
        QAction* mm = action(window, QStringLiteral("Millimetres"));
        QAction* cm = action(window, QStringLiteral("Centimetres"));
        ItemsPanel* items = window.findChild<ItemsPanel*>();
        check(mm != nullptr && cm != nullptr && items != nullptr,
              "both units and the items panel are available for this check");

        trigger(window, QStringLiteral("Start Sketch"));
        settle(100);
        clickAt(view, QPointF(300, 300));
        moveTo(view, QPointF(430, 300));
        settle(120);
        check(view->dimension().isShowing(), "a live dimension is up before the switch");

        gp_Pnt cursor;
        const gp_Pnt anchor = window.sketch().points().empty() ? gp_Pnt()
                                                               : window.sketch().points().front();
        check(!window.sketch().points().empty() && view->lastHoverPoint(cursor),
              "the segment's two ends are known independently of the renderer");
        const double span = anchor.Distance(cursor);
        check(view->dimension().labelText() == Measure::formatLength(span),
              "and it reads in millimetres to begin with");

        if (mm && cm && items) {
            cm->trigger();
            settle(150);
            const QString label = QString::fromStdString(view->dimension().labelText());
            check(label == QString::fromStdString(Measure::formatLength(span)),
                  QStringLiteral("the label already on screen re-reads in centimetres "
                                 "with no mouse move at all (\"%1\")").arg(label));
            check(label.endsWith(QStringLiteral("cm")),
                  QStringLiteral("and carries the new unit, not the old one (\"%1\")")
                      .arg(label));
            check(items->rowTextAt(0).contains(QStringLiteral("cm")),
                  QStringLiteral("the items panel switched in the same breath (\"%1\")")
                      .arg(items->rowTextAt(0)));

            // The status bar's cursor readout is written on each move, so one
            // move is what proves it formats in the new unit as well.
            moveTo(view, QPointF(432, 300));
            settle(100);
            const QString status = window.statusBar()->currentMessage();
            check(status.contains(QStringLiteral("cm")) &&
                      !status.contains(QStringLiteral("mm")),
                  QStringLiteral("and so does the status bar (\"%1\")").arg(status));

            mm->trigger();
            settle(150);
            gp_Pnt moved;
            view->lastHoverPoint(moved);
            check(view->dimension().labelText() ==
                      Measure::formatLength(anchor.Distance(moved)),
                  QStringLiteral("switching back restores millimetres on the label that "
                                 "is still up (\"%1\")")
                      .arg(QString::fromStdString(view->dimension().labelText())));
        }

        trigger(window, QStringLiteral("Cancel Sketch"));
        settle(120);
        check(!view->dimension().isShowing(),
              "and the probe leaves no dimension behind");
    }

    // --- switching the unit while the extrude preview is open updates it -----
    // Fix round 1, Important: onAppStateChanged() used to repaint the panel
    // (so the label read "(cm)") without rebuilding the preview, so the
    // shape on screen stayed the OLD unit's reading of the field - a user
    // could pick Centimetres with "10" still in the field and see the 10 mm
    // body they had before, then commit the 100 mm body the field silently
    // now meant.
    {
        QAction* mm = action(window, QStringLiteral("Millimetres"));
        QAction* cm = action(window, QStringLiteral("Centimetres"));
        check(mm != nullptr && cm != nullptr, "both units are still available for this check");

        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, QPointF(300, 300)); clickAt(view, QPointF(420, 300));
        clickAt(view, QPointF(420, 380)); clickAt(view, QPointF(300, 380));
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);
        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr && preview->field(),
              "a preview with a field is open for the switch-while-open check");

        if (preview && preview->field() && mm && cm) {
            preview->field()->setText(QStringLiteral("5"));
            settle(120);
            check(std::fabs(preview->height() - 5.0) < 1e-6,
                  "the field reads 5 mm before any unit switch");

            cm->trigger();
            settle(150);
            check(std::fabs(preview->height() - 50.0) < 1e-6,
                  QStringLiteral("the SAME field text means 50 mm once centimetres is "
                                 "selected, not the stale 5 mm reading (got %1)")
                      .arg(preview->height()));
            const Measure::Extents shownInCm = Measure::extentsOf(view->previewShape());
            check(std::fabs(shownInCm.z - 50.0) < 1e-6,
                  QStringLiteral("and the shape actually on screen is 50 mm tall, not "
                                 "still the 5 mm one from before the switch (got %1)")
                      .arg(shownInCm.z));

            mm->trigger();
            settle(150);
            check(std::fabs(preview->height() - 5.0) < 1e-6,
                  "switching back re-reads the same text as 5 mm again");

            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &commit);
            settle(250);
        }
        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "the body from the switch-while-open check was created");
    }

    // --- icons ----------------------------------------------------------------
    {
        // DisplayMode, Screenshot and Fit are gone - Task 3 folded Wireframe
        // and Fit All into the app bar as text buttons and left Save
        // Screenshot menu-only, so the rail never needed those three icons
        // and IconSet dropped them rather than keeping dead glyphs (see
        // IconSet.h).
        const IconSet::Glyph all[] = {
            IconSet::Glyph::Sketch,      IconSet::Glyph::Extrude,
            IconSet::Glyph::Fuse,        IconSet::Glyph::Cut,
            IconSet::Glyph::Intersect,   IconSet::Glyph::Delete,
            IconSet::Glyph::Undo,        IconSet::Glyph::Redo,
            IconSet::Glyph::Items,       IconSet::Glyph::Snap,
            IconSet::Glyph::SelectSolid, IconSet::Glyph::SelectFace,
        };
        bool allDrawn = true;
        for (IconSet::Glyph glyph : all) {
            const QPixmap pixmap = IconSet::icon(glyph).pixmap(16, 16);
            // A glyph that painted nothing yields a fully transparent pixmap.
            if (pixmap.isNull() || pixmap.toImage().isNull()) { allDrawn = false; break; }
            bool anyInk = false;
            const QImage image = pixmap.toImage();
            for (int y = 0; y < image.height() && !anyInk; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0) { anyInk = true; break; }
                }
            }
            if (!anyInk) { allDrawn = false; break; }
        }
        check(allDrawn, "every glyph paints something at 16x16");
    }

    // --- chips mirror their action -------------------------------------------
    {
        QAction probe(QStringLiteral("Probe"));
        probe.setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
        ToolChip chip(&probe, IconSet::Glyph::Sketch);

        probe.setEnabled(false);
        check(!chip.isEnabled(), "chip disables with its action");
        probe.setEnabled(true);
        check(chip.isEnabled(), "chip re-enables with its action");

        int fired = 0;
        QObject::connect(&probe, &QAction::triggered, [&fired] { ++fired; });
        chip.click();
        check(fired == 1, "clicking the chip triggers the action exactly once");

        probe.setCheckable(true);
        probe.setChecked(true);
        check(chip.isChecked(), "chip mirrors the checked state");

        // The action is the only thing that may change the checked state: a
        // click that does not reach the action must leave the chip alone.
        QObject::disconnect(&probe, nullptr, nullptr, nullptr);
        const bool before = chip.isChecked();
        chip.click();
        check(chip.isChecked() == before, "a chip never toggles its own checked state");
    }

    // --- overlay anchoring ----------------------------------------------------
    {
        // Glyph::Fit no longer exists (Task 3 folded Fit All into the app bar
        // as a text button; Minor 8 removed the now-dead icon) - any glyph
        // does for this probe, which only cares that a chip is a chip.
        QAction probe(QStringLiteral("Probe"));
        auto* cluster = new ToolCluster(view);
        cluster->addChip(new ToolChip(&probe, IconSet::Glyph::Sketch));

        // A second, independent cluster that outlives the first - the real
        // post-condition for "relayout survives a destroyed cluster" is that
        // this one is still laid out correctly afterwards.
        QAction probe2(QStringLiteral("Probe2"));
        auto* survivor = new ToolCluster(view);
        survivor->addChip(new ToolChip(&probe2, IconSet::Glyph::Sketch));

        ViewportOverlay overlay(view);
        overlay.addWidget(cluster, ViewportOverlay::Anchor::BottomLeft);
        overlay.addWidget(survivor, ViewportOverlay::Anchor::TopRight);
        overlay.relayout();

        const QRect bounds = view->rect();
        check(bounds.contains(cluster->geometry()),
              "an anchored cluster sits inside the viewport");
        const int bottomGap = bounds.bottom() - cluster->geometry().bottom();
        check(bottomGap >= 8 && bottomGap <= 32,
              QStringLiteral("bottom-anchored cluster keeps its margin (%1px)").arg(bottomGap));

        const int widthBefore = cluster->width();
        view->resize(view->width() + 120, view->height());
        settle(150);
        check(bounds.left() <= cluster->geometry().left() && cluster->width() == widthBefore,
              "cluster keeps its size and stays anchored after a resize");
        // A destroyed widget must not take the overlay down with it on the next
        // layout pass - QPointer entries go null and are skipped, and the
        // remaining, still-alive widget must still get laid out.
        delete cluster;
        overlay.relayout();
        check(view->rect().contains(survivor->geometry()),
              "the surviving cluster is still laid out after the destroyed one is skipped");
    }

    // --- view controls --------------------------------------------------------
    {
        QAction* wireframe = action(window, QStringLiteral("Wireframe"));
        check(wireframe != nullptr, "a Wireframe display-mode action exists");
        if (wireframe) {
            check(wireframe->isCheckable(), "Wireframe is a toggle");

            wireframe->trigger();
            settle(200);
            check(view->isWireframe(), "triggering Wireframe turns wireframe mode on");

            wireframe->trigger();
            settle(200);
            check(!view->isWireframe(), "triggering it again turns wireframe mode off");

            // Regression guard: displaySolid() used to hardcode AIS_Shaded, so
            // resyncView() - which Undo and Redo both run - silently reverted
            // every solid to shaded while the Wireframe action (and myWireframe
            // itself) stayed checked/true. isWireframe() alone cannot catch that
            // - it is untouched by resyncView() - so also check the solid's
            // actual live display mode via isSolidWireframe().
            wireframe->trigger();
            settle(200);
            check(view->isWireframe(), "wireframe is on going into undo/redo");
            const int solidId = window.document().solids().front().id;
            check(view->isSolidWireframe(solidId), "the solid itself renders wireframe before undo/redo");

            trigger(window, QStringLiteral("Undo"));
            trigger(window, QStringLiteral("Redo"));
            check(view->isWireframe(), "wireframe survives undo/redo (resyncView)");
            check(view->isSolidWireframe(solidId),
                  "the resynced solid still renders wireframe, not just the flag");

            // Leave the viewport shaded so later checks are unaffected.
            wireframe->trigger();
            settle(200);
            check(!view->isWireframe(), "wireframe turned back off, viewport left shaded");
        }
    }

    // --- standard views set turntable state -----------------------------------
    {
        trigger(window, QStringLiteral("Front"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg) < 1e-3,
              "Front is azimuth 0, elevation 0");

        trigger(window, QStringLiteral("Top"));
        settle(400);
        // setViewTop() requests 89 degrees, but CameraController's clamp caps
        // elevation at kMaxElevation = 88 (see CameraController.h and the
        // "setState clamps elevation" headless check) - 88 is what actually
        // lands, and it is still comfortably non-degenerate.
        check(std::fabs(view->camera().state().elevationDeg - 88.0) < 1e-3,
              "Top is elevation +88, clamped from the requested +89");

        trigger(window, QStringLiteral("Right"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-90.0)) < 1e-3,
              "Right is azimuth -90");

        trigger(window, QStringLiteral("Axonometric"));
        settle(400);
        check(std::fabs(view->camera().state().azimuthDeg - (-45.0)) < 1e-3 &&
              std::fabs(view->camera().state().elevationDeg - 30.0) < 1e-3,
              "Axonometric returns to the startup angles");
    }

    // --- animated transitions -------------------------------------------------
    {
        view->setAnimationsEnabled(true);
        CameraState goal = view->camera().state();
        goal.azimuthDeg += 90.0;
        const double azBefore = view->camera().state().azimuthDeg;
        view->animateTo(goal);
        // Mid-flight (a few event-loop turns in), the camera is between the
        // endpoints - that is what distinguishes animation from teleporting.
        settle(80);
        const double azMid = view->camera().state().azimuthDeg;
        check(std::fabs(azMid - azBefore) > 1.0 &&
              std::fabs(azMid - goal.azimuthDeg) > 1.0,
              "animateTo passes through intermediate states");
        settle(500);
        check(std::fabs(view->camera().state().azimuthDeg - goal.azimuthDeg) < 1e-3,
              "animateTo settles exactly on the goal");
        view->setAnimationsEnabled(false);
        check(!view->animationsEnabled(), "animations re-disabled for the rest of the suite");
    }

    // --- a fade cannot be double-clicked into a second undo -------------------
    // The suite otherwise runs with animations off, so ToastHost::dismiss()
    // is synchronous there and this exact bug is invisible to it - the toast
    // and its Undo pill vanish together in the same call that pops the undo
    // stack, leaving nothing for a second click to land on. With animations
    // on (the shipping default), a fade used to leave the pill visible and
    // clickable for the whole 160 ms, so an ordinary impatient double-click
    // undid two operations - one of them silently.
    {
        view->setAnimationsEnabled(true);

        check(!window.document().solids().empty(),
              "there is a body to delete for the double-click check");
        const int before = static_cast<int>(window.document().solids().size());
        view->setSelectedSolids({window.document().solids().front().id});
        settle(100);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(150);

        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr && toasts->isShowing(),
              "deleting a body raises a toast with animations enabled");
        QWidget* undo = toasts ? toasts->undoControl() : nullptr;
        check(undo != nullptr && undo->isVisible(), "the toast offers Undo");

        if (toasts && undo) {
            const QPoint centre = undo->mapTo(view, QPoint(undo->width() / 2, undo->height() / 2));
            check(view->childAt(centre) == undo,
                  "the Undo control is reachable before the first click");

            clickAt(undo, QPointF(undo->width() / 2.0, undo->height() / 2.0));
            // Deliberately not settled for the full fade duration - this is
            // the impatient double-click, landing mid fade-out.
            const int afterFirstClick = static_cast<int>(window.document().solids().size());
            check(afterFirstClick == before, "the first click's Undo restored the body");

            // dismiss() hides the Undo control at its own top, before the
            // fade even starts - so real hit-testing must already find
            // something other than the control here, exactly as it would
            // for a genuine second click a user fires off before the toast
            // has visibly finished fading.
            QWidget* hitDuringFade = view->childAt(centre);
            check(hitDuringFade != undo,
                  "the Undo control is not reachable by a real click during the fade");

            // The second click a real user's double-click would produce -
            // sent to whatever hit-testing actually finds there (nothing
            // claims this point once the control is hidden, so it falls
            // through to the viewport, same as clicking empty space).
            clickAt(view, QPointF(centre));
            settle(250);   // outlasts the 160 ms fade either way
            check(static_cast<int>(window.document().solids().size()) == afterFirstClick,
                  "a second click during the fade did not undo a second time");
        }

        // A resize mid-fade is the OTHER trigger for the same race:
        // ToastHost's own event filter re-derives the Undo control's
        // geometry - and, through Toast::syncUndoGeometry(), its visibility
        // - on every viewport resize for as long as the toast itself is
        // still isVisible(), which it is for the whole fade, not just until
        // dismiss() returns. A one-shot hide() at the top of dismiss() does
        // not survive that; folding "is this toast dismissing?" into the
        // same predicate syncUndoGeometry() already computes does.
        check(!window.document().solids().empty(),
              "there is a body to delete for the resize-mid-fade check");
        const int beforeResizeCheck = static_cast<int>(window.document().solids().size());
        view->setSelectedSolids({window.document().solids().front().id});
        settle(100);
        trigger(window, QStringLiteral("Delete Selected"));
        settle(150);

        QWidget* undo2 = toasts ? toasts->undoControl() : nullptr;
        check(undo2 != nullptr && undo2->isVisible(),
              "the toast offers Undo again for the resize-mid-fade check");
        if (undo2) {
            clickAt(undo2, QPointF(undo2->width() / 2.0, undo2->height() / 2.0));
            // Deliberately not settled for the fade duration - the resize
            // below has to land while it is still running.
            check(static_cast<int>(window.document().solids().size()) == beforeResizeCheck,
                  "Undo restored the body before the resize");

            const QSize original = view->size();
            view->resize(original.width() + 40, original.height());
            settle(30);   // well inside the 160 ms fade

            const QPoint centreAfterResize =
                undo2->mapTo(view, QPoint(undo2->width() / 2, undo2->height() / 2));
            check(view->childAt(centreAfterResize) != undo2,
                  "a viewport resize mid-fade does not re-show the Undo control");

            view->resize(original);
            settle(250);   // outlasts the fade, and lets the resize settle back
        }

        view->setAnimationsEnabled(false);
        check(!view->animationsEnabled(), "animations re-disabled again after the double-click check");
    }

    // --- grid subdivision policy ----------------------------------------------
    {
        check(GridRenderer::minorStepFor(700.0) == 10.0,
              "default working distance uses the 10mm grid");
        check(GridRenderer::minorStepFor(50.0) == 1.0,
              "zoomed close in, the 1mm grid appears");
        check(GridRenderer::minorStepFor(8000.0) == 100.0,
              "zoomed far out, the 100mm grid takes over");
        check(GridRenderer::minorStepFor(0.0) >= 1.0 &&
              GridRenderer::minorStepFor(1e9) <= 100.0,
              "extreme distances stay inside the defined levels");
        check(GridRenderer::minorStepFor(119.9) == 1.0 &&
              GridRenderer::minorStepFor(120.0) == 10.0,
              "the 120mm threshold flips exactly once");
        check(GridRenderer::minorStepFor(2499.9) == 10.0 &&
              GridRenderer::minorStepFor(2500.0) == 100.0,
              "the 2500mm threshold flips exactly once");

        // Line positions must sit on the absolute grid regardless of band
        // extent parity - a band edge is not in general a line position.
        const double f1 = GridRenderer::firstLineAtOrBelow(2150.0, 100.0);
        check(std::fmod(f1, 100.0) == 0.0 && f1 <= -2150.0 && f1 > -2350.0,
              "band start snaps outward onto the absolute grid (odd parity)");
        const double f2 = GridRenderer::firstLineAtOrBelow(2100.0, 100.0);
        check(f2 == -2100.0,
              "an already-aligned band edge is its own first line");
    }

    // --- vocabulary is enforced, not merely documented ------------------------
    {
        // A documented vocabulary drifts the moment someone is in a hurry. An
        // asserted one cannot.
        const QStringList banned = bannedWords();
        QStringList offenders;
        for (QAction* candidate : window.findChildren<QAction*>()) {
            const QString text = candidate->text().remove(QLatin1Char('&'));
            const QString tip = candidate->toolTip();
            for (const QString& word : banned) {
                if (text.contains(word, Qt::CaseInsensitive) ||
                    tip.contains(word, Qt::CaseInsensitive)) {
                    offenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(offenders.isEmpty(),
              QStringLiteral("no action uses a banned word (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("none")
                                           : offenders.join(QStringLiteral(", "))));

        QStringList tipOffenders;
        for (QWidget* widget : window.findChildren<QWidget*>()) {
            const QString tip = widget->toolTip();
            if (tip.isEmpty()) continue;
            for (const QString& word : banned) {
                if (tip.contains(word, Qt::CaseInsensitive))
                    tipOffenders << (tip.left(30) + QStringLiteral("…"));
            }
        }
        check(tipOffenders.isEmpty(),
              QStringLiteral("no widget tooltip uses a banned word (%1)")
                  .arg(tipOffenders.isEmpty() ? QStringLiteral("none")
                                              : tipOffenders.join(QStringLiteral(", "))));

        // The app bar's wordmark and its buttons' labels are painted, and the
        // two readouts carry no QAction of their own, so neither sweep above
        // can see them.
        AppBar* sweptBar = qobject_cast<AppBar*>(window.menuWidget());
        check(sweptBar != nullptr, "the app bar is there to sweep");
        if (sweptBar) {
            const QStringList barTexts = sweptBar->paintedTexts();
            check(barTexts.contains(sweptBar->wordmark()) && barTexts.size() >= 5,
                  QStringLiteral("the bar exposes its painted copy - wordmark and "
                                 "every button (%1)")
                      .arg(barTexts.join(QStringLiteral(", "))));
            QStringList barOffenders;
            for (const QString& text : barTexts) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        barOffenders << (text + QStringLiteral(" [") + word +
                                         QStringLiteral("]"));
                }
            }
            check(barOffenders.isEmpty(),
                  QStringLiteral("no app bar text uses a banned word (%1)")
                      .arg(barOffenders.isEmpty()
                               ? QStringLiteral("none")
                               : barOffenders.join(QStringLiteral(", "))));
        }

        // The walkthrough panel's text - title, skip control, and steps - is
        // all painted, not put on any action text or tooltip, so none of the
        // loops above ever see any of it. paintedTexts() is the full set, not
        // just the steps, so nothing painted there is left unswept.
        QStringList walkthroughOffenders;
        for (WalkthroughPanel* panel : window.findChildren<WalkthroughPanel*>()) {
            for (const QString& text : panel->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        walkthroughOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(walkthroughOffenders.isEmpty(),
              QStringLiteral("no walkthrough panel text uses a banned word (%1)")
                  .arg(walkthroughOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : walkthroughOffenders.join(QStringLiteral(", "))));

        // Same story for the hint balloon: its copy is painted, not put on an
        // action or a tooltip, so it needs its own explicit sweep too.
        QStringList hintOffenders;
        for (HintBalloon* hint : window.findChildren<HintBalloon*>()) {
            for (const QString& text : hint->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        hintOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(hintOffenders.isEmpty(),
              QStringLiteral("no hint balloon text uses a banned word (%1)")
                  .arg(hintOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : hintOffenders.join(QStringLiteral(", "))));

        // Same story for the toast: its copy is painted, not put on an action
        // or a tooltip, so it needs its own explicit sweep too. Unlike the
        // other three widgets it has no fixed set of strings to enumerate, so
        // it records every message it has been given this run and the sweep
        // covers all of them - it used to see only whichever one happened to
        // be live, which made the sweep a coin toss. That limit was HONEST but
        // it was still a limit: a message never triggered during a run is a
        // message the sweep never sees, and this branch shipped two of them.
        //
        // Both are refusals the kernel has to produce and a probe cannot make
        // it produce: a chamfer big enough to be refused (only the fillet half
        // of that pair is reachable - the box's geometry refuses one and
        // accepts the other at every size the drag can reach) and a transform
        // the kernel turns down (it accepts every gp_Trsf a gesture can build,
        // which is why this layer clamps the scale itself).
        //
        // So they are shown here, once each, through the SAME accessors
        // MainWindow's own refusal paths call - not through a second copy of
        // the sentence that only the sweep would ever see, which would pin
        // nothing at all. Each shown message lands in paintedTexts() and is
        // swept below with the rest.
        ToastHost* copyHost = window.findChild<ToastHost*>();
        check(copyHost != nullptr, "there is a toast host to show the untriggered copy on");
        if (copyHost) {
            gp_Trsf rotated;
            rotated.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), 0.4);
            gp_Trsf scaled;
            scaled.SetScale(gp_Pnt(0.0, 0.0, 0.0), 1.5);

            // Important 3's fix, asserted on the derivation itself rather than
            // on a string that happens to contain a word: the verb comes off
            // the transform, so a refused rotate names Rotate. It used to be
            // derived only on the SUCCESS path, with "Move" hard-coded above
            // it, so every refused rotate and every refused scale reported a
            // move the user never made.
            check(MainWindow::transformOperationName(rotated) == QStringLiteral("Rotate") &&
                      MainWindow::transformOperationName(scaled) == QStringLiteral("Scale") &&
                      MainWindow::transformOperationName(gp_Trsf()) == QStringLiteral("Move"),
                  QStringLiteral("a refusal names the operation the delta actually is "
                                 "(%1 / %2 / %3)")
                      .arg(MainWindow::transformOperationName(rotated),
                           MainWindow::transformOperationName(scaled),
                           MainWindow::transformOperationName(gp_Trsf())));
            check(MainWindow::transformRefusalText(rotated).contains(
                      QStringLiteral("rotated")) &&
                      MainWindow::transformRefusalText(scaled).contains(
                          QStringLiteral("scaled")),
                  QStringLiteral("and its sentence says so too (\"%1\")")
                      .arg(MainWindow::transformRefusalText(rotated)));

            // Stamped, so the last of them can be dismissed the way a real
            // one is - by telling the host the document moved past it - rather
            // than left sitting over the viewport for its full eight seconds,
            // where every later capture and layout probe would have to work
            // around it.
            constexpr int kCopyStamp = 1000000;
            copyHost->show(MainWindow::bevelRefusalText(false), Toast::Kind::Failure, false,
                           kCopyStamp);
            settle(60);
            copyHost->show(MainWindow::transformRefusalText(rotated), Toast::Kind::Failure,
                           false, kCopyStamp);
            settle(60);
            copyHost->show(MainWindow::transformRefusalText(scaled), Toast::Kind::Failure,
                           false, kCopyStamp);
            settle(60);
            copyHost->show(MainWindow::transformRefusalText(gp_Trsf()), Toast::Kind::Failure,
                           false, kCopyStamp);
            settle(60);
            check(copyHost->currentText() ==
                      MainWindow::transformRefusalText(gp_Trsf()),
                  "the refusal copy really reached the toast, so paintedTexts() "
                  "has it to sweep");
            copyHost->documentMovedTo(kCopyStamp + 1);
            settle(120);
            check(!copyHost->isShowing(),
                  "and the probe leaves no toast standing over the viewport");
        }

        Toast* sweptToast = window.findChild<Toast*>();
        check(sweptToast != nullptr && sweptToast->paintedTexts().size() > 2,
              QStringLiteral("the toast sweep covers every message shown this run, "
                             "not just the live one (%1 strings)")
                  .arg(sweptToast ? sweptToast->paintedTexts().size() : 0));
        QStringList toastOffenders;
        for (Toast* toastWidget : window.findChildren<Toast*>()) {
            for (const QString& text : toastWidget->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        toastOffenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(toastOffenders.isEmpty(),
              QStringLiteral("no toast text uses a banned word (%1)")
                  .arg(toastOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : toastOffenders.join(QStringLiteral(", "))));

        // Same story for the extrude preview: its label is painted, not put
        // on an action or a tooltip, so it needs its own explicit sweep too.
        QStringList extrudePreviewOffenders;
        for (ExtrudePreview* preview : window.findChildren<ExtrudePreview*>()) {
            for (const QString& text : preview->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        extrudePreviewOffenders
                            << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(extrudePreviewOffenders.isEmpty(),
              QStringLiteral("no extrude preview text uses a banned word (%1)")
                  .arg(extrudePreviewOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : extrudePreviewOffenders.join(QStringLiteral(", "))));

        // And the pull arrow's value chip, for the same reason: its label and
        // its key hint are painted, so no action or tooltip carries them.
        QStringList pullArrowOffenders;
        for (PullArrow* pull : window.findChildren<PullArrow*>()) {
            for (const QString& text : pull->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        pullArrowOffenders
                            << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(pullArrowOffenders.isEmpty(),
              QStringLiteral("no pull arrow text uses a banned word (%1)")
                  .arg(pullArrowOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : pullArrowOffenders.join(QStringLiteral(", "))));
        // Non-vacuity - the partner the bevel chip's sweep below already has,
        // and which this one was missing. findChildren() over no arrows at all
        // reports "none" exactly as loudly as a clean arrow does.
        PullArrow* sweptPull = window.findChild<PullArrow*>();
        check(sweptPull != nullptr && sweptPull->paintedTexts().size() == 2,
              QStringLiteral("and it really swept that arrow's two painted strings (%1)")
                  .arg(sweptPull ? sweptPull->paintedTexts().join(QStringLiteral(" / "))
                                 : QString()));

        // And the round/flatten chip, which is the one place the newly banned
        // word could most easily have leaked: the class is called BevelArrow
        // and every string it paints has to say Fillet or Chamfer instead.
        QStringList bevelOffenders;
        for (BevelArrow* arrow : window.findChildren<BevelArrow*>()) {
            for (const QString& text : arrow->paintedTexts()) {
                for (const QString& word : banned) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        bevelOffenders
                            << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
            }
        }
        check(bevelOffenders.isEmpty(),
              QStringLiteral("no round/flatten chip text uses a banned word (%1)")
                  .arg(bevelOffenders.isEmpty()
                           ? QStringLiteral("none")
                           : bevelOffenders.join(QStringLiteral(", "))));
        // Non-vacuity: the sweep above passes just as well over an empty list.
        BevelArrow* sweptBevel = window.findChild<BevelArrow*>();
        check(sweptBevel != nullptr && sweptBevel->paintedTexts().size() == 4,
              QStringLiteral("and it really swept that chip's four painted strings (%1)")
                  .arg(sweptBevel ? sweptBevel->paintedTexts().join(QStringLiteral(" / "))
                                  : QString()));

        // The state label is the app's most-updated string; it must obey the
        // vocabulary too. It is a permanent widget on the status bar.
        QString stateText;
        for (QLabel* label : window.statusBar()->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) stateText = label->text();
        }
        check(!stateText.contains(QStringLiteral("solid"), Qt::CaseInsensitive),
              QStringLiteral("the state label says body, not solid (\"%1\")").arg(stateText));
        check(!stateText.contains(QStringLiteral("(s)")),
              "the state label writes plurals out rather than using (s)");

        check(action(window, QStringLiteral("Union")) != nullptr, "the Union action exists");
        check(action(window, QStringLiteral("Subtract")) != nullptr, "the Subtract action exists");
        check(action(window, QStringLiteral("Intersect")) != nullptr, "the Intersect action exists");
    }

    // --- progress is recorded from real actions -------------------------------
    {
        // The suite has by now completed sketches, extrudes and a boolean, so
        // those events must have been counted.
        check(window.progress().count("extrude.completed") >= 2,
              QStringLiteral("extrudes were recorded (%1)")
                  .arg(window.progress().count("extrude.completed")));
        check(window.progress().count("boolean.completed") >= 1,
              "the boolean was recorded");
        check(window.progress().count("sketch.completed") >= 2,
              "closing an outline was recorded");

        const int before = window.progress().count("undo.used");
        trigger(window, QStringLiteral("Undo"));
        settle(150);
        check(window.progress().count("undo.used") == before + 1,
              "undo records exactly once");
        trigger(window, QStringLiteral("Redo"));
        settle(150);
    }

    // --- the Help menu ---------------------------------------------------------
    {
        check(action(window, QStringLiteral("Keyboard Shortcuts")) != nullptr,
              "a Keyboard Shortcuts action exists");
        QAction* reset = action(window, QStringLiteral("Show tips again"));
        check(reset != nullptr, "a Show tips again action exists");
        if (reset) {
            reset->trigger();
            settle(100);
            check(window.progress().count("extrude.completed") == 0,
                  "Show tips again clears the progress store");

            // Acceptance criterion 5: Show tips again must genuinely restore
            // the walkthrough, not just clear the counters underneath it -
            // and it must not instantly re-complete itself just because a
            // body from before the reset is still sitting in the document.
            check(window.document().count() > 0,
                  "a body from before the reset is still in the document");
            WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
            check(guide != nullptr && guide->isVisible(),
                  "Show tips again brings the guide back");
            check(guide != nullptr && !guide->isFinished(),
                  "the restored guide is not finished");
            check(guide != nullptr && guide->completedSteps() == 0,
                  "the restored guide starts over at zero steps, not "
                  "re-completed by the body already in the document");

            // Prove the restore is real, not cosmetic: walking through the
            // guide again - building one more body - completes it again.
            trigger(window, QStringLiteral("Start Sketch"));
            sketchQuad(window, 0.35, 0.35, 0.45, 0.45);
            trigger(window, QStringLiteral("Finish Sketch"));
            check(window.extrudePendingFace(5.0), "a third extrude reports success");
            settle(150);
            check(guide != nullptr && guide->isFinished(),
                  "building another body completes the restored guide");
            check(window.progress().hasLearned("walkthrough.done"),
                  "the guide records walkthrough.done again after completing for real");
        }
    }

    // --- the view hint retires on its EVENT, from either route ---------------
    // Two defects met here. The spec records view.changed when "a standard
    // view or the gizmo changes the camera", but only the View menu ever
    // recorded it - so a user who only clicked the gizmo dismissed that hint
    // every session and never crossed the threshold. And the hint's own
    // retire condition read AxisGizmo::labelText() rather than the event, so
    // pressing 0 recorded view.changed while leaving the camera at a pose the
    // gizmo labels "Persp" - the balloon sat there after the user had done
    // exactly what it taught. One probe per route, since a hint only gets one
    // showing per session.
    {
        MainWindow axoProbe(nullptr, /*persistProgress=*/false);
        axoProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        axoProbe.resize(900, 600);
        axoProbe.show();
        settle(300);
        axoProbe.view()->setAnimationsEnabled(false);
        // Learn face selection so the view hint is the one the first body
        // raises, rather than queueing behind it.
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            axoProbe.progress().record("faceMode.used");
        }

        HintBalloon* axoHint = axoProbe.findChild<HintBalloon*>();
        AxisGizmo* axoGizmo = axoProbe.findChild<AxisGizmo*>();
        check(axoHint != nullptr && axoGizmo != nullptr,
              "the axonometric probe has a hint balloon and a gizmo");
        if (axoHint && axoGizmo) {
            check(buildBody(axoProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the axonometric probe");
            check(axoHint->isVisible() &&
                      axoHint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint is up before any named view is used");

            trigger(axoProbe, QStringLiteral("Axonometric"));
            settle(200);
            check(axoProbe.progress().count("view.changed") == 1,
                  "pressing Axonometric records view.changed exactly once");
            // The pose the Axonometric view leaves behind is precisely the one
            // the gizmo calls "Persp", which is why the old pose-based
            // condition could never retire this hint from this route.
            check(axoProbe.view()->viewDirectionName() == QStringLiteral("Persp"),
                  "and the camera it leaves is still one no named direction fits");
            check(!axoHint->isVisible() && axoHint->currentHint().isEmpty(),
                  "the hint retires all the same - it reads the recorded event, "
                  "not the camera pose");
        }
        axoProbe.close();
    }

    {
        MainWindow gizmoProbe(nullptr, /*persistProgress=*/false);
        gizmoProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        gizmoProbe.resize(900, 600);
        gizmoProbe.show();
        settle(300);
        gizmoProbe.view()->setAnimationsEnabled(false);
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) {
            gizmoProbe.progress().record("faceMode.used");
        }

        HintBalloon* gizmoHint = gizmoProbe.findChild<HintBalloon*>();
        AxisGizmo* gizmo = gizmoProbe.findChild<AxisGizmo*>();
        check(gizmoHint != nullptr && gizmo != nullptr,
              "the gizmo probe has a hint balloon and a gizmo");
        if (gizmoHint && gizmo) {
            check(buildBody(gizmoProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the gizmo probe");
            check(gizmoHint->isVisible() &&
                      gizmoHint->currentHint().contains(QStringLiteral("gizmo")),
                  "the view hint is up for the gizmo probe too");
            check(gizmoProbe.progress().count("view.changed") == 0,
                  "and nothing has recorded view.changed yet");

            clickAt(gizmo, gizmo->tipCenter(2, true));
            settle(250);
            check(gizmoProbe.progress().count("view.changed") >= 1,
                  QStringLiteral("clicking an arm of the gizmo records view.changed "
                                 "(count %1)")
                      .arg(gizmoProbe.progress().count("view.changed")));
            check(!gizmoHint->isVisible() && gizmoHint->currentHint().isEmpty(),
                  "so the gizmo retires the hint that teaches it - a user who "
                  "only ever uses the gizmo now crosses the threshold");
        }
        gizmoProbe.close();
    }

    // --- Show tips again restores the hints, not only the guide ---------------
    // Acceptance criterion 5 says "the walkthrough AND all three hints". The
    // two reset checks elsewhere in this suite assert only the panel, which
    // is why a reset that left every hint silenced until a restart survived
    // two review rounds: HintBalloon::myShownThisSession is session state
    // that clearing the store cannot reach on its own. A dedicated probe,
    // because this needs a hint genuinely dismissed earlier in the SAME
    // session, with nothing else having consumed the other hints' turns.
    {
        MainWindow resetProbe(nullptr, /*persistProgress=*/false);
        resetProbe.setAttribute(Qt::WA_ShowWithoutActivating);
        resetProbe.resize(900, 600);
        resetProbe.show();
        settle(300);
        resetProbe.view()->setAnimationsEnabled(false);

        HintBalloon* resetHint = resetProbe.findChild<HintBalloon*>();
        WalkthroughPanel* resetGuide = resetProbe.findChild<WalkthroughPanel*>();
        check(resetHint != nullptr && resetGuide != nullptr,
              "the reset probe has both a hint balloon and a guide");
        if (resetHint && resetGuide) {
            check(buildBody(resetProbe, 0.30, 0.30, 0.50, 0.50, 10.0),
                  "a body for the reset probe");
            const QString dismissed = resetHint->currentHint();
            check(resetHint->isVisible() && !dismissed.isEmpty(),
                  QStringLiteral("a hint is up before the reset (\"%1\")").arg(dismissed));

            clickAt(resetHint, QPointF(resetHint->width() / 2.0,
                                       resetHint->height() / 2.0));
            check(!resetHint->isVisible() && resetHint->currentHint().isEmpty(),
                  "the hint is dismissed with \"got it\" before the reset");
            check(resetGuide->isFinished() && !resetGuide->isVisible(),
                  "and the guide has completed itself on that body");

            QAction* probeReset = action(resetProbe, QStringLiteral("Show tips again"));
            check(probeReset != nullptr, "the reset probe has the reset action");
            if (probeReset) {
                probeReset->trigger();
                settle(200);
                check(resetGuide->isVisible(), "Show tips again brings the guide back");
                // The point of the whole probe: the SAME hint, dismissed by
                // hand earlier this session, is available again. Without the
                // session set being cleared, a different hint - the one whose
                // turn was still unspent - comes back instead, and the
                // dismissed one stays gone until a restart.
                check(resetHint->isVisible() && resetHint->currentHint() == dismissed,
                      QStringLiteral("the hint dismissed earlier this session is "
                                     "available again after the reset (now: \"%1\")")
                          .arg(resetHint->currentHint()));

                // Both surfaces share this viewport, and below about 800 px of
                // viewport width the centred balloon lands on top of the
                // bottom-right guide. ViewportOverlay::relayout() raises the
                // guide over it, so an overlap would leave the guide covering
                // a balloon that is still the click target.
                check(!resetHint->geometry().intersects(resetGuide->geometry()),
                      QStringLiteral("the restored balloon and guide do not overlap "
                                     "(balloon %1,%2 %3x%4 - guide %5,%6 %7x%8)")
                          .arg(resetHint->x()).arg(resetHint->y())
                          .arg(resetHint->width()).arg(resetHint->height())
                          .arg(resetGuide->x()).arg(resetGuide->y())
                          .arg(resetGuide->width()).arg(resetGuide->height()));
                check(resetProbe.view()->childAt(resetHint->geometry().center()) == resetHint,
                      "and the balloon is still what a real click at its centre finds");
            }
        }
        resetProbe.close();
    }

    // --- the shortcut sheet lists every real binding --------------------------
    {
        QAction* open = action(window, QStringLiteral("Keyboard Shortcuts"));
        check(open != nullptr, "the shortcut sheet has an action to open it");
        if (open) {
            open->trigger();
            settle(150);
            ShortcutSheet* sheet = window.findChild<ShortcutSheet*>();
            check(sheet != nullptr && sheet->isVisible(), "triggering it shows the sheet");

            // Generated, not hand written: every action carrying a shortcut must
            // appear, so the sheet cannot go stale when a binding is added.
            int expected = 0;
            for (QAction* candidate : window.findChildren<QAction*>()) {
                if (!candidate->shortcut().isEmpty()) ++expected;
            }
            check(sheet != nullptr && sheet->rowCount() == expected,
                  QStringLiteral("the sheet lists all %1 bound actions (got %2)")
                      .arg(expected)
                      .arg(sheet ? sheet->rowCount() : -1));
            check(expected > 5, "there are enough bound actions for this to mean something");

            // Cancel Sketch is also bound to plain Escape with the default
            // WindowShortcut context, so in real dispatch QShortcutMap would
            // resolve it before a key press ever reaches the focused sheet.
            // ShortcutOverride is the mechanism Qt gives a widget to claim
            // the key back; sending it directly with sendEvent bypasses
            // QShortcutMap entirely, so this only proves the sheet uses that
            // mechanism correctly for Escape (and leaves everything else
            // alone) - it is not an end-to-end proof that the real keystroke
            // reaches the sheet instead of cancelling the sketch.
            QKeyEvent escapeOverride(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &escapeOverride);
            check(sheet != nullptr && escapeOverride.isAccepted(),
                  "the sheet claims Escape back from the shortcut map");

            QKeyEvent otherOverride(QEvent::ShortcutOverride, Qt::Key_E, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &otherOverride);
            check(sheet != nullptr && !otherOverride.isAccepted(),
                  "an unrelated key is left for the shortcut map, not grabbed");

            // Both bindings the design asks for, not just the first.
            check(open->shortcuts().contains(QKeySequence(Qt::Key_Question)) &&
                      open->shortcuts().contains(QKeySequence(Qt::Key_F1)),
                  "the sheet opens on ? and on F1");

            // Rows are grouped by the menu that owns them, and the headings
            // are read off the menu bar rather than written down twice.
            if (sheet) {
                const QStringList painted = sheet->paintedTexts();
                check(painted.contains(QStringLiteral("Sketch")) &&
                          painted.contains(QStringLiteral("View")) &&
                          painted.contains(QStringLiteral("Edit")),
                      "the sheet groups its rows under the menus that own them");
                check(!painted.contains(QStringLiteral("Other")),
                      "and every bound action is reachable from a menu, so the "
                      "catch-all group is empty");

                // Painted copy is invisible to the sweeps above, exactly like
                // the guide's and the balloon's, so it gets its own.
                QStringList sheetOffenders;
                for (const QString& text : painted) {
                    for (const QString& word : bannedWords()) {
                        if (text.contains(word, Qt::CaseInsensitive))
                            sheetOffenders << (text + QStringLiteral(" [") + word +
                                               QStringLiteral("]"));
                    }
                }
                check(sheetOffenders.isEmpty(),
                      QStringLiteral("no shortcut sheet text uses a banned word (%1)")
                          .arg(sheetOffenders.isEmpty()
                                   ? QStringLiteral("none")
                                   : sheetOffenders.join(QStringLiteral(", "))));

                // Re-centres on a window resize; it used to stay wherever the
                // window happened to be when it opened. HintBalloon solved
                // this with a filter on its parent and this now does the same.
                window.resize(1100, 760);
                settle(250);
                check(sheet->x() == (window.width() - sheet->width()) / 2 &&
                          sheet->y() == (window.height() - sheet->height()) / 2,
                      QStringLiteral("the sheet re-centres when the window resizes "
                                     "(at %1,%2 in a %3x%4 window)")
                          .arg(sheet->x()).arg(sheet->y())
                          .arg(window.width()).arg(window.height()));
                window.resize(1200, 800);
                settle(250);

                // A click OUTSIDE dismisses it - and must not also fall
                // through and pick in the viewport behind an
                // apparently-modal sheet, which is what it used to do while
                // a click INSIDE was what closed it. clickAt() delivers press
                // and release straight to the viewport, so if either reached
                // it the selection below would be cleared by the pick.
                const int keptId = window.document().solids().front().id;
                view->setSelectedSolids({keptId});
                settle(150);
                check(view->selectedSolidIds().size() == 1,
                      "one body is selected before the click-outside check");
                const QPoint outside(5, view->height() - 5);
                check(sheet->rect().contains(
                          sheet->mapFromGlobal(view->mapToGlobal(outside))) == false,
                      "the click-outside point really is outside the sheet");
                clickAt(view, QPointF(outside));
                settle(200);
                check(!sheet->isVisible(), "a click outside closes the sheet");
                check(view->selectedSolidIds().size() == 1,
                      "and does not fall through to pick in the viewport behind it");

                // Re-open for the Escape check below.
                open->trigger();
                settle(150);
                check(sheet->isVisible(), "the sheet re-opens");

                // A click INSIDE is not a dismissal: you have to hold still to
                // read a list, and clicking one used to close it.
                clickAt(sheet, QPointF(sheet->width() / 2.0, sheet->height() - 8.0));
                settle(150);
                check(sheet->isVisible(), "a click inside leaves the sheet open");
            }

            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(sheet, &escape);
            settle(150);
            check(sheet != nullptr && !sheet->isVisible(), "Escape closes the sheet");
        }
    }

    // --- the walkthrough completes and stays gone ------------------------------
    {
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr && guide->isFinished(),
              "building a body completes the guide");
        check(guide != nullptr && !guide->isVisible(),
              "a finished guide hides itself");
        check(window.progress().hasLearned("walkthrough.done"),
              "finishing records walkthrough.done");

        // A returning user does not see it again.
        MainWindow second(nullptr, /*persistProgress=*/false);
        // Shown (and settled) before anything below reads a widget's
        // position: ViewportOverlay lays overlay widgets out against the
        // viewport's size at the time of each addWidget() call, which
        // happens during MainWindow's constructor - before the constructor's
        // own resize(1280, 800) call near its end, let alone this resize()
        // and show(). The corrected layout for the real size only lands once
        // the resulting resize event is actually processed, which needs a
        // pump of the event loop.
        second.setAttribute(Qt::WA_ShowWithoutActivating);
        second.resize(900, 600);
        second.show();
        settle(300);

        // An earlier version of this check only asserted
        // Qt::WA_TransparentForMouseEvents on the panel and nothing more.
        // That missed that the attribute excludes a widget's ENTIRE SUBTREE
        // from hit-testing, not just the widget carrying it (verified
        // directly against this machine's Qt 6.11.1:
        // QWidgetPrivate::childAtRecursiveHelper `continue`s straight past a
        // transparent widget without descending into its children) - so the
        // skip control, then a child of the panel, was just as unreachable
        // by a real click as the panel's own painted "skip" text was meant
        // to be, even though it answered fine to an event sent straight to
        // it in a test. That is why the skip control is now a sibling of
        // the panel instead (see WalkthroughPanel.cpp), positioned over
        // skipRect() and raised above it, and why this check goes through
        // childAt() - the actual mechanism real hit-testing uses - rather
        // than an attribute flag. gui_smoke's clickAt() sends events
        // straight to a target widget, bypassing childAt() entirely, so it
        // could not have caught this either.
        WalkthroughPanel* secondGuide = second.findChild<WalkthroughPanel*>();
        check(secondGuide != nullptr, "the second window gets its own guide too");
        if (secondGuide) {
            OcctViewWidget* secondView = second.view();

            // skipRect()'s formula, in the panel's own local coordinates:
            // width() - margin - 14 - 34, margin + 8, 34, 18 (see
            // WalkthroughPanel.cpp) - margin being
            // Theme::surfaceShadowMargin(), the room the panel now reserves
            // around its visible card for paintSurface()'s shadow. Its
            // centre, translated into the shared parent's coordinates the
            // way syncSkipGeometry() does, is what a real click on it would
            // land on.
            const int shadowMargin = Theme::surfaceShadowMargin();
            const QPoint skipCentre =
                secondGuide->pos() +
                QPoint(secondGuide->width() - shadowMargin - 31, shadowMargin + 17);
            QWidget* hitSkip = secondView->childAt(skipCentre);
            // "not the panel" alone is the check that let an AxisGizmo
            // mis-hit through once already (see the fix-round report) - it
            // asserts what the bug happened not to violate, not what this
            // is actually supposed to prove. skipControl() gives the real
            // identity to compare against.
            check(hitSkip != nullptr && hitSkip == secondGuide->skipControl(),
                  "childAt() at the skip control's centre finds the skip "
                  "control itself, not some other widget");

            const QPoint insidePanelOutsideSkip =
                secondGuide->pos() + QPoint(20, secondGuide->height() - 20);
            QWidget* hitElsewhere = secondView->childAt(insidePanelOutsideSkip);
            check(hitElsewhere == nullptr,
                  "childAt() at a point inside the panel but well outside skip "
                  "finds neither the panel nor the skip control");

            if (hitSkip && hitSkip == secondGuide->skipControl()) {
                // Toast's UndoControl and the extrude field both carry
                // Qt::WA_NoMousePropagation and this control did not. It
                // accepts the press, which makes it the grab holder, so the
                // RELEASE lands here too - and QWidget's default release
                // handler ignores it, which propagates it to the parent. That
                // parent is the viewport, which performs a real pick on
                // release and unconditionally emits selectionChanged(), so
                // clicking "skip" on first run also selected whatever body sat
                // behind the guide. Counting that signal is the precise test:
                // an emission at all is the leak, whether or not this probe
                // has a body for the pick to land on.
                int picks = 0;
                QObject::connect(secondView, &OcctViewWidget::selectionChanged,
                                 secondView, [&picks] { ++picks; });

                const QPointF centre(hitSkip->width() / 2.0, hitSkip->height() / 2.0);
                QMouseEvent press(QEvent::MouseButtonPress, centre, centre,
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(hitSkip, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, centre, centre,
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(hitSkip, &release);
                settle(120);

                check(secondGuide->isFinished(),
                      "activating the skip control childAt() found finishes the guide");
                check(second.progress().hasLearned("walkthrough.done"),
                      "skipping records walkthrough.done, same as finishing for real");
                check(picks == 0,
                      QStringLiteral("clicking skip does not fall through to a pick in "
                                     "the viewport behind it (%1 selection changes)")
                          .arg(picks));
            }
        }

        // A check used to sit here re-recording walkthrough.done and
        // asserting the panel stayed hidden - but by this point the skip
        // click above has already finished and hidden it, so that assertion
        // passed regardless of whether the returning-user gate actually
        // works. The genuine test of that gate - a window whose progress
        // already says learned BEFORE its panel is ever built, which is the
        // state persistProgress=false can never produce here - lives in the
        // "Show tips again restores the walkthrough for a returning user
        // too" block below instead.
        second.close();
    }

    // --- nothing in the bottom strip lands on top of anything else ------------
    // Three surfaces share the viewport's bottom edge - the guide bottom
    // right, the Snap/Select chip cluster bottom left, and the toast centred
    // between them - and all of them are z-ABOVE the toast after the next
    // relayout(). Two separate defects met here. The toast and the balloon
    // each repositioned from their own filter on the viewport's resize event,
    // which runs BEFORE ViewportOverlay has moved the guide (filters run
    // last-installed-first and the overlay installs its own first), so a
    // shrink placed them against the guide's pre-resize rectangle and the
    // guide then landed on top of them. And the toast only ever stepped
    // around the guide, never the chip cluster, so at 800x500 with the guide
    // up it was pushed left to x~86 and put a third of its message under
    // Snap/Select - reachable on a first run the moment a self-crossing
    // outline raises a failure message.
    {
        MainWindow narrow(nullptr, /*persistProgress=*/false);
        narrow.setAttribute(Qt::WA_ShowWithoutActivating);
        narrow.resize(900, 620);
        narrow.show();
        settle(300);
        narrow.view()->setAnimationsEnabled(false);

        OcctViewWidget* nv = narrow.view();
        // The defect is stated in VIEWPORT pixels, and the items panel eats a
        // couple of hundred of the window's own width, so drive the window
        // until the viewport itself is the size under test. Iterated because
        // the splitter re-proportions the panel as the window shrinks, so one
        // pass does not land it.
        auto resizeViewport = [&](int w, int h) {
            for (int i = 0; i < 5; ++i) {
                const int dw = w - nv->width();
                const int dh = h - nv->height();
                if (dw == 0 && dh == 0) break;
                narrow.resize(narrow.width() + dw, narrow.height() + dh);
                settle(250);
            }
        };
        WalkthroughPanel* guide = narrow.findChild<WalkthroughPanel*>();
        ToastHost* toasts = narrow.findChild<ToastHost*>();
        check(guide != nullptr && guide->isVisible() && toasts != nullptr,
              "the narrow probe starts with a guide up and a toast host");

        if (guide && toasts) {
            toasts->show(QStringLiteral("This outline can't close into a flat face"),
                         Toast::Kind::Failure, false);
            settle(150);
            Toast* toast = narrow.findChild<Toast*>();
            check(toast != nullptr && toast->isVisible(),
                  "a failure message is up alongside the guide");

            // The shrink, to the 800-wide viewport the defect names, at the
            // shortest height this window can actually reach.
            //
            // Regression: the defect this block guards against was found at
            // a LITERAL 800x500, but 500 is no longer a height this window
            // can be shrunk to - MainWindow::buildOverlay() now derives the
            // viewport's minimum height from the rail's own sizeHint(), and
            // that floor (nv->minimumHeight()) is taller than 500. Aiming
            // past a real floor does not skip the collision-avoidance case
            // this test exists for, it just means the shortest viewport
            // really is the floor now - which is also the single most
            // cramped case left to test, so reading the floor here rather
            // than hard-coding 500 keeps this check meaningful instead of
            // quietly aiming at an unreachable size forever.
            const int floorHeight = nv->minimumHeight();
            resizeViewport(800, floorHeight);
            check(nv->width() == 800 && nv->height() == floorHeight,
                  QStringLiteral("the probe really is at the 800-wide viewport's own "
                                 "floor height under test (got %1x%2, floor %3)")
                      .arg(nv->width()).arg(nv->height()).arg(floorHeight));

            if (toast) {
                check(!toast->geometry().intersects(guide->geometry()),
                      QStringLiteral("after a shrink the toast is clear of the guide "
                                     "(viewport %1 wide - toast %2,%3 %4x%5 - guide "
                                     "%6,%7 %8x%9)")
                          .arg(nv->width())
                          .arg(toast->x()).arg(toast->y())
                          .arg(toast->width()).arg(toast->height())
                          .arg(guide->x()).arg(guide->y())
                          .arg(guide->width()).arg(guide->height()));

                QStringList collisions;
                for (ToolCluster* cluster : nv->findChildren<ToolCluster*>()) {
                    if (cluster->isVisible() &&
                        cluster->geometry().intersects(toast->geometry()))
                        collisions << QStringLiteral("%1,%2 %3x%4")
                                          .arg(cluster->x()).arg(cluster->y())
                                          .arg(cluster->width()).arg(cluster->height());
                }
                check(collisions.isEmpty(),
                      QStringLiteral("and clear of every anchored widget too "
                                     "(viewport %1x%2, toast %3,%4 %5x%6; hits: %7)")
                          .arg(nv->width()).arg(nv->height())
                          .arg(toast->x()).arg(toast->y())
                          .arg(toast->width()).arg(toast->height())
                          .arg(collisions.isEmpty() ? QStringLiteral("none")
                                                    : collisions.join(QStringLiteral("; "))));
                check(nv->rect().contains(toast->geometry()),
                      "and still entirely inside the viewport");

                // Non-vacuity for the loop above, now that the bottom-left
                // Snap/Select cluster it was written against has become a
                // rail spanning the WHOLE left edge. The rail is in every
                // horizontal band there is, so the band solver has to raise
                // the toast's floor past it at this width rather than
                // happening to miss it - which is only interesting if the
                // rail really does share the toast's rows.
                if (ToolCluster* rail = nv->findChild<ToolCluster*>()) {
                    const bool sharesTheBand = rail->geometry().top() <= toast->geometry().bottom() &&
                                               rail->geometry().bottom() >= toast->geometry().top();
                    check(sharesTheBand,
                          QStringLiteral("the rail really is in the toast's band, so "
                                         "stepping around it was not a no-op (rail "
                                         "%1,%2 %3x%4)")
                              .arg(rail->x()).arg(rail->y())
                              .arg(rail->width()).arg(rail->height()));
                    check(toast->geometry().left() > rail->geometry().right(),
                          QStringLiteral("and the toast sits clear to its right "
                                         "(toast left %1, rail right %2)")
                              .arg(toast->geometry().left()).arg(rail->geometry().right()));
                }
            }

            // The other half: a guide that appears UNDERNEATH a toast already
            // up. HintBalloon::reconsider() handled that case for itself and
            // ToastHost did not, so Show tips again under a live toast left
            // the restored guide sitting on top of it.
            check(buildBody(narrow, 0.32, 0.32, 0.52, 0.52, 10.0),
                  "a body for the narrow probe, which completes its guide");
            settle(200);
            check(!guide->isVisible(), "the completed guide is out of the way");

            toasts->show(QStringLiteral("This outline can't close into a flat face"),
                         Toast::Kind::Failure, false);
            settle(150);
            Toast* liveToast = narrow.findChild<Toast*>();
            check(liveToast != nullptr && liveToast->isVisible(),
                  "a message is up with no guide beneath it");

            QAction* again = action(narrow, QStringLiteral("Show tips again"));
            check(again != nullptr, "the narrow probe has the reset action");
            if (again && liveToast) {
                again->trigger();
                settle(250);
                check(guide->isVisible(), "the guide comes back under the live toast");
                check(!liveToast->geometry().intersects(guide->geometry()),
                      QStringLiteral("and the toast steps aside for it rather than "
                                     "being buried (toast %1,%2 %3x%4 - guide "
                                     "%5,%6 %7x%8)")
                          .arg(liveToast->x()).arg(liveToast->y())
                          .arg(liveToast->width()).arg(liveToast->height())
                          .arg(guide->x()).arg(guide->y())
                          .arg(guide->width()).arg(guide->height()));
            }

            // The hint balloon has the same obstacle problem as the toast and
            // used to solve it from a shorter list: it named WalkthroughPanel
            // and Toast by type, so it could not see the rail at all. Its
            // step-aside is "go to the LEFT of the obstacle", and to the left
            // of the bottom-right guide at these widths is underneath a rail
            // that now spans the whole left edge - x=8, behind it, with
            // relayout() raising the rail back on top. 600px is inside the
            // 585-640 band where that lands.
            {
                // Same floor-height reasoning as the 800-wide probe above:
                // 500 is no longer reachable, so use the real floor.
                resizeViewport(600, nv->minimumHeight());
                check(buildBody(narrow, 0.30, 0.30, 0.50, 0.50, 10.0),
                      "a second body on the narrow probe, so a hint has a reason to "
                      "be up");
                // The guide is what the balloon steps around, and building
                // that body completed it. Restoring it AFTER the build is
                // what puts both surfaces on screen at once - which is the
                // whole collision, and is exactly the sequence a real user
                // hits when Show tips again lands on a narrow window.
                if (QAction* restore = action(narrow, QStringLiteral("Show tips again"))) {
                    restore->trigger();
                    settle(250);
                }
                const auto narrowSolids = narrow.document().solids();
                HintBalloon* balloon = narrow.findChild<HintBalloon*>();
                ToolCluster* rail = nv->findChild<ToolCluster*>();
                WalkthroughPanel* narrowGuide = narrow.findChild<WalkthroughPanel*>();
                check(balloon != nullptr && rail != nullptr && narrowSolids.size() >= 2 &&
                          narrowGuide != nullptr && narrowGuide->isVisible(),
                      "the narrow probe has a balloon, a rail, a visible guide and two "
                      "bodies");
                if (balloon && rail && narrowGuide && narrowSolids.size() >= 2) {
                    nv->setSelectedSolids({narrowSolids[0].id, narrowSolids[1].id});
                    settle(250);
                    check(balloon->isVisible(),
                          "selecting two bodies raises the boolean hint on the narrow "
                          "viewport");
                    check(balloon->isVisible() &&
                              balloon->geometry().intersects(
                                  QRect(0, balloon->y(), narrowGuide->geometry().right(),
                                        balloon->height())) &&
                              narrowGuide->isVisible(),
                          QStringLiteral("and the guide really is in its way, so stepping "
                                         "aside is not a no-op (guide %1,%2 %3x%4)")
                              .arg(narrowGuide->x()).arg(narrowGuide->y())
                              .arg(narrowGuide->width()).arg(narrowGuide->height()));
                    if (balloon->isVisible()) {
                        check(!balloon->geometry().intersects(rail->geometry()),
                              QStringLiteral("and the balloon steps clear of the rail "
                                             "rather than under it (viewport %1 wide - "
                                             "balloon %2,%3 %4x%5 - rail %6,%7 %8x%9)")
                                  .arg(nv->width())
                                  .arg(balloon->x()).arg(balloon->y())
                                  .arg(balloon->width()).arg(balloon->height())
                                  .arg(rail->x()).arg(rail->y())
                                  .arg(rail->width()).arg(rail->height()));
                        check(!balloon->geometry().intersects(narrowGuide->geometry()),
                              "and still clear of the guide it was already avoiding");
                        check(nv->rect().contains(balloon->geometry()),
                              "and entirely inside the viewport");
                    }
                    nv->setSelectedSolids({});
                    settle(150);
                }
            }

            // ExtrudePreview has the mirror problem at the top edge: it is
            // raised once, at begin(), and relayout() then raises the
            // top-left Items/Undo/Redo cluster back over it, so its field
            // stopped being clickable on a narrow viewport. Overlap is not
            // itself the bug - being underneath it is.
            trigger(narrow, QStringLiteral("Start Sketch"));
            sketchQuad(narrow, 0.30, 0.30, 0.50, 0.50);
            trigger(narrow, QStringLiteral("Finish Sketch"));
            settle(150);
            trigger(narrow, QStringLiteral("Extrude..."));
            settle(200);
            // A resize AFTER the panel opened is what used to bury it.
            //
            // This drove the viewport to 500 while the top-left
            // Items/Undo/Redo cluster was the obstacle the centred panel ran
            // into. The rail replaced it and is only ~40px wide at the far
            // left, so the panel now clears it at every width this window can
            // reach; the obstacle still in the panel's row is the axis gizmo
            // top-right - the very collision ExtrudePreview::reposition()'s
            // own comment names ("the two overlap once viewportWidth drops
            // below 472px"). 440 is a width that produces it, and the check
            // below names whichever anchored widget actually collided rather
            // than assuming, so the probe cannot quietly become vacuous
            // again.
            resizeViewport(440, 500);

            ExtrudePreview* panel = narrow.findChild<ExtrudePreview*>();
            check(panel != nullptr && panel->isVisible() && panel->field() != nullptr,
                  "the extrude panel is open on the narrow viewport");
            if (panel && panel->field()) {
                QWidget* field = panel->field();
                QStringList overlaps;
                for (ToolCluster* cluster : nv->findChildren<ToolCluster*>()) {
                    if (cluster->isVisible() &&
                        cluster->geometry().intersects(panel->geometry()))
                        overlaps << QStringLiteral("the rail");
                }
                if (AxisGizmo* gizmo = nv->findChild<AxisGizmo*>()) {
                    if (gizmo->isVisible() && gizmo->geometry().intersects(panel->geometry()))
                        overlaps << QStringLiteral("the axis gizmo");
                }
                check(!overlaps.isEmpty(),
                      QStringLiteral("the panel really does collide with an anchored "
                                     "widget at this width, so this check is exercising "
                                     "something (viewport %1 wide; hits: %2)")
                          .arg(nv->width())
                          .arg(overlaps.isEmpty() ? QStringLiteral("none")
                                                  : overlaps.join(QStringLiteral(", "))));
                const QPoint centre =
                    field->mapTo(nv, QPoint(field->width() / 2, field->height() / 2));
                QWidget* hit = nv->childAt(centre);
                check(hit == field,
                      QStringLiteral("and a real click still finds its height field, "
                                     "not a cluster raised over it (found %1)")
                          .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                                   : QStringLiteral("nothing")));
            }
            sendKeyTo(&narrow, Qt::Key_Escape);
            settle(150);
        }
        narrow.close();
    }

    // --- the drawer is an obstacle like any other -----------------------------
    // The toast and the balloon both step around whatever ViewportOverlay has
    // anchored, so a floating drawer joins that set for free. "For free" is
    // exactly the kind of claim that is true right up until it is not, and
    // both surfaces are checked here NON-VACUOUSLY: the toast is driven into
    // the drawer's own horizontal band, and the balloon is measured with the
    // drawer open AND closed, so the assertion cannot pass because the drawer
    // happened to be nowhere near it.
    {
        MainWindow probe(nullptr, /*persistProgress=*/false);
        probe.setAttribute(Qt::WA_ShowWithoutActivating);
        probe.resize(900, 640);
        probe.show();
        settle(400);
        probe.view()->setAnimationsEnabled(false);
        OcctViewWidget* pv = probe.view();

        auto resizeViewport = [&](int w, int h) {
            for (int i = 0; i < 6; ++i) {
                const int dw = w - pv->width();
                const int dh = h - pv->height();
                if (dw == 0 && dh == 0) break;
                probe.resize(probe.width() + dw, probe.height() + dh);
                settle(220);
            }
        };

        // The guide shares the bottom strip and would be a second obstacle in
        // every band measured below, which would make the numbers unreadable
        // and the failures ambiguous. Skipping it the way a real user does -
        // a click on its own skip control - leaves the rail and the drawer as
        // the only left-hand obstacles.
        WalkthroughPanel* guide = probe.findChild<WalkthroughPanel*>();
        if (guide && guide->skipControl()) {
            QWidget* skip = guide->skipControl();
            clickAt(skip, QPointF(skip->width() / 2.0, skip->height() / 2.0));
            settle(250);
        }
        check(guide != nullptr && !guide->isVisible(),
              "the guide is skipped, so the drawer and the rail are the only "
              "left-hand obstacles in this probe");

        ItemsPanel* drawer = probe.itemsPanel();
        check(drawer != nullptr, "the obstacle probe has a drawer");

        ToolCluster* rail = pv->findChild<ToolCluster*>();
        ToastHost* toasts = probe.findChild<ToastHost*>();
        HintBalloon* balloon = probe.findChild<HintBalloon*>();
        QAction* items = action(probe, QStringLiteral("Items"));
        check(drawer != nullptr && drawer->isVisible() && rail != nullptr &&
                  toasts != nullptr && balloon != nullptr && items != nullptr,
              "the obstacle probe has a drawer, a rail, a toast host and a balloon");

        if (drawer && rail && toasts && balloon && items) {
            // Regression: MainWindow::buildOverlay() now sets the viewport's
            // own minimum height from the rail's sizeHint(), so "the
            // shortest viewport this window can be shrunk to" is a real
            // floor (pv->minimumHeight()) rather than an arbitrary number
            // this probe used to be able to aim past. Reaching that floor
            // FIRST - before the toast is shown and before the drawer is
            // grown - is what lets everything measured below (the toast's
            // row, how tall the drawer has to get to reach it) be read at
            // the actual worst case instead of at a height that turned out
            // to be unreachable once the rail raised the floor.
            resizeViewport(760, pv->minimumHeight());
            settle(200);
            check(pv->width() == 760 && pv->height() == pv->minimumHeight(),
                  QStringLiteral("the probe reaches its shortest reachable viewport "
                                 "(got %1x%2, floor %3)")
                      .arg(pv->width()).arg(pv->height()).arg(pv->minimumHeight()));

            // Failure, not Note, purely for its longer life: the growth loop
            // below takes over a second and a four-second message could
            // retire mid-probe.
            toasts->show(QStringLiteral("Graphite drawer probe"),
                         Toast::Kind::Failure, false);
            settle(180);
            Toast* toast = probe.findChild<Toast*>();
            check(toast != nullptr && toast->isVisible(),
                  "a message is up for the obstacle probe");

            if (toast) {
                // Bodies until the drawer is tall enough to reach the
                // toast's own band on the shortest viewport this window can
                // actually be shrunk to. Driven off the toast's live
                // geometry rather than a fixed drawer height, so this cannot
                // quietly go vacuous the day either widget's size changes -
                // exactly what happened to the fixed 240px threshold this
                // replaced once the rail's own minimum-height floor pushed
                // the toast further down than 240px could reach.
                int built = 0;
                for (int i = 0; i < 14 &&
                                drawer->geometry().bottom() < toast->geometry().top(); ++i) {
                    const double y0 = 0.10 + i * 0.065;
                    if (buildBody(probe, 0.55, y0, 0.70, y0 + 0.05, 10.0)) ++built;
                    settle(60);
                }
                check(built >= 2 && drawer->geometry().bottom() >= toast->geometry().top(),
                      QStringLiteral("enough bodies to make the drawer reach the toast's "
                                     "band (%1 bodies, %2 rows, drawer bottom %3, toast "
                                     "top %4, hint %5)")
                          .arg(built)
                          .arg(drawer->rowCount())
                          .arg(drawer->geometry().bottom())
                          .arg(toast->geometry().top())
                          .arg(static_cast<QWidget*>(drawer)->sizeHint().height()));
                // The card measures the rows it actually holds. It used to
                // report a sizeHint of 325 while sitting at its 176px
                // empty-state floor with eight bodies listed in it, because
                // a row is hidden until the event loop shows it and
                // QWidgetItem::isEmpty() is isHidden() - so the layout
                // measured the list as empty. Height and hint agreeing is
                // the property that was actually broken.
                // A band, not an equality, and the band is exactly the
                // overlay's device-pixel rounding: it grows an anchored card
                // by up to three pixels so its far edge lands on a whole
                // device pixel. Writing this as == wholeDevicePixels(hint)
                // looked tighter and was wrong - the card re-adjusts itself
                // when its rows change, so at some scales it is sitting at
                // the raw hint and at others at the rounded one, and pinning
                // either exact value fails at the other. The property this
                // check exists for survives both: the card measures the rows
                // it actually holds rather than sitting at its 176px
                // empty-state floor with eight bodies listed in it, which is
                // a ~150px error, not a three-pixel one.
                const int hint = static_cast<QWidget*>(drawer)->sizeHint().height();
                check(drawer->height() >= hint &&
                          drawer->height() <= Theme::wholeDevicePixels(hint),
                      QStringLiteral("and the card's height is the height its own "
                                     "contents ask for (%1 against %2)")
                          .arg(drawer->height()).arg(hint));

                // Drive the toast INTO the drawer's band. The drawer is a
                // top-left card and the toast a bottom-centre one, so on any
                // roomy viewport they never meet and "the toast steps around
                // the drawer" is a check that cannot fail - the resize and
                // growth above are what make it non-vacuous.
                check(pv->width() == 760 && pv->height() == pv->minimumHeight(),
                      QStringLiteral("the probe is still at the width and floor height "
                                     "under test (got %1x%2)")
                          .arg(pv->width()).arg(pv->height()));

                const QRect drawerRect = drawer->geometry();
                const QRect toastRect = toast->geometry();
                check(drawerRect.top() <= toastRect.bottom() &&
                          drawerRect.bottom() >= toastRect.top(),
                      QStringLiteral("the drawer really does share the toast's band, "
                                     "so stepping around it is not a no-op (viewport "
                                     "%1x%2 - drawer %3,%4 %5x%6 - toast %7,%8 %9x%10)")
                          .arg(pv->width()).arg(pv->height())
                          .arg(drawerRect.x()).arg(drawerRect.y())
                          .arg(drawerRect.width()).arg(drawerRect.height())
                          .arg(toastRect.x()).arg(toastRect.y())
                          .arg(toastRect.width()).arg(toastRect.height()));
                check(!toastRect.intersects(drawerRect),
                      "and the toast is clear of it");
                check(toastRect.left() > drawerRect.right(),
                      QStringLiteral("having been pushed past the drawer's right edge, "
                                     "not merely past the rail's (toast left %1, drawer "
                                     "right %2, rail right %3)")
                          .arg(toastRect.left()).arg(drawerRect.right())
                          .arg(rail->geometry().right()));
                check(pv->rect().contains(toastRect),
                      "and still entirely inside the viewport");
            }
            // The message is left up on purpose rather than dismissed: it
            // sits in the bottom band and the balloon rides 90px above it, so
            // it is one more thing the balloon has to be clear of while the
            // readings below are taken.

            // The balloon raises a floor for left-hand obstacles that share
            // its own horizontal band - ToastHost's rule, and now this
            // widget's. Measured three ways, because only the set of three
            // says what the rule is: with the drawer IN the band (it moves
            // the balloon), with the drawer closed (it stops), and on a
            // viewport tall enough that the drawer is nowhere near the
            // balloon's row (it must not move it at all).
            const auto probeSolids = probe.document().solids();
            if (probeSolids.size() >= 2) {
                pv->setSelectedSolids({probeSolids[0].id, probeSolids[1].id});
                settle(250);
            }
            check(balloon->isVisible(), "a hint is up for the obstacle probe");
            if (balloon->isVisible()) {
                const QRect drawerRect = drawer->geometry();
                // Non-vacuity for the whole open/closed comparison below: on
                // this short viewport the drawer genuinely reaches down into
                // the balloon's row, which is the only circumstance in which
                // a band-aware floor lets it move anything.
                check(drawerRect.top() <= balloon->geometry().bottom() &&
                          drawerRect.bottom() >= balloon->geometry().top(),
                      QStringLiteral("the drawer really does share the balloon's band here "
                                     "(drawer %1..%2, balloon %3..%4)")
                          .arg(drawerRect.top()).arg(drawerRect.bottom())
                          .arg(balloon->geometry().top()).arg(balloon->geometry().bottom()));
                check(balloon->x() > drawerRect.right(),
                      QStringLiteral("the balloon clears the open drawer (balloon x=%1, "
                                     "drawer right=%2)")
                          .arg(balloon->x()).arg(drawerRect.right()));
                check(!balloon->geometry().intersects(drawerRect),
                      "and does not overlap it at all");
                check(pv->rect().contains(balloon->geometry()),
                      "and stays inside the viewport");

                const int openX = balloon->x();
                items->trigger();               // close the drawer
                settle(250);
                check(!drawer->isVisible(), "the drawer closed for the second reading");
                check(balloon->x() < openX && balloon->x() < drawerRect.right(),
                      QStringLiteral("and the balloon comes back left once it does - so "
                                     "the drawer, not the rail, was what moved it "
                                     "(open x=%1, closed x=%2, rail right=%3)")
                          .arg(openX).arg(balloon->x()).arg(rail->geometry().right()));
                check(balloon->x() > rail->geometry().right(),
                      "though never back under the rail");

                items->trigger();               // and back open
                settle(250);
                check(drawer->isVisible() && balloon->x() == openX,
                      "reopening it puts the balloon back where it was");

                // The third reading, and the one that says the floor is a
                // BAND rule rather than a wall. A taller viewport moves the
                // balloon's row well below the drawer's bottom edge; a
                // top-left card the balloon can never touch must then stop
                // constraining it entirely, and the balloon returns to
                // centre. Treating every left-hand card as full-height did
                // the opposite: it held the balloon out at the drawer's right
                // edge at any height, which on a narrow window pushed it off
                // the right of the viewport - the case the two neighbouring
                // readings above check with rect().contains() and this one
                // therefore checks too.
                //
                // The target height used to be a flat 560 - a comfortable
                // margin above whatever short floor the window could reach
                // before the rail set a real one. With the drawer now grown
                // tall enough to reach the toast at THAT floor
                // (pv->minimumHeight()), a flat 560 can be only a few
                // pixels above it and no longer comfortable at all. Derived
                // instead from the drawer's own bottom and the balloon's own
                // height, the same way the toast's target further up is, so
                // this reading keeps real clearance rather than sitting on a
                // knife's edge the day either widget's size changes.
                const int clearH = drawerRect.bottom() + balloon->height() + 130;
                resizeViewport(540, clearH);
                settle(300);
                const QRect tallDrawer = drawer->geometry();
                check(pv->width() == 540 && pv->height() == clearH,
                      QStringLiteral("the probe reached the taller viewport "
                                     "(got %1x%2, wanted 540x%3)")
                          .arg(pv->width()).arg(pv->height()).arg(clearH));
                check(tallDrawer.bottom() < balloon->geometry().top(),
                      QStringLiteral("where the drawer is clear of the balloon's band, so "
                                     "a band-aware floor has to ignore it (drawer bottom "
                                     "%1, balloon top %2)")
                          .arg(tallDrawer.bottom()).arg(balloon->geometry().top()));
                check(balloon->x() < tallDrawer.right(),
                      QStringLiteral("so it no longer holds the balloon out past its right "
                                     "edge (balloon x=%1, drawer right=%2)")
                          .arg(balloon->x()).arg(tallDrawer.right()));
                check(balloon->x() > rail->geometry().right(),
                      QStringLiteral("while the rail - a genuine full-height spine, in "
                                     "every band there is - still holds its floor "
                                     "(balloon x=%1, rail right=%2)")
                          .arg(balloon->x()).arg(rail->geometry().right()));
                check(pv->rect().contains(balloon->geometry()),
                      QStringLiteral("and the balloon is entirely inside the viewport "
                                     "(balloon %1,%2 %3x%4 in %5x%6)")
                          .arg(balloon->x()).arg(balloon->y())
                          .arg(balloon->width()).arg(balloon->height())
                          .arg(pv->width()).arg(pv->height()));

                // Saved for the visual check the numbers above stand in for.
                // QWidget::grab() on the window renders the widget tree; the
                // viewport itself paints nothing into it (paintEngine() is
                // null, by design - see CLAUDE.md), but every card floating
                // over it does, at its real position. That is exactly the
                // layout evidence this case needs, and it comes from the
                // same in-process rendering the rest of this file uses
                // rather than from OS-level capture.
                probe.grab().save(outDir + QStringLiteral("/balloon_540_band.png"));
            }
        }
        probe.close();
    }

    // --- one type scale, and focus you can see --------------------------------
    {
        QSet<double> scale;
        for (const QFont& f : {Theme::titleFont(), Theme::bodyFont(),
                               Theme::labelFont(), Theme::badgeFont()}) {
            scale.insert(f.pointSizeF());
        }
        check(scale.size() == 4, "the type scale has four distinct sizes");

        QStringList offenders;
        for (QWidget* w : window.findChildren<QWidget*>()) {
            if (!w->isVisible()) continue;
            if (!scale.contains(w->font().pointSizeF()))
                offenders << (w->metaObject()->className() +
                              QStringLiteral(" @ %1").arg(w->font().pointSizeF()));
        }
        check(offenders.isEmpty(),
              QStringLiteral("every visible widget uses the type scale (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("all do")
                                           : offenders.join(QStringLiteral(", "))));

        // The sweep above skips anything not visible, and the toast, its Undo
        // control, the extrude panel and that panel's field are all hidden
        // whenever it runs - so they are structurally exempt from it however
        // their fonts drift. They inherit bodyFont() today; assert that
        // rather than leave it to chance.
        QStringList exempt;
        auto assertScale = [&](QWidget* w, const QString& name) {
            if (!w) { exempt << name + QStringLiteral(" (missing)"); return; }
            if (!scale.contains(w->font().pointSizeF()))
                exempt << name + QStringLiteral(" @ %1").arg(w->font().pointSizeF());
        };
        Toast* hiddenToast = window.findChild<Toast*>();
        ExtrudePreview* hiddenPreview = window.findChild<ExtrudePreview*>();
        assertScale(hiddenToast, QStringLiteral("Toast"));
        assertScale(hiddenToast ? hiddenToast->undoControl() : nullptr,
                    QStringLiteral("UndoControl"));
        assertScale(hiddenPreview, QStringLiteral("ExtrudePreview"));
        assertScale(hiddenPreview ? hiddenPreview->field() : nullptr,
                    QStringLiteral("ExtrudePreview field"));
        PullArrow* hiddenArrow = window.findChild<PullArrow*>();
        assertScale(hiddenArrow, QStringLiteral("PullArrow"));
        assertScale(hiddenArrow ? hiddenArrow->field() : nullptr,
                    QStringLiteral("PullArrow field"));
        BevelArrow* hiddenBevel = window.findChild<BevelArrow*>();
        assertScale(hiddenBevel, QStringLiteral("BevelArrow"));
        assertScale(hiddenBevel ? hiddenBevel->field() : nullptr,
                    QStringLiteral("BevelArrow field"));
        check(exempt.isEmpty(),
              QStringLiteral("the widgets hidden when that sweep runs use the type "
                             "scale too (%1)")
                  .arg(exempt.isEmpty() ? QStringLiteral("all do")
                                        : exempt.join(QStringLiteral(", "))));

        ToolChip* chip = window.findChild<ToolChip*>();
        check(chip != nullptr, "there is a chip to focus");
        if (chip) {
            const QImage unfocused = chip->grab().toImage();
            chip->setFocus(Qt::TabFocusReason);
            settle(80);
            const QImage focused = chip->grab().toImage();
            check(focused != unfocused, "keyboard focus is visible on a chip");

            // The check above renders whatever this harness's own window can
            // actually produce: WA_ShowWithoutActivating means window is
            // never the OS-active one (gui_smoke must never steal focus from
            // whatever else the user is doing), so window()->isActiveWindow()
            // is false throughout the whole suite and the ring painted above
            // is always the muted branch - see the comment at
            // ToolChip::paintEvent(). The active branch cannot be exercised
            // by rendering without genuinely activating a window, which this
            // suite must not do; checked at the token level instead, since
            // that is what determines whether the two branches would ever
            // look different on a window a real user is actually working in.
            check(Theme::focusRing() != Theme::focusRingMuted(),
                  "the active and muted focus-ring colours are visually distinct");
        }
    }

    // --- Graphite: exact tokens, chip anatomy, and the shared surface ---------
    {
        // A drive-by "cleanup" of the palette must fail loudly - these are
        // the exact Phase 5 values, not incidental ones a refactor could
        // silently drift.
        check(Theme::chip() == QColor(QStringLiteral("#2c2c31")),
              "chip() is the Graphite token");
        check(Theme::gridMinor() == QColor(QStringLiteral("#3e3e44")),
              "gridMinor() is the Graphite token");
        check(Theme::gridMajor() == QColor(QStringLiteral("#4d4d55")),
              "gridMajor() is the Graphite token");
        // Was 3, for a soft shadow ring paintSurface() painted around every
        // card. Fix round 1 removed the shadow entirely: translucent pixels
        // over OCCT's GL surface have nothing behind them in the widget's
        // backing store, so the alpha lands on black - a 3px halo on a chip,
        // and a black band down the viewport once the tool rail was the card.
        // The function stays, returning zero, so every caller's
        // grow-by-this-much arithmetic and the sibling-geometry sync built on
        // it still read as one scheme. Pinned at 0 for the same reason it was
        // pinned at 3: a drive-by "restore the shadow" must fail loudly.
        check(Theme::surfaceShadowMargin() == 0,
              "surfaceShadowMargin() is 0 - the family paints no shadow, so a "
              "card's widget rect and its painted card are the same rectangle");

        // A standalone chip driven by its own QAction, exactly like "chips
        // mirror their action" above - not one of MainWindow's real chips.
        // A real chip's enabled/checked state is policy that
        // MainWindow::updateActions() can recompute at any appStateChanged,
        // which would fight a test that pokes the action directly and could
        // silently overwrite it back before the next grab(). The probe is
        // driven by nothing but this block.
        QAction probe(QStringLiteral("Probe chip"));
        probe.setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
        ToolChip probeChip(&probe, IconSet::Glyph::Sketch);
        probeChip.resize(probeChip.sizeHint());
        const int chipMargin = Theme::surfaceShadowMargin();
        const QRect chipBody =
            probeChip.rect().adjusted(chipMargin, chipMargin, -chipMargin, -chipMargin);

        // normal vs hovered - a real Enter/Leave delivered the way Qt's own
        // hit-testing would, not myHovered flipped by hand. A coarse
        // whole-image sanity net; the pixel probes below are what actually
        // pin each state's specific anatomy.
        const QImage normal = probeChip.grab().toImage();
        const QPointF centre(probeChip.width() / 2.0, probeChip.height() / 2.0);
        QEnterEvent enter(centre, centre, probeChip.mapToGlobal(centre.toPoint()));
        QCoreApplication::sendEvent(&probeChip, &enter);
        settle(50);
        const QImage hovered = probeChip.grab().toImage();
        check(hovered != normal, "hovering a chip repaints it distinctly (chipHover())");
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(&probeChip, &leave);
        settle(50);

        // Pin the border: fix round 1, Important 1. An unchecked, enabled
        // chip's edge pixel must read closer to border() than the chip's
        // own interior fill does. This is what actually catches "dropped
        // the always-on border" - the whole-image diffs above were already
        // green against a chip with no border at all, since hover/disabled/
        // checked all repainted the fill regardless.
        {
            const QImage img = renderExact(&probeChip);
            const QColor edge = img.pixelColor(chipBody.left(), chipBody.center().y());
            const QColor interior = img.pixelColor(chipBody.center());
            check(colorDistance(edge, Theme::border()) < colorDistance(interior, Theme::border()),
                  "an unchecked, enabled chip's edge pixel reads closer to "
                  "border() than its own interior fill does");
        }

        // enabled vs disabled - glyph, label and the shortcut badge (the
        // probe carries one, so it is actually on screen to compare) all dim
        // to textDisabled() together.
        const QImage enabledImg = probeChip.grab().toImage();
        const QImage enabledExact = renderExact(&probeChip);
        probe.setEnabled(false);
        settle(50);
        const QImage disabledImg = probeChip.grab().toImage();
        const QImage disabledExact = renderExact(&probeChip);
        check(disabledImg != enabledImg,
              "a disabled chip repaints distinctly (glyph, label and badge all "
              "dim to textDisabled())");

        // Pin the badge specifically: fix round 1, Important 1. Sampled as
        // an average over the right quarter of the body, not one pixel - a
        // right-aligned shortcut string is mostly background between the
        // letterforms. This is what catches "un-dimmed just the badge",
        // which the whole-chip diff above cannot: the glyph and label
        // dimming alone is enough to make the two images differ.
        {
            const QRect badgeRegion(chipBody.left() + chipBody.width() * 3 / 4,
                                    chipBody.top() + 2, chipBody.width() / 4 - 4,
                                    chipBody.height() - 4);
            const double enabledLum = averageLuminance(enabledExact, badgeRegion);
            const double disabledLum = averageLuminance(disabledExact, badgeRegion);
            check(disabledLum < enabledLum - 3.0,
                  QStringLiteral("the shortcut badge itself dims when the chip "
                                 "disables (enabled avg %1, disabled avg %2)")
                      .arg(enabledLum, 0, 'f', 1)
                      .arg(disabledLum, 0, 'f', 1));
        }
        probe.setEnabled(true);
        settle(50);

        // checked vs unchecked - the inset accent() ring on top of the
        // border() every chip now always carries, not a swapped border.
        probe.setCheckable(true);
        probe.setChecked(false);
        settle(50);
        const QImage uncheckedImg = probeChip.grab().toImage();
        const QImage uncheckedExact = renderExact(&probeChip);
        probe.setChecked(true);
        settle(50);
        const QImage checkedImg = probeChip.grab().toImage();
        const QImage checkedExact = renderExact(&probeChip);
        check(checkedImg != uncheckedImg,
              "a checked chip repaints distinctly (chipActive() fill plus an "
              "inset accent() ring)");

        // Pin the ring itself: fix round 1, Important 1. Sampled 2px in from
        // the left edge - where ToolChip::paintEvent() draws the inset
        // accent() ring - and compared relatively (checked's inset vs
        // unchecked's inset), not against an absolute token: a single
        // antialiased pixel sits too close to a coverage tie to trust in
        // isolation, but the unchecked sample is unambiguously pure fill
        // with zero accent() contribution, which is exactly the baseline
        // this needs.
        {
            const QPoint insetPoint(chipBody.left() + 2, chipBody.center().y());
            const QColor uncheckedInset = uncheckedExact.pixelColor(insetPoint);
            const QColor checkedInset = checkedExact.pixelColor(insetPoint);
            check(colorDistance(checkedInset, Theme::accent()) <
                      colorDistance(uncheckedInset, Theme::accent()),
                  "the checked chip's ring-inset pixel reads far closer to "
                  "accent() than the same inset on an unchecked chip");
        }
        probe.setChecked(false);
        settle(50);

        // --- fix round 1, Important 2 + the mockup regression: pin every
        // card to Theme::paintSurface(), and confirm the two cards that keep
        // their own accent on top of it still show it. ---------------------

        // The guide: its own accent() outline, unconditional, replacing the
        // family's plain border() everywhere on its edge - not merely "the
        // guide is visible", which reverting to the old hand-rolled
        // background would still be.
        QAction* showTipsAgain = action(window, QStringLiteral("Show tips again"));
        check(showTipsAgain != nullptr, "Show tips again exists for the family-surface probes");
        if (showTipsAgain) {
            showTipsAgain->trigger();
            settle(150);
        }
        WalkthroughPanel* guide = window.findChild<WalkthroughPanel*>();
        check(guide != nullptr && guide->isVisible(),
              "the guide is up for the family-surface probe");
        if (guide && guide->isVisible()) {
            const int m = Theme::surfaceShadowMargin();
            const QRect body = guide->rect().adjusted(m, m, -m, -m);
            checkFamilySurface(guide, QPoint(body.left(), body.center().y()),
                               body.adjusted(4, 4, -4, -4), Theme::accent(),
                               QStringLiteral("WalkthroughPanel"));
        }

        // The hint balloon: plain family, no accent of its own. Selecting
        // exactly two bodies raises the boolean-operations hint
        // deterministically - the same trigger HintBalloon::conditionHolds()
        // uses elsewhere in this suite.
        HintBalloon* hint = window.findChild<HintBalloon*>();
        const auto solidsForHint = window.document().solids();
        if (hint && solidsForHint.size() >= 2) {
            view->setSelectedSolids({solidsForHint[0].id, solidsForHint[1].id});
            settle(150);
            check(hint->isVisible() && !hint->currentHint().isEmpty(),
                  "a hint is up for the family-surface probe");
            if (hint->isVisible()) {
                const int m = Theme::surfaceShadowMargin();
                const QRect body = hint->rect().adjusted(m, m, -m, -m);
                checkFamilySurface(hint, QPoint(body.left(), body.center().y()),
                                   body.adjusted(4, 4, -4, -4), Theme::border(),
                                   QStringLiteral("HintBalloon"));
            }
        }

        // The shortcut sheet: plain family too.
        ShortcutSheet* sheet = window.findChild<ShortcutSheet*>();
        check(sheet != nullptr, "there is a shortcut sheet for the family-surface probe");
        if (sheet) {
            sheet->showSheet();
            settle(100);
            check(sheet->isVisible(), "the probe sheet is up");
            if (sheet->isVisible()) {
                const int m = Theme::surfaceShadowMargin();
                const QRect body = sheet->rect().adjusted(m, m, -m, -m);
                checkFamilySurface(sheet, QPoint(body.left(), body.center().y()),
                                   body.adjusted(4, 4, -4, -4), Theme::border(),
                                   QStringLiteral("ShortcutSheet"));
            }
            sheet->hide();
        }

        // The toast: plain family border on the edges away from its own
        // stripe, plus the stripe itself, kind-tinted, restoring the
        // Note/Failure distinction the mockup regression flattened. Sampled
        // at the TOP edge for the family-surface pin, not the left - the
        // left edge carries the stripe on purpose and would read as neither
        // a plain border() nor a coverage bug, just a different accent.
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr, "there is a toast host for the family-surface probe");
        if (toasts) {
            toasts->show(QStringLiteral("Graphite family probe - note"), Toast::Kind::Note, false);
            settle(120);
            Toast* toastWidget = toasts->toast();
            check(toastWidget != nullptr && toastWidget->isVisible(),
                  "the probe Note toast is up");
            if (toastWidget) {
                const int m = Theme::surfaceShadowMargin();
                const QRect body = toastWidget->rect().adjusted(m, m, -m, -m);
                checkFamilySurface(toastWidget, QPoint(body.center().x(), body.top()),
                                   body.adjusted(4, 4, -4, -4),
                                   Theme::border(), QStringLiteral("Toast"));

                // Also saved to disk - the coordinator asked for a magnified
                // capture of the toast alongside the chip cluster, and a
                // toast only exists once real state (an outcome to report)
                // puts it there, unlike the chip clusters which are always
                // on screen. QWidget::render(), the same in-process
                // mechanism every check in this file already uses to drive
                // and inspect the app - never OS-level synthetic input.
                renderExact(toastWidget).save(outDir + QStringLiteral("/toast_note.png"));

                const QImage noteImg = renderExact(toastWidget);
                const QPoint stripePoint(body.left() + 1, body.center().y());
                const QColor noteStripe = noteImg.pixelColor(stripePoint);

                toasts->show(QStringLiteral("Graphite family probe - failure"),
                            Toast::Kind::Failure, false);
                settle(120);
                const QImage failureImg = renderExact(toastWidget);
                failureImg.save(outDir + QStringLiteral("/toast_failure.png"));
                const QColor failureStripe = failureImg.pixelColor(stripePoint);

                check(colorDistance(noteStripe, failureStripe) > 15.0,
                      "a Failure toast's stripe reads as a different colour "
                      "than a Note's");
                check(colorDistance(noteStripe, Theme::accent()) <
                          colorDistance(noteStripe, Theme::textMuted()),
                      "the Note toast's stripe reads closer to accent()");
                check(colorDistance(failureStripe, Theme::danger()) <
                          colorDistance(failureStripe, Theme::accent()),
                      "the Failure toast's stripe reads closer to danger()");
            }
        }

        // ExtrudePreview: fix round 1, Minor. Brought into the family too -
        // plain border() while valid, and its own danger() outline over the
        // shared base while the field's text does not parse, the same
        // pattern as the toast's stripe.
        trigger(window, QStringLiteral("Start Sketch"));
        sketchQuad(window, 0.30, 0.58, 0.42, 0.68);
        trigger(window, QStringLiteral("Finish Sketch"));
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);
        ExtrudePreview* extrudePreview = window.findChild<ExtrudePreview*>();
        check(extrudePreview != nullptr && extrudePreview->isVisible(),
              "a preview is open for the family-surface probe");
        if (extrudePreview && extrudePreview->isVisible()) {
            const int m = Theme::surfaceShadowMargin();
            const QRect body = extrudePreview->rect().adjusted(m, m, -m, -m);
            checkFamilySurface(extrudePreview, QPoint(body.left(), body.center().y()),
                               body.adjusted(4, 4, -4, -4),
                               Theme::border(), QStringLiteral("ExtrudePreview (valid)"));

            // The height field is a SIBLING parented straight to the
            // viewport (see ExtrudePreview.h for why it cannot be a child),
            // so it is invisible to every renderExact() sweep of the panel
            // above - and it sits directly on OCCT's GL surface, where an
            // unpainted pixel is not transparent but whatever the driver
            // left there. It carried `border-radius: 4px`, which leaves
            // exactly four such corners: the black-nub failure mode, on the
            // one control in this shell that had it and no card underneath.
            // Pinned, like every other guard in this file that stands between
            // a probe and its checks: without it the corner sweep below simply
            // does not run, and a sweep that does not run reports clean.
            check(extrudePreview->field() != nullptr,
                  "the preview's height field is there for the corner sweep");
            if (extrudePreview->field()) {
                const QImage fieldImg = renderExact(extrudePreview->field());
                QStringList seeThroughCorners;
                const QPoint corners[4] = {
                    QPoint(0, 0), QPoint(fieldImg.width() - 1, 0),
                    QPoint(0, fieldImg.height() - 1),
                    QPoint(fieldImg.width() - 1, fieldImg.height() - 1)};
                for (const QPoint& c : corners) {
                    if (!fieldImg.rect().contains(c)) continue;
                    if (qAlpha(fieldImg.pixel(c)) != 255)
                        seeThroughCorners << QStringLiteral("%1,%2 alpha %3")
                                                 .arg(c.x()).arg(c.y())
                                                 .arg(qAlpha(fieldImg.pixel(c)));
                }
                check(!fieldImg.isNull() && seeThroughCorners.isEmpty(),
                      QStringLiteral("the extrude field is opaque in all four "
                                     "corners - no rounded nub straight onto the "
                                     "GL surface (%1)")
                          .arg(seeThroughCorners.isEmpty()
                                   ? QStringLiteral("all four solid")
                                   : seeThroughCorners.join(QStringLiteral("; "))));
            }

            // And again for the invalid-state probe, which is a second guard
            // on the same condition and would go quiet just as silently.
            check(extrudePreview->field() != nullptr,
                  "and still there for the invalid-input outline probe");
            if (extrudePreview->field()) {
                extrudePreview->field()->setText(QStringLiteral("abc"));
                settle(150);
                const QImage invalidImg = renderExact(extrudePreview);
                const QColor invalidEdge =
                    invalidImg.pixelColor(body.left(), body.center().y());
                const QColor invalidInterior = invalidImg.pixelColor(body.center());
                check(colorDistance(invalidEdge, Theme::danger()) <
                          colorDistance(invalidInterior, Theme::danger()),
                      "an invalid ExtrudePreview's edge reads closer to "
                      "danger() than its interior does - the overlay painted "
                      "on top of the shared base");

                extrudePreview->field()->setText(QStringLiteral("25"));
                settle(100);
                QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(extrudePreview->field(), &commit);
                settle(200);
            }
        }
    }

    // --- the Appearance panel: every colour and the type scale, opened up ----
    //
    // Ordered LAST among the checks that drive `window`, and it restores
    // Theme::defaultSpec() before it ends. Theme's spec is application-wide
    // state, so a block that left it edited would hand every later check a
    // differently coloured app - and the returning-user block below builds a
    // whole second window.
    {
        QAction* appearance = action(window, QStringLiteral("Appearance..."));
        check(appearance != nullptr, "there is an Appearance action");
        check(appearance != nullptr && appearance->isCheckable(),
              "and it is checkable, so the panel's visibility can be derived from it");

        // The untouched app IS the shipped app. Both halves of that claim are
        // pinned: the spec compares equal to the default one, and a pixel of
        // real painted chrome is captured here to compare against after a
        // reset - an accessor that quietly returned something else would pass
        // the first check and fail the second.
        check(Theme::spec() == Theme::defaultSpec(),
              "an app nobody has themed is wearing exactly defaultSpec()");

        // ALL of them, against literal hex. Three was not a pin: a uniform
        // drift in any of the eighteen unlisted tokens passed both this check
        // and the before/after-reset pixel comparison further down, since
        // that one only proves the app is self-consistent WITHIN this build.
        // Written out so the byte-identity claim survives without a reviewer
        // re-deriving it from the pre-task file. The two highlight entries
        // are OCCT's Quantity_NOC_CYAN1 and _ORANGE, which is what they were
        // before they became tokens.
        {
            const QHash<QString, QString> shipped = {
                {QStringLiteral("chrome"), QStringLiteral("#1b1b1d")},
                {QStringLiteral("panel"), QStringLiteral("#232326")},
                {QStringLiteral("chip"), QStringLiteral("#2c2c31")},
                {QStringLiteral("chipHover"), QStringLiteral("#34343a")},
                {QStringLiteral("chipActive"), QStringLiteral("#3d3d45")},
                {QStringLiteral("accent"), QStringLiteral("#3d7eff")},
                {QStringLiteral("text"), QStringLiteral("#f0f0f0")},
                {QStringLiteral("textMuted"), QStringLiteral("#9a9aa2")},
                {QStringLiteral("textDisabled"), QStringLiteral("#5c5c64")},
                {QStringLiteral("border"), QStringLiteral("#3a3a40")},
                {QStringLiteral("viewport"), QStringLiteral("#45454b")},
                {QStringLiteral("gridMinor"), QStringLiteral("#3e3e44")},
                {QStringLiteral("gridMajor"), QStringLiteral("#4d4d55")},
                {QStringLiteral("axisX"), QStringLiteral("#7a4a4a")},
                {QStringLiteral("axisY"), QStringLiteral("#4a7a4a")},
                {QStringLiteral("sketchPointMarker"), QStringLiteral("#ff4fc3")},
                {QStringLiteral("danger"), QStringLiteral("#e0564a")},
                {QStringLiteral("focusRing"), QStringLiteral("#ffca4a")},
                {QStringLiteral("focusRingMuted"), QStringLiteral("#9f7e2e")},
                {QStringLiteral("highlightHover"), QStringLiteral("#00ffff")},
                {QStringLiteral("highlightSelected"), QStringLiteral("#ffa500")},
            };
            const Theme::Spec shippedSpec = Theme::defaultSpec();
            QStringList drifted;
            int pinned = 0;
            for (const Theme::ColourToken& token : Theme::colourTokens()) {
                if (!shipped.contains(token.id)) {
                    drifted << (token.id + QStringLiteral(" [no pinned value]"));
                    continue;
                }
                ++pinned;
                const QColor expected(shipped.value(token.id));
                const QColor actual = shippedSpec.*(token.member);
                if (actual != expected)
                    drifted << QStringLiteral("%1 %2 != %3")
                                   .arg(token.id, actual.name(), expected.name());
            }
            // Non-vacuity, and it is the half that makes this a pin rather
            // than a list: every token must have been checked, so a token
            // added to Spec without a line above fails here instead of
            // slipping through unexamined.
            check(pinned == Theme::colourTokens().size() &&
                      shipped.size() == Theme::colourTokens().size(),
                  QStringLiteral("every one of the %1 tokens has a pinned shipped value "
                                 "(%2 pinned, %3 listed)")
                      .arg(Theme::colourTokens().size()).arg(pinned).arg(shipped.size()));
            check(drifted.isEmpty(),
                  QStringLiteral("and defaultSpec() is Graphite byte for byte (%1)")
                      .arg(drifted.isEmpty() ? QStringLiteral("all 21 exact")
                                             : drifted.join(QStringLiteral(", "))));
            check(std::fabs(shippedSpec.basePt - 10.0) < 1e-9,
                  QStringLiteral("and the shipped base size is still 10pt (%1)")
                      .arg(shippedSpec.basePt));
        }

        // A CHECKED rail chip, so the inset accent() ring is actually painted.
        // Found by state rather than by index: which chip happens to be
        // checked is MainWindow::updateActions()'s business, not this
        // block's, and an index would go stale the day the rail is reordered.
        ToolChip* checkedChip = nullptr;
        for (ToolChip* candidate : window.findChildren<ToolChip*>()) {
            if (candidate->isChecked() && candidate->isVisible()) { checkedChip = candidate; break; }
        }
        check(checkedChip != nullptr,
              "there is a checked rail chip to sample the accent ring on");

        const QImage graphiteChip = checkedChip ? renderExact(checkedChip) : QImage();
        // ToolChip::paintEvent() draws the inset ring 2px in from the card's
        // left edge - the same point the Graphite anatomy probe samples.
        const QPoint ringPoint(2, checkedChip ? checkedChip->height() / 2 : 0);

        appearance->trigger();
        settle(200);
        AppearancePanel* panel = window.appearancePanel();
        check(panel != nullptr && panel->isVisible(),
              "triggering Appearance opens the panel");
        check(appearance->isChecked(), "and leaves the action checked, which is what shows it");

        if (panel && panel->isVisible()) {
            // Real hit-testing, not an attribute or a pointer comparison:
            // the panel is a child of the viewport, over OCCT's GL surface,
            // and CLAUDE.md's rule is that a control there is only reachable
            // if childAt() actually finds it. Walked up from the hit the way
            // the cluster probes do, since the point lands on a row or a
            // label inside the card rather than on the card itself.
            const QPoint inside = panel->geometry().center();
            QWidget* hit = view->childAt(inside);
            bool reachesPanel = false;
            for (QWidget* w = hit; w; w = w->parentWidget()) {
                if (w == panel) { reachesPanel = true; break; }
                if (w == view) break;
            }
            check(reachesPanel,
                  QStringLiteral("and a real click inside it reaches the panel (found %1)")
                      .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                               : QStringLiteral("nothing")));

            // It is an overlay occupant like every other anchored card, which
            // is the whole of what makes the toast, the balloon and the guide
            // step around it - so its rectangle has to be IN that set.
            bool anchored = false;
            for (const QRect& r : window.findChild<ViewportOverlay*>()->occupiedRects()) {
                if (r == panel->geometry()) anchored = true;
            }
            check(anchored,
                  "and its rectangle is one of ViewportOverlay::occupiedRects(), so "
                  "everything that avoids the anchored cards avoids it too");

            // A member of the floating-surface family, painted opaquely right
            // out to its own edge.
            const QRect body = panel->rect().adjusted(Theme::surfaceShadowMargin(),
                                                      Theme::surfaceShadowMargin(),
                                                      -Theme::surfaceShadowMargin(),
                                                      -Theme::surfaceShadowMargin());
            checkFamilySurface(panel, QPoint(body.left(), body.center().y()),
                               body.adjusted(4, 4, -4, -4), Theme::border(),
                               QStringLiteral("AppearancePanel"));
        }

        // --- one row per token, in the user's words -------------------------
        if (panel) {
            check(panel->colourRowCount() == Theme::colourTokens().size(),
                  QStringLiteral("every editable colour token has a row (%1 rows, %2 tokens)")
                      .arg(panel->colourRowCount()).arg(Theme::colourTokens().size()));
            QStringList nameless;
            for (const Theme::ColourToken& token : Theme::colourTokens()) {
                if (AppearancePanel::nameForToken(token.id).isEmpty()) nameless << token.id;
                if (!panel->swatchFor(token.id)) nameless << token.id + QStringLiteral(" (no swatch)");
            }
            check(nameless.isEmpty(),
                  QStringLiteral("and each one has a user-facing name and a swatch (%1)")
                      .arg(nameless.isEmpty() ? QStringLiteral("all do")
                                              : nameless.join(QStringLiteral(", "))));

            // The panel's copy is QLabel and button text, which neither the
            // action sweep nor the tooltip sweep can see - so it joins the
            // vocabulary check through paintedTexts() like every other
            // painted surface in the shell.
            const QStringList painted = panel->paintedTexts();
            check(painted.size() >= Theme::colourTokens().size() + 3,
                  QStringLiteral("the panel exposes all of its own copy for the sweep "
                                 "(%1 strings)").arg(painted.size()));
            QStringList offenders;
            for (const QString& text : painted) {
                for (const QString& word : bannedWords()) {
                    if (text.contains(word, Qt::CaseInsensitive))
                        offenders << (text + QStringLiteral(" [") + word + QStringLiteral("]"));
                }
                // Code names must not leak either - the whole point of
                // nameForToken() is that `gridMinor` is not what a user is
                // choosing.
                for (const Theme::ColourToken& token : Theme::colourTokens()) {
                    if (text == token.id) offenders << (text + QStringLiteral(" [code name]"));
                }
            }
            check(offenders.isEmpty(),
                  QStringLiteral("no Appearance panel text uses a banned or code word (%1)")
                      .arg(offenders.isEmpty() ? QStringLiteral("none")
                                               : offenders.join(QStringLiteral(", "))));
        }

        // --- the application stylesheet is built from the spec, not frozen --
        //
        // The sheet used to carry every colour as a hex literal, which made it
        // the one place a token's value was written twice. It is substituted
        // from the live spec now, and both halves of that are pinned: no
        // placeholder survives into the installed sheet (a new @token with no
        // substitution entry would otherwise ship as a colour name Qt silently
        // ignores), and the values in it are the ones Theme currently reports.
        {
            const QString sheet = qApp->styleSheet();
            const int leftover = sheet.indexOf(QLatin1Char('@'));
            check(!sheet.isEmpty() && leftover < 0,
                  QStringLiteral("every stylesheet placeholder was substituted (%1)")
                      .arg(leftover >= 0 ? sheet.mid(leftover, 24)
                                         : QStringLiteral("none left")));
            check(sheet.contains(Theme::chrome().name()) &&
                      sheet.contains(Theme::border().name()) &&
                      sheet.contains(Theme::panel().name()),
                  "and it carries the tokens Theme currently reports");
        }

        // --- a theme edit RESTYLES the drawer, it does not rebuild it -------
        //
        // ItemsPanel::applyTheme() used to call refresh(), which tears every
        // row down with deleteLater() and constructs it again. A theme edit
        // arrives once per mouse MOVE inside the colour picker, so that was a
        // full teardown-and-rebuild of the body list per frame of a drag - and
        // nothing about a colour changes WHICH bodies exist.
        //
        // Asserted on the row's VISIBILITY as well as its life, and the first
        // half is the one that bites. A QPointer alone does not: refresh()
        // retires a row with hide() + deleteLater(), and a DeferredDelete
        // posted inside this suite's processEvents() loop is not necessarily
        // collected by the time the next check runs - so the pointer can still
        // be non-null after a genuine teardown. hide() is synchronous, and
        // refresh() calls it FIRST and deliberately (see ItemsPanel::refresh),
        // so a row that is still visible is a row that was never retired.
        // Verified by stubbing applyTheme() back to a refresh(): the pointer
        // check passed, this one failed.
        QPointer<QWidget> survivingRow;
        {
            ItemsPanel* drawer = window.itemsPanel();
            check(drawer != nullptr && drawer->isVisible() && drawer->rowCount() > 0,
                  QStringLiteral("the items drawer is open with rows in it (%1)")
                      .arg(drawer ? drawer->rowCount() : -1));
            if (drawer) {
                // The first VISIBLE row, not simply the first one found.
                // refresh() retires a row with hide() + deleteLater(), and a
                // DeferredDelete posted inside this suite's processEvents()
                // loop is not reliably collected - so after the dozens of
                // document edits above, findChildren() still returns hidden
                // rows from earlier rebuilds. Capturing one of those made this
                // probe read "hidden" no matter what the code did.
                for (QWidget* row :
                     drawer->findChildren<QWidget*>(QStringLiteral("itemsRow"))) {
                    if (row->isVisible()) { survivingRow = row; break; }
                }
            }
            check(!survivingRow.isNull(),
                  "and a live row was captured to watch across the edit");
        }
        // The drawer's width is derived from the type scale now, so the
        // shipped look has to come out at exactly the number it always was -
        // opening the Appearance panel and resetting must not nudge a card
        // the user never asked to move. Captured here at Graphite and checked
        // again after the reset.
        const int graphiteDrawerWidth =
            window.itemsPanel() ? window.itemsPanel()->width() : -1;

        // --- the accent token repaints a rail chip's checked ring -----------
        const QColor probeAccent(QStringLiteral("#12d18e"));   // a green nothing else uses
        if (panel && checkedChip) {
            panel->setTokenColour(QStringLiteral("accent"), probeAccent);
            settle(200);
            check(!survivingRow.isNull() && survivingRow->isVisible(),
                  QStringLiteral("a theme edit restyled the drawer's rows in place - the "
                                 "same row widget is still alive and on screen, not "
                                 "retired and rebuilt (%1)")
                      .arg(survivingRow.isNull() ? QStringLiteral("deleted")
                                                 : (survivingRow->isVisible()
                                                        ? QStringLiteral("intact")
                                                        : QStringLiteral("hidden"))));
            check(Theme::accent() == probeAccent,
                  "setting the accent token through the panel moves Theme::accent()");
            const QImage themedChip = renderExact(checkedChip);
            const QColor before = graphiteChip.pixelColor(ringPoint);
            const QColor after = themedChip.pixelColor(ringPoint);
            check(colorDistance(after, probeAccent) < colorDistance(before, probeAccent),
                  QStringLiteral("and the checked chip's ring pixel now reads as the new "
                                 "accent rather than the old one (was %1, now %2)")
                      .arg(before.name(), after.name()));
            check(colorDistance(after, probeAccent) < 40.0,
                  QStringLiteral("- close enough to be that colour and not merely "
                                 "different (distance %1)")
                      .arg(colorDistance(after, probeAccent), 0, 'f', 1));
        }

        // --- and the one thing on that chip that is genuinely CACHED --------
        //
        // The ring above is painted from Theme inside paintEvent(), so it
        // would follow a spec change even if nothing subscribed to the
        // broadcast at all. The chip's GLYPH would not: IconSet rasterises it
        // into a QPixmap out of text() and textDisabled() once, and a QIcon
        // is pixels rather than a description. This is the check that fails
        // if ToolChip stops re-rasterising it - the shape of "no widget
        // caches a colour across the broadcast" that a repaint cannot fix.
        if (panel && checkedChip) {
            auto nearestTo = [](const QImage& image, const QColor& colour) {
                double best = 1e9;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x)
                        best = std::min(best, colorDistance(image.pixelColor(x, y), colour));
                }
                return best;
            };
            const QColor probeText(QStringLiteral("#ff2d55"));   // a red no token carries
            const double beforeGlyph = nearestTo(renderExact(checkedChip), probeText);
            panel->setTokenColour(QStringLiteral("text"), probeText);
            settle(250);
            const double afterGlyph = nearestTo(renderExact(checkedChip), probeText);
            check(beforeGlyph > 60.0,
                  QStringLiteral("nothing on the chip was that red beforehand, so the "
                                 "check below is not vacuous (nearest %1)")
                      .arg(beforeGlyph, 0, 'f', 1));
            check(afterGlyph < 24.0,
                  QStringLiteral("and the chip's rasterised glyph was rebuilt in the new "
                                 "text colour, not left at the cached one (nearest %1)")
                      .arg(afterGlyph, 0, 'f', 1));
        }

        // --- the viewport token reaches OCCT's own clear colour -------------
        //
        // Sampled from V3d_View::Dump, not from a Qt render: the background
        // is a driver clear colour that no Qt paint event ever touches, so a
        // widget-tree grab could not tell whether it had moved at all.
        {
            const QString before = outDir + QStringLiteral("/appearance_viewport_before.png");
            const QString after = outDir + QStringLiteral("/appearance_viewport_after.png");
            check(view->saveSnapshot(before), "the viewport can be snapshotted before the edit");
            const QImage beforeImg(before);

            const QColor probeViewport(QStringLiteral("#3a0d5c"));   // a deep violet
            if (panel) panel->setTokenColour(QStringLiteral("viewport"), probeViewport);
            settle(300);
            check(view->saveSnapshot(after), "and after it");
            const QImage afterImg(after);

            auto share = [](const QImage& image, const QColor& colour) {
                if (image.isNull()) return -1.0;
                int hits = 0, total = 0;
                for (int y = 0; y < image.height(); y += 3) {
                    for (int x = 0; x < image.width(); x += 3) {
                        ++total;
                        if (colorDistance(image.pixelColor(x, y), colour) < 8.0) ++hits;
                    }
                }
                return total > 0 ? double(hits) / total : -1.0;
            };
            const double beforeShare = share(beforeImg, probeViewport);
            const double afterShare = share(afterImg, probeViewport);
            // Non-vacuity first: a null image samples nothing and reports
            // zero exactly as loudly as a viewport that never changed.
            check(!beforeImg.isNull() && !afterImg.isNull() &&
                      beforeImg.width() > 100 && beforeImg.height() > 100,
                  QStringLiteral("both viewport snapshots are real images to sample "
                                 "(%1x%2)").arg(beforeImg.width()).arg(beforeImg.height()));
            check(beforeShare >= 0.0 && beforeShare < 0.01,
                  QStringLiteral("nothing in the viewport was that violet before the edit "
                                 "(%1%)").arg(beforeShare * 100.0, 0, 'f', 2));
            check(afterShare > 0.30,
                  QStringLiteral("and the 3D area is cleared to it afterwards (%1% of "
                                 "sampled pixels)").arg(afterShare * 100.0, 0, 'f', 1));
        }

        // --- the grid is REBUILT, not merely repainted ----------------------
        //
        // GridRenderer bakes its colours into the vertices it builds and
        // caches on the geometry it built them for, so a theme change that
        // did not invalidate that cache would leave the old palette on screen
        // until the camera crossed a level boundary. Asserted on the OCCT
        // dump, and non-vacuously: "the images differ" alone would pass on a
        // grid that had merely moved.
        {
            const QString before = outDir + QStringLiteral("/appearance_grid_before.png");
            const QString after = outDir + QStringLiteral("/appearance_grid_after.png");
            view->saveSnapshot(before);
            const QImage beforeImg(before);

            const QColor probeGrid(QStringLiteral("#ffd400"));   // a yellow nothing else uses
            if (panel) panel->setTokenColour(QStringLiteral("gridMajor"), probeGrid);
            settle(300);
            view->saveSnapshot(after);
            const QImage afterImg(after);

            auto hits = [](const QImage& image, const QColor& colour) {
                int found = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (colorDistance(image.pixelColor(x, y), colour) < 24.0) ++found;
                    }
                }
                return found;
            };
            const int beforeHits = beforeImg.isNull() ? -1 : hits(beforeImg, probeGrid);
            const int afterHits = afterImg.isNull() ? -1 : hits(afterImg, probeGrid);
            check(beforeHits >= 0 && beforeHits < 50,
                  QStringLiteral("no grid line was that yellow before the edit (%1 px)")
                      .arg(beforeHits));
            check(afterHits > 200,
                  QStringLiteral("and the ground grid is rebuilt in the new colour, not "
                                 "left at the cached one (%1 px)").arg(afterHits));
        }

        // --- a LIVE drag arrow follows the accent too -----------------------
        //
        // PullArrowRenderer bakes Theme::accent() into the AIS object it
        // builds, AND its show() early-outs on an unchanged pose - so an arrow
        // already on screen kept the old accent until the camera happened to
        // move far enough to defeat that cache.
        //
        // Raised through the REAL predicate - a face genuinely selected -
        // rather than by calling showPullArrow() directly. The first version
        // of this probe did call it directly, and the arrow vanished between
        // the two snapshots: onThemeChanged() ends in updateActions(), whose
        // appStateChanged() runs PullArrow::refresh(), which correctly tears
        // down an arrow that no selection is holding up. A probe that has to
        // dodge the feature's own derived visibility is testing something the
        // app never does.
        {
            // A dedicated window with ONE body, so the face is unoccluded and
            // the pick is deterministic - `window` has nine bodies in a camera
            // pose thirty checks of other work have left it in.
            MainWindow arrowProbe(nullptr, /*persistProgress=*/false);
            arrowProbe.setAttribute(Qt::WA_ShowWithoutActivating);
            arrowProbe.resize(1000, 760);
            arrowProbe.show();
            arrowProbe.view()->setAnimationsEnabled(false);
            settle(400);
            OcctViewWidget* probeView = arrowProbe.view();
            check(buildBody(arrowProbe, 0.35, 0.35, 0.60, 0.60, 40.0),
                  "a body is built for the arrow recolour probe");
            trigger(arrowProbe, QStringLiteral("Select Faces"));
            settle(150);

            auto outward = [](const TopoDS_Face& face) {
                gp_Dir n = BRepAdaptor_Surface(face).Plane().Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
                return n;
            };
            const auto probeSolids = arrowProbe.document().solids();
            if (!probeSolids.empty()) {
                for (TopExp_Explorer it(arrowProbe.document().shapeOf(probeSolids.front().id),
                                        TopAbs_FACE);
                     it.More() && !probeView->hasPullArrow(); it.Next()) {
                    const TopoDS_Face candidate = TopoDS::Face(it.Current());
                    if (BRepAdaptor_Surface(candidate).GetType() != GeomAbs_Plane) continue;
                    if (outward(candidate).Z() < 0.9) continue;   // the top face
                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(candidate, props);
                    QPoint at;
                    if (!probeView->projectToScreen(props.CentreOfMass(), at)) continue;
                    if (!probeView->rect().adjusted(40, 40, -40, -40).contains(at)) continue;
                    clickAt(probeView, QPointF(at));
                    settle(200);
                }
            }
            check(probeView->hasPullArrow(),
                  "selecting its top face raises a drag arrow for the recolour probe");

            const QString before = outDir + QStringLiteral("/appearance_arrow_before.png");
            const QString after = outDir + QStringLiteral("/appearance_arrow_after.png");
            probeView->saveSnapshot(before);
            const QImage beforeImg(before);

            const QColor probeArrow(QStringLiteral("#aaff00"));   // a lime nothing else uses
            if (panel) panel->setTokenColour(QStringLiteral("accent"), probeArrow);
            settle(300);
            probeView->saveSnapshot(after);
            const QImage afterImg(after);

            auto hits = [](const QImage& image, const QColor& colour) {
                int found = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (colorDistance(image.pixelColor(x, y), colour) < 60.0) ++found;
                    }
                }
                return found;
            };
            // Counted BOTH ways round, which is what makes this a recolour
            // check rather than a "something is lime somewhere" check: the
            // arrow has to be genuinely visible in the dump wearing the old
            // accent first, and that old accent has to be GONE afterwards.
            // Counting only the new colour would pass just as well against an
            // arrow that was never drawn at all.
            const int oldBefore = beforeImg.isNull() ? -1 : hits(beforeImg, probeAccent);
            const int newBefore = beforeImg.isNull() ? -1 : hits(beforeImg, probeArrow);
            const int oldAfter = afterImg.isNull() ? -1 : hits(afterImg, probeAccent);
            const int newAfter = afterImg.isNull() ? -1 : hits(afterImg, probeArrow);
            check(oldBefore > 60,
                  QStringLiteral("the arrow is genuinely in the 3D dump, wearing the "
                                 "accent it was built with (%1 px)").arg(oldBefore));
            check(newBefore >= 0 && newBefore < 20,
                  QStringLiteral("and nothing there was the new lime beforehand (%1 px)")
                      .arg(newBefore));
            check(newAfter > 60 && oldAfter < 20,
                  QStringLiteral("and the live arrow was rebuilt in the new accent rather "
                                 "than left at the one it was drawn with (lime %1 px, "
                                 "old accent %2 px)").arg(newAfter).arg(oldAfter));
            arrowProbe.close();
            settle(200);
        }

        // --- the type scale follows the base size ---------------------------
        {
            const double base = 13.0;
            if (panel) panel->setBaseSize(base);
            settle(200);
            check(std::fabs(Theme::bodyFont().pointSizeF() - base) < 1e-9,
                  QStringLiteral("bodyFont() follows the base size (%1)")
                      .arg(Theme::bodyFont().pointSizeF()));
            check(std::fabs(Theme::badgeFont().pointSizeF() - (base - 2.0)) < 1e-9 &&
                      std::fabs(Theme::labelFont().pointSizeF() - (base - 1.0)) < 1e-9 &&
                      std::fabs(Theme::titleFont().pointSizeF() - (base + 3.0)) < 1e-9,
                  QStringLiteral("and all four derive at their fixed offsets "
                                 "(%1 / %2 / %3 / %4)")
                      .arg(Theme::badgeFont().pointSizeF())
                      .arg(Theme::labelFont().pointSizeF())
                      .arg(Theme::bodyFont().pointSizeF())
                      .arg(Theme::titleFont().pointSizeF()));
            QSet<double> moved;
            for (const QFont& f : {Theme::titleFont(), Theme::bodyFont(),
                                   Theme::labelFont(), Theme::badgeFont()})
                moved.insert(f.pointSizeF());
            check(moved.size() == 4, "and the four are still four distinct sizes");

            // The type-scale LAW, re-run at a base size nobody shipped. The
            // sweep further up runs at 10pt, where a widget that had frozen
            // its font at construction would still be inside the scale by
            // accident - which is precisely the failure this task could
            // introduce and that sweep could not see.
            QStringList strays;
            for (QWidget* w : window.findChildren<QWidget*>()) {
                if (!w->isVisible()) continue;
                if (!moved.contains(w->font().pointSizeF()))
                    strays << (w->metaObject()->className() +
                               QStringLiteral(" @ %1").arg(w->font().pointSizeF()));
            }
            check(strays.isEmpty(),
                  QStringLiteral("every visible widget still uses the type scale at a "
                                 "non-default base size (%1)")
                      .arg(strays.isEmpty() ? QStringLiteral("all do")
                                            : strays.join(QStringLiteral(", "))));

            // The hidden four the main sweep is structurally blind to, at the
            // new size too - each of them sets its field's font explicitly,
            // and an explicit font does not follow QApplication::setFont.
            QStringList hidden;
            auto assertMoved = [&](QWidget* w, const QString& name) {
                if (!w) { hidden << name + QStringLiteral(" (missing)"); return; }
                if (!moved.contains(w->font().pointSizeF()))
                    hidden << name + QStringLiteral(" @ %1").arg(w->font().pointSizeF());
            };
            ExtrudePreview* themedPreview = window.findChild<ExtrudePreview*>();
            PullArrow* themedPull = window.findChild<PullArrow*>();
            BevelArrow* themedBevel = window.findChild<BevelArrow*>();
            assertMoved(themedPreview ? themedPreview->field() : nullptr,
                        QStringLiteral("ExtrudePreview field"));
            assertMoved(themedPull ? themedPull->field() : nullptr,
                        QStringLiteral("PullArrow field"));
            assertMoved(themedBevel ? themedBevel->field() : nullptr,
                        QStringLiteral("BevelArrow field"));
            check(hidden.isEmpty(),
                  QStringLiteral("and the hidden fields that set their own font followed "
                                 "it too (%1)")
                      .arg(hidden.isEmpty() ? QStringLiteral("all did")
                                            : hidden.join(QStringLiteral(", "))));
        }

        // --- a capture of the app actually wearing an edited spec -----------
        //
        // Taken here rather than after the reset, and it is the evidence the
        // numeric checks above stand in for: by this point the accent, the
        // text colour, the viewport, the grid and the base size have all been
        // moved, so a shell that had NOT re-dressed would be obvious in it.
        // Swept for unpainted black rows like every other capture - a
        // resized-by-the-type-scale card is exactly the sort that starts
        // landing on a fractional device row.
        if (panel && panel->isVisible()) {
            const QImage themed = printWindowCapture(
                &window, outDir + QStringLiteral("/appearance_themed.png"));
            checkNoBlackLine(themed, QStringLiteral("edited-appearance"));
        }

        // --- the same class, at the fractional scales this run is not at ----
        //
        // The capture above is the strongest evidence there is and it is
        // evidence at exactly ONE display scale - whatever QT_SCALE_FACTOR
        // this process was started with, which is fixed before QApplication
        // exists and cannot be changed for a second window. The defect it
        // caught proves why that is not enough: with the type scale edited,
        // the status bar came out 26 logical rows tall, the viewport's bottom
        // edge landed on device row 1354.5 at 175%, and a 2068-device-pixel
        // 0,0,0 line ran the full width of the window between the two. At
        // 150% the same 26 rows are a whole 39 and the capture is spotless.
        // A suite that only ever ran here would have called it clean.
        //
        // So the ratio is forced in ARITHMETIC. The seam between the viewport
        // and each chrome strip sits at the viewport's top and bottom edge in
        // window coordinates; the device row it lands on is that edge times
        // the ratio, and it has to be WHOLE at every quarter step Windows
        // offers - or Qt flushes a row that is inside neither widget's logical
        // rect, nothing paints it, and over the GL surface an unpainted row is
        // black. Same property Theme::wholeDevicePixels() promises for a
        // floating card; MainWindow::syncChromeHeights() is what now holds it
        // for the two strips that span the window.
        //
        // Run HERE, on the edited spec, because the default type scale happens
        // to give both strips a whole height - which is exactly how this
        // shipped.
        {
            const QPoint viewportTopLeft = view->mapTo(&window, QPoint(0, 0));
            const int seams[2] = {viewportTopLeft.y(),
                                  viewportTopLeft.y() + view->height()};
            const double ratios[] = {1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 2.75, 3.0};
            QStringList fractional;
            int probed = 0;
            for (const double ratio : ratios) {
                for (const int seam : seams) {
                    ++probed;
                    const double device = seam * ratio;
                    if (std::fabs(device - std::round(device)) > 1.0e-9) {
                        fractional << QStringLiteral("row %1 at %2x -> %3")
                                          .arg(seam).arg(ratio).arg(device);
                    }
                }
            }
            // Non-vacuity first, the rule this file already keeps for every
            // sweep: a loop that ran zero times reports clean exactly as
            // loudly as a clean window does.
            check(probed == 16,
                  QStringLiteral("the themed seam sweep really ran at all eight "
                                 "fractional scales, both edges (%1 probes)")
                      .arg(probed));
            check(fractional.isEmpty(),
                  QStringLiteral("and with the type scale edited the viewport's top and "
                                 "bottom edges land on whole device rows at every one of "
                                 "them - no unpainted line between the chrome and the "
                                 "3D area (top %1, bottom %2%3)")
                      .arg(seams[0]).arg(seams[1])
                      .arg(fractional.isEmpty()
                               ? QString()
                               : QStringLiteral("; ") +
                                     fractional.join(QStringLiteral(", "))));
        }

        // --- the picker opens without blocking -------------------------------
        if (panel) {
            // The FIRST token's swatch, not a hand-picked one: the rows live
            // in a scroll area, and a swatch far enough down the list is
            // genuinely clipped out of the card - childAt() finds nothing
            // there, correctly, because a user cannot click it either without
            // scrolling first. Read from the same table that orders the rows,
            // so this cannot go stale when the order changes.
            const QString topId = Theme::colourTokens().first().id;
            QWidget* swatch = panel->swatchFor(topId);
            check(swatch != nullptr && swatch->isVisible(),
                  QStringLiteral("the %1 swatch is on screen")
                      .arg(AppearancePanel::nameForToken(topId)));
            if (swatch) {
                // Reached the way a user reaches it - through the viewport's
                // own hit-testing - rather than by sending an event straight
                // at the pointer this block happens to hold. CLAUDE.md's rule
                // for anything interactive over the GL surface.
                const QPoint inView = swatch->mapTo(view, swatch->rect().center());
                QWidget* hit = view->childAt(inView);
                check(hit == swatch,
                      QStringLiteral("and a real click at its centre finds the swatch "
                                     "itself (found %1)")
                          .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                                   : QStringLiteral("nothing")));
                clickAt(swatch, swatch->rect().center());
                settle(200);
                QColorDialog* picker = panel->activeColourDialog();
                check(picker != nullptr, "clicking a swatch opens a colour picker");

                // Two properties, and the second is the one an earlier round
                // got wrong. show() returns immediately, so the suite REACHES
                // this line with the picker up - exec() would never have
                // returned and the run would hang here rather than fail,
                // which is the honest backstop CLAUDE.md already records for
                // the outcome dialogs. And the picker is genuinely NOT MODAL:
                // QDialog::open() forces Qt::WindowModal past setModal(false),
                // which locked the rail and the viewport while a colour was
                // being chosen, so this asserts isModal() outright rather
                // than the weaker "not application-modal" that a window-modal
                // dialog also satisfies.
                check(picker != nullptr && picker->isVisible(),
                      "and it returned with the picker on screen rather than exec()'s "
                      "nested loop swallowing this run");
                check(picker != nullptr && !picker->isModal() &&
                          picker->windowModality() == Qt::NonModal,
                      QStringLiteral("and it is genuinely modeless - nothing is blocked "
                                     "while a colour is chosen (modality %1)")
                          .arg(picker ? int(picker->windowModality()) : -1));

                // The shell behind the picker is still there and still wired:
                // hit-testable, and its action fires when an event reaches it.
                //
                // HONEST LIMIT, stated because the obvious reading is wrong:
                // this does NOT prove the app is unblocked. Qt enforces
                // modality inside QApplication::notify()'s spontaneous-event
                // path, and this suite delivers events with sendEvent()
                // straight to the widget - so these two pass against a
                // window-modal picker as well (measured: they did). The
                // isModal() assertion above is what carries that claim; this
                // pair guards the wiring underneath it.
                if (checkedChip && picker) {
                    QAction* chipAction = checkedChip->action();
                    const QPoint railPoint =
                        checkedChip->mapTo(view, checkedChip->rect().center());
                    check(view->childAt(railPoint) == checkedChip,
                          "a rail chip is still hit-testable behind the open picker");
                    if (chipAction && chipAction->isCheckable()) {
                        const bool was = chipAction->isChecked();
                        clickAt(checkedChip, checkedChip->rect().center());
                        settle(150);
                        check(chipAction->isChecked() != was,
                              "and a click delivered to it still toggles its action - "
                              "the shell behind the picker is wired, not torn down");
                        // Put it back: the rest of this block, and every
                        // block after it, expects the shell it found.
                        clickAt(checkedChip, checkedChip->rect().center());
                        settle(150);
                        check(chipAction->isChecked() == was,
                              "and it is restored to the state the suite found it in");
                    }
                }
                if (picker) {
                    // Closed here, deliberately: the suite's "no dialog is
                    // ever constructed for an outcome" checks are only
                    // meaningful while nothing leaves one lying around.
                    picker->close();
                    settle(200);
                }
                check(window.findChildren<QDialog*>().isEmpty(),
                      "and closing it leaves no dialog behind");
            }
        }

        // --- serialise, and refuse garbage ----------------------------------
        {
            const QString stored = Theme::serializeSpec();
            Theme::Spec round;
            check(Theme::deserializeSpec(stored, round),
                  "a serialised spec reads back");
            check(round == Theme::spec(),
                  "and round-trips to exactly the spec it came from");

            // Tolerant on an unknown KEY - that is how a token can be removed
            // in a later build without stranding everyone's stored appearance.
            Theme::Spec tolerant;
            check(Theme::deserializeSpec(
                      QStringLiteral("accent=#ff0000;somethingElse=#00ff00;base=11"), tolerant) &&
                      tolerant.accent == QColor(QStringLiteral("#ff0000")) &&
                      std::fabs(tolerant.basePt - 11.0) < 1e-9,
                  "an unrecognised token is ignored rather than refused");
            check(tolerant.viewport == Theme::defaultSpec().viewport,
                  "and a token the string never mentions comes back at its default");

            // Garbage is refused, and `out` is left ALONE - a deserialize that
            // half-filled its output on the way to failing would be worse than
            // one that failed loudly.
            Theme::Spec sentinel;
            sentinel.accent = QColor(QStringLiteral("#010203"));
            const Theme::Spec untouched = sentinel;
            check(!Theme::deserializeSpec(QStringLiteral(""), sentinel),
                  "an empty string is refused");
            check(!Theme::deserializeSpec(QStringLiteral("not a spec at all"), sentinel),
                  "so is a string with no key=value fragment");
            check(!Theme::deserializeSpec(QStringLiteral("accent=notacolour"), sentinel),
                  "so is a colour QColor cannot parse");
            check(!Theme::deserializeSpec(QStringLiteral("base=40"), sentinel),
                  "so is a base size outside the 8-14 pt band");
            check(sentinel == untouched,
                  "and every one of those refusals left the output untouched");

            // A family this machine does not have FALLS BACK rather than
            // being taken or refused - see Theme.h. Taken, Qt's matcher would
            // silently substitute something and the panel's combo would name
            // a family it does not list; refused, a spec carried from a
            // machine with one extra font installed would cost the user every
            // colour in it.
            Theme::Spec missingFont;
            check(Theme::deserializeSpec(
                      QStringLiteral("accent=#ff0000;family=NoSuchFamily Ultra Expanded"),
                      missingFont),
                  "a spec naming an uninstalled font still reads back");
            check(missingFont.fontFamily == Theme::defaultSpec().fontFamily,
                  QStringLiteral("with the family fallen back to the shipped one (\"%1\")")
                      .arg(missingFont.fontFamily));
            check(missingFont.accent == QColor(QStringLiteral("#ff0000")),
                  "and its colours intact - a missing font costs the font, not the spec");
            // The installed case still round-trips, so the guard is a filter
            // rather than a blanket.
            Theme::Spec realFont;
            check(Theme::deserializeSpec(
                      QStringLiteral("family=") + Theme::defaultSpec().fontFamily, realFont) &&
                      realFont.fontFamily == Theme::defaultSpec().fontFamily,
                  "while a family that IS installed is taken as given");
        }

        // --- reset restores Graphite exactly ---------------------------------
        if (panel) {
            check(Theme::spec() != Theme::defaultSpec(),
                  "the app is genuinely themed before the reset, so the check below "
                  "is not vacuous");
            QWidget* reset = panel->resetButton();
            check(reset != nullptr && reset->isVisible(), "the reset control is on screen");
            if (reset) {
                clickAt(reset, reset->rect().center());
                settle(250);
            }
            check(Theme::spec() == Theme::defaultSpec(),
                  "Reset restores defaultSpec() exactly");
            check(graphiteDrawerWidth > 0 && window.itemsPanel() &&
                      window.itemsPanel()->width() == graphiteDrawerWidth,
                  QStringLiteral("and the items drawer is back at exactly the width it "
                                 "had before any of this - a type-derived width still "
                                 "resolves to the shipped number at the shipped scale "
                                 "(%1 vs %2)")
                      .arg(window.itemsPanel() ? window.itemsPanel()->width() : -1)
                      .arg(graphiteDrawerWidth));
            // The panel is a VIEW of the live spec, not a second copy of it -
            // so a reset it did not itself perform still leaves its own
            // controls showing what the app is actually wearing. Both
            // directions of that binding are asserted: the size the panel had
            // pushed out is back at the default, and the family combo names
            // the family Theme reports.
            check(panel->sizeControl() != nullptr &&
                      std::fabs(panel->sizeControl()->value() - Theme::spec().basePt) < 1e-9,
                  QStringLiteral("and the panel's own size control followed it back "
                                 "(showing %1)")
                      .arg(panel->sizeControl() ? panel->sizeControl()->value() : -1));
            check(panel->familyControl() != nullptr &&
                      panel->familyControl()->currentText() == Theme::spec().fontFamily,
                  QStringLiteral("and its family control names the family Theme reports "
                                 "(\"%1\" vs \"%2\")")
                      .arg(panel->familyControl() ? panel->familyControl()->currentText()
                                                  : QString(),
                           Theme::spec().fontFamily));
            if (checkedChip) {
                const QImage restored = renderExact(checkedChip);
                check(!graphiteChip.isNull() && restored == graphiteChip,
                      "and the rail chip renders pixel-identically to the way it did "
                      "before a single token was touched");
            }
        }

        // --- a capture of the panel, and the black-hairline sweep -----------
        if (panel && panel->isVisible()) {
            const QImage shot = printWindowCapture(
                &window, outDir + QStringLiteral("/appearance_panel.png"));
            checkNoBlackLine(shot, QStringLiteral("Appearance panel"));
        }

        // --- persistProgress=false writes nothing ---------------------------
        {
            // Under a scoped, file-backed QSettings identity, so this probe
            // can look at what was written without ever reading or touching
            // the developer's real store.
            ScopedTestSettings scopedSettings;
            {
                QSettings clean;
                clean.remove(QStringLiteral("appearance"));
            }

            MainWindow quiet(nullptr, /*persistProgress=*/false);
            quiet.setAttribute(Qt::WA_ShowWithoutActivating);
            quiet.resize(900, 700);
            quiet.show();
            settle(300);
            AppearancePanel* quietPanel = quiet.appearancePanel();
            check(quietPanel != nullptr, "the probe window has an Appearance panel too");
            if (quietPanel) {
                quietPanel->setTokenColour(QStringLiteral("accent"),
                                           QColor(QStringLiteral("#ff00ff")));
                settle(200);
                check(Theme::accent() == QColor(QStringLiteral("#ff00ff")),
                      "a persistProgress=false window still applies a theme edit");
            }
            settle(MainWindow::kAppearanceWriteMs * 2);
            {
                QSettings after;
                check(!after.contains(QStringLiteral("appearance")),
                      QStringLiteral("but writes nothing to the store, even well past "
                                     "the debounce (%1)")
                          .arg(after.value(QStringLiteral("appearance")).toString()));
            }

            // And the persisting path DOES write - the check above is only
            // worth having if the guard is the reason nothing was stored,
            // rather than the write never happening at all. This is also
            // where the DEBOUNCE is pinned: a colour picker emits
            // currentColorChanged per mouse move, and a write-through would
            // put a registry write and a file sync on every frame of a drag.
            {
                MainWindow persisting(nullptr, /*persistProgress=*/true);
                persisting.setAttribute(Qt::WA_ShowWithoutActivating);
                persisting.resize(900, 700);
                persisting.show();
                settle(300);

                // A burst, the way a drag through the colour wheel arrives.
                const QColor burst[3] = {QColor(QStringLiteral("#112233")),
                                         QColor(QStringLiteral("#445566")),
                                         QColor(QStringLiteral("#00c2ff"))};
                for (const QColor& c : burst) {
                    if (persisting.appearancePanel())
                        persisting.appearancePanel()->setTokenColour(QStringLiteral("accent"), c);
                    settle(60);
                }
                check(Theme::accent() == burst[2],
                      "a burst of edits applies live, every one of them");
                {
                    QSettings midBurst;
                    check(!midBurst.contains(QStringLiteral("appearance")),
                          "and nothing has been written yet - the store is not touched "
                          "once per edit");
                }

                settle(MainWindow::kAppearanceWriteMs * 2);
                QSettings written;
                Theme::Spec readBack;
                check(Theme::deserializeSpec(
                          written.value(QStringLiteral("appearance")).toString(), readBack) &&
                          readBack.accent == burst[2],
                      "and once the edits stop, one write stores the spec that survived");
                persisting.close();
            }
            quiet.close();
            settle(150);
        }

        // Back to Graphite for everything that follows, and the action back to
        // unchecked so the shell is as the next block expects to find it.
        Theme::setSpec(Theme::defaultSpec());
        if (appearance && appearance->isChecked()) appearance->trigger();
        settle(200);
        check(Theme::spec() == Theme::defaultSpec() && panel && !panel->isVisible(),
              "the appearance block leaves the app back at Graphite with the panel closed");
    }

    // --- the picture the whole item is for -----------------------------------
    // A populated document seen dead on in a parallel projection: an
    // ELEVATION, the drawing a furniture maker actually works from. The same
    // scene is captured in perspective at the same pose immediately after, so
    // the pair shows the one difference and nothing else.
    //
    // Front rather than the angled pose, because convergence is hardest to
    // argue about when it is the vertical edges of a body that either stay
    // parallel or do not - the ground grid tells the same story, but a body
    // is what the user asked to see.
    {
        QAction* orthoForShot = action(window, QStringLiteral("Orthographic"));
        check(!window.document().solids().empty(),
              "the document still holds bodies for the elevation capture");
        if (orthoForShot && !window.document().solids().empty()) {
            view->setSelectedSolids({});
            trigger(window, QStringLiteral("Front"));
            settle(300);
            trigger(window, QStringLiteral("Fit All"));
            settle(300);

            if (!orthoForShot->isChecked()) orthoForShot->trigger();
            settle(250);
            check(view->viewIsOrthographic(),
                  "the elevation capture really is taken in a parallel projection");
            view->saveSnapshot(outDir + "/j-elevation-ortho.png");

            orthoForShot->trigger();
            settle(250);
            check(!view->viewIsOrthographic(),
                  "and its perspective twin at the very same pose");
            view->saveSnapshot(outDir + "/j-elevation-persp.png");

            // Back to the startup pose for whatever follows, exactly as the
            // gizmo block restores its own.
            view->camera().setTemporaryOrtho(false);
            trigger(window, QStringLiteral("Axonometric"));
            settle(250);
        }
    }

    // --- the base projection is a preference, and it comes back --------------
    // On exactly the terms the display unit and the theme spec are on: written
    // only under persistProgress, read once in the constructor, and applied
    // before the first frame is drawn. Both halves are checked, because a
    // preference that is stored and never read looks identical to one that was
    // never stored, and the guard is only worth having if the write it
    // suppresses actually happens without it.
    {
        ScopedTestSettings scopedSettings;
        {
            QSettings clean;
            clean.remove(QStringLiteral("projection"));
        }

        // A window that must not write.
        {
            MainWindow quiet(nullptr, /*persistProgress=*/false);
            quiet.setAttribute(Qt::WA_ShowWithoutActivating);
            quiet.resize(900, 700);
            quiet.show();
            settle(250);
            QAction* quietOrtho = action(quiet, QStringLiteral("Orthographic"));
            check(quietOrtho != nullptr && !quietOrtho->isChecked(),
                  "a fresh window starts in the perspective the app ships with");
            if (quietOrtho) quietOrtho->trigger();
            settle(200);
            check(quietOrtho != nullptr && quietOrtho->isChecked(),
                  "and a persistProgress=false window still applies the flip");
            {
                QSettings after;
                check(!after.contains(QStringLiteral("projection")),
                      QStringLiteral("but stores nothing (\"%1\")")
                          .arg(after.value(QStringLiteral("projection")).toString()));
            }
            quiet.close();
            settle(120);
        }

        // ...and one that must, so the check above is about the guard rather
        // than about a write that never happens at all.
        {
            MainWindow persisting(nullptr, /*persistProgress=*/true);
            persisting.setAttribute(Qt::WA_ShowWithoutActivating);
            persisting.resize(900, 700);
            persisting.show();
            settle(250);
            QAction* orthoAgain = action(persisting, QStringLiteral("Orthographic"));
            if (orthoAgain) orthoAgain->trigger();
            settle(200);
            QSettings written;
            check(written.value(QStringLiteral("projection")).toString() ==
                      QStringLiteral("ortho"),
                  QStringLiteral("a persisting window stores the base projection (\"%1\")")
                      .arg(written.value(QStringLiteral("projection")).toString()));
            persisting.close();
            settle(120);
        }

        // The returning user: the stored mode is on the camera before the
        // window has drawn anything, which is the half a write-only check
        // cannot see. The LIVE camera is the oracle, not the action's tick -
        // an action that came back checked over a perspective viewport is
        // precisely the failure this reads for.
        {
            MainWindow returning(nullptr, /*persistProgress=*/true);
            returning.setAttribute(Qt::WA_ShowWithoutActivating);
            returning.resize(900, 700);
            returning.show();
            settle(300);
            QAction* returnedOrtho = action(returning, QStringLiteral("Orthographic"));
            check(returnedOrtho != nullptr && returnedOrtho->isChecked(),
                  "a returning window comes back with Orthographic checked");
            check(returning.view() != nullptr && returning.view()->viewIsOrthographic(),
                  "and the camera is actually drawing that way, not merely ticked");
            returning.close();
            settle(120);
        }
    }

    // --- Show tips again restores the walkthrough for a returning user too ---
    {
        // Every walkthrough check above uses persistProgress=false, so
        // hasLearned() is always false at the moment buildOverlay() runs -
        // none of them can exercise the actual returning-user path, where
        // MainWindow's constructor deserializes progress from QSettings
        // BEFORE buildOverlay() ever runs. That is the path Show tips again
        // exists for: someone who quit, came back, and wants the guide
        // again. ScopedTestSettings switches to a dedicated, file-backed
        // QSettings identity for this block and guarantees the restore on
        // the way out, however the scope ends.
        ScopedTestSettings scopedSettings;

        UserProgress seed;
        for (int i = 0; i < UserProgress::kLearnedThreshold; ++i) seed.record("walkthrough.done");
        {
            QSettings seedSettings;
            seedSettings.setValue(QStringLiteral("progress"),
                                  QString::fromStdString(seed.serialize()));
        }

        MainWindow returning(nullptr, /*persistProgress=*/true);
        WalkthroughPanel* returningGuide = returning.findChild<WalkthroughPanel*>();
        check(returningGuide != nullptr,
              "a returning user still gets a panel built, just hidden");
        check(returningGuide != nullptr && returningGuide->isFinished(),
              "and it already knows it is finished before ever being shown");

        returning.setAttribute(Qt::WA_ShowWithoutActivating);
        returning.resize(900, 600);
        returning.show();
        settle(300);
        check(returningGuide != nullptr && !returningGuide->isVisible(),
              "the panel stays hidden even once the window is shown - progress "
              "was already learned before it was built");

        // The panel being hidden is not the whole story. Its skip control is
        // a SIBLING, kept in step through the panel's show/hide/move events -
        // and on this exact path refresh() calls hide() on a panel that was
        // never shown, for which Qt delivers no QHideEvent at all. Without an
        // explicit hidden state the control was then revealed by
        // showChildren() when the window appeared, at its stale constructor
        // geometry near the TOP-LEFT corner, where it silently ate picks for
        // every returning user. Both halves are checked: that it is genuinely
        // not visible, and - the part an isVisible() check alone would miss
        // if the geometry were ever wrong instead - that real hit-testing
        // finds nothing at that stale rectangle.
        OcctViewWidget* returningView = returning.view();
        check(returningGuide != nullptr && returningGuide->skipControl() != nullptr &&
                  !returningGuide->skipControl()->isVisible(),
              "the skip control is hidden too, not just the panel it belongs to");
        // The stale rect is wherever the constructor's own initial
        // syncSkipGeometry() call left the (hidden) skip control - it never
        // moved for a returning user, since the panel itself never showed.
        // Read from the control's own geometry rather than a hard-coded
        // point: sizeHint() now measures the panel's actual step strings
        // (see WalkthroughPanel::sizeHint()), so a fixed x/y here would go
        // stale the moment the wording or the type scale changed width.
        const QPoint stale = returningGuide && returningGuide->skipControl()
                                  ? returningGuide->skipControl()->geometry().center()
                                  : QPoint();
        // "childAt finds NOTHING there" is no longer the right question: the
        // items drawer is a floating card anchored at the viewport's top left
        // now, which is exactly the region the skip control's constructor-time
        // rectangle falls in, and something legitimately answering a click
        // there is the correct arrangement rather than the bug. The question
        // that still matters - and the one the defect was actually about - is
        // whether the SKIP CONTROL is reachable there, so that is what is
        // asked, walking up from the hit the way the cluster probes do rather
        // than comparing one pointer.
        QWidget* hitStale = returningView->childAt(stale);
        bool skipLurks = false;
        for (QWidget* w = hitStale; w; w = w->parentWidget()) {
            if (returningGuide && w == returningGuide->skipControl()) skipLurks = true;
            if (w == returningView) break;
        }
        check(!skipLurks,
              QStringLiteral("the skip control does not lurk at its stale "
                             "constructor rectangle (found %1 there)")
                  .arg(hitStale ? QString::fromLatin1(hitStale->metaObject()->className())
                                : QStringLiteral("nothing")));

        QAction* returningReset = action(returning, QStringLiteral("Show tips again"));
        check(returningReset != nullptr, "the returning user's window has the reset action too");
        if (returningReset) {
            returningReset->trigger();
            settle(150);
            check(returningGuide != nullptr && returningGuide->isVisible(),
                  "Show tips again restores the panel for a returning user, "
                  "not just a same-session one");
            check(returningGuide != nullptr && !returningGuide->isFinished(),
                  "the restored panel starts fresh rather than staying finished");
            // And the skip control comes back with it, at the panel's real
            // position rather than the constructor-time one.
            check(returningGuide != nullptr && returningGuide->skipControl() != nullptr &&
                      returningGuide->skipControl()->isVisible(),
                  "the restored panel's skip control is visible again");
            if (returningGuide && returningGuide->skipControl()) {
                // Same margin-adjusted formula as the earlier skip-control
                // check above - see the comment there.
                const int shadowMargin = Theme::surfaceShadowMargin();
                const QPoint skipCentre =
                    returningGuide->pos() +
                    QPoint(returningGuide->width() - shadowMargin - 31, shadowMargin + 17);
                check(returningView->childAt(skipCentre) == returningGuide->skipControl(),
                      "and a real click at its centre finds the skip control itself");
            }
        }
        returning.close();
        // scopedSettings restores the real QSettings identity as it goes
        // out of scope here.
    }

    // The coverage floor, asserted OUTSIDE check() on purpose: an assertion
    // about how many checks ran must not be one of the checks it counts, or
    // the number it reports and the number it tests are two different things.
    // Reported the same way trigger()'s own failure is.
    if (g_checks < kCheckFloor) {
        std::printf("[FAIL] the run executed %d checks, below the floor of %d - "
                    "a guard has stopped letting its checks run; find the guard, "
                    "do not lower the floor\n",
                    g_checks, kCheckFloor);
        ++g_failures;
    }

    std::printf("\n%s (%d failure%s, %d checks, floor %d)  volumes: A=%.1f B=%.1f\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s", g_checks, kCheckFloor, volumeA, volumeB);
    return g_failures == 0 ? 0 : 1;
}
