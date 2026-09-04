#include "BevelArrow.h"

#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <TopoDS_Face.hxx>

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
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
constexpr int kMinWidth = 176;
constexpr int kLabelHeight = 18;
constexpr int kFieldHeight = 24;
constexpr int kHintGap = 4;
constexpr int kHintHeight = 13;
constexpr int kHintLineGap = 1;
// The gap the kind name keeps from the value read out beside it, and the
// widest value the chip reserves room for - a snapped drag on furniture never
// reaches five figures, and Qt clips rather than overflowing if one ever did.
constexpr int kKindGap = 12;
// How far the chip stands off the arrow's projected head, and how far it is
// kept inside the viewport's own edges.
constexpr int kChipGap = 18;
constexpr int kEdgeInset = 8;

}   // namespace

// --- the value chip ---------------------------------------------------------

BevelArrow::BevelArrow(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // Paints its own card and must never eat a click meant for the model
    // behind it - only the sibling field below is ever interactive. See
    // PullArrow.h for why that means the field cannot be a child of this
    // widget: WA_TransparentForMouseEvents excludes a whole SUBTREE from
    // hit-testing.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    myField = new QLineEdit(view);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    // Both this card's SIZE (measured with the fonts it paints with) and the
    // field's explicit font move when the type scale does, and neither can be
    // re-derived inside paintEvent() - so both are set through applyTheme(),
    // once here and again on every Theme broadcast.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &BevelArrow::applyTheme);
    connect(myField, &QLineEdit::textChanged, this,
            [this](const QString&) { updatePreview(); });
    syncFieldTooltip();
    markInvalid(false);

    syncFieldGeometry();
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &BevelArrow::refresh);
    if (myView) {
        connect(myView, &OcctViewWidget::cameraChanged, this, &BevelArrow::reposition);
        connect(myView, &OcctViewWidget::bevelDragged, this, &BevelArrow::onDragged);
        connect(myView, &OcctViewWidget::bevelReleased, this, &BevelArrow::onReleased);
    }

    // Milestone 5 item 2: this card's own corners over the GL surface, at
    // the same radius paintEvent() paints its card with. myField is NOT
    // masked - see markInvalid()'s own comment: border-radius 0, deliberately.
    Theme::installCardMask(this, 8);
}

BevelArrow::~BevelArrow()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    // Sibling, not a child - Qt's parent-child cascade does not reach it when
    // this panel alone is destroyed, and QPointer makes the delete a safe
    // no-op when the shared parent tears both down instead.
    delete myField;
}

void BevelArrow::applyTheme()
{
    // Measured with the fonts these strings are actually painted with, not
    // assumed from a character count - CLAUDE.md's rule, which exists
    // because a title measured non-bold and painted bold clips. The two
    // hint lines are the long ones; the title row reserves room for the
    // widest kind word beside a comfortably large value.
    const QFontMetrics badge(Theme::badgeFont());
    const QFontMetrics label(Theme::labelFont());
    int content = 0;
    for (int line = 0; line < 2; ++line)
        content = std::max(content, badge.horizontalAdvance(hintText(line)));
    // The widest title row this chip can paint: the longer kind word, the
    // count suffix a multi-edge gesture adds, and a comfortably large value
    // beside it. Measured with the label font it is painted with - a title
    // measured without the suffix and painted with one clips, which is
    // exactly the failure CLAUDE.md's measure-with-the-font rule names.
    content = std::max(content,
                       label.horizontalAdvance(tr("%1 — %2 edges")
                                                   .arg(tr("Chamfer"), QStringLiteral("12"))) +
                           kKindGap +
                           label.horizontalAdvance(QStringLiteral("C 1,200 mm")));

    const int margin = Theme::surfaceShadowMargin();
    // Through Theme::wholeDevicePixels(), not straight to setFixedSize().
    // The measured height here is 93, and 93 logical rows at this
    // machine's 150% scaling is 139.5 DEVICE rows: Qt flushes 140 and the
    // paint event's logical clip stops this widget's own painter at 139,
    // so the last row keeps whatever the backing store held. Over the GL
    // surface that is not transparent - the first magnified capture of
    // this chip carried an exact 0,0,0 hairline 264 device pixels wide
    // along its bottom edge. paintSurface() cannot reach outside the clip
    // and the viewport cannot paint underneath a child, so the size is
    // where this is fixed. See Theme.h.
    setFixedSize(Theme::wholeDevicePixels(
        QSize(std::max(kMinWidth, content + kPad * 2) + margin * 2,
              kPad * 2 + kLabelHeight + kFieldHeight + kHintGap +
                  kHintHeight * 2 + kHintLineGap + margin * 2)));

    // An explicitly set font does not follow QApplication::setFont - see
    // ExtrudePreview's constructor.
    if (myField) myField->setFont(Theme::bodyFont());
    // The field is a sibling positioned against this card's rectangle, which
    // has just moved.
    syncFieldGeometry();
    update();
}

