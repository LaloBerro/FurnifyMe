#pragma once
// Unity-style orientation gizmo: colored axis cones around a hub, projected
// live from the camera, each one a button that snaps the view to its axis.
// Painted with QPainter as an overlay child of the viewport - the view cube it
// replaces could only ever look like a box.
//
// It wears the floating-surface family's own card (Theme::paintSurface()),
// like the rail, the drawer, the guide, the balloon and the toast - at the
// family's default radius (8), the same rounding every other card in the
// shell carries. It used to fill itself flat with Theme::viewport() instead,
// on the theory that the panel would disappear against the sky - which it
// did, right up until the viewport painted a ground grid over its own flat
// background colour, after which the flat fill read as a lighter box pasted
// onto the scene. Nothing over this surface is translucent (see Theme.h), so
// an honest card is the only alternative to a fake one; paintSurface()'s
// opaque ground fill is what lets this card round its corners without
// leaving the small triangles outside the rounded shape and inside the
// widget's own rect unpainted - the reason it no longer needs the radius-zero
// stopgap it once carried. See AxisGizmo.cpp's paintEvent() for the whole
// argument.
//
// Axes and tips, and nothing else. It used to carry a chip below them naming
// the current view, and that chip's job - showing the name, and snapping back
// to the angled view when clicked - moved into the app bar. The string itself
// has one source, OcctViewWidget::viewLabelText(); this widget no longer
// knows it exists.
#include <QPointF>
#include <QWidget>

class OcctViewWidget;

class AxisGizmo : public QWidget {
    Q_OBJECT

public:
    explicit AxisGizmo(OcctViewWidget* view, QWidget* parent = nullptr);

    // Screen-space centre of an axis tip: axis 0=X, 1=Y, 2=Z. Exposed so the
    // test suite can click exactly where a user would.
    QPointF tipCenter(int axis, bool positive) const;

signals:
    // A tip was clicked and the camera is on its way to that axis. The gizmo
    // deliberately does not know what anyone makes of that: MainWindow
    // connects this to its own "a named view was used" bookkeeping, and this
    // widget keeps knowing nothing but its OcctViewWidget.
    void viewSnapped();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override { return QSize(120, kHeight); }

private:
    // The whole widget now: the projected axes and nothing under them.
    static constexpr int kHeight = 118;

    struct Tip {
        int axis = 0;        // 0=X 1=Y 2=Z
        bool positive = true;
        QPointF screen;      // widget coordinates
        double depth = 0.0;  // along the view direction; larger = farther away
    };

    // The six tips for the current camera pose, unsorted.
    void computeTips(Tip tips[6]) const;
    void snapToAxis(int axis, bool positive);
    // The two appearance values this card cannot re-derive inside
    // paintEvent(): its per-widget font-size stylesheet, and its FIXED size -
    // sizeHint() is a constant here, but wholeDevicePixels() is applied to it
    // once, and setFixedSize() means ViewportOverlay's own rounding is a
    // silent no-op on this widget (see Theme.h). Both are set from here at
    // construction and again on every Theme broadcast.
    void applyTheme();

    OcctViewWidget* myView = nullptr;
    int myHoverAxis = -1;        // -1 none; else axis index
    bool myHoverPositive = true;
};
