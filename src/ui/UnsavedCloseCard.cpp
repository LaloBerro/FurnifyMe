#include "UnsavedCloseCard.h"

#include "Theme.h"

#include <QCoreApplication>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

namespace {

// The mockup's own card: 380 wide, 20 padding, 12 between the title, the body
// and the buttons, buttons 34-36 tall with a 7-8 gap. The sizes that become
// WIDGET extents (the card width and every button) are multiples of four so
// they are whole device pixels at every Windows scale without further
// rounding - Theme::wholeDevicePixels() is still applied, as the rule asks.
constexpr int kCardWidth = 380;
constexpr int kPad = 20;
constexpr int kBottomPad = 16;
constexpr int kGap = 12;
constexpr int kButtonHeight = 36;
constexpr int kButtonGap = 8;
constexpr int kCardRadius = 14;
constexpr int kButtonRadius = 8;
constexpr int kButtonTextInset = 12;
// The side gutter the card keeps inside a viewport narrower than the card.
constexpr int kViewportGutter = 16;
// How much of the viewport the wash covers it with - the mockup's #0a0a0dcc.
constexpr int kScrimAlpha = 204;

QFont titleFontBold()
{
    QFont f = Theme::titleFont();
    f.setBold(true);
    return f;
}

QFont labelFontMedium()
{
    QFont f = Theme::bodyFont();
    f.setWeight(QFont::Medium);
    return f;
}

}  // namespace

// --- UnsavedCloseCard ---------------------------------------------------------

UnsavedCloseCard::UnsavedCloseCard(QWidget* viewport)
    : QWidget(viewport)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // The scrim swallows every click that reaches it, and none of them may
    // travel on to the viewport underneath - which picks on the release.
    setAttribute(Qt::WA_NoMousePropagation);
    // A hover over the scrim must not reach the viewport's hover highlight
    // either: tracking makes the move events this widget's own to swallow.
    setMouseTracking(true);
    Theme::makeSurfaceTransparent(this);
    setFocusPolicy(Qt::NoFocus);

    mySave = new CloseCardButton(CloseCardButton::Look::Primary, tr("Save and close"),
                                 tr("Enter"), this);
    myDiscard = new CloseCardButton(CloseCardButton::Look::Danger, tr("Close without saving"),
                                    QString(), this);
    myKeep = new CloseCardButton(CloseCardButton::Look::Plain, tr("Keep editing"), tr("Esc"),
                                 this);
    connect(mySave, &QAbstractButton::clicked, this, [this] { answer(Answer::Save); });
    connect(myDiscard, &QAbstractButton::clicked, this, [this] { answer(Answer::Discard); });
    connect(myKeep, &QAbstractButton::clicked, this, [this] { answer(Answer::Keep); });

    // A viewport resize while the question stands re-fits it. laidOut() is
    // the ordered route (MainWindow connects replace() to it); this filter is
    // the belt for a viewport with no overlay laying it out.
    if (viewport) viewport->installEventFilter(this);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        if (isAsking()) replace();
        update();
    });
    hide();
}

QAbstractButton* UnsavedCloseCard::saveButton() const { return mySave; }
QAbstractButton* UnsavedCloseCard::discardButton() const { return myDiscard; }
QAbstractButton* UnsavedCloseCard::keepButton() const { return myKeep; }

QString UnsavedCloseCard::titleText() const
{
    return tr("Save changes to %1 before closing?").arg(myName);
}

QString UnsavedCloseCard::bodyText() const
{
    return tr("You have changes since the last save.");
}

QStringList UnsavedCloseCard::paintedTexts() const
{
    QStringList texts;
    // The title's own words, with the user's name taken OUT rather than
    // swept along with them - see paintedUserTexts().
    for (const QString& fragment :
         tr("Save changes to %1 before closing?").split(QStringLiteral("%1"))) {
        const QString trimmed = fragment.trimmed();
        if (!trimmed.isEmpty()) texts << trimmed;
    }
    texts << bodyText();
    for (const CloseCardButton* button : {mySave, myDiscard, myKeep}) {
        texts << button->label();
        if (!button->keyHint().isEmpty()) texts << button->keyHint();
    }
    return texts;
}

void UnsavedCloseCard::ask(const QString& furnitureName)
{
    myName = furnitureName;
    replace();
    show();
    raise();
    update();
}

void UnsavedCloseCard::replace()
{
    if (!parentWidget()) return;
    // The scrim is the viewport, edge to edge - its extent is the viewport's
    // own, never rounded, or a wash a pixel short would leave a live strip.
    setGeometry(parentWidget()->rect());
    layoutCard();
    if (isAsking()) raise();
}

int UnsavedCloseCard::titleHeight(int textWidth) const
{
    const QFontMetrics metrics(titleFontBold());
    return metrics.boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap, titleText())
        .height();
}

