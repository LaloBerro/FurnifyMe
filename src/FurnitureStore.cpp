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
#include <QSaveFile>
#include <QUuid>

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

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

// Task 1 reserved the "symmetry" manifest key; this task is what actually
// writes and reads it: {"on": bool, "plane": {"origin": [x,y,z],
// "normal": [x,y,z]}, "pairs": [[i,j], ...]}. Pairs are the SAME
// position-based indices DocumentModel::DocumentMeta::symmetryPairs already
// carries - see its own comment for why ids cannot be persisted directly.
//
// The plane is stored as origin + normal only, not a full placement the way
// FurnifySerial stores an outline's plane - gp_Trsf::SetMirror(gp_Ax2) and
// the straddle check both only ever read a gp_Pln's location and normal, so
// there is nothing else here worth a third pair of numbers to round-trip.
QJsonObject symmetryToJson(const DocumentModel::DocumentMeta& meta)
{
    QJsonObject obj;
    obj[QStringLiteral("on")] = meta.symmetryOn;

    const gp_Pnt origin = meta.symmetryPlane.Location();
    const gp_Dir normal = meta.symmetryPlane.Axis().Direction();
    QJsonObject planeObj;
    planeObj[QStringLiteral("origin")] =
        QJsonArray{origin.X(), origin.Y(), origin.Z()};
    planeObj[QStringLiteral("normal")] =
        QJsonArray{normal.X(), normal.Y(), normal.Z()};
    obj[QStringLiteral("plane")] = planeObj;

    QJsonArray pairsArr;
    for (const std::pair<int, int>& pair : meta.symmetryPairs) {
        pairsArr.append(QJsonArray{pair.first, pair.second});
    }
    obj[QStringLiteral("pairs")] = pairsArr;
    return obj;
}

// The inverse. Absent entirely - every furniture created before this task,
// and every version saved before it - decodes to symmetry OFF with
// `meta` otherwise untouched (its DEFAULT plane), per the brief's own
// future-proofing ruling: "loader must default symmetry-off when absent".
// Never refuses the load outright; a corrupt or missing plane simply leaves
// the default in place, and a corrupt pair entry is skipped - the rest of
// the document is still good.
void jsonToSymmetry(const QJsonObject& obj, DocumentModel::DocumentMeta& meta)
{
    if (!obj.contains(QStringLiteral("on"))) {
        meta.symmetryOn = false;
        return;
    }
    meta.symmetryOn = obj.value(QStringLiteral("on")).toBool(false);

    const QJsonObject planeObj = obj.value(QStringLiteral("plane")).toObject();
    const QJsonArray originArr = planeObj.value(QStringLiteral("origin")).toArray();
    const QJsonArray normalArr = planeObj.value(QStringLiteral("normal")).toArray();
    if (originArr.size() == 3 && normalArr.size() == 3) {
        const double nx = normalArr.at(0).toDouble(1.0);
        const double ny = normalArr.at(1).toDouble(0.0);
        const double nz = normalArr.at(2).toDouble(0.0);
        // A zero-magnitude normal is corrupt data, not a valid plane -
        // gp_Dir's constructor throws on one, so this guards it rather than
        // crashing on a hand-edited manifest.
        if (nx * nx + ny * ny + nz * nz > 1.0e-12) {
            const gp_Pnt origin(originArr.at(0).toDouble(0.0), originArr.at(1).toDouble(0.0),
                                originArr.at(2).toDouble(0.0));
            meta.symmetryPlane = gp_Pln(origin, gp_Dir(nx, ny, nz));
        }
    }

    const QJsonArray pairsArr = obj.value(QStringLiteral("pairs")).toArray();
    for (const QJsonValue& v : pairsArr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() != 2) continue;
        meta.symmetryPairs.push_back({pair.at(0).toInt(-1), pair.at(1).toInt(-1)});
    }
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

