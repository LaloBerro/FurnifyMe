#include "ItemsPanel.h"

#include <cstdio>

#include "DocumentModel.h"
#include "IconSet.h"
#include "InlineRename.h"
#include "OcctViewWidget.h"
#include "Theme.h"

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
#include <QVBoxLayout>

#include <algorithm>

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
constexpr int kPad = 12;
// The height the empty state genuinely needs - title, the "no bodies yet"
// message wrapped at this width, and the padding around them. Used as a
// FLOOR rather than only as the empty layout's natural size, so the card does
// not visibly shrink the moment the first body arrives and grow again when it
// is deleted. A drawer that changes shape on every edit reads as a glitch.
constexpr int kMinHeight = 176;
// How far one level of the folder tree indents a row (improvements item 10).
// 12 is the twisty's own width plus its gap, so a folder's contents line up
// under its name rather than under its twisty.
constexpr int kIndentPx = 12;
// How far the pointer has to travel before a press on a row becomes a DRAG
// rather than a click. Qt's own startDragDistance() is 10 at default settings
// and is about dragging OUT of an application; a row moving inside a 200px
// drawer wants less.
constexpr int kDragThresholdPx = 6;
// What the drawer leaves between its bottom edge and the viewport's -
// ViewportOverlay::kEdgeMargin plus a hair, so a full-height list still reads
// as a floating card rather than as something wedged into the corner.
constexpr int kBottomClearance = 16;
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
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);
    myTitle = new QLabel(tr("Items"), this);
    titleRow->addWidget(myTitle, 1);
    myNewFolder = new QPushButton(this);
    myNewFolder->setFixedSize(20, 20);
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

    // refresh() FIRST and applyTheme() second, and the order is load-bearing
    // both ways: refresh() builds the rows applyTheme() then restyles, and
    // applyTheme() walks myRowList, which refresh() is what fills.
    refresh();
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

    for (const Row& row : myRowList) {
        if (row.name)
            row.name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                        .arg(Theme::text().name()));
        // Rasterised out of text()/textDisabled() when it was built, so it is
        // pixels rather than a description - the same cached-appearance value
        // ToolChip::applyTheme() has to rebuild.
        if (row.eye) row.eye->setIcon(IconSet::icon(IconSet::Glyph::Body));
    }

    // The empty state's message is a plain child of myRows with no entry in
    // myRowList; it reads bodyFont() through its own stylesheet, so it is
    // restyled by the same walk the rows would need. Found rather than
    // stored: it exists only while the list is empty, and a pointer to it
    // would be a fifth thing to keep in step.
    if (myRowList.empty()) {
        for (QLabel* empty : findChildren<QLabel*>()) {
            if (empty == myTitle) continue;
            empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                "font-size: %2pt;")
                                     .arg(Theme::textMuted().name())
                                     .arg(Theme::bodyFont().pointSizeF()));
        }
    }

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
    int wanted = kPad * 2 + title + myOuter->spacing() + rows;
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
        // what they are called, where they hang and which are open. All four
        // are things a row shows, and the Phase-5 lesson this function's own
        // comment records is that a field a row shows but the early-out does
        // not compare is a field that stops updating.
        for (const DocumentModel::Group& group : myDocument->groups()) {
            signature += QStringLiteral("g") + QString::number(group.id) + QLatin1Char('') +
                         QString::fromStdString(group.name) + QLatin1Char('') +
                         QString::number(group.parent) + QLatin1Char('') +
                         (isGroupExpanded(group.id) ? QLatin1Char('1') : QLatin1Char('0')) +
                         QLatin1Char('');
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

    // Rebuild wholesale: the list is short, and diffing it would be more code
    // than it saves.
    // Everything but the trailing stretch, which is structural and belongs to
    // the layout rather than to any row.
    while (myRows->count() > 1) {
        QLayoutItem* item = myRows->takeAt(0);
        if (QWidget* widget = item->widget()) {
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
        delete item;
    }
    myRowList.clear();
    if (!myDocument) return;

    // The tree, from the root down - folders and the items inside them, to
    // any depth (improvements item 10). A flat document has no folders, so
    // this walks straight into the two item loops and builds exactly the
    // list it always did.
    buildGroupRows(0, 0);

    if (myRowList.empty()) {
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
    myRows->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    updateGeometry();
    adjustSize();

    if (myView) showSelection(myView->selectedSolidIds());
}

void ItemsPanel::addItemRow(int id, const QString& itemName, bool visible, bool isOutline,
                            int depth)
{
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    // Indented by how deep in the folder tree it sits (improvements item 10).
    // The INDENT is the only thing depth changes about a row: everything else
    // - height, fill, the eye, the rename gesture - is identical at every
    // level, because it is the same row in the same list.
    layout->setContentsMargins(6 + depth * kIndentPx, 4, 6, 4);
    layout->setSpacing(8);

    auto* name = new QLabel(itemName, row);
    // The name reads MUTED when the item is not actually on screen -
    // hidden by its own eye, or (a body only) by Isolate's session
    // filter, which the eye's checked state deliberately does not
    // reflect (the eye is the document's persisted choice; the filter
    // is the session's). Asked of the view, the one place the composed
    // answer lives, so the row cannot disagree with the viewport
    // (Milestone 5 feedback: "the items list is not displaying well
    // when an item is isolated or turned off").
    const bool onScreen = visible && (isOutline || !myView || myView->isSolidVisible(id));
    name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                            .arg((onScreen ? Theme::text() : Theme::textMuted()).name()));
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
    eye->setFixedSize(24, 24);
    eye->setIcon(IconSet::icon(IconSet::Glyph::Body));
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
    entry.id = id;
    entry.isOutline = isOutline;
    entry.depth = depth;
    entry.text = name->text();
    myRowList.push_back(entry);
}

void ItemsPanel::addGroupRow(int groupId, const QString& groupName, int depth)
{
    // A FOLDER row (improvements item 10): a twisty, the folder's name, and
    // one eye that speaks for everything under it. It is the same row an item
    // gets - same height, same fill when marked, same rename gesture - because
    // it IS a row in the same list, not a header the list happens to contain.
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(6 + depth * kIndentPx, 4, 6, 4);
    layout->setSpacing(6);

    auto* twisty = new QPushButton(row);
    twisty->setFixedSize(16, 16);
    twisty->setFlat(true);
    twisty->setCursor(Qt::PointingHandCursor);
    twisty->setFocusPolicy(Qt::NoFocus);
    const bool open = isGroupExpanded(groupId);
    twisty->setIcon(
        IconSet::icon(open ? IconSet::Glyph::ChevronDown : IconSet::Glyph::ChevronRight));
    twisty->setToolTip(open ? tr("Close this folder") : tr("Open this folder"));
    twisty->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    connect(twisty, &QPushButton::clicked, this,
            [this, groupId] { setGroupExpanded(groupId, !isGroupExpanded(groupId)); });
    layout->addWidget(twisty);

    auto* name = new QLabel(groupName, row);
    name->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;").arg(Theme::text().name()));
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(name, 1);

    auto* eye = new QPushButton(row);
    eye->setCheckable(true);
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
    eye->setChecked(allVisible);
    eye->setFixedSize(24, 24);
    eye->setIcon(IconSet::icon(IconSet::Glyph::Folder));
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
    entry.text = groupName;
    myRowList.push_back(entry);
}

void ItemsPanel::buildGroupRows(int groupId, int depth)
{
    if (!myDocument) return;
    // Folders first, then the loose items at this level - so a folder's own
    // contents sit directly under its row rather than after everything else
    // that shares its parent.
    for (int childId : myDocument->childGroups(groupId)) {
        addGroupRow(childId, QString::fromStdString(myDocument->groupNameOf(childId)), depth);
        if (isGroupExpanded(childId)) buildGroupRows(childId, depth + 1);
    }
    // Outlines ABOVE bodies, at every level: an outline is the thing the user
    // is about to act on, and it is the newest item in the document whenever
    // one exists.
    for (const DocumentModel::Outline& outline : myDocument->outlines()) {
        if (myDocument->groupOf(outline.id) != groupId) continue;
        addItemRow(outline.id, QString::fromStdString(outline.name),
                   myDocument->isVisible(outline.id), /*isOutline=*/true, depth);
    }
    for (const DocumentModel::Solid& solid : myDocument->solids()) {
        if (myDocument->groupOf(solid.id) != groupId) continue;
        addItemRow(solid.id, QString::fromStdString(solid.name),
                   myDocument->isVisible(solid.id), /*isOutline=*/false, depth);
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
    // WHICH ROWS EXIST changes, so this is a rebuild rather than a restyle -
    // and refresh()'s signature carries the open/closed state for exactly
    // this call's sake, or the early-out would swallow it.
    refresh();
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
        for (std::size_t i = 0; i < myRowList.size(); ++i) {
            if (myRowList[i].isGroup || myRowList[i].isOutline) continue;
            if (myRowList[i].id == myAnchorRowId) from = static_cast<int>(i);
            if (myRowList[i].id == id) to = static_cast<int>(i);
        }
        if (from >= 0 && to >= 0) {
            if (from > to) std::swap(from, to);
            std::vector<int> ids;
            for (int i = from; i <= to; ++i) {
                if (myRowList[static_cast<std::size_t>(i)].isGroup ||
                    myRowList[static_cast<std::size_t>(i)].isOutline)
                    continue;
                ids.push_back(myRowList[static_cast<std::size_t>(i)].id);
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
    for (const Row& row : myRowList) {
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
        emit itemsDropped(dragged, target);
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
               "the ground to draw your first outline.")};
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
    // panel at a time, full stop.
    if (findChild<QLineEdit*>()) return;

    for (const Row& row : myRowList) {
        if (row.id != id || row.isOutline != isOutline) continue;
        if (!row.name) return;
        const QString current = row.name->text();
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
    if (findChild<QLineEdit*>()) return;

    for (const Row& row : myRowList) {
        if (!row.isGroup || row.id != groupId) continue;
        if (!row.name) return;
        InlineRename::beginRename(
            row.widget, row.name->geometry(), row.name->text(),
            [this, groupId](QString newName) { emit groupRenameCommitted(groupId, newName); });
        return;
    }
}

void ItemsPanel::restyleRows()
{
    for (const Row& row : myRowList) {
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
        if (myDragging && row.isGroup && row.id == myDropTarget && myDropTarget != 0) {
            row.widget->setStyleSheet(QStringLiteral("#itemsRow { background-color: %1; "
                                                     "border: 1px solid %2; "
                                                     "border-radius: 4px; }")
                                          .arg(Theme::chipHover().name(),
                                               Theme::accent().name()));
            continue;
        }
        // The SAME fill a selected body row wears. One mark, one meaning -
        // "this is the row the next thing you do will act on" - rather than a
        // second visual language for the second kind of item.
        row.widget->setStyleSheet(
            marked ? QStringLiteral("#itemsRow { background-color: %1; "
                                    "border-radius: 4px; }")
                         .arg(Theme::chipActive().name())
                   : QStringLiteral("#itemsRow { background: transparent; }"));
    }
}
