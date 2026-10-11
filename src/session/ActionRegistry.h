#pragma once

// Every command in the app by a stable id, for anything that is not a Qt
// widget - the HTML layer first. The QActions stay owned where they were made
// (MainWindow); this only indexes them. updateActions() remains the one place
// availability is decided: trigger() obeys isEnabled(), which
// QAction::trigger() itself does not.

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QAction;

class ActionRegistry : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // The id an action's text gives it: the mnemonic '&' and a trailing
    // ellipsis dropped, lower case, every run of anything else a '-'.
    static QString idFor(const QString& actionText);
    // False, logged, when the text gives no id or the id is taken - two
    // actions under one name is refused rather than silently keeping either.
    bool add(QAction* action);
    // add() for every QAction child of `owner` that has text; how many took.
    int adoptChildren(QObject* owner);
    QAction* action(const QString& id) const;
    // Per action, in registration order: id, text (no mnemonic), shortcut
    // (portable text), tooltip, enabled, checkable, checked.
    QJsonArray snapshot() const;
    // False, and nothing done, for an unknown id or a disabled action.
    bool trigger(const QString& id);

private:
    QHash<QString, QPointer<QAction>> myActions;
    QStringList myOrder;
};
