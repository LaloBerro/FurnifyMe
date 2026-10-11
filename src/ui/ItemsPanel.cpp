#include "ItemsPanel.h"
#include "Themes.h"

#include <cstdio>

#include "DocumentModel.h"
#include "DragGhost.h"
#include "DropIndicator.h"
#include "IconSet.h"
#include "InlineRename.h"
#include "ItemMoveText.h"
#include "ItemsFilterField.h"
#include "ItemsRow.h"
#include "KeyClaim.h"
#include "OcctViewWidget.h"
#include "Theme.h"
#include "ToolChip.h"

#include <QAccessible>
#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
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
// The height the empty state genuinely needs - title, the "nothing to list yet"
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
// How far the hand must travel past a zone's edge before the target flips
// back - a resting hand must not flicker between "above" and "into".
constexpr int kZoneHysteresisPx = 4;
// Spring-loading (T-063): a folded folder the hand rests on for this long
// opens, the Finder's own delay - long enough that sweeping across a folder
// on the way somewhere else never opens it.
constexpr int kSpringLoadMs = 700;
// Auto-scroll: a hand held within this band of the list's top or bottom edge
// scrolls it by a step per tick - about 270 px a second, slow enough to stop
// on the row wanted.
constexpr int kAutoScrollBandPx = 20;
constexpr int kAutoScrollStepPx = 8;
constexpr int kAutoScrollTickMs = 30;
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
// The ink a row a drag carries is dimmed to (T-063) - an alpha on the name's
// own token, so it still reads as that row while showing it is in the hand.
constexpr double kCarriedAlpha = 0.45;
// The eye every visibility button in this drawer wears: open while the
// thing is on screen (the button checked), struck through while it is hidden.
// The 2026-10 review's F14 - these rows wore the Body glyph, which said
// nothing about seeing.
QIcon eyeIcon()
{
    return IconSet::toggleIcon(IconSet::Glyph::Eye, IconSet::Glyph::EyeHidden);
}
// What the list says while it holds nothing (T-063): every kind of row it
// will hold, then the first step towards one. One function, so the label
// and the vocabulary sweep (paintedTexts()) read the same words.
QString emptyText()
{
    return ItemsPanel::tr("Nothing to list yet — bodies, outlines and folders you make show up "
                          "here\n\nPress Ctrl+K and click points on the ground to draw your "
                          "first outline.");
}
// "Side left, in Carcass, hidden" - the row's name, the folder it sits in and
// whether it is on screen: what a screen reader needs to know a row by
// (T-063). `visibility` is empty, ", hidden" or ", partly hidden".
QString rowAccessibleName(const QString& name, const QString& folder, const QString& visibility)
{
    QString text = name;
    if (!folder.isEmpty()) text += ItemsPanel::tr(", in %1").arg(folder);
    return text + visibility;
}
// The pending outline's words (ruling D4), at the end of its name and tooltip.
QString waitingToExtrude() { return ItemsPanel::tr(", waiting to extrude"); }
// A partly selected folder's count, in words (F18): "2 of 4 bodies in Carcass
// are selected". A partial count always has two or more bodies under it.
QString partialCountWords(int chosen, int total, const QString& folder)
{
    return chosen == 1 ? ItemsPanel::tr("1 of %1 bodies in %2 is selected").arg(total).arg(folder)
                       : ItemsPanel::tr("%1 of %2 bodies in %3 are selected")
                             .arg(chosen)
                             .arg(total)
                             .arg(folder);
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

// Escape, claimed application-wide while - and ONLY while - a drag is live:
// beginDrag() installs it and finishDrag() removes it, so the mitre, re-measure,
// boolean and joint chips keep their own Escape the rest of the time. A press
// starting the drag went to a row, never to a field, so any focus may hold
// the key; the claim follows KeyClaim's one implementation.
class ItemsPanel::DragKeyClaim : public QObject {
public:
    explicit DragKeyClaim(ItemsPanel* panel) : QObject(panel), myPanel(panel) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // A DRAG WHOSE RELEASE NEVER ARRIVES must not leave its app-wide
        // things up (ruling R10a). Qt's implicit grab sends the drag's own
        // release to the PRESSED ROW, where the row filter lands the drop -
        // so a left press or release reaching any widget outside the rows
        // means the release this drag was waiting for went somewhere else
        // (a row destroyed under the grab, focus taken away, a stale press).
        // Widgets only: a real event reaches the QWidgetWindow (a QWindow)
        // first, and the drag's own release passes through there on its way
        // to the row. The event itself is never eaten - the click does what
        // it always did.
        const QEvent::Type type = event->type();
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
                !onARow(widget))
                myPanel->cancelDrag();
            return false;
        }
        // Alt+Tab, a dialog of another app: the release will be delivered
        // to whatever the user does next, never to this drag.
        if (type == QEvent::ApplicationStateChange) {
            if (static_cast<QApplicationStateChangeEvent*>(event)->applicationState() !=
                Qt::ApplicationActive)
                myPanel->cancelDrag();
            return false;
        }
        int key = 0;
        if (!KeyClaim::claim(myPanel, watched, event, /*wantEnter=*/false,
                             /*exemptLineEdits=*/false, &key))
            return false;
        if (key == Qt::Key_Escape) myPanel->cancelDrag();
        return true;
    }

private:
    // Whether `widget` is one of this panel's rows or inside one - the rows
    // carry their item's id as a property, which is how the row filter
    // itself recognises them.
    bool onARow(const QWidget* widget) const
    {
        for (const QWidget* w = widget; w && w != myPanel; w = w->parentWidget()) {
            if (w->property("groupId").isValid() || w->property("outlineId").isValid() ||
                w->property("solidId").isValid())
                return myPanel->isAncestorOf(w);
        }
        return false;
    }

    ItemsPanel* myPanel;
};

ItemsPanel::ItemsPanel(DocumentModel* document, OcctViewWidget* view, QWidget* parent)
    // The accessibility factory is installed BEFORE QWidget's constructor
    // runs (the comma operator in the base's argument): this panel answers as
    // a Tree (F7), and an interface Qt resolved for it during its own
    // construction would be the generic one, cached for good.
    : QWidget((ItemsRowWidget::installAccessibility(), parent))
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
    // ClickFocus: a click on a row gives the list the keys; Tab never stops
    // here - the filter field, which Tab does reach, hands over to the list
    // on Down or Enter (ruling D2). It paints no ring - the selection mark
    // (fill and bar) is the focus cue (WCAG 2.4.7), because the focused row
    // IS the selected row.
    setFocusPolicy(Qt::ClickFocus);

    myOuter = new QVBoxLayout(this);
    myOuter->setContentsMargins(kPad, kPad, kPad, kPad);
    myOuter->setSpacing(8);

    // The title row: the word, and the one control this list needs - New
    // folder, which files whatever is selected (a ToolChip mirroring
    // MainWindow's action, added by setNewFolderAction()). It sits where the hover x
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
    myTitleRow = titleRow;
    myTitle = new QLabel(tr("Items"), this);
    titleRow->addWidget(myTitle, 1);
    myOuter->addLayout(titleRow);
    myTitle->show();   // measured immediately, same reason as the rows in refresh()

    // The filter (T-063): permanent, under the title. Escape in it, and the
    // hidden-pick line's Clear filter (Task 8), trigger ONE action.
    myFilter = new ItemsFilterField(this);
    myOuter->addWidget(myFilter);
    myFilter->show();
    myClearFilterAction = new QAction(tr("Clear filter"), this);
    connect(myClearFilterAction, &QAction::triggered, this, [this] {
        const int pending = myHiddenPickId;
        myFilter->clear();
        hideHiddenPick();
        if (pending != 0) reveal(pending);
    });
    connect(myFilter, &QLineEdit::textChanged, this, &ItemsPanel::setFilter);
    connect(myFilter, &ItemsFilterField::escapePressed, myClearFilterAction, &QAction::trigger);
    connect(myFilter, &ItemsFilterField::listRequested, this, &ItemsPanel::enterListFromFilter);

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
    // THE LIST TAKES FOCUS, NEVER THE SCROLL AREA INSIDE IT (review R12b).
    // QAbstractScrollArea is StrongFocus by default, so a real click on a
    // twisty, an eye, a grain mark (all NoFocus) or on blank space among the
    // rows walked up to IT, the list lost the keys and the arrows scrolled
    // instead of walking - and Tab could stop on it with nothing on screen to
    // show where. NoFocus on both, so that same click walks on up to this
    // card (ClickFocus), and Tab passes the card by entirely.
    myScroll->setFocusPolicy(Qt::NoFocus);
    myScroll->viewport()->setFocusPolicy(Qt::NoFocus);
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
    // Where a drag will land (T-063): one mark, inside the scrolling content
    // so it moves with the rows it points between.
    myIndicator = new DropIndicator(rowHost);
    // ...and what the drag carries, over the VIEWPORT so it can follow the
    // hand out of this card (where a release cancels).
    if (myView) {
        myGhost = new DragGhost(myView);
        // The viewport owns it, and may go first in a teardown - this panel's
        // destructor must not reach a ghost that is already gone.
        connect(myGhost, &QObject::destroyed, this, [this] { myGhost = nullptr; });
    }
    mySpringTimer = new QTimer(this);
    mySpringTimer->setSingleShot(true);
    connect(mySpringTimer, &QTimer::timeout, this, &ItemsPanel::springOpen);
    myAutoScrollTimer = new QTimer(this);
    myAutoScrollTimer->setInterval(kAutoScrollTickMs);
    connect(myAutoScrollTimer, &QTimer::timeout, this, &ItemsPanel::autoScrollTick);
    myOuter->addWidget(myScroll, 1);

    // What the filter says when it matches nothing. PLAIN text: the query in
    // it is the user's own, and a "<b>" typed there must read as typed.
    myNoMatch = new QLabel(this);
    myNoMatch->setTextFormat(Qt::PlainText);
    myNoMatch->setWordWrap(true);
    myNoMatch->hide();
    myOuter->addWidget(myNoMatch);

    // The hidden-pick foot: "Board 121 is hidden by the filter —" and a Clear
    // filter control that triggers the one action and holds no state.
    myFoot = new QWidget(this);
    myFoot->setAttribute(Qt::WA_NoSystemBackground);
    myFoot->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* footRow = new QHBoxLayout(myFoot);
    footRow->setContentsMargins(kRowInset, 0, kRowInset, 0);
    footRow->setSpacing(4);
    myFootText = new QLabel(myFoot);
    myFootText->setTextFormat(Qt::PlainText);
    myFootText->setWordWrap(true);
    myFootClear = new QPushButton(myClearFilterAction->text(), myFoot);
    myFootClear->setFlat(true);
    myFootClear->setFocusPolicy(Qt::NoFocus);
    myFootClear->setCursor(Qt::PointingHandCursor);
    myFootClear->setAccessibleName(myClearFilterAction->text());
    connect(myFootClear, &QPushButton::clicked, myClearFilterAction, &QAction::trigger);
    footRow->addWidget(myFootText, 1);
    footRow->addWidget(myFootClear, 0, Qt::AlignBottom);
    myFoot->hide();
    myOuter->addWidget(myFoot);

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
    QColor colour = (row.muted || !row.onScreen) ? Theme::textMuted() : Theme::text();
    // The rows a drag carries dim to 45% - an alpha on the ink, never a
    // QGraphicsOpacityEffect, which crashes over the viewport's GL surface.
    if (isCarried(row)) colour.setAlphaF(kCarriedAlpha);
    return colour;
}

