# Editor Session, Slice A — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give FurnifyMe an `EditorSession` — a QtCore + QtGui object, no Widgets — that owns the document, its checkpoints, one outcome channel every toast and status line goes through, and a registry of every action, with `MainWindow` reduced to rendering what the session reports. Nothing the user sees changes.

**Architecture:** A new static library `furnify_session` links `furnify_geometry`, `Qt6::Core` and `Qt6::Gui` and nothing from Widgets, so a widget include inside it fails to compile — the same structural guard `furnify_geometry` already gives the kernel. `MainWindow` owns one `EditorSession` and keeps a `DocumentModel&` alias to its document, so the 355 existing `myDocument.` call sites are untouched. Outcomes travel as a value (`Outcome`) on one signal; `MainWindow::onReported()` is the only code that turns one into a toast or a status line. This is the seam Phase 3 plugs the HTML layer into: the page subscribes to the same signal.

**Tech Stack:** C++17, Qt 6.11.1 (Core, Gui; Widgets only in `furnify_app`), OCCT 8.0.1, CMake presets (`windows` for the gate, vcpkg Qt).

**Spec:** the migration report, "Recommended architecture → The app core" and "Migration plan → Phase 1": https://claude.ai/code/artifact/30802edc-e639-4532-a1f0-19661e952d5a

## Global Constraints

- **No visible change.** Every sentence, toast, status line, enabled state and shortcut reads exactly as before.
- **Phase 1 gate:** an unfiltered `gui_smoke` run reports the identical count (checks + environment skips) before and after, measured twice on each side. A filtered run is never evidence for a contract shared across blocks.
- `furnify_session` includes no QtWidgets header and links no Widgets target.
- `ModelingOps` keeps its zero-Qt rule; nothing here touches `furnify_geometry`.
- Measured, never computed: `kCheckFloor` is not edited in this slice — the count must not move.
- No modal dialogs; Notes may be silenced, Failures never (CLAUDE.md "Reporting outcomes").
- Work on branch `editor-session` in worktree `D:/PizzaData/C++/FurnifyMe-html-spike`; `items-drawer` is never touched.

## Review Focus

1. **A Note that used to update the status bar must still do so, and the one raw Note toast must still NOT.** `reportNote()` writes the left channel; the single direct `Toast::Kind::Note` call outside it does not. Pinned in Task 3 by the two `Outcome` flavours and a session test.
2. **Render mode still exits before every checkpoint.** The exit moved behind a signal; a reordering would let a checkpoint land while render mode is still on. Pinned in Task 4 by a session test on signal order and by gui_smoke's render-mode blocks.
3. **The status bar's "refusal withdrawal" still compares against what is really on screen.** `MainWindow` reads `statusBar()->currentMessage()` back in several places; `onReported(Status)` must write synchronously so those reads see the new line. Pinned in Task 3 (direct connection, asserted by a session test that the signal is delivered before `report…()` returns).
4. **Two actions whose text slugs to the same id.** The registry must refuse rather than silently keep one. Pinned in Task 5 by a test with two actions named "Fit All" and "Fit all".
5. **Triggering a disabled action from the registry.** `QAction::trigger()` ignores `isEnabled()`; a page button that fires a disabled action would bypass `updateActions()`. Pinned in Task 5: `ActionRegistry::trigger()` returns false and does nothing.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/session/Outcome.h` | The value one outcome travels as: kind (Note, Failure, Status), text, whether it offers Undo, the document stamp, whether a Note also writes the status line |
| `src/session/EditorSession.{h,cpp}` | Owns `DocumentModel`; `checkpoint()`/`undo()`/`redo()`; the outcome channel; `statusLineFor()`; the undo/redo status sentences |
| `src/session/ActionRegistry.{h,cpp}` | Every `QAction` by a stable id; a JSON snapshot of their state; `trigger(id)` that obeys `isEnabled()` |
| `tests/session.cpp` | Session tests — QtGui on the `offscreen` platform, no GPU, registered with ctest |
| `src/MainWindow.{h,cpp}` | Owns one `EditorSession`; `onReported()` renders outcomes; call sites switched to the session |
| `CMakeLists.txt` | The `furnify_session` target and the `session` test |

---

### Task 1: Measure the baseline

**Files:** none changed.

- [ ] **Step 1: Build `gui_smoke` with the normal preset**

Run (worktree root): `cmake --preset windows && cmake --build --preset windows --target gui_smoke --parallel`
Expected: `build/RelWithDebInfo/gui_smoke.exe` with a LastWriteTime from this build.

- [ ] **Step 2: Two unfiltered runs**

Run: `build/RelWithDebInfo/gui_smoke.exe build/smoke-baseline-1 > build/smoke-baseline-1.log` and again into `-2`.
Expected: both logs end with the floor line; record `checks`, `skips` and failures from each. They must agree to the digit. If they do not, stop: the gate has no baseline.

- [ ] **Step 3: Record the number** in this plan's "Gate record" section at the bottom and in the board card.

---

### Task 2: `furnify_session` with the document in it

**Files:**
- Create: `src/session/EditorSession.h`, `src/session/EditorSession.cpp`, `tests/session.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.h` (member `myDocument`), `src/MainWindow.cpp` (constructor init list)

**Interfaces:**
- Produces: `class EditorSession : public QObject` with `DocumentModel& document()`, `const DocumentModel& document() const`.

- [ ] **Step 1: Write the failing test** — `tests/session.cpp`

```cpp
// Session tests: QtGui on the offscreen platform - no window, no GPU - so
// they run anywhere Qt does, unlike gui_smoke.
#include "session/EditorSession.h"
#include "ModelingOps.h"