QLineEdit* BevelArrow::field() const
{
    return myField;
}

void BevelArrow::refresh()
{
    if (!myWindow || !myView) return;

    // ONE predicate, shared with updateStateLabel(). Note it refuses outside
    // edge-selection mode and while a face is pending, which is what keeps
    // this, the pull arrow, the transform gizmo and ExtrudePreview mutually
    // exclusive - and with them their application-wide Enter/Escape claims.
    std::vector<TopoDS_Edge> edges;
    TopoDS_Edge edge;
    int bodyId = 0;
    gp_Pnt centre;
    gp_Dir outward;
    if (!myWindow->bevelTarget(edges, edge, bodyId, centre, outward)) {
        end();
        return;
    }

    if (myEdge.IsNull() || !myEdge.IsSame(edge) || bodyId != myBodyId || !sameEdges(edges)) {
        begin(edges, edge, bodyId, centre, outward);
        return;
    }

    // Already up on this edge. Repaint and re-read: the value readout carries
    // the unit and the field is read in it, so a unit switch has to rebuild
    // the shape as well as the text - ExtrudePreview's lesson.
    myCentre = centre;
    myOutward = outward;
    update();
    updatePreview();
    reposition();
}

bool BevelArrow::sameEdges(const std::vector<TopoDS_Edge>& edges) const
{
    // As SETS, not as sequences. The order is OCCT's - these come from
    // AIS_InteractiveContext's own selection iteration, which is under no
    // obligation to hand the same edges back in the same order twice. A
    // positional comparison would report "different" for a selection nothing
    // had happened to, which restarts the gesture: the size field is zeroed,
    // a preview the user was judging vanishes, and none of it is visible in a
    // test that only ever selects in one order.
    if (edges.size() != myEdges.size()) return false;

    std::vector<bool> matched(myEdges.size(), false);
    for (const TopoDS_Edge& edge : edges) {
        bool found = false;
        for (std::size_t i = 0; i < myEdges.size() && !found; ++i) {
            if (matched[i] || !edge.IsSame(myEdges[i])) continue;
            matched[i] = true;   // one-to-one, so a repeat cannot pair twice
            found = true;
        }
        if (!found) return false;
    }
    return true;
}

void BevelArrow::begin(const std::vector<TopoDS_Edge>& edges, const TopoDS_Edge& edge,
                       int bodyId, const gp_Pnt& centre, const gp_Dir& outward)
{
    myEdge = edge;
    myEdges = edges;
    myBodyId = bodyId;
    myCentre = centre;
    myOutward = outward;

    mySize = 0.0;
    myFillet = true;
    myHasPreview = false;
    myView->clearModelingPreview();
    myView->showBevelArrow(myCentre, myOutward);

    if (myField) {
        // Zero, unconditionally: a size left over from the last edge would
        // silently preview a bevel the user never asked for.
        myField->blockSignals(true);
        myField->setText(textForSize(0.0));
        myField->blockSignals(false);
    }
    syncFieldTooltip();
    markInvalid(false);

    reposition();
    show();
    raise();
    if (myField) myField->raise();
}

void BevelArrow::end()
{
    if (myView) {
        myView->clearModelingPreview();
        myView->clearBevelArrow();
    }
    myEdge.Nullify();
    myEdges.clear();
    myBodyId = 0;
    mySize = 0.0;
    myHasPreview = false;
    markInvalid(false);
    hide();
}

