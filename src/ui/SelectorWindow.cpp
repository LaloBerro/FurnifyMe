#include "SelectorWindow.h"

#include "FurnitureStore.h"
#include "InlineRename.h"
#include "Theme.h"

#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// Landscape 16:10, per the picked mockup - the thumbnail's own width drives
// its height, the same relationship VersionsPanel's kThumbAspect already
// uses for its own cards.
constexpr int kThumbWidth = 220;
constexpr double kThumbAspect = 10.0 / 16.0;
constexpr int kCardRadius = 10;
constexpr int kUnderRowHeight = 26;
constexpr int kActionButtonHeight = 22;
constexpr int kCellSpacing = 8;     // between the thumbnail and its under-row
constexpr int kGridSpacing = 24;
constexpr int kGridMargin = 32;
// How long the inline failure banner stays up - Toast::Kind::Failure's own
// duration (Toast.h), so a refusal here reads for exactly as long as one
// would if this window had a real ToastHost to route it through.
constexpr int kFailureMs = 8000;

int thumbHeight()
{
    return Theme::wholeDevicePixels(static_cast<int>(std::lround(kThumbWidth * kThumbAspect)));
}

QString labelChrome(const QColor& colour, const QFont& font, bool bold = false)
{
    return QStringLiteral("background: transparent; border: none; color: %1; "
                          "font-size: %2pt;%3")
        .arg(colour.name())
        .arg(font.pointSizeF())
        .arg(bold ? QStringLiteral(" font-weight: 500;") : QString());
}

QString actionButtonCss()
{
    // "small bordered buttons" - VersionsPanel's own buttonCss(), reused
    // verbatim in spirit: this is the established shape for a hover-only
    // action button in this app's shell, and Rename/Delete are exactly that.
    return QStringLiteral(
               "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
               "border-radius: 4px; font-size: %4pt; padding: 2px 8px; } "
               "QPushButton:hover { background-color: %5; } "
               "QPushButton:disabled { color: %6; border-color: %6; }")
        .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name())
        .arg(Theme::labelFont().pointSizeF())
        .arg(Theme::chipHover().name(), Theme::textDisabled().name());
}

// The bordered, rounded thumbnail - the "card" proper. A plain QWidget
// cannot paint its own rounded background (InitScreen.cpp's own precedent),
// so this paints Theme::paintSurface() with `ground` = Theme::chrome() (this
// widget sits on the window's own chrome-filled background, never on OCCT's
// GL surface - see the header for why that distinction does not matter for
// the opaque-paint law itself, only for which ground colour is correct), then
// redraws the border in accent() while hovered - same geometry
// paintSurface() already stroked in border(), so the swap fully replaces it
// rather than adding a second ring.
class ThumbCardWidget : public QWidget {
public:
    explicit ThumbCardWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(Theme::wholeDevicePixels(QSize(kThumbWidth, thumbHeight())));
    }

    void setThumbnailPath(const QString& path)
    {
        QPixmap pix;
        myHasImage = !path.isEmpty() && QFileInfo::exists(path) && pix.load(path);
        myPixmap = myHasImage ? pix.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation)
                              : QPixmap();
        update();
    }

    void setHovered(bool hovered)
    {
        if (myHovered == hovered) return;
        myHovered = hovered;
        update();
    }
    bool hovered() const { return myHovered; }

    std::function<void()> onActivated;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onActivated) {
            onActivated();
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        Theme::paintSurface(painter, rect(), kCardRadius, Theme::chrome());

        if (myHasImage) {
            QPainterPath clip;
            clip.addRoundedRect(QRectF(rect()), kCardRadius, kCardRadius);
            painter.save();
            painter.setClipPath(clip);
            painter.drawPixmap(rect(), myPixmap);
            painter.restore();
        }

        Theme::drawCrispBorder(painter, QRectF(rect()),
                               myHovered ? Theme::accent() : Theme::border(), kCardRadius);
    }

private:
    bool myHovered = false;
    bool myHasImage = false;
    QPixmap myPixmap;
};

