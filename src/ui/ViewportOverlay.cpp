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
// The same value as the public ViewportOverlay::kStackGap, read through that
// name rather than redeclared, so MainWindow's own stacked-height arithmetic
// can never quietly drift from what relayout() actually places against.
constexpr int kGap = ViewportOverlay::kStackGap;
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

    // The SPINE among however many entries share Anchor::LeftEdge - the
    // last one added, structurally, regardless of which ones are visible
    // right now (see the Anchor comment in the header for why that has to
    // be true rather than "the last one currently shown"). Found once, up
    // front, rather than re-derived per entry below: every LeftEdge entry
    // that is NOT this one is a header, stacked above it in the same
    // column, at its own natural size.
    QWidget* spineWidget = nullptr;
    for (const Entry& entry : myEntries) {
        if (entry.widget && entry.anchor == Anchor::LeftEdge) spineWidget = entry.widget;
    }

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

    // The bottom of whatever LeftEdge HEADERS precede the spine in the same
    // column (Milestone 5, item 3's fix round: the pill leads the rail) -
    // the symmetric counterpart to leftX above. A header does not span the
    // viewport's full height the way the spine does, so it only needs
    // pushing TopLeft's own stack DOWN clear of it, never pushing anything
    // right - once past a header's own bottom edge, only the spine (which
    // DOES span the remaining height) is still there to dodge horizontally.
    int leftEdgeHeaderBottom = kEdgeMargin;

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
        if (entry.anchor == Anchor::LeftEdge) {
            if (entry.widget == spineWidget)
                leftX = std::max(leftX, kEdgeMargin + entry.widget->width() + kGap);
            else
                leftEdgeHeaderBottom += entry.widget->height() + kGap;
        }
    }
    int leftCursor = (h - (leftCenterY - kGap)) / 2;
    int rightCursor = (h - (rightCenterY - kGap)) / 2;
    // TopLeft's own stack starts below whatever headers lead the LeftEdge
    // column, when that is lower than the corner's own ordinary margin -
    // std::max, not a plain assignment, so a build with no header at all
    // (leftEdgeHeaderBottom left at its untouched kEdgeMargin) leaves
    // topLeftY exactly as it always was.
    topLeftY = std::max(topLeftY, leftEdgeHeaderBottom);

    // The POSITION half of Theme's whole-device-pixel rule, for everything
    // anchored here - see Theme.h.
    //
    // Rounding the sizes alone is not enough and this cost a second round to
    // learn: a card's far edge is its origin plus its extent, so a whole
    // extent on a fractional origin lands right back between device rows. The
    // origins here look safe - the rail goes to a flat (8, 8) - but they are
    // relative to the VIEWPORT, and the viewport sits under the app bar at
    // whatever offset that bar's height leaves. At 125% that put the rail's
    // bottom edge back on a half row and the black-line sweep caught it again,
    // 39 device pixels of it, after the size half had already been fixed.
    const QPoint origin = myViewport->mapTo(myViewport->window(), QPoint(0, 0));
    const double dpr = myViewport->devicePixelRatioF();
    auto snapped = [&origin, dpr](int x, int y) {
        return QPoint(Theme::snapToDevicePixels(x, origin.x(), dpr),
                      Theme::snapToDevicePixels(y, origin.y(), dpr));
    };

    // The LeftEdge column's own stacking cursor - the pill's header sits at
    // kEdgeMargin, and the rail (the spine, whichever entry equals
    // spineWidget) starts wherever the header's own bottom edge plus kGap
    // leaves it, read off the header's REAL placed height a few lines below
    // rather than any fixed offset - the whole reason a header can grow a
    // row when the type scale does and the rail simply follows.
    int leftEdgeY = kEdgeMargin;

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
        // Grown to a whole number of DEVICE pixels before anything is
        // positioned against it - see Theme.h.
        //
        // A card's size comes from its layout and lands wherever the content
        // put it, so at a fractional display scale its far edge falls between
        // device rows: Qt flushes the row the card's own logical clip cannot
        // reach, and over the GL surface that row is black rather than
        // transparent. MEASURED, not theorised - the whole-window black-run
        // sweep in gui_smoke found a 113-device-pixel 0,0,0 line along the
        // RAIL's bottom edge at 225% scaling, which is the same defect the
        // round/flatten chip found at its own size and the drawer's corner
        // nubs were one scale down.
        //
        // Applied to every anchored entry rather than to the rail alone: the
        // rail is simply the one that was measured, and a card added later
        // would rediscover this. The growth is at most three pixels of slack
        // inside a card whose contents are top-aligned, so nothing inside
        // moves; the positions below then follow the grown size.
        //
        // It does NOT reach a card that pins itself with setFixedSize(), and
        // that is not a gap this line can close: resize() on a fixed-size
        // widget is a silent no-op, not an error. WalkthroughPanel and
        // AxisGizmo both do, and both therefore round their OWN sizeHint()
        // through wholeDevicePixels() in their constructors. Anything added
        // here that pins its size has to do the same.
        placed->resize(Theme::wholeDevicePixels(placed->size()));
        const int cw = placed->width();
        const int ch = placed->height();

        switch (entry.anchor) {
            case Anchor::TopLeft:
                placed->move(snapped(leftX, topLeftY));
                topLeftY += ch + kGap;
                break;
            case Anchor::LeftCenter:
                placed->move(snapped(leftX, leftCursor));
                leftCursor += ch + kGap;
                break;
            case Anchor::BottomLeft:
                bottomLeftY -= ch;
                placed->move(snapped(leftX, bottomLeftY));
                bottomLeftY -= kGap;
                break;
            case Anchor::TopRight:
                placed->move(snapped(w - cw - kMargin, topRightY));
                topRightY += ch + kGap;
                break;
            case Anchor::RightCenter:
                placed->move(snapped(w - cw - kMargin, rightCursor));
                rightCursor += ch + kGap;
                break;
            case Anchor::BottomRight:
                bottomRightY -= ch;
                placed->move(snapped(w - cw - kMargin, bottomRightY));
                bottomRightY -= kGap;
                break;
            case Anchor::LeftEdge:
                // Every LeftEdge entry shares x = kEdgeMargin and stacks at
                // the column's own running cursor - a header (the pill)
                // simply advances it by its own natural height plus kGap; only
                // the spine (whichever entry equals spineWidget, the rail)
                // additionally stretches to reach the viewport's bottom edge.
                placed->move(snapped(kEdgeMargin, leftEdgeY));
                if (placed == spineWidget) {
                    // std::max, not the available height alone: on a viewport
                    // too short for every tool the rail carries, shrinking it
                    // would ask its layout to squeeze fourteen fixed-size chips
                    // into a space they do not fit, which Qt resolves by
                    // overlapping them. Keeping the rail at its natural height
                    // instead means a short viewport clips the last button
                    // cleanly off the bottom edge - still wrong, but legibly so,
                    // and every button above it stays the size it should be.
                    //
                    // This class has no way to know it, but the branch is dead
                    // in the shipped app: MainWindow::buildOverlay() sets the
                    // viewport's own minimum height from the pill's and the
                    // rail's own sizeHint()s STACKED, plus kEdgeMargin twice
                    // and one kStackGap between them, specifically so `h`
                    // here can never be smaller than `leftEdgeY + ch` needs.
                    // Kept as a real std::max rather than an assert, because
                    // this class is not the one that enforces that invariant
                    // and must not assume a caller always will.
                    // Through wholeDevicePixels() as well: this one is the
                    // ONLY anchored size not taken from a layout, and the
                    // viewport's height is as arbitrary a number as they come
                    // - which is exactly why the rail was the card the
                    // black-run sweep caught.
                    placed->resize(cw, Theme::wholeDevicePixels(
                                           std::max(ch, h - leftEdgeY - kEdgeMargin)));
                }
                // Advances the column's cursor by this entry's REAL, final
                // height - already the stretched one for the spine, since
                // the resize above already ran. A header (the pill) reads
                // its own natural height here, so the rail that follows it
                // always starts from the pill's actual size, never a
                // constant - the whole point of this fix round.
                leftEdgeY += placed->height() + kGap;
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
