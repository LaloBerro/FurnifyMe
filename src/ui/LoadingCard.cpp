#include "LoadingCard.h"

#include "IconSet.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>

namespace {
// The close question's own card metrics, so the two read as one family.
constexpr int kCardWidth = 380;
constexpr int kPad = 20;
constexpr int kMarkPx = 28;
constexpr int kGap = 12;
constexpr int kBarHeight = 6;
constexpr int kCardRadius = 14;
constexpr int kViewportGutter = 16;
constexpr int kScrimAlpha = 204;

QFont titleFontBold()
{
    QFont f = Theme::titleFont();
    f.setBold(true);
    return f;
}
}  // namespace

LoadingCard::LoadingCard(QWidget* viewport)
    : QWidget(viewport)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    setMouseTracking(true);
    Theme::makeSurfaceTransparent(this);
    setFocusPolicy(Qt::NoFocus);
    if (viewport) viewport->installEventFilter(this);
    hide();
}

void LoadingCard::begin(const QString& title, const QString& step)
{
    myTitle = title;
    myStep = step;
    myDone = 0;
    myTotal = 0;
    replace();
    show();
    raise();
    // Painted before the caller starts blocking: the whole point of this card
    // is to be on screen for the seconds that follow, and nothing after this
    // call returns to the event loop on its own.
    pump();
}

void LoadingCard::setTotal(int total)
{
    myTotal = std::max(0, total);
    myDone = 0;
    update();
    pump();
}

void LoadingCard::step(const QString& stepText)
{
    if (isHidden()) return;
    ++myDone;
    if (!stepText.isEmpty()) myStep = stepText;
    update();
    pump();
}

void LoadingCard::end()
{
    myDone = 0;
    myTotal = 0;
    hide();
}

void LoadingCard::pump()
{
    // INPUT EXCLUDED, and that is the load-bearing half - see the header. The
    // repaint reaches the screen; a click, a key or a wheel event does not
    // reach a document that is half-way through being replaced.
    if (QCoreApplication::instance())
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void LoadingCard::replace()
{
    if (!parentWidget()) return;
    setGeometry(parentWidget()->rect());
    layoutCard();
    if (!isHidden()) raise();
}

void LoadingCard::layoutCard()
{
    const int available = std::max(kPad * 4, width() - kViewportGutter * 2);
    const int cardWidth = Theme::wholeDevicePixels(std::min(kCardWidth, available));
    const QFontMetrics title(titleFontBold());
    const QFontMetrics body(Theme::bodyFont());
    const int cardHeight = Theme::wholeDevicePixels(kPad + std::max(kMarkPx, title.height()) +
                                                    kGap + kBarHeight + kGap + body.height() +
                                                    kPad);

    const QPoint origin = mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    const int x = Theme::snapToDevicePixels((width() - cardWidth) / 2, origin.x(), dpr);
    const int y = Theme::snapToDevicePixels(
        std::max(kViewportGutter, static_cast<int>(height() * 0.46) - cardHeight / 2), origin.y(),
        dpr);
    myCard = QRect(x, y, cardWidth, cardHeight);
}

QStringList LoadingCard::paintedTexts() const
{
    // The step lines this card can paint - its own copy. myTitle is the
    // furniture's name, which is the user's word, and is deliberately absent.
    return {tr("Reading the file"), tr("Putting the bodies on screen")};
}

void LoadingCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor wash = Theme::viewport();
    wash.setAlpha(kScrimAlpha);
    painter.fillRect(rect(), wash);

    Theme::paintSurface(painter, myCard, kCardRadius);

    // The app's own mark, from the cache - a paint event may not decode or
    // rescale an asset (CLAUDE.md's per-frame budget), and appMarkPixmap() is
    // QPixmapCache-backed for exactly that reason.
    const int markTop = myCard.top() + kPad;
    painter.drawPixmap(QRect(myCard.left() + kPad, markTop, kMarkPx, kMarkPx),
                       IconSet::appMarkPixmap(kMarkPx));

    const int textLeft = myCard.left() + kPad + kMarkPx + kGap;
    const QFontMetrics title(titleFontBold());
    painter.setFont(titleFontBold());
    painter.setPen(Theme::text());
    painter.drawText(QRect(textLeft, markTop, myCard.right() + 1 - kPad - textLeft, kMarkPx),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     title.elidedText(myTitle, Qt::ElideMiddle,
                                      myCard.right() + 1 - kPad - textLeft));

    // The bar: a track in chip(), filled in accent() to whatever fraction has
    // arrived. With no total yet - the file is still being read - the fill is
    // a short bar at the left rather than a sweep, since nothing here runs an
    // animation while the thread is busy.
    const int barTop = markTop + std::max(kMarkPx, title.height()) + kGap;
    const QRect track(myCard.left() + kPad, barTop, myCard.width() - kPad * 2, kBarHeight);
    QPainterPath trackPath;
    trackPath.addRoundedRect(track, kBarHeight / 2.0, kBarHeight / 2.0);
    painter.fillPath(trackPath, Theme::chip());

    const double fraction =
        myTotal > 0 ? std::clamp(static_cast<double>(myDone) / myTotal, 0.0, 1.0) : 0.12;
    QRect fill(track.left(), track.top(), static_cast<int>(track.width() * fraction),
               track.height());
    if (fill.width() >= kBarHeight) {
        QPainterPath fillPath;
        fillPath.addRoundedRect(fill, kBarHeight / 2.0, kBarHeight / 2.0);
        painter.fillPath(fillPath, Theme::accent());
    }

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::textMuted());
    const QFontMetrics body(Theme::bodyFont());
    painter.drawText(QRect(myCard.left() + kPad, track.bottom() + 1 + kGap,
                           myCard.width() - kPad * 2, body.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, myStep);
}

bool LoadingCard::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && !isHidden()) replace();
    return QWidget::eventFilter(watched, event);
}

// Every mouse event that reaches the scrim is swallowed: nothing behind a
// load in progress may be picked, hovered, orbited or dragged.
void LoadingCard::mousePressEvent(QMouseEvent* event) { event->accept(); }
void LoadingCard::mouseReleaseEvent(QMouseEvent* event) { event->accept(); }
void LoadingCard::mouseDoubleClickEvent(QMouseEvent* event) { event->accept(); }
void LoadingCard::mouseMoveEvent(QMouseEvent* event) { event->accept(); }
void LoadingCard::wheelEvent(QWheelEvent* event) { event->accept(); }
