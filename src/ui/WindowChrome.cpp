#include "WindowChrome.h"

#include "Theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QWindow>

#include <vector>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <windowsx.h>
#endif

namespace {

// One geometry per look - see WindowButtons' own header for which window
// wears which.
struct ButtonMetrics {
    int pad;
    int chipW;
    int chipH;
    int gap;
    int radius;   // of the hover fill; the Card look's outer card keeps
                  // Theme::paintSurface()'s own radius
};

ButtonMetrics metricsFor(WindowButtons::Look look)
{
    if (look == WindowButtons::Look::Card) return {6, 36, 26, 4, 6};
    return {0, 44, 32, 0, 0};   // Flat: bare full-height segments, no gaps
}

}  // namespace

// ---------------------------------------------------------------------------
// WindowButtons
// ---------------------------------------------------------------------------

WindowButtons::WindowButtons(Look look, QWidget* parent) : QWidget(parent), myLook(look)
{
    setAttribute(Qt::WA_NoSystemBackground);
    Theme::makeSurfaceTransparent(this);
    setMouseTracking(true);
    const ButtonMetrics m = metricsFor(myLook);
    setFixedSize(Theme::wholeDevicePixels(
        QSize(2 * m.pad + 3 * m.chipW + 2 * m.gap, 2 * m.pad + m.chipH)));
}

QRect WindowButtons::chipRect(int index) const
{
    const ButtonMetrics m = metricsFor(myLook);
    return QRect(m.pad + index * (m.chipW + m.gap), m.pad, m.chipW, m.chipH);
}

int WindowButtons::chipAt(const QPoint& pos) const
{
    for (int i = 0; i < 3; ++i) {
        if (chipRect(i).contains(pos)) return i;
    }
    return -1;
}

QRect WindowButtons::maxChipRectIn(const QWidget* ancestor) const
{
    const QRect r = chipRect(1);
    return QRect(mapTo(const_cast<QWidget*>(ancestor), r.topLeft()), r.size());
}

void WindowButtons::setMaxHovered(bool hovered)
{
    if (myMaxHovered == hovered) return;
    myMaxHovered = hovered;
    update();
}

void WindowButtons::setMaxPressed(bool pressed)
{
    if (myMaxPressed == pressed) return;
    myMaxPressed = pressed;
    update();
}

void WindowButtons::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const ButtonMetrics m = metricsFor(myLook);
    if (myLook == Look::Card) Theme::paintSurface(painter, rect(), 10);

    for (int i = 0; i < 3; ++i) {
        const QRect chip = chipRect(i);
        const bool hovered = (i == 1) ? myMaxHovered : (myHovered == i);
        const bool pressed = (i == 1) ? myMaxPressed : (myPressed == i);

        // Close wears the platform's own red on hover - the one colour every
        // Windows user already reads as "this closes the window" - and the
        // other two the ordinary chip-hover fill.
        if (hovered || pressed) {
            QColor fill = (i == 2) ? Theme::danger() : Theme::chipHover();
            if (pressed) fill = (i == 2) ? Theme::danger().darker(120) : Theme::chipActive();
            QPainterPath path;
            path.addRoundedRect(QRectF(chip), m.radius, m.radius);
            painter.fillPath(path, fill);
        }

        QColor ink = hovered || pressed ? Theme::text() : Theme::textMuted();
        if (i == 2 && (hovered || pressed)) ink = QColor(255, 255, 255);
        QPen pen(ink, 1.2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        const QPointF c = QRectF(chip).center();
        const double g = 4.5;   // glyph half-extent
        switch (i) {
            case 0:   // minimize
                painter.drawLine(QPointF(c.x() - g, c.y()), QPointF(c.x() + g, c.y()));
                break;
            case 1:   // maximize / restore
                if (window() && window()->isMaximized()) {
                    const QRectF back(c.x() - g + 2.0, c.y() - g, 2.0 * g - 2.0,
                                      2.0 * g - 2.0);
                    const QRectF front(c.x() - g, c.y() - g + 2.0, 2.0 * g - 2.0,
                                       2.0 * g - 2.0);
                    painter.drawLine(QPointF(back.left(), back.top()),
                                     QPointF(back.right(), back.top()));
                    painter.drawLine(QPointF(back.right(), back.top()),
                                     QPointF(back.right(), back.bottom()));
                    painter.drawRect(front);
                } else {
                    painter.drawRect(QRectF(c.x() - g, c.y() - g, 2.0 * g, 2.0 * g));
                }
                break;
            case 2:   // close
                painter.drawLine(QPointF(c.x() - g, c.y() - g), QPointF(c.x() + g, c.y() + g));
                painter.drawLine(QPointF(c.x() - g, c.y() + g), QPointF(c.x() + g, c.y() - g));
                break;
        }
    }
}

