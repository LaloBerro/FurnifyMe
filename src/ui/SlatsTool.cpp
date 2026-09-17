#include "SlatsTool.h"

#include "GestureChip.h"
#include "KeyClaim.h"
#include "MainWindow.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QLatin1Char>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPad = GestureChip::kPad;
constexpr int kGap = 8;
constexpr int kRowHeight = 26;
constexpr int kFieldWidth = 62;
constexpr int kFlipWidth = 56;
constexpr int kRowGap = 6;
constexpr int kReasonGap = 4;
constexpr int kReasonHeight = 16;
// How far the card stands off the viewport's bottom edge. The toast's own
// margin, so the two read as one band rather than two arbitrary heights - and
// a toast raised during the gesture lands over this card rather than beside
// it, which is the right way round: a refusal is the thing to read.
constexpr int kBottomMargin = 24;
// Width, Gap, Depth - the three numbers, in the order the chip paints them.
constexpr int kFieldCount = 3;

}  // namespace

SlatsTool::SlatsTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and never eats a click meant for the model behind
    // it - only the four siblings are interactive (see the header).
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    Theme::makeSurfaceTransparent(this);

    const auto makeField = [this, view] {
        auto* field = new QLineEdit(view);
        field->setAttribute(Qt::WA_NoMousePropagation);
        connect(field, &QLineEdit::textChanged, this,
                [this](const QString&) { updatePreview(); });
        field->hide();
        return field;
    };
    myWidth = makeField();
    myGap = makeField();
    myDepth = makeField();

    auto* flip = new QPushButton(tr("Flip"), view);
    flip->setAttribute(Qt::WA_NoMousePropagation);
    flip->setCursor(Qt::PointingHandCursor);
    // No focus: the fields are what the user types into, and a button that
    // took focus would swallow the digits meant for the number beside it.
    flip->setFocusPolicy(Qt::NoFocus);
    connect(flip, &QPushButton::clicked, this, [this] {
        // Which way the slats RUN. The geometry's own default follows the
        // face's proportions; this is the swap for the case it cannot guess.
        myPlan.runAcross = !myPlan.runAcross;
        updatePreview();
    });
    flip->hide();
    myFlip = flip;

    applyControlStyles();
    applySize();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyControlStyles();
        applySize();
        update();
    });

    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &SlatsTool::refresh);
    if (myView) connect(myView, &OcctViewWidget::cameraChanged, this, &SlatsTool::reposition);
}

SlatsTool::~SlatsTool()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    delete myWidth;
    delete myGap;
    delete myDepth;
    delete myFlip;
}

QAbstractButton* SlatsTool::flipButton() const
{
    return myFlip;
}

void SlatsTool::refresh()
{
    if (!myWindow || !myView) return;

    if (!myWindow->slatsActive()) {
        if (myActive) end();
        return;
    }

    const int bodyId = myWindow->slatsBodyId();
    if (!myActive || bodyId != myBodyId) {
        begin();
        return;
    }

    // Already up. Every other gizmo's refresh() clears the shared preview
    // channel on the way past, so the ghost is put back from the cache -
    // never rebuilt by a second path.
    updatePreview();
    reposition();
    update();
}

void SlatsTool::begin()
{
    myActive = true;
    myBodyId = myWindow->slatsBodyId();
    // The numbers the gesture opens on: the defaults on a fresh face, the
    // ones measured back off the slats on a rebuild - MainWindow decides
    // which, so this chip has no second idea of what a run is made of.
    myPlan = myWindow->slatsPlan();
    myHasPreview = false;
    myReason.clear();
    myPreviewShape.Nullify();
    myPreviewRevision = -1;
    markInvalid(false);

    seedFields();
    applySize();
    show();
    raise();
    reposition();
    syncControls();
    if (myWidth) {
        // Typing IS the gesture: the digits go to the field rather than to
        // the view shortcuts 0-3 would otherwise trigger.
        myWidth->setFocus(Qt::OtherFocusReason);
        myWidth->selectAll();
    }
    updatePreview();
}

