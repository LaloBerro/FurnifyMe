#include "VersionsPanel.h"

#include "FurnitureStore.h"
#include "InlineRename.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
// Card width AT THE SHIPPED TYPE SCALE - ItemsPanel::cardWidth()'s own
// reasoning applies here unchanged: fixed rather than min/preferred, so a
// long version name cannot walk the drawer's width around under the user,
// and grown only by what a larger base size actually costs, measured
// against specimen text rather than live content. The name and the meta
// line are STACKED now, not side by side, so the measurement is the wider
// of the two specimens rather than their sum.
constexpr int kBaseWidth = 240;
QString nameSpecimen() { return QStringLiteral("A version name that runs a little long"); }
QString dateSpecimen() { return QStringLiteral("Jan 1, 2026, 12:00 PM"); }
constexpr int kRadius = 10;
constexpr int kCardRadius = 8;
constexpr int kPad = 12;
constexpr int kMinHeight = 160;
constexpr int kButtonHeight = 22;
// 16:10-ish, per the picked mockup.
constexpr double kThumbAspect = 10.0 / 16.0;
// fix round 2 (review, Important 1): keeps every child of a card - the
// thumbnail most of all - clear of the card's own crisp 1px border rather
// than flush with it. A card's paintEvent() (VersionCardWidget below)
// paints the WHOLE rounded shape - ground, panel fill, border - and Qt
// then paints children ON TOP; a thumbnail with a real pixmap fills its
// own label's full rect edge to edge, which is EXACTLY the rect the
// border occupies along the top row and the left/right columns for the
// thumbnail's own height, so a real photo painted flush overwrote the
// border there while the placeholder (a transparent label with no
// pixmap, painting nothing) never touched it - the two states looked
// like two different cards. A few px of inset is invisible at this
// card's size and keeps the border the SAME regardless of what a child
// paints inside it.
constexpr int kCardBorderInset = 3;
// fix round 2 (review, Important 2): Compare/Restore/Delete measured at
// 29px wide against a 70/60/54px sizeHint when they shared the name bar's
// own row with the name/date column - three buttons plus a name column
// simply do not fit side by side at this card's ~216px inner width (see
// this task's report for the numbers). Given their own row below the bar
// instead - right-aligned, matching the picked mockup's "actions at the
// bar's right" as closely as the width allows - which comfortably holds
// all three at their natural size. Always present at this FIXED height
// (never hidden itself - only the buttons inside it toggle), so revealing
// them on hover does not grow the card and shove every row below it down
// the drawer.
constexpr int kActionsRowHeight = 34;

// One version's own card - the thumbnail-plus-bar shape both a real,
// already-saved row and the in-progress "create" gesture share. A plain
// QWidget cannot paint its own rounded card background (see
// InitScreen.cpp's InitCardWidget for the exact same reasoning), so this
// is the one place that does: Theme::paintSurface() with `ground` =
// Theme::panel(), the colour actually sitting behind it (this widget is a
// child of VersionsPanel, not of the viewport - see paintSurface()'s own
// comment on why the ground parameter exists at all).
class VersionCardWidget : public QWidget {
public:
    explicit VersionCardWidget(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        Theme::paintSurface(painter, rect(), kCardRadius, Theme::panel());
    }
};

QString buttonCss()
{
    // "small bordered buttons" - the picked mockup's own words for
    // Compare/Restore/Delete, unlike the borderless chip style the rest of
    // this app's small buttons wear. A real border rather than a hand-
    // painted one: these are plain QPushButtons, the same idiom
    // BevelArrow/ExtrudePreview/PullArrow's own field chrome already uses
    // for a 1px border on a control sitting over the GL surface.
    return QStringLiteral(
               "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
               "border-radius: 4px; font-size: %4pt; padding: 2px 8px; } "
               "QPushButton:hover { background-color: %5; } "
               "QPushButton:disabled { color: %6; border-color: %6; }")
        .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name())
        .arg(Theme::labelFont().pointSizeF())
        .arg(Theme::chipHover().name(), Theme::textDisabled().name());
}
}  // namespace

