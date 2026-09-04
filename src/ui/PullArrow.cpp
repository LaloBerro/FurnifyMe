#include "PullArrow.h"

#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <Aspect_TypeOfLine.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <TopAbs_Orientation.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <QCoreApplication>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPad = 10;
constexpr int kWidth = 176;
constexpr int kLabelHeight = 18;
constexpr int kFieldHeight = 24;
constexpr int kHintGap = 4;
constexpr int kHintHeight = 14;
// How far the chip stands off the arrow's projected head, and how far it is
// kept inside the viewport's own edges.
constexpr int kChipGap = 18;
constexpr int kEdgeInset = 8;

Quantity_Color toOcct(const QColor& c)
{
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

// The shaft and the two arrowheads, one segment array, one object, never
// pickable - the same shape as DimensionRenderer's DimensionLines and
// OcctViewWidget's SketchPointMarker. The arrowheads are OPEN, two strokes
// each, for the reason recorded on DimensionRenderer::show():
// Graphic3d_ArrayOfTriangles draws nothing at all in this build, so a filled
// head would be an invisible one.
//
// Never pickable is deliberate even though this arrow IS the thing the user
// grabs. An AIS object with a real ComputeSelection would join the
// hover/selection pipeline, and this arrow sits exactly on top of the face
// that raised it: every hover would detect the arrow instead of the face and
// every click would replace the face selection with it, which would retire
// the arrow the click was aimed at. OcctViewWidget hit-tests it in screen
// space instead, against its own projected endpoints.
class PullArrowLines : public AIS_InteractiveObject {
public:
    Handle(Graphic3d_ArrayOfSegments) lines;
    Quantity_Color colour;

    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override
    {
        if (lines.IsNull()) return;
        Handle(Graphic3d_Group) group = presentation->NewGroup();
        Handle(Graphic3d_AspectLine3d) aspect =
            new Graphic3d_AspectLine3d(colour, Aspect_TOL_SOLID, 2.6);
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(lines);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override
    {
        // See the class comment: picked in screen space by the viewport, not
        // through AIS.
    }
};

}  // namespace

// --- the 3D arrow -----------------------------------------------------------

void PullArrowRenderer::attach(const Handle(AIS_InteractiveContext)& context)
{
    myContext = context;
}

void PullArrowRenderer::clear(bool updateViewer)
{
    if (!myContext.IsNull() && !myObjects.empty()) {
        for (auto& obj : myObjects) myContext->Remove(obj, Standard_False);
        // UpdateCurrentViewer() is a full viewer redraw and this build blocks
        // on vsync for it - measured at ~16 ms a call. show() therefore
        // clears with updateViewer = false and updates ONCE at the end,
        // rather than paying for two frames to replace one arrow.
        if (updateViewer) myContext->UpdateCurrentViewer();
    }
    myObjects.clear();
}

void PullArrowRenderer::reapplyTheme()
{
    // Nothing on screen, nothing to recolour - and re-showing here would make
    // an arrow appear that no gesture asked for.
    if (!isShowing()) return;
    myForceRebuild = true;
    show(myCentre, myOutward, myViewDirection, myWorldPerPixel);
}

gp_Pnt PullArrowRenderer::head() const
{
    return myCentre.Translated(gp_Vec(myOutward) * myHalfLength);
}

gp_Pnt PullArrowRenderer::tail() const
{
    return myCentre.Translated(gp_Vec(myOutward) * -myHalfLength);
}

void PullArrowRenderer::show(const gp_Pnt& centre, const gp_Dir& outward,
                             const gp_Dir& viewDirection, double worldPerPixel,
                             bool updateViewer)
{
    if (myContext.IsNull()) return;

    // Nothing to do when nothing has actually moved.
    //
    // PullArrow::reposition() drives this from cameraChanged AND from every
    // appStateChanged, so the great majority of calls ask for exactly the
    // arrow already on screen - and a rebuild is emphatically not free.
    //
    // MEASURED, not assumed. The first version of this file cost 33.3 ms per
    // rebuild: it called UpdateCurrentViewer() twice, once to remove the old
    // arrow and once to display the new one, and each of those blocks on
    // vsync in this build. Two whole frames to redraw five line segments, on
    // every camera step. Three changes fixed it, and this early-out is the
    // third:
    //   - clear() no longer updates the viewer on show()'s internal path:
    //     33.3 ms -> 16.3 ms, which is one vsync and therefore what that
    //     figure almost entirely IS;
    //   - applyCameraState() emits cameraChanged() before its own Redraw(),
    //     so a rebuild driven by a camera move passes updateViewer = false
    //     and rides along with the redraw already coming - no viewer update
    //     at all on that path, leaving just the AIS rebuild, which the
    //     16.3-ms-is-one-vsync figure shows is well under a millisecond;
    //   - and a call asking for the arrow already on screen returns right
    //     here without touching AIS: 0.17 ms. That is the common case -
    //     every pan, every zoom notch inside the same octave, and every
    //     appStateChanged that moved no camera at all.
    //
    // The tolerances are what "actually moved" means: half a degree of view
    // rotation is below the point where the arrowheads' fan direction reads
    // differently, and 1% of scale is below the point where the arrow's
    // pixel length changes at all.
    constexpr double kHalfDegree = 0.0087266;   // radians
    if (!myForceRebuild && isShowing() && centre.IsEqual(myCentre, 1.0e-9) &&
        outward.IsEqual(myOutward, 1.0e-9) &&
        viewDirection.IsEqual(myViewDirection, kHalfDegree) &&
        std::fabs(worldPerPixel - myWorldPerPixel) <=
            std::max(myWorldPerPixel, 1.0e-9) * 0.01) {
        return;
    }
    // Consumed here, not at the end: every path below rebuilds, and a flag
    // left set would defeat the cache for the next camera step too.
    myForceRebuild = false;

    clear(/*updateViewer=*/false);

    myCentre = centre;
    myOutward = outward;
    myViewDirection = viewDirection;
    myWorldPerPixel = worldPerPixel;

    // Every size here is a target in SCREEN PIXELS, converted at the point of
    // use - DimensionRenderer's rule, and for the same reason: furniture
    // sized in millimetres swamps a close-up and vanishes on a distant one.
    const double wpp = std::max(worldPerPixel, 1.0e-9);
    myHalfLength = 52.0 * wpp;
    const double headLength = 15.0 * wpp;
    const double headHalfWidth = 6.5 * wpp;

    // The arrowheads' strokes fan out across the screen rather than toward
    // the eye, so the head reads as a head from wherever the camera is. Falls
    // back to any perpendicular when the arrow points straight at the viewer,
    // where no direction reads better than another anyway.
    gp_Vec sideways = gp_Vec(outward).Crossed(gp_Vec(viewDirection));
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(outward).Crossed(gp_Vec(0.0, 0.0, 1.0));
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(outward).Crossed(gp_Vec(1.0, 0.0, 0.0));
    const gp_Vec side = gp_Vec(gp_Dir(sideways));
    const gp_Vec along(outward);

    const gp_Pnt tipOut = head();
    const gp_Pnt tipIn = tail();

    // 1 shaft + 2 heads x 2 strokes = 5 segments, 10 vertices.
    Handle(Graphic3d_ArrayOfSegments) segs = new Graphic3d_ArrayOfSegments(10);
    auto addSeg = [&](const gp_Pnt& a, const gp_Pnt& b) {
        segs->AddVertex(a);
        segs->AddVertex(b);
    };
    addSeg(tipIn, tipOut);
    auto addHead = [&](const gp_Pnt& tip, const gp_Vec& inward) {
        const gp_Pnt base = tip.Translated(inward * headLength);
        addSeg(tip, base.Translated(side * headHalfWidth));
        addSeg(tip, base.Translated(side * -headHalfWidth));
    };
    addHead(tipOut, -along);
    addHead(tipIn, along);

    Handle(PullArrowLines) arrow = new PullArrowLines();
    arrow->lines = segs;
    arrow->colour = toOcct(Theme::accent());
    myContext->Display(arrow, 0, -1, Standard_False);   // mode -1: feedback only
    // Drawn over everything. The arrow sits at the face's centre, so the
    // moment a pull previews, the preview body it is pulling stands directly
    // in front of it - the first capture of this gesture showed the chip and
    // the preview perfectly and the arrow not at all, buried inside the very
    // shape it had just made. A handle the user is expected to grab cannot be
    // occluded by the thing it is moving.
    //
    // CLAUDE.md rejects this same layer for the GROUND GRID, and rightly:
    // Topmost draws with the depth buffer cleared, so a grid on it would
    // paint over every body standing on the ground. That argument is about
    // scene content pretending to be in front of other scene content. This is
    // a gizmo, and being in front of everything is its whole job.
    myContext->SetZLayer(arrow, Graphic3d_ZLayerId_Topmost);
    myObjects.push_back(arrow);

    // Skipped when the caller is about to redraw anyway - see
    // OcctViewWidget::applyCameraState(), which now emits cameraChanged()
    // BEFORE its own Redraw() precisely so this rebuild can ride along with
    // it instead of forcing a second frame of its own.
    if (updateViewer) myContext->UpdateCurrentViewer();
}

// --- the value chip ---------------------------------------------------------

PullArrow::PullArrow(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and must never eat a click meant for the model
    // behind it - only the sibling field below is ever interactive. See the
    // header for why that means the field cannot be a child of this widget.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    {
        const int margin = Theme::surfaceShadowMargin();
        // Through Theme::wholeDevicePixels() - see Theme.h. This card's 176x80
        // happens to be whole at every quarter-step scale already, so today
        // this changes nothing; it is here so that the day a line is added to
        // it the card does not silently start painting a black hairline over
        // the GL surface, which is exactly how the round/flatten chip found
        // the rule.
        setFixedSize(Theme::wholeDevicePixels(
            QSize(kWidth + margin * 2,
                  kPad * 2 + kLabelHeight + kFieldHeight + kHintGap + kHintHeight +
                      margin * 2)));
    }

    myField = new QLineEdit(view);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    // An explicitly set font does not follow QApplication::setFont, so the
    // Theme broadcast has to put it back - see ExtrudePreview's constructor
    // for the full reason.
    myField->setFont(Theme::bodyFont());
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        myField->setFont(Theme::bodyFont());
        update();
    });
    connect(myField, &QLineEdit::textChanged, this,
            [this](const QString&) { updatePreview(); });
    markInvalid(false);

    syncFieldGeometry();
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &PullArrow::refresh);
    if (myView) {
        // The arrow lives in the scene, so a camera move changes both where
        // the chip belongs on screen and how big the arrow has to be drawn to
        // keep its pixel size.
        connect(myView, &OcctViewWidget::cameraChanged, this, &PullArrow::reposition);
        connect(myView, &OcctViewWidget::pullDragged, this, &PullArrow::onDragged);
        connect(myView, &OcctViewWidget::pullReleased, this, &PullArrow::onReleased);
    }

    // Milestone 5 item 2: this card's own corners over the GL surface, at
    // the same radius paintEvent() paints its card with. myField is NOT
    // masked - see markInvalid()'s own comment: border-radius 0, deliberately.
    Theme::installCardMask(this, 8);
}

