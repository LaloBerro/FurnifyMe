#include "ItemsPanel.h"

#include <cstdio>

#include "DocumentModel.h"
#include "IconSet.h"
#include "InlineRename.h"
#include "ItemsFilterField.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {
// The card's width AT THE SHIPPED TYPE SCALE. Fixed rather than
// min/preferred: this is a floating drawer anchored beside a fixed-width
// rail, and a card that changed width with the longest body name would move
// the viewport's usable area around under the user. cardWidth() grows it by
// what a larger base size actually costs, and by nothing else.
// 240 while rows carried a dimension string beside the name; 200 since sizes
// moved to the viewport's selection-sizes drawing and a row became a name and
// an eye button - narrowed by the user's call ("adjust the width a little
// bit"), and kept wide enough that an ordinary renamed piece still fits.
constexpr int kBaseWidth = 200;
// What kBaseWidth was chosen to hold: a typical renamed piece beside the eye
// button. A specimen, not live content - see cardWidth().
QString nameSpecimen() { return QStringLiteral("Left side panel"); }
// The same radius the rail wears (ToolCluster's kCardRadius), not the
// family's default 8: the two cards sit side by side against the same top
// edge, and a different corner between immediate neighbours reads as a
// mistake rather than as a distinction.
constexpr int kRadius = 10;
constexpr int kPad = Theme::Space::Snug;
// The height the empty state genuinely needs - title, the "no bodies yet"
// message wrapped at this width, and the padding around them. Used as a
// FLOOR rather than only as the empty layout's natural size, so the card does
// not visibly shrink the moment the first body arrives and grow again when it
// is deleted. A drawer that changes shape on every edit reads as a glitch.
constexpr int kMinHeight = 176;
// How far one level of the folder tree indents a row (improvements item 10).
// 12 is the twisty's own width plus its gap, so a folder's contents line up
// under its name rather than under its twisty.
constexpr int kIndentPx = Theme::Space::Snug;
// How far the pointer has to travel before a press on a row becomes a DRAG
// rather than a click. Qt's own startDragDistance() is 10 at default settings
// and is about dragging OUT of an application; a row moving inside a 200px
// drawer wants less.
constexpr int kDragThresholdPx = 6;
// What a row keeps between its own rounded background and the content inside
// it - and therefore what the TITLE row must keep too, or the title and its
// button do not line up with the list below (see the titleRow margins).
constexpr int kRowInset = 6;
// Every small icon button in this drawer - a row's eye, a folder's eye, and
// the + in the title - is this size. They sit in one column, so a difference
// between them reads as a misalignment however carefully their edges agree.
constexpr int kRowButtonPx = 24;
// What the drawer leaves between its bottom edge and the viewport's -
// ViewportOverlay::kEdgeMargin plus a hair, so a full-height list still reads
// as a floating card rather than as something wedged into the corner.
constexpr int kBottomClearance = 16;
// The eye every visibility button in this drawer wears: open while the
// thing is on screen (the button checked), struck through while it is hidden.
// The 2026-10 review's F14 - these rows wore the Body glyph, which said
// nothing about seeing.
QIcon eyeIcon()
{
    return IconSet::toggleIcon(IconSet::Glyph::Eye, IconSet::Glyph::EyeHidden);
}

// A row's name. A QLabel that answers its size from the PLAIN name in either
// format, measured exactly as QLabel measures plain text: the filter's
// highlight is the same words in the same font with one run recoloured, so
// it is the same size - but a rich-text QLabel works that out by laying out
// a QTextDocument, three times per hint, and at the filter's first letter
// that ran for a hundred rows per keystroke (measured at about 0.08 ms a row,
// most of the keystroke budget). The plain answer is a font-metrics call.
class RowNameLabel : public QLabel {
public:
    RowNameLabel(const QString& name, QWidget* parent) : QLabel(name, parent), myPlain(name)
    {
        setTextFormat(Qt::PlainText);
    }
    // What the row is CALLED - the text the hint is measured from, whatever
    // the label is currently showing.
    void setPlainName(const QString& name)
    {
        if (name == myPlain) return;
        myPlain = name;
        myHint = QSize();
        updateGeometry();
    }
    QSize sizeHint() const override
    {
        if (textFormat() == Qt::PlainText) return QLabel::sizeHint();
        return plainHint();
    }
    QSize minimumSizeHint() const override
    {
        if (textFormat() == Qt::PlainText) return QLabel::minimumSizeHint();
        // QLabel's own minimum for unwrapped plain text is its single line.
        return plainHint();
    }

protected:
    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            myHint = QSize();
        QLabel::changeEvent(event);
    }

private:
    // QLabelPrivate::sizeForWidth(-1) for a plain, unwrapped, unindented
    // label: the text's bounding box with the label's alignment less its
    // centring, plus the margins.
    QSize plainHint() const
    {
        if (myHint.isValid()) return myHint;
        ensurePolished();
        const int flags = int((alignment() & ~(Qt::AlignVCenter | Qt::AlignHCenter)) |
                              Qt::TextExpandTabs);
        const QRect box = fontMetrics().boundingRect(0, 0, -1, 2000, flags, myPlain);
        const QMargins m = contentsMargins();
        myHint = QSize(box.width() + 2 * margin() + m.left() + m.right(),
                       box.height() + 2 * margin() + m.top() + m.bottom())
                     .expandedTo(minimumSize());
        return myHint;
    }
    QString myPlain;
    mutable QSize myHint;
};

// The filter's highlight: the matched run in accentText, every part ESCAPED
// first - a user name is data, and "<b>Top</b>" must read as typed (REVIEW
// FOCUS 2). white-space:pre keeps a name's own double spaces.
QString highlighted(const QString& name, const QString& query)
{
    const int at = query.isEmpty() ? -1 : name.indexOf(query, 0, Qt::CaseInsensitive);
    if (at < 0)
        return QStringLiteral("<span style=\"white-space:pre\">%1</span>").arg(name.toHtmlEscaped());
    return QStringLiteral(
               "<span style=\"white-space:pre\">%1<span style=\"color:%2\">%3</span>%4</span>")
        .arg(name.left(at).toHtmlEscaped(), Theme::accentText().name(),
             name.mid(at, query.size()).toHtmlEscaped(),
             name.mid(at + query.size()).toHtmlEscaped());
}

}  // namespace

ItemsPanel::ItemsPanel(DocumentModel* document, OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myDocument(document)
    , myView(view)
{
    // A floating card, painted in paintEvent() rather than filled by a
    // stylesheet: a stylesheet background is a square, and this card has
    // rounded corners and a border. See the header for the two rules that
    // follow from sitting over OCCT's GL surface instead of inside a
    // splitter.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    setFixedWidth(cardWidth());

    myOuter = new QVBoxLayout(this);
    myOuter->setContentsMargins(kPad, kPad, kPad, kPad);
    myOuter->setSpacing(8);

    // The title row: the word, and the one control this list needs - a + that
    // makes a folder out of whatever is selected. It sits where the hover x
    // used to (that x is gone from THIS drawer by the user's call; the rail
    // chip and the menu entry still close it).
    auto* titleRow = new QHBoxLayout();
    // THE SAME SIDE INSET EVERY ROW TAKES, so the title and its button line up
    // with the list underneath rather than with the card's own edge. A row
    // insets its content by kRowInset either side (it paints a rounded
    // background behind itself, and the content must not touch that shape);
    // the title row had none, so its + sat kRowInset further right than every
    // row's own trailing button and the icons did not form a column. Read from
    // the same constant the rows use rather than repeated as a literal, so the
    // two cannot drift apart again.
    titleRow->setContentsMargins(kRowInset, 0, kRowInset, 0);
    titleRow->setSpacing(6);
    myTitle = new QLabel(tr("Items"), this);
    titleRow->addWidget(myTitle, 1);
    myNewFolder = new QPushButton(this);
    // THE SAME SIZE THE ROWS' OWN BUTTONS ARE. It was 20 against their 24,
    // and because a button centres its glyph two right-aligned buttons of
    // different widths put their ICONS in different columns even when their
    // edges line up - which is half of why this control never looked square
    // with the list. One size for one kind of control.
    myNewFolder->setFixedSize(kRowButtonPx, kRowButtonPx);
    myNewFolder->setCursor(Qt::PointingHandCursor);
    myNewFolder->setFocusPolicy(Qt::NoFocus);
    myNewFolder->setIcon(IconSet::icon(IconSet::Glyph::Folder));
    myNewFolder->setToolTip(tr("New folder from the selected bodies"));
    myNewFolder->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    connect(myNewFolder, &QPushButton::clicked, this,
            [this] { emit newFolderRequested(); });
    titleRow->addWidget(myNewFolder);
    myOuter->addLayout(titleRow);
    myTitle->show();   // measured immediately, same reason as the rows in refresh()
    myNewFolder->show();

    // The filter (T-063): permanent, under the title. Escape in it, and the
    // hidden-pick line's Clear filter (Task 8), trigger ONE action.
    myFilter = new ItemsFilterField(this);
    myOuter->addWidget(myFilter);
    myFilter->show();
    myClearFilterAction = new QAction(tr("Clear filter"), this);
    connect(myClearFilterAction, &QAction::triggered, this, [this] { myFilter->clear(); });
    connect(myFilter, &QLineEdit::textChanged, this, &ItemsPanel::setFilter);
    connect(myFilter, &ItemsFilterField::escapePressed, myClearFilterAction, &QAction::trigger);

    // The rows live inside a scroll area now (improvements item 12: "the items
    // list is too long and it does not have a scroll bar, can you add an
    // invisible scroll bar?"). Invisible is literal - both bars are off, so
    // nothing is drawn, nothing is clickable, and the card's own painted
    // rectangle is unchanged. The wheel scrolls it, which is what a
    // QAbstractScrollArea does regardless of whether its bars are shown.
    //
    // Transparent all the way down, through the per-widget stylesheet this
    // app's other transparent children already use (SelectorWindow's grid,
    // ItemsPanel's own rows): the style engine fills a scroll area and its
    // viewport with the window colour otherwise, which would stamp an opaque
    // square over the card's rounded corners.
    myScroll = new QScrollArea(this);
    myScroll->setFrameShape(QFrame::NoFrame);
    myScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setWidgetResizable(true);
    // NOTHING here asks the scroll area how tall it should be, and that is
    // deliberate: QScrollArea::sizeHint() answers with a CACHED copy of its
    // widget's hint bounded to about 24 text lines, and it ignores
    // sizeAdjustPolicy entirely - so the moment the rows moved inside one,
    // the card's height stopped following the list and parked at four rows
    // with the fifth cut in half. sizeHint() below measures the ROWS' own
    // layout instead and never consults this widget at all.
    myScroll->setMinimumHeight(0);
    myScroll->setAttribute(Qt::WA_NoSystemBackground);
    myScroll->setStyleSheet(QStringLiteral("background: transparent;"));
    myScroll->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    myScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* rowHost = new QWidget(myScroll);
    rowHost->setAttribute(Qt::WA_NoSystemBackground);
    rowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    myRows = new QVBoxLayout(rowHost);
    myRows->setContentsMargins(0, 0, 0, 0);
    myRows->setSpacing(2);
    myRows->addStretch(1);
    myScroll->setWidget(rowHost);
    myOuter->addWidget(myScroll, 1);

    // What the filter says when it matches nothing. PLAIN text: the query in
    // it is the user's own, and a "<b>" typed there must read as typed.
    myNoMatch = new QLabel(this);
    myNoMatch->setTextFormat(Qt::PlainText);
    myNoMatch->setWordWrap(true);
    myNoMatch->hide();
    myOuter->addWidget(myNoMatch);

    // refresh() FIRST and applyTheme() second, and the order is load-bearing
    // both ways: refresh() builds the rows applyTheme() then restyles, and
    // applyTheme() walks myRowList, which refresh() is what fills. FORCED,
    // although a freshly constructed child is still hidden: the document is
    // empty at this point, so the build is the empty state alone, and the
    // card then measures right before anything has shown it.
    myForceBuild = true;
    refresh();
    myForceBuild = false;
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ItemsPanel::applyTheme);
}