VersionsPanel::VersionsPanel(MainWindow* window, OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    setFixedWidth(cardWidth());

    myOuter = new QVBoxLayout(this);
    myOuter->setContentsMargins(kPad, kPad, kPad, kPad);
    myOuter->setSpacing(10);

    auto* header = new QWidget(this);
    header->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    myTitle = new QLabel(tr("Versions"), header);
    headerLayout->addWidget(myTitle, 1);

    myAddButton = new QPushButton(QStringLiteral("+"), header);
    myAddButton->setFixedSize(Theme::wholeDevicePixels(QSize(26, 26)));
    myAddButton->setToolTip(tr("Save a new version"));
    headerLayout->addWidget(myAddButton);
    connect(myAddButton, &QPushButton::clicked, this, &VersionsPanel::beginNewVersion);

    myOuter->addWidget(header);

    myRowsLayout = new QVBoxLayout();
    myRowsLayout->setContentsMargins(0, 0, 0, 0);
    myRowsLayout->setSpacing(10);
    myOuter->addLayout(myRowsLayout);
    myOuter->addStretch(1);

    // refresh() ends by calling applyTheme() itself - see its own comment -
    // so nothing further is needed here on first build.
    refresh();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &VersionsPanel::applyTheme);
}

int VersionsPanel::cardWidth()
{
    const Theme::Spec shipped = Theme::defaultSpec();
    auto measure = [](const QFont& name, const QFont& date) {
        return std::max(QFontMetrics(name).horizontalAdvance(nameSpecimen()),
                        QFontMetrics(date).horizontalAdvance(dateSpecimen()));
    };
    const int now = measure(Theme::bodyFont(), Theme::labelFont());
    const int atShippedScale = measure(Theme::bodyFontFor(shipped), Theme::labelFontFor(shipped));
    return std::max(kBaseWidth, kBaseWidth + now - atShippedScale);
}

int VersionsPanel::cardInnerWidth()
{
    return cardWidth() - 2 * kPad;
}

int VersionsPanel::thumbHeightFor(int thumbWidth)
{
    return Theme::wholeDevicePixels(static_cast<int>(std::lround(thumbWidth * kThumbAspect)));
}

QString VersionsPanel::deleteLabel() { return tr("Delete"); }
QString VersionsPanel::deleteArmedLabel() { return tr("Delete — click again"); }

QStringList VersionsPanel::paintedTexts() const
{
    return {tr("Versions"), tr("Save a new version"),
            tr("No versions yet.\n\nPress + to keep a named snapshot you can come "
               "back to."),
            deleteLabel(), deleteArmedLabel(), tr("Compare"), tr("Restore")};
}

void VersionsPanel::applyTheme()
{
    if (myTitle) {
        myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                              "font-weight: 600; font-size: %2pt;")
                                   .arg(Theme::textMuted().name())
                                   .arg(Theme::titleFont().pointSizeF()));
    }
    if (myAddButton) {
        // Accent-bordered, per the picked mockup - the one control on this
        // panel that draws attention to itself, since it is the only way
        // left to make a version now that SaveVersionCard is retired.
        myAddButton->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; "
                          "border: 1.5px solid %3; border-radius: 6px; "
                          "font-weight: 600; font-size: %4pt; } "
                          "QPushButton:hover { background-color: %5; } "
                          "QPushButton:disabled { color: %6; border-color: %6; }")
                .arg(Theme::chip().name(), Theme::text().name(), Theme::accent().name())
                .arg(Theme::bodyFont().pointSizeF())
                .arg(Theme::chipHover().name(), Theme::textDisabled().name()));
    }

    const QString css = buttonCss();
    for (const Row& row : myRows) {
        if (row.name)
            row.name->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                    "font-weight: 500; font-size: %2pt;")
                                        .arg(Theme::text().name())
                                        .arg(Theme::bodyFont().pointSizeF()));
        if (row.meta)
            row.meta->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                    "font-size: %2pt;")
                                        .arg(Theme::textMuted().name())
                                        .arg(Theme::labelFont().pointSizeF()));
        for (QPushButton* button : {row.compare, row.restore, row.remove}) {
            if (button) button->setStyleSheet(css);
        }
        if (row.widget) row.widget->update();
    }

    if (QLabel* empty = findChild<QLabel*>(QStringLiteral("versionsEmptyState"))) {
        empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
    }

    setFixedWidth(cardWidth());
    myRowsLayout->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    updateGeometry();
    adjustSize();
}