// One grid cell: the thumbnail above, an under-row below carrying the name
// on the left and - at rest - the last-edited date on the right, swapped on
// hover for Rename/Delete (see the header comment on SelectorWindow for the
// mockup's own words). Hovering the CELL as a whole is what triggers both
// the thumbnail's border swap and the under-row's swap - a furniture entry
// reads as one unit, not two independently-hoverable halves.
class SelectorCardWidget : public QWidget {
public:
    explicit SelectorCardWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(kCellSpacing);

        myThumb = new ThumbCardWidget(this);
        layout->addWidget(myThumb);

        auto* underRow = new QWidget(this);
        underRow->setAttribute(Qt::WA_NoSystemBackground);
        underRow->setStyleSheet(QStringLiteral("background: transparent;"));
        underRow->setFixedHeight(kUnderRowHeight);
        auto* rowLayout = new QHBoxLayout(underRow);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        myName = new QLabel(underRow);
        myName->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(myName, 1);

        myDate = new QLabel(underRow);
        myDate->setAttribute(Qt::WA_TransparentForMouseEvents);
        myDate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(myDate);

        myRename = new QPushButton(SelectorCardWidget::renameLabel(), underRow);
        myDelete = new QPushButton(SelectorCardWidget::deleteLabel(), underRow);
        for (QPushButton* b : {myRename, myDelete}) {
            b->setFixedHeight(kActionButtonHeight);
            b->setVisible(false);
            rowLayout->addWidget(b);
        }

        layout->addWidget(underRow);
        setFixedSize(Theme::wholeDevicePixels(
            QSize(kThumbWidth, thumbHeight() + kCellSpacing + kUnderRowHeight)));

        myDeleteTimer = new QTimer(this);
        myDeleteTimer->setSingleShot(true);
        myDeleteTimer->setInterval(SelectorWindow::kDeleteConfirmMs);
        connect(myDeleteTimer, &QTimer::timeout, this, [this] { disarmDelete(); });

        connect(myRename, &QPushButton::clicked, this, [this] {
            if (onRenameRequested) onRenameRequested();
        });
        connect(myDelete, &QPushButton::clicked, this, [this] {
            if (myDeleteArmed) {
                // The callback may rebuild the whole grid (a successful
                // delete does, via refresh()) - nothing below may touch
                // `this` once it runs, same rule VersionsPanel's own Delete
                // handler follows for exactly the same reason.
                myDeleteArmed = false;
                if (onDeleteRequested) onDeleteRequested();
                return;
            }
            myDeleteArmed = true;
            myDelete->setText(SelectorCardWidget::deleteArmedLabel());
            myDeleteTimer->start();
        });

        myThumb->onActivated = [this] {
            if (onActivated) onActivated();
        };

        restyle();
    }

    static QString renameLabel() { return tr("Rename"); }
    static QString deleteLabel() { return tr("Delete"); }
    static QString deleteArmedLabel() { return tr("Delete — click again"); }

    void setFurniture(const QString& name, const QString& thumbPath, const QDateTime& lastEdited)
    {
        myName->setText(name);
        myDate->setText(lastEdited.isValid()
                            ? QLocale().toString(lastEdited.toLocalTime(), QLocale::ShortFormat)
                            : tr("Never saved"));
        myThumb->setThumbnailPath(thumbPath);
        update();
    }

    void restyle()
    {
        myName->setStyleSheet(labelChrome(Theme::text(), Theme::bodyFont(), true));
        myDate->setStyleSheet(labelChrome(Theme::textMuted(), Theme::labelFont()));
        const QString css = actionButtonCss();
        myRename->setStyleSheet(css);
        myDelete->setStyleSheet(css);
        update();
    }

    QString nameText() const { return myName->text(); }
    QRect nameLabelGeometry() const
    {
        return QRect(myName->mapTo(const_cast<SelectorCardWidget*>(this), QPoint(0, 0)),
                     myName->size());
    }

    QWidget* thumb() const { return myThumb; }
    QPushButton* renameButton() const { return myRename; }
    QPushButton* deleteButton() const { return myDelete; }
    QLabel* dateLabel() const { return myDate; }
    int deleteArmedMs() const
    {
        return (myDeleteArmed && myDeleteTimer && myDeleteTimer->isActive())
                   ? myDeleteTimer->remainingTime()
                   : -1;
    }

    std::function<void()> onActivated;
    std::function<void()> onRenameRequested;
    std::function<void()> onDeleteRequested;

