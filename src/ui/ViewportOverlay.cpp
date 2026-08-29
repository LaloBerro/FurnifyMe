#include "ViewportOverlay.h"

#include "Theme.h"

#include <QEvent>
#include <QWidget>

namespace {
// The mockup's gap from the viewport edge to what is actually PAINTED, not
// to a widget's own bounding box. Most anchored widgets (the axis gizmo, the
// unit readout) paint their whole bounding box, so the two coincide for
// them. The chip clusters and the walkthrough guide do not any more - they
// grew a Theme::surfaceShadowMargin() margin of shadow-only space on every
// side (see ToolCluster.cpp) - so anchoring their bounding box at the raw 16
// would leave their painted content sitting 16 + surfaceShadowMargin() from
// the edge instead. Pulling every entry's anchor in by that same margin
// keeps the ones that DID grow visually at 16 again; the ones that did not
// grow move 16 - surfaceShadowMargin() from the edge instead, close enough
// that nothing in this app currently distinguishes it from 16 by eye or by
// test (see the "bottom-anchored cluster keeps its margin" range check).
// Not constexpr: Theme::surfaceShadowMargin() is an ordinary function, not a
// constexpr one - CLAUDE.md's Theme surface is called, not compiled in, so a
// caller cannot accidentally bake in a stale 3 if that value ever moved.
const int kMargin = 16 - Theme::surfaceShadowMargin();
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

std::vector<QRect> ViewportOverlay::occupiedRects() const
{
    std::vector<QRect> rects;
    for (const Entry& entry : myEntries) {
        if (entry.widget && entry.widget->isVisible()) rects.push_back(entry.widget->geometry());
    }
    return rects;
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

    // Dependents that place themselves against one of the entries above -
    // ToastHost, HintBalloon and ExtrudePreview - re-place (and re-raise)
    // themselves here, now that every anchored widget is at its final
    // rectangle. See the comment on this signal in the header.
    emit laidOut();
}
