#pragma once
// Positions cluster widgets against the edges of the viewport. Not a widget
// itself: the clusters are direct children of the viewport, which is the
// arrangement verified to composite correctly over OCCT's OpenGL surface.
#include <QObject>
#include <QPointer>
#include <QRect>

#include <vector>

class QWidget;

class ViewportOverlay : public QObject {
    Q_OBJECT

public:
    // The six corner/edge-centre anchors place a widget at its own natural
    // size. LeftEdge is different in kind: it PINS its widget top to bottom
    // against the viewport's left edge, so the tool rail is a spine the
    // viewport is laid out beside rather than a tall card that happens to
    // start near the top. The rail's own layout decides where the slack goes
    // (a stretch between the last tool and Undo puts history at the bottom);
    // this anchor only decides how much slack there is.
    //
    // A visible LeftEdge entry also moves the three left-hand anchors -
    // TopLeft, LeftCenter, BottomLeft - out past it, so a card anchored there
    // (the items drawer) lands BESIDE the spine rather than underneath it.
    // That is a property of the layout rather than of any one caller: it does
    // not depend on the order entries were added, and it holds for whatever
    // is anchored left next.
    //
    // MULTIPLE widgets may share Anchor::LeftEdge (Milestone 5, item 3's fix
    // round: the pill leads the rail's own column, same left margin, one
    // gap between them). They stack downward from the viewport's top edge,
    // same x, in the order they were added - exactly like TopLeft's own
    // stack - but only the LAST one added stretches to reach the viewport's
    // bottom edge (the spine; the rail). Every earlier one (a header; the
    // pill) keeps its own natural size instead. Which is which is derived
    // from INSERTION ORDER alone, structurally, never from which entry
    // happens to be visible right now: the rail is still the last entry
    // added even while render mode hides it, so hiding it cannot promote the
    // pill into stretching to fill the viewport - a hidden spine is simply
    // skipped, the same as any other hidden entry. A header's height is
    // also what pushes TopLeft's own stack down clear of it (symmetric to
    // how the spine's width already pushes TopLeft/LeftCenter/BottomLeft
    // right of it) - see relayout()'s own comment for the arithmetic.
    // RightEdge (Milestone 5, render-mode UI): LeftEdge's mirror for a
    // single full-height panel pinned to the RIGHT edge - it stretches from
    // the viewport's top to its bottom at kEdgeMargin, and while visible it
    // pushes the three right-hand anchors (TopRight, RightCenter,
    // BottomRight) out past its width, exactly as the spine pushes the
    // left-hand ones. No header/spine split on this side: every RightEdge
    // entry stretches, there being only one caller (the render settings
    // panel) and no column to lead it.
    enum class Anchor { TopLeft, LeftCenter, BottomLeft, TopRight, RightCenter, BottomRight,
                        LeftEdge, RightEdge };

    // How far a LeftEdge entry (the rail) stands off the viewport's left,
    // top and bottom edges - the plan's 14px, two pixels tighter than the
    // corner/edge-centre anchors' own margin, because a rail pinned to an
    // edge hugs it while a card floating in a corner stands off it. Public
    // so a caller that needs to guarantee a LeftEdge entry actually fits -
    // MainWindow derives the viewport's minimum height from the rail's own
    // sizeHint() plus two of these - reads the real margin relayout() places
    // against, rather than a second copy that could drift from it.
    static constexpr int kEdgeMargin = 14;

    // The gap between two widgets stacked in the same column - the rail
    // below the pill, a TopLeft card below the one above it. Public for the
    // same reason kEdgeMargin is: MainWindow derives the viewport's minimum
    // height as a stacked SUM of the pill's and the rail's own sizeHint()s
    // plus this gap, and it has to read the real value relayout() places
    // against rather than a second copy that could drift from it.
    static constexpr int kStackGap = 8;

    explicit ViewportOverlay(QWidget* viewport);

    void addWidget(QWidget* widget, Anchor anchor);
    void relayout();

    // The rectangles currently occupied by the anchored widgets - the chip
    // clusters, the axis gizmo, the unit readout, the walkthrough guide.
    // Anything that places itself freely over the viewport (the toast) asks
    // for this rather than naming the widget types it happens to know about,
    // so a cluster added later is stepped around for free instead of
    // becoming the next collision to discover.
    std::vector<QRect> occupiedRects() const;

signals:
    // Emitted once relayout() has moved and raise()d every anchored entry.
    //
    // The overlay is the LAST thing to place anything on a viewport resize
    // (it installs its filter first, in MainWindow::buildOverlay(), and Qt
    // runs event filters last-installed-first), so anything that positions
    // itself against an anchored widget - the toast and the hint balloon
    // both step around the walkthrough guide - was reading the guide's
    // pre-resize rectangle and then being raise()d over. Ordering off this
    // signal, rather than off filter registration order, is what makes
    // "after the guide has moved" a property of the code instead of an
    // accident of construction order.
    void laidOut();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry {
        QPointer<QWidget> widget;
        Anchor anchor;
    };

    QWidget* myViewport = nullptr;
    std::vector<Entry> myEntries;
};
