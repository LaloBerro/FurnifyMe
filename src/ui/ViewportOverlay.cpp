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
    int leftCenterY = 0;
    int rightCenterY = 0;

    for (const Entry& entry : myEntries) {
        if (entry.anchor == Anchor::LeftCenter)  leftCenterY += entry.widget->height() + kGap;
        if (entry.anchor == Anchor::RightCenter) rightCenterY += entry.widget->height() + kGap;
    }
    int leftCursor = (h - (leftCenterY - kGap)) / 2;
    int rightCursor = (h - (rightCenterY - kGap)) / 2;

    for (const Entry& entry : myEntries) {
        QWidget* cluster = entry.widget;
        cluster->adjustSize();
        const int cw = cluster->width();
        const int ch = cluster->height();

        switch (entry.anchor) {
            case Anchor::TopLeft:
                cluster->move(kMargin, topLeftY);
                topLeftY += ch + kGap;
                break;
            case Anchor::LeftCenter:
                cluster->move(kMargin, leftCursor);
                leftCursor += ch + kGap;
                break;
            case Anchor::BottomLeft:
                bottomLeftY -= ch;
                cluster->move(kMargin, bottomLeftY);
                bottomLeftY -= kGap;
                break;
            case Anchor::TopRight:
                cluster->move(w - cw - kMargin, topRightY);
                topRightY += ch + kGap;
                break;
            case Anchor::RightCenter:
                cluster->move(w - cw - kMargin, rightCursor);
                rightCursor += ch + kGap;
                break;
        }
        cluster->raise();
    }
}
