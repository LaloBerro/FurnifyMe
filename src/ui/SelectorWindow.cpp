#include "SelectorWindow.h"

#include "FurnitureStore.h"
#include "IconSet.h"
#include "InlineRename.h"
#include "Theme.h"
#include "WindowChrome.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// Landscape 16:10, per the picked mockup - the thumbnail's own width drives
// its height, the same relationship VersionsPanel's kThumbAspect already
// uses for its own cards. 260 since the Gallery redesign's "bigger preview
// cards" - the grid is capped at three columns now (see kGridColumns), so
// width no longer buys more columns, it buys a bigger preview.
constexpr int kThumbWidth = 260;
constexpr double kThumbAspect = 10.0 / 16.0;
constexpr int kCardRadius = 10;
constexpr int kUnderRowHeight = 26;
constexpr int kActionButtonHeight = 22;
constexpr int kCellSpacing = 8;     // between the thumbnail and its under-row
constexpr int kGridSpacing = 24;
constexpr int kGridMargin = 32;
// THREE columns always - the Gallery pick's own words ("a grid max 3x3 and
// if it bigger get scroll to the bottom"). A wider window gets breathing
// room, never a fourth column; a fuller library grows rows behind
// myScroll's vertical scrollbar.
constexpr int kGridColumns = 3;
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
// so this paints Theme::paintSurface(), same as every other paintSurface-
// family member, then redraws the border in accent() while hovered - same
// geometry paintSurface() already stroked in border(), so the swap fully
// replaces it rather than adding a second ring.
class ThumbCardWidget : public QWidget {
public:
    explicit ThumbCardWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        Theme::makeSurfaceTransparent(this);
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
        Theme::paintSurface(painter, rect(), kCardRadius);

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

// The "+ New furniture" CARD - the Gallery redesign moved creating into the
// grid as its permanent first cell, so the library reads as one uniform
// grid rather than a header verb above a list. Still a QPushButton, so
// SelectorWindow::newFurnitureButton() keeps its type and every existing
// caller - the handoff wiring, gui_smoke's childAt-real click probes -
// drives it unchanged; only the paint is ours: a dashed accent outline at
// exactly a furniture cell's size, filled faintly on hover, its label
// centred where a thumbnail would be.
class NewFurnitureCard : public QPushButton {
public:
    explicit NewFurnitureCard(QWidget* parent) : QPushButton(parent)
    {
        setText(tr("+ New furniture"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_NoSystemBackground);
        Theme::makeSurfaceTransparent(this);
        setFixedSize(Theme::wholeDevicePixels(
            QSize(kThumbWidth, thumbHeight() + kCellSpacing + kUnderRowHeight)));
    }

protected:
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (underMouse() || isDown()) {
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                kCardRadius, kCardRadius);
            painter.fillPath(path, Theme::chipHover());
        }
        // Through drawCrispBorder - the ONE half-pixel-alignment idiom (its
        // own header records the three local copies it retired; this card
        // briefly grew a fourth, the branch review's find) - with the
        // dashed style the accent outline wants.
        Theme::drawCrispBorder(painter, QRectF(rect()),
                               underMouse() ? Theme::accent() : Theme::border(),
                               kCardRadius, 1.0, Qt::DashLine);
        painter.setPen(Theme::accent());
        painter.setFont(Theme::bodyFont());
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
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

    // The outer shell: the custom title strip across the very top (the
    // native caption is gone - WindowChrome::attach() at the bottom of this
    // ctor), then the content column with the gallery's own margins. The
    // strip runs edge to edge, which is exactly what "integrated" means
    // here - it is the same chrome() ground as the rest of the window, with
    // the app mark, the wordmark and the three window controls on it.
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    myTitleBar = new QWidget(this);
    myTitleBar->setAttribute(Qt::WA_NoSystemBackground);
    myTitleBar->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* barLayout = new QHBoxLayout(myTitleBar);
    barLayout->setContentsMargins(12, 0, 0, 0);
    barLayout->setSpacing(8);
    auto* mark = new QLabel(myTitleBar);
    mark->setPixmap(IconSet::appMarkPixmap(18));
    // Mouse-transparent, both of them: childAt() skips a transparent
    // widget, and "the deepest child here is nothing" is precisely how the
    // hit-test lambda below decides a point on the strip is CAPTION - so
    // the mark and the wordmark stay draggable ground instead of dead
    // pixels.
    mark->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(mark);
    myBarTitle = new QLabel(tr("FurnifyMe"), myTitleBar);
    myBarTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(myBarTitle);
    barLayout->addStretch(1);
    myWindowButtons = new WindowButtons(WindowButtons::Look::Flat, myTitleBar);
    barLayout->addWidget(myWindowButtons);
    shell->addWidget(myTitleBar);

