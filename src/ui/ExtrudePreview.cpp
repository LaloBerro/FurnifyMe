#include "ExtrudePreview.h"

#include "MainWindow.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"
#include "SketchController.h"
#include "Theme.h"

#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>

namespace {
constexpr int kPad = 12;
constexpr int kWidth = 200;
constexpr int kLabelHeight = 20;
constexpr int kFieldHeight = 26;
constexpr int kMargin = 16;   // matches ViewportOverlay's own edge margin
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
    setFixedSize(kWidth, kPad * 2 + kLabelHeight + kFieldHeight);

    myField = new QLineEdit(view);
    // Closes the same class of bug documented on HintBalloon's balloon and
    // Toast's UndoControl: an unhandled release would otherwise propagate to
    // the viewport behind this field and trigger a real pick underneath the
    // panel.
    myField->setAttribute(Qt::WA_NoMousePropagation);
    // The height the user types is body text, same as everything else they
    // read - set explicitly rather than left to inherit, since it is the one
    // widget on this panel the user actually types into.
    myField->setFont(Theme::bodyFont());
    myField->installEventFilter(this);   // catches Escape - see eventFilter()
    connect(myField, &QLineEdit::textChanged, this,
            [this](const QString&) { updatePreview(); });
    connect(myField, &QLineEdit::returnPressed, this, &ExtrudePreview::commit);
    markInvalid(false);   // paints the field's normal (valid) border once

    syncFieldGeometry();
    hide();

    // Repositions on a viewport resize, the same reason Toast and HintBalloon
    // each install this on their own parent.
    if (myView) myView->installEventFilter(this);

    // Ties this widget's own life to the pending face it was built from -
    // see onAppStateChanged() and the header for why a route this class does
    // not know about (onStartSketch(), onCancelSketch(), and any future one)
    // must not be trusted to remember to call cancel() individually.
    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this,
                          &ExtrudePreview::onAppStateChanged);
}

ExtrudePreview::~ExtrudePreview()
{
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

    reposition();
    show();
    raise();
    if (myField) myField->raise();

    if (myField) {
        // Force the same default every time, even if a previous use already
        // left "10" (or anything else) in the field: setText() would not
        // emit textChanged for a value that has not actually changed, and
        // the contract is "preview at 10 mm", unconditionally.
        myField->blockSignals(true);
        myField->setText(QStringLiteral("10"));
        myField->blockSignals(false);
        myField->setFocus(Qt::OtherFocusReason);
        myField->selectAll();
    }
    updatePreview();
}

void ExtrudePreview::cancel()
{
    if (myHasPreview && myView) {
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
    if (isVisible() && myWindow && !myWindow->hasPendingFace()) cancel();
}

double ExtrudePreview::height() const
{
    return myHeight;
}

QLineEdit* ExtrudePreview::field() const
{
    return myField;
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
    move(x, kMargin);
}

QRect ExtrudePreview::fieldRect() const
{
    return QRect(kPad, kPad + kLabelHeight, QWidget::width() - kPad * 2, kFieldHeight);
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

    bool ok = false;
    const double h = myField->text().toDouble(&ok);
    if (!ok || h == 0.0) {
        // Invalid input never previews: the last good shape, if any, stays
        // exactly as it was - only the field's own border marks the problem.
        markInvalid(true);
        return;
    }

    // The SAME ModelingOps::extrude() call the commit uses - a preview built
    // by a different path than the commit would be a lie.
    const TopoDS_Shape solid =
        ModelingOps::extrude(myFace, myWindow->sketch().plane().Axis().Direction(), h);
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

    bool ok = false;
    const double h = myField->text().toDouble(&ok);
    if (!ok || h == 0.0) return;

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
    myField->setStyleSheet(QStringLiteral(
                               "QLineEdit { background-color: %1; color: %2; "
                               "border: 1px solid %3; border-radius: 4px; padding: 2px 6px; }")
                               .arg(Theme::chip().name(), Theme::text().name(), border.name()));
}

QStringList ExtrudePreview::paintedTexts() const
{
    return {tr("Extrude height (mm)")};
}

void ExtrudePreview::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8.0, 8.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::accent(), 1.0));
    painter.drawPath(panel);

    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, kPad, QWidget::width() - kPad * 2, kLabelHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, paintedTexts().front());
}

void ExtrudePreview::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncFieldGeometry();
}

void ExtrudePreview::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myField) myField->hide();
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
    // Fix round 1, Minor 2: QShortcutMap resolves an enabled window-context
    // shortcut BEFORE a key press ever reaches the focused widget - myField
    // only saw Escape as an ordinary KeyPress below because Cancel Sketch's
    // own Escape binding happens to be disabled whenever this panel can be
    // open (mySketching is false). That was an accident of the two actions'
    // current enabled-state, not a real guarantee, and would silently break
    // the moment anything else claims Escape while extruding. ShortcutOverride
    // is the mechanism Qt gives a widget to claim a key back from the map -
    // ShortcutSheet::event() does this directly since it IS the focused
    // widget; myField is a plain QLineEdit this class does not subclass, so
    // the same claim has to happen through this eventFilter instead, on the
    // ShortcutOverride event Qt sends to the focus widget first.
    if (watched == myField && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (isVisible() && keyEvent->key() == Qt::Key_Escape &&
            keyEvent->modifiers() == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    if (watched == myField && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            cancel();
            // Swallowed here, not left to bubble: MainWindow's own Cancel
            // Sketch action is also bound to Escape and would otherwise null
            // the pending face too, contradicting "Escape cancels [this] and
            // leaves the pending face intact". The ShortcutOverride branch
            // above is what actually guarantees this KeyPress arrives at
            // all, regardless of what else is bound to Escape.
            return true;
        }
    }
    if (watched == myView && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}
