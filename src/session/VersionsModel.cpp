#include "VersionsModel.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {
QString panelTr(const char* text) { return QCoreApplication::translate("VersionsPanel", text); }
}  // namespace

VersionsModel::VersionsModel(FurnitureStore& store, Hooks hooks, QObject* parent)
    : QObject(parent), myStore(store), myHooks(std::move(hooks))
{
}

QVariantMap VersionsModel::words() const
{
    return {{QStringLiteral("title"), panelTr("Versions")},
            {QStringLiteral("add"), panelTr("Save a new version")},
            {QStringLiteral("empty"), panelTr("No versions yet.\n\nPress + to keep a named snapshot you "
                                              "can come back to.")},
            {QStringLiteral("compare"), panelTr("Compare")},
            {QStringLiteral("restore"), panelTr("Restore")},
            {QStringLiteral("delete"), panelTr("Delete")},
            {QStringLiteral("deleteArmed"), panelTr("Delete — click again")},
            {QStringLiteral("deleteMs"), kDeleteConfirmMs},
            {QStringLiteral("close"), QCoreApplication::translate("PanelCloseButton", "Close this panel")}};
}

QVariantList VersionsModel::readList() const
{
    QVariantList list;
    if (myFurnitureId.isEmpty()) return list;
    for (const FurnitureStore::VersionInfo& v : myStore.versions(myFurnitureId)) {
        QString url;
        const QFileInfo info(v.thumbPath);
        if (!v.thumbPath.isEmpty() && info.exists()) {
            QUrl u = QUrl::fromLocalFile(info.absoluteFilePath());
            u.setQuery(QStringLiteral("v=%1").arg(info.lastModified().toMSecsSinceEpoch()));
            url = u.toString();
        }
        list.push_back(QVariantMap{
            {QStringLiteral("name"), v.name},
            {QStringLiteral("savedText"), QLocale().toString(v.saved.toLocalTime(), QLocale::ShortFormat)},
            {QStringLiteral("saved"), v.saved.toMSecsSinceEpoch()},
            {QStringLiteral("thumbUrl"), url}});
    }
    return list;
}

void VersionsModel::refresh()
{
    const QVariantList next = readList();
    if (next == myVersions) return;
    myVersions = next;
    emit changed();
}

void VersionsModel::setState(const QString& furnitureId, bool canSave, bool rowsEnabled, bool shown)
{
    const bool gates = canSave != myCanSave || rowsEnabled != myRowsEnabled || furnitureId != myFurnitureId;
    myFurnitureId = furnitureId;
    myCanSave = canSave;
    myRowsEnabled = rowsEnabled;
    myShown = shown;
    // A hidden drawer reads nothing: this runs on every appStateChanged -
    // every selection click, every step of a colour drag (review I1). Showing
    // it reads the list again.
    if (!shown) {
        if (gates) emit changed();
        return;
    }
    const QVariantList next = readList();
    const bool list = next != myVersions;
    myVersions = next;
    if (gates || list) emit changed();
}

void VersionsModel::requestNew() { emit newRequested(); }

QString VersionsModel::defaultName() const
{
    // VersionsPanel::nextVersionDefaultName(): one past the highest
    // "Version <n>", whatever else the versions are called.
    int highest = 0;
    if (!myFurnitureId.isEmpty()) {
        static const QRegularExpression pattern(QStringLiteral("^Version (\\d+)$"));
        for (const FurnitureStore::VersionInfo& v : myStore.versions(myFurnitureId)) {
            const QRegularExpressionMatch m = pattern.match(v.name);
            if (m.hasMatch()) highest = std::max(highest, m.captured(1).toInt());
        }
    }
    return panelTr("Version %1").arg(highest + 1);
}

bool VersionsModel::save(const QString& name)
{
    const QString trimmed = name.trimmed();
    // !myShown: the drawer went away, and the page's late blur-commit must
    // write nothing (review C2) - the hide reaches this model before the
    // page's save call does.
    if (trimmed.isEmpty() || !myCanSave || !myShown || !myHooks.save) return false;
    return myHooks.save(trimmed);
}

bool VersionsModel::restore(const QString& name)
{
    return myRowsEnabled && myHooks.restore && myHooks.restore(name);
}

bool VersionsModel::compare(const QString& name)
{
    return myRowsEnabled && myHooks.compare && myHooks.compare(name);
}

bool VersionsModel::remove(const QString& name)
{
    return myRowsEnabled && myHooks.remove && myHooks.remove(name);
}

void VersionsModel::close()
{
    if (myHooks.close) myHooks.close();
}