QSize VersionsPanel::sizeHint() const
{
    const int cw = width() > 0 ? width() : cardWidth();
    if (!myOuter) return QSize(cw, kMinHeight);
    const int wrapped = myOuter->hasHeightForWidth() ? myOuter->heightForWidth(cw)
                                                     : myOuter->totalSizeHint().height();
    return QSize(cw, std::max(std::max(wrapped, myOuter->totalMinimumSize().height()),
                              kMinHeight));
}

void VersionsPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    Theme::paintSurface(painter, rect(), kRadius);
}

void VersionsPanel::setVisible(bool visible)
{
    QWidget::setVisible(visible);
    // Derived, not set - see the header's own comment. This is the route
    // MainWindow's View -> Versions toggle actually takes (a direct
    // setVisible() call on this widget, never through
    // updateActions()/appStateChanged), so it is also the one place that
    // has to catch it: canOpenSaveVersion()'s own auto-cancel inside
    // refresh() only runs when appStateChanged fires, which a plain drawer
    // close never does. Checked AFTER the base call, not before - the
    // guard only cares what visibility this widget is ABOUT to have, not
    // whatever bookkeeping Qt's own setVisible() does on the way.
    if (!visible && myPendingWidget) discardPendingCreate();
}

void VersionsPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // The backstop half of setVisible()'s own rule above. An ANCESTOR
    // being hidden delivers a QHideEvent straight to this widget without
    // ever calling ITS setVisible() - Qt propagates a parent's hide to
    // children by posting each child a QHideEvent directly, not by
    // invoking the child's own virtual setVisible(). Neither route alone
    // is guaranteed to fire for every way this panel can stop being
    // shown, so the cancel lives in both.
    if (myPendingWidget) discardPendingCreate();
}

bool VersionsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        const QVariant key = watched->property("versionCardKey");
        if (key.isValid()) {
            const QString name = key.toString();
            for (Row& row : myRows) {
                if (row.versionName != name) continue;
                if (row.actions) row.actions->setVisible(event->type() == QEvent::Enter);
                break;
            }
        }
    } else if (event->type() == QEvent::KeyPress && watched == myPendingEdit) {
        // See the header's own comment on myPendingEdit/teardownPendingCard()
        // for why this exists beside InlineRename's own Escape QShortcut.
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier) {
            discardPendingCreate();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString VersionsPanel::rowNameAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].versionName
                                                                 : QString();
}

QWidget* VersionsPanel::cardAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].widget
                                                                 : nullptr;
}

QRect VersionsPanel::thumbnailRectAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(myRows.size()) || !myRows[index].thumb)
        return QRect();
    return myRows[index].thumb->geometry();
}

QRect VersionsPanel::nameRectAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(myRows.size())) return QRect();
    const Row& row = myRows[index];
    if (!row.name || !row.widget) return QRect();
    // Unlike thumb (a direct child of the card), name's own parent is
    // textCol - mapTo() is what makes this comparable, in the SAME
    // coordinate space, against cardAt(index)'s own rect and a button's
    // own mapTo(cardAt(index), ...) rect.
    return QRect(row.name->mapTo(row.widget, QPoint(0, 0)), row.name->size());
}

QPushButton* VersionsPanel::compareButtonAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].compare
                                                                 : nullptr;
}

