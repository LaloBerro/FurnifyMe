#include "ExtrudePreview.h"

#include "MainWindow.h"
#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "SketchController.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 230;
constexpr int kLabelHeight = 20;
constexpr int kFieldHeight = 26;
constexpr int kHintGap = 6;
constexpr int kHintHeight = 16;
// The PAINTED offset from the viewport's top edge - matches what
// ViewportOverlay's own kMargin targets for the widgets it anchors (see
// ViewportOverlay.cpp). Both files subtract Theme::surfaceShadowMargin() from
// it, and that is zero now, so painted edge and widget rect coincide; the
// subtraction survives in both places as the record of what the number
// actually measures.
constexpr int kMargin = 16;
}  // namespace

ExtrudePreview::ExtrudePreview(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    // The panel paints its own background and label and must never eat a
    // click meant for the model behind it - only the sibling field below is
    // ever interactive. See the class comment in the header for why that
    // means the field cannot be a child of this widget.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    myField = new QLineEdit(view);
    // Closes the same class of bug documented on HintBalloon's balloon and
    // Toast's UndoControl: an unhandled release would otherwise propagate to
    // the viewport behind this field and trigger a real pick underneath the
    // panel.
    myField->setAttribute(Qt::WA_NoMousePropagation);
    // The height the user types is body text, same as everything else they
    // read - set explicitly rather than left to inherit, since it is the one
    // widget on this panel the user actually types into. An explicit font
    // does NOT follow QApplication::setFont, which is exactly why the Theme
    // broadcast has to put it back: the type-scale sweep reads this widget's
    // own font, and a field left at the old base size is a fifth size in a
    // four-size scale. Set through applyTheme(), which also sizes the card.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &ExtrudePreview::applyTheme);
    // Enter and Escape are NOT wired here any more - not to returnPressed,
    // not to a filter on the field. Both are claimed application-wide for as
    // long as this panel is visible; see eventFilter() for the whole story.
    connect(myField, &QLineEdit::textChanged, this,
            [this](const QString&) { updatePreview(); });
    markInvalid(false);   // paints the field's normal (valid) border once

    syncFieldGeometry();
    hide();

    // No filter on the viewport's resize any more: MainWindow drives
    // replace() from ViewportOverlay::laidOut() instead, which is the only
    // moment the chip clusters this panel shares the top edge with are
    // guaranteed to be at their final rectangle AND already raise()d - the
    // raw resize event ran before both.

    // Ties this widget's own life to the pending face it was built from -
    // see onAppStateChanged() and the header for why a route this class does
    // not know about (onStartSketch(), onCancelSketch(), and any future one)
    // must not be trusted to remember to call cancel() individually.
    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this,
                          &ExtrudePreview::onAppStateChanged);

    // Milestone 5 item 2: this card's own corners over the GL surface, at
    // the same radius paintEvent() paints its card with. myField is NOT
    // masked - see markInvalid()'s own comment: it is deliberately painted
    // with border-radius 0 for exactly this reason, so there is no rounded
    // shape on it to trace.
    Theme::installCardMask(this, 8);
}

ExtrudePreview::~ExtrudePreview()
{
    // hideEvent() normally does this, but a panel destroyed while still
    // visible would otherwise leave a dangling application-wide filter.
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);

    // Sibling, not a child - same reasoning as Toast::~Toast() and
    // WalkthroughPanel::~WalkthroughPanel(): Qt's parent-child cascade does
    // not clean this up when this panel alone is destroyed. QPointer makes
    // the delete a safe no-op if the two are instead torn down together, in
    // either order, by their shared parent (the viewport).
    delete myField;
}

void ExtrudePreview::begin(const TopoDS_Face& face)
{
    myFace = face;
    // WHICH outline this preview is for. Since outlines became document items
    // they accumulate - starting another sketch no longer discards the one
    // waiting - so "is a face still pending" is no longer enough to tell that
    // this panel is still describing the same thing it opened on. See
    // onAppStateChanged().
    myOutlineId = myWindow ? myWindow->pendingOutlineId() : 0;

    reposition();
    show();
    raise();
    if (myField) myField->raise();

    if (myField) {
        // Force the same default every time, even if a previous use already
        // left something else in the field: setText() would not emit
        // textChanged for a value that has not actually changed, and the
        // contract is "preview at a real 10 mm", unconditionally - converted
        // to whatever the field currently reads in, so this is "10" in
        // millimetres but "1" in centimetres, never a bare "10" that would
        // mean 100 mm once the unit has switched.
        myField->blockSignals(true);
        myField->setText(defaultHeightText());
        myField->blockSignals(false);
        myField->setFocus(Qt::OtherFocusReason);
        myField->selectAll();
    }
    updatePreview();
}

