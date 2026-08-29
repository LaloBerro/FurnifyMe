#include "ViewportOverlay.h"

#include <QEvent>
#include <QWidget>

namespace {
constexpr int kMargin = 16;   // gap from the viewport edge
constexpr int kGap = 8;       // gap between clusters sharing an edge
}  // namespace

ViewportOverlay::ViewportOverlay(QWidget* viewport)
    : QObject(viewport)
    , myViewport(viewport)
{
    myViewport->installEventFilter(this);
}

void ViewportOverlay::addWidget(QWidget* widget, Anchor anchor)
{
    widget->setParent(myViewport);
    // Every normal caller here hands over a freshly constructed widget that
    // has never been shown or hidden, so unconditional show() is exactly
    // right for it. WalkthroughPanel is the one exception: it can decide,
    // from its own constructor, that it must stay hidden (an
    // already-learned user). Qt marks that kind of explicit decision with
    // WA_WState_ExplicitShowHide - a widget that merely hasn't been shown
    // yet does not carry it - so checking for it here is what lets a
    // widget's own hidden decision survive being added, without changing
    // behaviour for anything that never makes that decision.
    if (!(widget->testAttribute(Qt::WA_WState_ExplicitShowHide) && widget->isHidden()))
        widget->show();
    widget->raise();
    myEntries.push_back(Entry{widget, anchor});
    relayout();
}

bool ViewportOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myViewport && event->type() == QEvent::Resize) relayout();
    return QObject::eventFilter(watched, event);
}

void ViewportOverlay::relayout()
{
    const int w = myViewport->width();
    const int h = myViewport->height();

    // Clusters sharing an anchor stack downward in the order they were added.
    int topLeftY = kMargin;
    int topRightY = kMargin;
    int bottomLeftY = h - kMargin;
    int bottomRightY = h - kMargin;
    int leftCenterY = 0;
    int rightCenterY = 0;

    for (const Entry& entry : myEntries) {
        if (!entry.widget) continue;
        // Size every widget before measuring: the centring sums below are wrong
        // if a widget is still at its default size on first layout.
        entry.widget->adjustSize();
        if (entry.anchor == Anchor::LeftCenter)  leftCenterY += entry.widget->height() + kGap;
        if (entry.anchor == Anchor::RightCenter) rightCenterY += entry.widget->height() + kGap;
    }
    int leftCursor = (h - (leftCenterY - kGap)) / 2;
    int rightCursor = (h - (rightCenterY - kGap)) / 2;

    for (const Entry& entry : myEntries) {
        if (!entry.widget) continue;   // the widget was destroyed; nothing to place
        QWidget* placed = entry.widget;
        placed->adjustSize();
        const int cw = placed->width();
        const int ch = placed->height();

        switch (entry.anchor) {
            case Anchor::TopLeft:
                placed->move(kMargin, topLeftY);
                topLeftY += ch + kGap;
                break;
            case Anchor::LeftCenter:
                placed->move(kMargin, leftCursor);
                leftCursor += ch + kGap;
                break;
            case Anchor::BottomLeft:
                bottomLeftY -= ch;
                placed->move(kMargin, bottomLeftY);
                bottomLeftY -= kGap;
                break;
            case Anchor::TopRight:
                placed->move(w - cw - kMargin, topRightY);
                topRightY += ch + kGap;
                break;
            case Anchor::RightCenter:
                placed->move(w - cw - kMargin, rightCursor);
                rightCursor += ch + kGap;
                break;
            case Anchor::BottomRight:
                bottomRightY -= ch;
                placed->move(w - cw - kMargin, bottomRightY);
                bottomRightY -= kGap;
                break;
        }
        placed->raise();
    }
}
