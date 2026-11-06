// The scene window's two cards as data for the chrome page (migration Phase
// 5b), on Qt's offscreen platform. The words are the Qt cards' own; what is
// pinned here is the rows the page draws, the two-click Remove, and that every
// request leaves as the same signal the Qt card sends - so SceneWindow wires
// both cards to one handler each.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "AddPieceCard.h"
#include "ScenePiecesPanel.h"
#include "Theme.h"
#include "web/SceneCardsBridge.h"

#include <QAction>
#include <QApplication>

#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

QVariantMap rowOf(const QVariantMap& state, int id)
{
    for (const QVariant& row : state.value(QStringLiteral("rows")).toList())
        if (row.toMap().value(QStringLiteral("id")).toInt() == id) return row.toMap();
    return {};
}
}  // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::apply(app);

    // --- the pieces card ------------------------------------------------------
    {
        ScenePiecesModel pieces;
        QAction add(QStringLiteral("Add piece"));
        pieces.setAddAction(&add);
        int announced = 0;
        QObject::connect(&pieces, &ScenePiecesModel::stateChanged, [&] { ++announced; });

        QVariantMap s = pieces.state();
        check(s.value(QStringLiteral("rows")).toList().isEmpty() &&
                  s.value(QStringLiteral("title")).toString() == ScenePiecesPanel::titleText() &&
                  s.value(QStringLiteral("empty")).toString() == ScenePiecesPanel::emptyText() &&
                  s.value(QStringLiteral("add")).toMap().value(QStringLiteral("text")).toString() == QLatin1String("Add piece"),
              "an empty scene: the card's title, its empty line and its Add piece row, in the Qt card's words");

        QVector<ScenePiecesPanel::Row> rows(2);
        rows[0].pieceId = 3;
        rows[0].name = QStringLiteral("Chair");
        rows[1].pieceId = 7;
        rows[1].name = QStringLiteral("Table");
        rows[1].visible = false;
        rows[1].reason = QStringLiteral("This furniture is gone");
        pieces.setRows(rows);
        pieces.setSelected(3);
        s = pieces.state();
        const QVariantMap chair = rowOf(s, 3), table = rowOf(s, 7);
        check(chair.value(QStringLiteral("name")).toString() == QLatin1String("Chair") &&
                  chair.value(QStringLiteral("selected")).toBool() && chair.value(QStringLiteral("visible")).toBool() &&
                  !table.value(QStringLiteral("visible")).toBool() &&
                  table.value(QStringLiteral("reason")).toString() == QLatin1String("This furniture is gone"),
              "each row carries its name, whether it shows, whether it is selected, and a broken piece's reason");
        check(announced > 0, "a change is announced");

        int removed = 0, removedId = 0;
        QObject::connect(&pieces, &ScenePiecesModel::removeRequested, [&](int id) { ++removed; removedId = id; });
        pieces.remove(3);
        check(removed == 0 && rowOf(pieces.state(), 3).value(QStringLiteral("armed")).toBool(),
              "the first click on Remove arms it and takes nothing out");
        check(s.value(QStringLiteral("words")).toMap().value(QStringLiteral("armed")).toString() ==
                  ScenePiecesPanel::armedRemoveText(),
              "and the armed words are the Qt card's own");
        pieces.remove(7);
        check(removed == 0 && !rowOf(pieces.state(), 3).value(QStringLiteral("armed")).toBool() &&
                  rowOf(pieces.state(), 7).value(QStringLiteral("armed")).toBool(),
              "arming another row disarms the first");
        pieces.remove(7);
        check(removed == 1 && removedId == 7, "the second click on the armed row takes it out");
        pieces.remove(3);
        pieces.disarm();
        pieces.remove(3);
        check(removed == 1, "disarm() drops the arm, so the next click arms again");

        int selected = 0, shownId = 0;
        bool shownTo = true;
        QString renamed;
        QObject::connect(&pieces, &ScenePiecesModel::selectionRequested, [&](int id) { selected = id; });
        QObject::connect(&pieces, &ScenePiecesModel::visibilityToggled, [&](int id, bool v) { shownId = id; shownTo = v; });
        QObject::connect(&pieces, &ScenePiecesModel::renameCommitted, [&](int, const QString& n) { renamed = n; });
        pieces.select(7);
        pieces.toggleVisible(3, false);
        pieces.rename(3, QStringLiteral("  Armchair "));
        check(selected == 7 && shownId == 3 && !shownTo && renamed == QLatin1String("Armchair"),
              "select, the eye and rename leave as the Qt card's own signals, the name trimmed");
        renamed.clear();
        pieces.rename(3, QStringLiteral("   "));
        pieces.rename(99, QStringLiteral("Ghost"));
        check(renamed.isEmpty(), "a blank name, or a piece that is not there, renames nothing");

        int adds = 0;
        QObject::connect(&add, &QAction::triggered, [&] { ++adds; });
        pieces.addPiece();
        add.setEnabled(false);
        pieces.addPiece();
        check(adds == 1 && !pieces.state().value(QStringLiteral("add")).toMap().value(QStringLiteral("enabled")).toBool(),
              "Add piece triggers the action, and refuses while it is disabled");
    }

    // --- the add card -----------------------------------------------------------
    {
        AddPieceModel card;
        QAction library(QStringLiteral("Close scene"));
        card.setLibraryAction(&library);
        check(!card.state().value(QStringLiteral("shown")).toBool(), "the add card starts closed");
        FurnitureStore::FurnitureInfo bench;
        bench.id = QStringLiteral("f1");
        bench.name = QStringLiteral("Bench");
        card.showFor({bench});
        QVariantMap s = card.state();
        const QVariantList entries = s.value(QStringLiteral("entries")).toList();
        check(s.value(QStringLiteral("shown")).toBool() && entries.size() == 1 &&
                  entries.at(0).toMap().value(QStringLiteral("name")).toString() == QLatin1String("Bench") &&
                  s.value(QStringLiteral("title")).toString() == AddPieceCard::titleText(),
              "showFor() opens it on the library's furniture, in the Qt card's words");
        QString chosen;
        QObject::connect(&card, &AddPieceModel::chosen, [&](const QString& id) { chosen = id; });
        card.choose(QStringLiteral("nope"));
        check(chosen.isEmpty() && card.state().value(QStringLiteral("shown")).toBool(),
              "an id the card does not list is refused");
        card.choose(QStringLiteral("f1"));
        check(chosen == QLatin1String("f1") && !card.state().value(QStringLiteral("shown")).toBool(),
              "choosing one closes the card and says which");
        card.showFor({});
        s = card.state();
        check(s.value(QStringLiteral("entries")).toList().isEmpty() &&
                  s.value(QStringLiteral("empty")).toString() == AddPieceCard::emptyLibraryText() &&
                  s.value(QStringLiteral("library")).toString() == AddPieceCard::libraryButtonText(),
              "an empty library says so and offers the way to it");
        int trips = 0;
        QObject::connect(&library, &QAction::triggered, [&] { ++trips; });
        card.goToLibrary();
        check(trips == 1 && !card.state().value(QStringLiteral("shown")).toBool(),
              "Go to the library closes the card and takes File's own route");
        card.showFor({bench});
        card.close();
        check(!card.state().value(QStringLiteral("shown")).toBool(), "close() closes it, adding nothing");
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
