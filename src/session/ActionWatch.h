#pragma once

// A few actions' state, for a page that shows them (migration Phase 3b): the
// Settings card mirrors a dozen actions out of the registry's hundred-odd, and
// updateActions() touches every action on every state change. So a watch
// listens to the ids it was given, and says changed() ONCE per event-loop turn
// however many of them moved in it - the page re-reads snapshot() then.

#include <QJsonArray>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

class ActionRegistry;

class ActionWatch : public QObject
{
    Q_OBJECT
public:
    explicit ActionWatch(ActionRegistry* registry, QObject* parent = nullptr);
    // Watches exactly these ids from now on; unknown ids are ignored.
    void setIds(const QStringList& ids);
    QStringList ids() const { return myIds; }
    // The watched actions as they are now, in setIds() order.
    QJsonArray snapshot() const;

signals:
    void changed();

private:
    ActionRegistry* myRegistry = nullptr;
    QStringList myIds;
    QList<QMetaObject::Connection> myConnections;
    // Zero-interval and single-shot: every change in one turn restarts the
    // same pending timeout, so the turn ends in one changed().
    QTimer myPending;
};