int ItemsPanel::cardWidth()
{
    // The DIFFERENCE between what a specimen row measures now and what it
    // measured under the shipped scale, added to the width that scale was
    // designed around. At defaultSpec() the two measurements are identical
    // and this returns exactly kBaseWidth, so opening the Appearance panel
    // and closing it again cannot nudge the drawer; at 13pt it returns
    // whatever the bigger type actually needs.
    //
    // Deriving from the ACTUAL longest body name instead would resize the
    // drawer every time a body was made or deleted, which is the behaviour
    // the fixed width exists to prevent. Specimens keep it stable.
    const Theme::Spec shipped = Theme::defaultSpec();
    auto measure = [](const QFont& name) {
        return QFontMetrics(name).horizontalAdvance(nameSpecimen());
    };
    const int now = measure(Theme::bodyFont());
    const int atShippedScale = measure(Theme::bodyFontFor(shipped));
    return std::max(kBaseWidth, kBaseWidth + now - atShippedScale);
}

qint64 ItemsPanel::rowKey(RowKind kind, int id)
{
    return (static_cast<qint64>(kind) << 32) | static_cast<quint32>(id);
}

qint64 ItemsPanel::rowKey(const Row& row)
{
    return rowKey(row.isGroup ? RowKind::Folder : (row.isOutline ? RowKind::Outline : RowKind::Body),
                  row.id);
}

bool ItemsPanel::takeReusable(RowKind kind, int id, Row& out)
{
    if (!myReusable) return false;
    const auto at = myReusable->find(rowKey(kind, id));
    if (at == myReusable->end()) return false;
    // Taken OUT of the map: what is left in it afterwards is exactly the rows
    // whose items are gone, which is what refresh() then destroys.
    out = at->second;
    myReusable->erase(at);
    return true;
}

QLineEdit* ItemsPanel::filterField() const { return myFilter; }

bool ItemsPanel::rowIsMutedAt(int index) const
{
    const Row* row = visibleRow(index);
    return row && row->muted;
}

QLineEdit* ItemsPanel::openRenameEditor() const
{
    // Every QLineEdit under this card that is not the filter: InlineRename
    // opens its edit as a child of the row it renames.
    for (QLineEdit* edit : findChildren<QLineEdit*>())
        if (edit != myFilter) return edit;
    return nullptr;
}

bool ItemsPanel::rowMatches(const Row& row) const
{
    // A LITERAL, case-blind substring - never a pattern, so "(l" and "50%"
    // find the names that hold them.
    return row.text.contains(myFilterText, Qt::CaseInsensitive);
}

QColor ItemsPanel::nameColour(const Row& row) const
{
    // Muted for two reasons, one look: kept on screen only by the filter, or
    // the item itself is not drawn (its eye, or Isolate).
    return (row.muted || !row.onScreen) ? Theme::textMuted() : Theme::text();
}

void ItemsPanel::styleNameColour(Row& row)
{
    if (!row.name) return;
    const QString sheet = QStringLiteral("background: transparent; color: %1;")
                              .arg(nameColour(row).name(QColor::HexArgb));
    if (row.nameSheet != sheet) {
        row.name->setStyleSheet(sheet);
        row.nameSheet = sheet;
    }
}

bool ItemsPanel::styleNameWords(Row& row, bool apply)
{
    if (!row.name) return false;
    // Every row's name is a RowNameLabel (addItemRow()/addGroupRow()), which
    // measures itself from the plain name - so what this writes never moves
    // a row, and writing it a moment later moves nothing either.
    static_cast<RowNameLabel*>(row.name)->setPlainName(row.text);
    const bool highlight = row.matched && filterActive();
    const Qt::TextFormat format = highlight ? Qt::RichText : Qt::PlainText;
    const QString text = highlight ? highlighted(row.text, myFilterText) : row.text;
    if (row.name->textFormat() == format && row.name->text() == text) return false;
    if (!apply) return true;
    if (row.name->textFormat() != format) {
        // Emptied first: QLabel::setTextFormat() re-sets whatever text the
        // label holds in the NEW format, which for a plain name going rich
        // would be the user's own text parsed, for a moment, as markup.
        row.name->clear();
        row.name->setTextFormat(format);
    }
    if (row.name->text() != text) row.name->setText(text);
    return false;
}

void ItemsPanel::flushNameWords()
{
    if (!myWordsPending) return;
    myWordsPending = false;
    for (Row& row : myRowList) styleNameWords(row, /*apply=*/true);
}

void ItemsPanel::setFilter(const QString& text)
{
    const QString query = text.trimmed();
    if (query == myFilterText) return;
    myFilterText = query;
    // Built and up to date first: a closed drawer defers its rebuild, and a
    // filter applied to a stale list would be applied to rows that are gone.
    ensureFresh();
    applyRowVisibility();
    relayoutCard();
    restyleRows();
}

void ItemsPanel::setGrainLook(QPushButton* grain, bool across)
{
    grain->setIcon(IconSet::icon(across ? IconSet::Glyph::GrainAcross : IconSet::Glyph::GrainAlong));
    grain->setToolTip(across ? tr("The grain runs across this body — click to turn it back")
                             : tr("The grain runs along this body — click to turn it"));
}

void ItemsPanel::flushPendingRefresh()
{
    if (!myRowsDirty) return;
    myForceBuild = true;
    refresh();
    myForceBuild = false;
}

void ItemsPanel::ensureFresh() const
{
    // The suite reads rows whether or not the drawer is open, and a closed
    // drawer defers its rebuild (refresh()); a reader must never see a list
    // older than the document it is asking about.
    if (myRowsDirty) const_cast<ItemsPanel*>(this)->flushPendingRefresh();
    // ...nor a row whose words are still waiting for the event loop (see
    // applyRowVisibility()).
    if (myWordsPending) const_cast<ItemsPanel*>(this)->flushNameWords();
}

void ItemsPanel::setVisible(bool visible)
{
    // Brought up to date BEFORE it is shown, so the overlay - which places
    // the card from sizeHint() right after the Items action shows it - never
    // measures a stale list.
    if (visible) flushPendingRefresh();
    QWidget::setVisible(visible);
}

void ItemsPanel::showEvent(QShowEvent* event)
{
    // The other way a closed-then-reopened drawer comes back: a parent shown
    // (showChildren()), which does not go through setVisible().
    flushPendingRefresh();
    QWidget::showEvent(event);
}

const ItemsPanel::Row* ItemsPanel::visibleRow(int index) const
{
    ensureFresh();
    if (index < 0 || index >= static_cast<int>(myVisible.size())) return nullptr;
    return &myRowList[myVisible[static_cast<std::size_t>(index)]];
}

int ItemsPanel::rowCount() const
{
    ensureFresh();
    return static_cast<int>(myVisible.size());
}

QPushButton* ItemsPanel::twistyAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->twisty : nullptr;
}

