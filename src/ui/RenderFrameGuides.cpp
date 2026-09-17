#include "RenderFrameGuides.h"

#include "OcctViewWidget.h"

#include <QPainter>
#include <QPaintEvent>

namespace {
// The mask outside the picture. Dark enough that the eye stops reading it as
// part of the shot and light enough that the furniture still shows through it
// - the point is to frame, not to crop, and a user pulling the camera back
// has to be able to see what is about to come into the picture.
constexpr int kMaskAlpha = 118;
// The picture's own edge, and the lines inside it. Both white rather than the
// accent: this sits on a pale studio backdrop whose colour the user chooses,
// and white is the one ink that reads as "not part of the furniture" against
// every wood in the library.
constexpr int kEdgeAlpha = 225;
constexpr int kGuideAlpha = 90;
constexpr int kCornerPx = 22;
}  // namespace

RenderFrameGuides::RenderFrameGuides(OcctViewWidget* viewport)
    : QWidget(viewport), myViewport(viewport)
{
    // Every gesture that frames a shot - orbit, pan, zoom - has to reach the
    // viewport through this. See the header: the attribute takes the whole
    // subtree out of hit-testing, and this widget deliberately has none.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void RenderFrameGuides::refresh()
{
    if (!myViewport) return;
    // Always the full viewport: the mask is the part OUTSIDE the picture, so
    // the widget has to cover everything the picture does not.
    const QRect all(0, 0, myViewport->width(), myViewport->height());
    if (geometry() != all) setGeometry(all);
    const QRect frame = myViewport->renderFrameRect();
    if (frame == myFrame) return;
    myFrame = frame;
    update();
}

void RenderFrameGuides::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (myFrame.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // The mask, as the four bands around the picture rather than a full-rect
    // fill with a cleared hole: a cleared hole needs CompositionMode_Clear on
    // a translucent widget, and that punches through to the window behind
    // rather than to the GL scene this is floating over.
    const QColor mask(13, 13, 15, kMaskAlpha);
    const QRect all = rect();
    if (myFrame.top() > all.top())
        p.fillRect(QRect(all.left(), all.top(), all.width(), myFrame.top() - all.top()), mask);
    if (myFrame.bottom() < all.bottom())
        p.fillRect(QRect(all.left(), myFrame.bottom() + 1, all.width(),
                         all.bottom() - myFrame.bottom()),
                   mask);
    if (myFrame.left() > all.left())
        p.fillRect(QRect(all.left(), myFrame.top(), myFrame.left() - all.left(),
                         myFrame.height()),
                   mask);
    if (myFrame.right() < all.right())
        p.fillRect(QRect(myFrame.right() + 1, myFrame.top(), all.right() - myFrame.right(),
                         myFrame.height()),
                   mask);

    // The guides, inside the picture, under its own edge.
    const OcctViewWidget::RenderGuides guides =
        myViewport ? myViewport->renderGuides() : OcctViewWidget::RenderGuides::Off;
    if (guides == OcctViewWidget::RenderGuides::Thirds) {
        p.setPen(QPen(QColor(255, 255, 255, kGuideAlpha), 1));
        for (int i = 1; i <= 2; ++i) {
            const int x = myFrame.left() + myFrame.width() * i / 3;
            const int y = myFrame.top() + myFrame.height() * i / 3;
            p.drawLine(x, myFrame.top(), x, myFrame.bottom());
            p.drawLine(myFrame.left(), y, myFrame.right(), y);
        }
    } else if (guides == OcctViewWidget::RenderGuides::Centre) {
        QPen pen(QColor(255, 255, 255, kGuideAlpha), 1, Qt::DashLine);
        pen.setDashPattern({5, 6});
        p.setPen(pen);
        const int cx = myFrame.center().x();
        const int cy = myFrame.center().y();
        p.drawLine(cx, myFrame.top(), cx, myFrame.bottom());
        p.drawLine(myFrame.left(), cy, myFrame.right(), cy);
    }

    // The picture's own edge last, so nothing is drawn over it. Inset by half
    // a pen width the way Theme::drawCrispBorder already does for cards: an
    // antialiased 1px pen at an integer coordinate smears across two rows at
    // half intensity, and this one is deliberately not antialiased, so the
    // rect is the one shrunk instead.
    p.setPen(QPen(QColor(255, 255, 255, kEdgeAlpha), 1));
    p.drawRect(myFrame.adjusted(0, 0, -1, -1));

    // Corner ticks, at full strength: the edge above is a hairline over a
    // busy render, and the four corners are what the eye actually finds a
    // rectangle by.
    p.setPen(QPen(QColor(255, 255, 255, 255), 2));
    const int t = std::min(kCornerPx, std::min(myFrame.width(), myFrame.height()) / 3);
    const QRect f = myFrame.adjusted(0, 0, -1, -1);
    p.drawLine(f.left(), f.top(), f.left() + t, f.top());
    p.drawLine(f.left(), f.top(), f.left(), f.top() + t);
    p.drawLine(f.right(), f.top(), f.right() - t, f.top());
    p.drawLine(f.right(), f.top(), f.right(), f.top() + t);
    p.drawLine(f.left(), f.bottom(), f.left() + t, f.bottom());
    p.drawLine(f.left(), f.bottom(), f.left(), f.bottom() - t);
    p.drawLine(f.right(), f.bottom(), f.right() - t, f.bottom());
    p.drawLine(f.right(), f.bottom(), f.right(), f.bottom() - t);
}