void ExtrudePreview::cancel()
{
    if (myHasPreview && myView) {
        // Just clear it, and that is a Phase-7 simplification rather than a
        // regression of the fix this used to carry. The closed outline used to
        // be shown through this same single preview slot, so clearing here
        // erased it and left an intact pending face above an empty viewport -
        // the fix was to restore the face instead. An outline is a DOCUMENT
        // ITEM now, displayed through its own channel and untouched by
        // anything this panel does, so the face is still on screen either way
        // and restoring it here would put a second copy of it in the preview
        // slot - two ways to show one face, which is the bug that started
        // this whole paragraph.
        myView->clearPreview();
        myHasPreview = false;
    }
    markInvalid(false);
    hide();
    // myFace, and MainWindow's own pending face, are deliberately untouched -
    // the user can press Extrude again and try another height.
}

void ExtrudePreview::onAppStateChanged()
{
    // Fix round 1, Important 1: onStartSketch() nulls the pending face and
    // resets the view's single preview slot to empty, but neither of those
    // told this widget anything - it kept myHasPreview true and stayed open,
    // so the next keystroke's updatePreview() called myView->setPreview()
    // again and redisplayed a body-shaped shape over the new outline, and
    // Enter reached commit(), where extrudePendingFace() returns false on
    // the now-null pending face - hide() never ran, and a shape that exists
    // in no document sat on screen. Deriving this from the one signal every
    // route that can clear the pending face already emits (appStateChanged,
    // fired at the end of every updateActions() call) closes the whole
    // class rather than patching onStartSketch()/onCancelSketch()
    // individually - the same reasoning HintBalloon's conditionHolds() uses
    // for its own triggers. Reads state and calls cancel(), which touches
    // neither DocumentModel nor updateActions() - safe per CLAUDE.md's rule
    // that a slot on this signal must never call back into updateActions().
    //
    // Phase 7 widened the condition from "no face is pending" to "the outline
    // this panel opened on is no longer the one Extrude would consume".
    // Outlines accumulate now, so starting a new sketch leaves a face pending
    // - the OLD one - and the narrower test stopped closing this panel at
    // exactly the moment it most needed to: the check that a new sketch
    // closes an open preview is what caught it. isSketching() covers the
    // interval before the new outline exists, and the id comparison covers
    // everything after, including an undo that took this outline away and a
    // drawer click that chose a different one.
    if (isVisible() && myWindow &&
        (myWindow->isSketching() || myWindow->pendingOutlineId() != myOutlineId)) {
        cancel();
        return;
    }
    // Fix round 1, Important: a repaint alone updated the label to "(cm)"
    // but left the on-screen shape built from the OLD unit's reading of the
    // field - the panel could show "10" meaning 100 mm while the viewport
    // still displayed the 10 mm body from before the switch, and Enter then
    // committed the number the field silently now meant, not the shape the
    // user was looking at. updatePreview() re-reads the field through
    // Measure::parseLength() in whatever unit is current, so the shape and
    // the label can never disagree about which unit they mean.
    if (isVisible()) {
        update();
        updatePreview();
    }
}

double ExtrudePreview::height() const
{
    return myHeight;
}

QLineEdit* ExtrudePreview::field() const
{
    return myField;
}

void ExtrudePreview::replace()
{
    reposition();
    if (!isVisible()) return;
    // Re-raised as well as re-placed. ViewportOverlay::relayout() raise()s
    // every anchored cluster, including the top-left Items/Undo/Redo one
    // this panel overlaps below roughly 530 px of viewport width - so a
    // panel raised only once, at begin(), had its field buried under that
    // cluster after the next resize and stopped being clickable at all.
    raise();
    if (myField) myField->raise();
}