QString ItemsPanel::rowTextAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->text : QString();
}

QWidget* ItemsPanel::rowWidgetAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->widget : nullptr;
}

int ItemsPanel::rowIdAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->id : 0;
}

QPushButton* ItemsPanel::grainMarkAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->grain : nullptr;
}

bool ItemsPanel::rowIsOutlineAt(int index) const
{
    const Row* row = visibleRow(index);
    return row && row->isOutline;
}

bool ItemsPanel::rowIsGroupAt(int index) const
{
    const Row* row = visibleRow(index);
    return row && row->isGroup;
}

int ItemsPanel::rowDepthAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->depth : 0;
}

void ItemsPanel::relayoutCard()
{
    // See refresh()'s own comment on why BOTH layouts are invalidated by
    // hand: QBoxLayout caches its hint behind a flag only its own
    // addItem/removeItem sets, so a row shown or hidden inside myRows leaves
    // myOuter handing back the hint it computed last time.
    myRows->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    updateGeometry();
    adjustSize();
}

void ItemsPanel::syncTwisty(Row& row, bool open, bool heldByFilter)
{
    if (!row.twisty || (row.twistyOpen == open && row.twistyHeld == heldByFilter)) return;
    // The two chevrons are rasterised ONCE per theme (chevron()), not per
    // call: IconSet::icon() renders eight pixmaps, and the filter's first
    // letter can force-open every folder in one keystroke.
    if (row.twistyOpen != open) row.twisty->setIcon(chevron(open));
    row.twistyOpen = open;
    row.twistyHeld = heldByFilter;
    // While the filter holds it the twisty does nothing (see addGroupRow()),
    // and says why rather than looking like a control that is broken.
    row.twisty->setToolTip(heldByFilter ? filterHeldTip()
                                        : (open ? tr("Close this folder") : tr("Open this folder")));
    row.twisty->setCursor(heldByFilter ? Qt::ArrowCursor : Qt::PointingHandCursor);
}

const QIcon& ItemsPanel::chevron(bool open)
{
    QIcon& slot = open ? myChevronOpen : myChevronClosed;
    if (slot.isNull())
        slot = IconSet::icon(open ? IconSet::Glyph::ChevronDown : IconSet::Glyph::ChevronRight);
    return slot;
}

QString ItemsPanel::filterHeldTip()
{
    return tr("Shown open by the filter — clear the filter to fold it");
}

void ItemsPanel::applyRowVisibility()
{
    myVisible.clear();
    const bool filtering = filterActive();
    // THE FILTER (T-063) is a second answer to the same question - which rows
    // are on screen - so it lives here rather than in a visibility mechanism
    // of its own. While it holds a query a row shows when its name matches,
    // or when it is a folder a match lies beneath: those folders are
    // FORCE-OPENED for as long as the query stands, without a word written to
    // myExpanded, so emptying the field puts back exactly the folds the user
    // had.
    std::unordered_set<int> keepFolders;
    if (filtering && myDocument) {
        for (Row& row : myRowList) {
            row.matched = rowMatches(row);
            if (!row.matched) continue;
            for (int folder = row.parentId; folder > 0; folder = myDocument->groupOf(folder))
                keepFolders.insert(folder);
        }
    }
    // folder id -> its row is shown AND it is open. The tree is built depth
    // first, so a folder's entry exists before any of its children ask.
    std::unordered_map<int, bool> openFolder;
    // ONE layout pass for the whole batch, not one per row shown. Qt's
    // QWidget::setVisible(true) activates the parent's layout before the
    // child appears, so showing a hundred rows (the filter's first letter at
    // 130 rows) laid the whole list out a hundred times - measured at about a
    // quarter of a millisecond per row. QLayout::activate() is a no-op while
    // its layout is disabled, so the batch runs disabled and is activated
    // once, synchronously, below - nothing that reads a row's geometry
    // straight after a fold sees it stale.
    myRows->setEnabled(false);
    for (std::size_t i = 0; i < myRowList.size(); ++i) {
        Row& row = myRowList[i];
        bool shown = false;
        if (filtering) {
            const bool kept = row.isGroup && keepFolders.count(row.id) > 0;
            shown = row.matched || kept;
            // Muted: on screen only because a match lies beneath it.
            row.muted = shown && !row.matched;
            if (row.isGroup) syncTwisty(row, kept, /*heldByFilter=*/true);
        } else {
            row.matched = false;
            row.muted = false;
            shown = row.parentId == 0 || openFolder[row.parentId];
            if (row.isGroup) {
                openFolder[row.id] = shown && isGroupExpanded(row.id);
                syncTwisty(row, isGroupExpanded(row.id));
            }
        }
        // A HIDDEN row is QWidgetItem::isEmpty(), so it contributes nothing
        // to myRows' sizeHint() and the card is measured from the visible
        // rows alone. Only touched when it actually changes: setVisible() on
        // a row already in that state still costs a layout request.
        if (row.widget->isHidden() == shown) row.widget->setVisible(shown);
        if (shown) myVisible.push_back(i);
        styleNameColour(row);
    }
    myRows->setEnabled(true);
    myRows->activate();
    // THE WORDS, NOW FOR WHAT CAN BE SEEN AND A MOMENT LATER FOR THE REST.
    // A highlight is a rich-text label, and turning a plain label rich builds
    // a text control per label: at the filter's first letter that is a
    // hundred of them, each announcing half a dozen child objects to every
    // application event filter - the largest single share of the keystroke.
    // Only rows that can be on screen at the next paint need theirs now:
    // anything from the list's top down to the current scroll offset plus the
    // tallest the card may be (rowsHeightCap()). The rest are written by
    // flushNameWords() from the event loop, and any reader asking first
    // (ensureFresh()) flushes them itself. Safe to split because a row's
    // name measures itself from its plain name (RowNameLabel): the deferred
    // words cannot move a single row.
    const int cap = rowsHeightCap();
    const int bandBottom =
        cap > 0 ? (myScroll ? myScroll->verticalScrollBar()->value() : 0) + cap : -1;
    // Where each row will SIT is added up from the rows' own hints, never
    // read off their geometry: the row host is resized to the new list only
    // when the scroll area next lays it out, so right now a hundred rows can
    // be squeezed into the old height and every one of them reads as "near
    // the top".
    bool deferred = false;
    int top = 0;
    for (Row& row : myRowList) {
        bool inBand = bandBottom < 0;
        if (!inBand && !row.widget->isHidden()) {
            inBand = top < bandBottom;
            top += row.widget->sizeHint().height() + myRows->spacing();
        }
        if (inBand) styleNameWords(row, /*apply=*/true);
        else if (styleNameWords(row, /*apply=*/false)) deferred = true;
    }
    if (deferred && !myWordsPending) {
        myWordsPending = true;
        QMetaObject::invokeMethod(this, &ItemsPanel::flushNameWords, Qt::QueuedConnection);
    }
    // Nothing matched: one line says so, rather than a card with no rows in
    // it and no reason. Only when there ARE rows to match - an empty document
    // keeps its own empty state.
    if (myNoMatch) {
        const bool none = filtering && myVisible.empty() && !myRowList.empty();
        if (none) {
            myNoMatch->setText(tr("Nothing matches \"%1\"").arg(myFilterText));
            // Measured and pinned here, at the width it will have, for the
            // reason the empty state's label is (see sizeHint()).
            const int width = std::max(1, cardWidth() - kPad * 2);
            myNoMatch->setFixedWidth(width);
            myNoMatch->setFixedHeight(QFontMetrics(Theme::bodyFont())
                                          .boundingRect(QRect(0, 0, width, 10000),
                                                        Qt::TextWordWrap, myNoMatch->text())
                                          .height());
        }
        if (myNoMatch->isHidden() == none) myNoMatch->setVisible(none);
    }
}

void ItemsPanel::applyTheme()
{
    // Restyle in place - NOT refresh(). See the header: a theme edit arrives
    // once per mouse move inside the colour picker, and rebuilding every row
    // per frame is work that changes nothing about which bodies exist.
    if (myTitle) {
        // A per-widget stylesheet wins over the app-wide one regardless of
        // selector specificity, so the size sticks reliably here - this is a
        // panel title, Theme::titleFont(). `background: transparent` is not
        // decoration: the app-wide sheet paints every QWidget chrome-black,
        // and a label that stamps its own rectangle over this card would be a
        // black bar across it.
        myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                              "font-weight: 600; font-size: %2pt;")
                                   .arg(Theme::textMuted().name())
                                   .arg(Theme::titleFont().pointSizeF()));
    }

    // The chevrons are pixels rasterised from text(): re-rasterised from the
    // new tokens and handed to every twisty.
    myChevronOpen = QIcon();
    myChevronClosed = QIcon();
    for (Row& row : myRowList) {
        if (row.twisty) row.twisty->setIcon(chevron(row.twistyOpen));
        // The name's colour is styleNameColour()'s, re-run below: its cache is
        // dropped here so every name is re-read from the new tokens (muted
        // while off screen or kept only by the filter, text() otherwise).
        row.nameSheet.clear();
        // Rasterised out of text()/textDisabled() when it was built, so it is
        // pixels rather than a description - the same cached-appearance value
        // ToolChip::applyTheme() has to rebuild. Every eye is the same pair
        // now, a folder's included (it used to be re-dressed as a body's here
        // after wearing the New-folder glyph on its first paint); the grain
        // mark is rebuilt in whichever direction its body runs.
        if (row.eye) row.eye->setIcon(eyeIcon());
        if (row.grain && myDocument)
            row.grain->setIcon(IconSet::icon(myDocument->bodyGrainAcross(row.id)
                                                 ? IconSet::Glyph::GrainAcross
                                                 : IconSet::Glyph::GrainAlong));
    }

    // The empty state's message and the filter's no-match line read
    // bodyFont() through their own stylesheets, so both are restyled here by
    // name - never by a sweep over every QLabel, which would now reach the
    // rows' names too.
    const QString quietLine = QStringLiteral("background: transparent; color: %1; "
                                             "font-size: %2pt;")
                                  .arg(Theme::textMuted().name())
                                  .arg(Theme::bodyFont().pointSizeF());
    if (myEmpty) myEmpty->setStyleSheet(quietLine);
    if (myNoMatch) myNoMatch->setStyleSheet(quietLine);
    if (myFilter) myFilter->applyTheme();
    // Every name re-styled (styleNameColour() and styleNameWords(), from the dropped caches above) and
    // the no-match line re-measured at the new type size - one pass, the
    // same one a fold or a keystroke takes.
    applyRowVisibility();

    // The type scale moved, so both of this card's dimensions have to be
    // re-derived: its width from cardWidth(), its height from the layout that
    // has just been handed bigger labels.
    setFixedWidth(cardWidth());
    myRows->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    updateGeometry();
    adjustSize();

    if (myView) showSelection(myView->selectedSolidIds());
}

