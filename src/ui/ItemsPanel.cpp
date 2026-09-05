#include "ItemsPanel.h"

#include "DocumentModel.h"
#include "IconSet.h"
#include "InlineRename.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// The card's width AT THE SHIPPED TYPE SCALE. Fixed rather than
// min/preferred: this is a floating drawer anchored beside a fixed-width
// rail, and a card that changed width with the longest body name would move
// the viewport's usable area around under the user. cardWidth() grows it by
// what a larger base size actually costs, and by nothing else.
constexpr int kBaseWidth = 240;
// What kBaseWidth was chosen to hold: a body name beside a comfortably large
// dimension string. Specimens, not live content - see cardWidth().
QString nameSpecimen() { return QStringLiteral("Body 88"); }
QString sizeSpecimen() { return QStringLiteral("482.9 × 590 × 10 mm"); }
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

    myTitle = new QLabel(tr("Items"), this);
    myOuter->addWidget(myTitle);
    myTitle->show();   // measured immediately, same reason as the rows in refresh()

    myRows = new QVBoxLayout();
    myRows->setContentsMargins(0, 0, 0, 0);
    myRows->setSpacing(2);
    myOuter->addLayout(myRows);
    myOuter->addStretch(1);

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
    auto measure = [](const QFont& name, const QFont& size) {
        return QFontMetrics(name).horizontalAdvance(nameSpecimen()) +
               QFontMetrics(size).horizontalAdvance(sizeSpecimen());
    };
    const int now = measure(Theme::bodyFont(), Theme::labelFont());
    const int atShippedScale = measure(Theme::bodyFontFor(shipped), Theme::labelFontFor(shipped));
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
        if (row.size)
            row.size->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                   "font-size: %2pt;")
                                        .arg(Theme::textMuted().name())
                                        .arg(Theme::labelFont().pointSizeF()));
        // Rasterised out of text()/textDisabled() when it was built, so it is
        // pixels rather than a description - the same cached-appearance value
        // ToolChip::applyTheme() has to rebuild.
        if (row.eye) row.eye->setIcon(IconSet::icon(IconSet::Glyph::SelectSolid));
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