PullArrow::~PullArrow()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    // Sibling, not a child - Qt's parent-child cascade does not reach it when
    // this panel alone is destroyed, and QPointer makes the delete a safe
    // no-op when the shared parent tears both down instead.
    delete myField;
}

QLineEdit* PullArrow::field() const
{
    return myField;
}

void PullArrow::refresh()
{
    if (!myWindow || !myView) return;

    // ONE predicate, shared with updateActions() and the status label. Note
    // it refuses while a face is pending, which is exactly when
    // ExtrudePreview can be open - so the two application-wide Enter/Escape
    // claims can never be installed at the same time.
    if (!myWindow->canPullSelectedFace()) {
        end();
        return;
    }

    const TopoDS_Face face = myView->selectedFace();
    const int bodyId = myWindow->bodyIdForFace(face);
    if (face.IsNull() || bodyId <= 0) {
        // A selected face that belongs to no document body - nothing this
        // gizmo can commit against.
        end();
        return;
    }

    if (myFace.IsNull() || !myFace.IsSame(face) || bodyId != myBodyId) {
        begin(face, bodyId);
        return;
    }

    // Already up on this face. Repaint and re-read: the label carries the
    // unit and the field is read in it, so a unit switch has to rebuild the
    // shape as well as the text - ExtrudePreview's lesson, where a repaint
    // alone left "(cm)" over a body built from the old unit's reading.
    update();
    updatePreview();
    reposition();
}