int ItemsPanel::rowsHeightCap() const
{
    // EVERYTHING from where this card actually sits down to the viewport's
    // bottom edge, less one margin. It went three fifths, then four, and the
    // answer the user kept asking for is simply "as tall as there is room
    // for" - a share of the height always leaves a band of empty viewport
    // under a list that is still scrolling.
    //
    // Reading the card's own placed y() is safe rather than circular: this
    // card is anchored TopLeft, and where the overlay puts its TOP does not
    // depend on how tall it is (it hangs under the app bar), so a cap derived
    // from it lands on the same number every relayout. Before the first
    // placement y() is 0 and the eighth below stands in - one frame, then the
    // real number.
    const QWidget* host = parentWidget();
    if (!host || host->height() <= 0) return 0;
    const int top = y() > 0 ? y() : host->height() / 8;
    return std::max(kMinHeight, host->height() - top - kBottomClearance);
}

QSize ItemsPanel::sizeHint() const
{
    const int cw = width() > 0 ? width() : cardWidth();
    if (!myOuter || !myRows) return QSize(cw, kMinHeight);

    // Added up from the parts, NOT asked of myOuter: the scroll area between
    // this card and its rows answers sizeHint() with a cached, line-bounded
    // number of its own (see the constructor), and a layout containing one
    // reports whatever that says. The rows' own layout is the only thing that
    // knows how tall the list is.
    //
    // sizeHint, NOT heightForWidth, and that is load-bearing: the rows live
    // inside a widgetResizable QScrollArea, which resizes its content widget
    // to its own viewport - so asking that content's layout what height it
    // wants AT A WIDTH re-enters the very layout pass that is asking, and the
    // recursion runs the stack out. It took the whole suite down at its first
    // window (a stack overflow before a single check printed), and only the
    // suite, because the app's editor is never shown on the empty document
    // whose word-wrapped empty-state message is what made the layout
    // width-dependent at all.
    //
    // Nothing is lost by it: the one wrapping widget in there - the empty
    // state - is given its own measured height where it is built, so every
    // item in this layout reports a plain, width-independent hint.
    const int rows = myRows->sizeHint().height();
    const int title = myTitle ? myTitle->sizeHint().height() : 0;
    const int gap = myOuter->spacing();
    int wanted = kPad * 2 + title + gap + rows;
    // The filter field always, and the no-match line while it shows - each
    // its own hint plus the gap the layout puts above it.
    if (myFilter) wanted += myFilter->sizeHint().height() + gap;
    if (myNoMatch && !myNoMatch->isHidden()) wanted += myNoMatch->height() + gap;
    wanted = std::max(wanted, kMinHeight);
    // CAPPED (improvements item 12): past this the rows scroll inside the
    // card rather than the card running off the bottom of the viewport, which
    // is what a list of two dozen slats did.
    const int cap = rowsHeightCap();
    if (cap > 0) wanted = std::min(wanted, cap);
    return QSize(cw, wanted);
}

void ItemsPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    // The rows and labels above paint no background of their own, so this
    // rounded card is what shows between and behind them. Outside the
    // rounded shape - this widget's own four corners - paintSurface() paints
    // nothing at all, and Theme::makeSurfaceTransparent() in the constructor
    // is what keeps the app-wide stylesheet from painting a flat square
    // there instead: the live scene shows through genuinely, not a colour
    // standing in for it.
    Theme::paintSurface(painter, rect(), kRadius);
}

