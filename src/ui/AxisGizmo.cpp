#include "AxisGizmo.h"

#include "OcctViewWidget.h"
#include "Theme.h"

#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

// Unity's axis colours, adjusted to sit on our dark viewport - promoted to
// Theme::Spec tokens in Milestone 3 (Task 5), editable in the Appearance
// panel. Read LIVE inside paintEvent() rather than cached here: a QColor
// array at file scope would be exactly the "cached appearance value" survives
// a themeChanged that Theme.h's own Notifier comment warns against - the
// gizmo would keep drawing the shipped hues after an edit until the next
// process restart. Byte-identical to what this array held before the tokens
// existed, so defaultSpec() repaints nothing on its own.
QColor axisColor(int axis)
{
    switch (axis) {
        case 0: return Theme::gizmoAxisX();
        case 1: return Theme::gizmoAxisY();
        default: return Theme::gizmoAxisZ();
    }
}
constexpr char kAxisLetter[3] = {'x', 'y', 'z'};

// The drawing's own numbers now live in AxisCard (see AxisGizmo.h) so the
// in-scene Move gizmo can be a proportional copy of this card rather than an
// interpretation of it. These four names are kept as local aliases because
// paintEvent() below reads them a dozen times and `kRadius` says what it is.
constexpr double kRadius = AxisCard::kArmPx;
constexpr double kConeSize = AxisCard::kConePx;
constexpr double kBallSize = AxisCard::kBallPx;
constexpr double kHitRadius = 11.0;   // click tolerance around a tip - this
                                      // card's own input, not part of the
                                      // drawing, so it stays here

// The card's OWN corner radius - paintEvent() below calls
// Theme::paintSurface(painter, rect()) with no third argument, so this is
// that default (see Theme.h). Named apart from kRadius above, which is the
// arm length in pixels and an unrelated number that happens to share the
// family default's old value.
constexpr int kCardRadius = 8;

// The widget's own centre. It used to be the centre of the area ABOVE the
// label chip, which needed the chip's height repeated here; with the chip gone
// the axes have the whole widget, so this reads the height rather than keeping
// a second copy of the number AxisGizmo::kHeight already holds.
QPointF hubCenter(const QWidget& w)
{
    return QPointF(w.width() / 2.0, w.height() / 2.0);
}

}  // namespace

// --- the card's own numbers, shared with the in-scene copy -------------------

QColor AxisCard::hubColour()
{
    // The one hex literal this drawing carries. It is deliberately NOT a Theme
    // token: a hub is neutral by definition and a user who tinted it would be
    // adding a fourth axis colour to a control whose whole language is three.
    return QColor("#c8c8cc");
}

QFont AxisCard::letterFont()
{
    QFont font = Theme::badgeFont();
    font.setBold(true);
    return font;
}

double AxisCard::letterHeightPx(QChar letter)
{
    // MEASURED off the font, never derived from its point size: a guess at
    // "8pt is about 11 pixels" is exactly the kind of number that is right on
    // one machine. And the TIGHT box, not capHeight() or height(): what a
    // pixel probe can see of a lowercase x, y or z is its own ink, and none of
    // the three reaches a capital's height - the first spelling of this
    // function said cap height was "the honest ceiling" for them and measured
    // 7.7 px against a rendering of 4.8.
    return QFontMetricsF(letterFont()).tightBoundingRect(QString(letter)).height();
}

double AxisCard::letterEmPx()
{
    // QFontInfo, not the QFont's own pointSizeF(): the request and what the
    // font engine actually resolved are two different numbers, and it is the
    // resolved one the card rasterizes with. Falls back through the metrics'
    // line height only if a platform ever hands back 0, which would otherwise
    // scale the scene's letters to nothing at all.
    const double px = QFontInfo(letterFont()).pixelSize();
    return px > 0.0 ? px : QFontMetricsF(letterFont()).height();
}

