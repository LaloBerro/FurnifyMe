#include "TransformGizmo.h"

#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <Aspect_TypeOfLine.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPoint>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

// --- the chip's own card metrics (PullArrow's, one gizmo over) --------------
constexpr int kPad = 10;
constexpr int kMinWidth = 128;
constexpr int kLabelHeight = 18;
constexpr int kValueHeight = 22;
constexpr int kHintGap = 4;
constexpr int kHintHeight = 14;
constexpr int kChipGap = 18;
constexpr int kEdgeInset = 8;

// --- the gizmo's own size, in SCREEN pixels --------------------------------
//
// The axis card (src/ui/AxisGizmo.cpp) draws 36-pixel arms with a 9-pixel cone
// on a 2.0-pixel stroke. This is that language at viewport scale: roughly
// two and a half times the arm, the same cone-to-arm proportion, and the
// IDENTICAL stroke weight, because a thicker line would be a different visual
// family rather than the same one further away.
//
// The arm length is the one number here chosen by eye rather than derived, and
// the spec parks it: Phase 3 is the user's feel-test on sizes, grab tolerances
// and chip placement. It is deliberately under the manipulator's own screen cap
// (kGizmoMaxViewportFraction, 15% of the viewport's smaller side - about 116 px
// in the suite's 1100x800 window), so swapping tools with Space never makes the
// handle jump outward.
constexpr double kArmPixels = 92.0;
constexpr double kConePixels = 20.0;
constexpr double kConeRadiusPixels = 6.0;
constexpr double kHubRadiusPixels = 4.5;
constexpr double kArmStrokePx = 2.0;
// How many segments a cone's base ring and a hub ring are drawn with. Twelve
// at eighteen screen pixels leaves no visible facet, and the whole gizmo is
// still under two hundred line segments.
constexpr int kRingSegments = 12;

// Spelled out rather than M_PI, which MSVC does not define without
// _USE_MATH_DEFINES - the same literal every other file in this tree carries.
constexpr double kPi = 3.14159265358979323846;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// One AIS object per colour, never pickable - the same shape as
// PullArrowLines, DimensionRenderer's DimensionLines and OcctViewWidget's
// SketchPointMarker.
//
// Never pickable is the whole reason this gizmo needs none of AIS_Manipulator's
// workaround pile. An AIS object with a real ComputeSelection joins the
// hover/selection pipeline, and AIS_ManipulatorOwner outranks a shape's own
// owner - which is why the manipulator has to be Deactivate()d around every
// additive pick, and why an arm crossing a second body used to swallow the
// click that meant to add it. This gizmo is hit-tested in SCREEN SPACE by
// OcctViewWidget (moveGizmoAxisAt()), so it never competes for a pick at all:
// it takes a press outright, before the picker runs, or it takes nothing.
class GizmoLines : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Quantity_Color colour;
    double width = 2.0;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (lines.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect =
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID,
                                       static_cast<Standard_ShortReal>(width));
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(lines);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // See the class comment: picked in screen space by the viewport, not
        // through AIS.
    }
};

// Two unit vectors perpendicular to `along` and to each other - the frame a
// ring around that direction is drawn in. Any pair will do; what matters is
// that it is well defined for every direction, which the fallback covers.
void perpendicularFrame(const gp_Dir& along, gp_Vec& u, gp_Vec& v)
{
    gp_Vec candidate(0.0, 0.0, 1.0);
    if (std::fabs(gp_Vec(along).Dot(candidate)) > 0.9) candidate = gp_Vec(1.0, 0.0, 0.0);
    u = gp_Vec(along).Crossed(candidate);
    if (u.Magnitude() < 1.0e-9) u = gp_Vec(along).Crossed(gp_Vec(0.0, 1.0, 0.0));
    u.Normalize();
    v = gp_Vec(along).Crossed(u);
    v.Normalize();
}

}  // namespace

// --- the shared presentation base -------------------------------------------

void GizmoRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void GizmoRenderer::detach()
{
    if (!myContext.IsNull()) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
    }
    myObjects.clear();
    myContext.Nullify();
}

bool GizmoRenderer::clear()
{
    const bool had = !myObjects.empty();
    if (!myContext.IsNull() && had) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
        // No UpdateCurrentViewer(): the caller owns the frame since the
        // QOpenGLWidget migration - PullArrowRenderer::clear()'s own rule.
    }
    myObjects.clear();
    return had;
}

void GizmoRenderer::reapplyTheme()
{
    if (!isShowing()) return;
    myForceRebuild = true;
    show(myPivot, myViewDirection, myWorldPerPixel);   // caller owns the frame
}