void WindowButtons::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const int chip = chipAt(event->pos());
        if (chip == 0 || chip == 2) {
            myPressed = chip;
            update();
            return;   // swallowed - the press belongs to the chip
        }
    }
    QWidget::mousePressEvent(event);
}

void WindowButtons::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && myPressed >= 0) {
        const int chip = chipAt(event->pos());
        const int pressed = myPressed;
        myPressed = -1;
        update();
        if (chip == pressed && window()) {
            if (chip == 0) window()->showMinimized();
            if (chip == 2) window()->close();
        }
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WindowButtons::mouseMoveEvent(QMouseEvent* event)
{
    const int chip = chipAt(event->pos());
    const int clientChip = (chip == 1) ? -1 : chip;   // max hover is native-driven
    if (clientChip != myHovered) {
        myHovered = clientChip;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void WindowButtons::leaveEvent(QEvent* event)
{
    if (myHovered != -1 || myPressed != -1) {
        myHovered = -1;
        myPressed = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void WindowButtons::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // The maximize glyph swaps to "restore" with the window's state, and the
    // state changes on gestures (snap, double-click, the native maximize)
    // this widget never sees - so watch the top-level window itself.
    if (QWidget* top = window(); top && top != myWatchedWindow) {
        if (myWatchedWindow) myWatchedWindow->removeEventFilter(this);
        top->installEventFilter(this);
        myWatchedWindow = top;
    }
}

bool WindowButtons::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myWatchedWindow && event->type() == QEvent::WindowStateChange) update();
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// The native filter (Windows only)
// ---------------------------------------------------------------------------

#ifdef Q_OS_WIN

namespace {

struct ChromeEntry {
    QPointer<QWidget> widget;
    std::function<WindowChrome::Hit(const QPoint&)> hitTest;
    QPointer<WindowButtons> buttons;
};

std::vector<ChromeEntry>& entries()
{
    static std::vector<ChromeEntry> list;
    return list;
}

ChromeEntry* entryFor(HWND hwnd)
{
    if (!hwnd) return nullptr;
    for (ChromeEntry& e : entries()) {
        if (!e.widget) continue;
        // internalWinId(), NEVER winId() or QWindow::winId(): both of those
        // CREATE the platform window when it does not exist yet, and this
        // filter runs for messages Windows pumps DURING CreateWindowEx
        // (WM_GETMINMAXINFO, WM_NCCALCSIZE) - so a forcing read here
        // re-entered window creation from inside window creation and
        // crashed the app before its first window ever appeared (the
        // startup crash the first build of this file shipped).
        // internalWinId() reads the handle and is zero until the window
        // genuinely exists, which is exactly the answer wanted here.
        if (reinterpret_cast<HWND>(e.widget->internalWinId()) == hwnd) return &e;
    }
    return nullptr;
}

int frameXFor(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

int frameYFor(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

class ChromeFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override
    {
        if (eventType != "windows_generic_MSG") return false;
        MSG* msg = static_cast<MSG*>(message);
        ChromeEntry* entry = entryFor(msg->hwnd);
        if (!entry || !entry->widget) return false;
        QWidget* w = entry->widget;

        switch (msg->message) {
            case WM_NCCALCSIZE: {
                // Remove the caption; keep the side and bottom resize
                // borders. The top is left FLUSH (client reaches the true
                // top edge) except while maximized, where Windows hangs the
                // window's frame off every monitor edge and the top inset
                // has to come back or the first rows of content are pushed
                // offscreen.
                if (!msg->wParam) return false;
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                RECT& r = params->rgrc[0];
                const int fx = frameXFor(msg->hwnd);
                const int fy = frameYFor(msg->hwnd);
                r.left += fx;
                r.right -= fx;
                r.bottom -= fy;
                if (IsZoomed(msg->hwnd)) r.top += fy;
                *result = 0;
                return true;
            }

            case WM_NCHITTEST: {
                POINT pt{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
                ScreenToClient(msg->hwnd, &pt);
                RECT client;
                GetClientRect(msg->hwnd, &client);
                // Outside the client is the surviving native frame - the
                // side/bottom borders DefWindowProc still owns correctly.
                if (pt.x < 0 || pt.y < 0 || pt.x >= client.right || pt.y >= client.bottom)
                    return false;
                // The top resize strip, ours to answer now that the caption
                // is gone (DefWindowProc would call this whole band a
                // caption from the styles alone).
                if (!IsZoomed(msg->hwnd) && pt.y < frameYFor(msg->hwnd)) {
                    const int fx = frameXFor(msg->hwnd);
                    if (pt.x < fx)
                        *result = HTTOPLEFT;
                    else if (pt.x >= client.right - fx)
                        *result = HTTOPRIGHT;
                    else
                        *result = HTTOP;
                    return true;
                }
                const double dpr = w->devicePixelRatioF();
                const QPoint logical(static_cast<int>(pt.x / dpr),
                                     static_cast<int>(pt.y / dpr));
                switch (entry->hitTest ? entry->hitTest(logical)
                                       : WindowChrome::Hit::Client) {
                    case WindowChrome::Hit::Caption: *result = HTCAPTION; return true;
                    case WindowChrome::Hit::MaxButton: *result = HTMAXBUTTON; return true;
                    case WindowChrome::Hit::Client: break;
                }
                *result = HTCLIENT;
                return true;
            }

            // HTMAXBUTTON makes the maximize chip's traffic NON-CLIENT, so
            // its click and hover are handled here and pushed back into the
            // widget - Qt never sees these messages.
            case WM_NCLBUTTONDOWN:
                if (msg->wParam == HTMAXBUTTON) {
                    if (entry->buttons) entry->buttons->setMaxPressed(true);
                    *result = 0;
                    return true;
                }
                return false;

            case WM_NCLBUTTONUP:
                if (msg->wParam == HTMAXBUTTON) {
                    if (entry->buttons) entry->buttons->setMaxPressed(false);
                    // POSTED, never performed here: calling showMaximized()
                    // synchronously from inside this filter changes the
                    // window's state in the middle of Qt's own dispatch of
                    // the very message being filtered, and that crashed the
                    // app on the first click of this chip. WM_SYSCOMMAND
                    // through the queue runs after this message completes,
                    // down Windows' own maximize path - animations, Qt's
                    // state sync and all.
                    PostMessageW(msg->hwnd, WM_SYSCOMMAND,
                                 IsZoomed(msg->hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    *result = 0;
                    return true;
                }
                return false;

            case WM_NCMOUSEMOVE: {
                if (entry->buttons)
                    entry->buttons->setMaxHovered(msg->wParam == HTMAXBUTTON);
                if (msg->wParam == HTMAXBUTTON) {
                    TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT),
                                          TME_LEAVE | TME_NONCLIENT, msg->hwnd, 0};
                    TrackMouseEvent(&track);
                }
                return false;
            }

            case WM_NCMOUSELEAVE:
            case WM_MOUSEMOVE:
                if (entry->buttons) {
                    entry->buttons->setMaxHovered(false);
                    entry->buttons->setMaxPressed(false);
                }
                return false;

            case WM_NCRBUTTONUP:
                // The caption's right-click system menu - one more native
                // behaviour the custom bar keeps.
                if (msg->wParam == HTCAPTION) {
                    HMENU menu = GetSystemMenu(msg->hwnd, FALSE);
                    if (menu) {
                        const int cmd = TrackPopupMenu(
                            menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                            GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam), 0,
                            msg->hwnd, nullptr);
                        if (cmd) PostMessageW(msg->hwnd, WM_SYSCOMMAND, cmd, 0);
                    }
                    *result = 0;
                    return true;
                }
                return false;
        }
        return false;
    }
};

ChromeFilter* installedFilter()
{
    static ChromeFilter* filter = nullptr;
    if (!filter) {
        filter = new ChromeFilter;
        QCoreApplication::instance()->installNativeEventFilter(filter);
    }
    return filter;
}

// The frame change only takes effect once Windows re-asks WM_NCCALCSIZE -
// forced here rather than waited for, so an already-visible window loses
// its native caption the moment chrome is attached.
void announceFrameChange(QWidget* widget)
{
    // internalWinId(), for the same never-force reason entryFor() records:
    // zero means the native window does not exist yet, and the
    // FrameChangeWatcher will announce once it does.
    HWND hwnd = reinterpret_cast<HWND>(widget->internalWinId());
    if (!hwnd) return;
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
}

// Waits for the widget's native window if it does not exist yet, then
// announces the frame change once.
class FrameChangeWatcher : public QObject {
public:
    explicit FrameChangeWatcher(QWidget* widget) : QObject(widget), myWidget(widget)
    {
        widget->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == myWidget &&
            (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)) {
            announceFrameChange(myWidget);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* myWidget = nullptr;
};

}  // namespace

void WindowChrome::attach(QWidget* topLevel, std::function<Hit(const QPoint&)> hitTest,
                          WindowButtons* buttons)
{
    installedFilter();
    entries().push_back({topLevel, std::move(hitTest), buttons});
    announceFrameChange(topLevel);   // a no-op until the native window exists
    new FrameChangeWatcher(topLevel);   // parented; covers later re-creation too
}

#else   // !Q_OS_WIN

void WindowChrome::attach(QWidget*, std::function<Hit(const QPoint&)>, WindowButtons*)
{
    // Not Windows: the native title bar stays, and nothing here is needed.
}

#endif