bool FurnitureStore::writeShapesFileAtomic(const QString& targetPath,
                                           const FurnifySerial::SerializedDocument& serial) const
{
    // Write to a TEMP file beside the target, verify the stream actually
    // flushed cleanly, and only then replace the live file - never truncate
    // it in place. A flush-time failure (a full disk, a dropped network
    // drive) must leave the OLD shapes.bin standing, not a half-written one
    // reported as "Saved" - see CLAUDE.md's never-lie-about-saving law.
    const QString tmpPath = targetPath + QStringLiteral(".tmp");
    {
        std::ofstream out(tmpPath.toStdString(), std::ios::binary | std::ios::trunc);
        if (!out || !FurnifySerial::writeShapes(serial, out).ok) {
            QFile::remove(tmpPath);
            return false;
        }
        out.close();
        // The write calls above can all report success while the FINAL
        // flush at close() still fails - exactly the failure a plain
        // std::ios::trunc write onto the live file would have hidden behind
        // a "Saved" toast. Caught here, before the temp file ever touches
        // the target.
        if (!out) {
            QFile::remove(tmpPath);
            return false;
        }
    }

    QFile::remove(targetPath);  // drop the stale target, if any, before the rename -
                                 // QFile::rename() refuses to replace an existing file
    if (!QFile::rename(tmpPath, targetPath)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool FurnitureStore::writeManifestObject(const QString& id, const QJsonObject& manifest) const
{
    if (!QDir().mkpath(furnitureDir(id))) return false;

    // Atomic: QSaveFile writes to a temp file beside manifest.json and only
    // replaces it on commit() - never QIODevice::Truncate straight onto the
    // live file. A flush that fails partway (a full disk, a dropped network
    // drive) must leave the OLD manifest standing, not a half-written one
    // sitting under a "Saved" toast - see CLAUDE.md's never-lie-about-saving
    // law. QSaveFile is used rather than a hand-rolled temp+rename (the
    // shapes blob's own approach, below) because it already gets the
    // Windows replace-an-existing-file quirks right; a manifest is small
    // JSON, not a stream FurnifySerial has to write incrementally, so
    // nothing here needs the lower-level control an ofstream gives the blob.
    QSaveFile file(manifestPath(id));
    if (!file.open(QIODevice::WriteOnly)) return false;

    const QJsonDocument doc(manifest);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) return false;  // commit() never called - temp discarded
    return file.commit();
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
    // Task 1 reserved this key; Task 4 is what actually writes it. A fresh
    // furniture starts symmetric-off, at DocumentModel's own default plane -
    // exactly what DocumentModel::DocumentMeta{} already default-constructs.
    manifest[QStringLiteral("symmetry")] = symmetryToJson(DocumentModel::DocumentMeta{});

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

    // Atomic: never truncate the live shapes.bin in place - see
    // writeShapesFileAtomic()'s own comment for why a flush failure must
    // leave the OLD document loadable rather than a half-written one
    // standing under a "Saved" toast.
    if (!writeShapesFileAtomic(shapesPath(id), serial)) return false;

    QJsonObject manifest = readManifestObject(id);
    if (manifest.isEmpty()) return false;  // manifest existed but is unreadable - do not paper over it
    manifest[QStringLiteral("lastEdited")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("bodies")] = itemMetaToJson(meta.bodyNames, meta.bodyVisible);
    manifest[QStringLiteral("outlines")] = itemMetaToJson(meta.outlineNames, meta.outlineVisible);
    manifest[QStringLiteral("symmetry")] = symmetryToJson(meta);
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
    // Absent entirely for any furniture created before this task - decodes
    // to symmetry OFF, never a refusal. See jsonToSymmetry()'s own comment.
    jsonToSymmetry(manifest.value(QStringLiteral("symmetry")).toObject(), meta);

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
    const QString blobPath = versionsDir(id) + QStringLiteral("/") + file;
    if (!writeShapesFileAtomic(blobPath, serial)) return false;

    QJsonObject entry;
    entry[QStringLiteral("name")] = name;
    entry[QStringLiteral("saved")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry[QStringLiteral("file")] = file;
    entry[QStringLiteral("bodies")] = itemMetaToJson(meta.bodyNames, meta.bodyVisible);
    entry[QStringLiteral("outlines")] = itemMetaToJson(meta.outlineNames, meta.outlineVisible);
    // A version snapshots the WHOLE document, per the plan's own ruling
    // (pairings must survive a restore or a stale live document could break
    // symmetry after an undo-of-restore) - so its symmetry state travels
    // with it exactly as the current furniture's does.
    entry[QStringLiteral("symmetry")] = symmetryToJson(meta);
    versionsArr.append(entry);
    manifest[QStringLiteral("versions")] = versionsArr;
    if (!writeManifestObject(id, manifest)) {
        // The blob is already on disk (writeShapesFileAtomic() above
        // succeeded) but the manifest never learned its filename - an
        // orphan nothing will ever load or list. Delete it rather than
        // leaving versions/ quietly accumulate a dangling file every time
        // this fails.
        QFile::remove(blobPath);
        return false;
    }
    return true;
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
        // Absent for a version saved before this task existed - decodes to
        // symmetry OFF, same rule as loadFurniture()'s own current-document
        // read above.
        jsonToSymmetry(entry.value(QStringLiteral("symmetry")).toObject(), meta);

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