bool GizmoRenderer::show(const gp_Pnt& pivot, const gp_Dir& viewDirection, double worldPerPixel)
{
    if (myContext.IsNull()) return false;

    // Nothing to do when nothing has actually moved. MoveTool::reposition()
    // drives this from cameraChanged AND from every appStateChanged, so the
    // great majority of calls ask for exactly the gizmo already on screen -
    // and a rebuild is emphatically not free (PullArrowRenderer::show() has
    // the measurements: 33.3 ms per rebuild before its own early-out, 0.17 ms
    // after). The tolerances are that function's, for the same reasons.
    constexpr double kHalfDegree = 0.0087266;   // radians
    if (!myForceRebuild && isShowing() && pivot.IsEqual(myPivot, 1.0e-9) &&
        viewDirection.IsEqual(myViewDirection, kHalfDegree) &&
        std::fabs(worldPerPixel - myWorldPerPixel) <=
            std::max(myWorldPerPixel, 1.0e-9) * 0.01) {
        return false;
    }
    myForceRebuild = false;

    clear();

    myPivot = pivot;
    myViewDirection = viewDirection;
    myWorldPerPixel = std::max(worldPerPixel, 1.0e-9);

    buildStrokes();
    return true;
}

void GizmoRenderer::addStrokes(const std::vector<Stroke>& strokes, const QColor& colour,
                               double widthPx)
{
    if (myContext.IsNull() || strokes.empty()) return;

    Handle(Graphic3d_ArrayOfSegments) segs =
        new Graphic3d_ArrayOfSegments(static_cast<Standard_Integer>(strokes.size() * 2));
    for (const Stroke& s : strokes) {
        segs->AddVertex(s.a);
        segs->AddVertex(s.b);
    }

    Handle(GizmoLines) object = new GizmoLines();
    object->lines = segs;
    object->colour = toOcct(colour);
    object->width = widthPx;
    myContext->Display(object, 0, -1, Standard_False);   // mode -1: feedback only
    // Drawn over everything. A move gizmo stands at its body's own bounding-box
    // centre, which is INSIDE the body - in the default layer it would be
    // depth-tested away and invisible, which is the same argument
    // attachManipulator() makes for putting the manipulator here. CLAUDE.md
    // rejects this layer for the ground GRID and that ruling stands: the layer
    // clears depth, so a grid on it would paint over every body standing on it.
    // Here painting over the body IS the requirement, and depth still applies
    // WITHIN the layer, so the three arms occlude each other correctly.
    myContext->SetZLayer(object, Graphic3d_ZLayerId_Topmost);
    myObjects.push_back(object);
}

// --- the Move tool's presentation -------------------------------------------

gp_Dir MoveGizmoRenderer::armDirection(int axis)
{
    switch (axis) {
        case 0: return gp_Dir(1.0, 0.0, 0.0);
        case 1: return gp_Dir(0.0, 1.0, 0.0);
        default: return gp_Dir(0.0, 0.0, 1.0);
    }
}

gp_Lin MoveGizmoRenderer::armAxis(int axis) const
{
    return gp_Lin(pivot(), armDirection(axis));
}

gp_Pnt MoveGizmoRenderer::armTip(int axis) const
{
    return pivot().Translated(gp_Vec(armDirection(axis)) * myArmLength);
}

gp_Pnt MoveGizmoRenderer::armGrabStart(int axis) const
{
    return pivot().Translated(gp_Vec(armDirection(axis)) * (myArmLength * kGrabStartFraction));
}

