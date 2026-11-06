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
// state to the window's callbacks (since Phase 7b the page draws every
// window's buttons), which is why the maximize chip is the one passive
// segment while Minimize and Close are ordinary client-side targets.
//
// Windows-only by construction: on any other platform attach() is a no-op
// and the native title bar simply stays.
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QWidget>

#include <functional>


namespace WindowChrome {

enum class Hit {
    Client,      // ordinary content - Qt handles it
    Caption,     // drag area - native move/maximize/system-menu behaviour
    MaxButton,   // the maximize chip - HTMAXBUTTON, so snap layouts appear
};

// A point inside `dragWidget` whose deepest child is nothing answers
// Caption; everything else is Client. Both widgets are QPointer-guarded.
std::function<Hit(const QPoint&)> captionHitTest(QWidget* topLevel, QWidget* dragWidget);

// `hitTest` answers in the top-level widget's own LOGICAL coordinates (a
// top-level QWidget covers its client area exactly, so widget coords ARE
// client coords). It is consulted on every WM_NCHITTEST that lands inside
// the client and outside the top resize strip. The window's buttons are
// drawn by its page. The maximize chip's traffic is NON-client
// (so Windows' snap layouts appear over it), so its hover and press never
// reach the window's content; they are delivered here instead.
void attach(QWidget* topLevel, std::function<Hit(const QPoint&)> hitTest,
            std::function<void(bool)> onMaxHover, std::function<void(bool)> onMaxPressed);

}  // namespace WindowChrome