    auto* content = new QWidget(this);
    content->setAttribute(Qt::WA_NoSystemBackground);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* outer = new QVBoxLayout(content);
    // Top margin halved against the other three - the strip above already
    // contributes its own height of breathing room.
    outer->setContentsMargins(kGridMargin, kGridMargin / 2, kGridMargin, kGridMargin);
    outer->setSpacing(16);
    myContentLayout = outer;
    shell->addWidget(content, 1);

    auto* header = new QWidget(this);
    header->setAttribute(Qt::WA_NoSystemBackground);
    header->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    myTitle = new QLabel(tr("Your furniture"), header);
    headerLayout->addWidget(myTitle);
    headerLayout->addStretch(1);

    // The Gallery header's own controls (the New button moved into the grid
    // below - see NewFurnitureCard). Search filters live on every keystroke;
    // the two sort chips are one exclusive pair whose state lives in
    // mySortByName alone, restyled through applyTheme() so active/inactive
    // is derived, never a second flag.
    mySearch = new QLineEdit(header);
    mySearch->setPlaceholderText(tr("Search"));
    mySearch->setClearButtonEnabled(true);
    mySearch->setFixedSize(Theme::wholeDevicePixels(QSize(180, 28)));
    // Debounced: relayoutCards() tears the grid layout down and re-sorts
    // on every call, and a keystroke per call re-did all of it once per
    // character (the branch review's finding). 150 ms trails the typing;
    // clearing via the field's own clear button still lands through the
    // same route.
    mySearchDebounce = new QTimer(this);
    mySearchDebounce->setSingleShot(true);
    mySearchDebounce->setInterval(150);
    connect(mySearchDebounce, &QTimer::timeout, this,
            [this] { relayoutCards(); });
    connect(mySearch, &QLineEdit::textChanged, this,
            [this] { mySearchDebounce->start(); });
    headerLayout->addWidget(mySearch);

    // CHECKABLE and AUTO-EXCLUSIVE: the active-sort state lives in the
    // buttons' own checked pair (Qt's radio mechanism), never in a second
    // bool this window keeps in step - sortedByName() derives from it, the
    // toggled handler only repaints and re-lays. The branch review's
    // stored-state finding here, closed with the platform's own idiom.
    mySortRecent = new QPushButton(tr("Recent"), header);
    mySortName = new QPushButton(tr("Name"), header);
    for (QPushButton* chip : {mySortRecent, mySortName}) {
        chip->setFixedHeight(Theme::wholeDevicePixels(28));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setAutoExclusive(true);
        headerLayout->addWidget(chip);
        connect(chip, &QPushButton::toggled, this, [this](bool on) {
            if (!on) return;   // the pair fires both halves; one relayout
            applyTheme();
            relayoutCards();
        });
    }
    mySortRecent->setChecked(true);

    outer->addWidget(header);

    myScroll = new QScrollArea(this);
    myScroll->setWidgetResizable(true);
    myScroll->setFrameShape(QFrame::NoFrame);
    // Vertical scroll ONLY - the pick's own words ("if it bigger get scroll
    // to the bottom"). The window's minimum width already holds the three
    // columns, so a horizontal bar could only ever appear as an artefact of
    // the two bars stealing each other's lane, which is exactly what the
    // first build showed.
    myScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    myNewButton = new NewFurnitureCard(myGrid);
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

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &SelectorWindow::applyTheme);

    refresh();

