#include "MitreTool.h"

#include "GestureChip.h"
#include "KeyClaim.h"
#include "MainWindow.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <gp_Vec.hxx>

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kPad = GestureChip::kPad;
constexpr int kGap = 8;
constexpr int kRowHeight = 26;
constexpr int kFieldWidth = 72;
constexpr int kReasonGap = 4;
constexpr int kReasonHeight = 16;
constexpr int kEdgeInset = GestureChip::kEdgeInset;
// How far the chip stands clear of the dial it belongs to.
constexpr int kDialGap = 14;

}  // namespace

MitreTool::MitreTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and never eats a click meant for the model behind
    // it - only the two sibling controls are interactive (see the header).
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    Theme::makeSurfaceTransparent(this);

    myField = new QLineEdit(view);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    connect(myField, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });

    myFlipButton = new QPushButton(flipText(), view);
    myFlipButton->setAttribute(Qt::WA_NoMousePropagation);
    // Never takes focus: a click on Flip must leave the typed angle's field
    // exactly as it was, caret and all.
    myFlipButton->setFocusPolicy(Qt::NoFocus);
    connect(myFlipButton, &QPushButton::clicked, this, [this] { flip(); });

    applyControlStyles();
    applySize();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyControlStyles();
        applySize();
        update();
    });

    myField->hide();
    myFlipButton->hide();
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &MitreTool::refresh);
    if (myView) {
        connect(myView, &OcctViewWidget::cameraChanged, this, &MitreTool::reposition);
        connect(myView, &OcctViewWidget::mitreDialDragged, this, &MitreTool::onDialDragged);
    }
}

MitreTool::~MitreTool()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    delete myField;
    delete myFlipButton;
}

QLineEdit* MitreTool::field() const
{
    return myField;
}

QPushButton* MitreTool::flipButton() const
{
    return myFlipButton;
}

void MitreTool::refresh()
{
    if (!myWindow || !myView) return;

    if (!myWindow->mitreEndActive()) {
        if (myActive) end();
        return;
    }

    const TopoDS_Face face = myWindow->mitreEndFace();
    const int bodyId = myWindow->mitreEndBodyId();
    if (!myActive || myFace.IsNull() || !myFace.IsSame(face) || bodyId != myBodyId) {
        begin();
        return;
    }

    // Already up. The other gizmos' own refresh() clears the shared preview
    // channel on the way past (each one's end() does, unconditionally), so
    // the ghost is put back from the cache - never rebuilt by a second path.
    updatePreview();
    reposition();
    update();
}

void MitreTool::begin()
{
    myActive = true;
    myFace = myWindow->mitreEndFace();
    myBodyId = myWindow->mitreEndBodyId();
    myAngle = 45.0;
    mySide = ModelingOps::MitreSide::WidthA;
    myHasPreview = false;
    myReason.clear();
    myPreviewShape.Nullify();
    myPreviewRevision = -1;
    myFrameRevision = -1;
    markInvalid(false);

    if (myField) {
        // The field is what previews, so seeding it previews 45 degrees.
        myField->blockSignals(true);
        myField->setText(QString::fromStdString(Measure::formatAngle(myAngle)));
        myField->blockSignals(false);
    }
    applySideTooltips();
    updatePreview();

    applySize();
    show();
    raise();
    reposition();
    syncControls();
    if (myField) {
        // Typing is half the gesture: the digits go to the field rather than
        // to the view shortcuts that 0-3 would otherwise trigger.
        myField->setFocus(Qt::OtherFocusReason);
        myField->selectAll();
    }
}

void MitreTool::end()
{
    myActive = false;
    if (myView) {
        if (myHasPreview) myView->clearModelingPreview();
        myView->clearMitreDial();
    }
    myHasPreview = false;
    myFace.Nullify();
    myBodyId = 0;
    myReason.clear();
    myPreviewShape.Nullify();
    markInvalid(false);
    hide();
}