void BevelArrow::cancel()
{
    if (myView) myView->clearModelingPreview();
    myHasPreview = false;
    mySize = 0.0;
    if (myField) {
        myField->blockSignals(true);
        myField->setText(textForSize(0.0));
        myField->blockSignals(false);
    }
    markInvalid(false);
    update();
    // The arrow and the selection are deliberately untouched: the edge is
    // still selected, the predicate still holds, and the user can drag again
    // without re-picking anything.
}

void BevelArrow::onDragged(double millimetres)
{
    if (!isVisible() || !myField) return;

    // The SIGN picks the operation and the MAGNITUDE is the size. A drag that
    // is exactly zero - which a snapped drag passes through on its way from
    // one side to the other - leaves the kind alone rather than flipping it
    // twice on the way past.
    const bool fillet =
        std::fabs(millimetres) > 1.0e-7 ? millimetres < 0.0 : myFillet;
    const bool kindChanged = fillet != myFillet;
    myFillet = fillet;
    if (kindChanged) syncFieldTooltip();

    // The drag writes the FIELD, and the field is what previews and commits -
    // one value, one path.
    const QString text = textForSize(std::fabs(millimetres));
    if (text != myField->text()) {
        myField->setText(text);   // textChanged -> updatePreview()
    } else if (kindChanged) {
        // Same number, other operation: dragged straight through zero to the
        // far side. textChanged cannot fire for that, and without this the
        // preview would keep showing the kind the user has just dragged away
        // from.
        updatePreview();
    }
    update();
}

void BevelArrow::onReleased(bool dragged)
{
    if (!isVisible()) return;
    if (dragged) commit();
}

void BevelArrow::updatePreview()
{
    if (!myField || !myView || !myWindow || myEdge.IsNull() || myEdges.empty()) return;

    // Through Measure::parseLength, never QString::toDouble: the chip's value
    // readout names the display unit, so the field has to be read back in that
    // same unit or typing "3" with centimetres selected would build 3 mm.
    double size = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), size)) {
        markInvalid(true);
        return;
    }
    size = std::fabs(size);

    if (size < 1.0e-7) {
        // Zero is "no bevel yet", not an error - it is what begin() seeds.
        markInvalid(false);
        mySize = 0.0;
        if (myHasPreview) {
            myView->clearModelingPreview();
            myHasPreview = false;
        }
        update();
        return;
    }

    // The SAME ModelingOps calls the commit uses - the LIST forms, over the
    // same list, so a three-edge preview is built by the same single build
    // that Enter will run. A preview built by a different path is a lie, and
    // this is the one place a user judges a number by what it looks like;
    // previewing one edge and committing three would be that lie at its
    // largest.
    const TopoDS_Shape body = myWindow->document().shapeOf(myBodyId);
    const ModelingOps::BooleanResult result =
        myFillet ? ModelingOps::filletEdges(body, myEdges, size)
                 : ModelingOps::chamferEdges(body, myEdges, size);
    if (!result.ok) {
        // A refusal MID-DRAG is not an error to report - at furniture scale a
        // radius that momentarily exceeds what the neighbouring face can give
        // up is a when, not an if, and OCCT fillets legitimately fail there.
        // The last good preview stays exactly as it was and only the field's
        // border marks the problem; the failure is only ever SPOKEN when the
        // user commits it (see commit()).
        markInvalid(true);
        return;
    }

    markInvalid(false);
    mySize = size;
    // The DEDICATED channel, never setPreview(): that slot is already shared
    // by the sketch outline and the extrude preview, and CLAUDE.md records the
    // bug that cost. myBodyId goes with it so the body the preview stands in
    // for is drawn as a cage - a fillet preview is coincident with the body it
    // rounds everywhere except at the one edge, so a shaded body underneath it
    // would be a z-fight across the whole shape.
    myView->setModelingPreview(result.shape, myBodyId);
    myHasPreview = true;
    update();
}

