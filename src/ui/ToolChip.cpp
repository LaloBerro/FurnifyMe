#include "ToolChip.h"

#include "Theme.h"

#include <QAction>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {
constexpr int kIcon = 16;
constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kGap = 8;
constexpr int kRadius = 6;
constexpr double kFocusRingWidth = 2.0;
// The IconOnly body, per the plan: a 34x34 square of painted card. The glyph
// inside it stays kIcon, the same 16px every other chip and bar button draws -
// IconSet renders exact pixmaps at 16/24/32/48, so 16 is a crisp rendering
// rather than a scaled one, and the labelled chip's own body is 32px tall
// around the same glyph, so the two read as the same control at two widths.
constexpr int kIconOnlySide = 34;
}  // namespace

ToolChip::ToolChip(QAction* action, IconSet::Glyph glyph, ChipMode mode, QWidget* parent)
    : QAbstractButton(parent)
    , myAction(action)
    , myMode(mode)
    , myGlyph(glyph)
{
    if (myMode == ChipMode::IconOnly) {
        // The rail stretches to the viewport's full height and distributes
        // the slack through one stretch item; a chip that let a QVBoxLayout
        // squeeze or grow it would turn that slack into fourteen slightly
        // different buttons instead. Fixed in both directions, so the square
        // is a square whatever the container does.
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    // Every chip is keyboard-reachable, not just clickable - a focus ring
    // that can never actually receive focus would be dead code.
    setFocusPolicy(Qt::StrongFocus);
    // The icon and the font-size stylesheet are the only two appearance
    // values this widget cannot re-derive inside paintEvent(), so they are
    // set through the same applyTheme() a live Appearance edit calls rather
    // than written out once here and again there.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ToolChip::applyTheme);

    if (myAction) {
        connect(this, &QAbstractButton::clicked, myAction, &QAction::trigger);
        connect(myAction, &QAction::changed, this, &ToolChip::syncFromAction);
        syncFromAction();
    }
}

void ToolChip::applyTheme()
{
    // Rasterised from Theme::text()/textDisabled() at this moment - a QIcon
    // is pixels, not a description, so it is stale the instant either colour
    // moves.
    setIcon(IconSet::icon(myGlyph));
    // Baseline for this widget's own font() (what the sweep in gui_smoke
    // checks): the chip's own text is a chip label. A per-widget stylesheet
    // wins over the app-wide one regardless of selector specificity, so this
    // sticks reliably rather than fighting the cascade.
    //
    // `background: transparent` is the corner fix (Milestone 5, item 2): the
    // app-wide `QWidget { background-color: @chrome }` rule stamps every
    // styled widget's FULL RECT before paintEvent() runs, so each chip wore
    // an opaque chrome square behind its rounded card - visibly darker than
    // the panel-coloured cluster card it sits on. A chip is always a child
    // of an already-opaquely-painted card (the rail cluster, the bar), never
    // a direct child of the GL surface, so letting the parent's paint show
    // through the corners breaks no law - the opaque-family rule governs
    // what touches OCCT's surface, and the parent still does that part.
    setStyleSheet(QStringLiteral("background: transparent; font-size: %1pt;")
                      .arg(Theme::labelFont().pointSizeF()));
    // A labelled chip's width is measured with labelFont()/badgeFont(), so a
    // base-size change moves it - the rail's icon-only chips are a fixed
    // square and are unaffected, but sizeHint() is one function for both.
    updateGeometry();
    update();
}

void ToolChip::syncFromAction()
{
    const QString label = myAction->text().remove(QLatin1Char('&'));
    setText(label);
    setEnabled(myAction->isEnabled());
    setCheckable(myAction->isCheckable());
    setChecked(myAction->isChecked());

    QString tip = myAction->toolTip();
    if (myMode == ChipMode::IconOnly) {
        // An icon-only button paints no name, so the tooltip has to carry
        // one. It is COMPOSED from the action's own two strings - its label
        // and its existing tooltip, which is already where the shortcut
        // lives - rather than written here: no new copy enters the app, and
        // the vocabulary sweep still sees words it has always seen. The
        // startsWith guard is what stops "Undo" being followed by "Undo the
        // last change to your bodies (Ctrl+Z)"; several actions already open
        // with their own name and do not need it twice.
        if (tip.isEmpty())              tip = label;
        else if (!tip.startsWith(label)) tip = label + QLatin1Char('\n') + tip;
    }
    setToolTip(tip);

    myShortcut = myAction->shortcut().toString(QKeySequence::NativeText);
    updateGeometry();
    update();
}

QSize ToolChip::sizeHint() const
{
    // Grown by Theme::surfaceShadowMargin() per side in both modes. That is
    // zero - the family paints no shadow and reserves no room for one (see
    // Theme.h) - so a chip's widget rect and its painted card coincide. The
    // arithmetic stays rather than being folded away: it is the same
    // compensation ToolCluster, AppBar and ViewportOverlay express, and
    // collapsing it in one place would leave four call sites disagreeing
    // about whether the scheme exists.
    const int margin = Theme::surfaceShadowMargin();
    if (myMode == ChipMode::IconOnly) {
        // No text is measured because none is painted: the label and the
        // shortcut are in the tooltip.
        return QSize(kIconOnlySide + margin * 2, kIconOnlySide + margin * 2);
    }

    // Measured with the same fonts paintEvent() actually draws with below -
    // the label at labelFont(), the shortcut badge at badgeFont().
    const QFontMetrics labelMetrics(Theme::labelFont());
    const QFontMetrics badgeMetrics(Theme::badgeFont());
    int width = kPadX + kIcon + kGap + labelMetrics.horizontalAdvance(text()) + kPadX;
    if (!myShortcut.isEmpty()) width += kGap + badgeMetrics.horizontalAdvance(myShortcut) + 8;
    return QSize(width + margin * 2, kIcon + kPadY * 2 + margin * 2);
}

void ToolChip::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // `body` is the visible card, inset from this widget's own bounds by
    // Theme::surfaceShadowMargin() - the margin sizeHint() reserved above,
    // which is zero, so the two are the same rectangle today. Every rect
    // below is body-relative rather than rect()-relative anyway, so should
    // that margin ever return, everything painted stays on the card while
    // hit-testing the margin still lands on the chip.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);

    // No Theme::paintSurface() call here, deliberately. It fills `body` with
    // panel() and strokes border() around it - and the two statements below
    // do exactly that again: the fill in this chip's own state colour over an
    // identical rounded path, the border through the identical crisp-border
    // idiom. Calling it first painted a panel() rectangle not one pixel of
    // which survived. A chip belongs to the floating-surface family by
    // wearing the family's fill, border and radius, not by routing through a
    // call whose every effect it overwrites.

    QColor background = Theme::chip();
    if (!isEnabled())        background = Theme::chip().darker(115);
    else if (isChecked())    background = Theme::chipActive();
    else if (isDown())       background = Theme::chipActive();
    else if (myHovered)      background = Theme::chipHover();

    QPainterPath path;
    path.addRoundedRect(body, kRadius, kRadius);
    painter.fillPath(path, background);
    // border(), always - not just when checked - at the user's own width
    // (Theme::chipStrokePx(), an Appearance token since the user asked for
    // it; 1px is the shipped default). Through Theme's one crisp-border
    // idiom: stroked on the integer path this used to use, a chip's border
    // painted two columns at half intensity, which was invisible until it
    // sat inside the rail's own crisp card. A width of 0 draws nothing at
    // all - the hover/pressed/checked fills still carry the states.
    const double stroke = Theme::chipStrokePx();
    if (stroke > 0.05)
        Theme::drawCrispBorder(painter, QRectF(body), Theme::border(), kRadius, stroke);

    if (isChecked() && stroke > 0.05) {
        // A second, inset ring - not a replacement for the border above.
        // Checked reads as "bordered, plus marked", not "a differently
        // coloured border instead of the usual one". Inset past the border's
        // own width so the two rings stay two rings at every stroke setting
        // rather than overlapping into one thick smear.
        const double inset = stroke + 1.0;
        Theme::drawCrispBorder(painter,
                               QRectF(body).adjusted(inset, inset, -inset, -inset),
                               Theme::accent(), std::max(0.0, kRadius - inset), stroke);
    }

    // The glyph. Centred in the body when there is nothing beside it,
    // left-padded when a label follows. It is the SAME QIcon and the same
    // Normal/Disabled mode selection in both modes, so an icon-only chip's
    // disabled state dims exactly as a labelled one's does - there is no
    // second dimming rule to keep in step.
    const QRect iconRect(myMode == ChipMode::IconOnly
                             ? body.left() + (body.width() - kIcon) / 2
                             : body.left() + kPadX,
                         body.top() + (body.height() - kIcon) / 2, kIcon, kIcon);
    icon().paint(&painter, iconRect, Qt::AlignCenter,
                 isEnabled() ? QIcon::Normal : QIcon::Disabled);

    // An icon-only chip stops here: its label and shortcut are in the
    // tooltip syncFromAction() composed, not on the card.
    if (myMode == ChipMode::Labelled) {
        painter.setFont(Theme::labelFont());
        painter.setPen(isEnabled() ? Theme::text() : Theme::textDisabled());
        const int textLeft = body.left() + kPadX + kIcon + kGap;
        painter.drawText(
            QRect(textLeft, body.top(), body.right() - kPadX - textLeft + 1, body.height()),
            Qt::AlignVCenter | Qt::AlignLeft, text());

        if (!myShortcut.isEmpty()) {
            painter.setFont(Theme::badgeFont());
            // Dims with the label and the glyph when disabled - the three read
            // as one unit going dark together, not two dimming while the badge
            // stays lit.
            painter.setPen(isEnabled() ? Theme::textMuted() : Theme::textDisabled());
            painter.drawText(QRect(body.left(), body.top(), body.width() - kPadX, body.height()),
                             Qt::AlignVCenter | Qt::AlignRight, myShortcut);
        }
    }

    // Keyboard focus must be visible - a focus state nobody can see is an
    // accessibility defect, not a polish item. Drawn last, inset further
    // than the checked-state ring above (4px vs. 2px, both from `body`)
    // rather than traced over it: a checked AND focused chip must show both
    // rings, not just one overdrawing the other.
    //
    // Deliberately window()->focusWidget() rather than hasFocus(): hasFocus()
    // (and QApplication::focusWidget()) answer for the whole application, and
    // stay false for every widget in a window that is not the OS-active one -
    // which this app's own test harness relies on (gui_smoke drives real
    // MainWindows shown with WA_ShowWithoutActivating specifically so it
    // never steals OS focus while it runs). setFocus() still updates this
    // top-level's own remembered focus child immediately, regardless of
    // activation, and that is what a chip's ring should reflect.
    //
    // Presence does not depend on window()->isActiveWindow() - only colour
    // and weight do. An earlier version added "&& isActiveWindow()" to the
    // condition above, which fixes the wrong problem: it makes the ring
    // disappear entirely (still correct for a user who moved to another
    // application) but also disappears while gui_smoke's own window, which
    // is never OS-activated by design, is the one under test - breaking the
    // very check this exists to satisfy. Painting a dimmer ring instead
    // solves both: it stops shouting focus at someone who has moved on
    // without ever going fully invisible.
    if (this == window()->focusWidget()) {
        const bool active = window()->isActiveWindow();
        const QColor ringColor = active ? Theme::focusRing() : Theme::focusRingMuted();
        const double ringWidth = active ? kFocusRingWidth : kFocusRingWidth - 0.5;
        Theme::drawCrispBorder(painter, QRectF(body).adjusted(4, 4, -4, -4),
                               ringColor, kRadius - 4, ringWidth);
    }
}

void ToolChip::enterEvent(QEnterEvent* /*event*/) { myHovered = true;  update(); }
void ToolChip::leaveEvent(QEvent* /*event*/)      { myHovered = false; update(); }

void ToolChip::focusInEvent(QFocusEvent* event)
{
    QAbstractButton::focusInEvent(event);
    update();
}

void ToolChip::focusOutEvent(QFocusEvent* event)
{
    QAbstractButton::focusOutEvent(event);
    update();
}