QPushButton* VersionsPanel::restoreButtonAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].restore
                                                                 : nullptr;
}

QPushButton* VersionsPanel::deleteButtonAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].remove
                                                                 : nullptr;
}

int VersionsPanel::deleteArmedMsFor(const QString& name) const
{
    for (const Row& row : myRows) {
        if (row.versionName != name) continue;
        return (row.deleteArmed && row.deleteTimer && row.deleteTimer->isActive())
                   ? row.deleteTimer->remainingTime()
                   : -1;
    }
    return -1;
}

void VersionsPanel::disarmDelete(Row& row)
{
    row.deleteArmed = false;
    if (row.deleteTimer) row.deleteTimer->stop();
    if (row.remove) row.remove->setText(deleteLabel());
}

QString VersionsPanel::nextVersionDefaultName() const
{
    int highest = 0;
    if (myWindow && !myWindow->currentFurnitureId().isEmpty()) {
        static const QRegularExpression pattern(QStringLiteral("^Version (\\d+)$"));
        for (const FurnitureStore::VersionInfo& v :
             myWindow->furnitureStore().versions(myWindow->currentFurnitureId())) {
            const QRegularExpressionMatch m = pattern.match(v.name);
            if (m.hasMatch()) highest = std::max(highest, m.captured(1).toInt());
        }
    }
    return tr("Version %1").arg(highest + 1);
}

void VersionsPanel::refresh()
{
    // Mirrors SaveVersionCard::onAppStateChanged()'s own reasoning (now
    // retired, but the reasoning is not): a create gesture open over this
    // panel is exactly as vulnerable to a real, competing application-wide
    // claim - a sketch started, render mode engaged, a face pulled or an
    // edge bevelled - as the old floating card was. Nothing was ever
    // written for a pending create (see the class comment), so "cancel" is
    // simply tearing the pending card down, not undoing a save.
    if (myPendingWidget && myWindow && !myWindow->canOpenSaveVersion())
        discardPendingCreate();

    QVector<FurnitureStore::VersionInfo> versions;
    if (myWindow && !myWindow->currentFurnitureId().isEmpty())
        versions = myWindow->furnitureStore().versions(myWindow->currentFurnitureId());

    // The same early-out ItemsPanel::refresh() uses, and for the same
    // reason: this is driven by appStateChanged, which fires on every
    // theme edit too (a colour dragged fires per mouse move), and nothing
    // about a theme change alters which versions exist.
    QString signature;
    for (const FurnitureStore::VersionInfo& v : versions) {
        signature += v.name + QLatin1Char('\x1f') + v.saved.toString(Qt::ISODateWithMs) +
                    QLatin1Char('\x1e');
    }

    const bool rowsEnabled =
        myWindow && !myWindow->isShowingInitScreen() && !myWindow->isSketching();
    // The + button's own gate mirrors exactly what used to gate the old
    // "Save version..." action (MainWindow::canOpenSaveVersion()) - it is
    // the SAME gesture, reached a different way - plus "no create already
    // open", since one gesture is one pending card.
    if (myAddButton)
        myAddButton->setEnabled(!myPendingWidget && myWindow &&
                                myWindow->canOpenSaveVersion());

    if (myRowsBuilt && signature == myRowSignature) {
        for (const Row& row : myRows) {
            if (row.compare) row.compare->setEnabled(rowsEnabled);
            if (row.restore) row.restore->setEnabled(rowsEnabled);
            if (row.remove) row.remove->setEnabled(rowsEnabled);
        }
        return;
    }
    myRowSignature = signature;
    myRowsBuilt = true;

    while (QLayoutItem* item = myRowsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    myRows.clear();

    for (const FurnitureStore::VersionInfo& v : versions)
        buildRealRow(v.name, v.saved, rowsEnabled);

    if (myRows.empty()) {
        auto* empty = new QLabel(tr("No versions yet.\n\nPress + to keep a named "
                                    "snapshot you can come back to."),
                                 this);
        empty->setObjectName(QStringLiteral("versionsEmptyState"));
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignTop);
        empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
        myRowsLayout->addWidget(empty);
        empty->show();
    }

    myRowsLayout->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    updateGeometry();
    adjustSize();

    // The rows just built need the live theme's colours - applyTheme() is
    // what the constructor calls after this on first build, but every
    // LATER refresh() (a version saved or deleted) rebuilds rows with no
    // theme broadcast to follow, so this call is what keeps a freshly
    // rebuilt row from painting Qt's own default button chrome instead of
    // this app's.
    applyTheme();
}