void PullArrow::begin(const TopoDS_Face& face, int bodyId)
{
    myFace = face;
    myBodyId = bodyId;

    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    myCentre = props.CentreOfMass();

    // The OUTWARD normal, derived here the same way lockToFace does it:
    // BRepAdaptor_Surface never applies TopAbs_Orientation, so on a REVERSED
    // face the surface normal points INTO the body. Get this wrong and
    // "drag out" carves. ModelingOps::pullFace derives it again for itself
    // rather than trusting a caller, so the two agree by construction.
    const gp_Pln plane = BRepAdaptor_Surface(face).Plane();
    myOutward = plane.Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) myOutward.Reverse();

    myDistance = 0.0;
    myHasPreview = false;
    myView->clearModelingPreview();
    myView->showPullArrow(myCentre, myOutward);

    if (myField) {
        // Zero, unconditionally: a distance left over from the last face
        // would silently preview a pull the user never asked for.
        myField->blockSignals(true);
        myField->setText(textForDistance(0.0));
        myField->blockSignals(false);
    }
    markInvalid(false);

    reposition();
    show();
    raise();
    if (myField) myField->raise();
}

void PullArrow::end()
{
    if (myView) {
        myView->clearModelingPreview();
        myView->clearPullArrow();
    }
    myFace.Nullify();
    myBodyId = 0;
    myDistance = 0.0;
    myHasPreview = false;
    markInvalid(false);
    hide();
}

