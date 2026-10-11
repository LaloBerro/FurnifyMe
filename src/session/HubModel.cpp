#include "session/HubModel.h"

#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace {

HubEntry fromInfo(const QString& id, const QString& name, const QString& thumbPath,
                  const QDateTime& lastEdited, bool scene)
{
    HubEntry e;
    e.id = id;
    e.name = name;
    e.thumbPath = thumbPath;
    e.lastEdited = lastEdited;
    e.scene = scene;
    const QFileInfo thumb(thumbPath);
    if (!thumbPath.isEmpty() && thumb.exists())
        e.thumbStamp = thumb.lastModified().toMSecsSinceEpoch();
    return e;
}

QString initialOf(const QString& name)
{
    for (const QChar c : name)
        if (c.isLetterOrNumber()) return QString(c.toUpper());
    return QString();
}

}  // namespace

HubModel::HubModel(FurnitureStore& store, QObject* parent)
    : QObject(parent), myStore(store)
{
    refresh();
}

QString HubModel::editedText(const QDateTime& when, const QDateTime& now)
{
    if (!when.isValid()) return tr("Never saved");
    const QLocale locale;
    const QDate day = when.date();
    const QDate today = now.date();
    const qint64 age = day.daysTo(today);
    const QString clock = locale.toString(when.time(), QLocale::ShortFormat);
    if (age == 0) return tr("Today, %1").arg(clock);
    if (age == 1) return tr("Yesterday, %1").arg(clock);
    if (age > 1 && age < 7) return tr("%1, %2").arg(locale.dayName(day.dayOfWeek()), clock);
    if (day.year() == today.year()) return locale.toString(day, QStringLiteral("d MMM"));
    return locale.toString(day, QStringLiteral("d MMM yyyy"));
}

QString HubModel::deletedNoteText(const QString& name, int scenes)
{
    if (scenes <= 0) return tr("Deleted %1").arg(name);
    if (scenes == 1) return tr("Deleted %1 — 1 scene now shows it as missing").arg(name);
    return tr("Deleted %1 — %2 scenes now show it as missing").arg(name, QString::number(scenes));
}

void HubModel::refresh()
{
    myFurniture.clear();
    for (const FurnitureStore::FurnitureInfo& i : myStore.listFurniture())
        myFurniture.push_back(fromInfo(i.id, i.name, i.thumbPath, i.lastEdited, false));
    myScenes.clear();
    for (const FurnitureStore::SceneInfo& i : myStore.listScenes())
        myScenes.push_back(fromInfo(i.id, i.name, i.thumbPath, i.lastEdited, true));
    emit changed();
}

QVariantList HubModel::listOf(const std::vector<HubEntry>& entries) const
{
    const QDateTime now = QDateTime::currentDateTime();
    QVariantList list;
    for (const HubEntry& e : entries) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("name"), e.name);
        m.insert(QStringLiteral("scene"), e.scene);
        QString url;
        if (e.thumbStamp > 0) {
            QUrl u = QUrl::fromLocalFile(e.thumbPath);
            u.setQuery(QStringLiteral("v=%1").arg(e.thumbStamp));
            url = u.toString();
        }
        m.insert(QStringLiteral("thumbUrl"), url);
        m.insert(QStringLiteral("edited"),
                 e.lastEdited.isValid() ? e.lastEdited.toMSecsSinceEpoch() : qint64(0));
        m.insert(QStringLiteral("editedText"), editedText(e.lastEdited.toLocalTime(), now));
        m.insert(QStringLiteral("initial"), initialOf(e.name));
        list.push_back(m);
    }
    return list;
}

QVariantList HubModel::furnitureList() const { return listOf(myFurniture); }
QVariantList HubModel::sceneList() const { return listOf(myScenes); }

QString HubModel::suggestedName() const { return myStore.nextFurnitureName(); }

QStringList HubModel::usedIn(const QString& furnitureId) const
{
    QStringList names;
    for (const FurnitureStore::SceneInfo& scene : myStore.scenesUsingFurniture(furnitureId))
        names << scene.name;
    return names;
}

void HubModel::createFurniture(const QString& name)
{
    emit createRequested();
    const QString id = myStore.createFurniture(name);
    if (id.isEmpty()) {
        emit failure(tr("Couldn't create %1 — Check that the library folder "
                        "still exists and isn't read-only")
                         .arg(name));
        return;
    }
    refresh();
    emit furnitureChosen(id);
}

void HubModel::createScene()
{
    emit createSceneRequested();
    const QString id = myStore.createScene(myStore.nextSceneName());
    if (id.isEmpty()) {
        emit failure(tr("Couldn't make a new scene — Check that the library folder "
                        "still exists and isn't read-only"));
        return;
    }
    refresh();
    emit sceneChosen(id);
}

void HubModel::rename(const QString& id, bool scene, const QString& name)
{
    // InlineRename's own rule: an empty or unchanged name is no change at all.
    const QString chosen = name.trimmed();
    const std::vector<HubEntry>& list = scene ? myScenes : myFurniture;
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const HubEntry& e) { return e.id == id; });
    if (chosen.isEmpty() || (it != list.end() && it->name == chosen)) return;

    const bool ok = scene ? myStore.renameScene(id, chosen) : myStore.renameFurniture(id, chosen);
    if (!ok) {
        emit failure(scene ? tr("Couldn't rename this scene to %1 — Check that its folder still "
                                "exists and isn't read-only")
                                 .arg(chosen)
                           : tr("Couldn't rename this furniture to %1 — Check that its folder "
                                "still exists and isn't read-only")
                                 .arg(chosen));
        return;
    }
    refresh();
}

void HubModel::remove(const QString& id, bool scene)
{
    if (scene) {
        if (!myStore.deleteScene(id)) {
            emit failure(tr("Couldn't delete this scene — Check that its folder still "
                            "exists and isn't read-only"));
            return;
        }
        refresh();
        return;
    }
    QString name;
    for (const HubEntry& e : myFurniture)
        if (e.id == id) name = e.name;
    // Counted BEFORE the delete - afterwards the question has no subject.
    const int scenes = static_cast<int>(myStore.scenesUsingFurniture(id).size());
    if (!myStore.deleteFurniture(id)) {
        emit failure(tr("Couldn't delete this furniture — Check that its "
                        "folder still exists and isn't read-only"));
        return;
    }
    refresh();
    // Reported, and with no Undo: deleting removes files, final once taken;
    // the two-click arm was the "are you sure".
    emit note(deletedNoteText(name, scenes));
}

void HubModel::choose(const QString& id, bool scene)
{
    if (scene) emit sceneChosen(id);
    else emit furnitureChosen(id);
}
