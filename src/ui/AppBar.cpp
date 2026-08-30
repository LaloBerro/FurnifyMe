#include "AppBar.h"

// For viewLabelNames() alone - the seven strings the view button reserves its
// width against, read from the one place that produces them rather than
// copied here.
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QAction>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {

constexpr int kPadX = 12;      // inside a bar button, left and right of its label
constexpr int kPadY = 6;
constexpr int kRadius = 6;
constexpr double kFocusRingWidth = 2.0;

// Room around the wordmark: the text sits kEdgeX from the bar's left edge and
// the menu bar starts kWordmarkGap after it.
constexpr int kEdgeX = 14;
constexpr int kWordmarkGap = 20;
constexpr int kButtonGap = 8;

// The wordmark is the one place in the shell a title-weight string appears
// outside a panel heading, and it reads as one unit: the glyph in accent(),
// the name in text(), both at bodyFont() bold so the bar stays a strip rather
// than a banner.
QFont wordmarkFont()
{
    QFont f = Theme::bodyFont();
    f.setBold(true);
    return f;
}

}  // namespace

// --- BarButton ---------------------------------------------------------------

BarButton::BarButton(QAction* action, QWidget* parent)
    : QAbstractButton(parent)
    , myAction(action)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    // Keyboard-reachable like every chip, and for the same reason: a focus
    // ring that can never receive focus would be dead code.
    setFocusPolicy(Qt::StrongFocus);
    // Baseline for this widget's own font() (what the type-scale sweep in
    // gui_smoke reads): a bar button's text is a chip label. A per-widget
    // stylesheet wins over the app-wide one regardless of selector
    // specificity, so this sticks rather than fighting the cascade.
    setStyleSheet(QStringLiteral("font-size: %1pt;").arg(Theme::labelFont().pointSizeF()));

    if (myAction) {
        connect(this, &QAbstractButton::clicked, myAction, &QAction::trigger);
        connect(myAction, &QAction::changed, this, &BarButton::syncFromAction);
        syncFromAction();
    }
}

void BarButton::syncFromAction()
{
    setText(myAction->text().remove(QLatin1Char('&')));
    setEnabled(myAction->isEnabled());
    setCheckable(myAction->isCheckable());
    setChecked(myAction->isChecked());
    setToolTip(myAction->toolTip());
    updateGeometry();
    update();
}

void BarButton::reserveWidthFor(const QStringList& candidates)
{
    const QFontMetrics metrics(Theme::labelFont());
    int widest = 0;
    for (const QString& candidate : candidates)
        widest = std::max(widest, metrics.horizontalAdvance(candidate));
    myReservedTextWidth = widest;
    updateGeometry();
}

QSize BarButton::sizeHint() const
{
    // Measured with the font paintEvent() actually draws with, not the
    // widget's inherited one - bold is wider than regular and a title
    // measured in the wrong weight clips.
    const QFontMetrics metrics(Theme::labelFont());
    const int textWidth = std::max(metrics.horizontalAdvance(text()), myReservedTextWidth);
    // Grown by Theme::surfaceShadowMargin() per side: the visible card is
    // inset by that much so paintSurface()'s shadow has somewhere to go
    // without painting past this widget's own bounds.
    const int margin = Theme::surfaceShadowMargin();
    return QSize(textWidth + kPadX * 2 + margin * 2,
                 metrics.height() + kPadY * 2 + margin * 2);
}

void BarButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);

    // The shared floating-surface routine paints the shadow and a panel()
    // fill/border() stroke; the fill is overpainted below with this button's
    // own state colour, so only the shadow is what this call is for.
    Theme::paintSurface(painter, body, kRadius);

    QColor background = Theme::chip();
    if (!isEnabled())        background = Theme::chip().darker(115);
    else if (isChecked())    background = Theme::chipActive();
    else if (isDown())       background = Theme::chipActive();
    else if (myHovered)      background = Theme::chipHover();

    QPainterPath path;
    path.addRoundedRect(body, kRadius, kRadius);
    painter.fillPath(path, background);
    // 1px border(), always. The fill above covers half the stroke
    // paintSurface() drew (a stroke straddles its path), so it is redrawn
    // here rather than trusted to survive underneath.
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    if (isChecked()) {
        // A second, inset ring - not a replacement for the border above.
        // Checked reads as "bordered, plus marked".
        QPainterPath ring;
        ring.addRoundedRect(body.adjusted(2, 2, -2, -2), kRadius - 1, kRadius - 1);
        painter.setPen(QPen(Theme::accent(), 1.0));
        painter.drawPath(ring);
    }

    painter.setFont(Theme::labelFont());
    painter.setPen(isEnabled() ? Theme::text() : Theme::textDisabled());
    painter.drawText(body, Qt::AlignCenter, text());

    // window()->focusWidget() rather than hasFocus(), for the reason recorded
    // at length in ToolChip::paintEvent(): hasFocus() stays false for every
    // widget in a window that is not the OS-active one, and this suite never
    // activates its windows.
    if (window() && this == window()->focusWidget()) {
        const bool active = window()->isActiveWindow();
        QPainterPath ring;
        ring.addRoundedRect(body.adjusted(3, 3, -3, -3), kRadius - 2, kRadius - 2);
        painter.setPen(QPen(active ? Theme::focusRing() : Theme::focusRingMuted(),
                            active ? kFocusRingWidth : kFocusRingWidth - 0.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(ring);
    }
}

void BarButton::enterEvent(QEnterEvent* /*event*/) { myHovered = true;  update(); }
void BarButton::leaveEvent(QEvent* /*event*/)      { myHovered = false; update(); }

// --- AppBar ------------------------------------------------------------------

AppBar::AppBar(QMenuBar* menuBar, QAction* wireframe, QAction* fitAll, QWidget* parent)
    : QWidget(parent)
    , myMenus(menuBar)
{
    // The app stylesheet paints a QWidget's background only for widgets that
    // ask for a styled background; this one paints its own chrome and its own
    // bottom rule in paintEvent() instead, the same way every other
    // custom-painted widget in the shell does.
    setAttribute(Qt::WA_NoSystemBackground);

    auto* row = new QHBoxLayout(this);
    // The bar's right-hand controls each reserve surfaceShadowMargin() px of
    // their own for the painted shadow, so the layout's margins and spacing
    // are reduced by that much - what should measure 14px and 8px is the gap
    // between PAINTED edges, not between widget rects. Same compensation
    // ToolCluster and ViewportOverlay make.
    const int margin = Theme::surfaceShadowMargin();
    row->setContentsMargins(kEdgeX, 4 - margin > 0 ? 4 - margin : 0,
                            kEdgeX - margin, 4 - margin > 0 ? 4 - margin : 0);
    row->setSpacing(kButtonGap - margin * 2);

    // The wordmark is painted, not a child widget, so the layout only has to
    // keep its space clear.
    const QFontMetrics wordmarkMetrics(wordmarkFont());
    row->addSpacing(wordmarkMetrics.horizontalAdvance(wordmark()) + kWordmarkGap);

    if (myMenus) {
        // Reparented in. Its own bottom border and background come from the
        // app stylesheet's QMenuBar rule and would draw a rule under the
        // menus alone, so they are cleared here; the item padding, hover and
        // popup rules in that same stylesheet still apply, because a
        // per-widget sheet is merged with the application one rather than
        // replacing it.
        myMenus->setStyleSheet(QStringLiteral(
            "QMenuBar { background: transparent; border: none; padding: 0; }"));
        // Without this a QMenuBar expands and eats the stretch below.
        // Maximum, not Fixed: it may still shrink in a narrow window.
        myMenus->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        row->addWidget(myMenus, 0, Qt::AlignVCenter);
    }

    row->addStretch(1);

    myViewLabel = new BarButton(nullptr, this);
    // Every string viewLabelText() can produce, asked for rather than
    // repeated, so the button never resizes as the camera turns and a named
    // view added later cannot leave a stale second list here.
    const QStringList& viewNames = OcctViewWidget::viewLabelNames();
    myViewLabel->setText(viewNames.first());
    myViewLabel->reserveWidthFor(viewNames);
    myViewLabel->setToolTip(tr("Which way the camera is looking — click to go "
                               "back to the angled view"));
    connect(myViewLabel, &QAbstractButton::clicked, this, &AppBar::viewLabelClicked);
    row->addWidget(myViewLabel);

    myUnit = new BarButton(nullptr, this);
    myUnit->setText(QStringLiteral("mm"));
    myUnit->reserveWidthFor({QStringLiteral("mm"), QStringLiteral("cm")});
    myUnit->setToolTip(tr("The unit every length is shown and typed in — click "
                          "to swap between millimetres and centimetres"));
    connect(myUnit, &QAbstractButton::clicked, this, &AppBar::unitClicked);
    row->addWidget(myUnit);

    myWireframe = new BarButton(wireframe, this);
    row->addWidget(myWireframe);

    myFit = new BarButton(fitAll, this);
    row->addWidget(myFit);
}

QString AppBar::wordmark() const
{
    return QStringLiteral("▰ FurnifyMe");
}

void AppBar::setViewLabel(const QString& text)
{
    // cameraChanged fires on every frame of an orbit, so this runs per frame
    // and almost always with the string already showing. The guard is ours,
    // deliberately: QAbstractButton::setText happens to early-out on an equal
    // string today, but relying on that leaves a repaint-per-frame one Qt
    // release away, with nothing here saying it ever mattered.
    if (!myViewLabel || myViewLabel->text() == text) return;
    myViewLabel->setText(text);
}

void AppBar::setUnitLabel(const QString& text)
{
    // Same guard, same reason: this hangs off appStateChanged, which fires at
    // the end of every updateActions().
    if (!myUnit || myUnit->text() == text) return;
    myUnit->setText(text);
}

QWidget* AppBar::viewLabelButton() const { return myViewLabel; }
QWidget* AppBar::unitButton() const { return myUnit; }

QStringList AppBar::paintedTexts() const
{
    QStringList texts{wordmark()};
    for (const BarButton* button : {myViewLabel, myUnit, myWireframe, myFit}) {
        if (button) texts << button->text();
    }
    return texts;
}

void AppBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), Theme::chrome());
    // The rule that used to belong to the QMenuBar, drawn across the whole
    // bar rather than under the menus alone.
    //
    // At height() - 0.5, not rect().bottomLeft(). An antialiased 1px pen is
    // centred on the coordinate it is given, so an integer y splits it across
    // two rows at half intensity each - the rule read as a soft #2b2b2f smudge
    // rather than one crisp row of border(). Half-pixel centres fill exactly
    // one row. Caught in a magnified crop, which is what they are for.
    const double ruleY = height() - 0.5;
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.drawLine(QPointF(0.0, ruleY), QPointF(width(), ruleY));

    // "<glyph> FurnifyMe" as one string, drawn in two runs so only the glyph
    // takes accent(). Splitting on the first space keeps the two runs and
    // wordmark() the same text; measuring with the font it is drawn with is
    // what keeps the layout's reserved space honest.
    const QFont font = wordmarkFont();
    const QFontMetrics metrics(font);
    const QString mark = wordmark();
    const int split = mark.indexOf(QLatin1Char(' '));
    const QString glyph = split < 0 ? mark : mark.left(split + 1);
    const QString name = split < 0 ? QString() : mark.mid(split + 1);

    painter.setFont(font);
    const int baseline = rect().center().y() + metrics.ascent() / 2 - 1;
    painter.setPen(Theme::accent());
    painter.drawText(kEdgeX, baseline, glyph);
    painter.setPen(Theme::text());
    painter.drawText(kEdgeX + metrics.horizontalAdvance(glyph), baseline, name);
}