void PullArrow::cancel()
{
    if (myView) myView->clearModelingPreview();
    myHasPreview = false;
    myDistance = 0.0;
    if (myField) {
        myField->blockSignals(true);
        myField->setText(textForDistance(0.0));
        myField->blockSignals(false);
    }
    markInvalid(false);
    update();
    // The arrow and the selection are deliberately untouched: the face is
    // still selected, the predicate still holds, and the user can drag again
    // without re-picking anything.
}

void PullArrow::onDragged(double millimetres)
{
    if (!isVisible() || !myField) return;
    // The drag writes the FIELD, and the field is what previews and commits -
    // one value, one path. It also means the number on screen is exactly the
    // number that gets built, rounding included.
    myField->setText(textForDistance(millimetres));
}

void PullArrow::onReleased(bool dragged)
{
    if (!isVisible()) return;
    if (dragged) commit();
}

void PullArrow::updatePreview()
{
    if (!myField || !myView || !myWindow || myFace.IsNull()) return;

    // Through Measure::parseLength, never QString::toDouble: the chip's label
    // names the display unit, so the field has to be read back in that same
    // unit or typing "3" with centimetres selected would build 3 mm.
    double distance = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), distance)) {
        // Invalid input never previews: the last good shape, if any, stays
        // exactly as it was and only the field's border marks the problem.
        markInvalid(true);
        return;
    }

    if (std::fabs(distance) < 1.0e-7) {
        // Zero is "no pull yet", not an error - it is what begin() seeds.
        markInvalid(false);
        myDistance = 0.0;
        if (myHasPreview) {
            myView->clearModelingPreview();
            myHasPreview = false;
        }
        update();
        return;
    }

    // The SAME ModelingOps::pullFace() the commit uses. A preview built by a
    // different path is a lie, and this is the one place the user judges a
    // number by what it looks like.
    const ModelingOps::BooleanResult result =
        ModelingOps::pullFace(myWindow->document().shapeOf(myBodyId), myFace, distance);
    if (!result.ok) {
        markInvalid(true);
        return;
    }

    markInvalid(false);
    myDistance = distance;
    // The DEDICATED channel, never setPreview(): that slot is already shared
    // by the sketch outline and the extrude preview, and CLAUDE.md records
    // the bug that cost - cancelling one erased the other's face. myBodyId
    // goes with it so the body the preview stands in for is drawn as a cage
    // rather than hiding a carve inside itself.
    myView->setModelingPreview(result.shape, myBodyId);
    myHasPreview = true;
    update();
}

