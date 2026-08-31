#include "ItemsPanel.h"

#include "DocumentModel.h"
#include "IconSet.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// The card's own width. Fixed rather than min/preferred: this is a floating
// drawer anchored beside a fixed-width rail, and a card that changed width
// with the longest body name would move the viewport's usable area around
// under the user.
constexpr int kWidth = 240;
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

ItemsPanel::ItemsPanel(const DocumentModel* document, OcctViewWidget* view, QWidget* parent)
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
    setFixedWidth(kWidth);

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

    // LAST, and the order is load-bearing: applyTheme() ends in refresh(),
    // which walks myRows, so it cannot run before that layout exists. The
    // title's stylesheet bakes in two Theme values, so it is written there
    // rather than here - and re-written on every Theme broadcast. The ROWS
    // need no such hook: refresh() rebuilds them from Theme every time, and
    // MainWindow already drives it from appStateChanged.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ItemsPanel::applyTheme);
}

void ItemsPanel::applyTheme()
{
    if (!myTitle) return;
    // A per-widget stylesheet wins over the app-wide one regardless of
    // selector specificity, so the size sticks reliably here - this is a
    // panel title, Theme::titleFont(). `background: transparent` is not
    // decoration: the app-wide sheet paints every QWidget chrome-black, and a
    // label that stamps its own rectangle over this card would be a black bar
    // across it.
    myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                          "font-weight: 600; font-size: %2pt;")
                               .arg(Theme::textMuted().name())
                               .arg(Theme::titleFont().pointSizeF()));
    // The card's height comes from its content, and the title just changed
    // size - refresh() is the one path that re-measures this drawer, so it is
    // the one used here rather than a second copy of that arithmetic.
    refresh();
}

QSize ItemsPanel::sizeHint() const
{
    if (!myOuter) return QSize(kWidth, kMinHeight);
    // heightForWidth, not totalSizeHint: the empty-state message word-wraps,
    // and a QBoxLayout's plain size hint asks a wrapping QLabel for a size it
    // can only guess at. Asking for the height AT this card's actual width is
    // the one question that has a right answer.
    const int wrapped = myOuter->hasHeightForWidth() ? myOuter->heightForWidth(kWidth)
                                                     : myOuter->totalSizeHint().height();
    return QSize(kWidth, std::max(std::max(wrapped, myOuter->totalMinimumSize().height()),
                                  kMinHeight));
}

void ItemsPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    // Every pixel of this widget is the card. The rows and labels above it
    // paint no background of their own, so this is what shows between and
    // behind them - and over the GL surface, anything this does not cover is
    // not transparent but whatever the driver left there.
    Theme::paintSurface(painter, rect(), kRadius);
}

void ItemsPanel::refresh()
{
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
    myRowWidgets.clear();
    myRowIds.clear();
    myRowTexts.clear();
    if (!myDocument) return;

    for (const DocumentModel::Solid& solid : myDocument->solids()) {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(8);

        auto* name = new QLabel(QString::fromStdString(solid.name), row);
        name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                .arg(Theme::text().name()));
        layout->addWidget(name, 1);

        auto* size = new QLabel(
            QString::fromStdString(Measure::formatDimensions(solid.shape)), row);
        // A secondary readout beside the name, sized like a chip label.
        size->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                           "font-size: %2pt;")
                                .arg(Theme::textMuted().name())
                                .arg(Theme::labelFont().pointSizeF()));
        layout->addWidget(size);

        auto* eye = new QPushButton(row);
        eye->setCheckable(true);
        eye->setChecked(myView && myView->isSolidVisible(solid.id));
        eye->setFixedSize(24, 24);
        eye->setIcon(IconSet::icon(IconSet::Glyph::SelectSolid));
        eye->setToolTip(tr("Show or hide this body"));
        const int id = solid.id;
        connect(eye, &QPushButton::toggled, this, [this, id](bool visible) {
            if (myView) myView->setSolidVisible(id, visible);
        });
        layout->addWidget(eye);

        // Clicking anywhere on the row selects that solid in the viewport.
        row->installEventFilter(this);
        row->setProperty("solidId", id);
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
        myRowWidgets.push_back(row);
        myRowIds.push_back(id);
        myRowTexts.push_back(name->text() + QLatin1Char(' ') + size->text());
    }

    if (myRowWidgets.empty()) {
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
        const QVariant id = watched->property("solidId");
        if (id.isValid()) {
            emit solidActivated(id.toInt());
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
        watched->property("solidId").isValid())
        return true;
    return QWidget::eventFilter(watched, event);
}

void ItemsPanel::showSelection(const std::vector<int>& ids)
{
    for (std::size_t i = 0; i < myRowWidgets.size(); ++i) {
        const bool selected =
            std::find(ids.begin(), ids.end(), myRowIds[i]) != ids.end();
        myRowWidgets[i]->setStyleSheet(
            selected ? QStringLiteral("#itemsRow { background-color: %1; "
                                      "border-radius: 4px; }")
                           .arg(Theme::chipActive().name())
                     : QStringLiteral("#itemsRow { background: transparent; }"));
    }
}
