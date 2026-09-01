#include "FurnitureStore.h"

#include "DocumentModel.h"
#include "FurnifySerial.h"

#include <algorithm>
#include <fstream>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QUuid>

namespace {

// A body/outline's names + visibility as one JSON object:
// {"names": [...], "visible": [...]} - the manifest's own copy of
// DocumentModel::DocumentMeta's two parallel vectors for one item kind.
QJsonObject itemMetaToJson(const std::vector<std::string>& names, const std::vector<bool>& visible)
{
    QJsonArray namesArr;
    QJsonArray visibleArr;
    for (const std::string& n : names) namesArr.append(QString::fromStdString(n));
    for (bool v : visible) visibleArr.append(v);

    QJsonObject obj;
    obj[QStringLiteral("names")] = namesArr;
    obj[QStringLiteral("visible")] = visibleArr;
    return obj;
}

// The inverse. False (leaving names/visible untouched) when the object is
// missing either array, the two arrays differ in length, or any entry is
// not the type it should be - all of which mean a hand-edited or corrupt
// manifest, and the caller maps that straight to a load refusal.
bool jsonToItemMeta(const QJsonObject& obj, std::vector<std::string>& names, std::vector<bool>& visible)
{
    if (!obj.contains(QStringLiteral("names")) || !obj.contains(QStringLiteral("visible"))) {
        return false;
    }
    const QJsonArray namesArr = obj.value(QStringLiteral("names")).toArray();
    const QJsonArray visibleArr = obj.value(QStringLiteral("visible")).toArray();
    if (namesArr.size() != visibleArr.size()) return false;

    std::vector<std::string> parsedNames;
    std::vector<bool> parsedVisible;
    parsedNames.reserve(static_cast<std::size_t>(namesArr.size()));
    parsedVisible.reserve(static_cast<std::size_t>(visibleArr.size()));
    for (int i = 0; i < namesArr.size(); ++i) {
        if (!namesArr.at(i).isString() || !visibleArr.at(i).isBool()) return false;
        parsedNames.push_back(namesArr.at(i).toString().toStdString());
        parsedVisible.push_back(visibleArr.at(i).toBool());
    }

    names = std::move(parsedNames);
    visible = std::move(parsedVisible);
    return true;
}

}  // namespace

FurnitureStore::FurnitureStore(const QString& rootDir) : myRootDir(rootDir) {}

QString FurnitureStore::furnitureDir(const QString& id) const
{
    return myRootDir + QStringLiteral("/") + id;
}

QString FurnitureStore::manifestPath(const QString& id) const
{
    return furnitureDir(id) + QStringLiteral("/manifest.json");
}

QString FurnitureStore::shapesPath(const QString& id) const
{
    return furnitureDir(id) + QStringLiteral("/shapes.bin");
}

QString FurnitureStore::thumbPath(const QString& id) const
{
    return furnitureDir(id) + QStringLiteral("/thumb.png");
}

QString FurnitureStore::versionsDir(const QString& id) const
{
    return furnitureDir(id) + QStringLiteral("/versions");
}

QJsonObject FurnitureStore::readManifestObject(const QString& id) const
{
    QFile file(manifestPath(id));
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();

    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll());
    if (!parsed.isObject()) return QJsonObject();
    return parsed.object();
}