int UnsavedCloseCard::bodyHeight(int textWidth) const
{
    const QFontMetrics metrics(Theme::bodyFont());
    return metrics.boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap, bodyText())
        .height();
}

void UnsavedCloseCard::layoutCard()
{
    const int available = std::max(kPad * 4, width() - kViewportGutter * 2);
    const int cardWidth = Theme::wholeDevicePixels(std::min(kCardWidth, available));
    const int textWidth = cardWidth - kPad * 2;
    // Measured with the fonts paintEvent() paints with - bold is wider than
    // regular, and a long furniture name wraps the title onto a second line.
    const int contentHeight = kPad + titleHeight(textWidth) + kGap + bodyHeight(textWidth) + kGap +
                              kButtonHeight * 3 + kButtonGap * 2 + kBottomPad;
    const int cardHeight = Theme::wholeDevicePixels(contentHeight);

    // Near-edge on a whole device pixel too: the position rule. The offset is
    // this widget's own origin inside the window, because the backing store
    // is the window's.
    const QPoint origin = mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    const int x = Theme::snapToDevicePixels((width() - cardWidth) / 2, origin.x(), dpr);
    const int y = Theme::snapToDevicePixels(
        std::max(kViewportGutter, static_cast<int>(height() * 0.48) - cardHeight / 2), origin.y(),
        dpr);
    myCard = QRect(x, y, cardWidth, cardHeight);

    const int buttonWidth = Theme::wholeDevicePixels(textWidth);
    const int buttonHeight = Theme::wholeDevicePixels(kButtonHeight);
    int buttonTop = myCard.bottom() + 1 - kBottomPad - (buttonHeight * 3 + kButtonGap * 2);
    for (CloseCardButton* button : {mySave, myDiscard, myKeep}) {
        button->setFixedSize(buttonWidth, buttonHeight);
        button->move(myCard.left() + kPad, buttonTop);
        buttonTop += buttonHeight + kButtonGap;
    }
}

void UnsavedCloseCard::answer(Answer which)
{
    // Hide FIRST: the key claim comes down with it (hideEvent), and whatever
    // the answer sets off - a save that fails and raises its Failure toast -
    // happens with nothing standing over the viewport.
    hide();
    switch (which) {
        case Answer::Save: emit saveChosen(); break;
        case Answer::Discard: emit discardChosen(); break;
        case Answer::Keep: emit keepChosen(); break;
    }
}

bool UnsavedCloseCard::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        if (event->type() == QEvent::Resize && isAsking()) replace();
        return QWidget::eventFilter(watched, event);
    }
    if (!isAsking()) return QWidget::eventFilter(watched, event);

    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress &&
        type != QEvent::KeyRelease) {
        return QWidget::eventFilter(watched, event);
    }
    // Application-wide means every window in this process - the library
    // window, and gui_smoke's many probes - so only keys headed for this
    // card's own window are this card's business.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window()) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        // Claims EVERY key back from QShortcutMap, not only Enter and Escape:
        // a Ctrl+Z or a Delete reaching its action behind the question would
        // change the document the question is about.
        event->accept();
        return true;
    }
    if (type == QEvent::KeyRelease) return true;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::NoModifier) {
        const int key = keyEvent->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            answer(Answer::Save);
            return true;
        }
        if (key == Qt::Key_Escape) {
            answer(Answer::Keep);
            return true;
        }
    }
    return true;   // swallowed - see the ShortcutOverride half above
}

void UnsavedCloseCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The wash: the viewport's own token, most of the way opaque, so the
    // model stays faintly there behind the question and follows an
    // Appearance edit like everything else.
    QColor wash = Theme::viewport();
    wash.setAlpha(kScrimAlpha);
    painter.fillRect(rect(), wash);

    Theme::paintSurface(painter, myCard, kCardRadius);

    const int textWidth = myCard.width() - kPad * 2;
    const int titleH = titleHeight(textWidth);
    painter.setFont(titleFontBold());
    painter.setPen(Theme::text());
    painter.drawText(QRect(myCard.left() + kPad, myCard.top() + kPad, textWidth, titleH),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, titleText());

    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(QRect(myCard.left() + kPad, myCard.top() + kPad + titleH + kGap, textWidth,
                           bodyHeight(textWidth)),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, bodyText());
}

// Every mouse event that reaches the scrim is swallowed: nothing behind the
// question may be picked, hovered, orbited, zoomed or dragged.
void UnsavedCloseCard::mousePressEvent(QMouseEvent* event) { event->accept(); }
void UnsavedCloseCard::mouseReleaseEvent(QMouseEvent* event) { event->accept(); }
void UnsavedCloseCard::mouseDoubleClickEvent(QMouseEvent* event) { event->accept(); }
void UnsavedCloseCard::mouseMoveEvent(QMouseEvent* event) { event->accept(); }
void UnsavedCloseCard::wheelEvent(QWheelEvent* event) { event->accept(); }
void UnsavedCloseCard::contextMenuEvent(QContextMenuEvent* event) { event->accept(); }

