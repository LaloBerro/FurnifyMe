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
    // (a stretch between Select Edges and Undo puts history at the bottom);
    // this anchor only decides how much slack there is.
    //
    // A visible LeftEdge entry also moves the three left-hand anchors -
    // TopLeft, LeftCenter, BottomLeft - out past it, so a card anchored there
    // (the items drawer) lands BESIDE the spine rather than underneath it.
    // That is a property of the layout rather than of any one caller: it does
    // not depend on the order entries were added, and it holds for whatever
    // is anchored left next.
    enum class Anchor { TopLeft, LeftCenter, BottomLeft, TopRight, RightCenter, BottomRight,
                        LeftEdge };

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