bool FurnitureStore::writeManifestObject(const QString& id, const QJsonObject& manifest) const
{
    if (!QDir().mkpath(furnitureDir(id))) return false;

    QFile file(manifestPath(id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    const QJsonDocument doc(manifest);
    return file.write(doc.toJson(QJsonDocument::Indented)) >= 0;
}

QVector<FurnitureStore::FurnitureInfo> FurnitureStore::listFurniture() const
{
    QVector<FurnitureInfo> result;

    QDir root(myRootDir);
    if (!root.exists()) return result;

    const QStringList ids = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& id : ids) {
        if (!QFileInfo::exists(manifestPath(id))) continue;

        const QJsonObject manifest = readManifestObject(id);
        if (manifest.isEmpty()) continue;  // unparseable - skip, don't hide the rest

        FurnitureInfo info;
        info.id = id;
        info.name = manifest.value(QStringLiteral("name")).toString();
        info.filePath = furnitureDir(id);
        info.thumbPath = thumbPath(id);
        info.lastEdited = QDateTime::fromString(manifest.value(QStringLiteral("lastEdited")).toString(),
                                                Qt::ISODateWithMs);
        result.push_back(info);
    }

    std::sort(result.begin(), result.end(), [](const FurnitureInfo& a, const FurnitureInfo& b) {
        return a.lastEdited > b.lastEdited;
    });
    return result;
}

QString FurnitureStore::nextFurnitureName() const
{
    int highest = 0;
    static const QString prefix = QStringLiteral("Furniture ");
    for (const FurnitureInfo& info : listFurniture()) {
        if (!info.name.startsWith(prefix)) continue;
        bool ok = false;
        const int n = info.name.mid(prefix.size()).toInt(&ok);
        if (ok) highest = std::max(highest, n);
    }
    return QStringLiteral("Furniture %1").arg(highest + 1, 2, 10, QLatin1Char('0'));
}

QString FurnitureStore::createFurniture(const QString& name)
{
    if (!QDir().mkpath(myRootDir)) return QString();

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(furnitureDir(id))) return QString();
    if (!QDir().mkpath(versionsDir(id))) return QString();

    QJsonObject manifest;
    manifest[QStringLiteral("format")] = kManifestFormatVersion;
    manifest[QStringLiteral("name")] = name;
    manifest[QStringLiteral("lastEdited")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("bodies")] = itemMetaToJson({}, {});
    manifest[QStringLiteral("outlines")] = itemMetaToJson({}, {});
    manifest[QStringLiteral("versions")] = QJsonArray();
    // "symmetry" is deliberately absent - reserved for a later task to
    // write; every reader here tolerates its absence.

    if (!writeManifestObject(id, manifest)) return QString();

    const FurnifySerial::SerializedDocument empty;
    std::ofstream shapesOut(shapesPath(id).toStdString(), std::ios::binary | std::ios::trunc);
    if (!shapesOut || !FurnifySerial::writeShapes(empty, shapesOut).ok) return QString();

    return id;
}

bool FurnitureStore::saveFurniture(const QString& id, const DocumentModel& doc, const QImage& thumbnail)
{
    if (!QFileInfo::exists(manifestPath(id))) return false;

    DocumentModel::DocumentMeta meta;
    const FurnifySerial::SerializedDocument serial = doc.toSerialized(meta);

    std::ofstream shapesOut(shapesPath(id).toStdString(), std::ios::binary | std::ios::trunc);
    if (!shapesOut || !FurnifySerial::writeShapes(serial, shapesOut).ok) return false;
    shapesOut.close();

    QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty()) return false;  // manifest existed but is unreadable - do not paper over it
    manifest[QStringLiteral("lastEdited")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("bodies")] = itemMetaToJson(meta.bodyNames, meta.bodyVisible);
    manifest[QStringLiteral("outlines")] = itemMetaToJson(meta.outlineNames, meta.outlineVisible);
    if (!writeManifestObject(id, manifest)) return false;

    // A null/empty thumbnail is not a failure - the caller may not have
    // captured one yet (createFurniture writes none at all).
    if (!thumbnail.isNull()) thumbnail.save(thumbPath(id), "PNG");

    return true;
}

bool FurnitureStore::loadFurniture(const QString& id, DocumentModel& doc, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };

    if (!QFileInfo::exists(manifestPath(id))) return fail(QStringLiteral("furniture not found"));

    const QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty() || !manifest.contains(QStringLiteral("format"))) {
        return fail(QStringLiteral("the manifest is unreadable or corrupt"));
    }
    const int format = manifest.value(QStringLiteral("format")).toInt(-1);
    if (format != kManifestFormatVersion) {
        return fail(QStringLiteral("this furniture was saved by a version of FurnifyMe this "
                                    "build does not understand (format %1)")
                         .arg(format));
    }

    std::ifstream shapesIn(shapesPath(id).toStdString(), std::ios::binary);
    if (!shapesIn) return fail(QStringLiteral("the shapes file is missing"));
    FurnifySerial::SerializedDocument serial;
    const FurnifySerial::SerialResult read = FurnifySerial::readShapes(shapesIn, serial);
    if (!read.ok) return fail(QString::fromStdString(read.error));

    DocumentModel::DocumentMeta meta;
    if (!jsonToItemMeta(manifest.value(QStringLiteral("bodies")).toObject(), meta.bodyNames,
                        meta.bodyVisible)) {
        return fail(QStringLiteral("the manifest's body names/visibility are corrupt"));
    }
    if (!jsonToItemMeta(manifest.value(QStringLiteral("outlines")).toObject(), meta.outlineNames,
                        meta.outlineVisible)) {
        return fail(QStringLiteral("the manifest's outline names/visibility are corrupt"));
    }

    // Scratch, then swap - never half-load, per the standing contract.
    DocumentModel scratch;
    if (!scratch.fromSerialized(serial, meta)) {
        return fail(QStringLiteral("the shapes do not match the manifest's item count - file is corrupt"));
    }

    doc = scratch;
    return true;
}

