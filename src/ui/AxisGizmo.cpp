#include "AxisGizmo.h"

#include "OcctViewWidget.h"
#include "Theme.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

// Unity's axis colours, adjusted to sit on our dark viewport.
const QColor kAxisColor[3] = {QColor("#e0564a"),    // X red
                              QColor("#7fc84e"),    // Y green
                              QColor("#4a80e0")};   // Z blue
constexpr char kAxisLetter[3] = {'x', 'y', 'z'};

constexpr double kRadius = 36.0;      // arm length in pixels
constexpr double kConeSize = 9.0;     // positive-tip cone
constexpr double kBallSize = 5.5;     // negative-tip hollow ball
constexpr double kHitRadius = 11.0;   // click tolerance around a tip
constexpr int kGizmoHeight = 118;     // area above the label chip

QPointF hubCenter(const QWidget& w)
{
    return QPointF(w.width() / 2.0, kGizmoHeight / 2.0);
}

}  // namespace

AxisGizmo::AxisGizmo(OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());

    // Repaint whenever the camera moves, so the gizmo rotates with the scene.
    connect(myView, &OcctViewWidget::cameraChanged, this,
            static_cast<void (QWidget::*)()>(&QWidget::update));
}

void AxisGizmo::computeTips(Tip tips[6]) const
{
    // Project each world axis into the camera frame: screen x along the
    // camera's right vector, screen y along -up, depth along the view
    // direction (pointing away from the eye).
    const CameraController& cam = myView->camera();
    const gp_Dir right = cam.rightVector();
    const gp_Dir up = cam.upVector();
    const gp_Dir viewDir = cam.viewDirection();

    const QPointF centre = hubCenter(*this);
    int index = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const double ax = axis == 0 ? 1.0 : 0.0;
        const double ay = axis == 1 ? 1.0 : 0.0;
        const double az = axis == 2 ? 1.0 : 0.0;
        const double sx = ax * right.X() + ay * right.Y() + az * right.Z();
        const double sy = ax * up.X() + ay * up.Y() + az * up.Z();
        const double sz = ax * viewDir.X() + ay * viewDir.Y() + az * viewDir.Z();
        for (int sign = 0; sign < 2; ++sign) {
            const double s = sign == 0 ? 1.0 : -1.0;
            Tip& tip = tips[index++];
            tip.axis = axis;
            tip.positive = (sign == 0);
            tip.screen = centre + QPointF(s * sx * kRadius, -s * sy * kRadius);
            tip.depth = s * sz;
        }
    }
}

QPointF AxisGizmo::tipCenter(int axis, bool positive) const
{
    Tip tips[6];
    computeTips(tips);
    for (const Tip& tip : tips) {
        if (tip.axis == axis && tip.positive == positive) return tip.screen;
    }
    return hubCenter(*this);
}

QRectF AxisGizmo::labelRect() const
{
    return QRectF(10.0, kGizmoHeight, width() - 20.0, height() - kGizmoHeight - 4.0);
}

QPointF AxisGizmo::labelCenter() const
{
    return labelRect().center();
}

QString AxisGizmo::labelText() const
{
    const CameraState& s = myView->camera().state();
    const double el = s.elevationDeg;
    // Azimuth normalized to (-180, 180] for comparison.
    double az = std::fmod(s.azimuthDeg, 360.0);
    if (az > 180.0) az -= 360.0;
    if (az <= -180.0) az += 360.0;

    const double tolerance = 0.5;
    if (el >= 87.5) return QStringLiteral("Top");
    if (el <= -87.5) return QStringLiteral("Bottom");
    if (std::fabs(el) < tolerance) {
        if (std::fabs(az) < tolerance) return QStringLiteral("Front");
        if (std::fabs(std::fabs(az) - 180.0) < tolerance) return QStringLiteral("Back");
        if (std::fabs(az + 90.0) < tolerance) return QStringLiteral("Right");
        if (std::fabs(az - 90.0) < tolerance) return QStringLiteral("Left");
    }
    return QStringLiteral("Persp");
}

void AxisGizmo::snapToAxis(int axis, bool positive)
{
    // Clicking a tip views the scene from that axis: the eye moves onto it.
    CameraState goal = myView->camera().state();
    const double s = positive ? 1.0 : -1.0;
    switch (axis) {
        case 0:   // eye on +/-X
            goal.azimuthDeg = positive ? -90.0 : 90.0;
            goal.elevationDeg = 0.0;
            break;
        case 1:   // eye on +/-Y
            goal.azimuthDeg = positive ? 0.0 : 180.0;
            goal.elevationDeg = 0.0;
            break;
        case 2:   // straight above or below, held just inside the clamp
            goal.elevationDeg = s * 88.0;
            break;
    }
    myView->animateTo(goal);
}

