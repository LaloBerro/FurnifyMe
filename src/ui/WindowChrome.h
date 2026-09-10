#pragma once
// The custom Windows title bar (Milestone 5, user pick: "the same bar but
// integrated in the app", editor variant 2 - no strip at all, the window
// controls float over the viewport and the pill drags the window).
//
// The mechanism is the Windows Terminal recipe, NOT Qt::FramelessWindowHint:
// the window keeps its ordinary WS_OVERLAPPEDWINDOW styles - so DWM shadows,
// snap layouts, the minimize/restore animations and edge resizing all stay
// native - and one application-wide QAbstractNativeEventFilter eats the
// caption in WM_NCCALCSIZE (the client area reaches the window's true top
// edge; the side and bottom resize borders are kept) and answers
// WM_NCHITTEST itself for everything inside the client: the top few pixels
// resize, whatever the window's own hit-test lambda calls Caption drags
// (and double-clicks to maximize, and right-clicks for the system menu -
// all DefWindowProc behaviour we get for free by answering HTCAPTION), the
// maximize chip answers HTMAXBUTTON - which is the ONE undocumented-feeling
// requirement for Windows 11's snap-layout flyout to appear over a custom
// button - and everything else is plain HTCLIENT.
//
// Because HTMAXBUTTON turns the mouse traffic over that chip into
// NON-CLIENT messages, Qt never sees them: the filter handles
// WM_NCLBUTTONDOWN/UP itself (toggling maximize) and pushes hover/pressed
// state back into the WindowButtons widget, which is why the maximize chip
// is the one painted-but-passive segment while Minimize and Close are
// ordinary client-side hit targets.
//
// Windows-only by construction: on any other platform attach() is a no-op
// and the native title bar simply stays.
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <functional>

class WindowButtons;

namespace WindowChrome {

enum class Hit {
    Client,      // ordinary content - Qt handles it
    Caption,     // drag area - native move/maximize/system-menu behaviour
    MaxButton,   // the maximize chip - HTMAXBUTTON, so snap layouts appear
};

// The hit-test both windows actually want, built once: the buttons'
// maximize chip answers MaxButton; a point inside `dragWidget` whose
// deepest child is nothing answers Caption (painted marks and wordmarks
// are mouse-transparent, so childAt() skips them - that is the contract);
// everything else is Client. All three widgets are QPointer-guarded.
std::function<Hit(const QPoint&)> captionHitTest(QWidget* topLevel,
                                                 QWidget* dragWidget,
                                                 WindowButtons* buttons);

// `hitTest` answers in the top-level widget's own LOGICAL coordinates (a
// top-level QWidget covers its client area exactly, so widget coords ARE
// client coords). It is consulted on every WM_NCHITTEST that lands inside
// the client and outside the top resize strip. `buttons` is the window's
// WindowButtons row, so the filter can push the maximize chip's
// hover/pressed state into it; pass nullptr for a window without one.
void attach(QWidget* topLevel, std::function<Hit(const QPoint&)> hitTest,
            WindowButtons* buttons);

}  // namespace WindowChrome

// The three window controls - Minimize, Maximize/Restore, Close - painted in
// this app's own tokens. Two modes: Card (a paintSurface-family rounded
// card, for floating over the editor's viewport beside the axis gizmo) and
// Flat (bare full-height segments, for sitting inside the selector's title
// strip). Minimize and Close are ordinary client-side targets; the maximize
// chip's interaction is native (see WindowChrome above), this widget only
// PAINTS the state the filter pushes in.
class WindowButtons : public QWidget {
    Q_OBJECT

public:
    enum class Look { Card, Flat };
    explicit WindowButtons(Look look, QWidget* parent = nullptr);

    // The maximize chip's rectangle in `ancestor`'s coordinates - what a
    // window's hit-test lambda compares against.
    QRect maxChipRectIn(const QWidget* ancestor) const;

    // Pushed by the native filter (see WindowChrome) - the maximize chip's
    // mouse traffic never reaches Qt.
    void setMaxHovered(bool hovered);
    void setMaxPressed(bool pressed);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QRect chipRect(int index) const;   // 0 minimize, 1 maximize, 2 close
    int chipAt(const QPoint& pos) const;

    Look myLook = Look::Card;
    int myHovered = -1;      // client-side hover: minimize/close only
    int myPressed = -1;
    bool myMaxHovered = false;
    bool myMaxPressed = false;
    QWidget* myWatchedWindow = nullptr;
};