void VersionsPanel::buildRealRow(const QString& name, const QDateTime& saved, bool enabled)
{
    auto* card = new VersionCardWidget(this);
    auto* cardLayout = new QVBoxLayout(card);
    // kCardBorderInset on every side - see its own comment: nothing this
    // card lays out may ever be flush with the rect the crisp border
    // paints along, or a real thumbnail's own full-bleed pixmap erases it.
    cardLayout->setContentsMargins(kCardBorderInset, kCardBorderInset, kCardBorderInset,
                                   kCardBorderInset);
    cardLayout->setSpacing(0);

    // The full-width thumbnail (full width of the INSET content area, not
    // of the card's own outer rect - see kCardBorderInset). A transparent
    // QLabel with no pixmap shows the card's OWN paintSurface() ground
    // through it unmangled - that flat fill IS the "flat neutral
    // placeholder block" the mockup calls for, not a second thing this
    // code has to paint - see thumbnailRectAt()'s own comment for why a
    // test has to render the CARD to see it.
    const int contentWidth = cardInnerWidth() - 2 * kCardBorderInset;
    auto* thumb = new QLabel(card);
    thumb->setFixedHeight(thumbHeightFor(contentWidth));
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
    QPixmap pix;
    const QString thumbPath =
        myWindow ? myWindow->furnitureStore().versionThumbPath(myWindow->currentFurnitureId(),
                                                               name)
                : QString();
    if (!thumbPath.isEmpty() && QFileInfo::exists(thumbPath) && pix.load(thumbPath)) {
        thumb->setPixmap(pix.scaled(thumb->size(), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
    } else {
        thumb->setPixmap(QPixmap());
    }
    cardLayout->addWidget(thumb);

    auto* bar = new QWidget(card);
    bar->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(10, 8, 10, 8);

    auto* textCol = new QWidget(bar);
    textCol->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    auto* nameLabel = new QLabel(name, textCol);
    nameLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-weight: 500; font-size: %2pt;")
                                 .arg(Theme::text().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    textLayout->addWidget(nameLabel);

    auto* metaLabel =
        new QLabel(QLocale().toString(saved.toLocalTime(), QLocale::ShortFormat), textCol);
    metaLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::labelFont().pointSizeF()));
    metaLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    textLayout->addWidget(metaLabel);

    // The name/date column is the ONLY thing in this row now - see
    // kActionsRowHeight's own comment for why the three buttons moved to
    // a row of their own below rather than sharing this one.
    barLayout->addWidget(textCol);
    cardLayout->addWidget(bar);

    // The actions row - ALWAYS present, at a fixed height, so revealing
    // its buttons on hover never changes the card's own size (which would
    // shove every row below it down the drawer). Right-aligned via the
    // leading stretch, the closest this width can come to the picked
    // mockup's "actions at the bar's right" once they no longer share the
    // bar itself.
    auto* actionsRow = new QWidget(card);
    actionsRow->setStyleSheet(QStringLiteral("background: transparent;"));
    actionsRow->setFixedHeight(kActionsRowHeight);
    auto* actionsRowLayout = new QHBoxLayout(actionsRow);
    actionsRowLayout->setContentsMargins(10, 6, 10, 6);
    actionsRowLayout->addStretch(1);

    auto* actions = new QWidget(actionsRow);
    actions->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(4);

    auto* compare = new QPushButton(tr("Compare"), actions);
    compare->setFixedHeight(kButtonHeight);
    compare->setEnabled(enabled);
    actionsLayout->addWidget(compare);

    auto* restore = new QPushButton(tr("Restore"), actions);
    restore->setFixedHeight(kButtonHeight);
    restore->setEnabled(enabled);
    actionsLayout->addWidget(restore);

    auto* remove = new QPushButton(deleteLabel(), actions);
    remove->setFixedHeight(kButtonHeight);
    remove->setEnabled(enabled);
    actionsLayout->addWidget(remove);

    actionsRowLayout->addWidget(actions);

    // Hidden at rest - "revealed on ROW HOVER only" is the mockup's own
    // words. See eventFilter() for what shows it again. Only `actions`
    // toggles, never `actionsRow` itself - see kActionsRowHeight's comment.
    actions->setVisible(false);
    cardLayout->addWidget(actionsRow);

    card->setProperty("versionCardKey", name);
    card->setAttribute(Qt::WA_Hover);
    card->installEventFilter(this);

    Row entry;
    entry.widget = card;
    entry.thumb = thumb;
    entry.name = nameLabel;
    entry.meta = metaLabel;
    entry.actions = actions;
    entry.compare = compare;
    entry.restore = restore;
    entry.remove = remove;
    entry.versionName = name;
    entry.deleteTimer = new QTimer(card);
    entry.deleteTimer->setSingleShot(true);
    entry.deleteTimer->setInterval(kDeleteConfirmMs);

    myRows.push_back(entry);
    const int rowIndex = static_cast<int>(myRows.size()) - 1;

    connect(compare, &QPushButton::clicked, this, [this, rowIndex] {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
        // Copied to a LOCAL before the call, not passed as a reference into
        // the Row - MainWindow::openCompare() ends in updateActions(),
        // which can run this panel's own refresh() synchronously, and a
        // refresh that rebuilds rows destroys the very Row this reference
        // would still be pointing into for the rest of the call.
        const QString versionName = myRows[rowIndex].versionName;
        if (myWindow) myWindow->openCompare(versionName);
    });
    connect(restore, &QPushButton::clicked, this, [this, rowIndex] {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
        const QString versionName = myRows[rowIndex].versionName;   // see compare's lambda above
        if (myWindow) myWindow->restoreVersion(versionName);
    });
    // The two-click confirmation. A first click arms it - the label
    // changes and the timer starts - and does NOT delete anything; a
    // second click while still armed is the one that actually calls
    // through to MainWindow. The timer's own timeout disarms it on its
    // own if the second click never comes, through the exact same
    // disarmDelete() a rebuild uses to tear an armed row down cleanly.
    connect(remove, &QPushButton::clicked, this, [this, rowIndex] {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
        Row& r = myRows[rowIndex];
        if (r.deleteArmed) {
            // A LOCAL copy, for the same reason compare's and restore's
            // lambdas take one: deleteVersionByName() ends in
            // updateActions(), which WILL rebuild this panel's rows
            // synchronously (the version list itself just changed, so
            // refresh()'s signature check cannot take its early-out) - a
            // reference into `r` would be dangling before its own
            // statement finished evaluating.
            const QString versionName = r.versionName;
            disarmDelete(r);
            if (myWindow) myWindow->deleteVersionByName(versionName);
            // `r`, and every other reference into myRows, may now be
            // dangling - nothing below this line may touch them again.
            return;
        }
        r.deleteArmed = true;
        if (r.remove) r.remove->setText(deleteArmedLabel());
        if (r.deleteTimer) r.deleteTimer->start();
    });
    connect(entry.deleteTimer, &QTimer::timeout, this, [this, rowIndex] {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
        disarmDelete(myRows[rowIndex]);
    });

    myRowsLayout->addWidget(card);
    card->show();
}

QWidget* VersionsPanel::buildPendingCard(const QString& defaultName)
{
    auto* card = new VersionCardWidget(this);
    auto* cardLayout = new QVBoxLayout(card);
    // Same kCardBorderInset as buildRealRow() - the pending card wears no
    // real thumbnail today (see its own comment below), but nothing about
    // that is a promise this card's layout gets to rely on going forward,
    // and the two cards should look identical in every way that is not
    // literally the create-gesture itself.
    cardLayout->setContentsMargins(kCardBorderInset, kCardBorderInset, kCardBorderInset,
                                   kCardBorderInset);
    cardLayout->setSpacing(0);

    // A placeholder thumbnail - the pending card has no saved snapshot yet
    // (nothing has been written to FurnitureStore), and the real one is
    // captured by MainWindow::saveVersion() itself the moment Enter
    // actually persists this version.
    auto* thumb = new QLabel(card);
    thumb->setFixedHeight(thumbHeightFor(cardInnerWidth() - 2 * kCardBorderInset));
    thumb->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(thumb);

    auto* bar = new QWidget(card);
    bar->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(10, 8, 10, 8);

    auto* nameLabel = new QLabel(defaultName, bar);
    nameLabel->setObjectName(QStringLiteral("pendingVersionName"));
    nameLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-weight: 500; font-size: %2pt;")
                                 .arg(Theme::text().name())
                                 .arg(Theme::bodyFont().pointSizeF()));
    barLayout->addWidget(nameLabel, 1);
    cardLayout->addWidget(bar);

    return card;
}

