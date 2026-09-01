#include "InitScreen.h"

#include "FurnitureStore.h"
#include "InlineRename.h"
#include "Theme.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace {

constexpr int kCardWidth = 200;
constexpr int kThumbHeight = 120;
constexpr int kCardHeight = 190;
constexpr int kCardRadius = 10;
constexpr int kCardPad = 10;
constexpr int kCardSpacing = 18;
constexpr int kGridMargin = 32;

// "2 minutes ago", "Yesterday", "5 days ago", and a plain date once it is far
// enough back that a relative count stops being useful information. Not
// Measure::formatLength's business - Measure formats furniture dimensions,
// not calendar time - so this is its own small helper, local to the one
// widget that needs it.
QString relativeDateString(const QDateTime& when)
{
    if (!when.isValid()) return QObject::tr("Never saved");

    const qint64 secs = when.secsTo(QDateTime::currentDateTimeUtc());
    if (secs < 60) return QObject::tr("Just now");
    if (secs < 3600) {
        const qint64 mins = secs / 60;
        return mins == 1 ? QObject::tr("1 minute ago")
                         : QObject::tr("%1 minutes ago").arg(mins);
    }
    if (secs < 86400) {
        const qint64 hours = secs / 3600;
        return hours == 1 ? QObject::tr("1 hour ago") : QObject::tr("%1 hours ago").arg(hours);
    }
    if (secs < 2 * 86400) return QObject::tr("Yesterday");
    if (secs < 7 * 86400) {
        const qint64 days = secs / 86400;
        return QObject::tr("%1 days ago").arg(days);
    }
    return when.toLocalTime().date().toString(QStringLiteral("MMM d, yyyy"));
}

// `background: transparent` on a child of this card is not decoration: the
// app-wide stylesheet paints every QWidget chrome-black, and a label that
// stamped its own rectangle over the card's painted surface would be a
// black bar across it - the same reasoning AppearancePanel::makeTransparent()
// and ItemsPanel's rows already carry.
QString labelChrome(const QColor& colour, const QFont& font, bool bold = false)
{
    return QStringLiteral("background: transparent; border: none; color: %1; "
                          "font-size: %2pt;%3")
        .arg(colour.name())
        .arg(font.pointSizeF())
        .arg(bold ? QStringLiteral(" font-weight: 600;") : QString());
}

// One furniture entry, or the New furniture card - the same shape either
// way (a thumbnail area, a name, a date), which is what makes "click
// anywhere to open, double-click the name to rename" a single small state
// machine rather than two card classes to keep in step.
class InitCardWidget : public QWidget {
public:
    explicit InitCardWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(Theme::wholeDevicePixels(QSize(kCardWidth, kCardHeight)));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(kCardPad, kCardPad, kCardPad, kCardPad);
        layout->setSpacing(6);

        // All three labels are mouse-TRANSPARENT. Without it they are what a
        // real click actually lands on - Qt delivers a press to the deepest
        // widget under the cursor, not to whichever ancestor happens to
        // override mousePressEvent() - so this card's own handlers below
        // would go dead across nearly its whole visible area, the exact trap
        // CLAUDE.md's childAt()-vs-sendEvent warning describes: a probe that
        // sends an event straight at the card passes regardless, and only a
        // real childAt() hit test catches it (as gui_smoke's does). None of
        // the three has interactive children of its own to lose by this, so
        // the WA_TransparentForMouseEvents trap that swallows a whole
        // subtree elsewhere in this app (see HintBalloon, ToastHost) does
        // not apply here - it hands the press straight to the card.
        myThumb = new QLabel(this);
        myThumb->setFixedSize(kCardWidth - 2 * kCardPad, kThumbHeight);
        myThumb->setAlignment(Qt::AlignCenter);
        myThumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(myThumb);

        myName = new QLabel(this);
        myName->setWordWrap(false);
        myName->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(myName);

        myDate = new QLabel(this);
        myDate->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(myDate);