void SlatsTool::end()
{
    myActive = false;
    if (myView && myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    myBodyId = 0;
    myCount = 0;
    myReason.clear();
    myPreviewShape.Nullify();
    markInvalid(false);
    hide();
}

void SlatsTool::seedFields()
{
    // Blocked: seeding is not an edit, and previewing on the way in would
    // build the same ghost three times over.
    for (QLineEdit* field : {myWidth.data(), myGap.data(), myDepth.data()}) {
        if (field) field->blockSignals(true);
    }
    if (myWidth) myWidth->setText(plainLength(myPlan.width));
    if (myGap) myGap->setText(plainLength(myPlan.gap));
    if (myDepth) myDepth->setText(plainLength(myPlan.depth));
    for (QLineEdit* field : {myWidth.data(), myGap.data(), myDepth.data()}) {
        if (field) field->blockSignals(false);
    }
    const QString unit = QString::fromStdString(Measure::unitSuffix());
    if (myWidth) myWidth->setToolTip(tr("How wide each slat is, in %1").arg(unit));
    if (myGap) myGap->setToolTip(tr("The smallest gap between two slats, in %1 — the ends "
                                    "come out flush, so the real gap may be a hair more")
                                     .arg(unit));
    if (myDepth) myDepth->setToolTip(tr("How far the slats stand out of the face, in %1")
                                         .arg(unit));
    if (myFlip) myFlip->setToolTip(tr("Turn the slats the other way"));
}

QString SlatsTool::plainLength(double millimetres)
{
    QString formatted = QString::fromStdString(Measure::formatLength(millimetres));
    const QString suffix = QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    // formatLength() separates thousands with a plain comma and
    // Measure::parseLength() refuses one by grammar.
    formatted.remove(QLatin1Char(','));
    return formatted;
}

void SlatsTool::updatePreview()
{
    if (!myActive || !myWindow || !myView) return;

    double width = 0.0;
    double gap = 0.0;
    double depth = 0.0;
    const bool readable =
        myWidth && myGap && myDepth &&
        Measure::parseLength(myWidth->text().toStdString(), width) &&
        Measure::parseLength(myGap->text().toStdString(), gap) &&
        Measure::parseLength(myDepth->text().toStdString(), depth);

    if (!readable) {
        // Invalid input never previews: the last good ghost stays exactly as
        // it was - ExtrudePreview's rule - and the chip says what it wants.
        markInvalid(true);
        myReason = tr("Type a size in each box");
        if (myHasPreview && !myPreviewShape.IsNull())
            myView->setModelingPreview(myPreviewShape, myBodyId);
        applySize();
        update();
        return;
    }

    myPlan.width = width;
    myPlan.gap = gap;
    myPlan.depth = depth;

    const int revision = myWindow->document().revision();
    const bool cached = !myPreviewShape.IsNull() && myPreviewRevision == revision &&
                        myPreviewPlan.width == myPlan.width &&
                        myPreviewPlan.gap == myPlan.gap &&
                        myPreviewPlan.depth == myPlan.depth &&
                        myPreviewPlan.runAcross == myPlan.runAcross;
    if (!cached) {
        // The SAME call MainWindow::slatsApply() commits - one derivation for
        // the ghost and the commit, so a preview can never promise slats
        // Enter will not lay.
        const ModelingOps::SlatResult result = myWindow->slatsResult(myPlan);
        myPreviewRevision = revision;
        myPreviewPlan = myPlan;
        myPreviewShape = result.ok && !result.slats.empty()
                             ? ModelingOps::makeCompound(result.slats)
                             : TopoDS_Shape();
        myCount = result.ok ? static_cast<int>(result.slats.size()) : 0;
    }

    if (myPreviewShape.IsNull()) {
        markInvalid(true);
        myReason = myWindow->slatsRefusalFor(myPlan);
        if (myReason.isEmpty()) myReason = tr("These sizes lay no slats here");
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

void SlatsTool::cancel()
{
    if (myWindow) myWindow->cancelSlats();
}

void SlatsTool::commit()
{
    if (!myActive || !myWindow) return;
    // Deliberately NOT gated on myInvalid: sizes the geometry refuses have to
    // be REPORTED on Enter - a chip that silently does nothing is
    // indistinguishable from a broken one. slatsApply() raises the Failure.
    myWindow->slatsApply(myPlan);
}

QString SlatsTool::labelFor(int index)
{
    switch (index) {
        case 0: return tr("Width");
        case 1: return tr("Gap");
        default: return tr("Depth");
    }
}

QString SlatsTool::countText() const
{
    if (myCount <= 0) return tr("no slats");
    return myCount == 1 ? tr("1 slat") : tr("%1 slats").arg(myCount);
}

QString SlatsTool::hintText() const
{
    return tr("Enter · Esc");
}

QStringList SlatsTool::paintedTexts() const
{
    QStringList texts{countText(), hintText(), tr("Flip")};
    for (int i = 0; i < kFieldCount; ++i) texts << labelFor(i);
    if (!myReason.isEmpty()) texts << myReason;
    return texts;
}

int SlatsTool::rowWidth() const
{
    const QFontMetrics label(Theme::labelFont());
    int w = kPad;
    for (int i = 0; i < kFieldCount; ++i)
        w += label.horizontalAdvance(labelFor(i)) + 4 + kFieldWidth + kGap;
    return w + kPad;
}

QRect SlatsTool::labelRect(int index) const
{
    const QFontMetrics label(Theme::labelFont());
    int x = kPad;
    for (int i = 0; i < index; ++i)
        x += label.horizontalAdvance(labelFor(i)) + 4 + kFieldWidth + kGap;
    return QRect(x, kPad, label.horizontalAdvance(labelFor(index)), kRowHeight);
}

QRect SlatsTool::fieldRect(int index) const
{
    const QRect label = labelRect(index);
    return QRect(label.right() + 1 + 4, kPad, kFieldWidth, kRowHeight);
}

QRect SlatsTool::flipRect() const
{
    return QRect(kPad, kPad + kRowHeight + kRowGap, kFlipWidth, kRowHeight);
}

QRect SlatsTool::countRect() const
{
    const int left = flipRect().right() + 1 + kGap;
    return QRect(left, kPad + kRowHeight + kRowGap, std::max(0, width() - left - kPad),
                 kRowHeight);
}

QRect SlatsTool::hintRect() const
{
    const QFontMetrics badge(Theme::badgeFont());
    const int w = badge.horizontalAdvance(hintText());
    return QRect(std::max(kPad, width() - kPad - w), kPad + kRowHeight + kRowGap, w, kRowHeight);
}

QRect SlatsTool::reasonRect() const
{
    return QRect(kPad, kPad + kRowHeight * 2 + kRowGap + kReasonGap,
                 std::max(0, width() - kPad * 2), kReasonHeight);
}

void SlatsTool::applySize()
{
    // Measured with the fonts it paints with, so a longer reason cannot clip.
    const QFontMetrics badge(Theme::badgeFont());
    int w = rowWidth();
    w = std::max(w, kPad + kFlipWidth + kGap +
                        badge.horizontalAdvance(countText()) + kGap +
                        badge.horizontalAdvance(hintText()) + kPad);
    int h = kPad * 2 + kRowHeight * 2 + kRowGap;
    if (!myReason.isEmpty()) {
        w = std::max(w, badge.horizontalAdvance(myReason) + kPad * 2);
        h += kReasonGap + kReasonHeight;
    }
    const QSize wanted = Theme::wholeDevicePixels(QSize(w, h));
    if (wanted != QWidget::size()) {
        setFixedSize(wanted);
        reposition();
    }
    syncControls();
}

void SlatsTool::applyControlStyles()
{
    for (QLineEdit* field : {myWidth.data(), myGap.data(), myDepth.data()}) {
        if (field) field->setFont(Theme::bodyFont());
    }
    if (myFlip) {
        myFlip->setFont(Theme::labelFont());
        myFlip->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 4px; font-size: %4pt; padding: 0px 6px; } "
                           "QPushButton:hover { background-color: %5; }")
                .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name())
                .arg(Theme::labelFont().pointSizeF())
                .arg(Theme::chipHover().name()));
    }
    markInvalid(myInvalid);
}

