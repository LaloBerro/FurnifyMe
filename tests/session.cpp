// Session tests: QtGui on the offscreen platform - no window, no GPU - so
// they run anywhere Qt does, unlike gui_smoke.
#include "session/EditorSession.h"
#include "ModelingOps.h"

#include <QAction>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    if (!ok) { ++gFailures; std::printf("FAIL  %s\n", what); }
}
}

int main(int argc, char* argv[])
{
    // NEVER A DIALOG. This runs unattended (ctest), and a fatal error that
    // opens a message box puts a window on the user's screen and hangs the
    // run until somebody clicks it - which happened once, before the
    // offscreen plugin was deployed beside this exe. Fatal messages go to
    // stderr, and Windows' own crash and error boxes are switched off.
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    {
        EditorSession session;
        check(session.document().count() == 0, "a new session holds an empty document");
        const int id = session.document().addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 10, 10, 10));
        check(id > 0 && session.document().count() == 1, "the session's document is a real DocumentModel");
    }

    {
        EditorSession session;
        std::vector<Outcome> seen;
        QObject::connect(&session, &EditorSession::reported,
                         [&](const Outcome& o) { seen.push_back(o); });

        session.reportNote(QStringLiteral("Deleted Body 02"), true, 7);
        // Delivered before reportNote() returns: MainWindow reads the status
        // bar back straight after reporting, so a queued delivery would show
        // it the previous line.
        check(seen.size() == 1, "a Note is delivered synchronously, once");
        check(seen.back().kind == Outcome::Kind::Note && seen.back().text == "Deleted Body 02" &&
                  seen.back().offersUndo && seen.back().documentStamp == 7 && seen.back().writesStatus,
              "reportNote carries text, Undo, stamp - and writes the status line");

        session.reportToastOnly(QStringLiteral("Tier: Shadows"));
        check(seen.back().kind == Outcome::Kind::Note && !seen.back().writesStatus,
              "the raw toast Note does NOT write the status line");

        session.reportFailure(QStringLiteral("Pull refused — the carve is deeper than the body."));
        check(seen.back().kind == Outcome::Kind::Failure, "a Failure is a Failure");

        session.reportStatus(QStringLiteral("Undone — 1 body in the document"));
        check(seen.back().kind == Outcome::Kind::Status && !seen.back().offersUndo,
              "a status line is its own kind and offers nothing");

        check(EditorSession::statusLineFor(QStringLiteral("  Saved.. ")) == QStringLiteral("Saved"),
              "statusLineFor trims and drops every trailing period");
        check(EditorSession::statusLineFor(QStringLiteral("v1.5 ready")) == QStringLiteral("v1.5 ready"),
              "statusLineFor keeps an inner period");
    }

    {
        EditorSession session;
        std::vector<int> revisionsAtSignal;
        QObject::connect(&session, &EditorSession::aboutToChangeDocument, [&] {
            revisionsAtSignal.push_back(session.document().revision());
        });
        const int before = session.document().revision();
        session.checkpoint();
        check(revisionsAtSignal.size() == 1 && revisionsAtSignal.front() == before,
              "aboutToChangeDocument fires BEFORE the checkpoint is taken");
        session.document().addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 5, 5, 5));
        check(session.undo() && session.document().count() == 0, "undo goes back through the session");
        check(session.redo() && session.document().count() == 1, "and redo comes forward");
        check(!session.redo(), "redo with nothing to redo answers false");
        check(EditorSession::undoneStatusText(1) == QStringLiteral("Undone — 1 body in the document") &&
                  EditorSession::undoneStatusText(3) == QStringLiteral("Undone — 3 bodies in the document"),
              "the undo sentence writes the plural out");
        check(EditorSession::redoneStatusText(1) == QStringLiteral("Redone — 1 body in the document"),
              "and so does the redo sentence");
    }

    {
        check(ActionRegistry::idFor(QStringLiteral("&Undo")) == QStringLiteral("undo"), "id drops the mnemonic");
        check(ActionRegistry::idFor(QStringLiteral("Save Screenshot…")) == QStringLiteral("save-screenshot"),
              "id drops a trailing ellipsis");
        check(ActionRegistry::idFor(QStringLiteral("New Folder with These")) ==
                  QStringLiteral("new-folder-with-these"),
              "id is a lower-case slug");

        EditorSession session;
        QObject owner;
        auto* fit = new QAction(QStringLiteral("Fit All"), &owner);
        fit->setShortcut(QKeySequence(QStringLiteral("F")));
        auto* clash = new QAction(QStringLiteral("Fit all"), &owner);
        (void)clash;
        auto* wire = new QAction(QStringLiteral("&Wireframe"), &owner);
        wire->setCheckable(true);
        wire->setChecked(true);
        int fired = 0;
        QObject::connect(fit, &QAction::triggered, [&] { ++fired; });
        new QAction(QString(), &owner);   // no text: not a command

        const int taken = session.actions().adoptChildren(&owner);
        check(taken == 2, "adopts the actions with text, refuses the slug clash");
        check(session.actions().action(QStringLiteral("fit-all")) == fit, "the first of a clash keeps the id");

        const QJsonArray snap = session.actions().snapshot();
        QJsonObject wireRow;
        for (const QJsonValue& v : snap)
            if (v.toObject().value("id").toString() == QStringLiteral("wireframe")) wireRow = v.toObject();
        check(wireRow.value("text").toString() == QStringLiteral("Wireframe") &&
                  wireRow.value("checkable").toBool() && wireRow.value("checked").toBool(),
              "the snapshot carries text without the mnemonic and the checked state");

        check(session.actions().trigger(QStringLiteral("fit-all")) && fired == 1, "trigger fires an enabled action");
        fit->setEnabled(false);
        check(!session.actions().trigger(QStringLiteral("fit-all")) && fired == 1,
              "trigger REFUSES a disabled action - QAction::trigger() would not");
        check(!session.actions().trigger(QStringLiteral("no-such-thing")), "an unknown id is refused");
    }

    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
