#include "NameFurnitureCard.h"

#include "Theme.h"
#include "UnsavedCloseCard.h"   // CloseCardButton - the two questions share one look

#include <QCoreApplication>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

namespace {
// The close question's own metrics - this is the same card in a different
// question, so the numbers are shared rather than picked again. Multiples of
// four, so every WIDGET extent is whole device pixels at any Windows scale.
constexpr int kCardWidth = 380;
constexpr int kPad = 20;
constexpr int kBottomPad = 16;
constexpr int kGap = 12;
constexpr int kFieldHeight = 36;
constexpr int kButtonHeight = 36;
constexpr int kButtonGap = 8;
constexpr int kCardRadius = 14;
constexpr int kHostGutter = 16;
constexpr int kScrimAlpha = 204;

QFont titleFontBold()
{
    QFont f = Theme::titleFont();
    f.setBold(true);
    return f;
}
}  // namespace

NameFurnitureCard::NameFurnitureCard(QWidget* host)
    : QWidget(host)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    setMouseTracking(true);
    Theme::makeSurfaceTransparent(this);
    setFocusPolicy(Qt::NoFocus);

    myField = new QLineEdit(this);
    myField->setAttribute(Qt::WA_NoMousePropagation);
    myField->setMaxLength(120);

    myCancel = new CloseCardButton(CloseCardButton::Look::Plain, tr("Cancel"), tr("Esc"), this);
    myCreate = new CloseCardButton(CloseCardButton::Look::Primary, tr("Create"), tr("Enter"), this);
    connect(myCreate, &QAbstractButton::clicked, this, [this] { accept(); });
    connect(myCancel, &QAbstractButton::clicked, this, [this] { reject(); });
    // A name with nothing in it cannot be created, and a control that looks
    // live and does nothing is the thing this app refuses to ship - so the
    // Create control follows the field.
    connect(myField, &QLineEdit::textChanged, this,
            [this](const QString&) { myCreate->setEnabled(!name().isEmpty()); });

    if (host) host->installEventFilter(this);
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyTheme();
        if (isAsking()) replace();
        update();
    });
    hide();
}

QAbstractButton* NameFurnitureCard::createButton() const { return myCreate; }
QAbstractButton* NameFurnitureCard::cancelButton() const { return myCancel; }

QString NameFurnitureCard::name() const
{
    return myField ? myField->text().trimmed() : QString();
}

QString NameFurnitureCard::titleText() const
{
    return tr("Name this furniture");
}

QString NameFurnitureCard::bodyText() const
{
    return tr("You can rename it later from the library.");
}

QStringList NameFurnitureCard::paintedTexts() const
{
    QStringList texts{titleText(), bodyText()};
    for (const CloseCardButton* button : {myCreate, myCancel}) {
        texts << button->label();
        if (!button->keyHint().isEmpty()) texts << button->keyHint();
    }
    return texts;
}

void NameFurnitureCard::ask(const QString& suggestion)
{
    myField->setText(suggestion);
    myCreate->setEnabled(!name().isEmpty());
    replace();
    show();
    raise();
    // Focus and a full selection together: the suggested name is a
    // suggestion, so the first character typed replaces it rather than
    // landing in the middle of it.
    myField->setFocus(Qt::OtherFocusReason);
    myField->selectAll();
    update();
}

void NameFurnitureCard::applyTheme()
{
    if (!myField) return;
    myField->setFont(Theme::bodyFont());
    myField->setStyleSheet(QStringLiteral("QLineEdit { background-color: %1; color: %2; "
                                          "border: 1px solid %3; border-radius: 8px; "
                                          "padding: 2px 10px; }")
                               .arg(Theme::chip().name(), Theme::text().name(),
                                    Theme::border().name()));
}

void NameFurnitureCard::replace()
{
    if (!parentWidget()) return;
    setGeometry(parentWidget()->rect());
    layoutCard();
    if (isAsking()) raise();
}

int NameFurnitureCard::titleHeight(int textWidth) const
{
    const QFontMetrics metrics(titleFontBold());
    return metrics.boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap, titleText())
        .height();
}