void SlatsTool::markInvalid(bool invalid)
{
    myInvalid = invalid;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    for (QLineEdit* field : {myWidth.data(), myGap.data(), myDepth.data()}) {
        if (!field) continue;
        field->setStyleSheet(QStringLiteral("QLineEdit { background-color: %1; color: %2; "
                                            "border: 1px solid %3; border-radius: 0px; "
                                            "padding: 2px 6px; }")
                                 .arg(Theme::chip().name(), Theme::text().name(),
                                      border.name()));
    }
}

void SlatsTool::syncControls()
{
    // Sibling geometry is this card's local rect translated by pos() - the
    // ExtrudePreview idiom - and visibility is DERIVED from the card's own.
    const QLineEdit* fields[kFieldCount] = {myWidth.data(), myGap.data(), myDepth.data()};
    for (int i = 0; i < kFieldCount; ++i) {
        QLineEdit* field = const_cast<QLineEdit*>(fields[i]);
        if (!field) continue;
        field->setGeometry(fieldRect(i).translated(pos()));
        field->setVisible(isVisible());
        if (isVisible()) field->raise();
    }
    if (myFlip) {
        myFlip->setGeometry(flipRect().translated(pos()));
        myFlip->setVisible(isVisible());
        if (isVisible()) myFlip->raise();
    }
}