void ItemsPanel::refresh()
{
    // CLOSED DRAWER, NO WORK (review ruling R5a). appStateChanged fires on
    // every edit whether or not the drawer is open, and a list nobody can see
    // has nothing to keep up to date - so a closed drawer only remembers that
    // it is stale, and setVisible(true) / showEvent() / any suite accessor
    // brings it up to date before anything reads it. isHidden(), not
    // !isVisible(): a drawer the user left OPEN inside a window that is not
    // on screen yet is still open, and still builds.
    if (isHidden() && !myForceBuild) {
        myRowsDirty = true;
        return;
    }
    myRowsDirty = false;

    // What the rows would SAY if they were rebuilt right now: id, name and
    // the eye's state, for every body in order.
    //
    // This exists because refresh() is driven by appStateChanged, which fires
    // at the end of every updateActions() - including the one a Theme edit
    // ends with, and a colour picker emits one per mouse MOVE. Rebuilding the
    // whole list per frame of a drag changes nothing the user can see.
    // Moving the connection to documentChanged instead is NOT the fix: a row
    // no longer paints a dimension (sizes moved to the viewport's own
    // selection-sizes drawing), but it still dims when the body it names
    // goes off screen, and Isolate reaches that through the VIEW rather than
    // through a document edit - see the view-visibility term below, which is
    // exactly the case appStateChanged, and not documentChanged, is needed
    // for.
    QString signature;
    if (myDocument) {
        // The FOLDER TREE first (improvements item 10): which folders exist,
        // what they are called and where they hang. All three are things a
        // row shows, and the Phase-5 lesson this function's own
        // comment records is that a field a row shows but the early-out does
        // not compare is a field that stops updating.
        for (const DocumentModel::Group& group : myDocument->groups()) {
            signature += QStringLiteral("g") + QString::number(group.id) + QLatin1Char('') +
                         QString::fromStdString(group.name) + QLatin1Char('') +
                         QString::number(group.parent) + QLatin1Char('');
        }
        // THE STORED ORDER (T-063): a reorder changes which row sits where,
        // so the early-out has to see it - the Phase-5 lesson again. Which
        // folders are OPEN is deliberately NOT here any more: opening one is
        // visibility (applyRowVisibility()), never a rebuild.
        for (const auto& kv : myDocument->storedOrder()) {
            signature += QStringLiteral("o") + QString::number(kv.first) + QLatin1Char(':');
            for (int id : kv.second) signature += QString::number(id) + QLatin1Char(',');
            signature += QLatin1Char('\x1e');
        }
        // Outlines first, exactly as the rows are built below. Everything a
        // row DISPLAYS goes into the signature - id, name, visibility - which
        // is the Phase-5 lesson: a field a row shows but the early-out does
        // not compare is a field that stops updating.
        for (const DocumentModel::Outline& outline : myDocument->outlines()) {
            signature += QString::number(outline.id) + QLatin1Char('\x1f') +
                         QString::fromStdString(outline.name) + QLatin1Char('\x1f') +
                         (myDocument->isVisible(outline.id) ? QLatin1Char('1')
                                                            : QLatin1Char('0')) +
                         // AND WHICH FOLDER IT IS IN - the fourth time this one
                         // lesson has had to be applied here. The folder TREE is in
                         // the signature above, but which folder each ITEM hangs in
                         // was not - and that is what decides a row's indent and its
                         // place in the list. Dragging a row into a folder therefore
                         // changed the rows while the signature said nothing had
                         // changed, so the early-out below kept the pre-drag list on
                         // screen. The user's report: "the item list is not being
                         // updated correctly when i move something".
                         QString::number(myDocument->groupOf(outline.id)) +
                         QLatin1Char('\x1e');
        }
        for (const DocumentModel::Solid& solid : myDocument->solids()) {
            signature += QString::number(solid.id) + QLatin1Char('\x1f') +
                         QString::fromStdString(solid.name) + QLatin1Char('\x1f') +
                         (myDocument->isVisible(solid.id) ? QLatin1Char('1')
                                                          : QLatin1Char('0')) +
                         // The VIEW's composed answer too, not only the
                         // document's flag: a row's name dims when the body
                         // is off screen, and Isolate hides bodies through
                         // the view alone - the Phase-5 lesson this comment
                         // block already records, applied to the field the
                         // dimming added ("a field a row shows but the
                         // early-out does not compare is a field that stops
                         // updating"). This term is why refresh() still has
                         // to stay on appStateChanged rather than move to
                         // documentChanged - dropping it is exactly the
                         // regression that lesson was learned from.
                         ((myView && myView->isSolidVisible(solid.id)) ? QLatin1Char('1')
                                                                       : QLatin1Char('0')) +
                         // AND WHICH WAY ITS GRAIN RUNS - the third time this one
                         // lesson has had to be applied to a new field: a body row
                         // draws the grain mark from the document, so a row shows
                         // it, so the early-out has to compare it. Without this
                         // term the mark kept whatever glyph and tooltip it was
                         // built with and the click appeared to do nothing - caught
                         // by the check that reads the tooltip back, not by the one
                         // that reads the document.
                         (myDocument->bodyGrainAcross(solid.id) ? QLatin1Char('1')
                                                               : QLatin1Char('0')) +
                         // AND WHICH FOLDER IT IS IN - see the outline loop above for
                         // why. This is the term the drag-and-drop gesture actually
                         // moves, and without it the drawer kept the pre-drag list.
                         QString::number(myDocument->groupOf(solid.id)) +
                         QLatin1Char('\x1e');
        }
    }
    // myRowsBuilt, not `signature.isEmpty()`: an empty document produces an
    // empty signature, and the very first call has to build the empty state
    // rather than mistake "nothing has been built" for "nothing changed".
    if (myRowsBuilt && signature == myRowSignature) {
        // Selection is NOT part of the signature - it changes far more often
        // than the list does and is a stylesheet swap rather than a rebuild -
        // so it is applied on this path too.
        if (myView) showSelection(myView->selectedSolidIds());
        return;
    }
    myRowSignature = signature;
    myRowsBuilt = true;

    // REUSE, NOT REBUILD (review ruling R5a). Every row of the tree exists
    // (folding is visibility), so a wholesale teardown cost a widget
    // construction per row - about a millisecond each, 130 of them at the
    // author's size - for an eye toggle that changes ONE row. The rows are
    // keyed by kind and id instead: a row whose item is still here keeps its
    // widget and has only what changed written into it (name, the eye, the
    // grain mark, the indent), a new item gets a new row, and only a row
    // whose item is GONE is destroyed. Kind is part of the key because ids
    // restart with every furniture opened - a body 3 in one and a folder 3 in
    // the next are different rows.
    std::unordered_map<qint64, Row> reusable;
    for (const Row& row : myRowList) reusable.emplace(rowKey(row), row);
    myRowList.clear();
    myVisible.clear();
    myReusable = &reusable;
    // The tree, from the root down - folders and the items inside them, to
    // any depth (improvements item 10). A flat document has no folders, so
    // this walks straight into the two item loops and builds exactly the
    // list it always did.
    if (myDocument) buildGroupRows(0, 0);
    myReusable = nullptr;

    // What is left in the map named an item that no longer exists.
    for (auto& entry : reusable) {
        QWidget* widget = entry.second.widget;
        myRows->removeWidget(widget);
        // hide() FIRST, and it is not tidiness. deleteLater() leaves the
        // widget alive, parented and - crucially - still visible until
        // control returns to the event loop, so between this rebuild and
        // that moment the card paints the previous list underneath the
        // new one and the stale rows are still hit-test targets. Caught
        // in a magnified render of the drawer: "Body 01" was drawn
        // straight over the empty state's "No bodies yet" it had just
        // replaced. deleteLater() itself stays - refresh() is reachable
        // from an eye button's own toggled handler, so deleting these
        // outright would destroy the widget whose signal is being
        // emitted.
        widget->hide();
        widget->deleteLater();
    }
    // The empty state is a plain child of myRows with no entry in myRowList;
    // it goes the moment there is a row to show, on the same hide-first terms.
    if (myEmpty && !myRowList.empty()) {
        myRows->removeWidget(myEmpty);
        myEmpty->hide();
        myEmpty->deleteLater();
        myEmpty = nullptr;
    }
    // The layout in the list's own order: row i at layout index i. A row
    // already in place is not touched, so a rebuild that reordered nothing
    // moves nothing.
    for (std::size_t i = 0; i < myRowList.size(); ++i) {
        QWidget* widget = myRowList[i].widget;
        if (myRows->indexOf(widget) == static_cast<int>(i)) continue;
        myRows->removeWidget(widget);
        myRows->insertWidget(static_cast<int>(i), widget);
    }
    // AND THE CHILD ORDER, which is not the layout's: a reused row keeps its
    // place in its parent's children() while a new one is appended, so a row
    // created for an item that lands mid-list would sit at the END of the
    // widget tree - the order a screen reader walks, and the order every
    // findChildren() scan reads. When the rows' child order no longer
    // matches the list, raise() each in list order, which moves it to the end
    // of its parent's children. Rows on their way to deleteLater() may sit
    // between them; a subsequence match is all that is asked.
    if (QWidget* host = myRows->parentWidget()) {
        std::size_t next = 0;
        for (QObject* child : host->children())
            if (next < myRowList.size() && child == myRowList[next].widget) ++next;
        if (next != myRowList.size())
            for (const Row& row : myRowList) row.widget->raise();
    }
    if (!myDocument) return;
    applyRowVisibility();

    if (myRowList.empty() && !myEmpty) {
        auto* empty = new QLabel(tr("No bodies yet.\n\nPress Ctrl+K and click points on "
                                    "the ground to draw your first outline."),
                                 this);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignTop);
        // MEASURED HERE, at the width it will actually have, and pinned - so
        // this label's height never depends on a layout pass asking it a
        // question mid-pass (see sizeHint()). Measured with the font it
        // paints with, which is the one applyTheme() sets on it.
        {
            const int contentWidth = std::max(1, cardWidth() - kPad * 2);
            const QFontMetrics metrics(Theme::bodyFont());
            const int wrapped = metrics
                                    .boundingRect(QRect(0, 0, contentWidth, 10000),
                                                  Qt::TextWordWrap, empty->text())
                                    .height();
            empty->setFixedWidth(contentWidth);
            empty->setFixedHeight(wrapped);
        }
        empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
        myRows->insertWidget(myRows->count() - 1, empty);
        empty->show();   // same reason as the rows above
        myEmpty = empty;
    }

    // The card is as tall as its content, and the overlay places it from
    // sizeHint(), so a row added or removed has to re-measure it here rather
    // than waiting for the next viewport resize.
    //
    // Both layouts are invalidated by hand first, and that is not belt and
    // braces. QBoxLayout caches its size hint behind a per-layout `dirty`
    // flag that only its OWN addItem/removeItem sets; QLayout::update(),
    // which is what a child layout calls on the way up, clears the parent's
    // "activated" bit and nothing else. So rebuilding the rows inside
    // myRows left myOuter handing back the hint it computed the first time
    // anyone asked - the empty state's - and the drawer stayed at that
    // height with eight bodies listed in it. Measured, not reasoned about:
    // sizeHint() reported 325 while the widget sat at 176.
    relayoutCard();

    if (myView) showSelection(myView->selectedSolidIds());
}