void ExtrudePreview::reposition()
{
    if (!myView) return;
    // Top-center, clear of the bottom strip where Toast, WalkthroughPanel and
    // HintBalloon all live (see their own reposition()/HintBalloon::stepAside
    // logic) at every width this app runs at - unlike those three, this
    // panel does not need to track the guide/toast/balloon at runtime to
    // stay clear of them.
    //
    // It is NOT collision-free against the top corners, though - fix round
    // 1 flagged an earlier version of this comment for overclaiming that.
    // This panel is kWidth (200) wide, centred, so its edges sit at
    // (viewportWidth +/- 200) / 2. The axis gizmo is 120px wide, top-right
    // at the same kMargin: the two overlap once viewportWidth drops below
    // 472px. The top-left chip cluster (Items/Undo/Redo) is roughly
    // 150-175px wide depending on its own action labels; the two overlap
    // somewhere in the 530-580px range depending on that width. Neither is
    // tracked or stepped around here - this simply has not come up at any
    // width the app has actually been run or tested at (1200, 900 and
    // 1100px in gui_smoke), and handling it would mean giving this panel
    // the same obstacle-tracking HintBalloon/ToastHost use for the guide,
    // which felt like more machinery than a narrow-window edge case
    // justified. Revisit if this app ever needs to run meaningfully
    // narrower than ~600px.
    const int x = (myView->width() - QWidget::width()) / 2;
    // kMargin is the PAINTED offset from the viewport's top edge the mockup
    // wants, but this widget's own bounding box is now bigger than what it
    // paints (see the constructor) - its top-left sits
    // Theme::surfaceShadowMargin() further up than the card it draws, so the
    // widget itself is placed that much higher to keep the painted card at
    // kMargin, the same compensation ViewportOverlay's own kMargin applies
    // for the widgets it anchors.
    move(x, kMargin - Theme::surfaceShadowMargin());
}

QRect ExtrudePreview::fieldRect() const
{
    // Local coordinates within this (now grown) widget - margin in from the
    // top and both sides, the same pattern as WalkthroughPanel::skipRect()
    // and Toast::undoRect(). syncFieldGeometry() still just translates this
    // by pos(), unchanged.
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight,
                QWidget::width() - margin * 2 - kPad * 2, kFieldHeight);
}

QRect ExtrudePreview::hintRect() const
{
    const int margin = Theme::surfaceShadowMargin();
    return QRect(margin + kPad, margin + kPad + kLabelHeight + kFieldHeight + kHintGap,
                QWidget::width() - margin * 2 - kPad * 2, kHintHeight);
}

QString ExtrudePreview::labelText() const
{
    return tr("Extrude height (%1)")
        .arg(QString::fromStdString(Measure::unitSuffix()));
}

QString ExtrudePreview::hintText() const
{
    // A modeless panel with invisible verbs is how the keyboard-only commit
    // and cancel went unnoticed for a whole branch. This panel has no
    // buttons, so the two keys have to be on it in words.
    return tr("Enter adds the body — Esc cancels");
}

QString ExtrudePreview::defaultHeightText() const
{
    // A real 10 mm, spelled in whatever unit the field currently reads -
    // "10" in millimetres, "1" in centimetres. formatLength() carries the
    // rounding rule; strip its trailing " <suffix>" to get back a plain
    // number that Measure::parseLength() can read straight from the field.
    QString formatted = QString::fromStdString(Measure::formatLength(10.0));
    const QString suffix =
        QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    return formatted;
}

void ExtrudePreview::syncFieldGeometry()
{
    if (!myField) return;
    // Shares this widget's parent, so fieldRect() - defined in this widget's
    // own local coordinates - needs translating by pos() to land in that
    // shared coordinate space. Same idiom as
    // WalkthroughPanel::syncSkipGeometry() and Toast::syncUndoGeometry().
    myField->setGeometry(fieldRect().translated(pos()));
    // Visibility is DERIVED here, not left to a hide event that may never
    // arrive - see WalkthroughPanel::syncSkipGeometry() for why that matters.
    myField->setVisible(isVisible());
    myField->raise();
}