protected:
    void enterEvent(QEnterEvent*) override { setHovered(true); }
    void leaveEvent(QEvent*) override { setHovered(false); }

private:
    void setHovered(bool hovered)
    {
        myThumb->setHovered(hovered);
        myDate->setVisible(!hovered);
        myRename->setVisible(hovered);
        myDelete->setVisible(hovered);
    }

    void disarmDelete()
    {
        myDeleteArmed = false;
        myDelete->setText(SelectorCardWidget::deleteLabel());
    }

    ThumbCardWidget* myThumb = nullptr;
    QLabel* myName = nullptr;
    QLabel* myDate = nullptr;
    QPushButton* myRename = nullptr;
    QPushButton* myDelete = nullptr;
    QTimer* myDeleteTimer = nullptr;
    bool myDeleteArmed = false;
};

}  // namespace

SelectorWindow::SelectorWindow(FurnitureStore& store, QWidget* parent)
    // Qt::Window explicitly: a plain QWidget(parent) with no flags becomes
    // an EMBEDDED CHILD the instant a parent is passed, not a separate
    // top-level window - `parent` here is for ownership/lifetime only (a
    // caller that wants this window torn down with something else, the same
    // idiom QDialog(parent) already establishes), never for layout. Without
    // this, a caller passing a parent would silently get a widget stuck
    // inside that parent's own window instead of the picker this class
    // promises to be.
    : QWidget(parent, Qt::Window), myStore(store)
{
    setWindowTitle(tr("FurnifyMe"));
    setAttribute(Qt::WA_NoSystemBackground);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    outer->setSpacing(16);

    auto* header = new QWidget(this);
    header->setAttribute(Qt::WA_NoSystemBackground);
    header->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    myTitle = new QLabel(tr("Your furniture"), header);
    headerLayout->addWidget(myTitle);
    headerLayout->addStretch(1);

    myNewButton = new QPushButton(tr("+ New furniture"), header);
    myNewButton->setFixedHeight(Theme::wholeDevicePixels(32));
    headerLayout->addWidget(myNewButton);
    connect(myNewButton, &QPushButton::clicked, this, [this] {
        emit createRequested();
        const QString name = myStore.nextFurnitureName();
        const QString id = myStore.createFurniture(name);
        // FurnitureStore::createFurniture() refuses (empty id) when the root
        // or the furniture's own directory cannot be created - never
        // swallowed: InitScreen's own furnitureCreateFailed() ruling, moved
        // in-window since there is no MainWindow toast to route it through
        // from here.
        if (id.isEmpty()) {
            showFailure(tr("Couldn't create %1 — Check that the library folder "
                          "still exists and isn't read-only")
                            .arg(name));
            return;
        }
        emit furnitureChosen(id);
    });

    outer->addWidget(header);

    myScroll = new QScrollArea(this);
    myScroll->setWidgetResizable(true);
    myScroll->setFrameShape(QFrame::NoFrame);
    myScroll->setAttribute(Qt::WA_NoSystemBackground);
    myScroll->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    // WA_NoSystemBackground alone is not enough here, unlike everywhere else
    // in this shell that relies on it: Theme::apply()'s application-wide
    // stylesheet puts every widget under QStyleSheetStyle, which paints a
    // QAbstractScrollArea's viewport from QPalette::Base (white, by
    // default) THROUGH the style engine rather than through the plain
    // erase-before-paint WA_NoSystemBackground suppresses - a real,
    // measured defect (checkNoBlackLine's own sweep caught it as a run of
    // non-black pixels between the cards; this app's own retired InitScreen
    // used this identical WA_NoSystemBackground-only pattern and most
    // likely carried the same white gap, unnoticed because nothing ever
    // pixel-swept it - see gui_smoke's own SelectorWindow-specific block).
    // An explicit `background: transparent` stylesheet rule is what this
    // shell's other transparent children (ItemsPanel's rows, this file's
    // own header/underRow) already use for exactly this reason, and it is
    // what actually overrides the style engine's fill.
    myScroll->setStyleSheet(QStringLiteral("background: transparent;"));
    myScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    outer->addWidget(myScroll, 1);

    myGrid = new QWidget(myScroll);
    myGrid->setAttribute(Qt::WA_NoSystemBackground);
    myGrid->setStyleSheet(QStringLiteral("background: transparent;"));
    new QGridLayout(myGrid);
    myScroll->setWidget(myGrid);

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &SelectorWindow::applyTheme);

    refresh();
}

