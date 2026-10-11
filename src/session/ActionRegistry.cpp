#include "session/ActionRegistry.h"

#include <QAction>
#include <QJsonObject>
#include <QKeySequence>
#include <QRegularExpression>

QString ActionRegistry::idFor(const QString& actionText)
{
    QString t = actionText;
    t.remove(QLatin1Char('&'));
    t = t.trimmed();
    if (t.endsWith(QChar(0x2026))) t.chop(1);
    while (t.endsWith(QLatin1Char('.'))) t.chop(1);
    t = t.toLower();
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    t.replace(nonWord, QStringLiteral("-"));
    while (t.startsWith(QLatin1Char('-'))) t.remove(0, 1);
    while (t.endsWith(QLatin1Char('-'))) t.chop(1);
    return t;
}

bool ActionRegistry::add(QAction* action)
{
    if (!action) return false;
    const QString id = idFor(action->text());
    if (id.isEmpty()) return false;
    if (myActions.contains(id)) {
        qWarning("ActionRegistry: \"%s\" gives the id \"%s\", which is taken",
                 qPrintable(action->text()), qPrintable(id));
        return false;
    }
    myActions.insert(id, action);
    myOrder.push_back(id);
    return true;
}

int ActionRegistry::adoptChildren(QObject* owner)
{
    int taken = 0;
    for (QAction* a : owner->findChildren<QAction*>())
        if (!a->text().isEmpty() && add(a)) ++taken;
    return taken;
}

QAction* ActionRegistry::action(const QString& id) const
{
    return myActions.value(id).data();
}

QJsonArray ActionRegistry::snapshot() const
{
    QJsonArray rows;
    for (const QString& id : myOrder) {
        QAction* a = myActions.value(id).data();
        if (!a) continue;
        QJsonObject row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("text"), QString(a->text()).remove(QLatin1Char('&')));
        row.insert(QStringLiteral("shortcut"), a->shortcut().toString(QKeySequence::PortableText));
        row.insert(QStringLiteral("tooltip"), a->toolTip());
        row.insert(QStringLiteral("enabled"), a->isEnabled());
        row.insert(QStringLiteral("checkable"), a->isCheckable());
        row.insert(QStringLiteral("checked"), a->isChecked());
        rows.push_back(row);
    }
    return rows;
}

bool ActionRegistry::trigger(const QString& id)
{
    QAction* a = action(id);
    if (!a || !a->isEnabled()) return false;
    a->trigger();
    return true;
}
