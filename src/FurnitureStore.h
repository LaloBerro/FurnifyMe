#pragma once
//
// Owns the managed library of `.furnify` furniture: enumerate, create, save,
// load, rename, and the versions that live inside each furniture's own
// files. App-layer (Qt: QString/QDateTime/QImage/QJsonDocument), unlike
// FurnifySerial - the shape blob itself stays Qt-free in furnify_geometry,
// and this file is what wraps it with the manifest, the thumbnail and the
// directory layout.
//
// "One `.furnify` file is one furniture" (the spec's words) is satisfied at
// this class's boundary, not literally: a furniture is a DIRECTORY under the
// root, named by its id, holding manifest.json + shapes.bin + thumb.png +
// versions/*.bin. Nothing outside this class touches paths inside that
// directory - the exact layout is FurnitureStore's private business, so a
// later task can change it (say, zip the directory into one real file on
// disk) without anything else in the app noticing.
//
// The root directory is INJECTED, never looked up here: MainWindow passes
// QStandardPaths::DocumentsLocation + "/FurnifyMe", and every test passes a
// temp directory (QTemporaryDir) - the same discipline UserProgress's
// storage injection and ScopedTestSettings already established, so the
// suite never touches a real user's files.
//
#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

class DocumentModel;
namespace FurnifySerial {
struct SerializedDocument;
}

class FurnitureStore {
public:
    struct FurnitureInfo {
        QString id;
        QString name;
        QString filePath;    // the furniture's own directory, <root>/<id>
        QString thumbPath;   // where thumb.png is/would be - callers check existence
        QDateTime lastEdited;
    };

    struct VersionInfo {
        QString name;
        QDateTime saved;
    };

    explicit FurnitureStore(const QString& rootDir);

    // Newest-edited first. Skips any subdirectory that is not a readable
    // furniture (no manifest.json, or one this build cannot parse) rather
    // than failing the whole listing - one damaged entry must not hide
    // every other furniture in the library.
    QVector<FurnitureInfo> listFurniture() const;

    // "Furniture NN" - the same two-digit-and-up numbering DocumentModel's
    // own Body/Outline names use - scanned from what the library actually
    // holds (listFurniture()) rather than counted, so a deleted-and-
    // recreated furniture never collides with a name still on screen.
    // Lives HERE rather than at a caller (InitScreen is the one caller
    // today) so a second creation path - a future import, a duplicate
    // gesture - cannot fork the numbering by carrying its own copy of this
    // scan. Does not itself create anything; a caller still calls
    // createFurniture(nextFurnitureName()).
    QString nextFurnitureName() const;

    // Creates a new, empty furniture and returns its id - a fresh
    // identifier, never derived from `name` (so renaming later never
    // touches the directory or breaks a held id). Returns an empty string
    // if the root directory or the furniture's own directory could not be
    // created. The new furniture round-trips through loadFurniture()
    // immediately - "writes empty file" means a valid, empty document is on
    // disk from the moment this returns, not a placeholder that needs a
    // save before it can be opened.
    QString createFurniture(const QString& name);

    // Writes doc's shapes + item names/visibility to shapes.bin, refreshes
    // the manifest (lastEdited, names/visible; name and the versions list
    // are preserved from whatever the manifest already held - this is not
    // renameFurniture), and writes `thumbnail` to thumb.png when it is not
    // null (a null thumbnail is not a failure - the caller may not have one
    // yet). False if `id` is not a known furniture or if the shapes fail to
    // serialize (see FurnifySerial::writeShapes's own refusal cases).
    bool saveFurniture(const QString& id, const DocumentModel& doc, const QImage& thumbnail);

    // Loads `id`'s current (not a version's) shapes and item state into a
    // SCRATCH document, and only assigns to `doc` on success - the standing
    // contract: a failed load never half-loads. False with *error set (when
    // error is non-null) for an unknown id, an unreadable manifest, a
    // future manifest format, or any refusal FurnifySerial::readShapes /
    // DocumentModel::fromSerialized reports.
    bool loadFurniture(const QString& id, DocumentModel& doc, QString* error);

    // Changes only the display name in the manifest - never the id, the
    // directory, or anything inside it. False for an unknown id.
    bool renameFurniture(const QString& id, const QString& name);

