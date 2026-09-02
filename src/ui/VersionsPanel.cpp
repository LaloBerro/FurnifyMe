#include "VersionsPanel.h"

#include "FurnitureStore.h"
#include "MainWindow.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// Card width AT THE SHIPPED TYPE SCALE - ItemsPanel::cardWidth()'s own
// reasoning applies here unchanged: fixed rather than min/preferred, so a
// long version name cannot walk the drawer's width around under the user,
// and grown only by what a larger base size actually costs, measured
// against specimen text rather than live content.
constexpr int kBaseWidth = 240;
QString nameSpecimen() { return QStringLiteral("A version name"); }
QString dateSpecimen() { return QStringLiteral("Jan 1, 2026"); }
constexpr int kRadius = 10;
constexpr int kPad = 12;
constexpr int kMinHeight = 120;
constexpr int kButtonHeight = 22;
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
    myOuter->setSpacing(8);

    myTitle = new QLabel(tr("Versions"), this);
    myOuter->addWidget(myTitle);
    myTitle->show();

    myRowsLayout = new QVBoxLayout();
    myRowsLayout->setContentsMargins(0, 0, 0, 0);
    myRowsLayout->setSpacing(6);
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
        return QFontMetrics(name).horizontalAdvance(nameSpecimen()) +
               QFontMetrics(date).horizontalAdvance(dateSpecimen());
    };
    const int now = measure(Theme::bodyFont(), Theme::labelFont());
    const int atShippedScale = measure(Theme::bodyFontFor(shipped), Theme::labelFontFor(shipped));
    return std::max(kBaseWidth, kBaseWidth + now - atShippedScale);
}

QString VersionsPanel::deleteLabel() { return tr("Delete"); }
QString VersionsPanel::deleteArmedLabel() { return tr("Delete — click again"); }

QStringList VersionsPanel::paintedTexts() const
{
    return {tr("Versions"),
            tr("No versions yet.\n\nFile \xE2\x86\x92 Save version\xE2\x80\xA6 keeps a named "
               "snapshot you can come back to."),
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

    for (const Row& row : myRows) {
        if (row.name)
            row.name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                        .arg(Theme::text().name()));
        if (row.saved)
            row.saved->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                     "font-size: %2pt;")
                                         .arg(Theme::textMuted().name())
                                         .arg(Theme::labelFont().pointSizeF()));
        const QString buttonCss =
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: none; "
                          "border-radius: 4px; font-size: %3pt; } "
                          "QPushButton:hover { background-color: %4; } "
                          "QPushButton:disabled { color: %5; }")
                .arg(Theme::chip().name(), Theme::text().name())
                .arg(Theme::labelFont().pointSizeF())
                .arg(Theme::chipHover().name(), Theme::textDisabled().name());
        for (QPushButton* button : {row.compare, row.restore, row.remove}) {
            if (button) button->setStyleSheet(buttonCss);
        }
    }

    if (myRows.empty()) {
        for (QLabel* empty : findChildren<QLabel*>()) {
            if (empty == myTitle) continue;
            empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                "font-size: %2pt;")
                                     .arg(Theme::textMuted().name())
                                     .arg(Theme::bodyFont().pointSizeF()));
        }
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

QString VersionsPanel::rowNameAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? myRows[index].versionName
                                                                 : QString();
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

void VersionsPanel::refresh()
{
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
    // Whether the row controls should be usable at all right now - reread on
    // EVERY call, including the early-out below: sketching or leaving the
    // init screen changes this without touching the version list itself
    // (the signature), and a row whose buttons only updated when a version
    // was added or removed would stay clickable mid-sketch until the next
    // unrelated change happened to rebuild it.
    const bool enabled =
        myWindow && !myWindow->isShowingInitScreen() && !myWindow->isSketching();
    if (myRowsBuilt && signature == myRowSignature) {
        for (const Row& row : myRows) {
            if (row.compare) row.compare->setEnabled(enabled);
            if (row.restore) row.restore->setEnabled(enabled);
            if (row.remove) row.remove->setEnabled(enabled);
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

    for (const FurnitureStore::VersionInfo& v : versions) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(2);

        auto* top = new QHBoxLayout();
        top->setContentsMargins(0, 0, 0, 0);
        top->setSpacing(8);
        auto* name = new QLabel(v.name, row);
        name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                .arg(Theme::text().name()));
        top->addWidget(name, 1);
        auto* saved = new QLabel(QLocale().toString(v.saved.toLocalTime(), QLocale::ShortFormat),
                                 row);
        saved->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::labelFont().pointSizeF()));
        top->addWidget(saved);
        rowLayout->addLayout(top);

        auto* buttons = new QHBoxLayout();
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(4);

        auto* compare = new QPushButton(tr("Compare"), row);
        compare->setFixedHeight(kButtonHeight);
        compare->setEnabled(enabled);
        buttons->addWidget(compare);

        auto* restore = new QPushButton(tr("Restore"), row);
        restore->setFixedHeight(kButtonHeight);
        restore->setEnabled(enabled);
        buttons->addWidget(restore);

        auto* remove = new QPushButton(deleteLabel(), row);
        remove->setFixedHeight(kButtonHeight);
        remove->setEnabled(enabled);
        buttons->addWidget(remove);

        rowLayout->addLayout(buttons);

        Row entry;
        entry.widget = row;
        entry.name = name;
        entry.saved = saved;
        entry.compare = compare;
        entry.restore = restore;
        entry.remove = remove;
        entry.versionName = v.name;
        entry.deleteTimer = new QTimer(row);
        entry.deleteTimer->setSingleShot(true);
        entry.deleteTimer->setInterval(kDeleteConfirmMs);

        myRows.push_back(entry);
        const int rowIndex = static_cast<int>(myRows.size()) - 1;

        connect(compare, &QPushButton::clicked, this, [this, rowIndex] {
            if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
            // Copied to a LOCAL before the call, not passed as a reference
            // into the Row - MainWindow::openCompare() ends in
            // updateActions(), which can run this panel's own refresh()
            // synchronously, and a refresh that rebuilds rows destroys the
            // very Row this reference would still be pointing into for the
            // rest of the call.
            const QString name = myRows[rowIndex].versionName;
            if (myWindow) myWindow->openCompare(name);
        });
        connect(restore, &QPushButton::clicked, this, [this, rowIndex] {
            if (rowIndex < 0 || rowIndex >= static_cast<int>(myRows.size())) return;
            const QString name = myRows[rowIndex].versionName;   // see compare's lambda above
            if (myWindow) myWindow->restoreVersion(name);
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
                // refresh()'s signature check cannot take its early-out) -
                // a reference into `r` would be dangling before its own
                // statement finished evaluating.
                const QString name = r.versionName;
                disarmDelete(r);
                if (myWindow) myWindow->deleteVersionByName(name);
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

        myRowsLayout->addWidget(row);
        row->show();
    }

    if (myRows.empty()) {
        auto* empty = new QLabel(tr("No versions yet.\n\nFile \xE2\x86\x92 Save version\xE2\x80\xA6 "
                                    "keeps a named snapshot you can come back to."),
                                 this);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignTop);
        empty->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
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

    // The buttons just built need the live theme's colours - applyTheme()
    // is what the constructor calls after this on first build, but every
    // LATER refresh() (a version saved or deleted) rebuilds rows with no
    // theme broadcast to follow, so this call is what keeps a freshly
    // rebuilt row from painting Qt's own default button chrome instead of
    // this app's.
    applyTheme();
}