void ItemsPanel::addItemRow(int id, const QString& itemName, bool visible, bool isOutline,
                            int depth, int parentId)
{
    // The name reads MUTED when the item is not actually on screen -
    // hidden by its own eye, or (a body only) by Isolate's session
    // filter, which the eye's checked state deliberately does not
    // reflect (the eye is the document's persisted choice; the filter
    // is the session's). Asked of the view, the one place the composed
    // answer lives, so the row cannot disagree with the viewport
    // (Milestone 5 feedback: "the items list is not displaying well
    // when an item is isolated or turned off").
    const bool onScreen = visible && (isOutline || !myView || myView->isSolidVisible(id));
    const bool across = !isOutline && myDocument && myDocument->bodyGrainAcross(id);

    // A row this item already had: write only what changed (refresh()).
    if (Row entry; takeReusable(isOutline ? RowKind::Outline : RowKind::Body, id, entry)) {
        // The label's words are styleNameWords()'s (applyRowVisibility() runs after
        // every build): writing the name straight in here would hand a label
        // that may be in RichText - the filter's highlight - unescaped markup.
        entry.text = itemName;
        entry.onScreen = onScreen;   // its colour is styleNameColour()'s, in applyRowVisibility()
        if (entry.eye->isChecked() != visible) {
            // Blocked: this is the document's state being SHOWN, not the user
            // asking for a change - the toggled handler writes the document.
            const QSignalBlocker quiet(entry.eye);
            entry.eye->setChecked(visible);
        }
        if (entry.grain && entry.grainAcross != across) {
            setGrainLook(entry.grain, across);
            entry.grainAcross = across;
        }
        if (entry.depth != depth) {
            entry.widget->layout()->setContentsMargins(kRowInset + depth * kIndentPx, 4,
                                                       kRowInset, 4);
            entry.depth = depth;
        }
        entry.parentId = parentId;
        myRowList.push_back(entry);
        return;
    }

    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    // Indented by how deep in the folder tree it sits (improvements item 10).
    // The INDENT is the only thing depth changes about a row: everything else
    // - height, fill, the eye, the rename gesture - is identical at every
    // level, because it is the same row in the same list.
    layout->setContentsMargins(kRowInset + depth * kIndentPx, 4, kRowInset, 4);
    layout->setSpacing(8);

    auto* name = new RowNameLabel(itemName, row);
    // PLAIN text (RowNameLabel sets it): a name is the user's own, and
    // "<b>Top</b>" reads as typed. Its colour - muted while off screen, see
    // above - is styleNameColour()'s, applied by applyRowVisibility() after every
    // build.
    // Mouse-TRANSPARENT - Task 5's own fix, the same trap CLAUDE.md
    // documents for InitCardWidget: Qt delivers a click to the DEEPEST
    // widget under the cursor, not to an ancestor whose eventFilter
    // happens to be watching for one, so without this a click landing on
    // the name's own text - exactly where a double-click-to-rename
    // gesture is aimed - never reached this row's eventFilter at all.
    // Neither label has an interactive child of its own to lose by this.
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(name, 1);

    auto* eye = new QPushButton(row);
    eye->setCheckable(true);
    eye->setChecked(visible);
    eye->setFixedSize(kRowButtonPx, kRowButtonPx);
    // No keyboard focus, like every chip-sized control in the app - see
    // ToolChip's init().
    eye->setFocusPolicy(Qt::NoFocus);
    eye->setIcon(eyeIcon());
    eye->setToolTip(isOutline ? tr("Show or hide this outline") : tr("Show or hide this body"));
    connect(eye, &QPushButton::toggled, this, [this, id, isOutline](bool show) {
        // DocumentModel owns visibility now (Task 1's isVisible()/
        // setVisible()); the view is a mirror of it, written second so a
        // save reads back exactly what the eye buttons show rather than
        // a copy that only ever lived in the viewport.
        if (myDocument) myDocument->setVisible(id, show);
        if (myView) {
            if (isOutline) myView->setOutlineVisible(id, show);
            else myView->setSolidVisible(id, show);
        }
        // Announced so MainWindow can compose this write with the
        // session filter (Isolate) at its one visibility writer - the
        // direct write above used to be the ONLY consequence, and with
        // Isolate active it showed a non-isolated body straight through
        // the filter (the branch review's finding).
        emit visibilityToggled();
    });
    layout->addWidget(eye);

    // WHICH WAY THE GRAIN RUNS, on every BODY row. The user picked this shape
    // from a drawn round over a dial on the wood and a row in the material
    // card: "Every piece's direction visible at once, without selecting
    // anything." That is what the column buys and it is why the mark is on
    // the row rather than anywhere else - a whole cabinet reads down one
    // list.
    //
    // Not on an OUTLINE row: an outline is not wood yet, and a control that
    // does nothing on half the rows in a list is worse than one that is only
    // on the rows it means something for.
    QPushButton* grainButton = nullptr;
    if (!isOutline) {
        auto* grain = new QPushButton(row);
        grain->setFixedSize(kRowButtonPx, kRowButtonPx);
        grain->setFocusPolicy(Qt::NoFocus);
        setGrainLook(grain, across);
        // NOT checkable, deliberately: a checked pill would say "this body is
        // special", and neither direction is. The GLYPH is the whole state,
        // which is also what makes the column readable at a glance.
        connect(grain, &QPushButton::clicked, this, [this, id] { emit grainToggled(id); });
        layout->addWidget(grain);
        grainButton = grain;
    }

    // Clicking anywhere on the row activates that item - a body row
    // selects it in the viewport, an outline row makes it the one Extrude
    // will consume.
    row->installEventFilter(this);
    row->setProperty(isOutline ? "outlineId" : "solidId", id);
    // Named so showSelection()'s stylesheet can address THIS widget
    // rather than the whole subtree: an unqualified rule set on a widget
    // applies to its children too, which would hand the eye button the
    // row's selection fill and lose its own chrome with it.
    row->setObjectName(QStringLiteral("itemsRow"));

    // Before the trailing stretch, which is what keeps a short list at the
    // top of the card instead of spread down it.
    myRows->insertWidget(myRows->count() - 1, row);
    // Shown explicitly, and this is not redundant. A widget constructed
    // with a parent starts hidden, and Qt only reveals it when the event
    // loop gets round to the layout request - but QWidgetItem::isEmpty()
    // is `isHidden()`, so until that happens the row contributes a size
    // of (0, 0) and the card measures itself as if the list were empty.
    row->show();

    Row entry;
    entry.widget = row;
    entry.name = name;
    entry.eye = eye;
    entry.grain = grainButton;
    entry.id = id;
    entry.isOutline = isOutline;
    entry.depth = depth;
    entry.parentId = parentId;
    entry.onScreen = onScreen;
    entry.grainAcross = across;
    entry.text = name->text();
    myRowList.push_back(entry);
}

void ItemsPanel::addGroupRow(int groupId, const QString& groupName, int depth, int parentId)
{
    // A FOLDER row (improvements item 10): a twisty, the folder's name, and
    // one eye that speaks for everything under it. It is the same row an item
    // gets - same height, same fill when marked, same rename gesture - because
    // it IS a row in the same list, not a header the list happens to contain.
    //
    // The folder's eye READS the bodies under it: on only while every one of
    // them is on, so a folder with something hidden inside does not claim
    // otherwise. Derived, never stored - a folder has no visibility of its
    // own that could drift from its contents'.
    bool allVisible = true;
    if (myDocument) {
        for (int bodyId : myDocument->bodiesUnderGroup(groupId)) {
            if (!myDocument->isVisible(bodyId)) {
                allVisible = false;
                break;
            }
        }
    }

    // A row this folder already had: write only what changed (refresh()).
    // The twisty is applyRowVisibility()'s, which runs after every build.
    if (Row entry; takeReusable(RowKind::Folder, groupId, entry)) {
        entry.text = groupName;   // the label follows in styleNameWords() - see addItemRow()
        if (entry.eye->isChecked() != allVisible) {
            const QSignalBlocker quiet(entry.eye);   // shown, not asked for
            entry.eye->setChecked(allVisible);
        }
        if (entry.depth != depth) {
            entry.widget->layout()->setContentsMargins(kRowInset + depth * kIndentPx, 4,
                                                       kRowInset, 4);
            entry.depth = depth;
        }
        entry.parentId = parentId;
        myRowList.push_back(entry);
        return;
    }

    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(kRowInset + depth * kIndentPx, 4, kRowInset, 4);
    layout->setSpacing(6);

    auto* twisty = new QPushButton(row);
    twisty->setFixedSize(16, 16);
    twisty->setFlat(true);
    twisty->setCursor(Qt::PointingHandCursor);
    twisty->setFocusPolicy(Qt::NoFocus);
    const bool open = isGroupExpanded(groupId);
    twisty->setIcon(chevron(open));
    twisty->setToolTip(open ? tr("Close this folder") : tr("Open this folder"));
    twisty->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    connect(twisty, &QPushButton::clicked, this, [this, groupId] {
        // INERT while the filter holds the folder open (ruling R7a): the
        // filter ignores myExpanded, so a click here would change nothing on
        // screen and still record a fold the user never saw - which the
        // drawer would then act on the moment the filter cleared.
        if (filterActive()) return;
        setGroupExpanded(groupId, !isGroupExpanded(groupId));
    });
    layout->addWidget(twisty);

    // PLAIN text, the user's word (RowNameLabel); coloured by styleNameColour().
    auto* name = new RowNameLabel(groupName, row);
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(name, 1);

    auto* eye = new QPushButton(row);
    eye->setCheckable(true);
    eye->setChecked(allVisible);   // read off the bodies under it - see above
    eye->setFixedSize(kRowButtonPx, kRowButtonPx);
    eye->setFocusPolicy(Qt::NoFocus);
    // The same eye every item row wears. It was the Folder glyph - the very
    // mark the title row's New folder button shows - so the one control on a
    // folder row that hides things looked like the one that makes folders.
    eye->setIcon(eyeIcon());
    eye->setToolTip(tr("Show or hide everything in this folder"));
    connect(eye, &QPushButton::toggled, this, [this, groupId](bool show) {
        if (!myDocument) return;
        // Every body at any depth under it, through the SAME two writes an
        // item row's own eye makes - the document first, the view second -
        // rather than a second visibility mechanism that folders alone use.
        for (int bodyId : myDocument->bodiesUnderGroup(groupId)) {
            myDocument->setVisible(bodyId, show);
            if (myView) myView->setSolidVisible(bodyId, show);
        }
        emit visibilityToggled();
    });
    layout->addWidget(eye);

    row->installEventFilter(this);
    row->setProperty("groupId", groupId);
    row->setObjectName(QStringLiteral("itemsRow"));
    myRows->insertWidget(myRows->count() - 1, row);
    row->show();

    Row entry;
    entry.widget = row;
    entry.name = name;
    entry.eye = eye;
    entry.twisty = twisty;
    entry.id = groupId;
    entry.isGroup = true;
    entry.depth = depth;
    entry.parentId = parentId;
    entry.twistyOpen = open;
    entry.text = groupName;
    myRowList.push_back(entry);
}

void ItemsPanel::buildGroupRows(int groupId, int depth)
{
    if (!myDocument) return;
    // EVERY row of the tree, open or folded, in the order the document
    // resolves (T-063): explicit sequence first, then the automatic order
    // (folders, outlines, bodies). Folding is visibility
    // (applyRowVisibility()), so opening a folder - a twisty, spring-loading
    // mid-drag, a reveal - creates no widget at all, and the row a drag was
    // pressed on is never destroyed under the hand.
    for (int id : myDocument->orderedChildren(groupId)) {
        if (myDocument->groupExists(id)) {
            addGroupRow(id, QString::fromStdString(myDocument->groupNameOf(id)), depth, groupId);
            buildGroupRows(id, depth + 1);
        } else if (myDocument->containsOutline(id)) {
            addItemRow(id, QString::fromStdString(myDocument->outlineNameOf(id)),
                       myDocument->isVisible(id), /*isOutline=*/true, depth, groupId);
        } else if (myDocument->contains(id)) {
            addItemRow(id, QString::fromStdString(myDocument->nameOf(id)),
                       myDocument->isVisible(id), /*isOutline=*/false, depth, groupId);
        }
    }
}