void NameFurnitureCard::layoutCard()
{
    const int available = std::max(kPad * 4, width() - kHostGutter * 2);
    const int cardWidth = Theme::wholeDevicePixels(std::min(kCardWidth, available));
    const int textWidth = cardWidth - kPad * 2;
    const QFontMetrics body(Theme::bodyFont());
    const int bodyH =
        body.boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap, bodyText()).height();
    const int contentHeight = kPad + titleHeight(textWidth) + kGap + kFieldHeight + kGap + bodyH +
                              kGap + kButtonHeight + kBottomPad;
    const int cardHeight = Theme::wholeDevicePixels(contentHeight);

    const QPoint origin = mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    const int x = Theme::snapToDevicePixels((width() - cardWidth) / 2, origin.x(), dpr);
    const int y = Theme::snapToDevicePixels(
        std::max(kHostGutter, static_cast<int>(height() * 0.44) - cardHeight / 2), origin.y(), dpr);
    myCard = QRect(x, y, cardWidth, cardHeight);

    myField->setFixedSize(Theme::wholeDevicePixels(textWidth),
                          Theme::wholeDevicePixels(kFieldHeight));
    myField->move(myCard.left() + kPad, myCard.top() + kPad + titleHeight(textWidth) + kGap);

    // The two controls side by side on one row - Cancel left, Create right -
    // because there are two of them and they are opposites, where the close
    // question's three answers are a column of alternatives.
    const int buttonWidth = Theme::wholeDevicePixels((textWidth - kButtonGap) / 2);
    const int buttonHeight = Theme::wholeDevicePixels(kButtonHeight);
    const int buttonTop = myCard.bottom() + 1 - kBottomPad - buttonHeight;
    myCancel->setFixedSize(buttonWidth, buttonHeight);
    myCancel->move(myCard.left() + kPad, buttonTop);
    myCreate->setFixedSize(buttonWidth, buttonHeight);
    myCreate->move(myCard.right() + 1 - kPad - buttonWidth, buttonTop);
}

void NameFurnitureCard::accept()
{
    const QString chosen = name();
    if (chosen.isEmpty()) return;   // Create is disabled; Enter lands here too
    hide();
    emit created(chosen);
}

void NameFurnitureCard::reject()
{
    hide();
    emit cancelled();
}

bool NameFurnitureCard::eventFilter(QObject* watched, QEvent* event)
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
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window()) return QWidget::eventFilter(watched, event);

    if (type == QEvent::ShortcutOverride) {
        // Every key, back from QShortcutMap - a shortcut reaching an action
        // behind this question would act on the furniture it is about to
        // replace.
        event->accept();
        return true;
    }
    if (type == QEvent::KeyRelease) return true;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    const int key = keyEvent->key();
    if (mods == Qt::NoModifier) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            accept();
            return true;
        }
        if (key == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    // Everything else the user types belongs to the field - this card's whole
    // subject is a name being typed, so unlike the close question it hands the
    // key on instead of swallowing it.
    if (widget == myField) return QWidget::eventFilter(watched, event);
    return true;
}

void NameFurnitureCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

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

    const QFontMetrics body(Theme::bodyFont());
    const QRect bodyRect(myCard.left() + kPad, myField->geometry().bottom() + 1 + kGap, textWidth,
                         body.boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap,
                                           bodyText())
                             .height());
    painter.setFont(Theme::bodyFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(bodyRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, bodyText());
}

void NameFurnitureCard::mousePressEvent(QMouseEvent* event) { event->accept(); }
void NameFurnitureCard::mouseReleaseEvent(QMouseEvent* event) { event->accept(); }
void NameFurnitureCard::mouseDoubleClickEvent(QMouseEvent* event) { event->accept(); }
void NameFurnitureCard::mouseMoveEvent(QMouseEvent* event) { event->accept(); }
void NameFurnitureCard::wheelEvent(QWheelEvent* event) { event->accept(); }
void NameFurnitureCard::contextMenuEvent(QContextMenuEvent* event) { event->accept(); }

void NameFurnitureCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!myClaimInstalled) {
        QCoreApplication::instance()->installEventFilter(this);
        myClaimInstalled = true;
    }
}

void NameFurnitureCard::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myClaimInstalled) {
        QCoreApplication::instance()->removeEventFilter(this);
        myClaimInstalled = false;
    }
}
