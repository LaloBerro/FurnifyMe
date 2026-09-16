#include "ReMeasureTool.h"

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
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPad = GestureChip::kPad;
constexpr int kGap = 8;
constexpr int kRowHeight = 26;
constexpr int kFieldWidth = 84;
constexpr int kReasonGap = 4;
constexpr int kReasonHeight = 16;

}  // namespace

ReMeasureTool::ReMeasureTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and never eats a click meant for the model behind
    // it - only the sibling field is interactive (see the header).
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    Theme::makeSurfaceTransparent(this);

    myField = new QLineEdit(view);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    connect(myField, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });

    applyControlStyles();
    applySize();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyControlStyles();
        applySize();
        update();
    });

    myField->hide();
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &ReMeasureTool::refresh);
    if (myView) connect(myView, &OcctViewWidget::cameraChanged, this, &ReMeasureTool::reposition);
}

ReMeasureTool::~ReMeasureTool()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    delete myField;
}

QLineEdit* ReMeasureTool::field() const
{
    return myField;
}

void ReMeasureTool::refresh()
{
    if (!myWindow || !myView) return;

    if (!myWindow->reMeasureActive()) {
        if (myActive) end();
        return;
    }

    const int bodyId = myWindow->reMeasureBodyId();
    const int index = myWindow->reMeasureSizeIndex();
    if (!myActive || bodyId != myBodyId || index != mySizeIndex) {
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

void ReMeasureTool::begin()
{
    myActive = true;
    myBodyId = myWindow->reMeasureBodyId();
    mySizeIndex = myWindow->reMeasureSizeIndex();
    mySize = myWindow->reMeasureCurrentSize();
    myHasPreview = false;
    myReason.clear();
    myPreviewShape.Nullify();
    myPreviewRevision = -1;
    myPreviewAnchor = -1;
    markInvalid(false);

    if (myField) {
        // Seeded with the size the user right-clicked, in the unit they are
        // reading - so selecting all and typing replaces exactly the number
        // on screen. Signals blocked: the seed is not an edit, and previewing
        // the size the body already is would build a ghost of nothing.
        myField->blockSignals(true);
        myField->setText(plainLength(mySize));
        myField->blockSignals(false);
        myField->setToolTip(tr("%1 in %2 — Enter applies, Esc cancels")
                                .arg(labelText(), QString::fromStdString(Measure::unitSuffix())));
    }

    applySize();
    show();
    raise();
    reposition();
    syncControls();
    if (myField) {
        // Typing IS the gesture: the digits go to the field rather than to the
        // view shortcuts 0-3 would otherwise trigger.
        myField->setFocus(Qt::OtherFocusReason);
        myField->selectAll();
    }
}

void ReMeasureTool::end()
{
    myActive = false;
    if (myView && myHasPreview) myView->clearModelingPreview();
    myHasPreview = false;
    myBodyId = 0;
    mySizeIndex = -1;
    myReason.clear();
    myPreviewShape.Nullify();
    markInvalid(false);
    hide();
}

QString ReMeasureTool::plainLength(double millimetres)
{
    QString formatted = QString::fromStdString(Measure::formatLength(millimetres));
    const QString suffix = QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    // formatLength() separates thousands with a plain comma and
    // Measure::parseLength() refuses one by grammar - so a 1,200 mm board
    // would seed a field that reads as invalid the instant it appears.
    formatted.remove(QLatin1Char(','));
    return formatted;
}

void ReMeasureTool::anchorChanged()
{
    if (!myActive) return;
    // Which end stays put changes the SHAPE, not merely the bookkeeping, so
    // the ghost is rebuilt rather than left standing.
    updatePreview();
}

void ReMeasureTool::updatePreview()
{
    if (!myActive || !myWindow || !myView || !myField) return;

    double typed = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), typed) ||
        typed <= ModelingOps::kResizeNoChange) {
        // Invalid input never previews: the last good ghost stays exactly as
        // it was - ExtrudePreview's rule - and the chip says what it wants.
        markInvalid(true);
        myReason = MainWindow::reMeasureSizeRefusalText();
        if (myHasPreview && !myPreviewShape.IsNull())
            myView->setModelingPreview(myPreviewShape, myBodyId);
        applySize();
        update();
        return;
    }

    mySize = typed;
    const int anchor = myView->resizePinAnchor();
    const int revision = myWindow->document().revision();
    const bool cached = !myPreviewShape.IsNull() && myPreviewRevision == revision &&
                        myPreviewSize == mySize && myPreviewAnchor == anchor;
    if (!cached) {
        // The SAME call MainWindow::reMeasureTo() commits - one derivation for
        // the ghost and the commit, so a preview can never promise a shape
        // Enter will not make.
        const ModelingOps::BooleanResult result = myWindow->reMeasureResult(mySize);
        myPreviewRevision = revision;
        myPreviewSize = mySize;
        myPreviewAnchor = anchor;
        myPreviewShape = result.ok ? result.shape : TopoDS_Shape();
    }

    if (myPreviewShape.IsNull()) {
        // A size the geometry refuses shows NO ghost - a ghost of the last
        // good size would be a picture of a body Enter will not build.
        markInvalid(true);
        myReason = myWindow->reMeasureRefusalFor(mySize);
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

void ReMeasureTool::cancel()
{
    if (myWindow) myWindow->cancelReMeasure();
}

void ReMeasureTool::commit()
{
    if (!myActive || !myWindow || !myField) return;
    double typed = 0.0;
    // Deliberately NOT gated on myInvalid: a size the geometry refuses has to
    // be REPORTED on Enter - a chip that silently does nothing is
    // indistinguishable from a broken one. An unreadable field goes in as 0,
    // which checkResize() names as a size at or below zero.
    const double size = Measure::parseLength(myField->text().toStdString(), typed) ? typed : 0.0;
    myWindow->reMeasureTo(size);
    // On success reMeasureTo() ended the gesture and its appStateChanged has
    // already run refresh() -> end() here.
}

QString ReMeasureTool::labelText() const
{
    return MainWindow::reMeasureSizeName(mySizeIndex);
}

QString ReMeasureTool::hintText() const
{
    return tr("Enter · Esc — click an end to keep it");
}

QStringList ReMeasureTool::paintedTexts() const
{
    QStringList texts{labelText(), hintText()};
    // Every name this chip can paint, not only the one it is painting - the
    // sweep must cover the two the current gesture happens not to be about.
    for (int i = 0; i < 3; ++i) texts << MainWindow::reMeasureSizeName(i);
    if (!myReason.isEmpty()) texts << myReason;
    return texts;
}

int ReMeasureTool::rowWidth() const
{
    const int label = QFontMetrics(Theme::labelFont()).horizontalAdvance(labelText());
    const int hint = QFontMetrics(Theme::badgeFont()).horizontalAdvance(hintText());
    return kPad + label + kGap + kFieldWidth + kGap + hint + kPad;
}

QRect ReMeasureTool::labelRect() const
{
    const int label = QFontMetrics(Theme::labelFont()).horizontalAdvance(labelText());
    return QRect(kPad, kPad, label, kRowHeight);
}

QRect ReMeasureTool::fieldRect() const
{
    return QRect(labelRect().right() + 1 + kGap, kPad, kFieldWidth, kRowHeight);
}

QRect ReMeasureTool::hintRect() const
{
    const int left = fieldRect().right() + 1 + kGap;
    return QRect(left, kPad, std::max(0, width() - left - kPad), kRowHeight);
}

QRect ReMeasureTool::reasonRect() const
{
    return QRect(kPad, kPad + kRowHeight + kReasonGap, std::max(0, width() - kPad * 2),
                 kReasonHeight);
}

void ReMeasureTool::applySize()
{
    // Measured with the fonts it paints with, so a longer reason cannot clip.
    int w = rowWidth();
    int h = kPad * 2 + kRowHeight;
    if (!myReason.isEmpty()) {
        w = std::max(w, QFontMetrics(Theme::badgeFont()).horizontalAdvance(myReason) + kPad * 2);
        h += kReasonGap + kReasonHeight;
    }
    const QSize wanted = Theme::wholeDevicePixels(QSize(w, h));
    if (wanted != QWidget::size()) {
        setFixedSize(wanted);
        reposition();
    }
    syncControls();
}

void ReMeasureTool::applyControlStyles()
{
    if (myField) myField->setFont(Theme::bodyFont());
    markInvalid(myInvalid);
}

void ReMeasureTool::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    myField->setStyleSheet(QStringLiteral("QLineEdit { background-color: %1; color: %2; "
                                          "border: 1px solid %3; border-radius: 0px; "
                                          "padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

void ReMeasureTool::syncControls()
{
    // Sibling geometry is this card's local rect translated by pos() - the
    // ExtrudePreview idiom - and visibility is DERIVED from the card's own.
    if (!myField) return;
    myField->setGeometry(fieldRect().translated(pos()));
    myField->setVisible(isVisible());
    if (isVisible()) myField->raise();
}

void ReMeasureTool::reposition()
{
    if (!myView || !isVisible()) return;
    // Beside the very number the gesture is about, the chip family's own
    // placement (flipped rather than clamped when it would run off the right
    // edge, then snapped to whole device pixels).
    //
    // Anchored on the number's own EDGE, not its centre: the gap the family
    // places by is smaller than half a size label, so anchoring on the centre
    // stood the card over the very number it is editing - measured on the
    // first capture, where "600 mm" was half hidden behind the field reading
    // 450. The old size stays readable beside the new one instead.
    QRect box;
    if (!myView->selectionSizeLabelRect(mySizeIndex, box)) return;
    move(GestureChip::placeBeside(this, myView,
                                  QPoint(box.right(), box.center().y())));
}

void ReMeasureTool::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    syncControls();
}

void ReMeasureTool::paintEvent(QPaintEvent* /*event*/)
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

void ReMeasureTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncControls();
    // Installed for exactly as long as the chip is up.
    QCoreApplication::instance()->installEventFilter(this);
}

void ReMeasureTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void ReMeasureTool::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncControls();
}

void ReMeasureTool::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncControls();
}

bool ReMeasureTool::eventFilter(QObject* watched, QEvent* event)
{
    // Enter and Escape belong to this chip for as long as it is VISIBLE,
    // whatever holds focus - the one KeyClaim implementation every gesture
    // chip shares. Not exempting line edits: Enter IN the size field is
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
