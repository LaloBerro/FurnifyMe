#pragma once
// gui_smoke's library hub since migration Phase 6 deleted the Qt one: the HTML
// hub's own logic (HubModel - lists, create, choose) in a plain top-level
// widget with HubWindow's three signals, so EditorSelectorHandoff::wire() -
// the function main.cpp calls with the real HubWindow - is wired here too and
// the suite drives the handoff that ships. What the hub page draws is
// hub_smoke's; this window paints nothing.
#include "FurnitureStore.h"
#include "session/HubModel.h"

#include <QCloseEvent>
#include <QWidget>

class SuiteHub : public QWidget
{
    Q_OBJECT

public:
    explicit SuiteHub(FurnitureStore& store) : myModel(store, this)
    {
        connect(&myModel, &HubModel::furnitureChosen, this, &SuiteHub::furnitureChosen);
        connect(&myModel, &HubModel::sceneChosen, this, &SuiteHub::sceneChosen);
        resize(480, 320);
    }

    HubModel& model() { return myModel; }
    void refresh() { myModel.refresh(); }

signals:
    void furnitureChosen(const QString& id);
    void sceneChosen(const QString& id);
    void closing();

protected:
    void closeEvent(QCloseEvent* event) override
    {
        emit closing();
        QWidget::closeEvent(event);
    }

private:
    HubModel myModel;
};
