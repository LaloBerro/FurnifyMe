#pragma once
// Unity-style orientation gizmo: colored axis cones around a hub, projected
// live from the camera, each one a button that snaps the view to its axis.
// Painted with QPainter as an overlay child of the viewport - the view cube it
// replaces could only ever look like a box.
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

    OcctViewWidget* myView = nullptr;
    int myHoverAxis = -1;        // -1 none; else axis index
    bool myHoverPositive = true;
};