void AxisCard::computeTips(const gp_Dir& right, const gp_Dir& up, const gp_Dir& view,
                           Tip tips[6])
{
    // Project each world axis into the camera frame: `right` along the
    // camera's right vector, `up` along its up vector, `depth` along the view
    // direction (pointing away from the eye).
    //
    // At an EXACT named view (a true pole since Task 6.2's +-90 fix), the
    // viewed axis and its opposite both project onto the hub, so the far tip
    // is coincident with the near one and unreachable until the user orbits
    // away - the same property Blender's and Fusion's gizmos have, permanent
    // and harmless: a depth sort keeps the NEAR tip on top, so clicking the
    // hub re-snaps the view already faced rather than flipping it.
    int index = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const double ax = axis == 0 ? 1.0 : 0.0;
        const double ay = axis == 1 ? 1.0 : 0.0;
        const double az = axis == 2 ? 1.0 : 0.0;
        const double sx = ax * right.X() + ay * right.Y() + az * right.Z();
        const double sy = ax * up.X() + ay * up.Y() + az * up.Z();
        const double sz = ax * view.X() + ay * view.Y() + az * view.Z();
        for (int sign = 0; sign < 2; ++sign) {
            const double s = sign == 0 ? 1.0 : -1.0;
            Tip& tip = tips[index++];
            tip.axis = axis;
            tip.positive = (sign == 0);
            tip.right = s * sx;
            tip.up = s * sy;
            tip.depth = s * sz;
        }
    }
}

AxisGizmo::AxisGizmo(OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    // Through Theme::wholeDevicePixels() - see Theme.h, and see
    // WalkthroughPanel's constructor for why a setFixedSize() card has to do
    // this itself rather than leaving it to ViewportOverlay: resize() on a
    // fixed-size widget is a silent no-op. This one measured 147.5 device
    // pixels tall at 125% scaling.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &AxisGizmo::applyTheme);

    // Repaint whenever the camera moves, so the gizmo rotates with the scene.
    connect(myView, &OcctViewWidget::cameraChanged, this,
            static_cast<void (QWidget::*)()>(&QWidget::update));
}

void AxisGizmo::applyTheme()
{
    // Through Theme::wholeDevicePixels() - see Theme.h, and see
    // WalkthroughPanel's constructor for why a setFixedSize() card has to do
    // this itself rather than leaving it to ViewportOverlay: resize() on a
    // fixed-size widget is a silent no-op. This one measured 147.5 device
    // pixels tall at 125% scaling.
    setFixedSize(Theme::wholeDevicePixels(sizeHint()));
    // Baseline for this widget's own font() (what the type-scale sweep in
    // gui_smoke checks): the only text it paints now is the axis letters,
    // which are badge-sized. A per-widget stylesheet wins over the app-wide
    // one regardless of selector specificity, so this sticks reliably rather
    // than fighting the cascade.
    //
    // "background: transparent" rides along in the SAME string rather than a
    // separate Theme::makeSurfaceTransparent(this) call - see that function's
    // header comment for why this is the one family member that folds it in
    // here instead: this is the one place this widget's own stylesheet is
    // set at all, and it runs again on every Theme broadcast, so a second
    // call would just replace whichever of the two ran last.
    setStyleSheet(QStringLiteral("background: transparent; font-size: %1pt;")
                      .arg(Theme::badgeFont().pointSizeF()));
    update();
}