QSize ItemsPanel::sizeHint() const
{
    const int cw = width() > 0 ? width() : cardWidth();
    if (!myOuter) return QSize(cw, kMinHeight);
    // heightForWidth, not totalSizeHint: the empty-state message word-wraps,
    // and a QBoxLayout's plain size hint asks a wrapping QLabel for a size it
    // can only guess at. Asking for the height AT this card's actual width is
    // the one question that has a right answer.
    const int wrapped = myOuter->hasHeightForWidth() ? myOuter->heightForWidth(cw)
                                                     : myOuter->totalSizeHint().height();
    return QSize(cw, std::max(std::max(wrapped, myOuter->totalMinimumSize().height()),
                              kMinHeight));
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
    // What the rows would SAY if they were rebuilt right now: id, name,
    // dimension text and the eye's state, for every body in order.
    //
    // This exists because refresh() is driven by appStateChanged, which fires
    // at the end of every updateActions() - including the one a Theme edit
    // ends with, and a colour picker emits one per mouse MOVE. Rebuilding the
    // whole list per frame of a drag changes nothing the user can see.
    // Moving the connection to documentChanged instead is NOT the fix:
    // appStateChanged is deliberately what drives this, because a unit switch
    // touches no document and still has to re-read every dimension shown here
    // (see MainWindow's constructor). Comparing what the rows would say keeps
    // that case working - a unit switch changes every dimension string, so it
    // rebuilds - while a theme edit, which changes none of them, does not.
    //
    // Costs one formatDimensions() per body, which is exactly what the
    // rebuild below already paid on every call.
    QString signature;
    if (myDocument) {
        // Outlines first, exactly as the rows are built below. Everything a
        // row DISPLAYS goes into the signature - id, name, extents,
        // visibility - which is the Phase-5 lesson: a field a row shows but
        // the early-out does not compare is a field that stops updating.
        for (const DocumentModel::Outline& outline : myDocument->outlines()) {
            signature += QString::number(outline.id) + QLatin1Char('\x1f') +
                         QString::fromStdString(outline.name) + QLatin1Char('\x1f') +
                         QString::fromStdString(
                             Measure::formatFaceExtents(outline.face, outline.plane)) +
                         QLatin1Char('\x1f') +
                         (myDocument->isVisible(outline.id) ? QLatin1Char('1')
                                                            : QLatin1Char('0')) +
                         QLatin1Char('\x1e');
        }
        for (const DocumentModel::Solid& solid : myDocument->solids()) {
            signature += QString::number(solid.id) + QLatin1Char('\x1f') +
                         QString::fromStdString(solid.name) + QLatin1Char('\x1f') +
                         QString::fromStdString(Measure::formatDimensions(solid.shape)) +
                         QLatin1Char('\x1f') +
                         (myDocument->isVisible(solid.id) ? QLatin1Char('1')
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
    while (QLayoutItem* item = myRows->takeAt(0)) {
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

    // One row builder for both kinds. The two differ only in the text, which
    // visibility channel the eye drives and which signal a click emits -
    // writing the widget construction twice would be two places to fix the
    // next time a row grows a control.
    auto addRow = [this](int id, const QString& itemName, const QString& sizeText,
                         bool visible, bool isOutline) {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(8);

        auto* name = new QLabel(itemName, row);
        name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                .arg(Theme::text().name()));
        // Mouse-TRANSPARENT - Task 5's own fix, the same trap CLAUDE.md
        // documents for InitCardWidget: Qt delivers a click to the DEEPEST
        // widget under the cursor, not to an ancestor whose eventFilter
        // happens to be watching for one, so without this a click landing on
        // the name's own text - exactly where a double-click-to-rename
        // gesture is aimed - never reached this row's eventFilter at all.
        // Neither label has an interactive child of its own to lose by this.
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(name, 1);

        auto* size = new QLabel(sizeText, row);
        // A secondary readout beside the name, sized like a chip label.
        size->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                           "font-size: %2pt;")
                                .arg(Theme::textMuted().name())
                                .arg(Theme::labelFont().pointSizeF()));
        size->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(size);

        auto* eye = new QPushButton(row);
        eye->setCheckable(true);
        eye->setChecked(visible);
        eye->setFixedSize(24, 24);
        eye->setIcon(IconSet::icon(IconSet::Glyph::SelectSolid));
        eye->setToolTip(isOutline ? tr("Show or hide this outline")
                                  : tr("Show or hide this body"));
        connect(eye, &QPushButton::toggled, this, [this, id, isOutline](bool show) {
            // DocumentModel owns visibility now (Task 1's isVisible()/
            // setVisible()); the view is a mirror of it, written second so a
            // save reads back exactly what the eye buttons show rather than
            // a copy that only ever lived in the viewport.
            if (myDocument) myDocument->setVisible(id, show);
            if (!myView) return;
            if (isOutline) myView->setOutlineVisible(id, show);
            else myView->setSolidVisible(id, show);
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

        myRows->addWidget(row);
        // Shown explicitly, and this is not redundant. A widget constructed
        // with a parent starts hidden, and Qt only reveals it when the event
        // loop gets round to the layout request - but QWidgetItem::isEmpty()
        // is `isHidden()`, so until that happens the row contributes a size
        // of (0, 0) and the card measures itself as if the list were empty.
        // That is exactly what happened: sizeHint() said 325 while the drawer
        // sat at 176, its empty-state floor, with eight bodies listed inside
        // it. adjustSize() below has to see the real rows, now, not one event
        // loop turn from now.
        row->show();
        Row entry{row, name, size, eye, id, isOutline,
                  name->text() + QLatin1Char(' ') + size->text()};
        myRowList.push_back(entry);
    };

    // Outlines ABOVE bodies: an outline is the thing the user is about to act
    // on, and it is the newest item in the document whenever one exists.
    for (const DocumentModel::Outline& outline : myDocument->outlines()) {
        addRow(outline.id, QString::fromStdString(outline.name),
               QString::fromStdString(Measure::formatFaceExtents(outline.face, outline.plane)),
               myDocument->isVisible(outline.id), /*isOutline=*/true);
    }
    for (const DocumentModel::Solid& solid : myDocument->solids()) {
        addRow(solid.id, QString::fromStdString(solid.name),
               QString::fromStdString(Measure::formatDimensions(solid.shape)),
               myDocument->isVisible(solid.id), /*isOutline=*/false);
    }

    if (myRowList.empty()) {
        auto* empty = new QLabel(tr("No bodies yet.\n\nPress Ctrl+K and click points on "
                                    "the ground to draw your first outline."),
                                 this);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignTop);
        empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
        myRows->addWidget(empty);
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

bool ItemsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const QVariant outline = watched->property("outlineId");
        if (outline.isValid()) {
            emit outlineActivated(outline.toInt());
            return true;
        }
        const QVariant id = watched->property("solidId");
        if (id.isValid()) {
            emit solidActivated(id.toInt());
            return true;
        }
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
    if (event->type() == QEvent::MouseButtonRelease &&
        (watched->property("solidId").isValid() ||
         watched->property("outlineId").isValid()))
        return true;
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
        // "text cell" InlineRename opens over. Not the whole row: the size
        // readout beside it is derived, not editable, and the eye button is
        // its own control.
        const QRect cellRect = row.name->geometry();
        InlineRename::beginRename(row.widget, cellRect, current,
                                  [this, id, isOutline](QString newName) {
                                      emit renameCommitted(id, isOutline, newName);
                                  });
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
        const bool marked =
            row.isOutline
                ? (myPendingOutlineId != 0 && row.id == myPendingOutlineId)
                : std::find(mySelectedIds.begin(), mySelectedIds.end(), row.id) !=
                      mySelectedIds.end();
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