bool ItemsPanel::isGroupExpanded(int groupId) const
{
    return myExpanded.find(groupId) != myExpanded.end();
}

void ItemsPanel::setGroupExpanded(int groupId, bool expanded)
{
    if (expanded == isGroupExpanded(groupId)) return;
    if (expanded) myExpanded.insert(groupId);
    else myExpanded.erase(groupId);
    // VISIBILITY, not a rebuild: every row of the tree already exists (see
    // buildGroupRows()), so opening or folding a folder only shows or hides
    // the rows under it and re-measures the card.
    applyRowVisibility();
    relayoutCard();
    restyleRows();
}

std::vector<int> ItemsPanel::selectionFor(int id, Qt::KeyboardModifiers mods) const
{
    // Ctrl toggles this row in or out of what is already selected.
    if (mods & Qt::ControlModifier) {
        std::vector<int> ids = mySelectedIds;
        const auto at = std::find(ids.begin(), ids.end(), id);
        if (at != ids.end()) ids.erase(at);
        else ids.push_back(id);
        return ids;
    }

    // Shift takes every BODY row between the anchor and this one, in the
    // order the list is drawn - which is why this lives here: nothing else
    // knows that order, and a folder's rows move in it every time one opens.
    if ((mods & Qt::ShiftModifier) && myAnchorRowId != 0) {
        int from = -1;
        int to = -1;
        // VISIBLE rows only: a folded folder's bodies are rows that exist but
        // that the user cannot see, and a range swept across a folded folder
        // takes only what was on screen between the two clicks.
        for (std::size_t v = 0; v < myVisible.size(); ++v) {
            const Row& row = myRowList[myVisible[v]];
            if (row.isGroup || row.isOutline) continue;
            if (row.id == myAnchorRowId) from = static_cast<int>(v);
            if (row.id == id) to = static_cast<int>(v);
        }
        if (from >= 0 && to >= 0) {
            if (from > to) std::swap(from, to);
            std::vector<int> ids;
            for (int v = from; v <= to; ++v) {
                const Row& row = myRowList[myVisible[static_cast<std::size_t>(v)]];
                if (row.isGroup || row.isOutline) continue;
                ids.push_back(row.id);
            }
            return ids;
        }
    }

    return {id};
}

void ItemsPanel::showRowMenu(const QPoint& globalPos, int rowId, bool isGroupRow)
{
    QMenu menu(this);
    QAction* folder = menu.addAction(tr("New folder with these"));
    QAction* rename = menu.addAction(tr("Rename"));
    QAction* ungroup = menu.addAction(tr("Ungroup"));
    // Only over a FOLDER, and worded so the two cannot be confused: Ungroup
    // dissolves the folder and keeps the wood, Delete takes both.
    QAction* remove = isGroupRow ? menu.addAction(tr("Delete folder and its contents")) : nullptr;
    // Availability, derived: a folder needs something to put in it, a rename
    // needs a row to open over, and Ungroup needs the row to BE in a folder.
    folder->setEnabled(!mySelectedIds.empty());
    rename->setEnabled(rowId != 0);
    ungroup->setEnabled(myDocument &&
                        (isGroupRow ? myDocument->groupExists(rowId)
                                    : (rowId != 0 && myDocument->groupOf(rowId) != 0)));

    const QAction* chosen = menu.exec(globalPos);
    if (chosen == folder) {
        emit newFolderRequested();
    } else if (chosen == rename && rowId != 0) {
        if (isGroupRow) beginRenameForGroup(rowId);
        else beginRenameForItem(rowId, /*isOutline=*/false);
    } else if (chosen == ungroup) {
        emit ungroupRequested();
    } else if (remove && chosen == remove) {
        emit deleteGroupRequested(rowId);
    }
}

int ItemsPanel::dropTargetAt(const QPoint& pos) const
{
    // VISIBLE rows only: a hidden row keeps whatever geometry it last had,
    // and a point over where it used to be must not drop into it.
    for (std::size_t i : myVisible) {
        const Row& row = myRowList[i];
        // Mapped, not compared: a row's own geometry() is in the scrolling
        // host's coordinates now, and `pos` is in this card's - the two agree
        // only while the list is scrolled to the top, which is exactly the
        // case a test would pass and a real drag would not.
        if (!row.widget || !row.widget->rect().contains(row.widget->mapFrom(
                               const_cast<ItemsPanel*>(this), pos)))
            continue;
        // A folder row takes the drop itself; an item row hands it to the
        // folder that item is in, so dropping BESIDE something means "in
        // there with it".
        if (row.isGroup) return row.id;
        return myDocument ? myDocument->groupOf(row.id) : 0;
    }
    return 0;   // the card's own background is the document's root
}