// The key claim follows the card's REAL visibility, both ways. ask() shows it,
// which lands here; but a hide the card never asked for - the window being
// minimized while the question stands - also delivers hideEvent(), and the
// question is still standing when the window comes back. Installing only in
// ask() left that returning card with no claim: Enter and Escape reached the
// actions behind it.
void UnsavedCloseCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!myClaimInstalled) {
        QCoreApplication::instance()->installEventFilter(this);
        myClaimInstalled = true;
    }
}

void UnsavedCloseCard::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myClaimInstalled) {
        QCoreApplication::instance()->removeEventFilter(this);
        myClaimInstalled = false;
    }
}

// --- CloseCardButton ----------------------------------------------------------

CloseCardButton::CloseCardButton(Look look, const QString& label, const QString& keyHint,
                                 QWidget* parent)
    : QAbstractButton(parent)
    , myLook(look)
    , myLabel(label)
    , myKeyHint(keyHint)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);
    // Never takes focus: the card's key claim is what answers Enter, and a
    // focused button answering Space as well would be a second, unadvertised
    // verb.
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setText(label);
    setToolTip(QString());
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            qOverload<>(&QWidget::update));
}

void CloseCardButton::enterEvent(QEnterEvent* event)
{
    myHovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void CloseCardButton::leaveEvent(QEvent* event)
{
    myHovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void CloseCardButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = QRectF(rect());
    QColor fill;
    QColor stroke;
    QColor ink;
    QColor hint;
    switch (myLook) {
        case Look::Primary:
            fill = Theme::accent();
            if (isDown()) fill = fill.darker(115);
            else if (myHovered) fill = fill.lighter(115);
            stroke = fill;
            ink = Theme::text();
            hint = Theme::text();
            break;
        case Look::Danger:
        case Look::Plain:
            fill = isDown() ? Theme::chipActive() : (myHovered ? Theme::chipHover() : Theme::chip());
            stroke = Theme::border();
            ink = myLook == Look::Danger ? Theme::danger() : Theme::text();
            hint = Theme::textMuted();
            break;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(box, kButtonRadius, kButtonRadius);
    const double strokeWidth = std::max(1.0, Theme::chipStrokePx());
    Theme::drawCrispBorder(painter, box, stroke, kButtonRadius, strokeWidth);

    const QRect inner = rect().adjusted(kButtonTextInset, 0, -kButtonTextInset, 0);
    painter.setFont(labelFontMedium());
    painter.setPen(ink);
    painter.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, myLabel);
    if (!myKeyHint.isEmpty()) {
        painter.setFont(Theme::badgeFont());
        painter.setPen(hint);
        painter.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, myKeyHint);
    }
}

// --- FurnitureNameMark --------------------------------------------------------

namespace {
constexpr int kDotDiameter = 7;
constexpr int kDotGap = 6;
}  // namespace

FurnitureNameMark::FurnitureNameMark(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFont(Theme::labelFont());
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        setFont(Theme::labelFont());
        updateGeometry();
        update();
    });
}

void FurnitureNameMark::setState(const QString& name, bool unsaved)
{
    if (name == myName && unsaved == myUnsaved) return;
    myName = name;
    myUnsaved = unsaved;
    updateGeometry();
    update();
}

QString FurnitureNameMark::unsavedText() const
{
    return tr("unsaved changes");
}

QStringList FurnitureNameMark::paintedTexts() const
{
    return {unsavedText()};
}

namespace {
// The one composition sizeHint() measures and paintEvent() paints.
QString composedText(const QString& name, bool unsaved, const QString& unsavedText)
{
    return unsaved ? QStringLiteral("%1 — %2").arg(name, unsavedText) : name;
}
}  // namespace

QSize FurnitureNameMark::sizeHint() const
{
    const QFontMetrics metrics(Theme::labelFont());
    const int textWidth =
        metrics.horizontalAdvance(composedText(myName, myUnsaved, unsavedText()));
    const int dotWidth = myUnsaved ? kDotDiameter + kDotGap : 0;
    return QSize(dotWidth + textWidth + 4, metrics.height());
}

QRect FurnitureNameMark::dotRect() const
{
    if (!myUnsaved) return QRect();
    return QRect(0, (height() - kDotDiameter) / 2, kDotDiameter, kDotDiameter);
}

void FurnitureNameMark::paintEvent(QPaintEvent* /*event*/)
{
    if (myName.isEmpty()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    int x = 0;
    if (myUnsaved) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::caution());
        painter.drawEllipse(dotRect());
        x = kDotDiameter + kDotGap;
    }
    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(x, 0, width() - x, height()), Qt::AlignLeft | Qt::AlignVCenter,
                     composedText(myName, myUnsaved, unsavedText()));
}