        layout->addStretch(1);
        restyle();
    }

    void setFurniture(const QString& name, const QString& thumbPath, const QDateTime& lastEdited)
    {
        myIsNew = false;
        myName->setText(name);
        myDate->setText(relativeDateString(lastEdited));
        myDate->show();
        QPixmap pix;
        if (!thumbPath.isEmpty() && QFileInfo::exists(thumbPath) && pix.load(thumbPath)) {
            myThumb->setPixmap(pix.scaled(myThumb->size(), Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
        } else {
            myThumb->setPixmap(QPixmap());
        }
        update();
    }

    void setAsNewCard(const QString& label)
    {
        myIsNew = true;
        myName->setText(label);
        myDate->hide();
        myThumb->setPixmap(QPixmap());
        update();
    }

    void restyle()
    {
        myThumb->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        myName->setStyleSheet(labelChrome(Theme::text(), Theme::bodyFont(), true));
        myDate->setStyleSheet(labelChrome(Theme::textMuted(), Theme::labelFont()));
        update();
    }

    QRect nameLabelGeometry() const { return myName->geometry(); }
    QString nameText() const { return myName->text(); }

    // Single click (press+release inside the card) or Enter opens it;
    // double-clicking the NAME, or F2 anywhere on the card, renames it
    // instead - never both for the same gesture.
    std::function<void()> onActivated;
    std::function<void()> onRenameRequested;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) setFocus(Qt::MouseFocusReason);
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onActivated)
            onActivated();
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (!myIsNew && myName->geometry().contains(event->pos())) {
            if (onRenameRequested) onRenameRequested();
        } else if (onActivated) {
            onActivated();
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!myIsNew && event->key() == Qt::Key_F2) {
            if (onRenameRequested) onRenameRequested();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && onActivated) {
            onActivated();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        Theme::paintSurface(painter, rect(), kCardRadius, Theme::chrome());

        if (myIsNew) {
            // A plus, drawn as geometry rather than a character - the same
            // reasoning IconSet::appIcon() gives its own mark: no font stack
            // is guaranteed to carry a glyph this card could fall back to.
            const QRectF plusArea(0, 0, width(), kThumbHeight + kCardPad);
            const QPointF c = plusArea.center();
            constexpr double kArm = 16.0;
            QPen pen(Theme::accent(), 3.0, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(pen);
            painter.drawLine(QPointF(c.x() - kArm, c.y()), QPointF(c.x() + kArm, c.y()));
            painter.drawLine(QPointF(c.x(), c.y() - kArm), QPointF(c.x(), c.y() + kArm));
        }

        if (this == window()->focusWidget()) {
            const bool active = window()->isActiveWindow();
            Theme::drawCrispBorder(painter, QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5),
                                   active ? Theme::focusRing() : Theme::focusRingMuted(),
                                   kCardRadius - 1, active ? 2.0 : 1.5);
        }
    }

private:
    bool myIsNew = false;
    QLabel* myThumb = nullptr;
    QLabel* myName = nullptr;
    QLabel* myDate = nullptr;
};

}  // namespace

InitScreen::InitScreen(FurnitureStore* store, QWidget* parent)
    : QWidget(parent), myStore(store)
{
    // Covers the whole viewport opaquely (see the header) and must never let
    // a press or release fall through to whatever the viewport would have
    // picked underneath - the same rule every floating card over the GL
    // surface already carries.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    outer->setSpacing(16);

    myTitle = new QLabel(tr("Your furniture"), this);
    outer->addWidget(myTitle);

    myScroll = new QScrollArea(this);
    myScroll->setWidgetResizable(true);
    myScroll->setFrameShape(QFrame::NoFrame);
    myScroll->setAttribute(Qt::WA_NoSystemBackground);
    myScroll->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    outer->addWidget(myScroll, 1);

    myGrid = new QWidget(myScroll);
    myGrid->setAttribute(Qt::WA_NoSystemBackground);
    new QGridLayout(myGrid);
    myScroll->setWidget(myGrid);

    myNewCard = buildNewCard();

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &InitScreen::applyTheme);

    refresh();
}

QWidget* InitScreen::buildNewCard()
{
    auto* card = new InitCardWidget(myGrid);
    card->setAsNewCard(tr("New furniture"));
    card->onActivated = [this] {
        if (!myStore) return;
        const QString name = myStore->nextFurnitureName();
        const QString id = myStore->createFurniture(name);
        // FurnitureStore::createFurniture() refuses (empty id) when the
        // root or the furniture's own directory cannot be created - a
        // refusal this screen must not swallow. Never silent: every outcome
        // this app reports either succeeds visibly or says so as a Failure.
        if (id.isEmpty()) {
            emit furnitureCreateFailed(name);
            return;
        }
        emit furnitureCreated(id);
    };
    return card;
}

QWidget* InitScreen::buildFurnitureCard(const QString& id, const QString& name,
                                       const QString& thumbPath, const QDateTime& lastEdited)
{
    auto* card = new InitCardWidget(myGrid);
    card->setFurniture(name, thumbPath, lastEdited);
    card->onActivated = [this, id] { emit furnitureChosen(id); };
    card->onRenameRequested = [this, id, card] {
        if (!myStore) return;
        InlineRename::beginRename(card, card->nameLabelGeometry(), card->nameText(),
                                  [this, id](QString newName) {
                                      if (!myStore) return;
                                      // A refusal here (the furniture's own
                                      // manifest is unreadable or gone) must
                                      // not be repainted over in silence -
                                      // refresh() only runs on the success
                                      // path, so a refused rename leaves the
                                      // card showing exactly what it showed
                                      // before, with the refusal reported
                                      // rather than merely implied by
                                      // nothing changing.
                                      if (!myStore->renameFurniture(id, newName)) {
                                          emit furnitureRenameFailed(id, newName);
                                          return;
                                      }
                                      refresh();
                                  });
    };
    return card;
}

