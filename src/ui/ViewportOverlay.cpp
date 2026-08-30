#include "ViewportOverlay.h"

#include "Theme.h"

#include <QEvent>
#include <QWidget>

#include <algorithm>

namespace {
// The mockup's gap from the viewport edge to what is actually PAINTED, not
// to a widget's own bounding box. The two coincide again now that the
// floating-surface family reserves no shadow margin
// (Theme::surfaceShadowMargin() is zero), so this is simply 16 - but the
// subtraction stays rather than being folded away: it is the one line that
// records WHY the anchor is measured against painted edges, and the same
// compensation AppBar's layout and the rail's own padding express. Not
// constexpr, deliberately: CLAUDE.md's Theme surface is called, not compiled
// in, so a caller cannot bake in a stale value if that number ever moves
// again.
const int kMargin = 16 - Theme::surfaceShadowMargin();
constexpr int kGap = 8;       // gap between clusters sharing an edge

// The rail's own margin: the plan's 14px from the viewport's left, top and
// bottom edges, two pixels tighter than kMargin deliberately - a rail pinned
// to an edge hugs it; a card floating in a corner stands off it.
constexpr int kEdgeMargin = 14;
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

    // Where the left-hand corner and edge-centre anchors start. Ordinarily
    // kMargin from the viewport's edge - but a LeftEdge entry is a spine
    // pinned against that edge for the viewport's whole height, so anything
    // anchored to the left would otherwise be placed UNDER it rather than
    // beside it. The items drawer is the first widget for which that matters;
    // computing it here rather than at the drawer's own call site means the
    // next left-hand card is placed beside the rail for free, and means the
    // answer does not depend on which entry happens to have been added first.
    int leftX = kMargin;

    for (const Entry& entry : myEntries) {
        if (!entry.widget) continue;
        // Size every widget before measuring: the centring sums below are wrong
        // if a widget is still at its default size on first layout.
        entry.widget->adjustSize();
        // A hidden entry takes up none of an edge - it neither raises the left
        // margin nor occupies a slot in a stack. That has to hold for the
        // CURSORS as well as for leftX below, or a closed items drawer would
        // still push the next TopLeft card down by its height. Latent while
        // the drawer is the only thing anchored there, and exactly the kind of
        // half-applied invariant that stops being latent the moment something
        // else is added beside it.
        if (entry.widget->isHidden()) continue;
        if (entry.anchor == Anchor::LeftCenter)  leftCenterY += entry.widget->height() + kGap;
        if (entry.anchor == Anchor::RightCenter) rightCenterY += entry.widget->height() + kGap;
        // isHidden(), not isVisible(). isVisible() is false for every child of
        // a window that has not been shown yet - and the ONLY relayout the
        // application performs before the user touches anything runs inside
        // that window's own show sequence, because QMainWindow's layout sizes
        // the viewport (which is what this class filters for) BEFORE
        // showChildren() marks the rail visible. Guarding on isVisible()
        // therefore computed a left edge of kMargin at exactly the moment
        // that mattered, and the drawer opened on top of the rail, hiding its
        // top six buttons until the first action of the session moved it.
        // Green suite throughout: gui_smoke reaches this check after dozens
        // of relayouts, by which time it has long since corrected itself.
        // Caught in a PrintWindow capture, like every visual bug this project
        // has shipped.
        //
        // isHidden() asks the question that is actually meant - "is this
        // widget meant to be on screen" - and its answer does not depend on
        // whether an ancestor has been shown yet.
        if (entry.anchor == Anchor::LeftEdge)
            leftX = std::max(leftX, kEdgeMargin + entry.widget->width() + kGap);
    }
    int leftCursor = (h - (leftCenterY - kGap)) / 2;
    int rightCursor = (h - (rightCenterY - kGap)) / 2;

    for (const Entry& entry : myEntries) {
        if (!entry.widget) continue;   // the widget was destroyed; nothing to place
        // Hidden entries are skipped here for the same reason they are skipped
        // in the measuring pass above: they occupy no slot. Nothing is lost by
        // leaving one at a stale rectangle - occupiedRects() ignores it too,
        // and whatever shows it again runs a relayout in the same breath (see
        // MainWindow's Items toggle).
        if (entry.widget->isHidden()) continue;
        QWidget* placed = entry.widget;
        placed->adjustSize();
        const int cw = placed->width();
        const int ch = placed->height();

        switch (entry.anchor) {
            case Anchor::TopLeft:
                placed->move(leftX, topLeftY);
                topLeftY += ch + kGap;
                break;
            case Anchor::LeftCenter:
                placed->move(leftX, leftCursor);
                leftCursor += ch + kGap;
                break;
            case Anchor::BottomLeft:
                bottomLeftY -= ch;
                placed->move(leftX, bottomLeftY);
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
            case Anchor::LeftEdge:
                placed->move(kEdgeMargin, kEdgeMargin);
                // std::max, not the available height alone: on a viewport
                // too short for every tool the rail carries, shrinking it
                // would ask its layout to squeeze fourteen fixed-size chips
                // into a space they do not fit, which Qt resolves by
                // overlapping them. Keeping the rail at its natural height
                // instead means a short viewport clips the last button
                // cleanly off the bottom edge - still wrong, but legibly so,
                // and every button above it stays the size it should be.
                placed->resize(cw, std::max(ch, h - kEdgeMargin * 2));
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