void MoveGizmoRenderer::buildStrokes()
{
    // Every size here is a target in SCREEN PIXELS converted at the point of
    // use - DimensionRenderer's rule, and the reason this gizmo needs none of
    // OCCT's zoom-persistence machinery: furniture-sized handles swamp a
    // close-up and vanish on a distant one, and worldPerPixel() is the one
    // formula this app relates world units to pixels with, verified in both
    // projections.
    const double wpp = worldPerPixel();
    myArmLength = kArmPixels * wpp;
    const double coneLength = kConePixels * wpp;
    const double coneRadius = kConeRadiusPixels * wpp;
    const double hubRadius = kHubRadiusPixels * wpp;

    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = armDirection(axis);
        const gp_Vec along(dir);
        const gp_Pnt tip = armTip(axis);
        const gp_Pnt coneBase = tip.Translated(along * -coneLength);

        gp_Vec u, v;
        perpendicularFrame(dir, u, v);

        std::vector<Stroke> strokes;
        strokes.reserve(1 + kRingSegments * 2);
        // The shaft, from the hub to where the cone begins.
        strokes.push_back({pivot(), coneBase});

        // The cone as its own silhouette: a base ring plus one generatrix per
        // ring point. See addStrokes()' comment for why it is not a filled
        // triangle fan.
        gp_Pnt previous;
        gp_Pnt first;
        for (int i = 0; i <= kRingSegments; ++i) {
            const double angle = 2.0 * kPi * double(i) / double(kRingSegments);
            const gp_Pnt onRing =
                coneBase.Translated(u * (coneRadius * std::cos(angle)) +
                                    v * (coneRadius * std::sin(angle)));
            if (i == 0) {
                first = onRing;
            } else {
                strokes.push_back({previous, onRing});
                strokes.push_back({onRing, tip});
            }
            previous = onRing;
        }
        strokes.push_back({previous, first});

        const QColor colour = axis == 0   ? Theme::gizmoAxisX()
                              : axis == 1 ? Theme::gizmoAxisY()
                                          : Theme::gizmoAxisZ();
        addStrokes(strokes, colour, kArmStrokePx);
    }

    // The hub: three small rings in the principal planes, so it reads as a
    // ball from any camera. Neutral, like the axis card's own hub - it belongs
    // to no axis, and colouring it would make it look like a fourth handle.
    {
        std::vector<Stroke> hub;
        hub.reserve(3 * kRingSegments);
        for (int plane = 0; plane < 3; ++plane) {
            gp_Vec u, v;
            perpendicularFrame(armDirection(plane), u, v);
            gp_Pnt previous;
            for (int i = 0; i <= kRingSegments; ++i) {
                const double angle = 2.0 * kPi * double(i) / double(kRingSegments);
                const gp_Pnt onRing = pivot().Translated(u * (hubRadius * std::cos(angle)) +
                                                         v * (hubRadius * std::sin(angle)));
                if (i > 0) hub.push_back({previous, onRing});
                previous = onRing;
            }
        }
        addStrokes(hub, Theme::text(), kArmStrokePx);
    }
}

// --- the value chip ---------------------------------------------------------

MoveTool::MoveTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and must never eat a press meant for an arm behind
    // it - there is no interactive control on this widget at all, unlike
    // PullArrow's field, so the whole subtree can be click-through.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    applySize();
    hide();

    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applySize();
        update();
    });
    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &MoveTool::refresh);
    if (myView) {
        // The gizmo lives in the scene, so a camera move changes both where
        // the chip belongs on screen and how big the arms have to be drawn to
        // keep their pixel size.
        connect(myView, &OcctViewWidget::cameraChanged, this, &MoveTool::reposition);
        connect(myView, &OcctViewWidget::moveDragged, this, &MoveTool::onDragged);
        connect(myView, &OcctViewWidget::moveReleased, this, &MoveTool::onReleased);
    }
}

MoveTool::~MoveTool()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
}

void MoveTool::refresh()
{
    if (!myWindow || !myView) return;

    // ONE predicate, shared with updateActions() and the status label. Note
    // that it requires exactly one whole BODY selected, which is what keeps
    // this disjoint from the pull arrow (a face), the bevel arrow (edges) and
    // ExtrudePreview (a pending outline) - so at most one application-wide key
    // claim can ever be installed at a time.
    const int id = myWindow->moveToolBodyId();
    if (id <= 0) {
        end();
        return;
    }

    if (id != myBodyId) {
        begin(id);
        return;
    }

    // Already up on this body. Re-derive where it stands (a commit replaces
    // the body, and the replacement's bounding box is what the handle belongs
    // on now) and repaint - the chip's value follows the display unit, so a
    // unit switch has to reach it.
    showGizmo();
    update();
}

void MoveTool::begin(int bodyId)
{
    myBodyId = bodyId;
    myAxis = -1;
    myDistance = 0.0;
    if (myHasPreview) {
        myView->clearModelingPreview();
        myHasPreview = false;
    }
    showGizmo();
    updateVisibility();
}

void MoveTool::end()
{
    if (myView) {
        if (myHasPreview) myView->clearModelingPreview();
        myView->clearMoveGizmo();
    }
    myHasPreview = false;
    myBodyId = 0;
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
}