void InitScreen::refresh()
{
    rebuildCards();
    relayoutCards();
}

void InitScreen::rebuildCards()
{
    for (const Card& c : myCards) {
        if (c.widget) c.widget->deleteLater();
    }
    myCards.clear();

    if (!myStore) return;

    for (const FurnitureStore::FurnitureInfo& info : myStore->listFurniture()) {
        QWidget* widget = buildFurnitureCard(info.id, info.name, info.thumbPath, info.lastEdited);
        Card c;
        c.id = info.id;
        c.widget = widget;
        myCards.push_back(c);
    }
}

void InitScreen::relayoutCards()
{
    auto* grid = qobject_cast<QGridLayout*>(myGrid->layout());
    if (!grid) return;

    while (QLayoutItem* item = grid->takeAt(0)) delete item;   // widgets themselves are kept

    const int available = std::max(kCardWidth, myScroll ? myScroll->viewport()->width() : width());
    const int perRow = std::max(1, (available + kCardSpacing) / (kCardWidth + kCardSpacing));

    grid->setHorizontalSpacing(kCardSpacing);
    grid->setVerticalSpacing(kCardSpacing);

    int row = 0, col = 0;
    std::vector<QWidget*> placedWidgets;
    auto place = [&](QWidget* w) {
        grid->addWidget(w, row, col);
        w->show();
        placedWidgets.push_back(w);
        if (++col >= perRow) { col = 0; ++row; }
    };

    // New furniture leads the gallery - it is always reachable at a fixed
    // position rather than shuffling as the library grows.
    if (myNewCard) place(myNewCard);
    for (const Card& c : myCards) {
        if (c.widget) place(c.widget);
    }

    // Whole-device-pixel positions, the same rule every other floating card
    // in this shell follows (Theme.h) - each card's own SIZE already rounds
    // up whole (InitCardWidget's constructor), but QGridLayout still lands
    // its LOGICAL position wherever the row/column arithmetic puts it, which
    // a fractional display scale can leave on a fractional device row. The
    // seam here is chrome-on-chrome (this card's own paintSurface() ground
    // over InitScreen's own opaque fill) rather than over OCCT's GL surface,
    // so a miss reads as a soft antialiasing edge rather than the hard black
    // hairline the GL-surface cards risk - lower stakes, but "every floating
    // card" is the rule, not "every floating card where it would otherwise
    // be visible". activate() forces the grid to actually compute positions
    // now, synchronously, rather than leaving them pending for the next
    // paint/resize cycle - addWidget() alone only marks the layout dirty.
    grid->activate();
    const QPoint origin = myGrid->mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    for (QWidget* w : placedWidgets) {
        w->move(Theme::snapToDevicePixels(w->x(), origin.x(), dpr),
               Theme::snapToDevicePixels(w->y(), origin.y(), dpr));
    }
}

QWidget* InitScreen::cardAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(myCards.size())) return nullptr;
    return myCards[static_cast<std::size_t>(index)].widget;
}

QString InitScreen::cardName(int index) const
{
    QWidget* w = cardAt(index);
    auto* card = static_cast<InitCardWidget*>(w);
    return card ? card->nameText() : QString();
}

QString InitScreen::cardFurnitureId(int index) const
{
    if (index < 0 || index >= static_cast<int>(myCards.size())) return QString();
    return myCards[static_cast<std::size_t>(index)].id;
}

void InitScreen::beginRenameAt(int index)
{
    QWidget* w = cardAt(index);
    auto* card = static_cast<InitCardWidget*>(w);
    if (card && card->onRenameRequested) card->onRenameRequested();
}

QStringList InitScreen::paintedTexts() const
{
    QStringList texts;
    if (myTitle) texts << myTitle->text();
    if (auto* newCard = static_cast<InitCardWidget*>(myNewCard)) texts << newCard->nameText();
    return texts;
}

void InitScreen::applyTheme()
{
    myTitle->setStyleSheet(labelChrome(Theme::text(), Theme::titleFont(), true));
    for (const Card& c : myCards) {
        if (auto* card = static_cast<InitCardWidget*>(c.widget)) card->restyle();
    }
    if (auto* newCard = static_cast<InitCardWidget*>(myNewCard)) newCard->restyle();
    update();
}

void InitScreen::paintEvent(QPaintEvent*)
{
    // The full-bleed opaque fill CLAUDE.md's paint law requires here: this
    // widget covers the whole viewport, and an unpainted pixel over OCCT's
    // GL surface is not transparent, it is whatever the driver left there.
    QPainter painter(this);
    painter.fillRect(rect(), Theme::chrome());
}

void InitScreen::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayoutCards();
}