QWidget* SelectorWindow::buildCard(const QString& id, const QString& name,
                                  const QString& thumbPath, const QDateTime& lastEdited)
{
    auto* card = new SelectorCardWidget(myGrid);
    card->setFurniture(name, thumbPath, lastEdited);
    card->onActivated = [this, id] { emit furnitureChosen(id); };
    card->onRenameRequested = [this, id, card] {
        InlineRename::beginRename(card, card->nameLabelGeometry(), card->nameText(),
                                  [this, id](QString newName) {
                                      if (!myStore.renameFurniture(id, newName)) {
                                          showFailure(tr("Couldn't rename this furniture to "
                                                        "%1 — Check that its folder still "
                                                        "exists and isn't read-only")
                                                          .arg(newName));
                                          return;
                                      }
                                      refresh();
                                  });
    };
    card->onDeleteRequested = [this, id] {
        if (!myStore.deleteFurniture(id)) {
            showFailure(tr("Couldn't delete this furniture — Check that its "
                          "folder still exists and isn't read-only"));
            return;
        }
        refresh();
    };
    return card;
}

void SelectorWindow::refresh()
{
    rebuildCards();
    relayoutCards();
}

void SelectorWindow::rebuildCards()
{
    for (const Card& c : myCards) {
        if (c.widget) c.widget->deleteLater();
    }
    myCards.clear();

    for (const FurnitureStore::FurnitureInfo& info : myStore.listFurniture()) {
        QWidget* widget = buildCard(info.id, info.name, info.thumbPath, info.lastEdited);
        Card c;
        c.id = info.id;
        c.widget = widget;
        myCards.push_back(c);
    }
}

void SelectorWindow::relayoutCards()
{
    auto* grid = qobject_cast<QGridLayout*>(myGrid->layout());
    if (!grid) return;

    while (QLayoutItem* item = grid->takeAt(0)) delete item;   // widgets themselves are kept

    const int available =
        std::max(kThumbWidth, myScroll ? myScroll->viewport()->width() : width());
    const int perRow = std::max(1, (available + kGridSpacing) / (kThumbWidth + kGridSpacing));

    grid->setHorizontalSpacing(kGridSpacing);
    grid->setVerticalSpacing(kGridSpacing);

    int row = 0, col = 0;
    std::vector<QWidget*> placedWidgets;
    for (const Card& c : myCards) {
        if (!c.widget) continue;
        grid->addWidget(c.widget, row, col);
        c.widget->show();
        placedWidgets.push_back(c.widget);
        if (++col >= perRow) { col = 0; ++row; }
    }

    // Whole-device-pixel positions - InitScreen::relayoutCards()'s own
    // reasoning: each card's own SIZE already rounds up whole, but
    // QGridLayout still lands its LOGICAL position wherever the row/column
    // arithmetic puts it, which a fractional display scale can leave on a
    // fractional device row.
    grid->activate();
    const QPoint origin = myGrid->mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    for (QWidget* w : placedWidgets) {
        w->move(Theme::snapToDevicePixels(w->x(), origin.x(), dpr),
               Theme::snapToDevicePixels(w->y(), origin.y(), dpr));
    }
}

