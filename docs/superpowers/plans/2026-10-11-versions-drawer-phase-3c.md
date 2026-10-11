# Versions Drawer as HTML (Migration Phase 3c) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In HTML-chrome mode the editor's chrome page draws and drives the Versions drawer: the list of saved versions with thumbnails, the `+` that opens a named pending card, Compare, Restore and a two-click Delete. It is a faithful port, and it wears the theme. The Qt `VersionsPanel` stays built (and still gated by `gui_smoke`) until Phase 6.

**Architecture:** The same three parts the Settings card used:
- **A model.** `VersionsModel` (in `furnify_session`, QtCore + QtGui only) lists the open furniture's versions from `FurnitureStore`, offers the default name, holds the drawer's words, and forwards the four row requests through injected hooks to `MainWindow`'s existing `saveVersion`/`restoreVersion`/`openCompare`/`deleteVersionByName`.
- **A slot.** An `HtmlCardSlot` takes the Qt drawer's TopLeft anchor while the page is live. Unlike the Settings slot its height follows the page: the card reports its natural height, and the overlay relays out.
- **A card.** A Svelte card draws the drawer at the slot's rectangle.

**Tech Stack:** C++17, Qt 6.11.1 (installer build, WebEngine + WebChannel), Svelte 5 + TypeScript + Vite, vitest + jsdom.

**Spec:** These four, read together:
- CLAUDE.md "Files, versions and the library": a version is file data. Restore is one undoable checkpoint; delete is a two-click confirm with no Undo; Compare is the read-only second viewport.
- `src/ui/VersionsPanel.{h,cpp}`: the behaviour being ported. Read the comments on `beginNewVersion`, `openPendingNameEdit`, `refresh` and the delete lambda.
- CLAUDE.md (worktree) "The HTML UI", including "What a page over the viewport owes" and the themes section.
- `D:/PizzaData/C++/FurnifyMe/DESIGN.md`: the locked style and its themes.

## Global Constraints

- **Where:** branch `versions-drawer` (cut from `themes`), worktree `D:/PizzaData/C++/FurnifyMe-html-spike`. Build with `cmake --build --preset windows-web`. Never merge, push or delete without asking.
- **`gui_smoke`:** it never enables HTML chrome. Its gate is **6448 checks + 1 environment skip, 0 failures, twice, quiet**, on the final tree, with `kCheckFloor` (6449) untouched.
- **Copy:** verbatim from `VersionsPanel.cpp`, `PanelCloseButton.cpp` and `MainWindow.cpp`. Every painted string passes `web/test/banned.ts`.
- **Failures:** every refusal keeps MainWindow's existing sentence, reported through `EditorSession`. The page shows no failure of its own.
- **Page rules:**
  - no `role=alert` and no focus ring;
  - chips wear `use:chip`;
  - the typed name field claims the keyboard while it is edited and gives it back after (ruling R5 of Phase 3b);
  - the page uses tokens and theme variables only, with no hex outside `Theme.cpp`.
- **Card size and shape:**
  - width `VersionsPanel::cardWidth()`, at least 160 tall;
  - the list scrolls past 384 logical px;
  - thumbnail 10:16, inset 3 px inside the card border;
  - title in the title font, weight 600, `textMuted`;
  - the `+` is 26 × 26 with an accent border;
  - row buttons are 22 tall;
  - the actions row is always present and 34 tall, and its buttons appear only on hover (or while Delete is armed).
- **Delete:** two clicks within 4000 ms. Labels "Delete" and "Delete — click again". The arm drops on its timer and when the card goes away.
- **Pending create:**
  - one at a time, seeded "Version N" (the highest "Version <n>" + 1);
  - Enter or focus-out commits; Escape discards; an empty or blank name discards;
  - a refused save (duplicate) keeps the card open with what was typed;
  - the card is discarded when `canSave` turns false or the drawer hides.
- **Evidence:** every new check is watched red before its code exists. A page change is looked at in a composited capture.

## Rulings (decided while planning; each costs what it says if wrong)