void SlatsTool::reposition()
{
    if (!myView || !isVisible()) return;
    // BOTTOM CENTRE, not beside the face - the user's own call on the first
    // build. Every other gesture chip in this app stands beside the thing it
    // edits because that thing is small and local (one face, one edge, one
    // size number); a run of slats covers the whole panel, so a chip anchored
    // to it sits ON the work it is showing you, and the ghost is the thing
    // that has to stay visible.
    //
    // A fixed spot, so it does not move while the camera does - which is also
    // why this no longer projects anything.
    const int x = (myView->width() - width()) / 2;
    const int y = myView->height() - height() - kBottomMargin;
    // Whole DEVICE pixels, in the window's own coordinates - the position
    // half of Theme's rule, the same snap ToastHost::reposition() ends with.
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = myView->devicePixelRatioF();
    move(Theme::snapToDevicePixels(std::max(0, x), origin.x(), dpr),
         Theme::snapToDevicePixels(std::max(0, y), origin.y(), dpr));
}

void SlatsTool::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    syncControls();
}

void SlatsTool::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    GestureChip::paintFrame(painter, this, myInvalid);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    for (int i = 0; i < kFieldCount; ++i)
        painter.drawText(labelRect(i), Qt::AlignVCenter | Qt::AlignLeft, labelFor(i));

    painter.setFont(Theme::badgeFont());
    painter.setPen(myCount > 0 ? Theme::textMuted() : Theme::danger());
    painter.drawText(countRect(), Qt::AlignVCenter | Qt::AlignLeft, countText());

    painter.setPen(Theme::textMuted());
    painter.drawText(hintRect(), Qt::AlignVCenter | Qt::AlignRight, hintText());

    if (!myReason.isEmpty()) {
        painter.setPen(Theme::danger());
        painter.drawText(reasonRect(), Qt::AlignVCenter | Qt::AlignLeft, myReason);
    }
}

void SlatsTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncControls();
    // Installed for exactly as long as the chip is up.
    QCoreApplication::instance()->installEventFilter(this);
}

void SlatsTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    for (QLineEdit* field : {myWidth.data(), myGap.data(), myDepth.data()}) {
        if (field) field->hide();
    }
    if (myFlip) myFlip->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void SlatsTool::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncControls();
}

void SlatsTool::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncControls();
}

bool SlatsTool::eventFilter(QObject* watched, QEvent* event)
{
    // Enter and Escape belong to this chip for as long as it is VISIBLE,
    // whatever holds focus - the one KeyClaim implementation every gesture
    // chip shares. Not exempting line edits: Enter IN a size field is exactly
    // what the claim routes to commit().
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