void PullArrow::commit()
{
    if (!myField || !myWindow || myFace.IsNull()) return;

    double distance = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), distance)) return;
    // Zero is a cancel, not an error: a press and release with no movement
    // between them reaches here, and it must not raise a failure toast.
    if (std::fabs(distance) < 1.0e-7) return;

    // Deliberately NOT gated on myInvalid. A value the kernel refuses has to
    // be REPORTED when the user commits it - a chip that silently does
    // nothing on Enter is indistinguishable from a broken one. pullFaceBy()
    // owns both outcomes: the Failure toast in cause-and-fix form, or the
    // undo checkpoint, the replaced body and the Note toast with Undo.
    const TopoDS_Face face = myFace;
    if (!myWindow->pullFaceBy(face, distance)) {
        markInvalid(true);
        return;
    }
    // On success pullFaceBy() cleared the selection, so the appStateChanged
    // it emitted has already run refresh() -> end() on this widget. Nothing
    // to tear down here.
}

QString PullArrow::textForDistance(double millimetres) const
{
    QString formatted = QString::fromStdString(Measure::formatLength(millimetres));
    const QString suffix =
        QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    // The separator has to come out. This text is not just displayed - it is
    // READ BACK by Measure::parseLength() on the very next textChanged, and
    // that grammar has no comma in it (deliberately: see Measure.h, where
    // "1,2" is listed among the things it must refuse). formatLength() puts
    // one in past a thousand, so a drag past 1,000 mm wrote "1,410" into the
    // field and then could not read its own writing: the preview froze at the
    // last good value and Enter committed nothing. Caught by a probe that
    // dragged far enough to cross that boundary - every earlier drag in the
    // suite was a few hundred millimetres and never reached it.
    formatted.remove(QLatin1Char(','));
    return formatted;
}

QString PullArrow::labelText() const
{
    return tr("Pull distance (%1)").arg(QString::fromStdString(Measure::unitSuffix()));
}

QString PullArrow::hintText() const
{
    // A modeless panel with invisible verbs is how the keyboard commit and
    // cancel went unnoticed for a whole branch on ExtrudePreview. This chip
    // has no buttons either, so both keys are on it in words.
    return tr("Drag the arrow — Enter applies, Esc cancels");
}

QRect PullArrow::fieldRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight,
                 QWidget::width() - margin * 2 - kPad * 2, kFieldHeight);
}

QRect PullArrow::hintRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight + kFieldHeight + kHintGap,
                 QWidget::width() - margin * 2 - kPad * 2, kHintHeight);
}

void PullArrow::syncFieldGeometry()
{
    if (!myField) return;
    // Shares this widget's parent, so fieldRect() - in this widget's own
    // local coordinates - needs translating by pos(). Same idiom as
    // ExtrudePreview::syncFieldGeometry() and Toast::syncUndoGeometry().
    myField->setGeometry(fieldRect().translated(pos()));
    // DERIVED, not left to a hide event that may never arrive.
    myField->setVisible(isVisible());
    myField->raise();
}

