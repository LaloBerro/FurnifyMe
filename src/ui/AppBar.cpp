#include "AppBar.h"

#include "IconSet.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLayoutItem>
#include <QMenuBar>
#include <QPainter>

#include <algorithm>

namespace {

// The app mark's own fixed pixel size - small, beside the wordmark, the way
// the mockup's sketch shows it. Independent of the type scale: the mark is
// the user's own artwork (IconSet::appMarkPixmap(), Milestone 5 item 1),
// not text, so it does not grow or shrink with Theme::basePt() the way the
// wordmark beside it does.
constexpr int kMarkSize = 20;
// The one breathing-room gap used twice: between the mark and the wordmark,
// and between the wordmark and the menu bar that follows it.
constexpr int kGap = 8;

constexpr int kPadY = 6;        // the pill's own top/bottom padding
constexpr int kMinRadius = 12;  // a floor under updatePillMargins()'s derived value,
                                // so a still-empty layout (pre-first-paint) never
                                // collapses the pill to a near-zero-radius sliver

// The wordmark reads as one unit at a title-ish weight, the one place in the
// shell a bold string appears outside a panel heading - the same rule the
// window-spanning bar followed before this task, carried over unchanged.
QFont wordmarkFont()
{
    QFont f = Theme::bodyFont();
    f.setBold(true);
    return f;
}

}  // namespace

AppBar::AppBar(QMenuBar* menuBar, QWidget* parent)
    : QWidget(parent)
    , myMenus(menuBar)
{
    // This pill is one of Theme's floating-surface family now (it used to
    // paint its own flat chrome() strip across the whole window instead) -
    // see paintEvent(). Both calls are the family's own constructor pair:
    // WA_NoSystemBackground so this widget paints its own background, and
    // makeSurfaceTransparent() so the app-wide QSS rule cannot stamp an
    // opaque square over the corners paintSurface() leaves genuinely
    // unpainted.
    setAttribute(Qt::WA_NoSystemBackground);
    Theme::makeSurfaceTransparent(this);

    auto* row = new QHBoxLayout(this);
    // Left/right start at 0 here and are set for real by updatePillMargins()
    // below, once the menu bar is in the layout and a sizeHint exists to
    // derive a radius from - see that function's own comment for why margins
    // this wide cannot be a literal.
    row->setContentsMargins(0, kPadY, 0, kPadY);
    row->setSpacing(0);

    // The mark and the wordmark are painted, not child widgets (see the
    // header) - the layout only needs to keep their combined width clear, and
    // applyTheme() is what sizes that space, because it moves when the
    // wordmark's own font does.
    row->addSpacing(0);
    myMarkSpace = row->itemAt(row->count() - 1)->spacerItem();

    if (myMenus) {
        // Reparented in. Its own background and border come from the app
        // stylesheet's QMenuBar rule and would draw a rectangle behind the
        // menu titles alone, so they are cleared here; the item padding,
        // hover and popup rules in that same stylesheet still apply, because
        // a per-widget sheet merges with the application one rather than
        // replacing it.
        myMenus->setStyleSheet(QStringLiteral(
            "QMenuBar { background: transparent; border: none; padding: 0; }"));
        // Without this a QMenuBar expands to fill whatever space it is
        // given; Maximum keeps it sized to its own titles, and it may still
        // shrink in a narrow window.
        myMenus->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        row->addSpacing(kGap);
        row->addWidget(myMenus, 0, Qt::AlignVCenter);
    }

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &AppBar::applyTheme);
}

void AppBar::applyTheme()
{
    if (!myMarkSpace) return;
    // Measured with the font paintEvent() actually draws the wordmark in -
    // bold, and bold is wider than regular.
    const QFontMetrics metrics(wordmarkFont());
    myMarkSpace->changeSize(kMarkSize + kGap + metrics.horizontalAdvance(wordmark()), 0,
                            QSizePolicy::Fixed, QSizePolicy::Minimum);
    if (layout()) layout()->invalidate();
    // The menu bar's own row height moves with the type scale, which moves
    // this pill's height, which moves the radius its fully-rounded ends are
    // drawn at - recomputed here rather than once at construction.
    updatePillMargins();
    update();
}

void AppBar::updatePillMargins()
{
    auto* row = qobject_cast<QHBoxLayout*>(layout());
    if (!row) return;
    // The height a QHBoxLayout reports depends only on its top/bottom
    // margins and its tallest child's own sizeHint - never on left/right,
    // which is exactly what is being computed here. Asking for it BEFORE
    // the new left/right margins are set is therefore not a stale read; it
    // is the one order that avoids a circular layout pass altogether.
    const int contentHeight = row->sizeHint().height();
    const int radius = std::max(kMinRadius, contentHeight / 2);
    row->setContentsMargins(radius, kPadY, radius, kPadY);
}

QString AppBar::wordmark() const
{
    return QStringLiteral("FurnifyMe");
}

QStringList AppBar::paintedTexts() const
{
    return {wordmark()};
}

void AppBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Fully rounded ends - the mockup's own rule, radius = half the pill's
    // CURRENT height, read fresh on every paint rather than cached, so the
    // fill and the curve this widget draws can never disagree with
    // whatever updatePillMargins() derived its horizontal padding from.
    const int radius = height() / 2;
    Theme::paintSurface(painter, rect(), radius);

    // The mark and the wordmark, drawn starting at exactly `radius` from the
    // left edge - the same value the layout's own left margin uses (see
    // updatePillMargins()), and the widest point a true stadium shape's own
    // curve reaches at any vertical position, so neither the mark nor the
    // wordmark's first letter is ever clipped by it.
    const QPixmap mark = IconSet::appMarkPixmap(kMarkSize);
    const int markTop = (height() - mark.height()) / 2;
    painter.drawPixmap(radius, markTop, mark);

    const QFont font = wordmarkFont();
    const QFontMetrics metrics(font);
    painter.setFont(font);
    painter.setPen(Theme::text());
    const int baseline = rect().center().y() + metrics.ascent() / 2 - 1;
    painter.drawText(radius + mark.width() + kGap, baseline, wordmark());
}