    // Removes a furniture's own directory entirely - manifest.json,
    // shapes.bin, thumb.png and every version it holds. False for an unknown
    // id, or when the directory could not be fully removed (a file locked or
    // otherwise undeletable) - `QDir::removeRecursively()`'s own contract,
    // which may remove some entries and not others on a partial failure; a
    // caller that gets false back should treat the furniture as possibly
    // still partly on disk, not as untouched. Final and carries no undo, the
    // same "file data, not document data" ruling deleteVersion() above
    // already follows (see CLAUDE.md's "Files, versions and the library").
    bool deleteFurniture(const QString& id);

    // --- versions: named snapshots stored inside the same furniture -------
    // Oldest-saved first (the order they were made), matching a version
    // list's natural reading order - contrast listFurniture()'s
    // newest-first, which is a gallery of recency rather than a history.
    QVector<VersionInfo> versions(const QString& id) const;

    // Snapshots doc's current shapes + names/visibility as a named version
    // inside `id`'s own files. False for an unknown furniture id, a
    // duplicate version name (versions are named by the user and looked up
    // by that name, so two cannot share one), or a serialization failure.
    //
    // `thumbSourcePath` is the path of an ALREADY-CAPTURED PNG on disk -
    // MainWindow's call site writes it via OcctViewWidget::saveSnapshot()
    // straight to a temp file, mirroring saveFurniture()'s own thumbnail
    // capture without the QImage round-trip a version's lighter-weight
    // snapshot does not need. Empty (the default) means no thumbnail, which
    // is never a failure - a thumbnail is presentation, never document data
    // (see "Files, versions and the library" in CLAUDE.md), so a version
    // with no snapshot, or one whose copy failed, still saves and still
    // loads; it simply has nothing for versionThumbPath() to return.
    bool saveVersion(const QString& id, const QString& name, const DocumentModel& doc,
                     const QString& thumbSourcePath = QString());

    // The PNG path for one version's thumbnail, or empty when it has none -
    // absent from the manifest (saved before this task, or a copy that
    // failed at save time, or a hand-edited manifest missing the key) reads
    // exactly the same as "never had one": forward-compatible, never a
    // refusal. Empty too for an unknown furniture or version name.
    QString versionThumbPath(const QString& id, const QString& name) const;

    // Loads a version into a SCRATCH document first, exactly like
    // loadFurniture - a version that fails to decode must not disturb
    // `doc`. False for an unknown furniture id or version name, or any
    // refusal the decode reports.
    bool loadVersion(const QString& id, const QString& name, DocumentModel& doc);

    // Removes a version's blob and its manifest entry. False if the
    // furniture or the named version does not exist.
    bool deleteVersion(const QString& id, const QString& name);

private:
    // `format` (int) is the manifest's own version - bumped only if this
    // task's JSON layout itself ever needs to change shape (separate from
    // FurnifySerial::kFormatVersion, which versions shapes.bin's binary
    // layout). A manifest missing the key, or carrying anything other than
    // this value, refuses to load - see loadFurniture()'s doc comment.
    static constexpr int kManifestFormatVersion = 1;

    QString myRootDir;

    QString furnitureDir(const QString& id) const;
    QString manifestPath(const QString& id) const;
    QString shapesPath(const QString& id) const;
    QString thumbPath(const QString& id) const;
    QString versionsDir(const QString& id) const;

    // Parses manifest.json into a QJsonObject; an empty object (rather than
    // a null/error return) for a missing file, unreadable file, or a file
    // that does not parse as a JSON object - callers distinguish "no
    // furniture here" via the file-existence checks they already make
    // before calling this.
    QJsonObject readManifestObject(const QString& id) const;
    // Atomic manifest write: QSaveFile temp-file-then-commit, never a
    // truncate onto the live manifest.json. False (and the OLD manifest left
    // standing) on any failure - see the .cpp's own comment.
    bool writeManifestObject(const QString& id, const QJsonObject& manifest) const;

    // Atomic shapes-blob write, shared by saveFurniture() and the version
    // blob saveVersion() writes: serializes `serial` to a temp file beside
    // `targetPath`, verifies the stream actually flushed at close(), and
    // only then replaces the live file with QFile::rename() - never a
    // std::ios::trunc write straight onto it. False (and the OLD file at
    // `targetPath`, if any, left standing) on any failure - see the .cpp's
    // own comment.
    bool writeShapesFileAtomic(const QString& targetPath,
                               const FurnifySerial::SerializedDocument& serial) const;
};