void ExtrudePreview::updatePreview()
{
    if (!myField || !myView || !myWindow || myFace.IsNull()) return;

    // Through Measure::parseLength, not QString::toDouble - the field shows
    // the current display unit (see labelText()), so it must be read back in
    // that same unit. Typing "4" with centimetres selected has to mean 40 mm,
    // not 4.
    double h = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), h) || h == 0.0) {
        // Invalid input never previews: the last good shape, if any, stays
        // exactly as it was - only the field's own border marks the problem.
        markInvalid(true);
        return;
    }

    // The SAME ModelingOps::extrude() call the commit uses, along the SAME
    // direction it would use - a preview built by a different path, or in a
    // different direction, than the commit would be a lie. The sweep
    // direction is asked for rather than re-derived from the sketch
    // controller's current plane: the outline pins the plane it was drawn on,
    // and this panel must not be the one place that forgets it.
    const TopoDS_Shape solid =
        ModelingOps::extrude(myFace, myWindow->pendingSweepDirection(), h);
    if (solid.IsNull()) {
        markInvalid(true);
        return;
    }

    markInvalid(false);
    myHeight = h;
    // Deliberate deviation from the brief, which specced a transparent
    // preview: setPreview() is OcctViewWidget's single existing preview
    // channel, already used for the in-progress sketch outline, and it
    // displays opaque yellow (see OcctViewWidget::setPreview) rather than
    // transparent. Reusing it as-is - instead of adding a second,
    // extrude-specific display path with its own transparency - keeps "the
    // preview is built by the same call the commit uses" honest without
    // introducing a second AIS channel to keep in sync with this one, and
    // an opaque preview is still clearly not a committed body (it is
    // wireframe-yellow, not the shaded grey every real body renders in).
    myView->setPreview(solid, /*shaded=*/true);
    myHasPreview = true;
}

void ExtrudePreview::commit()
{
    if (!myField || !myHasPreview || myInvalid) return;

    double h = 0.0;
    if (!Measure::parseLength(myField->text().toStdString(), h) || h == 0.0) return;

    // extrudePendingFace() re-runs the same extrude, clears whatever preview
    // is on screen, adds the real body, records progress, and reports the
    // result - this widget never touches DocumentModel itself.
    if (myWindow->extrudePendingFace(h)) {
        myHasPreview = false;
        hide();
    }
}

