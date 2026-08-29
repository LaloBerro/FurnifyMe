#include "ItemsPanel.h"

#include "DocumentModel.h"
#include "IconSet.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

ItemsPanel::ItemsPanel(const DocumentModel* document, OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myDocument(document)
    , myView(view)
{
    setMinimumWidth(220);
    setStyleSheet(QStringLiteral("background-color: %1;").arg(Theme::panel().name()));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    auto* title = new QLabel(tr("Items"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                             .arg(Theme::textMuted().name()));
    outer->addWidget(title);

    myRows = new QVBoxLayout();
    myRows->setContentsMargins(0, 0, 0, 0);
    myRows->setSpacing(2);
    outer->addLayout(myRows);
    outer->addStretch(1);

    refresh();
}

void ItemsPanel::refresh()
{
    // Rebuild wholesale: the list is short, and diffing it would be more code
    // than it saves.
    while (QLayoutItem* item = myRows->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    myRowWidgets.clear();
    myRowIds.clear();
    if (!myDocument) return;

    for (const DocumentModel::Solid& solid : myDocument->solids()) {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(8);

        auto* name = new QLabel(QString::fromStdString(solid.name), row);
        name->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
        layout->addWidget(name, 1);

        auto* size = new QLabel(
            QString::fromStdString(Measure::formatDimensions(solid.shape)), row);
        size->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                .arg(Theme::textMuted().name()));
        layout->addWidget(size);

        auto* eye = new QPushButton(row);
        eye->setCheckable(true);
        eye->setChecked(myView && myView->isSolidVisible(solid.id));
        eye->setFixedSize(24, 24);
        eye->setIcon(IconSet::icon(IconSet::Glyph::SelectSolid));
        eye->setToolTip(tr("Show or hide this solid"));
        const int id = solid.id;
        connect(eye, &QPushButton::toggled, this, [this, id](bool visible) {
            if (myView) myView->setSolidVisible(id, visible);
        });
        layout->addWidget(eye);

        // Clicking anywhere on the row selects that solid in the viewport.
        row->installEventFilter(this);
        row->setProperty("solidId", id);

        myRows->addWidget(row);
        myRowWidgets.push_back(row);
        myRowIds.push_back(id);
    }

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
    return QWidget::eventFilter(watched, event);
}

void ItemsPanel::showSelection(const std::vector<int>& ids)
{
    for (std::size_t i = 0; i < myRowWidgets.size(); ++i) {
        const bool selected =
            std::find(ids.begin(), ids.end(), myRowIds[i]) != ids.end();
        myRowWidgets[i]->setStyleSheet(
            selected ? QStringLiteral("background-color: %1; border-radius: 4px;")
                           .arg(Theme::chipActive().name())
                     : QString());
    }
}