void BevelArrow::commit()
{
    if (!myField || !myWindow || myEdge.IsNull() || myEdges.empty()) return;

    double size = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), size)) return;
    size = std::fabs(size);
    // Zero is a cancel, not an error: a press and release with no movement
    // between them reaches here, and it must not raise a failure toast.
    if (size < 1.0e-7) return;

    // Deliberately NOT gated on myInvalid. A size the kernel refuses has to be
    // REPORTED when the user commits it - a chip that silently does nothing on
    // Enter is indistinguishable from a broken one. bevelEdgeBy() owns both
    // outcomes: the Failure toast in cause-and-fix form, or the undo
    // checkpoint, the replaced body and the Note toast with Undo.
    const std::vector<TopoDS_Edge> edges = myEdges;
    if (!myWindow->bevelEdgesBy(edges, size, myFillet)) {
        markInvalid(true);
        return;
    }
    // On success bevelEdgeBy() cleared the selection, so the appStateChanged
    // it emitted has already run refresh() -> end() on this widget.
}

QString BevelArrow::textForSize(double millimetres) const
{
    QString formatted = QString::fromStdString(Measure::formatLength(millimetres));
    const QString suffix =
        QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    // The separator has to come out - this text is READ BACK by
    // Measure::parseLength() on the very next textChanged, and that grammar
    // has no comma in it. See PullArrow::textForDistance(), where a drag past
    // a thousand froze the preview because the chip could not read its own
    // writing.
    formatted.remove(QLatin1Char(','));
    return formatted;
}

QString BevelArrow::kindText() const
{
    // The vocabulary, in the user's words. Fillet rounds, Chamfer flattens -
    // and the word the code uses for the pair of them never reaches a painted
    // string.
    const QString kind = myFillet ? tr("Fillet") : tr("Chamfer");
    // Only when there is a count worth reporting. One edge reads exactly as
    // it always did, and the plural is written out rather than parenthesised:
    // this chip is only ever asked for the count when it is two or more, so
    // "edges" is the only form it can need.
    if (myEdges.size() < 2) return kind;
    return tr("%1 — %2 edges")
        .arg(kind, QString::number(static_cast<int>(myEdges.size())));
}

QString BevelArrow::valueText() const
{
    double size = 0.0;
    if (!myField || !Measure::parseLength(myField->text().toStdString(), size)) size = 0.0;
    // Through Measure::formatLength, so it follows the display unit for free:
    // "R 20 mm" in millimetres is "R 2 cm" in centimetres. R for the radius a
    // fillet is specified by, C for the distance a chamfer is - the two
    // letters a drawing would use.
    const QString formatted =
        QString::fromStdString(Measure::formatLength(std::fabs(size)));
    return myFillet ? tr("R %1").arg(formatted) : tr("C %1").arg(formatted);
}

QString BevelArrow::hintText(int line) const
{
    // A modeless panel with invisible verbs is how the keyboard commit and
    // cancel went unnoticed for a whole branch on ExtrudePreview. This chip
    // has no buttons either, so both the gesture and both keys are on it in
    // words - and the gesture needs saying most of all, because one drag axis
    // doing two different things is not something an arrow can show.
    // Named with the vocabulary table's own words rather than with what the
    // two operations do to the edge. "round"/"flatten" describe the result
    // correctly and are still the wrong copy: the chip's own title row, the
    // field's tooltip and every refusal say Fillet and Chamfer, so a hint that
    // says something else is a second name for the same thing.
    return line == 0 ? tr("Drag in to Fillet, out to Chamfer")
                     : tr("Enter applies, Esc cancels");
}

QRect BevelArrow::fieldRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight,
                 QWidget::width() - margin * 2 - kPad * 2, kFieldHeight);
}

QRect BevelArrow::hintRect(int line) const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad,
                 margin + kPad + kLabelHeight + kFieldHeight + kHintGap +
                     line * (kHintHeight + kHintLineGap),
                 QWidget::width() - margin * 2 - kPad * 2, kHintHeight);
}

void BevelArrow::syncFieldGeometry()
{
    if (!myField) return;
    // Shares this widget's parent, so fieldRect() - in this widget's own local
    // coordinates - needs translating by pos().
    myField->setGeometry(fieldRect().translated(pos()));
    // DERIVED, not left to a hide event that may never arrive.
    myField->setVisible(isVisible());
    myField->raise();
}