void PullArrow::reposition()
{
    if (!myView) return;

    // Rebuild the arrow at the new scale first: it is drawn in world units
    // but sized in screen pixels, so a zoom changes the geometry it needs.
    if (isVisible() && !myFace.IsNull()) myView->showPullArrow(myCentre, myOutward);

    gp_Pnt headPoint;
    QPoint at;
    if (!myView->pullArrowHead(headPoint) || !myView->projectToScreen(headPoint, at)) return;

    // Beside the outward head, the way the reference puts its value chip
    // beside the arrow - flipped to the other side rather than clamped when
    // that would run off the right edge, so the chip stays associated with
    // the arrow it labels.
    //
    // HONEST LIMIT: unlike ToastHost and HintBalloon, this does NOT step
    // around the rail or the items drawer. It cannot: a value chip that walks
    // away from its arrow stops labelling it, which is worse than an overlap.
    // replace() does at least keep it z-above the clusters, so where they do
    // overlap the field stays clickable.
    int x = at.x() + kChipGap;
    if (x + QWidget::width() > myView->width() - kEdgeInset)
        x = at.x() - kChipGap - QWidget::width();
    x = std::clamp(x, kEdgeInset, std::max(kEdgeInset, myView->width() - QWidget::width() - kEdgeInset));

    int y = at.y() - QWidget::height() / 2;
    y = std::clamp(y, kEdgeInset, std::max(kEdgeInset, myView->height() - QWidget::height() - kEdgeInset));

    // Whole DEVICE pixels, in the window's own coordinates - the position half
    // of Theme's rule (the size half is at the constructor). A card placed at
    // whatever pixel a projection returned lands on a fractional device row
    // half the time, and the row Qt flushes but the widget's logical clip
    // cannot reach is black over the GL surface. Snapped last, after the
    // clamps, and always downward, so it cannot push the card back outside the
    // viewport edges the clamps just brought it inside.
    {
        const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
        const double dpr = devicePixelRatioF();
        x = Theme::snapToDevicePixels(x, origin.x(), dpr);
        y = Theme::snapToDevicePixels(y, origin.y(), dpr);
    }

    move(x, y);
}

void PullArrow::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    if (myField) myField->raise();
}

void PullArrow::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    // border-radius: 0, for the reason spelled out on
    // ExtrudePreview::markInvalid() - this field is a sibling parented
    // straight to the viewport, sitting on OCCT's GL surface with no card of
    // its own underneath, and over that surface an unpainted corner pixel is
    // not transparent but whatever the driver left there.
    myField->setStyleSheet(QStringLiteral(
                               "QLineEdit { background-color: %1; color: %2; "
                               "border: 1px solid %3; border-radius: 0px; padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

QStringList PullArrow::paintedTexts() const
{
    return {labelText(), hintText()};
}

void PullArrow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    // The one thing this card keeps on top of the shared base, exactly as
    // ExtrudePreview does: a danger() outline while the current value does
    // not parse or is one the kernel refuses.
    if (myInvalid) {
        QPainterPath outline;
        outline.addRoundedRect(body, 8, 8);
        painter.setPen(QPen(Theme::danger(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(hintRect(), Qt::AlignVCenter | Qt::AlignLeft, hintText());
}

void PullArrow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncFieldGeometry();
    // Installed for exactly as long as the chip is up - ShortcutSheet's and
    // ExtrudePreview's lifetime rule for an application-wide filter.
    QCoreApplication::instance()->installEventFilter(this);
}

void PullArrow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void PullArrow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncFieldGeometry();
}

void PullArrow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncFieldGeometry();
}

bool PullArrow::eventFilter(QObject* watched, QEvent* event)
{
    // Enter and Escape belong to this chip for as long as it is VISIBLE,
    // whatever holds focus. The whole reason a live preview exists is that
    // the user orbits to judge the shape before committing - and an orbit is
    // a press in the viewport, which takes focus off the field. A filter on
    // the field alone stops working at exactly the moment it is needed; this
    // is ExtrudePreview's shape, and it is not optional. See
    // ExtrudePreview::eventFilter() for the full account.
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    // Application-wide means every window in this process - gui_smoke builds
    // several at once - so only keys headed for this chip's own window count.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window())
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier) return QWidget::eventFilter(watched, event);

    const int key = keyEvent->key();
    const bool commits = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool cancels = key == Qt::Key_Escape;
    if (!commits && !cancels) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        event->accept();   // claims the key back from QShortcutMap
        return true;
    }

    if (commits)
        commit();
    else
        cancel();
    return true;
}