void AxisGizmo::computeTips(Tip tips[6]) const
{
    // THE projection is AxisCard::computeTips() now, shared with the in-scene
    // Move gizmo - see AxisGizmo.h for why two copies of it was the whole
    // defect. All this adds is the card's own coordinates: kRadius pixels per
    // unit, and screen y running downward where `up` runs up.
    const CameraController& cam = myView->camera();
    AxisCard::Tip projected[6];
    AxisCard::computeTips(cam.rightVector(), cam.upVector(), cam.viewDirection(), projected);

    const QPointF centre = hubCenter(*this);
    for (int i = 0; i < 6; ++i) {
        tips[i].axis = projected[i].axis;
        tips[i].positive = projected[i].positive;
        tips[i].screen =
            centre + QPointF(projected[i].right * kRadius, -projected[i].up * kRadius);
        tips[i].depth = projected[i].depth;
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
        case 2:   // straight above or below - the true pole (Task 6.2's fix)
            // Milestone 5 item 4: force the squared azimuth here too, or a
            // click on the Top/Bottom tip from an arbitrary orbit position
            // rotated world X/Y arbitrarily on screen - the same bug
            // setViewTop() had, reached through the gizmo instead of the
            // menu/key. See CameraController::kTopBottomSquaredAzimuthDeg.
            goal.azimuthDeg = CameraController::kTopBottomSquaredAzimuthDeg;
            goal.elevationDeg = s * 90.0;
            break;
    }
    // An axis view IS a face-on view, and perspective convergence is exactly
    // what stops one reading as square. Set before the flight, not after: the
    // animation's very first frame already goes through applyCameraState(),
    // and a look that only becomes orthographic once it lands would flash.
    // It is a loan - the user's first orbit hands it back and their base
    // projection returns (see CameraController::Projection).
    myView->camera().setTemporaryOrtho(true);
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
    // a ground grid over its own flat background colour
    // (OcctViewWidget::initializeViewer() sets SetBackgroundColor once, not a
    // gradient), so a flat viewport() rectangle read as a lighter BOX sitting
    // on the scene the moment the grid was under it - a fake transparency
    // that announced itself. Since fix round 1's ruling is that nothing over
    // the GL surface is translucent anyway, the honest form is the one the
    // rail and the drawer already wear: panel() fill and a 1px border().
    //
    // Radius 8, the family default - not the 0 this widget carried as a
    // stopgap while its rounded corners had nowhere honest to land. That gap
    // is closed now for a second reason as well as the first: the QOpenGLWidget
    // migration means the area outside the rounded shape and inside the
    // widget rect genuinely composites through to the live scene behind it
    // (see paintSurface()'s own header), rather than needing a flat viewport()
    // fill to stand in for "transparent". This is the settlement Task 4
    // referenced; the gizmo rejoins the family radius rather than being the
    // one card that dodges it.
    Theme::paintSurface(painter, rect());

    Tip tips[6];
    computeTips(tips);

    // Painter's algorithm: draw far tips first so near ones overlap them.
    Tip* order[6];
    for (int i = 0; i < 6; ++i) order[i] = &tips[i];
    std::sort(order, order + 6,
              [](const Tip* a, const Tip* b) { return a->depth > b->depth; });

    const QPointF centre = hubCenter(*this);

    for (const Tip* tip : order) {
        QColor colour = axisColor(tip->axis);
        const bool hovered = tip->axis == myHoverAxis && tip->positive == myHoverPositive;
        // Far side dims, hover brightens - same depth cue Unity uses.
        if (tip->depth > 0.15) colour = colour.darker(140);
        if (hovered) colour = colour.lighter(130);

        if (tip->positive) {
            // Arm plus a cone at the end, pointing outward.
            painter.setPen(QPen(colour, AxisCard::kArmStrokePx));
            const QPointF dir = tip->screen - centre;
            const double length = std::hypot(dir.x(), dir.y());
            if (length > 1.0) {
                const QPointF unit = dir / length;
                const QPointF normal(-unit.y(), unit.x());
                const QPointF base = tip->screen - unit * kConeSize;
                painter.drawLine(centre, base);
                QPainterPath cone;
                cone.moveTo(tip->screen + unit * (kConeSize * AxisCard::kConeApexFactor));
                cone.lineTo(base + normal * (kConeSize * AxisCard::kConeHalfWidthFactor));
                cone.lineTo(base - normal * (kConeSize * AxisCard::kConeHalfWidthFactor));
                cone.closeSubpath();
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour);
                painter.drawPath(cone);

                // Axis letter just past the cone - a small badge, like the
                // shortcut badges Theme::badgeFont() is sized for.
                painter.setPen(colour.lighter(AxisCard::kLetterLighten));
                painter.setFont(AxisCard::letterFont());
                const QPointF letterPos = tip->screen + unit * AxisCard::kLetterOffsetPx;
                painter.drawText(QRectF(letterPos.x() - 6, letterPos.y() - 7, 12, 14),
                                 Qt::AlignCenter, QString(QLatin1Char(kAxisLetter[tip->axis])));
            }
        } else {
            // Negative axis: a short arm and a hollow ball, like Unity.
            painter.setPen(QPen(colour, AxisCard::kNegativeStrokePx));
            const QPointF dir = tip->screen - centre;
            painter.drawLine(centre + dir * AxisCard::kNegativeStubStart, tip->screen);
            // The card's own fill, not Theme::viewport() - a "hollow" ball is
            // hollow onto whatever this widget is painted on, and that is the
            // card now.
            painter.setBrush(Theme::panel());
            painter.drawEllipse(tip->screen, kBallSize, kBallSize);
        }
    }

    // Hub on top of everything.
    painter.setPen(Qt::NoPen);
    painter.setBrush(AxisCard::hubColour());
    painter.drawEllipse(centre, AxisCard::kHubPx, AxisCard::kHubPx);
}