void VersionsPanel::beginNewVersion()
{
    if (!myWindow || !myWindow->canOpenSaveVersion()) return;
    if (myPendingWidget) return;   // one create gesture at a time

    // Note the one behaviour this inherits rather than chooses:
    // InlineRename::beginRename() (called below, via openPendingNameEdit())
    // wires QLineEdit::editingFinished() - which fires on Enter AND on an
    // ordinary focus-out, by the helper's own documented, app-wide contract
    // - to the same commit path Enter uses, so clicking away before typing
    // anything commits a real version under the scanned default name. Kept
    // deliberately (see the review ruling this task carries forward): one
    // rename helper, one focus-out semantic, used everywhere it is used.
    const QString defaultName = nextVersionDefaultName();
    myPendingWidget = buildPendingCard(defaultName);
    // Above the real rows, below the header - myOuter's index 0 is the
    // header widget and index 1 is myRowsLayout (added as a nested
    // layout), so inserting a WIDGET at 1 lands it exactly between them.
    myOuter->insertWidget(1, myPendingWidget);
    myPendingWidget->show();
    if (myAddButton) myAddButton->setEnabled(false);

    openPendingNameEdit(defaultName);
}

void VersionsPanel::openPendingNameEdit(const QString& seedText)
{
    if (!myPendingWidget) return;
    QLabel* nameLabel =
        myPendingWidget->findChild<QLabel*>(QStringLiteral("pendingVersionName"));
    if (!nameLabel) return;
    nameLabel->setText(seedText);

    // Guards against a double outcome the exact way InlineRename's own
    // `settled` flag does: my commit lambda runs SYNCHRONOUSLY, inside
    // InlineRename's doCommit(), before it calls edit->deleteLater() - so
    // by the time the edit's destroyed() signal below actually fires
    // (always later - deleteLater() only posts a deferred-delete event),
    // this flag already says whether that was a real commit or an Escape.
    auto committed = std::make_shared<bool>(false);
    InlineRename::beginRename(myPendingWidget, nameLabel->geometry(), seedText,
                              [this, committed](QString finalName) {
                                  *committed = true;
                                  commitPendingCreate(finalName);
                              });

    // InlineRename has no cancel callback of its own - by design, every
    // OTHER caller (InitScreen, ItemsPanel) wants Escape to leave the OLD
    // name standing, which needs no cleanup at all. This gesture is
    // different: there is no "old" card to fall back to, so Escape has to
    // tear the whole pending card down, not just abandon the rename. The
    // edit's own destroyed() signal - which fires whether it was Enter's
    // deleteLater() or Escape's - is what stands in for that missing hook:
    // if `committed` is still false when it fires, nothing committed this
    // gesture, and discardPendingCreate() is the only remaining outcome.
    //
    // findChildren(), taking the LAST one - not findChild(), which returns
    // the FIRST match. On a duplicate-name retry this runs from INSIDE the
    // outgoing edit's own commit handling (commitPendingCreate() calls this
    // again before that edit's caller gets back to its own
    // edit->deleteLater()), so the OLD edit is still transiently a live
    // child of myPendingWidget alongside the brand new one - findChild()
    // would find the OLD one first (children are appended in construction
    // order) and wire this connection to an edit that is seconds from being
    // destroyed regardless of what the user does with the new one, tearing
    // the retry card down out from under them. The new edit, just
    // constructed by beginRename() above, is always last in the list.
    const QList<QLineEdit*> edits = myPendingWidget->findChildren<QLineEdit*>();
    if (!edits.isEmpty()) {
        myPendingEdit = edits.last();
        // Belt-and-suspenders Escape delivery - see the header's own
        // comment on myPendingEdit for why a plain eventFilter sits beside
        // InlineRename's own QShortcut here rather than replacing it.
        myPendingEdit->installEventFilter(this);
        connect(edits.last(), &QObject::destroyed, this, [this, committed] {
            if (!*committed) discardPendingCreate();
        });
    }
}

