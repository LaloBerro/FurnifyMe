#include "ActionWatch.h"

#include "ActionRegistry.h"

#include <QAction>

ActionWatch::ActionWatch(ActionRegistry* registry, QObject* parent)
    : QObject(parent), myRegistry(registry)
{
    myPending.setSingleShot(true);
    myPending.setInterval(0);
    connect(&myPending, &QTimer::timeout, this, &ActionWatch::changed);
}

void ActionWatch::setIds(const QStringList& ids)
{
    for (const QMetaObject::Connection& c : myConnections) disconnect(c);
    myConnections.clear();
    myIds = ids;
    if (!myRegistry) return;
    for (const QString& id : myIds) {
        if (QAction* action = myRegistry->action(id))
            myConnections.push_back(connect(action, &QAction::changed, &myPending,
                                            qOverload<>(&QTimer::start)));
    }
}

QJsonArray ActionWatch::snapshot() const
{
    return myRegistry ? myRegistry->snapshotOf(myIds) : QJsonArray();
}