    // Sized to what the library actually holds (user feedback on the first
    // build, which always opened at the full 3x3 and left a small library
    // over a field of empty rows): as many rows as the New card plus the
    // real furniture fill, capped at the pick's own three - the scrollbar
    // carries everything past that. Width always holds the three columns;
    // the user may still grow the window, which buys breathing room, never
    // a fourth column. Bounded by the screen the window will land on: even
    // three rows of 260-wide cards outgrow a 1080-row display at 150%
    // scaling and beyond, and a window taller than the screen is this
    // suite's own documented capture hazard as well as a real user's
    // clipped scrollbar. Sizing runs AFTER refresh(), which is what fills
    // myCards - the row count is derived from the same cards the grid just
    // laid out.
    // The arithmetic reads MEASURED sizes, not re-derived ones - the first
    // pass re-computed the cell height from the same constants the cells
    // were built from and still came up short (the cells' own
    // wholeDevicePixels rounding and the grid layout's default contents
    // margins were both missing), which put a scrollbar over a window that
    // was supposed to fit exactly. The New card IS a cell, so its measured
    // minimum height is every cell's; the grid layout's margins are asked
    // for; the header answers its own sizeHint.
    {
        const int cellCount = 1 + static_cast<int>(myCards.size());   // the New card leads
        const int rows =
            std::clamp((cellCount + kGridColumns - 1) / kGridColumns, 1, 3);
        const int cellH = myNewButton->minimumHeight();
        const QMargins gm = myGrid->layout()->contentsMargins();
        const int gridW = kGridColumns * kThumbWidth + (kGridColumns - 1) * kGridSpacing +
                          gm.left() + gm.right();
        const int gridH = rows * cellH + (rows - 1) * kGridSpacing + gm.top() + gm.bottom();
        const int chromeW = 2 * kGridMargin + 24;   // margins + the scrollbar's own lane
        const QMargins cm = outer->contentsMargins();
        const int chromeH = myTitleBar->sizeHint().height() + cm.top() + cm.bottom() +
                            outer->spacing() + header->sizeHint().height();
        QSize wanted = Theme::wholeDevicePixels(QSize(gridW + chromeW, gridH + chromeH));
        if (const QScreen* screen = QGuiApplication::primaryScreen()) {
            wanted = wanted.boundedTo(screen->availableSize() * 9 / 10);
        }
        resize(wanted);
        setMinimumWidth(std::min(wanted.width(), Theme::wholeDevicePixels(gridW + chromeW)));
    }

    // The custom title bar's native half: eat the caption, answer hit tests.
    // Any point on the strip whose deepest child is nothing (the mark and
    // the wordmark are mouse-transparent, so childAt() skips them) drags
    // the window; the maximize chip answers MaxButton so Windows 11's snap
    // layouts appear over it; everything else - the three window controls
    // included - is ordinary client content. See WindowChrome.h.
    WindowChrome::attach(this,
                         WindowChrome::captionHitTest(this, myTitleBar, myWindowButtons),
                         myWindowButtons);
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
        c.name = info.name;
        c.lastEdited = info.lastEdited;
        myCards.push_back(c);
    }
}