void SelectorWindow::showFailure(const QString& text)
{
    if (!myFailureBanner) {
        myFailureBanner = new QLabel(this);
        myFailureBanner->setWordWrap(true);
        myFailureBanner->setAttribute(Qt::WA_NoSystemBackground);
        auto* outer = qobject_cast<QVBoxLayout*>(layout());
        // Index 1: just below the header (index 0), above the grid.
        if (outer) outer->insertWidget(1, myFailureBanner);
        myFailureTimer = new QTimer(this);
        myFailureTimer->setSingleShot(true);
        myFailureTimer->setInterval(kFailureMs);
        connect(myFailureTimer, &QTimer::timeout, myFailureBanner, &QWidget::hide);
    }
    myShownFailures << text;
    myFailureBanner->setText(text);
    myFailureBanner->setStyleSheet(
        QStringLiteral("background-color: %1; color: %2; border: 1px solid %2; "
                      "border-radius: 6px; padding: 8px 12px; font-size: %3pt;")
            .arg(Theme::chip().name(), Theme::danger().name())
            .arg(Theme::bodyFont().pointSizeF()));
    myFailureBanner->show();
    myFailureTimer->start();
}

QString SelectorWindow::currentFailureText() const
{
    return (myFailureBanner && myFailureBanner->isVisible()) ? myFailureBanner->text()
                                                              : QString();
}

bool SelectorWindow::failureVisible() const
{
    return myFailureBanner && myFailureBanner->isVisible();
}

QWidget* SelectorWindow::cardAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(myCards.size())) return nullptr;
    return myCards[static_cast<std::size_t>(index)].widget;
}

QString SelectorWindow::cardName(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->nameText() : QString();
}

QString SelectorWindow::cardFurnitureId(int index) const
{
    if (index < 0 || index >= static_cast<int>(myCards.size())) return QString();
    return myCards[static_cast<std::size_t>(index)].id;
}

QWidget* SelectorWindow::thumbnailAt(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->thumb() : nullptr;
}

QPushButton* SelectorWindow::renameButtonAt(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->renameButton() : nullptr;
}

QPushButton* SelectorWindow::deleteButtonAt(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->deleteButton() : nullptr;
}

QLabel* SelectorWindow::dateLabelAt(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->dateLabel() : nullptr;
}

int SelectorWindow::deleteArmedMsFor(int index) const
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    return card ? card->deleteArmedMs() : -1;
}

void SelectorWindow::beginRenameAt(int index)
{
    auto* card = static_cast<SelectorCardWidget*>(cardAt(index));
    if (card && card->onRenameRequested) card->onRenameRequested();
}

QStringList SelectorWindow::paintedTexts() const
{
    QStringList texts;
    if (myTitle) texts << myTitle->text();
    if (myNewButton) texts << myNewButton->text();
    // Rename/Delete's two labels are fixed copy, painted identically on
    // every card - one representative pair is enough, the same reasoning
    // VersionsPanel's own static deleteLabel()/deleteArmedLabel() sweep via.
    texts << SelectorCardWidget::renameLabel() << SelectorCardWidget::deleteLabel()
          << SelectorCardWidget::deleteArmedLabel();
    texts << myShownFailures;
    return texts;
    // Furniture NAMES are deliberately excluded - see the class comment.
}

void SelectorWindow::applyTheme()
{
    if (myTitle) myTitle->setStyleSheet(labelChrome(Theme::text(), Theme::titleFont(), true));
    if (myNewButton) {
        // Accent-FILLED, per the picked mockup's own words ("an accent-
        // filled '+ New furniture' button") - unlike VersionsPanel's own +
        // button, which is only accent-BORDERED. White text reads against
        // every shipped accent hue in this app's palette (all saturated,
        // none pastel), the same assumption PullArrow/BevelArrow's own
        // value chips already make for text on a coloured ground.
        myNewButton->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: white; "
                          "border: none; border-radius: 6px; padding: 4px 14px; "
                          "font-weight: 600; font-size: %2pt; } "
                          "QPushButton:hover { background-color: %1; }")
                .arg(Theme::accent().name())
                .arg(Theme::bodyFont().pointSizeF()));
    }
    for (const Card& c : myCards) {
        if (auto* card = static_cast<SelectorCardWidget*>(c.widget)) card->restyle();
    }
    update();
}

void SelectorWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::chrome());
}

void SelectorWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayoutCards();
}