void VersionsPanel::commitPendingCreate(const QString& name)
{
    // The exact call the old "Save version..." menu action made - one
    // implementation of "persist a version", reached two ways.
    if (myWindow && myWindow->saveVersion(name)) {
        teardownPendingCard();
        return;
    }
    // Refused - a duplicate version name, the only real refusal
    // MainWindow::saveVersion() has (it already showed the Failure toast
    // naming the clash). The card stays up so the user can retype, the
    // same thing SaveVersionCard used to do on this exact refusal -
    // InlineRename's own edit has already destroyed itself unconditionally
    // (doCommit() calls deleteLater() regardless of outcome), so this
    // reopens a fresh one, seeded with what was just tried.
    openPendingNameEdit(name);
}

void VersionsPanel::discardPendingCreate()
{
    // Nothing was ever written to FurnitureStore for a pending create (see
    // the class comment) - discarding is exactly teardownPendingCard() and
    // nothing more, which is the whole point: "no unnamed version left
    // behind" is true because nothing was ever named IN THE STORE, not
    // because this function went and deleted one.
    teardownPendingCard();
}

void VersionsPanel::teardownPendingCard()
{
    if (!myPendingWidget) return;
    myOuter->removeWidget(myPendingWidget);
    // hide() before deleteLater(), not instead of it - ItemsPanel::refresh()'s
    // own reasoning: deleteLater() leaves the widget alive, parented and
    // visible until control returns to the event loop, and this may be
    // called from inside the very QLineEdit child this widget still holds
    // (InlineRename's own event handling) - a synchronous delete here
    // would free memory a still-executing call frame is about to use. hide()
    // is what a caller can actually observe SYNCHRONOUSLY - the object's
    // real destruction can be delayed well past this call (measured: a
    // deleteLater() posted from inside setRenderMode()'s own tier probe sat
    // undelivered through many later settle()s, only actually destroyed at
    // the test binary's own shutdown - QEvent::DeferredDelete is tagged
    // with the event-loop NESTING LEVEL it was posted at, and is skipped by
    // sendPostedEvents() until execution returns to at least that depth
    // again), so hidden-ness, not existence, is the property any caller -
    // this file's own tests included - should ever assert against.
    myPendingWidget->hide();
    myPendingWidget->deleteLater();
    myPendingWidget = nullptr;
    if (myAddButton)
        myAddButton->setEnabled(myWindow && myWindow->canOpenSaveVersion());
}