bool ItemsPanel::eventFilter(QObject* watched, QEvent* event)
{
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    const QVariant groupProperty = watched->property("groupId");
    const QVariant outlineProperty = watched->property("outlineId");
    const QVariant solidProperty = watched->property("solidId");
    const bool isRow =
        groupProperty.isValid() || outlineProperty.isValid() || solidProperty.isValid();

    if (event->type() == QEvent::MouseButtonPress) {
        // The press ARMS a drag as well as activating the row - both, not one
        // or the other. A press that never travels is an ordinary click and
        // has to behave exactly as it did before folders existed, so the
        // activation happens here and the move handler below only takes over
        // once the pointer has actually gone somewhere.
        if (isRow && watchedWidget) {
            myDragId = groupProperty.isValid()
                           ? groupProperty.toInt()
                           : (outlineProperty.isValid() ? outlineProperty.toInt()
                                                        : solidProperty.toInt());
            myDragIsGroup = groupProperty.isValid();
            myDragStart = watchedWidget->mapTo(this, static_cast<QMouseEvent*>(event)->position().toPoint());
            myDragging = false;
            myDropTarget = 0;
        }
        if (groupProperty.isValid()) {
            const int groupId = groupProperty.toInt();
            myDragIds = {groupId};
            // A folder row selects the BODIES under it, and Ctrl ADDS them to
            // what is already selected - which is how two folders, or a
            // folder and a loose board, end up under one gizmo. Computed here
            // for the same reason a body row's selection is: this panel is
            // what knows the tree.
            const Qt::KeyboardModifiers mods =
                static_cast<QMouseEvent*>(event)->modifiers();
            std::vector<int> ids =
                myDocument ? myDocument->bodiesUnderGroup(groupId) : std::vector<int>();
            if (mods & Qt::ControlModifier) {
                std::vector<int> merged = mySelectedIds;
                for (int id : ids) {
                    if (std::find(merged.begin(), merged.end(), id) == merged.end())
                        merged.push_back(id);
                }
                ids.swap(merged);
            }
            emit selectionRequested(ids);
            return true;
        }
        if (outlineProperty.isValid()) {
            myDragIds = {outlineProperty.toInt()};
            emit outlineActivated(outlineProperty.toInt());
            return true;
        }
        if (solidProperty.isValid()) {
            const int id = solidProperty.toInt();
            const Qt::KeyboardModifiers mods =
                static_cast<QMouseEvent*>(event)->modifiers();
            // A press on a row that is ALREADY part of a multiple selection
            // leaves that selection alone: it is the start of a drag carrying
            // all of them, and replacing the selection first would throw away
            // the very thing being dragged. A release that turns out not to
            // have travelled falls through to the plain click below.
            const bool inSelection =
                std::find(mySelectedIds.begin(), mySelectedIds.end(), id) != mySelectedIds.end();
            if (inSelection && mySelectedIds.size() > 1 && mods == Qt::NoModifier) {
                myDragIds = mySelectedIds;
                return true;
            }
            myDragIds = {id};
            emit selectionRequested(selectionFor(id, mods));
            if (!(mods & Qt::ShiftModifier)) myAnchorRowId = id;
            return true;
        }
    }

    // The drag itself (improvements item 10). Qt delivers the moves to the
    // widget the press landed on, and every row filters through here, so this
    // sees the whole gesture without a mouse grab of its own.
    if (event->type() == QEvent::MouseMove && myDragId != 0 && watchedWidget) {
        const QPoint here = watchedWidget->mapTo(this, static_cast<QMouseEvent*>(event)->position().toPoint());
        if (!myDragging && (here - myDragStart).manhattanLength() >= kDragThresholdPx)
            myDragging = true;
        if (myDragging) {
            const int target = dropTargetAt(here);
            if (target != myDropTarget) {
                myDropTarget = target;
                restyleRows();   // the folder under the pointer is marked
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease && myDragId != 0 && myDragging &&
        watchedWidget) {
        const QPoint here = watchedWidget->mapTo(this, static_cast<QMouseEvent*>(event)->position().toPoint());
        const int target = dropTargetAt(here);
        std::vector<int> dragged = myDragIds.empty() ? std::vector<int>{myDragId} : myDragIds;
        myDragId = 0;
        myDragIds.clear();
        myDragging = false;
        myDropTarget = 0;
        restyleRows();
        // A folder dropped into itself or into its own descendant is refused
        // by DocumentModel::setGroupParent(), and a row dropped into the
        // folder it is already in is a no-op there too - so this reports the
        // move and lets the one place that owns the rule answer it, rather
        // than carrying a second copy of the rule here.
        emit itemsMoveRequested(dragged, target, target == 0 ? 0 : DocumentModel::kIntoFolder);
        return true;
    }
    // Double-click renames - the FIRST press above already ran the single-
    // click activation (select the body, or make the outline pending), which
    // is harmless here: neither writes anything checkpointed, and refresh()
    // does not rebuild rows over a selection-only change (see its own
    // signature comment), so the row this rename opens over is still the
    // live widget the press just fired on. The release that follows the
    // dblclick is swallowed by the branch below, on the same terms every
    // other release on a row already is.
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (groupProperty.isValid()) {
            beginRenameForGroup(groupProperty.toInt());
            return true;
        }
        const QVariant outline = watched->property("outlineId");
        if (outline.isValid()) {
            beginRenameForItem(outline.toInt(), /*isOutline=*/true);
            return true;
        }
        const QVariant id = watched->property("solidId");
        if (id.isValid()) {
            beginRenameForItem(id.toInt(), /*isOutline=*/false);
            return true;
        }
    }
    // A widget that accepts a press must accept the release too - CLAUDE.md's
    // rule, learned from HintBalloon letting one through and having the
    // viewport re-pick underneath it. WA_NoMousePropagation on this panel
    // already stops the release reaching the viewport, but swallowing it here
    // as well means the row's own two halves are handled in one place rather
    // than one of them relying on an attribute set somewhere else.
    // The right button, over any row or over the card's own background: the
    // two folder gestures, as a menu. The Model menu's entries are the same
    // two actions - this is a second ROUTE to them, never a second
    // implementation (MainWindow answers both).
    if (event->type() == QEvent::ContextMenu) {
        auto* menuEvent = static_cast<QContextMenuEvent*>(event);
        const int rowId = groupProperty.isValid()
                              ? groupProperty.toInt()
                              : (solidProperty.isValid() ? solidProperty.toInt() : 0);
        showRowMenu(menuEvent->globalPos(), rowId, groupProperty.isValid());
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease && isRow) {
        // A press that never travelled: the click itself has already been
        // dealt with above, and all that is left is to disarm the drag it
        // armed.
        myDragId = 0;
        myDragIds.clear();
        myDragging = false;
        myDropTarget = 0;
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QStringList ItemsPanel::paintedTexts() const
{
    // Fixed app copy ONLY - never a row's name, which is the user's own
    // text. Same boundary InitScreen::paintedTexts() and
    // VersionsPanel::paintedTexts() already draw for their own user-supplied
    // names.
    return {tr("Items"), tr("Show or hide this body"), tr("Show or hide this outline"),
            tr("No bodies yet.\n\nPress Ctrl+K and click points on "
               "the ground to draw your first outline."),
            ItemsFilterField::placeholder(), tr("Clear filter"), tr("Nothing matches \"%1\""),
            filterHeldTip()};
}

void ItemsPanel::showSelection(const std::vector<int>& ids)
{
    mySelectedIds = ids;
    restyleRows();
}

void ItemsPanel::showPendingOutline(int id)
{
    myPendingOutlineId = id;
    restyleRows();
}

void ItemsPanel::beginRenameForItem(int id, bool isOutline)
{
    // Fix round 1 (review): refuse outright while this panel is not visible.
    // MainWindow's updateActions() already disables the F2 action while
    // View -> Items is off (the discoverable half - a disabled control that
    // says why), but disabling a QAction does not stop a caller from
    // invoking trigger() directly, which Qt runs regardless of isEnabled() -
    // only real shortcut/menu input respects it. This is the one place the
    // gesture actually opens a QLineEdit, so it is the one place the wedge
    // has to be impossible rather than merely discouraged: InlineRename's
    // setFocus() cannot take focus inside a hidden widget hierarchy, so an
    // edit opened here would sit with no way to commit, cancel, or lose
    // focus - and the re-entrancy guard just below would then read that
    // stray editor as "a rename is already open" and refuse every LATER
    // rename too, drawer shown or not, until an unrelated document change
    // rebuilds the rows out from under it.
    if (!isVisible()) return;

    // Fix round 1 (Task 3.2 review): refuse outright while a mirror-placement
    // gesture is live. MainWindow's canRename now excludes it too (the
    // discoverable half, matching canOpenSaveVersion()'s own exclusion of
    // the pull/bevel/extrude claims), but a disabled QAction does not stop a
    // direct trigger() call - the same reasoning the isVisible() guard just
    // above already carries for this exact function. The gesture's own
    // X/Y/Z filter additionally lets keystrokes through to a focused text
    // field now (MirrorPlacementChip::eventFilter's own guard, in
    // MainWindow.cpp), so this is belt AND suspenders, not the only thing
    // standing between a rename and a silently eaten "x".
    if (myView && myView->mirrorPlacementActive()) return;

    // Re-entrancy guard: F2 is a plain QAction shortcut, which fires
    // regardless of what currently holds focus - including the QLineEdit an
    // earlier call to this very function just opened, since that edit claims
    // only Enter/Escape (InlineRename's own filter), never F2. Without this,
    // F2 pressed twice - or F2 then a double-click on the same row - stacks a
    // SECOND independent QLineEdit on top of the first, each with its own
    // commit callback racing the other's. One rename gesture live on this
    // panel at a time, full stop. openRenameEditor(), not a bare QLineEdit
    // lookup: the filter field is a QLineEdit that is always there.
    if (openRenameEditor()) return;

    for (const Row& row : myRowList) {
        if (row.id != id || row.isOutline != isOutline) continue;
        // A row inside a FOLDED folder exists but is hidden, and an edit
        // opened over it is the wedge the isVisible() guard above describes:
        // it could never take focus. Before every row was built this row
        // simply did not exist and the call fell through to nothing - kept.
        if (!row.name || row.widget->isHidden()) return;
        // Row::text, the PLAIN name (ruling P5): while the filter holds a
        // query the label carries escaped rich text, which is not the name.
        const QString current = row.text;
        // The name label's own geometry, in row.widget's coordinates - the
        // "text cell" InlineRename opens over. Not the whole row: the eye
        // button beside it is its own control.
        const QRect cellRect = row.name->geometry();
        InlineRename::beginRename(row.widget, cellRect, current,
                                  [this, id, isOutline](QString newName) {
                                      emit renameCommitted(id, isOutline, newName);
                                  });
        return;
    }
}

void ItemsPanel::beginRenameForGroup(int groupId)
{
    // beginRenameForItem()'s three guards, for the same three reasons - see
    // its own comments. One rename gesture live on this panel at a time,
    // whichever kind of row it is over.
    if (!isVisible()) return;
    if (myView && myView->mirrorPlacementActive()) return;
    if (openRenameEditor()) return;

    for (const Row& row : myRowList) {
        if (!row.isGroup || row.id != groupId) continue;
        // Hidden inside a folded parent - see beginRenameForItem().
        if (!row.name || row.widget->isHidden()) return;
        InlineRename::beginRename(
            row.widget, row.name->geometry(), row.text,   // plain - ruling P5
            [this, groupId](QString newName) { emit groupRenameCommitted(groupId, newName); });
        return;
    }
}

void ItemsPanel::restyleRows()
{
    // The three looks a row can wear, built ONCE per call rather than once per
    // row, and each row's stylesheet set only when its look actually CHANGED
    // (review ruling R5a): setStyleSheet() re-polishes the whole row subtree
    // even when handed the string it already has, which at 130 rows was the
    // entire cost of a selection change - and of every fold, since a fold
    // restyles too.
    const QString dropLook = QStringLiteral("#itemsRow { background-color: %1; "
                                            "border: 1px solid %2; "
                                            "border-radius: 4px; }")
                                 .arg(Theme::chipHover().name(), Theme::accent().name());
    const QString markedLook = QStringLiteral("#itemsRow { background-color: %1; "
                                              "border-radius: 4px; }")
                                   .arg(Theme::chipActive().name());
    const QString plainLook = QStringLiteral("#itemsRow { background: transparent; }");
    for (Row& row : myRowList) {
        // Two ways a row can be marked, and which one applies depends on
        // which KIND of row it is - never on which list happens to contain
        // the number. A body row is marked when the viewport has it selected;
        // an outline row is marked when it is the one Extrude would consume.
        // Ids come from a single counter, so the two can never collide - but
        // styling a row from a list it is not a member of is the kind of
        // coincidence worth refusing outright rather than relying on, which
        // is why each kind asks only its own question.
        // A FOLDER row is marked when every body under it is selected - the
        // selection is bodies, always (see DocumentModel::Group), so a
        // folder's own mark is READ from that rather than stored beside it.
        bool marked = false;
        if (row.isGroup) {
            const std::vector<int> under =
                myDocument ? myDocument->bodiesUnderGroup(row.id) : std::vector<int>();
            marked = !under.empty() && std::all_of(under.begin(), under.end(), [this](int id) {
                         return std::find(mySelectedIds.begin(), mySelectedIds.end(), id) !=
                                mySelectedIds.end();
                     });
        } else {
            marked = row.isOutline
                         ? (myPendingOutlineId != 0 && row.id == myPendingOutlineId)
                         : std::find(mySelectedIds.begin(), mySelectedIds.end(), row.id) !=
                               mySelectedIds.end();
        }
        // The folder a drag is hovering over wears the accent BORDER rather
        // than the selection fill, so "this is where it would land" cannot be
        // mistaken for "this is selected".
        //
        // Otherwise the SAME fill a selected body row wears. One mark, one
        // meaning - "this is the row the next thing you do will act on" -
        // rather than a second visual language for the second kind of item.
        const bool dropTarget =
            myDragging && row.isGroup && row.id == myDropTarget && myDropTarget != 0;
        const QString& look = dropTarget ? dropLook : (marked ? markedLook : plainLook);
        if (row.style == look) continue;
        row.widget->setStyleSheet(look);
        row.style = look;
    }
}