#include <QGuiApplication>
#include <cstdio>

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
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    {
        EditorSession session;
        check(session.document().count() == 0, "a new session holds an empty document");
        const int id = session.document().addSolid(ModelingOps::makeBox(gp_Pnt(0, 0, 0), 10, 10, 10));
        check(id > 0 && session.document().count() == 1, "the session's document is a real DocumentModel");
    }

    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Add the target and the test to `CMakeLists.txt`**, inside `if(APP_SOURCES_PRESENT)` before `add_library(furnify_app ...)`:

```cmake
    # The editor session (migration Phase 1): the document, its checkpoints,
    # the outcome channel and the action registry - everything the HTML layer
    # will talk to. Core and Gui ONLY: a QtWidgets include in here fails to
    # compile, which is the structural guard furnify_geometry gives the kernel.
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(furnify_session STATIC
      src/session/EditorSession.cpp
    )
    target_include_directories(furnify_session PUBLIC src)
    target_link_libraries(furnify_session PUBLIC furnify_geometry Qt6::Core Qt6::Gui)

    add_executable(session_tests tests/session.cpp)
    target_link_libraries(session_tests PRIVATE furnify_session)
    add_test(NAME session COMMAND session_tests)
```

and add `furnify_session` to `furnify_app`'s `target_link_libraries(... PUBLIC ...)` list.

- [ ] **Step 3: Run it to see it fail**

Run: `cmake --build --preset windows --target session_tests`
Expected: compile error, `session/EditorSession.h` not found.

- [ ] **Step 4: Write the session** — `src/session/EditorSession.h`

```cpp
#pragma once

// The editor session (migration Phase 1, slice A): the document and
// everything about it that is not a widget. QtCore and QtGui only - the
// furnify_session target links nothing from Widgets, so a widget include here
// fails to compile. MainWindow owns one and renders what it reports; the HTML
// layer will subscribe to the same signals.

#include "DocumentModel.h"

#include <QObject>

class EditorSession : public QObject
{
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    DocumentModel& document() { return myDocument; }
    const DocumentModel& document() const { return myDocument; }

private:
    DocumentModel myDocument;
};
```

`src/session/EditorSession.cpp`

```cpp
#include "session/EditorSession.h"

EditorSession::EditorSession(QObject* parent)
    : QObject(parent)
{
}
```

- [ ] **Step 5: Run the test**

Run: `cmake --build --preset windows --target session_tests && ctest --test-dir build -C RelWithDebInfo -R session --output-on-failure`
Expected: `2 checks, 0 failures`, ctest passes.

- [ ] **Step 6: Move `MainWindow`'s document into the session.** In `src/MainWindow.h` replace `DocumentModel myDocument;` (line ~1905) with:

```cpp
    // The document lives in the session (migration Phase 1). The alias keeps
    // every existing myDocument. call site as it was; it is bound in the
    // constructor's init list to mySession.document(), and mySession is
    // declared first so it exists when the alias is bound.
    EditorSession mySession;
    DocumentModel& myDocument;
```

