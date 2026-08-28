#pragma once
// Unity-style orientation gizmo: colored axis cones around a hub, projected
// live from the camera, each one a button that snaps the view to its axis.
// Painted with QPainter as an overlay child of the viewport - the OCCT view
// cube it replaces could only ever look like a box.
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

class OcctViewWidget;

class AxisGizmo : public QWidget {
    Q_OBJECT

public:
    explicit AxisGizmo(OcctViewWidget* view, QWidget* parent = nullptr);

    // Screen-space centre of an axis tip: axis 0=X, 1=Y, 2=Z. Exposed so the
    // test suite can click exactly where a user would.
    QPointF tipCenter(int axis, bool positive) const;
    QPointF labelCenter() const;

    // "Top", "Front", ... when the camera is axis-aligned; "Persp" otherwise.
    QString labelText() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override { return QSize(120, 148); }

private:
    struct Tip {
        int axis = 0;        // 0=X 1=Y 2=Z
        bool positive = true;
        QPointF screen;      // widget coordinates
        double depth = 0.0;  // along the view direction; larger = farther away
    };

    // The six tips for the current camera pose, unsorted.
    void computeTips(Tip tips[6]) const;
    QRectF labelRect() const;
    void snapToAxis(int axis, bool positive);

    OcctViewWidget* myView = nullptr;
    int myHoverAxis = -1;        // -1 none; else axis index
    bool myHoverPositive = true;
    bool myHoverLabel = false;
};