void BevelArrow::syncFieldTooltip()
{
    if (!myField) return;
    // The one place the two operations are taught in words. Follows the kind,
    // so the tooltip can never describe the other one.
    //
    // "rounds the edge" / "flattens the edge" is what this said until the
    // whole-branch review taught the sweep to see `round` and `flatten` at a
    // word boundary. They are the Never column for Fillet and Chamfer: a user
    // who reads "rounds" has no control anywhere in the app spelled that way,
    // and the operation is the only word that leads anywhere. The teaching
    // sentence says what the SHAPE becomes instead, which is the part a name
    // cannot carry.
    myField->setToolTip(myFillet ? tr("Fillet radius — the edge becomes a curve")
                                 : tr("Chamfer size — the edge becomes a flat"));
}

void BevelArrow::reposition()
{
    if (!myView) return;

    // Rebuild the arrow at the new scale first: it is drawn in world units but
    // sized in screen pixels, so a zoom changes the geometry it needs.
    if (isVisible() && !myEdge.IsNull()) myView->showBevelArrow(myCentre, myOutward);

    gp_Pnt headPoint;
    QPoint at;
    if (!myView->bevelArrowHead(headPoint) || !myView->projectToScreen(headPoint, at)) return;

    // Beside the outward head, flipped to the other side rather than clamped
    // when that would run off the right edge, so the chip stays associated
    // with the arrow it labels. Same honest limit as PullArrow: this does not
    // step around the rail or the drawer, because a value chip that walks away
    // from its arrow stops labelling it.
    int x = at.x() + kChipGap;
    if (x + QWidget::width() > myView->width() - kEdgeInset)
        x = at.x() - kChipGap - QWidget::width();
    x = std::clamp(x, kEdgeInset,
                   std::max(kEdgeInset, myView->width() - QWidget::width() - kEdgeInset));

    int y = at.y() - QWidget::height() / 2;
    y = std::clamp(y, kEdgeInset,
                   std::max(kEdgeInset, myView->height() - QWidget::height() - kEdgeInset));

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

void BevelArrow::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    if (myField) myField->raise();
}

void BevelArrow::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    // border-radius: 0, for the reason spelled out on
    // ExtrudePreview::markInvalid() - this field is a sibling parented
    // straight to the viewport, sitting on the GL surface with no card of its
    // own underneath, and over that surface an unpainted corner pixel is not
    // transparent but whatever the driver left there.
    myField->setStyleSheet(QStringLiteral(
                               "QLineEdit { background-color: %1; color: %2; "
                               "border: 1px solid %3; border-radius: 0px; padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

QStringList BevelArrow::paintedTexts() const
{
    return {kindText(), valueText(), hintText(0), hintText(1)};
}

void BevelArrow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    if (myInvalid) {
        QPainterPath outline;
        outline.addRoundedRect(body, 8, 8);
        painter.setPen(QPen(Theme::danger(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }

    // The kind in words on the left, the value it would build on the right.
    // Both on one row, because they are one statement: "Fillet ... R 20 mm".
    const QRect titleRow(body.left() + kPad, body.top() + kPad,
                         body.width() - kPad * 2, kLabelHeight);
    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(titleRow, Qt::AlignVCenter | Qt::AlignLeft, kindText());
    painter.setPen(Theme::accent());
    painter.drawText(titleRow, Qt::AlignVCenter | Qt::AlignRight, valueText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    for (int line = 0; line < 2; ++line)
        painter.drawText(hintRect(line), Qt::AlignVCenter | Qt::AlignLeft, hintText(line));
}

void BevelArrow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncFieldGeometry();
    // Installed for exactly as long as the chip is up - ShortcutSheet's and
    // ExtrudePreview's lifetime rule for an application-wide filter.
    QCoreApplication::instance()->installEventFilter(this);
}

void BevelArrow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void BevelArrow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncFieldGeometry();
}

void BevelArrow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncFieldGeometry();
}

bool BevelArrow::eventFilter(QObject* watched, QEvent* event)
{
    // Enter and Escape belong to this chip for as long as it is VISIBLE,
    // whatever holds focus. The whole reason a live preview exists is that the
    // user orbits to judge the shape before committing - and an orbit is a
    // press in the viewport, which takes focus off the field. See
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