Add `#include "session/EditorSession.h"` beside `#include "DocumentModel.h"`. In `MainWindow::MainWindow(...)`'s init list (`src/MainWindow.cpp:651`) add `, myDocument(mySession.document())` in declaration order. If the compiler warns about init-list order, move the two lines to where `myDocument` was declared and keep `mySession` directly above it.

- [ ] **Step 7: Build the app and run the cheap blocks**

Run: `cmake --build --preset windows --parallel && build/RelWithDebInfo/gui_smoke.exe build/smoke-t2 undo`
Expected: builds; the filtered run prints `FILTERED (0 failures, N checks)`.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/session tests/session.cpp src/MainWindow.h src/MainWindow.cpp
git commit -m "Editor session: furnify_session owns the document; MainWindow aliases it"
```

---

### Task 3: One outcome channel

**Files:**
- Create: `src/session/Outcome.h`
- Modify: `src/session/EditorSession.{h,cpp}`, `tests/session.cpp`, `src/MainWindow.{h,cpp}`

**Interfaces:**
- Consumes: `EditorSession` (Task 2).
- Produces:
  - `struct Outcome { enum class Kind { Note, Failure, Status }; Kind kind; QString text; bool offersUndo; int documentStamp; bool writesStatus; }`
  - `EditorSession::reportNote(const QString& text, bool offersUndo = false, int documentStamp = -1)` — a Note that also writes the status line (today's `MainWindow::reportNote`).
  - `EditorSession::reportToastOnly(const QString& text, bool offersUndo = false, int documentStamp = -1)` — a Note that does not (today's single raw `Toast::Kind::Note` call).
  - `EditorSession::reportFailure(const QString& text, bool offersUndo = false, int documentStamp = -1)`
  - `EditorSession::reportStatus(const QString& text)`
  - `static QString EditorSession::statusLineFor(const QString& sentence)`
  - signal `void reported(const Outcome& outcome)`

- [ ] **Step 1: Write the failing tests** — append inside `main()` of `tests/session.cpp`, before the summary line:

```cpp
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
```

Add `#include <vector>` at the top.

- [ ] **Step 2: Run to see it fail**

Run: `cmake --build --preset windows --target session_tests`
Expected: compile errors — `Outcome` and `reportNote` unknown.

- [ ] **Step 3: Write `src/session/Outcome.h`**

```cpp
#pragma once

#include <QString>

// One thing the app has to tell the user, as a value. EditorSession emits it;
// MainWindow renders it as a toast or a status line, and the HTML layer will
// render the same value. Kinds follow CLAUDE.md's taxonomy: a Note reports a
// success and may be silenced, a Failure carries a reason and never is, and a
// Status is a line for the status bar's left channel alone.
struct Outcome
{
    enum class Kind { Note, Failure, Status };
    Kind kind = Kind::Status;
    QString text;
    bool offersUndo = false;
    int documentStamp = -1;
    // A Note normally also leaves its sentence on the status bar; the one raw
    // toast that never did keeps not doing so (reportToastOnly()).
    bool writesStatus = true;
};
```

- [ ] **Step 4: Extend `EditorSession`** — in the header add `#include "session/Outcome.h"` and, in the public section:

```cpp
    void reportNote(const QString& text, bool offersUndo = false, int documentStamp = -1);
    void reportToastOnly(const QString& text, bool offersUndo = false, int documentStamp = -1);
    void reportFailure(const QString& text, bool offersUndo = false, int documentStamp = -1);
    void reportStatus(const QString& text);
    // Status-bar copy takes no trailing period; a toast's sentence may end in
    // one, and the bar is where the same words have to read as a status line.
    static QString statusLineFor(const QString& sentence);

signals:
    void reported(const Outcome& outcome);
```

In the `.cpp`:

```cpp
namespace {
Outcome make(Outcome::Kind kind, const QString& text, bool offersUndo, int stamp, bool writesStatus)
{
    Outcome o;
    o.kind = kind;
    o.text = text;
    o.offersUndo = offersUndo;
    o.documentStamp = stamp;
    o.writesStatus = writesStatus;
    return o;
}
}

void EditorSession::reportNote(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Note, text, offersUndo, documentStamp, true));
}

void EditorSession::reportToastOnly(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Note, text, offersUndo, documentStamp, false));
}

void EditorSession::reportFailure(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Failure, text, offersUndo, documentStamp, false));
}

void EditorSession::reportStatus(const QString& text)
{
    emit reported(make(Outcome::Kind::Status, text, false, -1, true));
}

QString EditorSession::statusLineFor(const QString& sentence)
{
    QString line = sentence.trimmed();
    while (line.endsWith(QLatin1Char('.'))) line.chop(1);
    return line;
}
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset windows --target session_tests && ctest --test-dir build -C RelWithDebInfo -R session --output-on-failure`
Expected: `9 checks, 0 failures`.