void MoveTool::showGizmo()
{
    if (!myWindow || !myView || myBodyId <= 0) return;
    gp_Pnt pivot;
    // The SAME bounding-box centre AIS_Manipulator's own AdjustPosition
    // derives, through the one shared implementation - see
    // ModelingOps::boundingBoxCentre() for why that matters when Space swaps
    // one tool for the other.
    if (!ModelingOps::boundingBoxCentre(myWindow->document().shapeOf(myBodyId), pivot)) return;
    myView->showMoveGizmo(pivot);
}

void MoveTool::cancel()
{
    if (!myView) return;
    myView->cancelMoveDrag();
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
    // The gizmo and the selection are deliberately untouched: the body is
    // still selected, the predicate still holds, and the user can simply drag
    // again without re-picking anything.
}

void MoveTool::onDragged(int axis, double millimetres)
{
    if (myBodyId <= 0) return;
    myAxis = axis;
    myDistance = millimetres;
    updatePreview();
    updateVisibility();
    reposition();
    update();
}

void MoveTool::onReleased(bool dragged)
{
    if (myBodyId <= 0) return;
    const bool committing = dragged && std::fabs(myDistance) > 1.0e-7;
    // The preview goes FIRST, and unconditionally. Unlike the face pull, a
    // move keeps the body selected, so the predicate still holds after the
    // commit and refresh() will not reach end() - which means nothing else
    // would ever take this preview down.
    if (myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    if (committing) commit();
    myAxis = -1;
    myDistance = 0.0;
    updateVisibility();
}

void MoveTool::updatePreview()
{
    if (!myWindow || !myView || myBodyId <= 0 || myAxis < 0) return;

    if (std::fabs(myDistance) < 1.0e-7) {
        if (myHasPreview) {
            myView->clearModelingPreview();
            myHasPreview = false;
        }
        return;
    }

    // The SAME ModelingOps::transformShape() the commit uses, through the same
    // gp_Trsf. A preview built by a different path is a lie, and this is the
    // one place the user judges a distance by what it looks like.
    gp_Trsf delta;
    delta.SetTranslation(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
    const ModelingOps::BooleanResult result =
        ModelingOps::transformShape(myWindow->document().shapeOf(myBodyId), delta);
    if (!result.ok) return;   // the last good preview stands

    // The DEDICATED channel, never setPreview(): that slot is already shared
    // by the sketch outline and the extrude preview, and CLAUDE.md records
    // what a third writer on it cost. myBodyId goes with it so the body the
    // ghost stands in for is drawn as a cage rather than sitting solid at the
    // old position.
    myView->setModelingPreview(result.shape, myBodyId);
    myHasPreview = true;
}

void MoveTool::commit()
{
    if (!myWindow || myBodyId <= 0 || myAxis < 0) return;
    gp_Trsf delta;
    delta.SetTranslation(gp_Vec(MoveGizmoRenderer::armDirection(myAxis)) * myDistance);
    // THE existing commit path - the checkpoint, the toast with Undo, the
    // mirror twin, the linked copies and the kernel refusal all belong to it.
    // This gesture adds no commit logic of its own whatsoever.
    myWindow->transformBody(myBodyId, delta);
}

void MoveTool::updateVisibility()
{
    // DERIVED, in one place, from the drag the viewport says is live - never
    // toggled by whichever event happened to run last. The chip has something
    // to say exactly while a drag has produced a distance, which is also
    // exactly as long as it may claim Escape.
    const bool wanted = myBodyId > 0 && myAxis >= 0 && myView && myView->moveDragActive();
    if (wanted == isVisible()) return;
    if (wanted) {
        reposition();
        show();
        raise();
    } else {
        hide();
    }
}

void MoveTool::reposition()
{
    if (!myView) return;

    // Rebuild the gizmo at the new scale first: it is drawn in world units but
    // sized in screen pixels, so a zoom changes the geometry it needs.
    if (myBodyId > 0) showGizmo();

    if (!isVisible() || myAxis < 0) return;

    gp_Pnt tip;
    QPoint at;
    if (!myView->moveGizmoArmTip(myAxis, tip) || !myView->projectToScreen(tip, at)) return;

    // Beside the dragged arm's tip, flipped to the other side rather than
    // clamped when that would run off the right edge - PullArrow's own layout,
    // for the same reason: a value chip that walks away from the handle it
    // labels stops labelling it.
    int x = at.x() + kChipGap;
    if (x + width() > myView->width() - kEdgeInset) x = at.x() - kChipGap - width();
    x = std::clamp(x, kEdgeInset, std::max(kEdgeInset, myView->width() - width() - kEdgeInset));
    int y = at.y() - height() / 2;
    y = std::clamp(y, kEdgeInset, std::max(kEdgeInset, myView->height() - height() - kEdgeInset));

    // Whole DEVICE pixels, Theme's position rule - PullArrow's own closing
    // lines, and for the same reason: a card placed at whatever pixel a
    // projection returned lands on a fractional device row half the time.
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    x = Theme::snapToDevicePixels(x, origin.x(), dpr);
    y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    move(x, y);
}

void MoveTool::replace()
{
    reposition();
    if (isVisible()) raise();
}

QString MoveTool::labelText() const
{
    if (myAxis < 0) return tr("Move");
    const QChar letter = myAxis == 0 ? QLatin1Char('X')
                         : myAxis == 1 ? QLatin1Char('Y')
                                       : QLatin1Char('Z');
    return tr("Move along %1").arg(letter);
}

QString MoveTool::valueText() const
{
    return QString::fromStdString(Measure::formatLength(myDistance));
}

QString MoveTool::hintText() const
{
    // A modeless panel with invisible verbs is how a keyboard cancel went
    // unnoticed for a whole branch on ExtrudePreview. This chip has no buttons
    // either, so its one key is on it in words.
    return tr("Drag an arm — Esc cancels");
}

QStringList MoveTool::paintedTexts() const
{
    // Every spelling of the label, not merely the one a given drag happens to
    // be showing: the sweep must cover the copy, and a run that never dragged
    // the Y arm would otherwise leave that string uncovered.
    return {tr("Move"), tr("Move along %1").arg(QLatin1Char('X')),
            tr("Move along %1").arg(QLatin1Char('Y')),
            tr("Move along %1").arg(QLatin1Char('Z')), hintText()};
}

void MoveTool::applySize()
{
    // Measured with the fonts it paints with - a fixed guess is exactly the
    // kind of number that silently clips the day the copy changes.
    const int margin = Theme::surfaceShadowMargin();
    const QFontMetrics hintMetrics(Theme::badgeFont());
    const QFontMetrics labelMetrics(Theme::labelFont());
    const QFontMetrics bodyMetrics(Theme::bodyFont());
    const int textWidth = std::max(
        {hintMetrics.horizontalAdvance(hintText()),
         labelMetrics.horizontalAdvance(tr("Move along %1").arg(QLatin1Char('X'))),
         // A four-figure length with a separator is the widest value this chip
         // can be asked to paint before it leaves the viewport entirely.
         bodyMetrics.horizontalAdvance(
             QString::fromStdString(Measure::formatLength(-1234.5)))});
    const int cardWidth = std::max(kMinWidth, textWidth + kPad * 2);
    setFixedSize(Theme::wholeDevicePixels(
        QSize(cardWidth + margin * 2,
              kPad * 2 + kLabelHeight + kValueHeight + kHintGap + kHintHeight + margin * 2)));
}

void MoveTool::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::accent());
    painter.drawText(QRect(body.left() + kPad, body.top() + kLabelHeight,
                           body.width() - kPad * 2, kValueHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, valueText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(QRect(body.left() + kPad, body.top() + kLabelHeight + kValueHeight + kHintGap,
                           body.width() - kPad * 2, kHintHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, hintText());
}

void MoveTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Installed for exactly as long as the chip is up - ExtrudePreview's and
    // PullArrow's own lifetime rule for an application-wide filter. Here that
    // is narrower than either: the chip is up only while a drag is live, so
    // this claim cannot even coexist with a gesture that has not started.
    QCoreApplication::instance()->installEventFilter(this);
}

void MoveTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    QCoreApplication::instance()->removeEventFilter(this);
}

bool MoveTool::eventFilter(QObject* watched, QEvent* event)
{
    // Escape belongs to this chip for as long as it is VISIBLE, whatever holds
    // focus - PullArrow::eventFilter()'s own reasoning, and it is not
    // optional: the press that started this drag went to the viewport, so the
    // viewport holds focus and a filter on this widget alone would never see
    // the key.
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    // Application-wide means every window in this process - gui_smoke builds
    // several at once - so only keys headed for this chip's own window count.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window())
        return QWidget::eventFilter(watched, event);
    // Never steal a keystroke out of a focused text field - the mirror chip's
    // own structural backstop, one gesture over. Escape in a rename field
    // means "abandon the rename", and this claim must not outrank it.
    if (qobject_cast<QLineEdit*>(widget)) return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier) return QWidget::eventFilter(watched, event);
    if (keyEvent->key() != Qt::Key_Escape) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        event->accept();   // claims the key back from QShortcutMap
        return true;
    }
    cancel();
    return true;
}