void ItemsPanel::styleNameColour(Row& row)
{
    if (!row.name) return;
    QString sheet = QStringLiteral("background: transparent; color: %1;")
                        .arg(nameColour(row).name(QColor::HexArgb));
    if (const Themes::List& l = Themes::shell().list; l.on) {
        const int weight = row.isGroup ? l.folderWeight
                           : (row.widget && row.widget->marked()) ? l.selWeight
                                                                  : l.nameWeight;
        sheet += QStringLiteral(" font-weight: %1;").arg(weight);
    }
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
    // Any edit to the query retires a standing hidden-pick notice.
    myHiddenPickId = 0;
    if (myFoot && !myFoot->isHidden()) myFoot->hide();
    // Built and up to date first: a closed drawer defers its rebuild, and a
    // filter applied to a stale list would be applied to rows that are gone.
    ensureFresh();
    applyRowVisibility();
    relayoutCard();
    restyleRows();
    emit filterChanged();
}

void ItemsPanel::setGrainLook(QPushButton* grain, bool across)
{
    grain->setIcon(IconSet::icon(across ? IconSet::Glyph::GrainAcross : IconSet::Glyph::GrainAlong));
    grain->setToolTip(across ? tr("The grain runs across this body — click to turn it back")
                             : tr("The grain runs along this body — click to turn it"));
    // Its NAME carries the state (ruling P8): the mark is deliberately not
    // checkable - neither direction is "on" - so a screen reader hears the
    // direction itself rather than a toggle state.
    grain->setAccessibleName(across ? tr("Grain across") : tr("Grain along"));
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
    // older than the document it is asking about - except while a drag is
    // live, which holds the rows still until it ends (refresh() defers even a
    // forced build then): a reader mid-drag sees the list the hand is over.
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
    // A drawer going away takes a live drag with it, moving nothing - a
    // release can never reach a row that is no longer on screen.
    else cancelDrag();
    QWidget::setVisible(visible);
}

void ItemsPanel::showEvent(QShowEvent* event)
{
    // The other way a closed-then-reopened drawer comes back: a parent shown
    // (showChildren()), which does not go through setVisible().
    flushPendingRefresh();
    QWidget::showEvent(event);
}

void ItemsPanel::hideEvent(QHideEvent* event)
{
    // A release can never reach a row that is no longer on screen, so the
    // drag ends here, moving nothing - and with it the closed hand, the
    // ghost, the Escape claim and the drag line in the status bar (R10a).
    cancelDrag();
    QWidget::hideEvent(event);
}

ItemsPanel::~ItemsPanel()
{
    if (!myDragging) return;
    // No signal leaves a dying panel: MainWindow may be mid-teardown itself,
    // and the drag line it would refresh belongs to a window going away.
    const QSignalBlocker quiet(this);
    // Nor does a held refresh run here: the document and the viewport it
    // reads may already be gone.
    myRowsDirty = false;
    cancelDrag();
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
    // The same answer, as a state a screen reader hears change (F7).
    row.widget->setExpanded(open);
    // While the filter holds it the twisty does nothing (see addGroupRow()),
    // and says why rather than looking like a control that is broken.
    row.twisty->setToolTip(heldByFilter ? filterHeldTip()
                                        : (open ? tr("Close this folder") : tr("Open this folder")));
    row.twisty->setAccessibleName(twistyName(open, row.text));
    row.twisty->setCursor(heldByFilter ? Qt::ArrowCursor : Qt::PointingHandCursor);
}

QString ItemsPanel::twistyName(bool open, const QString& folderName)
{
    // What a click will do, with the folder named - a screen reader moving
    // down the list meets several of these and "Open this folder" alone
    // would not say which.
    return open ? tr("Close %1").arg(folderName) : tr("Open %1").arg(folderName);
}

void ItemsPanel::nameRow(Row& row, bool shown, bool anyShown)
{
    const QString folderName = row.parentId > 0 && myDocument
                                   ? QString::fromStdString(myDocument->groupNameOf(row.parentId))
                                   : QString();
    const QString visibility = shown ? QString()
                               : anyShown ? tr(", partly hidden")
                                          : tr(", hidden");
    // Each write only when the words moved: QWidget::setAccessibleName()
    // raises a NameChanged event per call, and a rebuild touches every row.
    const auto name = [](QWidget* widget, const QString& text) {
        if (widget && widget->accessibleName() != text) widget->setAccessibleName(text);
    };
    if (row.isGroup) {
        row.baseName = rowAccessibleName(tr("%1, folder").arg(row.text), folderName, visibility);
        writeRowName(row);
        // The eye reads checked only while EVERYTHING under it shows, and a
        // click flips that - so its name is the click's outcome.
        name(row.eye, shown ? tr("Hide everything in %1").arg(row.text)
                            : tr("Show everything in %1").arg(row.text));
        if (row.twisty) name(row.twisty, twistyName(row.twistyOpen, row.text));
        return;
    }
    row.baseName = rowAccessibleName(row.text, folderName, visibility);
    writeRowName(row);
    // The eye's checked state is the item's visibility; its name is what a
    // click will do, so the two read together ("Hide Side left", checked).
    name(row.eye, shown ? tr("Hide %1").arg(row.text) : tr("Show %1").arg(row.text));
}

void ItemsPanel::writeRowName(Row& row)
{
    // What the SELECTION adds to a row's name, after the words nameRow()
    // composed: the pending outline is "waiting to extrude" (ruling D4 - it
    // wears no mark, so its name has to carry it), and a partly selected
    // folder spells its count out (F18), since "2/4" alone says nothing aloud.
    QString text = row.baseName;
    if (row.pending) text += waitingToExtrude();
    if (!row.partialWords.isEmpty()) text += QStringLiteral(", ") + row.partialWords;
    if (row.widget->accessibleName() != text) row.widget->setAccessibleName(text);
    // The tooltip a pointer finds there says the same - and only on the
    // pending outline, the one row whose state no mark shows.
    const QString tip = row.pending ? row.text + waitingToExtrude() : QString();
    if (row.widget->toolTip() != tip) row.widget->setToolTip(tip);
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
    styleTitle();

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
    if (myFootText)
        myFootText->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
                                      .arg(Theme::textMuted().name())
                                      .arg(Theme::labelFont().pointSizeF()));
    if (myFootClear)
        myFootClear->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none; color: %1; "
                           "font-size: %2pt; padding: 0px; }")
                .arg(Theme::accentText().name())
                .arg(Theme::labelFont().pointSizeF()));
    for (Row& row : myRowList) {
        if (row.count) row.count->setStyleSheet(countSheet());
    }
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

    if (myView) applySelection(myView->selectedSolidIds());
}

QString ItemsPanel::countSheet()
{
    // The partly-selected count: words in accentText, or - in a theme that
    // draws it as a badge (themes part 2) - its mockup's pill.
    if (const Themes::List& l = Themes::shell().list; l.on) {
        return QStringLiteral("background: %1; border: 1px solid %2; border-radius: %3px; color: %4; "
                              "font-size: %5pt; font-weight: %6; padding: 0px 6px;")
            .arg((l.badgeFill.isValid() ? l.badgeFill : QColor(Qt::transparent)).name(QColor::HexArgb),
                 (l.badgeEdge.isValid() ? l.badgeEdge : QColor(Qt::transparent)).name(QColor::HexArgb))
            .arg(l.badgeRadius)
            .arg((l.badgeInk.isValid() ? l.badgeInk : Theme::accentText()).name())
            .arg(Theme::labelFont().pointSizeF())
            .arg(l.badgeWeight);
    }
    return QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
        .arg(Theme::accentText().name())
        .arg(Theme::labelFont().pointSizeF());
}

void ItemsPanel::styleTitle()
{
    if (!myTitle) return;
    // Lit in accentText while it is the drop target for the end of the top
    // level; textMuted at rest. A per-widget stylesheet wins over the
    // app-wide one regardless of selector specificity, so the size sticks
    // reliably here - this is a panel title, Theme::titleFont().
    // `background: transparent` is not decoration: the app-wide sheet paints
    // every QWidget chrome-black, and a label that stamps its own rectangle
    // over this card would be a black bar across it.
    const Themes::List& l = Themes::shell().list;
    const QColor rest = l.on && l.titleBright ? Theme::text() : Theme::textMuted();
    const QColor ink = !myTitleLit     ? rest
                       : myTitleRefused ? Theme::dangerText()
                                        : Theme::accentText();
    myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                          "font-weight: %3; font-size: %2pt;")
                               .arg(ink.name())
                               .arg(Theme::titleFont().pointSizeF())
                               .arg(l.on ? l.titleWeight : 600));
}