- [ ] **Step 6: `MainWindow` renders outcomes.** Add a private slot `void onReported(const Outcome& outcome);` to `MainWindow.h` (in the `private slots:` section — read up to the nearest access specifier first, CLAUDE.md's moc trap). Implement it from today's `reportNote()` body:

```cpp
void MainWindow::onReported(const Outcome& outcome)
{
    switch (outcome.kind) {
    case Outcome::Kind::Note:
        myToasts->show(outcome.text, Toast::Kind::Note, outcome.offersUndo, outcome.documentStamp);
        if (!outcome.writesStatus) break;
        // NOT ALSO ON THE LEFT (the 2026-10 review's F9): while the toast
        // carries the report, the left channel goes back to the selection.
        // With notifications off the toast was dropped, and the bar is then
        // the quiet record of what happened.
        if (myToasts->isShowing() && myToasts->currentText() == outcome.text) showSelectionStatus();
        else statusBar()->showMessage(EditorSession::statusLineFor(outcome.text));
        break;
    case Outcome::Kind::Failure:
        // The status line follows through ToastHost::failureShown, as before.
        myToasts->show(outcome.text, Toast::Kind::Failure, outcome.offersUndo, outcome.documentStamp);
        break;
    case Outcome::Kind::Status:
        statusBar()->showMessage(outcome.text);
        break;
    }
}
```

Connect it in the constructor directly after `myToasts = new ToastHost(myView, this);`:

```cpp
    // Every outcome the session reports lands here, and only here. Direct, so
    // the status bar holds the new line before report…() returns - several
    // callers read currentMessage() straight back.
    connect(&mySession, &EditorSession::reported, this, &MainWindow::onReported,
            Qt::DirectConnection);
```

- [ ] **Step 7: Switch the call sites, mechanically.**
  - `reportNote(a, b, c)` → `mySession.reportNote(a, b, c)` (36 sites). Then delete `MainWindow::reportNote()` and its declaration.
  - `myToasts->show(X, Toast::Kind::Failure, U[, S])` → `mySession.reportFailure(X, U[, S])` (35 sites).
  - The one `myToasts->show(X, Toast::Kind::Note, …)` outside `onReported()` → `mySession.reportToastOnly(…)`.
  - `statusBar()->showMessage(X)` → `mySession.reportStatus(X)` (44 sites) **except** inside `showSelectionStatus()`, `onReported()` and the `failureShown` lambda, which stay on the status bar because they are the renderer.
  - `MainWindow::statusLineFor(...)` → `EditorSession::statusLineFor(...)`; delete the member.

After the edit, these greps must print only the renderer lines:

```bash
grep -n "myToasts->show(" src/MainWindow.cpp          # only inside onReported()
grep -n "showMessage(" src/MainWindow.cpp             # onReported, showSelectionStatus, failureShown lambda
grep -n "reportNote(" src/MainWindow.cpp | grep -v mySession   # nothing
```

- [ ] **Step 8: Build and run the blocks that report the most**

Run: `cmake --build --preset windows --parallel && build/RelWithDebInfo/gui_smoke.exe build/smoke-t3 toast`
Expected: `FILTERED (0 failures, …)`. Then the same with filters `mitre`, `boolean`, `re-measure`.

- [ ] **Step 9: Commit**

```bash
git add src/session tests/session.cpp src/MainWindow.h src/MainWindow.cpp
git commit -m "Editor session: one outcome channel; MainWindow only renders it"
```

---

### Task 4: Checkpoints, undo and redo through the session

**Files:**
- Modify: `src/session/EditorSession.{h,cpp}`, `tests/session.cpp`, `src/MainWindow.cpp` (`checkpointDocument()` ~6788, `onUndo()` ~6184, `onRedo()` ~6234)

**Interfaces:**
- Produces:
  - `void EditorSession::checkpoint()` — emits `aboutToChangeDocument()` then checkpoints.
  - `bool EditorSession::undo()`, `bool EditorSession::redo()` — the document's own, unwrapped.
  - `static QString EditorSession::undoneStatusText(int bodies)`, `static QString EditorSession::redoneStatusText(int bodies)`.
  - signal `void aboutToChangeDocument()`.

- [ ] **Step 1: Write the failing tests** — append to `tests/session.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to see it fail**

Run: `cmake --build --preset windows --target session_tests`
Expected: compile errors — `checkpoint`, `aboutToChangeDocument` unknown.

- [ ] **Step 3: Implement**

Header (public): `void checkpoint(); bool undo(); bool redo(); static QString undoneStatusText(int bodies); static QString redoneStatusText(int bodies);` and in `signals:` `void aboutToChangeDocument();`.

```cpp
void EditorSession::checkpoint()
{
    // "Any document-changing action leaves render mode first" - the listener
    // (MainWindow) does the leaving; the order is the contract.
    emit aboutToChangeDocument();
    myDocument.checkpoint();
}

bool EditorSession::undo() { return myDocument.undo(); }
bool EditorSession::redo() { return myDocument.redo(); }

QString EditorSession::undoneStatusText(int bodies)
{
    return bodies == 1 ? tr("Undone — 1 body in the document")
                       : tr("Undone — %1 bodies in the document").arg(bodies);
}

QString EditorSession::redoneStatusText(int bodies)
{
    return bodies == 1 ? tr("Redone — 1 body in the document")
                       : tr("Redone — %1 bodies in the document").arg(bodies);
}
```

- [ ] **Step 4: Run the tests** — Expected: `15 checks, 0 failures`.

- [ ] **Step 5: Route `MainWindow` through it.**
  - In the constructor, beside the `reported` connection: `connect(&mySession, &EditorSession::aboutToChangeDocument, this, [this] { if (myRenderModeOn) setRenderModeEnabled(false); }, Qt::DirectConnection);`
  - `checkpointDocument()`: replace `if (myRenderModeOn) setRenderModeEnabled(false); myDocument.checkpoint();` with `mySession.checkpoint();` — keep the comment block, adding one line saying the exit is now the `aboutToChangeDocument` listener.
  - `onUndo()`/`onRedo()`: `myDocument.undo()` → `mySession.undo()`, `myDocument.redo()` → `mySession.redo()`; the closing `mySession.reportStatus(...)` from Task 3 takes `EditorSession::undoneStatusText(myDocument.count())` / `redoneStatusText(...)`. Keep the explicit render-mode exit at the top of both: undo takes no new checkpoint, which is exactly why it was written there.
  - `grep -n "myDocument.checkpoint()" src/MainWindow.cpp` must print nothing.

- [ ] **Step 6: Run the render-mode and undo blocks**

Run: `build/RelWithDebInfo/gui_smoke.exe build/smoke-t4 render` and `… undo`
Expected: `FILTERED (0 failures, …)` for both.

- [ ] **Step 7: Commit** — `git commit -am "Editor session: checkpoints, undo and redo go through it"`

---

### Task 5: The action registry

**Files:**
- Create: `src/session/ActionRegistry.{h,cpp}`
- Modify: `CMakeLists.txt` (add the `.cpp` to `furnify_session`), `src/session/EditorSession.h` (own one), `tests/session.cpp`, `src/MainWindow.cpp` (adopt actions at the end of the constructor)

**Interfaces:**
- Produces:
  - `class ActionRegistry : public QObject` with
    - `static QString idFor(const QString& actionText)` — mnemonic `&` and a trailing `…`/`...` removed, lower-cased, runs of non-alphanumerics → `-`, trimmed of `-`.
    - `bool add(QAction* action)` — false (and logs the clash) when the id is empty or already taken.
    - `int adoptChildren(QObject* owner)` — `add()` for every `QAction` child with text; returns how many it took.
    - `QAction* action(const QString& id) const`
    - `QJsonArray snapshot() const` — per action `{id, text, shortcut, tooltip, enabled, checkable, checked}`, `text` without the mnemonic, `shortcut` in `QKeySequence::PortableText`.
    - `bool trigger(const QString& id)` — false and nothing done when unknown or disabled.
  - `ActionRegistry& EditorSession::actions()`.

- [ ] **Step 1: Write the failing tests** — append to `tests/session.cpp` (add `#include <QAction>`, `#include <QJsonArray>`, `#include <QJsonObject>`):

```cpp
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
```

- [ ] **Step 2: Run to see it fail** — Expected: compile error, `ActionRegistry` unknown.

- [ ] **Step 3: Implement** — `src/session/ActionRegistry.h`

```cpp
#pragma once

// Every command in the app by a stable id, for anything that is not a Qt
// widget - the HTML layer first. The QActions stay owned where they were made
// (MainWindow); this only indexes them. updateActions() remains the one place
// availability is decided: trigger() obeys isEnabled(), which
// QAction::trigger() itself does not.

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>

class QAction;

class ActionRegistry : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    static QString idFor(const QString& actionText);
    bool add(QAction* action);
    int adoptChildren(QObject* owner);
    QAction* action(const QString& id) const;
    QJsonArray snapshot() const;
    bool trigger(const QString& id);

private:
    QHash<QString, QPointer<QAction>> myActions;
    QStringList myOrder;   // registration order, so a snapshot is stable
};
```

`src/session/ActionRegistry.cpp`

```cpp
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
        qWarning("ActionRegistry: \"%s\" slugs to \"%s\", which is taken",
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
```

`EditorSession`: add `#include "session/ActionRegistry.h"`, a member `ActionRegistry myActions{this};` and `ActionRegistry& actions() { return myActions; }`. Add `src/session/ActionRegistry.cpp` to `furnify_session`.

- [ ] **Step 4: Run the tests** — Expected: `24 checks, 0 failures`.

- [ ] **Step 5: Adopt `MainWindow`'s actions.** At the end of `MainWindow::MainWindow()`:

```cpp
    // Every command, by id, for the HTML layer (migration Phase 1). Indexed,
    // not moved: the actions stay this window's, and updateActions() still
    // decides what is available.
    const int adopted = mySession.actions().adoptChildren(this);
    qDebug("EditorSession: %d actions registered", adopted);
```

- [ ] **Step 6: Check the registry saw every action once.** Run `furnifyme.exe` briefly is not needed — run `build/RelWithDebInfo/gui_smoke.exe build/smoke-t5 shortcut`; read the `qDebug` line in its output and the `ActionRegistry:` warnings. Expected: no clash warnings. If a clash appears, it is two actions with one name — report it in the board card rather than renaming either (a rename is a visible change, out of scope).

- [ ] **Step 7: Commit** — `git add src/session CMakeLists.txt tests/session.cpp src/MainWindow.cpp && git commit -m "Editor session: every action by id, and a trigger that obeys isEnabled()"`

---

### Task 6: The gate

**Files:** `CLAUDE.md` (file table), the board card.

- [ ] **Step 1: Two unfiltered runs** on the final tree, exactly as Task 1.
- [ ] **Step 2: Compare.** Expected: checks + skips equal to Task 1's number to the digit, 0 failures, in both runs. Any difference is a failure of this slice — find it, do not explain it.
- [ ] **Step 3: `ctest --test-dir build -C RelWithDebInfo`** — Expected: every test passes, `session` included.
- [ ] **Step 4: Document.** Add three rows to CLAUDE.md's Architecture table:

```markdown
| `session/EditorSession.{h,cpp}` | the document, checkpoints, undo/redo and the ONE outcome channel; Core + Gui only (`furnify_session`), headless-tested |
| `session/Outcome.h` | one thing to tell the user, as a value: Note, Failure or Status |
| `session/ActionRegistry.{h,cpp}` | every `QAction` by a stable id, a JSON snapshot, and a `trigger()` that obeys `isEnabled()` |
```

- [ ] **Step 5: Commit** — `git commit -am "Editor session slice A: gate measured identical; documented"`

---

## After slice A (separate plans, each ending on the same gate)

- **B — availability.** `updateActions()`'s predicates move into the session over a small `SelectionSource` interface (`selectionKind()`, `selectedSolidIds()`, `selectedFace()`, `selectedEdges()`) that `OcctViewWidget` implements. The session decides; `MainWindow` applies enabled/checked/tooltip.
- **C — gestures with no chip state:** boolean, slats.
- **D — gestures with a chip:** mitre, Re-measure, joint card, Mirror placement.
- **E — sketch, extrude, pull, bevel, transform.**
- **F — files:** open, save, autosave, versions, the close question.

## Gate record

| When | Checks | Skips | Failures | Run |
| --- | --- | --- | --- | --- |
| Baseline 1 | | | | |
| Baseline 2 | | | | |
| After slice A, run 1 | | | | |
| After slice A, run 2 | | | | |
