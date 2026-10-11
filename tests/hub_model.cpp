// HubModel tests: the library hub's logic, with no window and no GPU (Qt's
// offscreen platform), registered with ctest. The behaviour pinned here is
// SelectorWindow's own, sentence for sentence - the HTML hub is a port.
#include "session/HubModel.h"

#include "DocumentModel.h"
#include "FurnitureStore.h"
#include "ModelingOps.h"
#include "SceneModel.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const QString& what)
{
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FAIL  %s\n", qPrintable(what));
    }
}

// QSignalSpy lives in Qt Test, which the vcpkg Qt does not ship; the session
// tests build on both Qts, so a signal's arguments are collected by hand.
struct Strings {
    QStringList seen;
    int count() const { return static_cast<int>(seen.size()); }
    bool isEmpty() const { return seen.isEmpty(); }
    QString at(int i) const { return seen.value(i); }
    QString last() const { return seen.isEmpty() ? QString() : seen.last(); }
};
template <typename Signal>
void collect(HubModel& hub, Signal signal, Strings& into)
{
    QObject::connect(&hub, signal, [&into](const QString& s) { into.seen << s; });
}

}  // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    // --- dates ---------------------------------------------------------------------
    {
        const QDateTime now(QDate(2026, 10, 11), QTime(15, 0));   // a Sunday
        const QString clock = QLocale().toString(QTime(9, 5), QLocale::ShortFormat);
        check(HubModel::editedText(QDateTime(QDate(2026, 10, 11), QTime(9, 5)), now) ==
                  QStringLiteral("Today, ") + clock,
              "today reads Today, <time>");
        check(HubModel::editedText(QDateTime(QDate(2026, 10, 10), QTime(9, 5)), now) ==
                  QStringLiteral("Yesterday, ") + clock,
              "yesterday reads Yesterday, <time>");
        check(HubModel::editedText(QDateTime(QDate(2026, 10, 7), QTime(9, 5)), now) ==
                  QStringLiteral("Wednesday, ") + clock,
              "inside a week reads <weekday>, <time> - got " +
                  HubModel::editedText(QDateTime(QDate(2026, 10, 7), QTime(9, 5)), now));
        check(HubModel::editedText(QDateTime(QDate(2026, 3, 2), QTime(9, 5)), now) ==
                  QStringLiteral("2 Mar"),
              "this year reads d MMM");
        check(HubModel::editedText(QDateTime(QDate(2025, 3, 2), QTime(9, 5)), now) ==
                  QStringLiteral("2 Mar 2025"),
              "another year reads d MMM yyyy");
        check(HubModel::editedText(QDateTime(), now) == QStringLiteral("Never saved"),
              "no save yet reads Never saved");
    }

    // --- the delete Note -------------------------------------------------------------
    check(HubModel::deletedNoteText(QStringLiteral("Bench"), 0) == QStringLiteral("Deleted Bench"),
          "a delete that breaks nothing names the furniture");
    check(HubModel::deletedNoteText(QStringLiteral("Bench"), 1) ==
              QStringLiteral("Deleted Bench — 1 scene now shows it as missing"),
          "one scene, singular");
    check(HubModel::deletedNoteText(QStringLiteral("Bench"), 2) ==
              QStringLiteral("Deleted Bench — 2 scenes now show it as missing"),
          "several scenes, plural");

    // --- against a real store ----------------------------------------------------------
    {
        QTemporaryDir root;
        check(root.isValid(), "a temp library");
        FurnitureStore store(root.path());
        HubModel hub(store);
        Strings chosen, failures, notes;
        collect(hub, &HubModel::furnitureChosen, chosen);
        collect(hub, &HubModel::failure, failures);
        collect(hub, &HubModel::note, notes);

        check(hub.suggestedName() == store.nextFurnitureName(), "the suggested name is the store's next");
        hub.createFurniture(QStringLiteral("Bench"));
        check(chosen.count() == 1 && failures.isEmpty(), "create chooses the new furniture");
        const QString benchId = chosen.at(0);
        check(hub.furnitureList().size() == 1 &&
                  hub.furnitureList().at(0).toMap().value("name").toString() == "Bench",
              "and lists it");

        // A scene that uses the bench, for the delete's count.
        const QString sceneId = store.createScene(QStringLiteral("Dining"));
        SceneModel scene;
        scene.addPiece(benchId.toStdString(), "Bench");
        check(store.saveScene(sceneId, scene, QImage()), "a scene using the bench is saved");
        hub.refresh();
        check(hub.sceneList().size() == 1, "the scene is listed in its own list");
        check(hub.usedIn(benchId) == QStringList({"Dining"}), "usedIn names the scene");

        // Rename: empty and unchanged are no change.
        int changedCount = 0;
        QObject::connect(&hub, &HubModel::changed, [&] { ++changedCount; });
        hub.rename(benchId, false, QStringLiteral("   "));
        hub.rename(benchId, false, QStringLiteral("Bench"));
        check(changedCount == 0 && failures.isEmpty(), "an empty or unchanged rename writes nothing");
        hub.rename(benchId, false, QStringLiteral("Hall bench"));
        check(store.listFurniture().value(0).name == QStringLiteral("Hall bench") && changedCount == 1,
              "a real rename reaches the store and refreshes");

        // The thumbnail's stamp moves when the picture is rewritten.
        QImage pic(16, 10, QImage::Format_RGB32);
        pic.fill(Qt::red);
        DocumentModel doc;
        doc.addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 10, 10, 10));
        store.saveFurniture(benchId, doc, pic);
        hub.refresh();
        const QString url1 = hub.furnitureList().at(0).toMap().value("thumbUrl").toString();
        QThread::msleep(1100);   // file times on some filesystems are whole seconds
        pic.fill(Qt::blue);
        store.saveFurniture(benchId, doc, pic);
        hub.refresh();
        const QString url2 = hub.furnitureList().at(0).toMap().value("thumbUrl").toString();
        check(!url1.isEmpty() && url1 != url2,
              "a rewritten thumbnail gets a new URL, so the page cannot show the old one (" + url1 +
                  " / " + url2 + ")");

        // Delete: the Note counts the scenes BEFORE the delete.
        hub.remove(benchId, false);
        check(notes.count() == 1 &&
                  notes.at(0) ==
                      QStringLiteral("Deleted Hall bench — 1 scene now shows it as missing"),
              "delete reports a Note naming the scene count taken before it - got " +
                  (notes.count() ? notes.at(0) : QStringLiteral("nothing")));
        check(hub.furnitureList().isEmpty(), "and the card is gone");

        // A scene: created with a numbered name and chosen at once.
        Strings sceneChosen;
        collect(hub, &HubModel::sceneChosen, sceneChosen);
        hub.createScene();
        check(sceneChosen.count() == 1 && hub.sceneList().size() == 2,
              "+ New scene makes a numbered scene and chooses it");

        // Rename refusal: the furniture's folder is gone from under it.
        hub.createFurniture(QStringLiteral("Doomed"));
        const QString doomed = chosen.at(1);
        QString doomedDir;
        for (const FurnitureStore::FurnitureInfo& i : store.listFurniture())
            if (i.id == doomed) doomedDir = i.filePath;
        check(!doomedDir.isEmpty() && QDir(doomedDir).removeRecursively(), "the doomed folder is removed");
        hub.rename(doomed, false, QStringLiteral("New Name"));
        check(failures.count() == 1 &&
                  failures.last() ==
                      QStringLiteral("Couldn't rename this furniture to New Name — Check that its "
                                     "folder still exists and isn't read-only"),
              "a refused rename says the hub's own sentence - got " +
                  (failures.count() ? failures.last() : QStringLiteral("nothing")));
    }

    // --- create refusal: the library path runs through a file -------------------------
    {
        QTemporaryDir parent;
        const QString blocking = parent.path() + QStringLiteral("/blocked");
        QFile f(blocking);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
        FurnitureStore store(blocking + QStringLiteral("/library"));
        HubModel hub(store);
        Strings failures, chosen;
        collect(hub, &HubModel::failure, failures);
        collect(hub, &HubModel::furnitureChosen, chosen);
        hub.createFurniture(QStringLiteral("Nowhere"));
        check(chosen.isEmpty() && failures.count() == 1 &&
                  failures.at(0) ==
                      QStringLiteral("Couldn't create Nowhere — Check that the library folder "
                                     "still exists and isn't read-only"),
              "a refused create says the hub's own sentence and chooses nothing");
    }

    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