bool MitreTool::frame(ModelingOps::MitreFrame& out)
{
    const int revision = myWindow->document().revision();
    if (myFrameRevision != revision || myFrameSide != mySide) {
        myFrameOk = ModelingOps::mitreFrame(myWindow->document().shapeOf(myBodyId), myFace,
                                            mySide, myFrame);
        myFrameRevision = revision;
        myFrameSide = mySide;
    }
    out = myFrame;
    return myFrameOk;
}

void MitreTool::pushDial()
{
    if (!myView || !myActive) return;
    ModelingOps::MitreFrame f;
    if (!frame(f)) {
        myView->clearMitreDial();
        return;
    }
    // The dial lies in the plane the angle is MEASURED in - square to the
    // pivot edge - so on one of the two board faces the pivot edge pierces:
    // a thickness face for a width side, a width face for a thickness side.
    // Of those two, the one that faces the camera, so the protractor lies on
    // wood the user can see rather than on the underside.
    const gp_Vec towardEye = -gp_Vec(myView->liveCameraDirection());
    const double facing = gp_Vec(f.pivotAxis).Dot(towardEye) >= 0.0 ? 1.0 : -1.0;
    const gp_Pnt centre = f.pivot.Translated(gp_Vec(f.pivotAxis) * (facing * 0.5 * f.sweep));
    myView->showMitreDial(centre, f.across, f.outward, f.pivotAxis, myAngle);
}

void MitreTool::updatePreview()
{
    if (!myActive || !myWindow || !myView || !myField) return;

    double typed = 0.0;
    if (!Measure::parseAngle(myField->text().toStdString(), typed) || typed < 1.0 ||
        typed > 89.0) {
        // Invalid input never previews: the last good ghost stays exactly as
        // it was - ExtrudePreview's rule - and the chip says what it wants.
        markInvalid(true);
        myReason = MainWindow::mitreAngleRangeRefusalText();
        if (myHasPreview && !myPreviewShape.IsNull())
            myView->setModelingPreview(myPreviewShape, myBodyId);
        pushDial();
        applySize();
        update();
        return;
    }

    myAngle = typed;
    myWindow->setMitreLiveValue(myAngle, mySide);
    pushDial();

    const int revision = myWindow->document().revision();
    const bool cached = !myPreviewShape.IsNull() && myPreviewRevision == revision &&
                        myPreviewAngle == myAngle && myPreviewSide == mySide;
    if (!cached) {
        // The SAME ModelingOps::mitreEnd() MainWindow::mitreEndBy() commits.
        const ModelingOps::BooleanResult result = ModelingOps::mitreEnd(
            myWindow->document().shapeOf(myBodyId), myFace, myAngle, mySide);
        myPreviewRevision = revision;
        myPreviewAngle = myAngle;
        myPreviewSide = mySide;
        myPreviewShape = result.ok ? result.shape : TopoDS_Shape();
    }

    if (myPreviewShape.IsNull()) {
        // A value the geometry refuses shows NO ghost - a ghost of the last
        // good angle would be a picture of a mitre Enter will not make.
        markInvalid(true);
        myReason = myWindow->mitreRefusalFor(myAngle, mySide);
        if (myReason.isEmpty()) myReason = MainWindow::mitreKernelRefusalText();
        if (myHasPreview) myView->clearModelingPreview();
        myHasPreview = false;
    } else {
        markInvalid(false);
        myReason.clear();
        myView->setModelingPreview(myPreviewShape, myBodyId);
        myHasPreview = true;
    }
    applySize();
    update();
}

void MitreTool::onDialDragged(double angleDeg)
{
    if (!myActive || !myField) return;
    // The drag writes the FIELD, and the field is what previews and commits.
    myField->setText(QString::fromStdString(Measure::formatAngle(angleDeg)));
}