bool FurnitureStore::renameFurniture(const QString& id, const QString& name)
{
    if (!QFileInfo::exists(manifestPath(id))) return false;

    QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty()) return false;
    manifest[QStringLiteral("name")] = name;
    return writeManifestObject(id, manifest);
}

QVector<FurnitureStore::VersionInfo> FurnitureStore::versions(const QString& id) const
{
    QVector<VersionInfo> result;
    const QJsonObject manifest = readManifestObject(id);
    const QJsonArray versionsArr = manifest.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue& v : versionsArr) {
        const QJsonObject entry = v.toObject();
        VersionInfo info;
        info.name = entry.value(QStringLiteral("name")).toString();
        info.saved = QDateTime::fromString(entry.value(QStringLiteral("saved")).toString(), Qt::ISODateWithMs);
        result.push_back(info);
    }
    return result;
}

bool FurnitureStore::saveVersion(const QString& id, const QString& name, const DocumentModel& doc)
{
    if (!QFileInfo::exists(manifestPath(id))) return false;

    QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty()) return false;
    QJsonArray versionsArr = manifest.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue& v : versionsArr) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return false;  // duplicate name - the caller's job to say why
        }
    }

    DocumentModel::DocumentMeta meta;
    const FurnifySerial::SerializedDocument serial = doc.toSerialized(meta);

    if (!QDir().mkpath(versionsDir(id))) return false;
    // A fresh, never-reused filename - the version's own NAME is user text
    // and not filesystem-safe, and reusing a small counter after a delete
    // is exactly the "a name reappearing on different content" trap
    // DocumentModel's own id/name counters were written to avoid.
    const QString file = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".bin");
    std::ofstream out((versionsDir(id) + QStringLiteral("/") + file).toStdString(),
                      std::ios::binary | std::ios::trunc);
    if (!out || !FurnifySerial::writeShapes(serial, out).ok) return false;
    out.close();

    QJsonObject entry;
    entry[QStringLiteral("name")] = name;
    entry[QStringLiteral("saved")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry[QStringLiteral("file")] = file;
    entry[QStringLiteral("bodies")] = itemMetaToJson(meta.bodyNames, meta.bodyVisible);
    entry[QStringLiteral("outlines")] = itemMetaToJson(meta.outlineNames, meta.outlineVisible);
    versionsArr.append(entry);
    manifest[QStringLiteral("versions")] = versionsArr;
    return writeManifestObject(id, manifest);
}

bool FurnitureStore::loadVersion(const QString& id, const QString& name, DocumentModel& doc)
{
    if (!QFileInfo::exists(manifestPath(id))) return false;

    const QJsonObject manifest = readManifestObject(id);
    const QJsonArray versionsArr = manifest.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue& v : versionsArr) {
        const QJsonObject entry = v.toObject();
        if (entry.value(QStringLiteral("name")).toString() != name) continue;

        const QString file = entry.value(QStringLiteral("file")).toString();
        std::ifstream in((versionsDir(id) + QStringLiteral("/") + file).toStdString(), std::ios::binary);
        if (!in) return false;
        FurnifySerial::SerializedDocument serial;
        if (!FurnifySerial::readShapes(in, serial).ok) return false;

        DocumentModel::DocumentMeta meta;
        if (!jsonToItemMeta(entry.value(QStringLiteral("bodies")).toObject(), meta.bodyNames,
                            meta.bodyVisible)) {
            return false;
        }
        if (!jsonToItemMeta(entry.value(QStringLiteral("outlines")).toObject(), meta.outlineNames,
                            meta.outlineVisible)) {
            return false;
        }

        DocumentModel scratch;
        if (!scratch.fromSerialized(serial, meta)) return false;

        doc = scratch;
        return true;
    }
    return false;
}

bool FurnitureStore::deleteVersion(const QString& id, const QString& name)
{
    if (!QFileInfo::exists(manifestPath(id))) return false;

    QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty()) return false;
    QJsonArray versionsArr = manifest.value(QStringLiteral("versions")).toArray();
    for (int i = 0; i < versionsArr.size(); ++i) {
        const QJsonObject entry = versionsArr.at(i).toObject();
        if (entry.value(QStringLiteral("name")).toString() != name) continue;

        const QString file = entry.value(QStringLiteral("file")).toString();
        QFile::remove(versionsDir(id) + QStringLiteral("/") + file);
        versionsArr.removeAt(i);
        manifest[QStringLiteral("versions")] = versionsArr;
        return writeManifestObject(id, manifest);
    }
    return false;
}