void AxisGizmo::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        // Let orbit and pan drags pass through to the viewport underneath.
        event->ignore();
        return;
    }

    const QPointF pos = event->position();
    if (labelRect().contains(pos)) {
        CameraState goal = myView->camera().state();
        goal.azimuthDeg = -45.0;
        goal.elevationDeg = 30.0;
        myView->animateTo(goal);
        return;
    }

    Tip tips[6];
    computeTips(tips);
    // Nearest tip within tolerance wins; prefer the nearer of overlapping tips.
    const Tip* best = nullptr;
    double bestScore = kHitRadius;
    for (const Tip& tip : tips) {
        const double d = QLineF(pos, tip.screen).length();
        if (d < bestScore || (best && std::fabs(d - bestScore) < 1e-9 &&
                              tip.depth < best->depth)) {
            best = &tip;
            bestScore = d;
        }
    }
    if (best) snapToAxis(best->axis, best->positive);
}

void AxisGizmo::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    const bool overLabel = labelRect().contains(pos);

    int hoverAxis = -1;
    bool hoverPositive = true;
    if (!overLabel) {
        Tip tips[6];
        computeTips(tips);
        double bestScore = kHitRadius;
        for (const Tip& tip : tips) {
            const double d = QLineF(pos, tip.screen).length();
            if (d < bestScore) {
                hoverAxis = tip.axis;
                hoverPositive = tip.positive;
                bestScore = d;
            }
        }
    }

    if (hoverAxis != myHoverAxis || hoverPositive != myHoverPositive ||
        overLabel != myHoverLabel) {
        myHoverAxis = hoverAxis;
        myHoverPositive = hoverPositive;
        myHoverLabel = overLabel;
        update();
    }
}

void AxisGizmo::leaveEvent(QEvent* /*event*/)
{
    myHoverAxis = -1;
    myHoverLabel = false;
    update();
}

void AxisGizmo::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The app stylesheet would paint this widget chrome-black. True per-pixel
    // transparency over the OCCT GL surface is the one compositing case the
    // overlay probe flagged as unreliable, so fill with the viewport's own
    // colour instead - the panel disappears against the empty sky.
    painter.fillRect(rect(), Theme::viewport());

    Tip tips[6];
    computeTips(tips);

    // Painter's algorithm: draw far tips first so near ones overlap them.
    Tip* order[6];
    for (int i = 0; i < 6; ++i) order[i] = &tips[i];
    std::sort(order, order + 6,
              [](const Tip* a, const Tip* b) { return a->depth > b->depth; });

    const QPointF centre = hubCenter(*this);

    for (const Tip* tip : order) {
        QColor colour = kAxisColor[tip->axis];
        const bool hovered = tip->axis == myHoverAxis && tip->positive == myHoverPositive;
        // Far side dims, hover brightens - same depth cue Unity uses.
        if (tip->depth > 0.15) colour = colour.darker(140);
        if (hovered) colour = colour.lighter(130);

        if (tip->positive) {
            // Arm plus a cone at the end, pointing outward.
            painter.setPen(QPen(colour, 2.0));
            const QPointF dir = tip->screen - centre;
            const double length = std::hypot(dir.x(), dir.y());
            if (length > 1.0) {
                const QPointF unit = dir / length;
                const QPointF normal(-unit.y(), unit.x());
                const QPointF base = tip->screen - unit * kConeSize;
                painter.drawLine(centre, base);
                QPainterPath cone;
                cone.moveTo(tip->screen + unit * (kConeSize * 0.4));
                cone.lineTo(base + normal * (kConeSize * 0.55));
                cone.lineTo(base - normal * (kConeSize * 0.55));
                cone.closeSubpath();
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour);
                painter.drawPath(cone);

                // Axis letter just past the cone.
                painter.setPen(colour.lighter(115));
                QFont letterFont = font();
                letterFont.setPointSizeF(8.0);
                letterFont.setBold(true);
                painter.setFont(letterFont);
                const QPointF letterPos = tip->screen + unit * 9.0;
                painter.drawText(QRectF(letterPos.x() - 6, letterPos.y() - 7, 12, 14),
                                 Qt::AlignCenter, QString(QLatin1Char(kAxisLetter[tip->axis])));
            }
        } else {
            // Negative axis: a short arm and a hollow ball, like Unity.
            painter.setPen(QPen(colour, 1.4));
            const QPointF dir = tip->screen - centre;
            painter.drawLine(centre + dir * 0.35, tip->screen);
            painter.setBrush(Theme::viewport());
            painter.drawEllipse(tip->screen, kBallSize, kBallSize);
        }
    }

    // Hub on top of everything.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#c8c8cc"));
    painter.drawEllipse(centre, 5.0, 5.0);

    // Label chip.
    const QRectF chip = labelRect();
    QPainterPath chipPath;
    chipPath.addRoundedRect(chip, 6.0, 6.0);
    painter.setBrush(myHoverLabel ? Theme::chipHover() : Theme::chip());
    painter.drawPath(chipPath);
    painter.setPen(Theme::text());
    QFont labelFont = font();
    labelFont.setPointSizeF(9.0);
    painter.setFont(labelFont);
    painter.drawText(chip, Qt::AlignCenter,
                     QStringLiteral("≡ ") + labelText());
}
