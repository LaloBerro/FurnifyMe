// VersionsModel (migration Phase 3c), headless: what the Versions drawer
// lists, the name a new version is offered, and that every request reaches
// MainWindow's hook exactly as asked - against a real FurnitureStore in a
// temp directory.
#include "DocumentModel.h"
#include "FurnitureStore.h"
#include "ModelingOps.h"
#include "session/VersionsModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const QString& what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    if (!ok) ++gFailures;
}
}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    FurnitureStore store(dir.path());
    const QString id = store.createFurniture(QStringLiteral("Bench"));
    DocumentModel doc;
    doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 600, 300, 18));
    store.saveFurniture(id, doc, QImage());

    QStringList saved, restored, compared, removed;
    int closes = 0;
    bool saveAnswer = true;
    VersionsModel::Hooks hooks;
    hooks.save = [&](const QString& n) { saved << n; return saveAnswer; };
    hooks.restore = [&](const QString& n) { restored << n; return true; };
    hooks.compare = [&](const QString& n) { compared << n; return true; };
    hooks.remove = [&](const QString& n) { removed << n; return true; };
    hooks.close = [&] { ++closes; };
    VersionsModel model(store, hooks);
    // QSignalSpy is Qt Test, which the vcpkg Qt does not ship - counted by hand.
    struct { int n = 0; int count() const { return n; } } changed, asked;
    QObject::connect(&model, &VersionsModel::changed, [&] { ++changed.n; });
    QObject::connect(&model, &VersionsModel::newRequested, [&] { ++asked.n; });

    // --- before a furniture is open ----------------------------------------
    check(model.versions().isEmpty() && !model.canSave() && !model.rowsEnabled(),
          "with no furniture open the drawer lists nothing and offers nothing");

    // --- the list ---------------------------------------------------------
    model.setState(id, true, true);
    check(model.versions().isEmpty() && model.canSave() && model.rowsEnabled(),
          "an open furniture with no versions: an empty list, + open");
    check(model.defaultName() == QStringLiteral("Version 1"), "the first version is offered as Version 1");

    QImage thumb(32, 20, QImage::Format_RGB32);
    thumb.fill(Qt::gray);
    const QString thumbFile = dir.filePath(QStringLiteral("t.png"));
    thumb.save(thumbFile);
    store.saveVersion(id, QStringLiteral("Version 2"), doc, thumbFile);
    store.saveVersion(id, QStringLiteral("Kitchen"), doc);
    const int changesBefore = changed.count();
    model.refresh();
    check(changed.count() == changesBefore + 1, "a refresh that finds new versions says so once");
    const QVector<FurnitureStore::VersionInfo> infos = store.versions(id);
    check(model.versions().size() == infos.size() && infos.size() == 2,
          QStringLiteral("the drawer lists what the store holds (%1)").arg(model.versions().size()));
    bool sameOrder = true;
    for (int i = 0; i < infos.size(); ++i)
        if (model.versions()[i].toMap().value("name").toString() != infos[i].name) sameOrder = false;
    check(sameOrder, "in the store's own order");
    const QVariantMap v2 = model.versions()[0].toMap().value("name") == QStringLiteral("Version 2")
                               ? model.versions()[0].toMap()
                               : model.versions()[1].toMap();
    const QVariantMap kitchen = model.versions()[0].toMap().value("name") == QStringLiteral("Kitchen")
                                    ? model.versions()[0].toMap()
                                    : model.versions()[1].toMap();
    const FurnitureStore::VersionInfo& v2Info =
        infos[0].name == QStringLiteral("Version 2") ? infos[0] : infos[1];
    check(v2.value("savedText").toString() ==
              QLocale().toString(v2Info.saved.toLocalTime(), QLocale::ShortFormat),
          "a version's date reads as the Qt drawer writes it - \"" + v2.value("savedText").toString() + "\"");
    check(v2.value("saved").toLongLong() == v2Info.saved.toMSecsSinceEpoch(), "and carries its time");
    const QUrl url(v2.value("thumbUrl").toString());
    check(url.isLocalFile() &&
              QFileInfo(url.toLocalFile()) == QFileInfo(store.versionThumbPath(id, QStringLiteral("Version 2"))) &&
              url.query().startsWith(QStringLiteral("v=")),
          "a version with a picture carries it as a stamped file URL - " + url.toString());
    check(kitchen.value("thumbUrl").toString().isEmpty(), "a version with none carries an empty URL");
    check(model.defaultName() == QStringLiteral("Version 3"),
          "the next name is one past the highest Version <n>, whatever else is named");

    const int quiet = changed.count();
    model.refresh();
    model.setState(id, true, true);
    check(changed.count() == quiet, "a refresh or a state that changes nothing says nothing");
    model.setState(id, false, true);
    check(changed.count() == quiet + 1 && !model.canSave(), "a gate moving says so");
    model.setState(QString(), false, false);
    check(model.versions().isEmpty(), "back at the library the list empties");
    model.setState(id, true, true);

    // --- the requests -----------------------------------------------------
    check(model.save(QStringLiteral("  Shelf added  ")) && saved == QStringList{QStringLiteral("Shelf added")},
          "save trims the name and asks MainWindow");
    check(!model.save(QStringLiteral("   ")) && saved.size() == 1, "a blank name saves nothing and asks nothing");
    saveAnswer = false;
    check(!model.save(QStringLiteral("Kitchen")), "a refused save answers false (the card stays up)");
    model.restore(QStringLiteral("Kitchen"));
    model.compare(QStringLiteral("Version 2"));
    model.remove(QStringLiteral("Kitchen"));
    model.close();
    check(restored == QStringList{QStringLiteral("Kitchen")} && compared == QStringList{QStringLiteral("Version 2")} &&
              removed == QStringList{QStringLiteral("Kitchen")} && closes == 1,
          "Restore, Compare, Delete and the close x each reach their hook once");
    model.setState(id, true, false);
    model.restore(QStringLiteral("Kitchen"));
    check(restored.size() == 1, "with rows shut (sketching) a row asks nothing");
    model.setState(id, false, true);
    saveAnswer = true;
    check(!model.save(QStringLiteral("Late")) && saved.size() == 2,
          "with + shut a save asks nothing (the gate is the model's too, not only the page's)");

    model.requestNew();
    check(asked.count() == 1, "File -> Save version... asks the card for a pending version");

    // --- the words --------------------------------------------------------
    const QVariantMap w = model.words();
    check(w.value("title") == QStringLiteral("Versions") && w.value("add") == QStringLiteral("Save a new version") &&
              w.value("compare") == QStringLiteral("Compare") && w.value("restore") == QStringLiteral("Restore") &&
              w.value("delete") == QStringLiteral("Delete") &&
              w.value("deleteArmed") == QStringLiteral("Delete — click again") &&
              w.value("close") == QStringLiteral("Close this panel") &&
              w.value("deleteMs").toInt() == VersionsModel::kDeleteConfirmMs &&
              w.value("empty") == QStringLiteral("No versions yet.\n\nPress + to keep a named snapshot you can come back to."),
          "the drawer's words are the Qt drawer's, verbatim");

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