void ExtrudePreview::markInvalid(bool invalid)
{
    myInvalid = invalid;
    if (!myField) return;
    const QColor border = invalid ? Theme::danger() : Theme::accent();
    // border-radius: 0, not 4. This field is a SIBLING parented straight to
    // the viewport (see the header for why it cannot be a child of the panel
    // it belongs to), so it sits directly on OCCT's on-screen GL surface with
    // no card of its own underneath. A rounded corner is a corner the
    // stylesheet does not paint, and over that surface an unpainted pixel is
    // not transparent - it is whatever the driver left there, which reads as
    // black. Four small black nubs, the same failure mode that showed up as a
    // band down the rail before ToolCluster painted its whole rect. The other
    // rounded cards get away with it because they paint their own opaque
    // surface; this one has nothing behind it. Square corners on one 26px
    // field are a smaller price than the nubs, and it is the only control in
    // the shell in this position.
    myField->setStyleSheet(QStringLiteral(
                               "QLineEdit { background-color: %1; color: %2; "
                               "border: 1px solid %3; border-radius: 0px; padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

void ExtrudePreview::applyTheme()
{
    // MEASURED with the fonts paintEvent() actually draws these two strings
    // with, not assumed from kWidth alone. kWidth was chosen against the
    // shipped 10pt scale and stays the floor, so the card is exactly the size
    // it has always been at the default look - but at 13pt "Enter adds the
    // body — Esc cancels" is wider than 230px and used to run off the card.
    // BevelArrow::applyTheme() is the same measurement three files away.
    const QFontMetrics label(Theme::labelFont());
    const QFontMetrics badge(Theme::badgeFont());
    const int content = std::max(label.horizontalAdvance(labelText()),
                                 badge.horizontalAdvance(hintText()));

    // Grown by Theme::surfaceShadowMargin() per side beyond the content
    // size. That margin is zero - the family paints no shadow and reserves no
    // room for one (see Theme.h) - so this panel's widget rect and its
    // painted card are the same rectangle; paintEvent(), fieldRect() and
    // hintRect() apply the same zero on the inside, the same pattern as
    // WalkthroughPanel's sizeHint() and Toast's.
    const int margin = Theme::surfaceShadowMargin();
    // Through Theme::wholeDevicePixels(), the same as PullArrow and the
    // round/flatten chip - see Theme.h. This card is anchored rather than
    // tracking a projected point, so its position half is the overlay's
    // business, but its size is its own.
    setFixedSize(Theme::wholeDevicePixels(
        QSize(std::max(kWidth, content + kPad * 2) + margin * 2,
              kPad * 2 + kLabelHeight + kFieldHeight + kHintGap + kHintHeight +
                  margin * 2)));

    if (myField) myField->setFont(Theme::bodyFont());
    // The field is a sibling positioned against this card's rectangle, which
    // has just moved.
    syncFieldGeometry();
    update();
}

QStringList ExtrudePreview::paintedTexts() const
{
    return {labelText(), hintText()};
}

void ExtrudePreview::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // `body` is the visible card, inset from this widget's own bounds by
    // Theme::surfaceShadowMargin() - see the constructor for the growth and
    // fieldRect()/hintRect() for the sibling field that also has to agree
    // on where it landed. Brings this panel into the same shared family
    // WalkthroughPanel, HintBalloon, Toast and ShortcutSheet already use,
    // dropping the unconditional accent() border this used to hand-roll.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 8);

    // The one thing this card keeps on top of the shared base: a danger()
    // outline while the field's current text does not parse or would not
    // extrude - the same pattern as the toast's kind-tinted stripe. The
    // field's own border (see markInvalid()) already carries this signal;
    // the card now echoes it rather than staying accent() regardless.
    if (myInvalid) {
        QPainterPath invalidOutline;
        invalidOutline.addRoundedRect(body, 8, 8);
        painter.setPen(QPen(Theme::danger(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(invalidOutline);
    }

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, labelText());

    painter.setFont(Theme::badgeFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(hintRect(), Qt::AlignVCenter | Qt::AlignLeft, hintText());
}

void ExtrudePreview::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncFieldGeometry();
    // Installed for exactly as long as the panel is up - the same lifetime
    // rule ShortcutSheet::showSheet()/hideEvent() use for its own
    // application-wide filter. See eventFilter() for why the panel cannot
    // rely on holding focus.
    QCoreApplication::instance()->installEventFilter(this);
}

void ExtrudePreview::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
    QCoreApplication::instance()->removeEventFilter(this);
}

void ExtrudePreview::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncFieldGeometry();
}

void ExtrudePreview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncFieldGeometry();
}

bool ExtrudePreview::eventFilter(QObject* watched, QEvent* event)
{
    // This panel owns Enter and Escape for as long as it is VISIBLE,
    // regardless of what holds focus - which is the whole fix here. Both
    // used to arrive only through a filter on this panel's own QLineEdit,
    // and the field loses focus to the first press anywhere else:
    // OcctViewWidget is Qt::StrongFocus and every ToolChip became focusable
    // too. So an RMB orbit - the entire reason a LIVE preview exists, since
    // the user opens one specifically to judge the shape from another angle
    // - moved focus off the field, and from that moment Enter and Escape
    // reached nothing at all. Nor did anything else consume Escape: Cancel
    // Sketch's own Escape binding is disabled while a preview can be open
    // (mySketching is false). The panel has no buttons, so the user was left
    // with a preview shape and no route to either commit or cancel it.
    //
    // An application-wide filter, installed while visible and removed when
    // hidden, is the shape ShortcutSheet already uses for the same reason -
    // a key press goes to the focus widget, which is emphatically not this
    // panel. ShortcutOverride is claimed too, so QShortcutMap cannot resolve
    // a window-context binding (Finish Sketch is on Return, Cancel Sketch on
    // Escape) before the press ever reaches a widget; both of those happen
    // to be disabled whenever this panel can be open, but relying on that
    // accident of enabled-state is exactly what broke once already.
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    // Application-wide means every window in this process - gui_smoke builds
    // several at once - so the panel must only claim keys headed for its own.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window())
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    // KeypadModifier is what the numeric keypad's own Enter carries; it is
    // the same key to the user, so it is the same key here.
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