- **R1 The model is QtCore + QtGui, in `furnify_session`, beside `HubModel`.** It reads `FurnitureStore`. The four actions are `std::function` hooks MainWindow fills, so the model is tested headless with fake hooks.
  - *Cost:* a second listing of the rules (the Qt panel's) until Phase 6. That is the HubModel precedent.
- **R2 The slot's height follows the page.** `ChromeBridge::reportCardHeight(name, h)` sets a fixed height on the named slot, rounded through `Theme::wholeDevicePixels()`, and `MainWindow` relays the overlay out on `HtmlCardSlot::contentHeightChanged`. The card is drawn at the slot's rect. Its natural height is measured off an inner wrapper that does not depend on the rect, so the two cannot feed back.
  - *Cost:* one frame where a grown list is clipped before the slot follows.
- **R3 Thumbnails are file URLs carrying the picture's stamp** (`?v=<mtime ms>`), the HubModel rule. The page already reads local files (`LocalContentCanAccessFileUrls`).
  - *Cost:* none new.
- **R4 File → Save version… in HTML mode asks the page for a pending card** through `VersionsModel::newRequested`. `Editor.svelte` listens to that signal: it is always mounted, so a request that arrives before the card mounts is kept and the card consumes it on mount.
  - *Cost:* one more always-on listener.
- **R5 Focus-out commits a pending version, as in Qt.** That is InlineRename's app-wide contract, and the earlier ruling the Qt panel carries.
  - *Cost:* a click away saves "Version N" under the default name, exactly as today.
- **R6 The close × calls `VersionsModel::close()`**, which unchecks the Versions action through its hook. The action stays the drawer's only law.
  - *Cost:* none.

## Review Focus

1. **A typed name keeps every key.** Delete, Backspace, Ctrl+Z, `0`–`3` and Space typed into the pending name must edit the field and never reach a window shortcut. Escape discards the card and gives focus back to whatever had it. (Task 4 vitest, Task 5 chrome_smoke.)
2. **A duplicate name refuses without losing the card.** Saving a name that already exists shows MainWindow's Failure and leaves the card open, holding what was typed; a second, different name then saves. (Task 1 model, Task 5 chrome_smoke.)
3. **A refresh never disarms Delete or kills a pending card.** A Delete armed on one row must survive an unrelated refresh: a theme edit, a selection click. A refresh that changes the list (a version saved or deleted elsewhere) must not leave an armed Delete on the wrong row. (Task 4 vitest, keyed by name.)
4. **The card leaves cleanly.** When the drawer hides mid-edit (the action toggled, render mode, back to the library), the keyboard is given back, the pending card is discarded with nothing written, and an armed Delete is dropped. (Task 4 vitest, Task 5 chrome_smoke.)
5. **The slot follows the list in both directions.** Saving a version grows the slot so the new card is not clipped. Deleting the last one shrinks it back to the empty state's height, not to a stale tall slot that keeps swallowing presses over the viewport. (Task 5 chrome_smoke.)

---

## File Structure

| File | Responsibility |
|---|---|
| `src/session/VersionsModel.{h,cpp}` (new) | The drawer's model: list, default name, words, gates, the four hooks, `newRequested`. |
| `tests/versions_model.cpp` (new, ctest `versions_model`) | The model, headless, against a temp `FurnitureStore`. |
| `src/ui/HtmlCardSlot.{h,cpp}` | `setContentHeight(int)` + `contentHeightChanged()`. |
| `src/web/EditorChrome.{h,cpp}` | `ChromeBridge::reportCardHeight(name, h)` → the named slot. |
| `src/MainWindow.{h,cpp}` | The versions slot and model in `useHtmlChrome()`; `versionsSurface()`; `onSaveVersion()` in HTML mode; `syncVersionsModel()` on `appStateChanged`. |
| `web/src/editor/versions/{api,mock}.ts` (new) | The `versions` object's shape and a mock. |
| `web/src/editor/versions/Versions.svelte` (new) | The card: header, `+`, close ×, pending card, rows, empty state. |
| `web/src/editor/versions/VersionRow.svelte` (new) | One saved version: thumbnail, name, date, actions, the two-click Delete. |
| `web/src/editor/{Editor.svelte,main.ts,api.ts,mock.ts}` | Mount the card; `reportCardHeight`; the `newRequested` listener. |
| `web/test/versions.test.ts` (new) | The card against the mock. |
| `tests/settings_tests.cpp` | `HtmlCardSlot::setContentHeight`. |
| `tests/chrome_smoke.cpp` | The drawer end to end in the real app, and captures per theme. |

---

### Task 1: VersionsModel - the drawer's model, headless

**Files:**
- Create: `src/session/VersionsModel.h`, `src/session/VersionsModel.cpp`, `tests/versions_model.cpp`
- Modify: `CMakeLists.txt` (furnify_session sources; the test target)

**Interfaces:**
- Produces:
  - `VersionsModel(FurnitureStore&, VersionsModel::Hooks, QObject* = nullptr)`;
  - `Hooks{ std::function<bool(const QString&)> save, restore, compare, remove; std::function<void()> close; }`;
  - properties `versions` (QVariantList of `{name, savedText, saved, thumbUrl}`), `canSave`, `rowsEnabled`, `words` (CONSTANT QVariantMap);
  - `setState(const QString& furnitureId, bool canSave, bool rowsEnabled)`, `requestNew()`;
  - slots `refresh()`, `defaultName()`, `save(name)`, `restore(name)`, `compare(name)`, `remove(name)`, `close()`;
  - signals `changed()`, `newRequested()`;
  - `static constexpr int kDeleteConfirmMs = 4000`.

- [ ] **Step 1: Write the failing test** `tests/versions_model.cpp`:

```cpp
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
```

CMake: add `src/session/VersionsModel.cpp` and `src/session/VersionsModel.h` to `furnify_session`, and add beside `hub_model_tests`:

```cmake
    add_executable(versions_model_tests tests/versions_model.cpp)
    target_link_libraries(versions_model_tests PRIVATE furnify_session)
    add_test(NAME versions_model COMMAND versions_model_tests)
```


- [ ] **Step 2: Run it and watch it fail** — create the two source files empty except for the header guard and class declaration with no members (so CMake configures), build `versions_model_tests`.
Expected: compile errors naming `Hooks`, `setState`, `versions` — the class has none of them yet.

- [ ] **Step 3: Implement** `src/session/VersionsModel.h`:

```cpp
#pragma once

// THE VERSIONS DRAWER'S MODEL (migration Phase 3c): what the drawer lists
// for the open furniture, the name a new version is offered, the drawer's
// words, and the four things a row can ask for. The HTML card calls the
// slots below through the web channel; VersionsPanel (the Qt drawer, kept
// until Phase 6) carries its own copy of the same rules, which is the
// specification this file was ported from.
//
// Saving, restoring, comparing and deleting stay MainWindow's: they reach it
// through Hooks, so this class needs no window. A gate is the model's as well
// as the page's - a request arriving while it is shut asks nothing.
//
// QtCore + QtGui only (furnify_session); headless-tested by versions_model_tests.

#include "FurnitureStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class VersionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool rowsEnabled READ rowsEnabled NOTIFY changed)
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    struct Hooks
    {
        std::function<bool(const QString&)> save;
        std::function<bool(const QString&)> restore;
        std::function<bool(const QString&)> compare;
        std::function<bool(const QString&)> remove;
        std::function<void()> close;
    };
    // VersionsPanel::kDeleteConfirmMs - the second click's window.
    static constexpr int kDeleteConfirmMs = 4000;

    VersionsModel(FurnitureStore& store, Hooks hooks, QObject* parent = nullptr);

    // Per version, in the store's order: name, savedText (the Qt drawer's
    // short local date), saved (ms since epoch), thumbUrl (a file URL with
    // the picture's stamp; empty when there is none).
    QVariantList versions() const { return myVersions; }
    bool canSave() const { return myCanSave; }
    bool rowsEnabled() const { return myRowsEnabled; }
    QVariantMap words() const;

    // The open furniture ('' at the library) and the two gates MainWindow
    // derives (canOpenSaveVersion(), and not at the library nor sketching).
    // Re-reads the store; says changed() only when something moved.
    void setState(const QString& furnitureId, bool canSave, bool rowsEnabled);
    // File -> Save version...: the card opens its pending version.
    void requestNew();

public slots:
    void refresh();
    QString defaultName() const;
    bool save(const QString& name);
    bool restore(const QString& name);
    bool compare(const QString& name);
    bool remove(const QString& name);
    void close();

signals:
    void changed();
    void newRequested();

private:
    QVariantList readList() const;

    FurnitureStore& myStore;
    Hooks myHooks;
    QString myFurnitureId;
    bool myCanSave = false;
    bool myRowsEnabled = false;
    QVariantList myVersions;
};
```

`src/session/VersionsModel.cpp`:

```cpp
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
        const QString thumb = myStore.versionThumbPath(myFurnitureId, v.name);
        const QFileInfo info(thumb);
        if (!thumb.isEmpty() && info.exists()) {
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

void VersionsModel::setState(const QString& furnitureId, bool canSave, bool rowsEnabled)
{
    const bool gates = canSave != myCanSave || rowsEnabled != myRowsEnabled || furnitureId != myFurnitureId;
    myFurnitureId = furnitureId;
    myCanSave = canSave;
    myRowsEnabled = rowsEnabled;
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
    if (trimmed.isEmpty() || !myCanSave || !myHooks.save) return false;
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
```

- [ ] **Step 4: Run it and watch it pass** — `cmake --build --preset windows-web --target versions_model_tests` then `build-web/RelWithDebInfo/versions_model_tests.exe`.
Expected: `PASS (0 failures, N checks)`. Also `ctest --test-dir build-web -C RelWithDebInfo -R session_no_widgets` passes (the model includes no widget header).

- [ ] **Step 5: Commit** — `git add src/session/VersionsModel.* tests/versions_model.cpp CMakeLists.txt` then `git commit -m "Versions: the drawer's model, headless (VersionsModel)"`.

---

### Task 2: A slot whose height the page sets

**Files:**
- Modify: `src/ui/HtmlCardSlot.{h,cpp}`, `src/web/EditorChrome.{h,cpp}`, `tests/settings_tests.cpp`

**Interfaces:**
- Produces:
  - `void HtmlCardSlot::setContentHeight(int logicalHeight)`, a no-op when the rounded height is unchanged;
  - `signal HtmlCardSlot::contentHeightChanged()`;
  - `slot ChromeBridge::reportCardHeight(const QString& name, int height)` → `signal ChromeBridge::cardHeightReported(QString, int)`;
  - `EditorChrome` forwards that report to the slot named `name`.

- [ ] **Step 1: Write the failing test** — in `tests/settings_tests.cpp`, beside the existing `HtmlCardSlot` checks:

```cpp
    {
        QWidget host;
        host.resize(800, 600);
        HtmlCardSlot slot(QStringLiteral("versions"), QSize(240, 160), &host);
        struct { int n = 0; int count() const { return n; } } grew, placed;
        QObject::connect(&slot, &HtmlCardSlot::contentHeightChanged, [&] { ++grew.n; });
        QObject::connect(&slot, &HtmlCardSlot::placed, [&] { ++placed.n; });
        slot.setContentHeight(301);
        check(slot.height() == Theme::wholeDevicePixels(301) && slot.width() == Theme::wholeDevicePixels(240),
              QStringLiteral("a page-set height takes whole device pixels and keeps the width (%1)").arg(slot.height()));
        check(grew.count() == 1 && placed.count() >= 1, "and says so twice: to the overlay and to the page");
        slot.setContentHeight(301);
        check(grew.count() == 1, "the same height again says nothing");
        slot.setContentHeight(0);
        check(slot.height() >= 1, "a zero height never collapses the slot to nothing");
    }
```

- [ ] **Step 2: Run it and watch it fail** — build `settings_tests`.
Expected: compile error, `setContentHeight` / `contentHeightChanged` not members.

- [ ] **Step 3: Implement.** `HtmlCardSlot.h` adds:

```cpp
    // The page measured its card (Phase 3c: the Versions drawer grows with
    // its list). Rounded through Theme::wholeDevicePixels(); the width stays.
    void setContentHeight(int logicalHeight);
signals:
    // ... placed() as before ...
    // The height moved: the overlay relays out so whatever stacks below
    // follows.
    void contentHeightChanged();
```

`HtmlCardSlot.cpp`:

```cpp
void HtmlCardSlot::setContentHeight(int logicalHeight)
{
    const int h = Theme::wholeDevicePixels(std::max(1, logicalHeight));
    if (h == height()) return;
    setFixedHeight(h);
    emit contentHeightChanged();
}
```

`EditorChrome.h`: in `ChromeBridge` `public slots:` add `void reportCardHeight(const QString& name, int height);` and in `signals:` (C++-side) `void cardHeightReported(const QString& name, int height);`. `EditorChrome.cpp`:

```cpp
void ChromeBridge::reportCardHeight(const QString& name, int height)
{
    emit cardHeightReported(name, height);
}
```

and in the `EditorChrome` constructor:

```cpp
    // A card that sizes itself (the Versions drawer) tells us its height;
    // its slot takes it, and the overlay re-stacks around it (Phase 3c).
    connect(myBridge, &ChromeBridge::cardHeightReported, this, [this](const QString& name, int height) {
        for (const QPointer<HtmlCardSlot>& slot : myCards)
            if (slot && slot->name() == name) slot->setContentHeight(height);
    });
```

- [ ] **Step 4: Run and pass** — `settings_tests.exe`.
Expected: PASS with the four new checks.

- [ ] **Step 5: Commit** — `git commit -am "HtmlCardSlot: a height the page sets (Phase 3c)"`.

---

### Task 3: MainWindow - the versions slot, the model and the HTML route

**Files:**
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`

**Interfaces:**
- Consumes: `VersionsModel` (Task 1); `HtmlCardSlot::setContentHeight` and `contentHeightChanged` (Task 2).
- Produces:
  - the page's `versions` channel object;
  - `QWidget* MainWindow::versionsSurface() const`;
  - `VersionsModel* MainWindow::versionsModel() const` (for chrome_smoke);
  - a slot named `htmlCardSlot_versions`.

- [ ] **Step 1: Write the failing test** — at the end of the Settings section in `tests/chrome_smoke.cpp` (before the theme captures), add the first block of Task 5's section:

```cpp
    // --- the Versions card's place (Phase 3c) ------------------------------------
    QAction* versionsAction = actionNamed(&window, QStringLiteral("Versions"));
    check(versionsAction != nullptr, "the editor has Versions");
    if (versionsAction && !versionsAction->isChecked()) versionsAction->trigger();
    pump(400);
    QWidget* versionsSlot = window.findChild<QWidget*>(QStringLiteral("htmlCardSlot_versions"));
    check(versionsSlot && versionsSlot->isVisible(), "Versions shows the card's slot");
    check(!window.findChild<VersionsPanel*>()->isVisible(), "and the Qt drawer stays hidden");
    check(window.versionsModel() && window.versionsModel()->canSave(),
          "the page's versions object is there, with + open");
```

(Add `#include "ui/VersionsPanel.h"` and `#include "session/VersionsModel.h"` at the top.)

- [ ] **Step 2: Run it and watch it fail** — build `chrome_smoke`.
Expected: compile error, `versionsModel` is not a member of `MainWindow`.

- [ ] **Step 3: Implement.** In `MainWindow.h` (public, beside `htmlChrome()`):

```cpp
    // The Versions drawer's model on the chrome page (Phase 3c); null until
    // useHtmlChrome().
    VersionsModel* versionsModel() const { return myVersionsModel; }
```

private: `QWidget* versionsSurface() const;`, `void syncVersionsModel();`, `HtmlCardSlot* myVersionsSlot = nullptr;`, `VersionsModel* myVersionsModel = nullptr;` and a forward declaration `class VersionsModel;`. **Read up to the nearest access specifier before placing each member** (CLAUDE.md's moc trap).

In `useHtmlChrome()`, after the Settings slot block:

```cpp
    // THE VERSIONS CARD (Phase 3c): the Settings card's shape, with one
    // difference - its height is the page's, because the list grows.
    if (myOverlay && myVersionsPanel) {
        myVersionsSlot = new HtmlCardSlot(QStringLiteral("versions"),
                                          QSize(VersionsPanel::cardWidth(), 160), myView);
        myVersionsSlot->hide();
        myHtmlChrome->addCard(myVersionsSlot);
        connect(myVersionsSlot, &HtmlCardSlot::contentHeightChanged, this, [this] {
            if (myOverlay) myOverlay->relayout();
        });
        // The width follows the type scale, as the Qt drawer's does.
        connect(Theme::notifier(), &Theme::Notifier::changed, myVersionsSlot, [this] {
            myVersionsSlot->setFixedWidth(Theme::wholeDevicePixels(VersionsPanel::cardWidth()));
        });
    }
    VersionsModel::Hooks hooks;
    hooks.save = [this](const QString& name) { return saveVersion(name); };
    hooks.restore = [this](const QString& name) { return restoreVersion(name); };
    hooks.compare = [this](const QString& name) { return openCompare(name); };
    hooks.remove = [this](const QString& name) { return deleteVersionByName(name); };
    hooks.close = [this] { if (myVersionsPanelAction) myVersionsPanelAction->setChecked(false); };
    myVersionsModel = new VersionsModel(myStore, hooks, myHtmlChrome);
    myHtmlChrome->registerObject(QStringLiteral("versions"), myVersionsModel);
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::syncVersionsModel);
    syncVersionsModel();
```

(The `registerObject` call must come before `myHtmlChrome->start()` — the existing call stays last.)

In the `liveChanged` lambda, beside the Settings swap:

```cpp
        if (myOverlay && myVersionsSlot && myVersionsPanel) {
            if (live) myOverlay->replaceWidget(myVersionsPanel, myVersionsSlot);
            else myOverlay->replaceWidget(myVersionsSlot, myVersionsPanel);
        }
```

New functions:

```cpp
QWidget* MainWindow::versionsSurface() const
{
    // settingsSurface()'s rule: the page's card only while the page can draw it.
    if (myVersionsSlot && htmlChromeLive()) return myVersionsSlot;
    return myVersionsPanel;
}

void MainWindow::syncVersionsModel()
{
    if (!myVersionsModel) return;
    myVersionsModel->setState(myShowingInitScreen ? QString() : myFurnitureId, canOpenSaveVersion(),
                              !myShowingInitScreen && !mySketching);
}
```

Replace `myVersionsPanel->setVisible(...)` in the render-mode block (`if (myVersionsPanel) myVersionsPanel->setVisible(myVersionsPanelAction->isChecked() && !hiddenForRenderMode);`) and in the `myVersionsPanelAction` toggled lambda with `versionsSurface()`. In `onSaveVersion()`:

```cpp
    if (htmlChromeLive() && myVersionsModel) myVersionsModel->requestNew();
    else if (myVersionsPanel) myVersionsPanel->beginNewVersion();
```

- [ ] **Step 4: Run and pass** — build, `chrome_smoke.exe <out>`.
Expected: the three new checks pass and every earlier one stays green. The card is not drawn yet, so the drawer is an empty slot.

- [ ] **Step 5: Commit** — `git commit -am "MainWindow: the Versions card's slot and model in HTML-chrome mode"`.

---

### Task 4: The Versions card in the page

**Files:**
- Create: `web/src/editor/versions/api.ts`, `web/src/editor/versions/mock.ts`, `web/src/editor/versions/Versions.svelte`, `web/src/editor/versions/VersionRow.svelte`, `web/test/versions.test.ts`
- Modify: `web/src/editor/Editor.svelte`, `web/src/editor/main.ts`, `web/src/editor/api.ts`, `web/src/editor/mock.ts`

**Interfaces:**
- Consumes: the `versions` channel object (Task 3); `chrome.reportCardHeight` (Task 2); `chrome.claimKeyboard` / `releaseKeyboard`.
- Produces:
  - the DOM the end-to-end test drives:
    - `.versions-card`;
    - `.versions-card .add`;
    - `.versions-card .close`;
    - `.pending input`;
    - `.version[data-version="<name>"]`, holding `.compare`, `.restore`, `.delete` and `img.thumb`;
    - `.versions-card .empty`;
  - `window.__versionsHeight`, the last height reported.

- [ ] **Step 1: Write `api.ts` and `mock.ts`, then the failing tests.**

`api.ts`:

```ts
// What the Versions card talks to: VersionsModel, the editor chrome page's
// `versions` object (migration Phase 3c). `versions`, `canSave` and
// `rowsEnabled` are re-read on `changed`; `words` is sent once.
import type { Signal } from '../api';

export interface VersionEntry {
  name: string;
  savedText: string;
  saved: number;
  thumbUrl: string; // '' when the version has no picture
}

export interface VersionsWords {
  title: string;
  add: string;
  empty: string;
  compare: string;
  restore: string;
  delete: string;
  deleteArmed: string;
  deleteMs: number;
  close: string;
}

export interface VersionsApi {
  versions: VersionEntry[];
  canSave: boolean;
  rowsEnabled: boolean;
  words: VersionsWords;
  changed: Signal;
  newRequested: Signal;
  defaultName(): Promise<string>;
  save(name: string): Promise<boolean>;
  restore(name: string): unknown;
  compare(name: string): unknown;
  remove(name: string): unknown;
  close(): void;
}
```

`mock.ts`:

```ts
// A VersionsModel stand-in for the page tests and the dev server. Its words
// are the C++ strings verbatim (VersionsModel.cpp).
import { MockSignal, type Call } from '../../hub/mock';
import type { VersionEntry, VersionsApi } from './api';

export function mockVersions(list: VersionEntry[] = []) {
  const calls: Call[] = [];
  const api: VersionsApi & { calls: Call[]; changed: MockSignal; newRequested: MockSignal; saveAnswer: boolean } = {
    calls,
    saveAnswer: true,
    versions: list,
    canSave: true,
    rowsEnabled: true,
    words: {
      title: 'Versions',
      add: 'Save a new version',
      empty: 'No versions yet.\n\nPress + to keep a named snapshot you can come back to.',
      compare: 'Compare',
      restore: 'Restore',
      delete: 'Delete',
      deleteArmed: 'Delete — click again',
      deleteMs: 4000,
      close: 'Close this panel',
    },
    changed: new MockSignal(),
    newRequested: new MockSignal(),
    defaultName: async () => {
      calls.push({ method: 'defaultName', args: [] });
      return `Version ${api.versions.length + 1}`;
    },
    save: async (name: string) => {
      calls.push({ method: 'save', args: [name] });
      return api.saveAnswer;
    },
    restore: (name: string) => calls.push({ method: 'restore', args: [name] }),
    compare: (name: string) => calls.push({ method: 'compare', args: [name] }),
    remove: (name: string) => calls.push({ method: 'remove', args: [name] }),
    close: () => {
      calls.push({ method: 'close', args: [] });
    },
  };
  return api;
}

export const sampleVersions: VersionEntry[] = [
  { name: 'Version 1', savedText: '10/11/26, 9:12 AM', saved: 1, thumbUrl: '' },
  { name: 'Shelf added', savedText: '10/11/26, 11:40 AM', saved: 2, thumbUrl: '' },
];
```

`web/test/versions.test.ts` (the shape of `settings.test.ts`: mount with `@testing-library/svelte`'s `render`, flush with `tick`, `vi.useFakeTimers()` for the delete window):

```ts
import { render, fireEvent } from '@testing-library/svelte';
import { tick } from 'svelte';
import { afterEach, describe, expect, it, vi } from 'vitest';
import Versions from '../src/editor/versions/Versions.svelte';
import { mockVersions, sampleVersions } from '../src/editor/versions/mock';
import { usesBannedWord } from './banned';

const rect = { x: 60, y: 80, w: 240, h: 300, shown: true };
function keyboard() {
  const calls: string[] = [];
  return { calls, claimKeyboard: () => calls.push('claim'), releaseKeyboard: () => calls.push('release') };
}
const heights: number[] = [];
const report = (_name: string, h: number) => heights.push(h);

async function settle() {
  for (let i = 0; i < 4; i++) await tick();
  await Promise.resolve();
  await tick();
}

afterEach(() => vi.useRealTimers());

describe('the Versions card', () => {
  it('lists every version with its date, in the order given', () => {
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    const names = [...container.querySelectorAll('.version .name')].map((n) => n.textContent);
    expect(names).toEqual(['Version 1', 'Shelf added']);
    expect(container.querySelector('.version[data-version="Version 1"] .date')!.textContent).toBe('10/11/26, 9:12 AM');
  });

  it('shows the empty state when there is nothing', () => {
    const { container } = render(Versions, { versions: mockVersions([]), keyboard: keyboard(), rect, report, newRequest: 0 });
    expect(container.querySelector('.empty')!.textContent).toContain('No versions yet.');
  });

  it('a picture shows as an image, none as no image', () => {
    const api = mockVersions([{ ...sampleVersions[0], thumbUrl: 'file:///t.png?v=1' }, sampleVersions[1]]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    expect(container.querySelector('.version[data-version="Version 1"] img.thumb')!.getAttribute('src')).toBe('file:///t.png?v=1');
    expect(container.querySelector('.version[data-version="Shelf added"] img.thumb')).toBeNull();
  });

  it('Compare and Restore ask once, by name', async () => {
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.version[data-version="Shelf added"] .compare')!);
    await fireEvent.click(container.querySelector('.version[data-version="Version 1"] .restore')!);
    expect(api.calls.filter((c) => c.method !== 'defaultName')).toEqual([
      { method: 'compare', args: ['Shelf added'] },
      { method: 'restore', args: ['Version 1'] },
    ]);
  });

  it('Delete takes two clicks inside its window, and the arm drops after it', async () => {
    vi.useFakeTimers();
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    const del = () => container.querySelector<HTMLButtonElement>('.version[data-version="Version 1"] .delete')!;
    await fireEvent.click(del());
    expect(del().textContent).toBe('Delete — click again');
    expect(api.calls.some((c) => c.method === 'remove')).toBe(false);
    vi.advanceTimersByTime(4001);
    await tick();
    expect(del().textContent).toBe('Delete');
    await fireEvent.click(del());
    await fireEvent.click(del());
    expect(api.calls.filter((c) => c.method === 'remove')).toEqual([{ method: 'remove', args: ['Version 1'] }]);
  });

  it('an armed Delete survives a refresh that keeps its row (Review Focus 3)', async () => {
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.version[data-version="Version 1"] .delete')!);
    api.versions = [...sampleVersions, { name: 'Late', savedText: 'x', saved: 3, thumbUrl: '' }];
    api.changed.emit();
    await settle();
    expect(container.querySelector('.version[data-version="Version 1"] .delete')!.textContent).toBe('Delete — click again');
    expect(container.querySelector('.version[data-version="Late"] .delete')!.textContent).toBe('Delete');
  });

  it('+ opens one pending version, seeded and selected, holding the keyboard', async () => {
    const kb = keyboard();
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: kb, rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    const field = container.querySelector<HTMLInputElement>('.pending input')!;
    expect(field.value).toBe('Version 3');
    expect(document.activeElement).toBe(field);
    expect(kb.calls).toEqual(['claim']);
    expect(container.querySelector<HTMLButtonElement>('.add')!.disabled).toBe(true);
  });

  it('Enter saves the typed name; a refusal keeps the card with it (Review Focus 2)', async () => {
    const kb = keyboard();
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: kb, rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    const field = container.querySelector<HTMLInputElement>('.pending input')!;
    api.saveAnswer = false;
    await fireEvent.input(field, { target: { value: 'Shelf added' } });
    await fireEvent.keyDown(field, { key: 'Enter' });
    await settle();
    expect(api.calls.filter((c) => c.method === 'save')).toEqual([{ method: 'save', args: ['Shelf added'] }]);
    expect(container.querySelector<HTMLInputElement>('.pending input')!.value).toBe('Shelf added');
    api.saveAnswer = true;
    await fireEvent.input(container.querySelector('.pending input')!, { target: { value: 'Shelf moved' } });
    await fireEvent.keyDown(container.querySelector('.pending input')!, { key: 'Enter' });
    await settle();
    expect(container.querySelector('.pending')).toBeNull();
    expect(kb.calls.at(-1)).toBe('release');
  });

  it('Escape discards, saving nothing; a blank name discards too', async () => {
    const kb = keyboard();
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: kb, rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    await fireEvent.keyDown(container.querySelector('.pending input')!, { key: 'Escape' });
    await settle();
    expect(container.querySelector('.pending')).toBeNull();
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    await fireEvent.input(container.querySelector('.pending input')!, { target: { value: '   ' } });
    await fireEvent.keyDown(container.querySelector('.pending input')!, { key: 'Enter' });
    await settle();
    expect(container.querySelector('.pending')).toBeNull();
    expect(api.calls.some((c) => c.method === 'save')).toBe(false);
    expect(kb.calls).toEqual(['claim', 'release', 'claim', 'release']);
  });

  it('focus leaving the field saves, as the Qt drawer does (ruling R5)', async () => {
    const api = mockVersions([]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    await fireEvent.blur(container.querySelector('.pending input')!);
    await settle();
    expect(api.calls.filter((c) => c.method === 'save')).toEqual([{ method: 'save', args: ['Version 1'] }]);
  });

  it('+ shutting discards an open pending card and saves nothing', async () => {
    const kb = keyboard();
    const api = mockVersions([]);
    const { container } = render(Versions, { versions: api, keyboard: kb, rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    api.canSave = false;
    api.changed.emit();
    await settle();
    expect(container.querySelector('.pending')).toBeNull();
    expect(api.calls.some((c) => c.method === 'save')).toBe(false);
    expect(kb.calls).toEqual(['claim', 'release']);
    expect(container.querySelector<HTMLButtonElement>('.add')!.disabled).toBe(true);
  });

  it('unmounting mid-edit gives the keyboard back and saves nothing (Review Focus 4)', async () => {
    const kb = keyboard();
    const api = mockVersions([]);
    const { container, unmount } = render(Versions, { versions: api, keyboard: kb, rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.add')!);
    await settle();
    unmount();
    await settle();
    expect(kb.calls).toEqual(['claim', 'release']);
    expect(api.calls.some((c) => c.method === 'save')).toBe(false);
  });

  it('a request from the menu opens the pending card on mount', async () => {
    const api = mockVersions([]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 1 });
    await settle();
    expect(container.querySelector<HTMLInputElement>('.pending input')!.value).toBe('Version 1');
  });

  it('rows shut while sketching: Compare, Restore and Delete are disabled', async () => {
    const api = mockVersions([...sampleVersions]);
    api.rowsEnabled = false;
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    for (const b of container.querySelectorAll<HTMLButtonElement>('.version button')) expect(b.disabled).toBe(true);
  });

  it('the close x asks the model, and every painted word is clean', async () => {
    const api = mockVersions([...sampleVersions]);
    const { container } = render(Versions, { versions: api, keyboard: keyboard(), rect, report, newRequest: 0 });
    await fireEvent.click(container.querySelector('.close')!);
    expect(api.calls.some((c) => c.method === 'close')).toBe(true);
    for (const w of Object.values(api.words)) if (typeof w === 'string') expect(usesBannedWord(w)).toBe(false);
  });
});
```

- [ ] **Step 2: Run them and watch them fail** — `cd web; npx vitest run test/versions.test.ts`.
Expected: FAIL, cannot resolve `../src/editor/versions/Versions.svelte`.

- [ ] **Step 3: Implement the card.**

`VersionRow.svelte`:

```svelte
<script lang="ts">
  // One saved version (VersionsPanel::buildRealRow, ported): the picture at
  // 10:16, the name over its date, and an actions row that is always there
  // at a fixed height - its buttons appear on hover, so revealing them never
  // moves the rows below. Delete takes two clicks inside its window.
  import { onDestroy } from 'svelte';
  import { chip } from '../../actions';
  import type { VersionEntry, VersionsWords } from './api';

  let {
    entry,
    words,
    enabled,
    oncompare,
    onrestore,
    ondelete,
  }: {
    entry: VersionEntry;
    words: VersionsWords;
    enabled: boolean;
    oncompare: () => void;
    onrestore: () => void;
    ondelete: () => void;
  } = $props();

  let armed = $state(false);
  let timer = 0;
  function disarm() {
    armed = false;
    clearTimeout(timer);
  }
  function onDelete() {
    if (armed) {
      disarm();
      ondelete();
      return;
    }
    armed = true;
    clearTimeout(timer);
    timer = window.setTimeout(disarm, words.deleteMs);
  }
  onDestroy(() => clearTimeout(timer));
</script>

<div class="version" class:armed data-version={entry.name}>
  {#if entry.thumbUrl}
    <img class="thumb" src={entry.thumbUrl} alt="" draggable="false" />
  {:else}
    <div class="thumb blank"></div>
  {/if}
  <div class="bar">
    <span class="name">{entry.name}</span>
    <span class="date">{entry.savedText}</span>
  </div>
  <div class="actions">
    <button use:chip class="compare" disabled={!enabled} onclick={oncompare}>{words.compare}</button>
    <button use:chip class="restore" disabled={!enabled} onclick={onrestore}>{words.restore}</button>
    <button use:chip class="delete" disabled={!enabled} onclick={onDelete}
      >{armed ? words.deleteArmed : words.delete}</button
    >
  </div>
</div>

<style>
  .version {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    padding: 3px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--strip-fill, var(--chip));
  }
  .thumb {
    display: block;
    width: 100%;
    aspect-ratio: 16 / 10;
    object-fit: cover;
    border-radius: calc(var(--r-small, 6px) - 3px);
  }
  .thumb.blank {
    background: transparent;
  }
  .bar {
    display: flex;
    flex-direction: column;
    padding: 8px 10px;
    min-width: 0;
  }
  .name {
    color: var(--text);
    font-weight: 500;
    font-size: var(--font-body);
    overflow-wrap: anywhere;
  }
  .date {
    color: var(--text-muted);
    font-size: var(--font-label);
  }
  .actions {
    display: flex;
    justify-content: flex-end;
    gap: 4px;
    height: 34px;
    box-sizing: border-box;
    padding: 6px 10px;
  }
  .actions button {
    visibility: hidden;
    height: 22px;
    padding: 2px 8px;
    border: 1px solid var(--border);
    border-radius: var(--r-tiny, 4px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
  }
  .version:hover .actions button,
  .version.armed .actions button {
    visibility: visible;
  }
  .actions button:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .actions button:disabled {
    color: var(--text-disabled);
    border-color: var(--text-disabled);
    cursor: default;
  }
  .version.armed .delete {
    color: var(--danger-text);
    border-color: var(--danger-text);
  }
</style>
```

(Check the token variable names against `web/src/tokens.css` before writing — use the names it defines for `textMuted`, `chipHover`, `textDisabled` and `dangerText`. If one is missing, map it in `ThemeBridge`'s token list rather than inventing a hex.)

`Versions.svelte`:

```svelte
<script lang="ts">
  // THE VERSIONS DRAWER as a card on the editor's chrome page (migration
  // Phase 3c) - VersionsPanel, ported. Where it stands is the overlay's call
  // (an HtmlCardSlot in the Qt drawer's TopLeft anchor); how TALL it is, is
  // ours: the list grows, so the card measures itself and reports the height,
  // and the slot follows (ruling R2). The card is drawn at the slot's rect;
  // the measured wrapper inside does not depend on that rect, so the two
  // cannot feed back.
  //
  // A version is file data (CLAUDE.md "Files, versions and the library"): +
  // opens ONE pending card, named nothing in the store until it is saved;
  // Enter or a click away saves (InlineRename's contract), Escape or a blank
  // name discards, and a refused save keeps the card with what was typed.
  import { onDestroy, onMount, tick, untrack } from 'svelte';
  import { chip } from '../../actions';
  import type { CardRect, Keyboard } from '../settings/api';
  import type { VersionEntry, VersionsApi } from './api';
  import VersionRow from './VersionRow.svelte';

  let {
    versions,
    keyboard,
    rect,
    report,
    newRequest,
  }: {
    versions: VersionsApi;
    keyboard: Keyboard;
    rect: CardRect;
    report: (name: string, height: number) => void;
    newRequest: number;
  } = $props();

  const api: VersionsApi = untrack(() => versions);
  const words = api.words;

  let list: VersionEntry[] = $state([...api.versions]);
  let canSave = $state(api.canSave);
  let rowsEnabled = $state(api.rowsEnabled);
  let pending = $state(false);
  let draft = $state('');
  let field: HTMLInputElement | undefined = $state();
  let body: HTMLElement | undefined = $state();
  let holding = false;
  let saving = false;
  let alive = true;
  let seenRequest = untrack(() => newRequest);

  api.changed.connect(() => {
    if (!alive) return;
    list = [...api.versions];
    canSave = api.canSave;
    rowsEnabled = api.rowsEnabled;
    // VersionsPanel::refresh()'s auto-cancel: a competing claim (a sketch, a
    // pull, render mode) shuts +, and a pending card shuts with it.
    if (!canSave && pending) discard();
  });

  function claim() {
    if (holding) return;
    holding = true;
    keyboard.claimKeyboard();
  }
  function release() {
    if (!holding) return;
    holding = false;
    keyboard.releaseKeyboard();
  }

  async function begin() {
    if (pending || !canSave) return;
    pending = true;
    draft = await api.defaultName();
    if (!alive || !pending) return;
    await tick();
    claim();
    field?.focus();
    field?.select();
  }

  function discard() {
    pending = false;
    draft = '';
    release();
  }

  async function commit() {
    if (!pending || saving) return;
    const name = draft.trim();
    if (!name) {
      discard();
      return;
    }
    saving = true;
    const ok = await api.save(name);
    saving = false;
    if (!alive) return;
    if (ok) {
      discard();
      return;
    }
    // Refused (a duplicate): MainWindow already said why. The card stays,
    // holding what was typed, for another try.
    await tick();
    field?.focus();
    field?.select();
  }

  function onKey(event: KeyboardEvent) {
    if (event.key === 'Enter') {
      event.preventDefault();
      commit();
    } else if (event.key === 'Escape') {
      event.preventDefault();
      discard();
    }
  }

  function onBlur() {
    // A click away saves, as InlineRename does (ruling R5) - unless the save
    // just refused and focus is coming back to the field.
    if (pending && !saving) commit();
  }

  // A menu request (File -> Save version...), kept by Editor.svelte so one
  // that arrives before this card mounts is not lost (ruling R4).
  $effect(() => {
    if (newRequest !== seenRequest) {
      seenRequest = newRequest;
      untrack(() => begin());
    }
  });
  onMount(() => {
    if (newRequest > 0) begin();
    const measure = new ResizeObserver(() => {
      if (!body) return;
      const h = Math.ceil(body.getBoundingClientRect().height);
      (window as unknown as { __versionsHeight?: number }).__versionsHeight = h;
      report('versions', h);
    });
    if (body) measure.observe(body);
    return () => measure.disconnect();
  });
  onDestroy(() => {
    alive = false;
    pending = false;
    release();
  });
</script>

<section
  class="versions-card themed-rim"
  data-hit
  aria-label={words.title}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
>
  <div class="body" bind:this={body}>
    <header>
      <span class="title">{words.title}</span>
      <button use:chip class="add" title={words.add} aria-label={words.add} disabled={pending || !canSave}
        onclick={begin}>+</button
      >
      <button use:chip class="close" title={words.close} aria-label={words.close} onclick={() => api.close()}
        >×</button
      >
    </header>
    <div class="list">
      {#if pending}
        <div class="version pending">
          <div class="thumb blank"></div>
          <input
            bind:this={field}
            bind:value={draft}
            aria-label={words.add}
            spellcheck="false"
            onkeydown={onKey}
            onblur={onBlur}
          />
        </div>
      {/if}
      {#each list as entry (entry.name)}
        <VersionRow
          {entry}
          {words}
          enabled={rowsEnabled}
          oncompare={() => api.compare(entry.name)}
          onrestore={() => api.restore(entry.name)}
          ondelete={() => api.remove(entry.name)}
        />
      {/each}
      {#if list.length === 0 && !pending}
        <p class="empty">{words.empty}</p>
      {/if}
    </div>
  </div>
</section>

<style>
  .versions-card {
    position: fixed;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-body);
    overflow: hidden;
  }
  .body {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 12px;
    min-height: 160px;
  }
  header {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .title {
    flex: 1;
    color: var(--text-muted);
    font-family: var(--title-family, inherit);
    font-size: var(--title-size, var(--font-title));
    font-weight: var(--title-weight, 600);
    letter-spacing: var(--title-tracking, normal);
  }
  .add {
    width: 26px;
    height: 26px;
    border: 1.5px solid var(--accent);
    border-radius: var(--r-control, 6px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }
  .add:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .add:disabled {
    color: var(--text-disabled);
    border-color: var(--text-disabled);
    cursor: default;
  }
  .close {
    width: 20px;
    height: 20px;
    border: none;
    background: transparent;
    color: var(--text-muted);
    font: inherit;
    cursor: pointer;
  }
  .list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    max-height: 384px;
    overflow-y: auto;
  }
  .pending {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    padding: 3px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--strip-fill, var(--chip));
  }
  .pending .thumb {
    aspect-ratio: 16 / 10;
  }
  .pending input {
    margin: 8px 10px;
    padding: 2px 6px;
    border: 1px solid var(--accent);
    border-radius: var(--r-tiny, 4px);
    background: var(--panel);
    color: var(--text);
    font: inherit;
    font-weight: 500;
    outline: none;
  }
  .empty {
    margin: 0;
    color: var(--text-muted);
    white-space: pre-line;
  }
</style>
```

Wire it into `Editor.svelte`:
- the props gain `versions?: VersionsApi`;
- a `newRequest` counter is held at the top level, incremented on `versions.newRequested`;
- mount `{#if versions && cards.versions?.shown}<Versions {versions} keyboard={chrome} rect={cards.versions} report={(n, h) => chrome.reportCardHeight(n, h)} {newRequest} />{/if}` before the Settings card.

`api.ts` (`ChromeApi`) gains `reportCardHeight(name: string, height: number): void`. `mock.ts` records it. `main.ts` reads `objects.versions`; the dev server uses `mockVersions(sampleVersions)` and sets `mock.cards.versions = { x: 60, y: 120, w: 240, h: 400, shown: true }`.

- [ ] **Step 4: Run and pass**:
  - `npx vitest run` (every page test);
  - `npx svelte-check`;
  - `npm run build`.

Expected: all vitest files PASS (97 before plus the new ones), svelte-check reports 0 errors and 0 warnings, and the build writes `dist/editor/index.html`.

- [ ] **Step 5: Commit**:
  - `git add web/src/editor/versions web/test/versions.test.ts web/src/editor/Editor.svelte web/src/editor/main.ts web/src/editor/api.ts web/src/editor/mock.ts`;
  - `git commit -m "Page: the Versions card (Phase 3c)"`.

---

### Task 5: The drawer end to end in the real app

**Files:**
- Modify: `tests/chrome_smoke.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: the end-to-end evidence and the captures `chrome-versions-<theme>.png`.

- [ ] **Step 1: Extend the Task 3 block into the whole drawer** — after the three checks:

```cpp
    {
        QString furnitureId = window.currentFurnitureId();
        FurnitureStore& store = window.furnitureStore();
        waitFor([&] { return js(page, "!!document.querySelector('.versions-card')").toBool(); }, 3000);
        auto drawn = [&] {
            return QRect(js(page, "Math.round(document.querySelector('.versions-card').getBoundingClientRect().left)").toInt(),
                         js(page, "Math.round(document.querySelector('.versions-card').getBoundingClientRect().top)").toInt(),
                         js(page, "Math.round(document.querySelector('.versions-card').getBoundingClientRect().width)").toInt(),
                         js(page, "Math.round(document.querySelector('.versions-card').getBoundingClientRect().height)").toInt());
        };
        waitFor([&] { return versionsSlot && drawn() == versionsSlot->geometry(); }, 2000);
        check(versionsSlot && drawn() == versionsSlot->geometry(),
              QStringLiteral("the page draws the card exactly over its slot (%1x%2)").arg(drawn().width()).arg(drawn().height()));
        check(jsString(page, "document.querySelector('.versions-card .empty').textContent").startsWith("No versions yet."),
              "a furniture with no versions shows the empty state");
        const int emptyHeight = versionsSlot->height();

        // + opens a pending version, and typing in it keeps every key.
        js(page, "document.querySelector('.versions-card .add').click()");
        waitFor([&] { return chrome->holdsKeyboard(); }, 2000);
        check(jsString(page, "document.querySelector('.pending input').value") == QStringLiteral("Version 1"),
              "+ opens a pending version named Version 1");
        check(chrome->holdsKeyboard(), "and the field holds the keyboard");
        const int bodies = static_cast<int>(window.document().solids().size());
        for (int key : {int(Qt::Key_Delete), int(Qt::Key_Z), int(Qt::Key_1), int(Qt::Key_Space)}) {
            QKeyEvent over(QEvent::ShortcutOverride, key, key == Qt::Key_Z ? Qt::ControlModifier : Qt::NoModifier);
            QCoreApplication::sendEvent(chrome->view()->focusProxy(), &over);
            check(over.isAccepted(), QStringLiteral("key %1 in the name is the field's").arg(key));
        }
        check(static_cast<int>(window.document().solids().size()) == bodies, "and no body was deleted");

        // Enter saves.
        js(page, "(() => { const f = document.querySelector('.pending input'); f.value = 'Shelf added'; "
                 "f.dispatchEvent(new Event('input', {bubbles: true})); "
                 "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()");
        waitFor([&] { return !store.versions(furnitureId).isEmpty(); }, 3000);
        check(store.versions(furnitureId).size() == 1 && store.versions(furnitureId).front().name == QStringLiteral("Shelf added"),
              "Enter saves the version under the typed name");
        waitFor([&] { return toastText(page).contains(QStringLiteral("saved")); }, 2000);
        check(toastText(page) == QStringLiteral("Version \"Shelf added\" saved"), "and the page says so - \"" + toastText(page) + "\"");
        waitFor([&] { return !chrome->holdsKeyboard(); }, 2000);
        check(!chrome->holdsKeyboard(), "and gives the keyboard back");
        waitFor([&] { return js(page, "!!document.querySelector('.version[data-version=\"Shelf added\"]')").toBool(); }, 3000);
        waitFor([&] { return versionsSlot->height() > emptyHeight; }, 2000);
        check(versionsSlot->height() > emptyHeight && drawn() == versionsSlot->geometry(),
              QStringLiteral("the slot grows with the list and the card still covers it exactly (%1 -> %2) (Review Focus 5)")
                  .arg(emptyHeight).arg(versionsSlot->height()));
        check(js(page, "(() => { const i = document.querySelector('.version[data-version=\"Shelf added\"] img.thumb'); "
                       "return i ? i.complete && i.naturalWidth > 0 : false; })()").toBool(),
              "the saved version's picture loads");

        // A duplicate refuses, keeping the card (Review Focus 2).
        js(page, "document.querySelector('.versions-card .add').click()");
        waitFor([&] { return js(page, "!!document.querySelector('.pending input')").toBool(); }, 2000);
        js(page, "(() => { const f = document.querySelector('.pending input'); f.value = 'Shelf added'; "
                 "f.dispatchEvent(new Event('input', {bubbles: true})); "
                 "f.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true})); })()");
        waitFor([&] { return toastText(page).startsWith(QStringLiteral("Couldn't save version")); }, 3000);
        check(toastText(page).startsWith(QStringLiteral("Couldn't save version")), "a duplicate name is refused, saying why");
        check(js(page, "!!document.querySelector('.pending input')").toBool() &&
                  jsString(page, "document.querySelector('.pending input').value") == QStringLiteral("Shelf added"),
              "and the pending card stays, holding what was typed");
        js(page, "document.querySelector('.pending input').dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))");
        waitFor([&] { return !js(page, "!!document.querySelector('.pending')").toBool(); }, 2000);
        check(store.versions(furnitureId).size() == 1 && !chrome->holdsKeyboard(),
              "Escape discards it, saving nothing and giving the keyboard back");

        // File -> Save version... opens the page's pending card.
        if (QAction* saveVersion = actionNamed(&window, QStringLiteral("Save version...")))
            saveVersion->trigger();
        waitFor([&] { return js(page, "!!document.querySelector('.pending input')").toBool(); }, 2000);
        check(jsString(page, "(document.querySelector('.pending input') || {}).value || ''") == QStringLiteral("Version 1"),
              "File -> Save version... opens the same pending card on the page");
        versionsAction->trigger();   // hidden mid-edit (Review Focus 4)
        pump(300);
        check(!versionsSlot->isVisible() && !chrome->holdsKeyboard() && store.versions(furnitureId).size() == 1,
              "hiding the drawer mid-edit gives the keyboard back and saves nothing");
        versionsAction->trigger();
        waitFor([&] { return js(page, "!!document.querySelector('.version[data-version=\"Shelf added\"]')").toBool(); }, 3000);

        // Restore: one checkpoint, a Note with Undo.
        window.view()->setSelectedSolids({window.document().solids().front().id});
        actionNamed(&window, QStringLiteral("Delete Selected"))->trigger();
        pump(200);
        const int afterDelete = static_cast<int>(window.document().solids().size());
        js(page, "document.querySelector('.version[data-version=\"Shelf added\"] .restore').click()");
        waitFor([&] { return static_cast<int>(window.document().solids().size()) == afterDelete + 1; }, 3000);
        check(static_cast<int>(window.document().solids().size()) == afterDelete + 1,
              "Restore puts the version's bodies back");
        waitFor([&] { return toastText(page).startsWith(QStringLiteral("Restored")); }, 2000);
        check(toastText(page).startsWith(QStringLiteral("Restored")) &&
                  jsString(page, "(document.querySelector('.toast .undo') || {}).textContent || ''") == QStringLiteral("Undo"),
              "and its Note offers Undo - \"" + toastText(page) + "\"");

        // Compare opens the second viewport.
        js(page, "document.querySelector('.version[data-version=\"Shelf added\"] .compare').click()");
        waitFor([&] { return window.isCompareOpen(); }, 3000);
        check(window.isCompareOpen(), "Compare opens the side-by-side");
        window.closeCompare();
        pump(300);

        // Delete: the first click arms, the second deletes.
        js(page, "document.querySelector('.version[data-version=\"Shelf added\"] .delete').click()");
        pump(200);
        check(store.versions(furnitureId).size() == 1 &&
                  jsString(page, "document.querySelector('.version[data-version=\"Shelf added\"] .delete').textContent") ==
                      QStringLiteral("Delete — click again"),
              "one click on Delete arms it and deletes nothing");
        js(page, "document.querySelector('.version[data-version=\"Shelf added\"] .delete').click()");
        waitFor([&] { return store.versions(furnitureId).isEmpty(); }, 3000);
        check(store.versions(furnitureId).isEmpty(), "the second click deletes the version");
        waitFor([&] { return versionsSlot->height() == emptyHeight; }, 3000);
        check(versionsSlot->height() == emptyHeight,
              QStringLiteral("and the slot shrinks back to the empty state's height (%1) (Review Focus 5)").arg(versionsSlot->height()));

        // The close x unchecks the action.
        js(page, "document.querySelector('.versions-card .close').click()");
        pump(300);
        check(!versionsAction->isChecked() && !versionsSlot->isVisible(), "the close x closes the drawer");
    }
```

The real `MainWindow` accessor names (`currentFurnitureId()`, `furnitureStore()`, `isCompareOpen()`, `closeCompare()`) and the menu text "Save version..." must be checked in `MainWindow.h` and `buildActions()` before writing. If `closeCompare()` is private, close the pane through its own action or `Escape` path, and say which in the check.

Then, in the existing per-theme capture loop, add one capture of the drawer with one saved version in each theme (`chrome-versions-<id>.png`). Save the version first with `store.saveVersion(...)` + `window.versionsModel()->refresh()`.

- [ ] **Step 2: Run it and watch the new checks** — build `chrome_smoke` and run `chrome_smoke.exe <out>`, writing the output to a file in the workspace and reading the tail.
Expected: every new check passes; if one fails, debug with superpowers:systematic-debugging, never by loosening it. Then make one mutation at a time and watch the named red line:
  - the slot ignoring `reportCardHeight` → "the slot grows with the list …" red;
  - `discard()` not calling `release()` → "Escape discards it … giving the keyboard back" red;
  - `VersionRow` deleting on the first click → "one click on Delete arms it …" red.

  Restore each mutation by editing in place, and confirm the exe's write time moved before each run.

- [ ] **Step 3: Look at the captures** — open `chrome-versions-*.png` with Read. The card must stand under the pill, the rim and fill must be the theme's, the thumbnail must sit inside the border, and the text must not be clipped.

- [ ] **Step 4: Commit** — `git commit -am "chrome_smoke: the Versions card end to end, and a capture per theme"`.

---

### Task 6: Gate, documentation, board

- [ ] Run each of these:
  - `ctest --test-dir build-web -C RelWithDebInfo` (20 tests);
  - `npx vitest run` and `npx svelte-check`;
  - `chrome_smoke` and `hub_smoke`;
  - `gui_smoke` **twice, unfiltered, quiet**, with nothing else running beside it.

  Record each count in the Gate record below.
- [ ] CLAUDE.md (worktree): add an architecture row for `session/VersionsModel` and a short "The Versions card" paragraph under the HTML UI section, covering R2, R4 and what the page owes.
- [ ] Opus whole-branch review (`review-package`); fix Critical and Important findings test-first; ledger minors.
- [ ] Board: a card for Phase 3c, done with notes; nothing merged.

## Gate record

| Gate | Result |
|---|---|
| ctest | 20/20 in windows-web and in windows (vcpkg) |
| vitest / svelte-check | 115/115; 0 errors, 0 warnings |
| chrome_smoke | 147/147 |
| hub_smoke | 36/36 |
| gui_smoke run 1 | PASS 6448 + 1 skip, floor 6449 |
| gui_smoke run 2 | PASS 6448 + 1 skip, floor 6449 |