void SelectorWindow::relayoutCards()
{
    auto* grid = qobject_cast<QGridLayout*>(myGrid->layout());
    if (!grid) return;

    while (QLayoutItem* item = grid->takeAt(0)) delete item;   // widgets themselves are kept

    grid->setHorizontalSpacing(kGridSpacing);
    grid->setVerticalSpacing(kGridSpacing);

    // Sort a VIEW over the cards - Recent (newest edit first, a never-saved
    // furniture last) or Name, id as the tie-break so equal keys stay in
    // one stable order - then filter that view by the live search text.
    // myCards itself never reorders: it keeps the store's own enumeration,
    // which is the order every *At(index) accessor answers in.
    std::vector<const Card*> ordered;
    ordered.reserve(myCards.size());
    for (const Card& c : myCards) {
        if (c.widget) ordered.push_back(&c);
    }
    if (sortedByName()) {
        std::sort(ordered.begin(), ordered.end(), [](const Card* a, const Card* b) {
            const int byName = QString::compare(a->name, b->name, Qt::CaseInsensitive);
            return byName != 0 ? byName < 0 : a->id < b->id;
        });
    } else {
        std::sort(ordered.begin(), ordered.end(), [](const Card* a, const Card* b) {
            if (a->lastEdited.isValid() != b->lastEdited.isValid())
                return a->lastEdited.isValid();
            if (a->lastEdited != b->lastEdited) return a->lastEdited > b->lastEdited;
            return a->id < b->id;
        });
    }
    const QString needle = mySearch ? mySearch->text().trimmed() : QString();

    int row = 0, col = 0;
    std::vector<QWidget*> placedWidgets;
    auto place = [&](QWidget* widget) {
        grid->addWidget(widget, row, col, Qt::AlignLeft | Qt::AlignTop);
        widget->show();
        placedWidgets.push_back(widget);
        if (++col >= kGridColumns) { col = 0; ++row; }
    };
    // The New card leads the grid, always - creating is the one action a
    // search must never filter away.
    if (myNewButton) place(myNewButton);
    for (const Card* c : ordered) {
        if (!needle.isEmpty() && !c->name.contains(needle, Qt::CaseInsensitive)) {
            c->widget->hide();
            continue;
        }
        place(c->widget);
    }
    // Park the slack: an empty stretch column past the third and an empty
    // stretch row past the last keep a wider or taller viewport from
    // spreading the fixed-size cells apart instead of leaving the grid
    // packed to the top left.
    grid->setColumnStretch(kGridColumns, 1);
    grid->setRowStretch(row + (col > 0 ? 1 : 0), 1);

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
        // Index 1: just below the header (index 0), above the grid - in the
        // CONTENT column, since layout() is the outer shell holding the
        // title strip now.
        if (myContentLayout) myContentLayout->insertWidget(1, myFailureBanner);
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

bool SelectorWindow::sortedByName() const
{
    return mySortName && mySortName->isChecked();
}

int SelectorWindow::visibleCardCount() const
{
    int count = 0;
    for (const Card& c : myCards) {
        if (c.widget && !c.widget->isHidden()) ++count;
    }
    return count;
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
    if (myBarTitle) texts << myBarTitle->text();
    if (myNewButton) texts << myNewButton->text();
    if (mySearch) texts << mySearch->placeholderText();
    if (mySortRecent) texts << mySortRecent->text();
    if (mySortName) texts << mySortName->text();
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
    if (myBarTitle)
        myBarTitle->setStyleSheet(labelChrome(Theme::text(), Theme::labelFont(), true));
    if (myWindowButtons) myWindowButtons->update();
    // The New card paints itself from live tokens in its own paintEvent -
    // only a repaint is owed on a theme edit, never a stylesheet.
    if (myNewButton) myNewButton->update();
    if (mySearch) {
        mySearch->setStyleSheet(
            QStringLiteral("QLineEdit { background-color: %1; color: %2; border: 1px "
                          "solid %3; border-radius: 8px; padding: 2px 9px; "
                          "font-size: %4pt; }")
                .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name())
                .arg(Theme::labelFont().pointSizeF()));
    }
    // The two sort chips: the active one wears chipActive() under an accent
    // border, the idle one the ordinary chip/border pair - derived from
    // mySortByName alone, both restyled together so the pair can never both
    // read active.
    auto chipCss = [](bool on) {
        return QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px "
                              "solid %3; border-radius: 8px; padding: 2px 12px; "
                              "font-size: %4pt;%5 } "
                              "QPushButton:hover { background-color: %6; }")
            .arg(on ? Theme::chipActive().name() : Theme::chip().name(),
                 on ? Theme::text().name() : Theme::textMuted().name(),
                 on ? Theme::accent().name() : Theme::border().name())
            .arg(Theme::labelFont().pointSizeF())
            .arg(on ? QStringLiteral(" font-weight: 600;") : QString(),
                 Theme::chipHover().name());
    };
    if (mySortRecent) mySortRecent->setStyleSheet(chipCss(!sortedByName()));
    if (mySortName) mySortName->setStyleSheet(chipCss(sortedByName()));
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

void SelectorWindow::closeEvent(QCloseEvent* event)
{
    // Report the gesture and let the base class accept it normally - this
    // window genuinely does close (unlike MainWindow, which never does; see
    // its own closeEvent()). What "closing the app's own picker window"
    // MEANS is EditorSelectorHandoff::wire()'s call, not this class's -
    // see the header on closing() for why.
    emit closing();
    QWidget::closeEvent(event);
}
