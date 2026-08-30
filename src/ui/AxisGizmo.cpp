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

// The widget's own centre. It used to be the centre of the area ABOVE the
// label chip, which needed the chip's height repeated here; with the chip gone
// the axes have the whole widget, so this reads the height rather than keeping
// a second copy of the number AxisGizmo::kHeight already holds.
QPointF hubCenter(const QWidget& w)
{
    return QPointF(w.width() / 2.0, w.height() / 2.0);
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
    // Baseline for this widget's own font() (what the type-scale sweep in
    // gui_smoke checks): the only text it paints now is the axis letters,
    // which are badge-sized. A per-widget stylesheet wins over the app-wide
    // one regardless of selector specificity, so this sticks reliably rather
    // than fighting the cascade.
    setStyleSheet(QStringLiteral("font-size: %1pt;").arg(Theme::badgeFont().pointSizeF()));

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
    emit viewSnapped();
}

void AxisGizmo::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        // Let orbit and pan drags pass through to the viewport underneath.
        event->ignore();
        return;
    }

    const QPointF pos = event->position();

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

    int hoverAxis = -1;
    bool hoverPositive = true;
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

    if (hoverAxis != myHoverAxis || hoverPositive != myHoverPositive) {
        myHoverAxis = hoverAxis;
        myHoverPositive = hoverPositive;
        update();
    }
}

void AxisGizmo::leaveEvent(QEvent* /*event*/)
{
    myHoverAxis = -1;
    update();
}

void AxisGizmo::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A card, like every other floating widget over this viewport, rather
    // than a flat fill of Theme::viewport() pretending to be transparent.
    // That trick only ever worked against the empty sky: the viewport paints
    // a gradient and a ground grid, so a flat viewport() rectangle read as a
    // lighter BOX sitting on the scene - a fake transparency that announced
    // itself. Since fix round 1's ruling is that nothing over the GL surface
    // is translucent anyway, the honest form is the one the rail and the
    // drawer already wear: panel() fill and a 1px border().
    //
    // Radius ZERO, unlike the rest of the family, and that is the whole point
    // rather than an oversight. A rounded card does not cover its own four
    // corners, and over this surface an uncovered pixel is not transparent
    // but whatever the driver left there - black. Every other card in the
    // shell already carries those nubs and Task 5 will settle whether the
    // family keeps them; this widget did NOT, because a flat fillRect()
    // covers every pixel it owns. Bringing it into the family at radius 8
    // would have been a punch-list item that introduced the very defect the
    // punch list above it was clearing. Zero keeps the card filling its rect
    // exactly, which is what ruling 8 asks for, and leaves the corner
    // question where it belongs.
    Theme::paintSurface(painter, rect(), 0);

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

                // Axis letter just past the cone - a small badge, like the
                // shortcut badges Theme::badgeFont() is sized for.
                painter.setPen(colour.lighter(115));
                QFont letterFont = Theme::badgeFont();
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
            // The card's own fill, not Theme::viewport() - a "hollow" ball is
            // hollow onto whatever this widget is painted on, and that is the
            // card now.
            painter.setBrush(Theme::panel());
            painter.drawEllipse(tip->screen, kBallSize, kBallSize);
        }
    }

    // Hub on top of everything.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#c8c8cc"));
    painter.drawEllipse(centre, 5.0, 5.0);
}