void MitreTool::flip()
{
    if (!myActive) return;
    // One step round the end face's four edges - left, top, right, bottom -
    // through the geometry library's one statement of that order.
    mySide = ModelingOps::nextMitreSide(mySide);
    applySideTooltips();
    updatePreview();
    // A width side's dial lies on a thickness face and a thickness side's on
    // a width face, so the dial's projected extent moved: re-place the chip
    // against it, or it is left standing over the half it now covers.
    reposition();
}

void MitreTool::applySideTooltips()
{
    // The kind of cut in words, so it can be told without reading the ghost -
    // the same phrase the status label carries (MainWindow::mitreSideText()).
    const QString kind = MainWindow::mitreSideText(mySide);
    if (myFlipButton)
        myFlipButton->setToolTip(
            tr("Mitre %1 — Flip steps to the next edge of the end").arg(kind));
    if (myField) myField->setToolTip(tr("Mitre angle, %1 — 1° to 89°").arg(kind));
}

void MitreTool::cancel()
{
    if (myWindow) myWindow->cancelMitreEnd();
}

void MitreTool::commit()
{
    if (!myActive || !myWindow || !myField) return;
    double typed = 0.0;
    // Deliberately NOT gated on myInvalid: a value the geometry refuses has to
    // be REPORTED on Enter - a chip that silently does nothing is
    // indistinguishable from a broken one. An unreadable field goes in as NaN,
    // which ModelingOps::checkMitre() names as out of range.
    const double angle = Measure::parseAngle(myField->text().toStdString(), typed)
                             ? typed
                             : std::numeric_limits<double>::quiet_NaN();
    myWindow->mitreEndBy(angle, mySide);
    // On success mitreEndBy() ended the gesture and its appStateChanged has
    // already run refresh() -> end() here.
}

QString MitreTool::labelText() const
{
    return tr("Mitre");
}

QString MitreTool::hintText() const
{
    return tr("Enter · Esc");
}

QString MitreTool::flipText()
{
    return QString::fromUtf8("\xE2\x87\x84 ") + tr("Flip");
}

QStringList MitreTool::paintedTexts() const
{
    QStringList texts{labelText(), hintText(), flipText()};
    if (!myReason.isEmpty()) texts << myReason;
    return texts;
}

int MitreTool::rowWidth() const
{
    const int label = QFontMetrics(Theme::labelFont()).horizontalAdvance(labelText());
    const int flip = QFontMetrics(Theme::labelFont()).horizontalAdvance(flipText()) + 20;
    const int hint = QFontMetrics(Theme::badgeFont()).horizontalAdvance(hintText());
    return kPad + label + kGap + kFieldWidth + kGap + flip + kGap + hint + kPad;
}

QRect MitreTool::labelRect() const
{
    const int label = QFontMetrics(Theme::labelFont()).horizontalAdvance(labelText());
    return QRect(kPad, kPad, label, kRowHeight);
}

QRect MitreTool::fieldRect() const
{
    return QRect(labelRect().right() + 1 + kGap, kPad, kFieldWidth, kRowHeight);
}

QRect MitreTool::flipRect() const
{
    const int flip = QFontMetrics(Theme::labelFont()).horizontalAdvance(flipText()) + 20;
    return QRect(fieldRect().right() + 1 + kGap, kPad, flip, kRowHeight);
}

QRect MitreTool::hintRect() const
{
    const int left = flipRect().right() + 1 + kGap;
    return QRect(left, kPad, std::max(0, width() - left - kPad), kRowHeight);
}

QRect MitreTool::reasonRect() const
{
    return QRect(kPad, kPad + kRowHeight + kReasonGap, std::max(0, width() - kPad * 2),
                 kReasonHeight);
}

void MitreTool::applySize()
{
    // Measured with the fonts it paints with, so a longer reason cannot clip.
    int w = rowWidth();
    int h = kPad * 2 + kRowHeight;
    if (!myReason.isEmpty()) {
        w = std::max(w, QFontMetrics(Theme::badgeFont()).horizontalAdvance(myReason) + kPad * 2);
        h += kReasonGap + kReasonHeight;
    }
    const QSize size = Theme::wholeDevicePixels(QSize(w, h));
    if (size != this->size()) {
        setFixedSize(size);
        reposition();
    }
    syncControls();
}