void ItemsPanel::setTitleLit(bool lit, bool refused)
{
    if (lit == myTitleLit && refused == myTitleRefused) return;
    myTitleLit = lit;
    myTitleRefused = refused;
    styleTitle();
}

QRect ItemsPanel::titleRowRect() const
{
    if (!myTitle) return QRect();
    return QRect(0, 0, width(), myTitle->geometry().bottom() + myOuter->spacing() / 2 + 1);
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
    if (myFoot && !myFoot->isHidden()) wanted += myFoot->sizeHint().height() + gap;
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
    // A LIVE DRAG HOLDS THE ROWS STILL (T-063, Task 11). A rebuild renumbers
    // the rows the drag's target names by index and forgets that target, so
    // the ring or line under a still hand would vanish - and a row whose item
    // went away mid-drag (an Undo) would take the grab with it. The rebuild
    // is only postponed: the same stale mark a closed drawer keeps, which
    // finishDrag() honours on every route a drag ends by.
    if (myDragging) {
        myRowsDirty = true;
        return;
    }
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
        if (myView) applySelection(myView->selectedSolidIds());
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
    // No live drag reaches this line (see the top of this function), so no
    // drag target names a row index this rebuild renumbers.
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
        // straight over the empty state's "Nothing to list yet" it had just
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
        auto* empty = new QLabel(emptyText(), this);
        // Named so a reader - the suite, an accessibility tool - can find the
        // one label that says the list is empty.
        empty->setObjectName(QStringLiteral("itemsEmpty"));
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

    if (myView) applySelection(myView->selectedSolidIds());
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
        nameRow(entry, visible, visible);
        myRowList.push_back(entry);
        return;
    }

    auto* row = new ItemsRowWidget(isOutline ? ItemsRowWidget::Kind::Outline
                                             : ItemsRowWidget::Kind::Body,
                                   this);
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
    nameRow(entry, visible, visible);
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
    // ...and whether ANY of them is, so the row's accessible name can tell
    // "hidden" from "partly hidden" (T-063). An empty folder hides nothing.
    bool anyVisible = true;
    if (myDocument) {
        const std::vector<int> bodies = myDocument->bodiesUnderGroup(groupId);
        anyVisible = bodies.empty();
        for (int bodyId : bodies) {
            if (myDocument->isVisible(bodyId)) anyVisible = true;
            else allVisible = false;
            if (anyVisible && !allVisible) break;
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
        nameRow(entry, allVisible, anyVisible);
        myRowList.push_back(entry);
        return;
    }

    auto* row = new ItemsRowWidget(ItemsRowWidget::Kind::Folder, this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(kRowInset + depth * kIndentPx, 4, kRowInset, 4);
    layout->setSpacing(6);
    row->setExpanded(isGroupExpanded(groupId));

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

    // "2/5" while the folder's subtree is PARTLY selected (T-063): words, so
    // accentText - a mark distinct from the full selection fill and from the
    // pending outline's.
    auto* count = new QLabel(row);
    count->setTextFormat(Qt::PlainText);
    count->setAttribute(Qt::WA_TransparentForMouseEvents);
    count->setStyleSheet(countSheet());
    count->hide();
    layout->addWidget(count);
    // Transparent for the mouse (a press on it is the row's), so Qt never
    // sends it a ToolTip event: the row answers for it (F18).
    row->setPassThroughTip(count);

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
    entry.count = count;
    entry.id = groupId;
    entry.isGroup = true;
    entry.depth = depth;
    entry.parentId = parentId;
    entry.twistyOpen = open;
    entry.text = groupName;
    nameRow(entry, allVisible, anyVisible);
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

std::vector<int> ItemsPanel::idsForMenu(int rowId, RowKind kind) const
{
    // A selected body row carries the whole selection, exactly as a drag
    // from it does; any other row carries itself.
    if (kind == RowKind::Body &&
        std::find(mySelectedIds.begin(), mySelectedIds.end(), rowId) != mySelectedIds.end())
        return mySelectedIds;
    return {rowId};
}

void ItemsPanel::setNewFolderAction(QAction* action)
{
    if (!action || myNewFolderChip || !myTitleRow) return;
    myNewFolderAction = action;
    // A ToolChip, so it holds no state of its own: enabled, tooltip (the
    // reason when disabled) and accessible name all follow the action on
    // every QAction::changed - the shell's one rule for a control.
    myNewFolderChip =
        new ToolChip(action, IconSet::Glyph::Folder, ToolChip::ChipMode::IconOnly, this);
    // THE SAME SIZE THE ROWS' OWN BUTTONS ARE, so its glyph sits in the eyes'
    // column: two right-aligned buttons of different widths centre their
    // icons in different columns even when their edges line up.
    myNewFolderChip->setFixedSize(kRowButtonPx, kRowButtonPx);
    myNewFolderChip->setFocusPolicy(Qt::NoFocus);
    myTitleRow->addWidget(myNewFolderChip);
    myNewFolderChip->show();   // measured immediately, as the title is
}

void ItemsPanel::populateRowMenu(QMenu& menu, int rowId, RowKind kind)
{
    QAction* folder = menu.addAction(tr("New folder with these"));
    QAction* rename = menu.addAction(tr("Rename"));
    QAction* up = menu.addAction(tr("Move up"));
    QAction* down = menu.addAction(tr("Move down"));
    // The list's keys in the menu's shortcut column (ruling D19), read off the
    // ONE table the key handling reads. Context WidgetShortcut, so the only
    // place Qt's shortcut map could match one is the menu itself while it has
    // the keyboard: nothing is registered for the window, and the list's own
    // Alt+Up/Down claim (claimsKey()) never meets a rival.
    const auto showKeys = [](QAction* entry, ListCommand command) {
        entry->setShortcut(listKeySequence(command));
        entry->setShortcutContext(Qt::WidgetShortcut);
        entry->setShortcutVisibleInContextMenu(true);
    };
    showKeys(up, ListCommand::MoveUp);
    showKeys(down, ListCommand::MoveDown);
    // MOVE INTO (ruling D1): the non-drag way to file a row in a folder that
    // already exists (WCAG 2.5.7) - every folder the carried rows may enter.
    QMenu* into = menu.addMenu(tr("Move into"));
    // Never "group" for a folder (ruling P6): this dissolves a folder and keeps
    // every piece of wood in it - the right-clicked folder, or a body row's
    // own folder (ruling RF1).
    QAction* ungroup = menu.addAction(tr("Remove folder, keep contents"));
    // Only over a FOLDER, and worded so the two cannot be confused: the entry
    // above keeps the wood, this one takes it too.
    QAction* remove =
        kind == RowKind::Folder ? menu.addAction(tr("Delete folder and its contents")) : nullptr;

    // Availability, derived: a folder needs something to put in it, a rename
    // needs a row to open over, a move needs somewhere to go (the same
    // DocumentModel answer Alt+arrows read), and removing a folder needs the
    // row to BE one or to sit in one.
    const std::vector<int> ids = idsForMenu(rowId, kind);
    DocumentModel::MoveTarget upTarget;
    DocumentModel::MoveTarget downTarget;
    if (myDocument) {
        upTarget = myDocument->siblingStepTarget(ids, -1);
        downTarget = myDocument->siblingStepTarget(ids, +1);
    }
    // EACH ENTRY ACTS ON WHAT WAS RIGHT-CLICKED (rulings RF1, RF2). Over a
    // body row "New folder with these" files the selection - which a right
    // press has just made that row, or a selection it belongs to (R9a) - so
    // it reads the action's own answer (whole bodies selected -
    // MainWindow::updateActions()), and the title chip, the Model menu and
    // this entry cannot disagree; a picked FACE reports its body in
    // mySelectedIds, and filing that body would file nothing the user chose.
    // Over a FOLDER row it wraps that folder, which needs only the folder.
    // Over an OUTLINE row it is never offered: it filed the viewport's body
    // selection, which is not the outline that was pointed at.
    const bool folderRow = kind == RowKind::Folder;
    const bool liveFolder = folderRow && myDocument && myDocument->groupExists(rowId);
    if (kind == RowKind::Outline)
        folder->setEnabled(false);
    else if (folderRow)
        folder->setEnabled(myFolderEditsAllowed && liveFolder);
    else
        folder->setEnabled(myNewFolderAction ? myNewFolderAction->isEnabled()
                                             : !mySelectedIds.empty());
    // Every kind of row renames - an outline row passed id 0 here once, so
    // its Rename sat disabled while a double-click on the same row worked.
    rename->setEnabled(rowId != 0);
    // A STEP AMONG SIBLINGS IS REFUSED WHILE FILTERED (ruling RF3): the filter
    // hides some of those siblings, so "one place up" would land beside rows
    // nobody can see - the same reason a reorder drop is not offered then.
    // Disabled with the reason, never silently.
    const bool filtering = filterActive();
    up->setEnabled(upTarget.ok && !filtering);
    down->setEnabled(downTarget.ok && !filtering);
    if (filtering) {
        up->setToolTip(ItemMoveText::filteredReorderRefusal());
        down->setToolTip(ItemMoveText::filteredReorderRefusal());
        menu.setToolTipsVisible(true);
    }
    // The folder it removes is the right-clicked one (ruling RF1): a folder
    // row's own id, a body row's own folder - never the selection's shared
    // folder, which could be a different one or none at all. Removing an
    // existing folder always does something (an empty one simply goes), so
    // "the folder exists" is the whole of the enabled state. NEVER on an
    // outline row (review R12c, kept).
    const int removeTarget =
        !myDocument || kind == RowKind::Outline || rowId == 0
            ? 0
            : (folderRow ? (liveFolder ? rowId : 0) : myDocument->groupOf(rowId));
    ungroup->setEnabled(myFolderEditsAllowed && removeTarget != 0);

    if (folderRow)
        connect(folder, &QAction::triggered, this, [this, rowId] { emit wrapFolderRequested(rowId); });
    else if (kind == RowKind::Body)
        connect(folder, &QAction::triggered, this, [this] { emit newFolderRequested(); });
    connect(rename, &QAction::triggered, this, [this, rowId, kind] {
        if (kind == RowKind::Folder) beginRenameForGroup(rowId);
        else beginRenameForItem(rowId, kind == RowKind::Outline);
    });
    // The non-drag route (WCAG 2.5.7): the same moveItems() a drop takes, so
    // MainWindow checkpoints and reports it exactly as it reports a drop.
    // A trigger() past the disabled state (QAction runs it regardless) is
    // refused here too, while filtered, rather than moving blind.
    connect(up, &QAction::triggered, this, [this, ids, upTarget] {
        if (filterActive()) {
            emit moveRefused(ItemMoveText::filteredReorderRefusal());
            return;
        }
        emit itemsMoveRequested(ids, upTarget.parent, upTarget.beforeId);
    });
    connect(down, &QAction::triggered, this, [this, ids, downTarget] {
        if (filterActive()) {
            emit moveRefused(ItemMoveText::filteredReorderRefusal());
            return;
        }
        emit itemsMoveRequested(ids, downTarget.parent, downTarget.beforeId);
    });
    // Move into's entries: each folder checkMove() answers Moved for, depth
    // first in the list's own order and indented by depth. A refused target
    // (a folder into itself or one it holds) and a no-change one (where the
    // rows already are) are left out rather than offered and refused. The
    // folder Alt+Right would choose wears that key, so the menu teaches it.
    {
        const int above = filtering ? 0 : folderAbove(rowId);
        int offered = 0;
        std::vector<std::pair<int, int>> folders;   // id, depth
        if (myDocument) {
            const std::function<void(int, int)> walk = [&](int parent, int depth) {
                for (int child : myDocument->orderedChildren(parent)) {
                    if (!myDocument->groupExists(child)) continue;
                    folders.emplace_back(child, depth);
                    walk(child, depth + 1);
                }
            };
            walk(0, 0);
        }
        for (const auto& [folderId, depth] : folders) {
            if (myDocument->checkMove(ids, folderId, DocumentModel::kIntoFolder).check !=
                DocumentModel::MoveCheck::Moved)
                continue;
            // The user's own word, with its '&' doubled so Qt never reads it
            // as a mnemonic.
            QString label = QString::fromStdString(myDocument->groupNameOf(folderId));
            label.replace(QLatin1Char('&'), QStringLiteral("&&"));
            QAction* entry = into->addAction(QString(depth * 4, QLatin1Char(' ')) + label);
            entry->setData(folderId);
            if (folderId == above) showKeys(entry, ListCommand::MoveIntoFolderAbove);
            connect(entry, &QAction::triggered, this, [this, ids, folderId] {
                if (filterActive()) {
                    emit moveRefused(ItemMoveText::filteredIntoRefusal());
                    return;
                }
                emit itemsMoveRequested(ids, folderId, DocumentModel::kIntoFolder);
            });
            ++offered;
        }
        if (offered == 0) {
            // Never an empty submenu: one disabled entry that says why.
            QAction* none = into->addAction(folders.empty() ? tr("No folders yet")
                                                            : tr("No other folder can take this"));
            none->setEnabled(false);
        }
        // RF3's rule for a move into, too (D1): the filter hides folders and
        // the siblings a row would land among.
        into->menuAction()->setEnabled(!filtering);
        if (filtering) into->menuAction()->setToolTip(ItemMoveText::filteredIntoRefusal());
    }
    if (removeTarget != 0)
        connect(ungroup, &QAction::triggered, this,
                [this, removeTarget] { emit removeFolderRequested(removeTarget); });
    if (remove)
        connect(remove, &QAction::triggered, this, [this, rowId] { emit deleteGroupRequested(rowId); });
}

void ItemsPanel::showRowMenu(const QPoint& globalPos, int rowId, RowKind kind)
{
    QMenu menu(this);
    populateRowMenu(menu, rowId, kind);
    menu.exec(globalPos);
}

// --- the keyboard (T-063) ------------------------------------------------------

bool ItemsPanel::holdsFocus() const
{
    const QWidget* top = window();
    return top && top->focusWidget() == this;
}

bool ItemsPanel::claimsKey(const QKeyEvent* key) const
{
    // Only while the list itself holds focus: a key the filter field or a
    // rename editor ignored propagates up here, and Down typed into a rename
    // must not walk the rows out from under it. Nor during a drag, whose own
    // Escape claim is the only key it has (ruling R10a).
    if (!holdsFocus() || myDragging) return false;
    // THE TABLE decides (ruling D19) - letters are never in it, so E stays
    // Extrude.
    const ListKey* entry = listKeyFor(
        QKeyCombination(key->modifiers() & ~Qt::KeypadModifier, Qt::Key(key->key())),
        /*fromFilterField=*/false);
    if (!entry) return false;
    // F2 only over a live row: with none, F2 goes on to the Rename action,
    // which renames whatever the viewport has selected.
    if (entry->command == ListCommand::Rename) return visibleIndexOf(myCurrentId) >= 0;
    return true;
}

const std::vector<ItemsPanel::ListKey>& ItemsPanel::listKeys()
{
    // One row of the ShortcutSheet's "Items list" group per entry, in this
    // order. The labels say what the KEY does, in the sheet's own register.
    static const std::vector<ListKey> table = {
        {ListCommand::Step, {QKeyCombination(Qt::Key_Up), QKeyCombination(Qt::Key_Down)},
         QT_TR_NOOP("Select the row above or below"), false},
        {ListCommand::ExtendStep,
         {QKeyCombination(Qt::ShiftModifier, Qt::Key_Up),
          QKeyCombination(Qt::ShiftModifier, Qt::Key_Down)},
         QT_TR_NOOP("Add the row above or below to the selection"), false},
        {ListCommand::FirstOrLast, {QKeyCombination(Qt::Key_Home), QKeyCombination(Qt::Key_End)},
         QT_TR_NOOP("Select the first or the last row"), false},
        {ListCommand::FoldOrParent, {QKeyCombination(Qt::Key_Left)},
         QT_TR_NOOP("Close the folder, or go to the folder the row is in"), false},
        {ListCommand::UnfoldOrChild, {QKeyCombination(Qt::Key_Right)},
         QT_TR_NOOP("Open the folder, or go to its first row"), false},
        {ListCommand::Rename, {QKeyCombination(Qt::Key_F2)}, QT_TR_NOOP("Rename the row"), false},
        {ListCommand::MoveUp, {QKeyCombination(Qt::AltModifier, Qt::Key_Up)},
         QT_TR_NOOP("Move up"), false},
        {ListCommand::MoveDown, {QKeyCombination(Qt::AltModifier, Qt::Key_Down)},
         QT_TR_NOOP("Move down"), false},
        {ListCommand::MoveOutOfFolder, {QKeyCombination(Qt::AltModifier, Qt::Key_Left)},
         QT_TR_NOOP("Move out of its folder"), false},
        {ListCommand::MoveIntoFolderAbove, {QKeyCombination(Qt::AltModifier, Qt::Key_Right)},
         QT_TR_NOOP("Move into the folder above"), false},
        {ListCommand::EnterList, {QKeyCombination(Qt::Key_Down), QKeyCombination(Qt::Key_Return)},
         QT_TR_NOOP("From the filter field, go to the list"), true},
    };
    return table;
}

const ItemsPanel::ListKey* ItemsPanel::listKeyFor(QKeyCombination combination, bool fromFilterField)
{
    // Keypad Enter is the same key as Return to the hand that presses it.
    if (combination.key() == Qt::Key_Enter)
        combination = QKeyCombination(combination.keyboardModifiers(), Qt::Key_Return);
    for (const ListKey& entry : listKeys()) {
        if (entry.fromFilterField != fromFilterField) continue;
        for (const QKeyCombination& key : entry.keys)
            if (key == combination) return &entry;
    }
    return nullptr;
}

QString ItemsPanel::listKeyLabel(const ListKey& entry) { return tr(entry.label); }

QString ItemsPanel::listKeyText(const ListKey& entry)
{
    QStringList keys;
    for (QKeyCombination key : entry.keys) {
        // "Enter", the app's word for the key (KeyClaim's "Enter applies"),
        // never NativeText's "Return" - one key, one name on every surface.
        if (key.key() == Qt::Key_Return)
            key = QKeyCombination(key.keyboardModifiers(), Qt::Key_Enter);
        keys << QKeySequence(key).toString(QKeySequence::NativeText);
    }
    return keys.join(QStringLiteral(", "));
}

QKeySequence ItemsPanel::listKeySequence(ListCommand command)
{
    for (const ListKey& entry : listKeys())
        if (entry.command == command && !entry.keys.empty()) return QKeySequence(entry.keys.front());
    return QKeySequence();
}

bool ItemsPanel::event(QEvent* event)
{
    // The keys the list claims are taken back from the shortcut map: F2's
    // action renames a body or an outline only, never a focused folder row.
    if (event->type() == QEvent::ShortcutOverride && claimsKey(static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void ItemsPanel::keyPressEvent(QKeyEvent* event)
{
    if (!claimsKey(event) || rowCount() == 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    // claimsKey() found the entry; its COMMAND decides, and the key only
    // says which way (up or down, first or last).
    const ListKey* entry = listKeyFor(
        QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(event->key())),
        /*fromFilterField=*/false);
    const bool upward = event->key() == Qt::Key_Up || event->key() == Qt::Key_Home;
    const int at = visibleIndexOf(myCurrentId);
    const int last = rowCount() - 1;
    const int next = upward ? (at <= 0 ? 0 : at - 1) : (at < 0 ? 0 : std::min(at + 1, last));
    switch (entry->command) {
    case ListCommand::Step: stepTo(next, Qt::NoModifier); break;
    case ListCommand::ExtendStep: stepTo(next, Qt::ShiftModifier); break;
    case ListCommand::FirstOrLast: stepTo(upward ? 0 : last, Qt::NoModifier); break;
    case ListCommand::FoldOrParent: foldOrParent(); break;
    case ListCommand::UnfoldOrChild: unfoldOrChild(); break;
    case ListCommand::Rename: renameCurrent(); break;
    case ListCommand::MoveUp:
    case ListCommand::MoveDown:
    case ListCommand::MoveOutOfFolder:
    case ListCommand::MoveIntoFolderAbove: moveByCommand(entry->command); break;
    case ListCommand::EnterList: break;   // the filter field's, never claimed here
    }
    event->accept();
}

int ItemsPanel::visibleIndexOf(int id) const
{
    // One id space for bodies, outlines and folders (DocumentModel::Group),
    // so the id alone names the row.
    if (id == 0) return -1;
    const int count = rowCount();
    for (int v = 0; v < count; ++v)
        if (visibleRow(v)->id == id) return v;
    return -1;
}

void ItemsPanel::stepTo(int visibleIndex, Qt::KeyboardModifiers mods)
{
    const Row* row = visibleRow(visibleIndex);
    if (!row) return;
    const int id = row->id;
    const RowKind kind =
        row->isGroup ? RowKind::Folder : (row->isOutline ? RowKind::Outline : RowKind::Body);
    // Focus IS selection: stepping selects, exactly as a click on the row.
    const bool extend = (mods & Qt::ShiftModifier) != 0;
    activateRow(id, kind, extend ? Qt::KeyboardModifiers(Qt::ShiftModifier)
                                 : Qt::KeyboardModifiers(Qt::NoModifier));
    scrollToRow(id);
    announceCurrent(extend);
}

ItemsPanel::Row* ItemsPanel::rowById(int id)
{
    for (Row& row : myRowList)
        if (row.id == id) return &row;
    return nullptr;
}

void ItemsPanel::announceCurrent(bool extend)
{
    // A KEYBOARD STEP, told to a screen reader (finding F7): the row the keys
    // are on now gets the focus, and the selection follows it - Selection
    // (it is now the selection) or SelectionAdd (a Shift step joined it).
    // The row's own state changes - selected, expanded - raise StateChanged
    // from ItemsRowWidget as they happen. Never an Alert (CLAUDE.md, F26).
    for (Row& row : myRowList) row.widget->setCurrent(row.id == myCurrentId);
    Row* row = rowById(myCurrentId);
    if (!row || row->widget->isHidden()) return;
    QAccessibleEvent focus(row->widget, QAccessible::Focus);
    QAccessible::updateAccessibility(&focus);
    QAccessibleEvent selection(row->widget, extend ? QAccessible::SelectionAdd
                                                   : QAccessible::Selection);
    QAccessible::updateAccessibility(&selection);
}

void ItemsPanel::enterListFromFilter()
{
    // DOWN OR ENTER IN THE FILTER FIELD (ruling D2): the keyboard's way into
    // the rows, which Tab passes by. Focus lands on the first row and selects
    // it - focus and selection never separate in this list. With no row to
    // land on the field keeps the keys.
    //
    // While a query stands it is the first MATCHING row (ruling DA2), never a
    // muted ancestor folder: that folder is on screen only because a match
    // lies beneath it, and selecting it would select every body under it -
    // including the ones the filter is hiding.
    int target = -1;
    for (int v = 0; v < rowCount() && target < 0; ++v)
        if (!filterActive() || !visibleRow(v)->muted) target = v;
    if (target < 0) return;
    setFocus(Qt::OtherFocusReason);
    stepTo(target, Qt::NoModifier);
}

int ItemsPanel::folderAbove(int id) const
{
    const int at = visibleIndexOf(id);
    const Row* row = visibleRow(at);
    if (!row) return 0;
    for (int v = at - 1; v >= 0; --v) {
        const Row* previous = visibleRow(v);
        // Deeper: inside an earlier sibling folder that is open - walk past.
        if (previous->depth > row->depth) continue;
        // Shallower: the row's own folder - it has no sibling above it.
        if (previous->depth < row->depth) return 0;
        return previous->isGroup ? previous->id : 0;
    }
    return 0;
}

void ItemsPanel::foldOrParent()
{
    const Row* row = visibleRow(visibleIndexOf(myCurrentId));
    if (!row) return;
    // A folder the filter holds open is not the user's to fold (ruling R7a).
    if (row->isGroup && isGroupExpanded(row->id) && !filterActive()) {
        setGroupExpanded(row->id, false);
        return;
    }
    if (row->parentId != 0) stepTo(visibleIndexOf(row->parentId), Qt::NoModifier);
}

void ItemsPanel::unfoldOrChild()
{
    const int at = visibleIndexOf(myCurrentId);
    const Row* row = visibleRow(at);
    if (!row || !row->isGroup) return;
    const int folderId = row->id;
    if (!isGroupExpanded(folderId) && !filterActive()) {
        setGroupExpanded(folderId, true);
        return;
    }
    const Row* next = visibleRow(at + 1);
    if (next && next->parentId == folderId) stepTo(at + 1, Qt::NoModifier);
}

void ItemsPanel::renameCurrent()
{
    if (visibleIndexOf(myCurrentId) < 0) return;
    if (myCurrentKind == RowKind::Folder) beginRenameForGroup(myCurrentId);
    else beginRenameForItem(myCurrentId, myCurrentKind == RowKind::Outline);
}

std::vector<int> ItemsPanel::movableIds() const
{
    if (myCurrentId == 0) return {};
    // A body row moves only while it is SELECTED - the backstop to
    // activateRow()'s rule, so no route can leave a key move carrying a row
    // the user has deselected.
    if (myCurrentKind == RowKind::Body &&
        std::find(mySelectedIds.begin(), mySelectedIds.end(), myCurrentId) == mySelectedIds.end())
        return {};
    return idsForMenu(myCurrentId, myCurrentKind);
}

void ItemsPanel::moveByCommand(ListCommand command)
{
    if (!myDocument) return;
    const std::vector<int> ids = movableIds();
    if (ids.empty()) return;
    // ALT+RIGHT, INTO THE FOLDER ABOVE (ruling D1): the previous visible
    // sibling, when it is a folder, takes the selection at its end - the
    // same "into" a drop on a folder row asks for, so the Note reads the
    // same ("Moved Shelf to Doors"). Refused while filtered (RF3's reason:
    // "the row above" is the row the filter left above), and quietly when
    // that row is not a folder - a reason in the status bar, no checkpoint.
    if (command == ListCommand::MoveIntoFolderAbove) {
        if (filterActive()) {
            emit moveRefused(ItemMoveText::filteredIntoRefusal());
            return;
        }
        const int folder = folderAbove(myCurrentId);
        if (folder == 0) {
            emit moveRefused(ItemMoveText::noFolderAboveRefusal());
            return;
        }
        emit itemsMoveRequested(ids, folder, DocumentModel::kIntoFolder);
        return;
    }
    const int key = command == ListCommand::MoveOutOfFolder ? Qt::Key_Left
                    : command == ListCommand::MoveUp        ? Qt::Key_Up
                                                            : Qt::Key_Down;
    // Alt+Up/Down WHILE FILTERED is refused (ruling RF3): "one place up"
    // counts siblings the filter is hiding, so the row would land beside
    // something nobody can see. Quiet - no checkpoint, no toast - and never
    // silent: the reason goes to the status bar's left channel. Alt+Left is
    // NOT refused: "just after its folder" names a place the user can see
    // (the folder row is on screen as the row's ancestor), whatever siblings
    // the filter hides after it.
    if (key != Qt::Key_Left && filterActive()) {
        emit moveRefused(ItemMoveText::filteredReorderRefusal());
        return;
    }
    const DocumentModel::MoveTarget target =
        key == Qt::Key_Left ? myDocument->outOfFolderTarget(ids)
                            : myDocument->siblingStepTarget(ids, key == Qt::Key_Up ? -1 : +1);
    // Nowhere to go is not a move: nothing is asked, nothing is reported.
    if (target.ok) emit itemsMoveRequested(ids, target.parent, target.beforeId);
}

ItemsPanel::DropTarget ItemsPanel::dropTargetAt(const QPoint& pos) const
{
    DropTarget target;
    if (!myDocument || !rect().contains(pos)) return target;   // outside: cancel
    if (titleRowRect().contains(pos)) {
        if (filterActive()) return target;   // a filtered list hides where a reorder lands
        target.kind = DropKind::TopLevelEnd;  // parent 0, beforeId 0: the end of the top level
        return target;
    }
    // ONLY WHAT THE LIST SHOWS (ruling P10). A row scrolled out of sight keeps
    // its geometry - up under the filter field, in the gap above the list -
    // and a hand resting there must not land on a row nobody can see.
    if (myScroll) {
        const QWidget* seen = myScroll->viewport();
        if (!QRect(seen->mapTo(this, QPoint(0, 0)), seen->size()).contains(pos)) return target;
    }
    const int gap = myRows->spacing();
    for (std::size_t i : myVisible) {
        const Row& row = myRowList[i];
        // Mapped, not compared: a row's own geometry() is in the scrolling
        // host's coordinates, and `pos` is in this card's.
        const QPoint local = row.widget->mapFrom(this, pos);
        QRect band = row.widget->rect();
        band.setTop(band.top() - gap);   // the gap above a row belongs to it
        if (!band.contains(local)) continue;
        const int h = row.widget->height();
        const int y = local.y();
        const bool sameRow = myDropTarget.rowIndex == static_cast<int>(i);
        target.rowIndex = static_cast<int>(i);
        // While filtered, only into-folder drops are offered.
        if (filterActive()) {
            if (!row.isGroup) return DropTarget{};
            target.kind = DropKind::Into;
            target.parent = row.id;
            target.beforeId = DocumentModel::kIntoFolder;
            return target;
        }
        enum class Zone { Above, Into, Below };
        Zone zone = Zone::Above;
        if (row.isGroup) {
            // 25 / 50 / 25, the into band dominant (WCAG 2.5.8), each edge
            // pushed 4 px in favour of the zone the hand is already in.
            int upper = h / 4;
            int lower = h - h / 4;
            if (sameRow && myDropTarget.kind == DropKind::Into) {
                upper -= kZoneHysteresisPx;
                lower += kZoneHysteresisPx;
            } else if (sameRow && myDropTarget.kind == DropKind::Line) {
                if (myDropTarget.lineAtBottom) lower -= kZoneHysteresisPx;
                else upper += kZoneHysteresisPx;
            }
            zone = y < upper ? Zone::Above : (y < lower ? Zone::Into : Zone::Below);
        } else {
            // A body or an outline: halves. The old hidden rule - a drop on
            // an item files into that item's folder - is retired.
            int middle = h / 2;
            if (sameRow && myDropTarget.kind == DropKind::Line)
                middle += myDropTarget.lineAtBottom ? -kZoneHysteresisPx : kZoneHysteresisPx;
            zone = y < middle ? Zone::Above : Zone::Below;
        }
        if (zone == Zone::Into) {
            target.kind = DropKind::Into;
            target.parent = row.id;
            target.beforeId = DocumentModel::kIntoFolder;
            return target;
        }
        target.kind = DropKind::Line;
        if (zone == Zone::Above) {
            target.parent = row.parentId;
            target.beforeId = row.id;
            target.depth = row.depth;
            return target;
        }
        target.lineAtBottom = true;
        if (row.isGroup && isGroupExpanded(row.id)) {
            // Below an OPEN folder's row: the item becomes its first child.
            const std::vector<int> children = myDocument->orderedChildren(row.id);
            if (!children.empty()) {
                target.parent = row.id;
                target.beforeId = children.front();
                target.depth = row.depth + 1;
                return target;
            }
        }
        const std::vector<int> siblings = myDocument->orderedChildren(row.parentId);
        const auto at = std::find(siblings.begin(), siblings.end(), row.id);
        target.parent = row.parentId;
        target.beforeId = (at != siblings.end() && at + 1 != siblings.end()) ? *(at + 1) : 0;
        target.depth = row.depth;
        return target;
    }
    return target;   // the card's blank space: no target
}

void ItemsPanel::showDropTarget(const DropTarget& target)
{
    // EXACTLY ONE indicator: the lit title, or the line, or the ring.
    setTitleLit(target.kind == DropKind::TopLevelEnd, target.refused);
    if (!myIndicator) return;
    if (target.kind == DropKind::Line && target.rowIndex >= 0) {
        const QRect geo = myRowList[static_cast<std::size_t>(target.rowIndex)].widget->geometry();
        const int gap = myRows->spacing();
        const int y = target.lineAtBottom ? geo.bottom() + 1 + gap / 2 : geo.top() - (gap + 1) / 2;
        const int left = kRowInset + target.depth * kIndentPx;
        myIndicator->showLine(y, left, geo.right() - kRowInset, target.refused);
    } else if (target.kind == DropKind::Into && target.rowIndex >= 0) {
        myIndicator->showRing(myRowList[static_cast<std::size_t>(target.rowIndex)].widget->geometry(),
                              target.refused);
    } else {
        myIndicator->clear();
    }
}

void ItemsPanel::activateRow(int id, RowKind kind, Qt::KeyboardModifiers mods)
{
    // THE KEYS' ROW MOVED - announced whatever kind of row it moved to (ruling
    // DA1). An outline row changes no body selection, so neither
    // selectionChanged nor filterChanged fires for it, and a quiet move
    // refusal written for the row the keys LEFT stood under the next one.
    const int before = myCurrentId;
    selectRow(id, kind, mods);
    if (myCurrentId != before) emit currentRowChanged();
}

void ItemsPanel::selectRow(int id, RowKind kind, Qt::KeyboardModifiers mods)
{
    // The row the keys continue from - whichever route selected it. A folder
    // or outline row is always selected by its own activation; a body row is
    // only when the click leaves it in the selection (see the end).
    if (kind != RowKind::Body) {
        myCurrentId = id;
        myCurrentKind = kind;
    }
    if (kind == RowKind::Folder) {
        // A folder row selects the BODIES under it, and Ctrl ADDS them to
        // what is already selected - which is how two folders, or a folder
        // and a loose board, end up under one gizmo. Computed here because
        // this panel is what knows the tree.
        std::vector<int> ids = myDocument ? myDocument->bodiesUnderGroup(id) : std::vector<int>();
        if (mods & Qt::ControlModifier) {
            std::vector<int> merged = mySelectedIds;
            for (int body : ids)
                if (std::find(merged.begin(), merged.end(), body) == merged.end()) merged.push_back(body);
            ids.swap(merged);
        }
        requestSelection(ids);
        return;
    }
    if (kind == RowKind::Outline) {
        emit outlineActivated(id);
        return;
    }
    const std::vector<int> ids = selectionFor(id, mods);
    // FOCUS AND SELECTION NEVER SEPARATE: a Ctrl+click that toggles this row
    // OFF must not make it the keys' row, or Alt+Up would move a row nobody
    // has selected. The keys' row falls back to what stays selected.
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        myCurrentId = id;
        myCurrentKind = RowKind::Body;
    } else if (myCurrentKind != RowKind::Body || myCurrentId == id ||
               std::find(ids.begin(), ids.end(), myCurrentId) == ids.end()) {
        myCurrentId = ids.empty() ? 0 : ids.back();
        myCurrentKind = RowKind::Body;
    }
    requestSelection(ids);
    if (!(mods & Qt::ShiftModifier)) myAnchorRowId = id;
}

bool ItemsPanel::rowInSelection(int id, RowKind kind) const
{
    const auto selected = [this](int body) {
        return std::find(mySelectedIds.begin(), mySelectedIds.end(), body) != mySelectedIds.end();
    };
    if (kind == RowKind::Body) return selected(id);
    if (kind == RowKind::Outline) return myPendingOutlineId != 0 && id == myPendingOutlineId;
    // A folder is part of the selection when every body under it is - the
    // same answer its own fill gives (restyleRows()).
    const std::vector<int> under =
        myDocument ? myDocument->bodiesUnderGroup(id) : std::vector<int>();
    return !under.empty() && std::all_of(under.begin(), under.end(), selected);
}

void ItemsPanel::beginDrag(const QPoint& /*panelPos*/, const QPoint& globalPos)
{
    myPressArmed = false;
    myDragging = true;
    myDragOutside = false;
    // A press on a row that is ALREADY part of a multiple selection carries
    // the whole of it - "grab them and insert them into a folder" - and one
    // anywhere else carries just that row.
    const bool inSelection =
        std::find(mySelectedIds.begin(), mySelectedIds.end(), myPressId) != mySelectedIds.end();
    if (myPressKind == RowKind::Body && inSelection && mySelectedIds.size() > 1 &&
        myPressMods == Qt::NoModifier)
        myDragIds = mySelectedIds;
    else
        myDragIds = {myPressId};
    myDropTarget = DropTarget{};

    // Escape is this gesture's for exactly as long as it lasts.
    if (!myKeyClaim) myKeyClaim = new DragKeyClaim(this);
    QCoreApplication::instance()->installEventFilter(myKeyClaim);
    QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
    if (myGhost && myDocument) {
        // The ghost names the first carried row in LIST order - the order
        // the user sees - never the selection's, which is the order picked.
        int first = myDragIds.front();
        for (std::size_t i : myVisible) {
            const int id = myRowList[i].id;
            if (std::find(myDragIds.begin(), myDragIds.end(), id) != myDragIds.end()) {
                first = id;
                break;
            }
        }
        const IconSet::Glyph glyph = myDocument->groupExists(first)       ? IconSet::Glyph::Folder
                                     : myDocument->containsOutline(first) ? IconSet::Glyph::Sketch
                                                                          : IconSet::Glyph::Body;
        myGhost->begin(glyph, ItemMoveText::nameOf(*myDocument, first),
                       static_cast<int>(myDragIds.size()) - 1);
        if (myView) myGhost->follow(myView->mapFromGlobal(globalPos));
    }
    restyleRows();   // dims what is carried
}

void ItemsPanel::updateDrag(const QPoint& panelPos, const QPoint& globalPos)
{
    if (myGhost && myView) myGhost->follow(myView->mapFromGlobal(globalPos));
    // Where the hand IS, for the two timers that re-aim the drag without a
    // move of their own (a folder springing open, the list scrolling).
    myLastDragPos = panelPos;
    myLastDragGlobal = globalPos;
    updateAutoScroll(panelPos);
    // OUTSIDE THE CARD there is no target at all, whatever the geometry
    // reads: a release there cancels.
    const bool outside = !rect().contains(panelPos);
    DropTarget target = outside ? DropTarget{} : dropTargetAt(panelPos);
    // Nothing to re-say while the hand stays over the same spot - but the
    // first move of a drag always says something, even over blank space.
    if (target.sameSpot(myDropTarget) && outside == myDragOutside && !myDragStatus.isEmpty())
        return;
    myDragOutside = outside;

    QString status;
    if (outside) {
        status = ItemMoveText::releaseToCancel();
    } else if (target.kind == DropKind::None) {
        status = filterActive() ? ItemMoveText::filteredDropHint() : ItemMoveText::noTargetHint();
    } else if (myDocument) {
        // THE DRY RUN decides the refusal and the words - the same answer the
        // release, the Note and the Failure will read.
        const DocumentModel::MoveResult dry =
            myDocument->checkMove(myDragIds, target.parent, target.beforeId);
        target.refused = dry.check == DocumentModel::MoveCheck::IntoItself ||
                         dry.check == DocumentModel::MoveCheck::Unknown;
        status = target.refused ? ItemMoveText::refusal(*myDocument, dry)
                                : ItemMoveText::dragStatus(*myDocument, dry);
    }
    myDropTarget = target;
    // A folded folder the hand rests on (into, not refused, not while the
    // filter holds the folds) starts the spring; any other target stops it.
    // The SAME folder keeps its running timer - a hand wobbling inside the
    // into band is still resting there.
    const bool springs = target.kind == DropKind::Into && !target.refused && !filterActive() &&
                         !isGroupExpanded(target.parent);
    if (!springs) {
        mySpringFolder = 0;
        mySpringTimer->stop();
    } else if (mySpringFolder != target.parent) {
        mySpringFolder = target.parent;
        mySpringTimer->start(kSpringLoadMs);
    }
    showDropTarget(target);
    if (myGhost)
        myGhost->setVariant(outside          ? DragGhost::Variant::Cancel
                            : target.refused ? DragGhost::Variant::Refused
                                             : DragGhost::Variant::Live);
    QGuiApplication::changeOverrideCursor(target.refused ? Qt::ForbiddenCursor : Qt::ClosedHandCursor);
    setDragStatus(status);
}

void ItemsPanel::finishDrag(bool commit)
{
    const bool wasDragging = myDragging;
    const DropTarget target = myDropTarget;
    const std::vector<int> carried = myDragIds;
    myDragging = false;
    myPressArmed = false;
    myDragOutside = false;
    myDragIds.clear();
    myDropTarget = DropTarget{};
    // Both timers end with the drag, whichever way it ended - a spring firing
    // after the drop would open a folder nobody is hovering, and a scroll
    // would move the list under a hand that has let go.
    mySpringFolder = 0;
    mySpringTimer->stop();
    myAutoScrollStep = 0;
    myAutoScrollTimer->stop();
    if (wasDragging) {
        // Everything beginDrag() put up comes down, whichever way it ended.
        if (myKeyClaim) QCoreApplication::instance()->removeEventFilter(myKeyClaim);
        QGuiApplication::restoreOverrideCursor();
        if (myGhost) myGhost->end();
    }
    showDropTarget(DropTarget{});
    restyleRows();   // the carried rows come back to full ink
    setDragStatus(QString());
    // The refresh the drag held back (see refresh()). Not forced: a drawer
    // the drag ended BY closing keeps the mark for its next showing.
    if (myRowsDirty) refresh();
    // A release with no target - outside the card, or over blank space -
    // cancels. A refused target still asks, so MainWindow's Failure says why.
    // DocumentModel's own rules answer it (a folder into its own subtree, a
    // row onto where it already is) - this panel carries no second copy.
    if (commit && wasDragging && target.kind != DropKind::None)
        emit itemsMoveRequested(carried, target.parent, target.beforeId);
}

void ItemsPanel::setDragStatus(const QString& text)
{
    if (text == myDragStatus) return;
    myDragStatus = text;
    emit dragStatusChanged();
}

bool ItemsPanel::isCarried(const Row& row) const
{
    if (!myDragging || !myDocument) return false;
    for (int id : myDragIds) {
        if (id == row.id) return true;
        // Inside a carried folder, at any depth.
        if (row.parentId != 0 && myDocument->groupExists(id) &&
            myDocument->groupContains(id, row.parentId))
            return true;
    }
    return false;
}

void ItemsPanel::cancelDrag()
{
    if (myDragging || myPressArmed) finishDrag(/*commit=*/false);
}

void ItemsPanel::springOpen()
{
    if (!myDragging || mySpringFolder == 0) return;
    const int folder = mySpringFolder;
    mySpringFolder = 0;
    // The arming rule, asked again now that it fires: inside the 700 ms the
    // filter may have taken over (it holds folds on its own account, and a
    // write here would outlive it - R7a), an Undo may have taken the folder
    // away, or something else may already have opened it.
    if (filterActive() || !myDocument || !myDocument->groupExists(folder) ||
        isGroupExpanded(folder))
        return;
    // Written into myExpanded, so it STAYS open after the drop - the Finder's
    // rule, and the user's to fold again. Visibility only (every row of the
    // tree exists), so the row the drag was pressed on is never destroyed.
    setGroupExpanded(folder, true);
    myDropTarget = DropTarget{};   // the rows moved under a still hand
    updateDrag(myLastDragPos, myLastDragGlobal);
}

void ItemsPanel::updateAutoScroll(const QPoint& panelPos)
{
    int step = 0;
    if (myScroll && rect().contains(panelPos)) {
        const QWidget* list = myScroll->viewport();
        const QPoint inList = list->mapFrom(this, panelPos);
        const int height = list->height();
        if (inList.x() >= 0 && inList.x() < list->width()) {
            if (inList.y() >= 0 && inList.y() < kAutoScrollBandPx)
                step = -kAutoScrollStepPx;
            else if (inList.y() < height && inList.y() >= height - kAutoScrollBandPx)
                step = kAutoScrollStepPx;
        }
    }
    myAutoScrollStep = step;
    if (step == 0) myAutoScrollTimer->stop();
    else if (!myAutoScrollTimer->isActive()) myAutoScrollTimer->start();
}

void ItemsPanel::autoScrollTick()
{
    if (!myDragging || myAutoScrollStep == 0 || !myScroll) {
        myAutoScrollTimer->stop();
        return;
    }
    QScrollBar* bar = myScroll->verticalScrollBar();
    const int before = bar->value();
    bar->setValue(before + myAutoScrollStep);
    // At either end there is nothing left to scroll: stop ticking rather than
    // re-aim the drag thirty times a second over a list that is not moving.
    if (bar->value() == before) {
        myAutoScrollTimer->stop();
        return;
    }
    // The rows moved under a still hand: re-aim from where it rests.
    updateDrag(myLastDragPos, myLastDragGlobal);
}

bool ItemsPanel::eventFilter(QObject* watched, QEvent* event)
{
    auto* rowWidget = qobject_cast<QWidget*>(watched);
    const QVariant groupProperty = watched->property("groupId");
    const QVariant outlineProperty = watched->property("outlineId");
    const QVariant solidProperty = watched->property("solidId");
    if (!rowWidget ||
        !(groupProperty.isValid() || outlineProperty.isValid() || solidProperty.isValid()))
        return QWidget::eventFilter(watched, event);
    const RowKind kind = groupProperty.isValid()     ? RowKind::Folder
                         : outlineProperty.isValid() ? RowKind::Outline
                                                     : RowKind::Body;
    const int rowId = groupProperty.isValid()     ? groupProperty.toInt()
                      : outlineProperty.isValid() ? outlineProperty.toInt()
                                                  : solidProperty.toInt();

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::RightButton) {
            // THE MENU ACTS ON THE SELECTION (ruling R9a): "New folder with
            // these" and "Remove folder, keep contents" read the current
            // selection, so a right
            // press selects its row first - unless the row is already part of
            // the selection, which a multi-row menu gesture must keep.
            if (!rowInSelection(rowId, kind)) {
                activateRow(rowId, kind, Qt::NoModifier);
            } else {
                // Already selected, so it can be the keys' row without
                // separating focus from the selection.
                myCurrentId = rowId;
                myCurrentKind = kind;
            }
            // The keys follow a right click too, so they work straight after
            // the menu's Move down.
            setFocus(Qt::MouseFocusReason);
            return true;
        }
        if (mouse->button() != Qt::LeftButton) return true;
        // A drag whose release never arrived (its row destroyed under the
        // held button, capture lost) must not turn THIS click into a move of
        // what it carried (ruling R9b).
        if (myDragging) finishDrag(/*commit=*/false);
        // ARMED, not acted on: the press changes nothing. A release that
        // never travelled is a click (activateRow); one that did is a drop.
        myPressId = rowId;
        myPressKind = kind;
        myPressMods = mouse->modifiers();
        myPressArmed = true;
        // The keys follow the click. Set here rather than left to Qt's
        // click-focus, which only acts on a SPONTANEOUS press and never sees
        // one the suite sends.
        setFocus(Qt::MouseFocusReason);
        myDragStart = rowWidget->mapTo(this, mouse->position().toPoint());
        return true;
    }
    case QEvent::MouseMove: {
        if (!myPressArmed && !myDragging) return true;
        auto* mouse = static_cast<QMouseEvent*>(event);
        // Only a move with the left button HELD carries a drag.
        if (!(mouse->buttons() & Qt::LeftButton)) return true;
        // Qt's implicit grab delivers every move to the pressed row, inside
        // the card or out of it, which is what lets "outside" be read off
        // this card's own rect.
        const QPoint here = rowWidget->mapTo(this, mouse->position().toPoint());
        if (!myDragging) {
            if ((here - myDragStart).manhattanLength() < kDragThresholdPx) return true;
            beginDrag(here, mouse->globalPosition().toPoint());
        }
        updateDrag(here, mouse->globalPosition().toPoint());
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return true;
        if (myDragging) {
            updateDrag(rowWidget->mapTo(this, mouse->position().toPoint()),
                       mouse->globalPosition().toPoint());
            finishDrag(/*commit=*/true);
        } else if (myPressArmed) {
            myPressArmed = false;
            activateRow(myPressId, myPressKind, myPressMods);
        }
        // A widget that accepts a press accepts its release too - CLAUDE.md's
        // rule, learned from HintBalloon letting one through and having the
        // viewport re-pick underneath it.
        return true;
    }
    case QEvent::MouseButtonDblClick:
        // Press, release (which selected), DblClick, release: the rename
        // opens over the row the release just selected. refresh() does not
        // rebuild rows over a selection-only change, so the row is still the
        // live widget.
        if (kind == RowKind::Folder) beginRenameForGroup(rowId);
        else beginRenameForItem(rowId, kind == RowKind::Outline);
        return true;
    case QEvent::ContextMenu:
        // The right button, over any row: the folder gestures, as a menu -
        // a second ROUTE to the Model menu's actions, never a second
        // implementation (MainWindow answers both).
        showRowMenu(static_cast<QContextMenuEvent*>(event)->globalPos(), rowId, kind);
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

QStringList ItemsPanel::paintedTexts() const
{
    // Fixed app copy ONLY - never a row's name, which is the user's own
    // text. Same boundary InitScreen::paintedTexts() and
    // VersionsPanel::paintedTexts() already draw for their own user-supplied
    // names.
    //
    // EVERYTHING this drawer says, painted or spoken (T-063): the accessible
    // names below are words a screen reader reads aloud, and the row menu's
    // entries are words a user reads, so the vocabulary sweep covers them
    // too. The name fragments come from the same functions the rows use.
    return {tr("Items"), tr("Show or hide this body"), tr("Show or hide this outline"),
            tr("Show or hide everything in this folder"), emptyText(),
            ItemsFilterField::placeholder(), tr("Clear filter"), tr("Nothing matches \"%1\""),
            filterHeldTip(), tr("%1 is hidden by the filter —"),
            tr("The grain runs along this body — click to turn it"),
            tr("The grain runs across this body — click to turn it back"),
            // Accessible names (nameRow()).
            tr(", in %1"), tr(", hidden"), tr(", partly hidden"), tr("%1, folder"),
            tr("Hide %1"), tr("Show %1"), tr("Grain along"), tr("Grain across"),
            tr("Hide everything in %1"), tr("Show everything in %1"), tr("Open %1"),
            tr("Close %1"), tr("Close this folder"), tr("Open this folder"), waitingToExtrude(),
            tr("1 of %1 bodies in %2 is selected"), tr("%1 of %2 bodies in %3 are selected"),
            // The row menu (populateRowMenu()).
            tr("New folder with these"), tr("Rename"), tr("Move up"), tr("Move down"),
            tr("Remove folder, keep contents"), tr("Delete folder and its contents"),
            tr("Move into"), tr("No folders yet"), tr("No other folder can take this")};
}

void ItemsPanel::showSelection(const std::vector<int>& ids)
{
    // The newest body in the selection that was not there before - an
    // additive pick appends - is the one to reveal, unless this drawer asked
    // for the selection itself.
    //
    // "Was not there before" is measured against what the last PICK left, not
    // mySelectedIds: a rebuild (refresh(), applyTheme()) re-applies the
    // viewport's selection through applySelection() and can run before this
    // slot does, which would leave nothing new to see.
    int revealId = 0;
    if (!myRequestingSelection) {
        for (int id : ids)
            if (std::find(myPickBaseline.begin(), myPickBaseline.end(), id) == myPickBaseline.end())
                revealId = id;
    }
    myPickBaseline = ids;
    // A standing hidden-pick notice names a body that must still be selected.
    if (myHiddenPickId != 0 &&
        std::find(ids.begin(), ids.end(), myHiddenPickId) == ids.end())
        hideHiddenPick();
    applySelection(ids);
    if (revealId != 0) reveal(revealId);
}

void ItemsPanel::resetPick()
{
    // Body ids restart per document, so a baseline from the last one could
    // swallow a reveal; a notice for its body is meaningless too.
    myPickBaseline.clear();
    myCurrentId = 0;
    myCurrentKind = RowKind::Body;
    hideHiddenPick();
}

void ItemsPanel::forgetFurniture()
{
    // The query was typed against the furniture that just closed. Cleared
    // through the field itself, so setFilter("") runs the one route every
    // other clear takes - and with it every folder the filter was holding
    // open lets go, since that hold is derived from the query (R7a), never
    // written into myExpanded.
    if (myFilter && !myFilter->text().isEmpty()) myFilter->clear();
}

void ItemsPanel::applySelection(const std::vector<int>& ids)
{
    mySelectedIds = ids;
    restyleRows();
}

void ItemsPanel::requestSelection(const std::vector<int>& ids)
{
    const QScopedValueRollback<bool> guard(myRequestingSelection, true);
    emit selectionRequested(ids);
}

void ItemsPanel::reveal(int bodyId)
{
    if (!myDocument || !myDocument->contains(bodyId)) return;
    // A pick in the viewport moves the keys' row too: Down goes on from it.
    myCurrentId = bodyId;
    myCurrentKind = RowKind::Body;
    // A closed drawer defers its rebuild; walk only an up-to-date list.
    ensureFresh();
    // A's rule: every ancestor opens and STAYS open - written into
    // myExpanded, the same set the twisty writes.
    bool opened = false;
    for (int folder = myDocument->groupOf(bodyId); folder > 0; folder = myDocument->groupOf(folder))
        opened = myExpanded.insert(folder).second || opened;
    if (opened) {
        applyRowVisibility();
        relayoutCard();
        restyleRows();
    }
    // A reveal never fails silently: hidden by the filter, the foot says so.
    // The row's own state, applyRowVisibility()'s answer - never a second
    // copy of the matching rule.
    bool hiddenByFilter = false;
    if (filterActive()) {
        hiddenByFilter = true;
        for (const Row& row : myRowList)
            if (!row.isGroup && !row.isOutline && row.id == bodyId) {
                hiddenByFilter = !row.matched;
                break;
            }
    }
    if (hiddenByFilter) {
        showHiddenPick(bodyId);
        return;
    }
    hideHiddenPick();
    scrollToRow(bodyId);
}

void ItemsPanel::scrollToRow(int id)
{
    for (std::size_t i : myVisible) {
        const Row& row = myRowList[i];
        if (row.id != id) continue;
        if (QWidget* host = myScroll->widget())
            if (host->layout()) host->layout()->activate();
        myScroll->ensureWidgetVisible(row.widget, 0, 8);
        return;
    }
}

void ItemsPanel::showHiddenPick(int bodyId)
{
    myHiddenPickId = bodyId;
    myFootText->setText(tr("%1 is hidden by the filter —")
                            .arg(QString::fromStdString(myDocument->nameOf(bodyId))));
    myFoot->show();
    relayoutCard();
}

void ItemsPanel::hideHiddenPick()
{
    myHiddenPickId = 0;
    if (!myFoot || myFoot->isHidden()) return;
    myFoot->hide();
    relayoutCard();
}

QString ItemsPanel::partialCountAt(int index) const
{
    const Row* row = visibleRow(index);
    return row && row->count && !row->count->isHidden() ? row->count->text() : QString();
}

QLabel* ItemsPanel::partialCountLabelAt(int index) const
{
    const Row* row = visibleRow(index);
    return row ? row->count : nullptr;
}

bool ItemsPanel::rowIsMarkedAt(int index) const
{
    const Row* row = visibleRow(index);
    return row && row->widget->marked();
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
        // On screen first (ruling RF4): an editor opened over a row scrolled
        // out of the list is typed into blind.
        scrollToRow(id);
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
        scrollToRow(groupId);   // on screen first - see beginRenameForItem()
        InlineRename::beginRename(
            row.widget, row.name->geometry(), row.text,   // plain - ruling P5
            [this, groupId](QString newName) { emit groupRenameCommitted(groupId, newName); });
        return;
    }
}

void ItemsPanel::restyleRows()
{
    // The two looks a row can wear, built ONCE per call rather than once per
    // row, and each row's stylesheet set only when its look actually CHANGED
    // (review ruling R5a): setStyleSheet() re-polishes the whole row subtree
    // even when handed the string it already has, which at 130 rows was the
    // entire cost of a selection change - and of every fold, since a fold
    // restyles too. A drag's target is NOT a look: the DropIndicator draws
    // it over the rows, so "where it would land" never touches a fill.
    QString markedLook = QStringLiteral("#itemsRow { background-color: %1; "
                                        "border-radius: 4px; }")
                             .arg(Theme::chipActive().name());
    QString plainLook = QStringLiteral("#itemsRow { background: transparent; }");
    // A theme's own row (themes part 2): its mockup's selection capsule, and
    // Ultraviolet's rule under every row.
    if (const Themes::List& l = Themes::shell().list; l.on) {
        const QString rule = l.hairline.isValid()
                                 ? QStringLiteral(" border-bottom: 1px solid %1;").arg(l.hairline.name(QColor::HexArgb))
                                 : QString();
        markedLook = QStringLiteral("#itemsRow { background-color: %1; border: 1px solid %2; border-radius: %3px;%4 }")
                         .arg(l.selFill.name(QColor::HexArgb),
                              (l.selEdge.isValid() ? l.selEdge : l.selFill).name(QColor::HexArgb))
                         .arg(l.rowRadius)
                         .arg(rule);
        plainLook = QStringLiteral("#itemsRow { background: transparent; border: 1px solid transparent; "
                                   "border-radius: %1px;%2 }")
                        .arg(l.rowRadius)
                        .arg(rule);
    }
    // One set lookup per body instead of a linear find per row per body.
    const std::unordered_set<int> selected(mySelectedIds.begin(), mySelectedIds.end());
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
            int chosen = 0;
            for (int id : under) chosen += selected.count(id) ? 1 : 0;
            marked = !under.empty() && chosen == static_cast<int>(under.size());
            // PARTLY selected: a count instead of the fill - and the same
            // count in words, on its tooltip and in the row's name (F18).
            const bool partial = chosen > 0 && !marked;
            const QString words =
                partial ? partialCountWords(chosen, static_cast<int>(under.size()), row.text)
                        : QString();
            if (row.count) {
                const QString text =
                    partial ? tr("%1/%2").arg(chosen).arg(static_cast<int>(under.size())) : QString();
                if (row.count->text() != text) row.count->setText(text);
                if (row.count->toolTip() != words) row.count->setToolTip(words);
                if (row.count->isHidden() == partial) row.count->setVisible(partial);
            }
            if (row.partialWords != words) {
                row.partialWords = words;
                writeRowName(row);
            }
        } else if (row.isOutline) {
            // THE PENDING OUTLINE IS NOT MARKED (ruling D4). It used to wear
            // the selection fill, so the list showed one more selected row
            // than the status bar counted - and since the fill is also the
            // focus cue, two rows looked focused. Its name says it instead.
            const bool pending = myPendingOutlineId != 0 && row.id == myPendingOutlineId;
            row.widget->setPendingOutline(pending);
            if (row.pending != pending) {
                row.pending = pending;
                writeRowName(row);
            }
        } else {
            marked = selected.count(row.id) > 0;
        }
        // The fill's partner (ruling D3): a 3 px bar the row paints itself,
        // in a token that clears 3:1 where the fill alone does not.
        row.widget->setMarked(marked);
        row.widget->setCurrent(row.id == myCurrentId);
        if (Themes::shell().list.on) styleNameColour(row);   // the selected name's weight
        // WHAT A DRAG CARRIES (T-063): its name at 45% (nameColour()), and its
        // buttons out of reach for as long as the hand holds it. Each set only
        // when it changes, by the same rule as the look below - a drag begins
        // and ends with one restyle, never one per move.
        const bool carried = isCarried(row);
        if (row.eye && row.eye->isEnabled() == carried) row.eye->setEnabled(!carried);
        if (row.grain && row.grain->isEnabled() == carried) row.grain->setEnabled(!carried);
        styleNameColour(row);
        // One mark, one meaning - "this is part of the selection the status
        // bar counts" - on a body row and on a folder whose every body is in
        // it, and on nothing else.
        const QString& look = marked ? markedLook : plainLook;
        if (row.style == look) continue;
        row.widget->setStyleSheet(look);
        row.style = look;
    }
}