void MitreTool::applyControlStyles()
{
    if (myField) myField->setFont(Theme::bodyFont());
    if (myFlipButton) {
        myFlipButton->setFont(Theme::labelFont());
        myFlipButton->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 4px; font-size: %4pt; padding: 0px 6px; } "
                           "QPushButton:hover { background-color: %5; }")
                .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name())
                .arg(Theme::labelFont().pointSizeF())
                .arg(Theme::chipHover().name()));
    }
    markInvalid(myInvalid);
}

void MitreTool::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    myField->setStyleSheet(QStringLiteral("QLineEdit { background-color: %1; color: %2; "
                                          "border: 1px solid %3; border-radius: 0px; "
                                          "padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

void MitreTool::syncControls()
{
    // Sibling geometry is this card's local rects, translated by pos() - the
    // ExtrudePreview idiom - and visibility is DERIVED from the card's own.
    if (myField) {
        myField->setGeometry(fieldRect().translated(pos()));
        myField->setVisible(isVisible());
        if (isVisible()) myField->raise();
    }
    if (myFlipButton) {
        myFlipButton->setGeometry(flipRect().translated(pos()));
        myFlipButton->setVisible(isVisible());
        if (isVisible()) myFlipButton->raise();
    }
}

void MitreTool::reposition()
{
    if (!myView || !isVisible()) return;
    // Centred under the dial's projected extent - the mockup's chip sits just
    // below the protractor - or above it when there is no room below.
    QRect extent;
    for (int degrees = 0; degrees <= 180; degrees += 15) {
        gp_Pnt p;
        QPoint at;
        if (!myView->mitreDialPointAt(degrees, p) || !myView->projectToScreen(p, at)) continue;
        extent = extent.isNull() ? QRect(at, QSize(1, 1)) : extent.united(QRect(at, QSize(1, 1)));
    }
    if (extent.isNull()) return;

    int x = extent.center().x() - width() / 2;
    int y = extent.bottom() + kDialGap;
    if (y + height() > myView->height() - kEdgeInset) y = extent.top() - kDialGap - height();
    x = std::clamp(x, kEdgeInset, std::max(kEdgeInset, myView->width() - width() - kEdgeInset));
    y = std::clamp(y, kEdgeInset, std::max(kEdgeInset, myView->height() - height() - kEdgeInset));
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    move(Theme::snapToDevicePixels(x, origin.x(), dpr),
         Theme::snapToDevicePixels(y, origin.y(), dpr));
}

void MitreTool::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    syncControls();
}

void MitreTool::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    GestureChip::paintFrame(painter, this, myInvalid);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(labelRect(), Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(hintRect(), Qt::AlignVCenter | Qt::AlignLeft, hintText());

    if (!myReason.isEmpty()) {
        painter.setPen(Theme::danger());
        painter.drawText(reasonRect(), Qt::AlignVCenter | Qt::AlignLeft, myReason);
    }
}

void MitreTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncControls();
    // Installed for exactly as long as the chip is up.
    QCoreApplication::instance()->installEventFilter(this);
}

void MitreTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    if (myFlipButton) myFlipButton->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void MitreTool::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncControls();
}

void MitreTool::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncControls();
}

bool MitreTool::eventFilter(QObject* watched, QEvent* event)
{
    // Enter and Escape belong to this chip for as long as it is VISIBLE,
    // whatever holds focus - the one KeyClaim implementation every gesture
    // chip shares. Not exempting line edits: Enter IN the angle field is
    // exactly what the claim routes to commit().
    int key = 0;
    if (KeyClaim::claim(this, watched, event, /*wantEnter=*/true,
                        /*exemptLineEdits=*/false, &key)) {
        if (key == Qt::Key_Escape)
            cancel();
        else if (key != 0)
            commit();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
