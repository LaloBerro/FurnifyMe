# Scene Editor — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Scene is a saved arrangement of several furniture, opened from the hub, arranged by moving and turning whole pieces, and rendered with the studio render mode already gives one furniture.

**Architecture:** A new `SceneModel` in `furnify_geometry` holds references to furniture plus placements and render settings. `FurnitureStore` grows scene CRUD under `<root>/scenes/<id>/`. The render layer is extracted out of `MainWindow` into a `RenderStudio` that a new `SceneWindow` drives as a second caller. The hub gains a Scenes section and `EditorSelectorHandoff` a third leg.

**Tech Stack:** C++17, Qt 6.11 Widgets, OCCT 8.0.1, CMake. MSVC `RelWithDebInfo` only (no debug OCCT/Qt binaries exist).

**Spec:** `docs/superpowers/specs/2026-10-02-scene-editor-design.md`

## Global Constraints

Copied verbatim from the spec and from CLAUDE.md. Every task's requirements implicitly include these.

- **`furnify_geometry` must not include a single Qt header.** `SceneModel` lives there; it may use OCCT and the standard library only. Enforced structurally — that target does not link Qt.
- **Vocabulary, enforced by test.** One furniture placed in a scene is a **piece**. Never "instance", "item", "object" or "copy" in any action text or widget tooltip. `gui_smoke`'s `usesBannedWord()` sweeps action text and tooltips; the existing bans (`OCCT`, `Fuse`, `Solid`, `mm3`, `(s)`, `Merge`, `Join`, `bevel`, `symmetry`, `round`, `flatten`) all still apply.
- **No modal dialogs anywhere.** `gui_smoke` asserts the window holds no `QDialog` after an outcome. Refusals are Failure toasts; questions are cards over a dimmed viewport.
- **Every refusal returns a value, never a sentence.** `ModelingOps::checkMitre()`'s contract. The UI maps the value to copy; nothing re-derives a reason by matching substrings.
- **Numbers are formatted by `Measure`**, never by hand at a call site.
- **A floating card's logical size goes through `Theme::wholeDevicePixels()`; its position through `Theme::snapToDevicePixels()`.**
- **No overlay `paintEvent` may decode, load or rescale an asset.** One dirty overlay repaints every visible overlay in the window.
- **Show the target first, hide the source second** in every handoff leg. Qt fires its last-window-closed check on a mere `hide()`.
- **Build:** `cmake --build --preset windows`. **Never build while the app may be open** — on `LNK1168`, stop and ask the user to close it. Never kill `furnifyme.exe`.
- **`assets/improvements.md` is the user's own file — never stage it.** Stage by explicit path; never `git add -A`.
- **Commit trailer:** `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
- **`kCheckFloor` is measured, never computed** — two agreeing unfiltered runs. Do not touch it inside a task; it is re-ratcheted once at the end.

## Review Focus

Five input classes the spec implies but which no task's happy path exercises. Each line's test is added to the task that owns the code, named in brackets.

1. **A scene naming a furniture that was deleted while the scene was closed.** Expected: the scene opens, the piece keeps its row with a reason where its name goes, renders nothing, and saving preserves the dangling reference. [Task 2 for the load, Task 7 for the row]
2. **A `scene.json` whose placement carries scale or shear.** Expected: refused with `SceneCheck::PlacementNotRigid` — never normalised, because silently drawing a 0.75× chair is the app lying about a dimension. [Task 1]
3. **Two pieces naming one furniture id.** Expected: they move, turn, rename and render independently; nothing keys off the id as if it were unique. [Task 1 for the model, Task 8 for the window]
4. **A `scene.json` with a format version this build does not know.** Expected: refused outright rather than guessed at, exactly as the furniture manifest refuses one. [Task 2]
5. **An empty scene — no pieces — entering render mode.** Expected: no studio floor (the floor is built from the lowest displayed body and there is none), no crash, and an export that produces a backdrop rather than failing. [Task 10]

---

## File Structure

**Created:**
- `src/SceneModel.{h,cpp}` — the scene document. `furnify_geometry`, zero Qt.
- `src/ui/RenderStudio.{h,cpp}` — the render layer, extracted from `MainWindow`.
- `src/ui/SceneWindow.{h,cpp}` — the third top-level window.
- `src/ui/ScenePiecesPanel.{h,cpp}` — the pieces list. `ItemsPanel`'s shape, far smaller.
- `src/ui/AddPieceCard.{h,cpp}` — the furniture picker.
- `tests/scene_model.cpp` — headless.

**Modified:**
- `src/FurnitureStore.{h,cpp}` — scene CRUD.
- `src/ui/SelectorWindow.{h,cpp}` — the Scenes section.
- `src/EditorSelectorHandoff.{h,cpp}` — the third leg.
- `src/MainWindow.{h,cpp}` — the render layer moves out.
- `src/OcctViewWidget.{h,cpp}` — per-body material lookup.
- `CMakeLists.txt` — new sources, new headless test.
- `tests/gui_smoke.cpp` — new blocks.
- `CLAUDE.md` — the vocabulary row and a Scene editor section.

---

## Task 1: `SceneModel` — the scene document

**Files:**
- Create: `src/SceneModel.h`, `src/SceneModel.cpp`
- Create: `tests/scene_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `CameraState` (`src/CameraController.h`), `DocumentModel::Shot` (`src/DocumentModel.h`). Both already in `furnify_geometry`.
- Produces:
  - `struct SceneModel::Piece { std::string furnitureId; std::string name; gp_Trsf placement; }`
  - `enum class SceneCheck { Ok, FurnitureMissing, FurnitureUnreadable, PlacementNotRigid }`
  - `int SceneModel::addPiece(const std::string& furnitureId, const std::string& name)` → new piece id, or 0
  - `bool SceneModel::removePiece(int pieceId)`
  - `const std::vector<Piece>& SceneModel::pieces() const`
  - `bool SceneModel::setPlacement(int pieceId, const gp_Trsf& placement)`
  - `bool SceneModel::setPieceName(int pieceId, const std::string& name)`
  - `static SceneCheck SceneModel::checkPlacement(const gp_Trsf& t)`
  - `std::size_t SceneModel::revision() const`
  - render settings accessors mirroring `DocumentModel`'s shot list: `shots()`, `addShot()`, `removeShot()`

- [ ] **Step 1: Write the failing test**

Create `tests/scene_model.cpp`:

```cpp
// SceneModel: the scene document, headless. No Qt, no GPU - it links
// furnify_geometry alone, exactly as every other headless test here does.
#include "SceneModel.h"

#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <cstdio>
#include <string>

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what)
{
    ++g_checks;
    std::printf(ok ? "[ ok ] %s\n" : "[FAIL] %s\n", what.c_str());
    if (!ok) ++g_failures;
}

gp_Trsf movedBy(double x, double y, double z)
{
    gp_Trsf t;
    t.SetTranslation(gp_Vec(x, y, z));
    return t;
}
}  // namespace

int main()
{
    SceneModel scene;
    check(scene.pieces().empty(), "a fresh scene holds no pieces");

    // --- two pieces naming ONE furniture ---------------------------------
    // Four chairs round a table is four pieces naming one id. This is the
    // normal case, not an edge case, so nothing in the model may key off the
    // furniture id as though it were unique.
    const int a = scene.addPiece("oak-chair", "Chair left");
    const int b = scene.addPiece("oak-chair", "Chair right");
    check(a > 0 && b > 0 && a != b, "two pieces naming one furniture get distinct ids");
    check(scene.pieces().size() == 2, "and both are in the list");

    check(scene.setPlacement(a, movedBy(100.0, 0.0, 0.0)), "the first is placed");
    check(scene.setPlacement(b, movedBy(-100.0, 0.0, 0.0)), "the second is placed");
    const gp_XYZ pa = scene.pieces()[0].placement.TranslationPart();
    const gp_XYZ pb = scene.pieces()[1].placement.TranslationPart();
    check(std::fabs(pa.X() - 100.0) < 1e-9 && std::fabs(pb.X() + 100.0) < 1e-9,
          "they move independently - one id, two placements");

    check(scene.setPieceName(a, "Chair by the window"), "a piece renames");
    check(scene.pieces()[0].name == "Chair by the window" &&
              scene.pieces()[1].name == "Chair right",
          "and only that one - the name is the SCENE's, not the furniture's");

    // --- a placement must be RIGID ---------------------------------------
    // A scaled chair is not a chair. Refused as a value, never normalised:
    // drawing a 0.75x chair would be this app lying about a dimension.
    gp_Trsf scaled;
    scaled.SetScale(gp_Pnt(0.0, 0.0, 0.0), 0.75);
    check(SceneModel::checkPlacement(scaled) == SceneCheck::PlacementNotRigid,
          "a scaling placement is refused by name");
    check(SceneModel::checkPlacement(movedBy(1.0, 2.0, 3.0)) == SceneCheck::Ok,
          "a translation is fine");
    gp_Trsf turned;
    turned.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0.5);
    check(SceneModel::checkPlacement(turned) == SceneCheck::Ok, "so is a rotation");
    check(!scene.setPlacement(a, scaled), "and setPlacement REFUSES one");
    check(std::fabs(scene.pieces()[0].placement.TranslationPart().X() - 100.0) < 1e-9,
          "leaving the piece exactly where it was");

    // --- the revision moves on every change ------------------------------
    const std::size_t before = scene.revision();
    scene.setPlacement(b, movedBy(-120.0, 0.0, 0.0));
    check(scene.revision() > before, "a placement moves the revision");

    check(scene.removePiece(a), "a piece is removed");
    check(scene.pieces().size() == 1 && scene.pieces()[0].name == "Chair right",
          "and the other one survives it");
    check(!scene.removePiece(a), "removing it twice refuses");

    std::printf(g_failures == 0 ? "\nPASS (%d checks)\n" : "\nFAIL (%d failures of %d)\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `CMakeLists.txt` beside the other headless tests:

```cmake
add_executable(headless_scene_model tests/scene_model.cpp)
target_link_libraries(headless_scene_model PRIVATE furnify_geometry)
add_test(NAME scene_model COMMAND headless_scene_model)
```

Run: `cmake --build --preset windows-headless`
Expected: FAIL to compile — `SceneModel.h` does not exist.

- [ ] **Step 3: Write `src/SceneModel.h`**

```cpp
#pragma once
//
// A SCENE: several furniture arranged in one picture.
//
// A scene REFERENCES its furniture rather than copying them, so editing a
// chair changes every scene the chair stands in. One source of truth - a
// scene can never show wood the user has since changed. The cost is that a
// referenced furniture can be deleted out from under a scene, which is
// handled loudly rather than designed away (see SceneCheck and
// FurnitureStore::loadScene()).
//
// Zero Qt, like every other file in furnify_geometry - which is what makes
// tests/scene_model.cpp runnable with no window and no GPU.
#include "CameraController.h"
#include "DocumentModel.h"

#include <gp_Trsf.hxx>

#include <cstddef>
#include <string>
#include <vector>

// Why a placement, or a referenced furniture, was refused. A VALUE, never a
// sentence: the UI maps it to copy, and nothing re-derives a reason by
// matching substrings of an error string (ModelingOps::checkMitre()'s own
// contract).
enum class SceneCheck {
    Ok,
    FurnitureMissing,      // the id names nothing in the store
    FurnitureUnreadable,   // it exists and failed to load
    PlacementNotRigid      // the placement carries scale or shear
};

class SceneModel {
public:
    // ONE furniture standing in this scene. The same `furnitureId` may appear
    // many times - four chairs round a table is four pieces naming one id -
    // so nothing may key off it as though it were unique.
    struct Piece {
        int id = 0;                  // this scene's own, unique, never reused
        std::string furnitureId;     // into FurnitureStore
        std::string name;            // what the SCENE calls it
        gp_Trsf placement;
    };

    // Appends a piece and returns its new id, or 0 for an empty furniture id.
    // The name is the caller's: four pieces all reading the furniture's own
    // name would leave the user unable to say which one they are about to
    // move, so the window seeds it and the user renames it.
    int addPiece(const std::string& furnitureId, const std::string& name);
    bool removePiece(int pieceId);
    const std::vector<Piece>& pieces() const { return myPieces; }
    const Piece* piece(int pieceId) const;

    // REFUSES a non-rigid placement rather than normalising it, and leaves the
    // piece exactly where it was.
    bool setPlacement(int pieceId, const gp_Trsf& placement);
    bool setPieceName(int pieceId, const std::string& name);

    // Rigid means rotation and translation only. gp_Trsf tells us directly:
    // ScaleFactor() is 1 for a rigid motion, and Form() names the kinds that
    // are not. Checked rather than trusted, because a placement arrives from
    // a file as readily as from a gizmo.
    static SceneCheck checkPlacement(const gp_Trsf& placement);

    // --- what a picture is taken with -------------------------------------
    // The same list a furniture's render mode owns. DocumentModel::Shot is
    // reused whole rather than near-copied: a shot is "how a picture is
    // taken" and that does not change because several furniture are in it.
    const std::vector<DocumentModel::Shot>& shots() const { return myShots; }
    void addShot(const DocumentModel::Shot& shot);
    bool removeShot(std::size_t index);

    // Render settings this scene owns, mirroring what a furniture's render
    // mode persists. Plain values: the window reads and writes them, and
    // FurnitureStore round-trips them.
    int aspect = 0;              // OcctViewWidget::RenderAspect's ordinal
    int guides = 1;              // OcctViewWidget::RenderGuides' ordinal
    double lightAngleDeg = 142.0;
    double lightStrength = 2.0;
    double fovDeg = 45.0;
    bool orthographic = false;
    int exportSize = 0;          // OcctViewWidget::ExportSize's ordinal
    int quality = 2;             // OcctViewWidget::RenderQuality's ordinal
    bool cutout = false;
    CameraState camera;

    // Monotonic, never rolled back. Autosave and the unsaved dot read it,
    // exactly as they read DocumentModel::revision().
    std::size_t revision() const { return myRevision; }
    void clear();

private:
    std::vector<Piece> myPieces;
    std::vector<DocumentModel::Shot> myShots;
    int myNextPieceId = 1;
    std::size_t myRevision = 0;
};
```

- [ ] **Step 4: Write `src/SceneModel.cpp`**

```cpp
#include "SceneModel.h"

#include <algorithm>
#include <cmath>

SceneCheck SceneModel::checkPlacement(const gp_Trsf& placement)
{
    // gp_Trsf::ScaleFactor() is exactly 1 for a rigid motion. Form() names
    // the non-rigid kinds outright, which catches a shear a scale factor
    // alone would not.
    if (std::fabs(placement.ScaleFactor() - 1.0) > 1.0e-9) return SceneCheck::PlacementNotRigid;
    const gp_TrsfForm form = placement.Form();
    if (form == gp_Scale || form == gp_CompoundTrsf || form == gp_Other) {
        // gp_CompoundTrsf covers a rotation+translation too, which IS rigid -
        // so it is only refused when the scale factor above already said so.
        // Reaching here with a unit scale means Form() is gp_Other: a shear
        // or a mirror, neither of which is a placement this app can make.
        if (form == gp_Other) return SceneCheck::PlacementNotRigid;
    }
    return SceneCheck::Ok;
}

int SceneModel::addPiece(const std::string& furnitureId, const std::string& name)
{
    if (furnitureId.empty()) return 0;
    Piece piece;
    piece.id = myNextPieceId++;
    piece.furnitureId = furnitureId;
    piece.name = name;
    myPieces.push_back(piece);
    ++myRevision;
    return piece.id;
}

bool SceneModel::removePiece(int pieceId)
{
    const auto at = std::find_if(myPieces.begin(), myPieces.end(),
                                 [pieceId](const Piece& p) { return p.id == pieceId; });
    if (at == myPieces.end()) return false;
    myPieces.erase(at);
    ++myRevision;
    return true;
}

const SceneModel::Piece* SceneModel::piece(int pieceId) const
{
    const auto at = std::find_if(myPieces.begin(), myPieces.end(),
                                 [pieceId](const Piece& p) { return p.id == pieceId; });
    return at == myPieces.end() ? nullptr : &*at;
}

bool SceneModel::setPlacement(int pieceId, const gp_Trsf& placement)
{
    if (checkPlacement(placement) != SceneCheck::Ok) return false;
    const auto at = std::find_if(myPieces.begin(), myPieces.end(),
                                 [pieceId](const Piece& p) { return p.id == pieceId; });
    if (at == myPieces.end()) return false;
    at->placement = placement;
    ++myRevision;
    return true;
}

bool SceneModel::setPieceName(int pieceId, const std::string& name)
{
    const auto at = std::find_if(myPieces.begin(), myPieces.end(),
                                 [pieceId](const Piece& p) { return p.id == pieceId; });
    if (at == myPieces.end()) return false;
    at->name = name;
    ++myRevision;
    return true;
}

void SceneModel::addShot(const DocumentModel::Shot& shot)
{
    myShots.push_back(shot);
    ++myRevision;
}

bool SceneModel::removeShot(std::size_t index)
{
    if (index >= myShots.size()) return false;
    myShots.erase(myShots.begin() + static_cast<std::ptrdiff_t>(index));
    ++myRevision;
    return true;
}

void SceneModel::clear()
{
    myPieces.clear();
    myShots.clear();
    myNextPieceId = 1;
    ++myRevision;
}
```

Add `src/SceneModel.cpp` to the `furnify_geometry` source list in `CMakeLists.txt`.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset windows-headless && ctest --preset windows-headless -R scene_model --output-on-failure`
Expected: PASS, all checks.

- [ ] **Step 6: Commit**

```bash
git add src/SceneModel.h src/SceneModel.cpp tests/scene_model.cpp CMakeLists.txt
git commit -m "SceneModel: pieces that reference furniture, and a rigid placement

A scene references its furniture rather than copying them, so one id may
appear many times - four chairs round a table is four pieces naming one
chair, and nothing keys off the id as though it were unique. Each piece
carries the SCENE's own name for it, because four rows all reading the
furniture's name leave the user unable to say which one they are about to
move.

A placement must be rigid. A scaling one is refused by value rather than
normalised: a scaled chair is not a chair, and drawing one would be this
app lying about a dimension.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `FurnitureStore` — scene CRUD

**Files:**
- Modify: `src/FurnitureStore.h`, `src/FurnitureStore.cpp`
- Test: `tests/gui_smoke.cpp` (new independent block)

**Interfaces:**
- Consumes: `SceneModel` (Task 1).
- Produces:
  - `struct FurnitureStore::SceneInfo { QString id; QString name; QString filePath; QString thumbPath; QDateTime lastEdited; }`
  - `QVector<SceneInfo> listScenes() const`
  - `QString createScene(const QString& name)` → new id, or empty
  - `bool loadScene(const QString& id, SceneModel& scene, QString* error)`
  - `bool saveScene(const QString& id, const SceneModel& scene, const QImage& thumbnail)`
  - `bool renameScene(const QString& id, const QString& name)`
  - `bool deleteScene(const QString& id)`
  - `QString nextSceneName() const`

- [ ] **Step 1: Write the failing test**

Register a new block name in `kBlocks[]` in `tests/gui_smoke.cpp`:

```cpp
    { "the-store-keeps-scenes-beside-the-furniture", false, true },
```

Add the block, before the autosave block:

```cpp
    // --- the store keeps scenes beside the furniture -------------------------
    //
    // A scene lives in <root>/scenes/<id>/ so listFurniture() can never return
    // one by accident, and the layout stays store-private exactly as the
    // class header already promises.
    if (blockEnabled("the-store-keeps-scenes-beside-the-furniture")) {
        RequiredTempDir sceneLib;
        FurnitureStore store(sceneLib.path());

        check(store.listScenes().isEmpty(), "a fresh library has no scenes");

        const QString id = store.createScene(QStringLiteral("Dining set"));
        check(!id.isEmpty(), "a scene is created");
        check(store.listScenes().size() == 1, "and it is listed");
        check(store.listFurniture().isEmpty(),
              "and listFurniture() does NOT return it - the two kinds cannot be "
              "confused for one another");

        // --- a scene round trips ---------------------------------------------
        SceneModel out;
        const int p1 = out.addPiece("chair-id", "Chair left");
        const int p2 = out.addPiece("chair-id", "Chair right");
        gp_Trsf left;
        left.SetTranslation(gp_Vec(-250.0, 0.0, 0.0));
        gp_Trsf right;
        right.SetTranslation(gp_Vec(250.0, 0.0, 0.0));
        out.setPlacement(p1, left);
        out.setPlacement(p2, right);
        out.aspect = 2;
        out.lightAngleDeg = 200.0;
        out.lightStrength = 3.25;
        out.fovDeg = 72.0;
        out.orthographic = true;
        DocumentModel::Shot shot;
        shot.name = "Three-quarter";
        shot.camera.distance = 1234.0;
        out.addShot(shot);
        check(store.saveScene(id, out, QImage()), "it saves");

        SceneModel back;
        QString error;
        check(store.loadScene(id, back, &error),
              QStringLiteral("and loads again (\"%1\")").arg(error));
        check(back.pieces().size() == 2, "both pieces came back");
        check(back.pieces().size() == 2 && back.pieces()[0].furnitureId == "chair-id" &&
                  back.pieces()[1].furnitureId == "chair-id",
              "both naming the same furniture - a repeated id survives the file");
        check(back.pieces().size() == 2 &&
                  std::fabs(back.pieces()[0].placement.TranslationPart().X() + 250.0) < 1e-6 &&
                  std::fabs(back.pieces()[1].placement.TranslationPart().X() - 250.0) < 1e-6,
              "with their own placements, to the micron");
        check(back.pieces().size() == 2 && back.pieces()[0].name == "Chair left",
              "and their own names");
        check(back.aspect == 2 && std::fabs(back.lightAngleDeg - 200.0) < 1e-9 &&
                  std::fabs(back.fovDeg - 72.0) < 1e-9 && back.orthographic,
              "every render setting came back");
        check(back.shots().size() == 1 &&
                  std::fabs(back.shots().front().camera.distance - 1234.0) < 1e-9,
              "and the shot with it");

        // --- a FUTURE format is refused outright -----------------------------
        // Guessing at a newer layout is exactly how a document silently loses
        // data - the furniture manifest's own rule.
        {
            const QString path = sceneLib.path() + QStringLiteral("/scenes/") + id +
                                 QStringLiteral("/scene.json");
            QFile file(path);
            check(file.open(QIODevice::ReadWrite), "the scene file opens for the format probe");
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            obj[QStringLiteral("format")] = 9999;
            file.resize(0);
            file.seek(0);
            file.write(QJsonDocument(obj).toJson());
            file.close();

            SceneModel future;
            QString futureError;
            check(!store.loadScene(id, future, &futureError),
                  "a scene from a FUTURE format refuses to load");
            check(!futureError.isEmpty(), "and says so rather than failing silently");
        }

        check(store.renameScene(id, QStringLiteral("Kitchen")), "a scene renames");
        check(!store.listScenes().isEmpty() &&
                  store.listScenes().front().name == QStringLiteral("Kitchen"),
              "and the listing says so");
        check(store.deleteScene(id), "a scene is deleted");
        check(store.listScenes().isEmpty(), "and is gone from the listing");
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build --preset windows` then
`./build/RelWithDebInfo/gui_smoke.exe build/shots the-store-keeps-scenes`
Expected: FAIL to compile — `listScenes` is not a member of `FurnitureStore`.

- [ ] **Step 3: Add the scene API to `src/FurnitureStore.h`**

Beside `VersionInfo`:

```cpp
    // A SCENE in the library. Deliberately a second struct rather than a flag
    // on FurnitureInfo: the hub lists the two in separate sections and a
    // caller that could hold either would have to ask which it had before it
    // could do anything with it.
    struct SceneInfo {
        QString id;
        QString name;
        QString filePath;    // <root>/scenes/<id>
        QString thumbPath;
        QDateTime lastEdited;
    };
```

Beside the furniture methods:

```cpp
    // --- scenes ----------------------------------------------------------
    // Scenes live under <root>/scenes/, so listFurniture() cannot return one.
    // That layout is store-private exactly as the furniture layout is -
    // nothing outside this class touches a path inside it.
    QVector<SceneInfo> listScenes() const;
    QString nextSceneName() const;
    QString createScene(const QString& name);
    // Scratch-then-swap, as every load here is: decodes into a fresh
    // SceneModel and only assigns to `scene` on success, so a refusal leaves
    // the caller's scene untouched. A FUTURE format version refuses outright.
    bool loadScene(const QString& id, SceneModel& scene, QString* error);
    bool saveScene(const QString& id, const SceneModel& scene, const QImage& thumbnail);
    bool renameScene(const QString& id, const QString& name);
    bool deleteScene(const QString& id);
```

And in the private section:

```cpp
    static constexpr int kSceneFormatVersion = 1;
    QString scenesRoot() const;
    QString sceneDir(const QString& id) const;
    QString scenePath(const QString& id) const;
    QString sceneThumbPath(const QString& id) const;
```

- [ ] **Step 4: Implement in `src/FurnitureStore.cpp`**

Add `#include "SceneModel.h"` at the top. Add the path helpers and the JSON round trip:

```cpp
QString FurnitureStore::scenesRoot() const
{
    return myRootDir + QStringLiteral("/scenes");
}
QString FurnitureStore::sceneDir(const QString& id) const
{
    return scenesRoot() + QLatin1Char('/') + id;
}
QString FurnitureStore::scenePath(const QString& id) const
{
    return sceneDir(id) + QStringLiteral("/scene.json");
}
QString FurnitureStore::sceneThumbPath(const QString& id) const
{
    return sceneDir(id) + QStringLiteral("/thumb.png");
}

namespace {
QJsonObject sceneToJson(const SceneModel& scene)
{
    QJsonArray pieces;
    for (const SceneModel::Piece& piece : scene.pieces()) {
        QJsonObject entry;
        entry[QStringLiteral("furniture")] = QString::fromStdString(piece.furnitureId);
        entry[QStringLiteral("name")] = QString::fromStdString(piece.name);
        // The placement as its twelve numbers, in gp_Trsf's own row-major
        // order. Written out rather than as a named pose, because a rotation
        // has no single honest decomposition and a round trip through one
        // would not come back to the micron.
        QJsonArray m;
        for (int row = 1; row <= 3; ++row) {
            for (int col = 1; col <= 4; ++col) m.append(piece.placement.Value(row, col));
        }
        entry[QStringLiteral("placement")] = m;
        pieces.append(entry);
    }
    QJsonObject obj;
    obj[QStringLiteral("pieces")] = pieces;
    obj[QStringLiteral("aspect")] = scene.aspect;
    obj[QStringLiteral("guides")] = scene.guides;
    obj[QStringLiteral("lightAngle")] = scene.lightAngleDeg;
    obj[QStringLiteral("lightStrength")] = scene.lightStrength;
    obj[QStringLiteral("fov")] = scene.fovDeg;
    obj[QStringLiteral("orthographic")] = scene.orthographic;
    obj[QStringLiteral("exportSize")] = scene.exportSize;
    obj[QStringLiteral("quality")] = scene.quality;
    obj[QStringLiteral("cutout")] = scene.cutout;
    obj[QStringLiteral("cameraTargetX")] = scene.camera.target.X();
    obj[QStringLiteral("cameraTargetY")] = scene.camera.target.Y();
    obj[QStringLiteral("cameraTargetZ")] = scene.camera.target.Z();
    obj[QStringLiteral("cameraAzimuth")] = scene.camera.azimuthDeg;
    obj[QStringLiteral("cameraElevation")] = scene.camera.elevationDeg;
    obj[QStringLiteral("cameraDistance")] = scene.camera.distance;
    QJsonArray shots;
    for (const DocumentModel::Shot& shot : scene.shots()) {
        QJsonObject s;
        s[QStringLiteral("name")] = QString::fromStdString(shot.name);
        s[QStringLiteral("targetX")] = shot.camera.target.X();
        s[QStringLiteral("targetY")] = shot.camera.target.Y();
        s[QStringLiteral("targetZ")] = shot.camera.target.Z();
        s[QStringLiteral("azimuth")] = shot.camera.azimuthDeg;
        s[QStringLiteral("elevation")] = shot.camera.elevationDeg;
        s[QStringLiteral("distance")] = shot.camera.distance;
        s[QStringLiteral("orthographic")] = shot.orthographic;
        s[QStringLiteral("fov")] = shot.fovDeg;
        s[QStringLiteral("aspect")] = shot.aspect;
        s[QStringLiteral("lightAngle")] = shot.lightAngleDeg;
        s[QStringLiteral("lightStrength")] = shot.lightStrength;
        shots.append(s);
    }
    obj[QStringLiteral("shots")] = shots;
    return obj;
}

// Returns false and leaves `scene` untouched on any refusal - the
// scratch-then-swap law every load in this file keeps.
bool jsonToScene(const QJsonObject& obj, SceneModel& scene, QString* error)
{
    SceneModel scratch;
    for (const QJsonValue& value : obj.value(QStringLiteral("pieces")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString furniture = entry.value(QStringLiteral("furniture")).toString();
        if (furniture.isEmpty()) continue;
        const int id = scratch.addPiece(furniture.toStdString(),
                                        entry.value(QStringLiteral("name")).toString().toStdString());
        const QJsonArray m = entry.value(QStringLiteral("placement")).toArray();
        if (m.size() == 12) {
            gp_Trsf placement;
            placement.SetValues(m.at(0).toDouble(), m.at(1).toDouble(), m.at(2).toDouble(),
                                m.at(3).toDouble(), m.at(4).toDouble(), m.at(5).toDouble(),
                                m.at(6).toDouble(), m.at(7).toDouble(), m.at(8).toDouble(),
                                m.at(9).toDouble(), m.at(10).toDouble(), m.at(11).toDouble());
            // REFUSED, not normalised. A file asking for a 0.75x chair is
            // corrupt or from a build that allowed something this one does
            // not, and drawing it would be a lie about a dimension.
            if (SceneModel::checkPlacement(placement) != SceneCheck::Ok) {
                if (error) *error = QObject::tr("A piece in this scene is not placed squarely.");
                return false;
            }
            scratch.setPlacement(id, placement);
        }
    }
    scratch.aspect = obj.value(QStringLiteral("aspect")).toInt(0);
    scratch.guides = obj.value(QStringLiteral("guides")).toInt(1);
    scratch.lightAngleDeg = obj.value(QStringLiteral("lightAngle")).toDouble(142.0);
    scratch.lightStrength =
        std::clamp(obj.value(QStringLiteral("lightStrength")).toDouble(2.0), 0.25, 4.0);
    scratch.fovDeg = std::clamp(obj.value(QStringLiteral("fov")).toDouble(45.0), 10.0, 90.0);
    scratch.orthographic = obj.value(QStringLiteral("orthographic")).toBool(false);
    scratch.exportSize = obj.value(QStringLiteral("exportSize")).toInt(0);
    scratch.quality = obj.value(QStringLiteral("quality")).toInt(2);
    scratch.cutout = obj.value(QStringLiteral("cutout")).toBool(false);
    scratch.camera.target = gp_Pnt(obj.value(QStringLiteral("cameraTargetX")).toDouble(0.0),
                                   obj.value(QStringLiteral("cameraTargetY")).toDouble(0.0),
                                   obj.value(QStringLiteral("cameraTargetZ")).toDouble(0.0));
    scratch.camera.azimuthDeg = obj.value(QStringLiteral("cameraAzimuth")).toDouble(-45.0);
    scratch.camera.elevationDeg =
        std::clamp(obj.value(QStringLiteral("cameraElevation")).toDouble(30.0),
                   CameraController::kMinElevation, CameraController::kMaxElevation);
    scratch.camera.distance =
        std::clamp(obj.value(QStringLiteral("cameraDistance")).toDouble(700.0),
                   CameraController::kMinDistance, CameraController::kMaxDistance);
    for (const QJsonValue& value : obj.value(QStringLiteral("shots")).toArray()) {
        const QJsonObject s = value.toObject();
        DocumentModel::Shot shot;
        shot.name = s.value(QStringLiteral("name")).toString().toStdString();
        if (shot.name.empty()) continue;
        shot.camera.target = gp_Pnt(s.value(QStringLiteral("targetX")).toDouble(0.0),
                                    s.value(QStringLiteral("targetY")).toDouble(0.0),
                                    s.value(QStringLiteral("targetZ")).toDouble(0.0));
        shot.camera.azimuthDeg = s.value(QStringLiteral("azimuth")).toDouble(-45.0);
        shot.camera.elevationDeg = s.value(QStringLiteral("elevation")).toDouble(30.0);
        shot.camera.distance = s.value(QStringLiteral("distance")).toDouble(700.0);
        shot.orthographic = s.value(QStringLiteral("orthographic")).toBool(false);
        shot.fovDeg = s.value(QStringLiteral("fov")).toDouble(45.0);
        shot.aspect = s.value(QStringLiteral("aspect")).toInt(0);
        shot.lightAngleDeg = s.value(QStringLiteral("lightAngle")).toDouble(142.0);
        shot.lightStrength = s.value(QStringLiteral("lightStrength")).toDouble(2.0);
        scratch.addShot(shot);
    }
    scene = scratch;
    return true;
}
}  // namespace
```

Then the six public methods. `createScene` mirrors `createFurniture`: `QUuid::createUuid().toString(QUuid::WithoutBraces)`, `mkpath(sceneDir(id))`, write a `scene.json` carrying `format = kSceneFormatVersion`, `name`, `lastEdited` and an empty `pieces` array. `loadScene` reads the file, refuses when `format != kSceneFormatVersion` with an error naming the file, then calls `jsonToScene`. `saveScene` writes through a `QSaveFile` (never a truncate onto the live file) and writes `thumbnail` to `sceneThumbPath(id)` when it is not null. `listScenes` walks `scenesRoot()`'s subdirectories, reading each `scene.json`'s `name` and `lastEdited`. `renameScene` rewrites the `name` key. `deleteScene` removes the directory recursively. `nextSceneName()` returns `tr("Scene %1")` for the first free number, exactly as `nextFurnitureName()` does for furniture.

- [ ] **Step 5: Run the block to verify it passes**

Run: `cmake --build --preset windows` then
`./build/RelWithDebInfo/gui_smoke.exe build/shots the-store-keeps-scenes`
Expected: `FILTERED (0 failures, N checks)`.

- [ ] **Step 6: Commit**

```bash
git add src/FurnitureStore.h src/FurnitureStore.cpp tests/gui_smoke.cpp
git commit -m "Store: scenes live beside the furniture, in their own subdirectory

<root>/scenes/<id>/, so listFurniture() can never return one by accident,
and the layout stays store-private exactly as the furniture layout is.

scene.json carries its own format version and refuses a future one
outright rather than guessing at a newer layout - the manifest's own rule,
and the reason a document does not silently lose data. A placement is
refused if it is not rigid, leaving the caller's scene untouched: the
scratch-then-swap law every load in this file keeps.

A placement is written as its twelve numbers in gp_Trsf's own order rather
than as a named pose - a rotation has no single honest decomposition, and
a round trip through one would not come back to the micron.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `RenderStudio` — the render layer leaves `MainWindow`

This is the spine. Nothing else in the scene window can be built until the render layer has a second caller.

**Files:**
- Create: `src/ui/RenderStudio.h`, `src/ui/RenderStudio.cpp`
- Modify: `src/MainWindow.h`, `src/MainWindow.cpp`, `CMakeLists.txt`
- Test: `tests/gui_smoke.cpp` — **no new block**; the gate is that every existing render block passes unchanged.

**Interfaces:**
- Consumes: `OcctViewWidget`, `RenderSettingsPanel`, `RenderFrameGuides`, `ToolChip`, `AppBar`, `CardSlide`, `ViewportOverlay`.
- Produces:
  - `RenderStudio(OcctViewWidget* view, QMainWindow* host, ViewportOverlay* overlay, AppBar* bar, QObject* parent)`
  - `void setEnabled(bool on)` / `bool isEnabled() const`
  - `RenderSettingsPanel* panel() const`, `RenderFrameGuides* frame() const`, `ToolChip* exitChip() const`
  - `void refresh()` — called from the host's `appStateChanged`
  - `void setShotNames(const QStringList& names)`
  - signals: `shotSaveRequested()`, `shotApplied(int)`, `shotRemoved(int)`, `enabledChanged(bool)`

- [ ] **Step 1: Record the baseline**

Run the full suite and record the exact numbers. This is the gate for the whole task.

```bash
cmake --build --preset windows
mkdir -p build/shotsbase
./build/RelWithDebInfo/gui_smoke.exe build/shotsbase > build/sweepbase.txt 2>&1
tail -2 build/sweepbase.txt
```
Expected: `PASS (0 failures, N checks, 1 skipped by the environment, floor 4914)`. Write N down; it must not change.

- [ ] **Step 2: Create `src/ui/RenderStudio.h`**

```cpp
#pragma once
//
// THE RENDER LAYER, owned by nobody in particular.
//
// Everything about TAKING A PICTURE - the settings panel and its dock, the
// frame and guides, the Back chip, the app bar's slide-away, the tier ticker,
// and the mode's own entry and exit - used to live in MainWindow, which is
// ten thousand lines about furniture. A scene needs all of it and none of
// the furniture, so it comes out here and both windows drive it.
//
// It owns THE WIDGETS AND THE VIEWPORT-FACING STATE ONLY. The host still owns
// its own document: this class re-emits "save this view", "apply shot N" and
// "forget shot N", and MainWindow writes them to its DocumentModel while
// SceneWindow writes them to its SceneModel. Pushing the shot list down here
// would mean an interface both document types implement, dragging a UI
// concept into furnify_geometry for no gain - the boundary already works
// where it is.
//
// EditorSelectorHandoff is this project's own precedent: one implementation
// of a thing two callers need, rather than two that drift.
#include <QObject>
#include <QStringList>

class AppBar;
class CardSlide;
class OcctViewWidget;
class QMainWindow;
class QTimer;
class QWidget;
class RenderFrameGuides;
class RenderSettingsPanel;
class ToolChip;
class ViewportOverlay;

class RenderStudio : public QObject {
    Q_OBJECT

public:
    RenderStudio(OcctViewWidget* view, QMainWindow* host, ViewportOverlay* overlay,
                 AppBar* bar, QObject* parent = nullptr);

    // THE SINGLE AUTHORITY for whether render mode is on, exactly as
    // MainWindow::setRenderModeEnabled() was. Docks the panel, slides the bar
    // away, raises the Back chip, starts the tier ticker, and emits
    // enabledChanged() so the host can re-derive its own surfaces.
    void setEnabled(bool on);
    bool isEnabled() const { return myEnabled; }

    // Re-derives everything that follows live state. The host calls this from
    // its own appStateChanged - the sibling-visibility law every overlay in
    // this app already follows: derived on every state change, never a
    // one-shot at the control that moved.
    void refresh();

    // The shots this host holds, by name. Rebuilt only when the list actually
    // changed - this runs on every appStateChanged.
    void setShotNames(const QStringList& names);

    RenderSettingsPanel* panel() const { return myPanel; }
    RenderFrameGuides* frame() const { return myFrame; }
    ToolChip* exitChip() const { return myExitChip; }

signals:
    void shotSaveRequested();
    void shotApplied(int index);
    void shotRemoved(int index);
    void enabledChanged(bool on);

private:
    void setDockOpen(bool open);

    OcctViewWidget* myView = nullptr;
    QMainWindow* myHost = nullptr;
    ViewportOverlay* myOverlay = nullptr;
    AppBar* myBar = nullptr;
    CardSlide* myBarSlide = nullptr;
    RenderSettingsPanel* myPanel = nullptr;
    RenderFrameGuides* myFrame = nullptr;
    ToolChip* myExitChip = nullptr;
    QTimer* myTierTicker = nullptr;
    QWidget* myDock = nullptr;
    bool myEnabled = false;
};
```

- [ ] **Step 3: Move the implementation, by cut and paste**

This is a **move, not a rewrite**. From `src/MainWindow.cpp`, relocate into `RenderStudio.cpp` verbatim, keeping every comment:

- the body of `setRenderModeEnabled()` minus its document-facing lines (the toast naming the tier stays in `MainWindow`, raised from `enabledChanged`)
- `setRenderDockOpen()` whole, renamed `setDockOpen()`
- the `myRenderSettingsPanel` construction block and every `connect` to it, with the three shot connects becoming re-emits
- the `myRenderFrame` construction and its `renderFrameChanged` connect
- the `myRenderExitChip` construction and its overlay registration
- the `myAppBarSlide` construction
- the `myRenderTierTicker` block
- the `hiddenForRenderMode` lines from `updateActions()` that concern the panel, frame, exit chip and bar — these become `RenderStudio::refresh()`

Leave in `MainWindow`: `applyMaterialLook()`, `activeMaterialName()`, `currentShot()`, `applyShot()`, `nextShotName()`, the material card, and every line about a document.

In `MainWindow`, replace the moved members with one `RenderStudio* myStudio`, construct it in `buildOverlay()`, and wire its three shot signals to the existing document handlers. Keep `MainWindow::renderSettingsPanel()`, `renderFrame()` and `renderExitChip()` as one-line forwards to `myStudio` — `gui_smoke` calls all three and this task must not change the suite.

- [ ] **Step 4: Run the FULL suite and compare against the baseline**

```bash
cmake --build --preset windows
mkdir -p build/shotsmove
./build/RelWithDebInfo/gui_smoke.exe build/shotsmove > build/sweepmove.txt 2>&1
tail -2 build/sweepmove.txt
```
Expected: **0 failures, and the identical check count from Step 1.** A changed count means something stopped running — that is a failure of this task, not a tolerable difference.

- [ ] **Step 5: Commit**

```bash
git add src/ui/RenderStudio.h src/ui/RenderStudio.cpp src/MainWindow.h src/MainWindow.cpp CMakeLists.txt
git commit -m "RenderStudio: the render layer comes out of MainWindow

Everything about taking a picture - the settings panel and its dock, the
frame and guides, the Back chip, the app bar's slide-away, the tier ticker
and the mode's own entry and exit - lived in a ten-thousand-line class
about furniture. A scene needs all of it and none of the furniture.

It owns the widgets and the viewport-facing state only. Shots stay a host
concern: this re-emits save/apply/forget and MainWindow writes them to its
DocumentModel, so the boundary stays where it already works rather than
dragging a UI concept into furnify_geometry.

A MOVE, NOT A REDESIGN, and that is a testable claim rather than a hope:
the entire existing render-mode suite passes with the identical check
count it had before this commit.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Per-piece wood in the viewport

**Files:**
- Modify: `src/OcctViewWidget.h`, `src/OcctViewWidget.cpp`
- Test: `tests/gui_smoke.cpp` (new independent block)

**Interfaces:**
- Produces:
  - `struct OcctViewWidget::BodyWood { double red, green, blue, brightness, surface, metal, grainSize, grainAngle; }`
  - `void setBodyWood(int bodyId, const BodyWood& wood)`
  - `void clearBodyWood()`
  - `bool hasBodyWood(int bodyId) const`

- [ ] **Step 1: Write the failing test**

Register `{ "two-bodies-can-wear-two-different-woods", false, true },` and add:

```cpp
    // --- two bodies can wear two different woods -----------------------------
    //
    // refreshWoodOverlays() already builds ONE overlay per body, each with its
    // own material - it was simply handed the same one every time. A scene
    // needs the table in its wood and the chairs in theirs, so a body may now
    // carry its own, falling back to the single live material when it does
    // not. The furniture editor sets none and is unchanged.
    if (blockEnabled("two-bodies-can-wear-two-different-woods")) {
        RequiredTempDir woodLib;
        MainWindow probe(nullptr, /*persistProgress=*/false, woodLib.path());
        probe.setAttribute(Qt::WA_ShowWithoutActivating);
        probe.resize(1000, 700);
        probe.show();
        settle(300);
        probe.view()->setAnimationsEnabled(false);
        enterFreshFurniture(probe);
        OcctViewWidget* wview = probe.view();

        check(buildBody(probe, 0.12, 0.35, 0.34, 0.62, 40.0), "the left body");
        const int leftId = probe.document().solids().back().id;
        check(buildBody(probe, 0.60, 0.35, 0.82, 0.62, 40.0), "the right body");
        const int rightId = probe.document().solids().back().id;

        QAction* renderAction = action(probe, QStringLiteral("Render mode"));
        check(renderAction != nullptr, "there is a Render mode action");
        if (renderAction) renderAction->trigger();
        settle(400);

        // Deliberately far apart, so a pixel from one cannot be mistaken for a
        // pixel from the other at any blend.
        OcctViewWidget::BodyWood dark;
        dark.red = 0.18; dark.green = 0.10; dark.blue = 0.05; dark.brightness = 1.0;
        OcctViewWidget::BodyWood pale;
        pale.red = 0.92; pale.green = 0.88; pale.blue = 0.74; pale.brightness = 1.0;
        wview->setBodyWood(leftId, dark);
        wview->setBodyWood(rightId, pale);
        settle(400);

        const QString path = outDir + QStringLiteral("/two-woods.png");
        check(wview->saveSnapshot(path), "a snapshot is taken");
        const QImage shot(path);
        check(!shot.isNull(), "and it loads back");

        // The two bodies sit left and right of centre; sample a band inside
        // each rather than one pixel, because this tier is grainy and a median
        // does not care.
        const auto bandMedian = [&shot](double x0, double x1) {
            std::vector<int> levels;
            for (int x = int(shot.width() * x0); x < int(shot.width() * x1); ++x) {
                for (int y = int(shot.height() * 0.42); y < int(shot.height() * 0.52); ++y)
                    levels.push_back(qGray(shot.pixel(x, y)));
            }
            if (levels.empty()) return -1;
            std::sort(levels.begin(), levels.end());
            return levels[levels.size() / 2];
        };
        const int leftLevel = bandMedian(0.18, 0.30);
        const int rightLevel = bandMedian(0.66, 0.78);
        check(leftLevel > 0 && rightLevel > 0,
              QStringLiteral("both bodies are in frame (%1, %2)").arg(leftLevel).arg(rightLevel));
        check(rightLevel - leftLevel > 40,
              QStringLiteral("and they render as two DIFFERENT woods - the pale one reads "
                             "well above the dark one (%1 against %2)")
                  .arg(rightLevel).arg(leftLevel));

        // AND THE FALLBACK IS INTACT: clearing the per-body entries puts both
        // back on the single live material, which is what the furniture editor
        // relies on and must not have changed.
        wview->clearBodyWood();
        settle(400);
        const QString plainPath = outDir + QStringLiteral("/one-wood.png");
        check(wview->saveSnapshot(plainPath), "a second snapshot is taken");
        const QImage plain(plainPath);
        const auto plainBand = [&plain](double x0, double x1) {
            std::vector<int> levels;
            for (int x = int(plain.width() * x0); x < int(plain.width() * x1); ++x) {
                for (int y = int(plain.height() * 0.42); y < int(plain.height() * 0.52); ++y)
                    levels.push_back(qGray(plain.pixel(x, y)));
            }
            if (levels.empty()) return -1;
            std::sort(levels.begin(), levels.end());
            return levels[levels.size() / 2];
        };
        check(!plain.isNull() && std::abs(plainBand(0.18, 0.30) - plainBand(0.66, 0.78)) < 25,
              QStringLiteral("with no per-body wood set, both read the same single live "
                             "material again (%1 against %2)")
                  .arg(plainBand(0.18, 0.30)).arg(plainBand(0.66, 0.78)));

        probe.close();
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `./build/RelWithDebInfo/gui_smoke.exe build/shots two-bodies-can-wear`
Expected: FAIL to compile — `BodyWood` is not a member of `OcctViewWidget`.

- [ ] **Step 3: Add the lookup to `src/OcctViewWidget.h`**

Beside `setBodyGrainAcross`:

```cpp
    // ONE BODY'S OWN WOOD. refreshWoodOverlays() already builds an overlay per
    // body with its own material; this is where that material comes from when
    // a body has one. A body with no entry falls back to the single live
    // material, so the furniture editor - which sets none - is unchanged.
    //
    // A scene is what needs this: the table renders in the wood it was saved
    // with and the chairs in theirs. It is deliberately NOT generalised into
    // per-body materials inside one furniture, which is a feature with its own
    // assignment gesture and its own design round.
    struct BodyWood {
        double red = 0.70, green = 0.70, blue = 0.68;
        double brightness = 1.0;
        double surface = 0.45;
        double metal = 0.0;
        double grainSize = 300.0;
        double grainAngle = 0.0;
    };
    void setBodyWood(int bodyId, const BodyWood& wood);
    void clearBodyWood();
    bool hasBodyWood(int bodyId) const;
```

With `std::map<int, BodyWood> myBodyWood;` among the members.

- [ ] **Step 4: Use it in `refreshWoodOverlays()`**

In `src/OcctViewWidget.cpp`, inside the per-body loop, replace the single `overlay->material = material;` with a per-body derivation:

```cpp
        // THIS BODY'S OWN WOOD when it has one, the single live material when
        // it does not. The fallback is what keeps the furniture editor
        // identical: it sets no entries at all.
        const auto own = myBodyWood.find(entry.first);
        if (own == myBodyWood.end()) {
            overlay->material = material;
            overlay->tileMm = std::max(10.0, myWoodTileMm);
            overlay->angleDeg = myWoodAngleDeg + (bodyGrainAcross(entry.first) ? 90.0 : 0.0);
        } else {
            const BodyWood& wood = own->second;
            Graphic3d_MaterialAspect mine(Graphic3d_NameOfMaterial_UserDefined);
            mine.SetColor(Quantity_Color(wood.red * wood.brightness,
                                         wood.green * wood.brightness,
                                         wood.blue * wood.brightness, Quantity_TOC_sRGB));
            Graphic3d_PBRMaterial pbr;
            pbr.SetColor(mine.Color());
            pbr.SetMetallic(static_cast<float>(wood.metal));
            pbr.SetRoughness(static_cast<float>(1.0 - wood.surface));
            mine.SetPBRMaterial(pbr);
            // SetPBRMaterial() never writes the BSDF, and the BSDF is the only
            // description OCCT's path tracer integrates - without this the
            // body renders black on the top tier (CLAUDE.md's Pitfalls).
            mine.SetBSDF(Graphic3d_BSDF::CreateMetallicRoughness(pbr));
            overlay->material = mine;
            overlay->tileMm = std::max(10.0, wood.grainSize);
            overlay->angleDeg = wood.grainAngle + (bodyGrainAcross(entry.first) ? 90.0 : 0.0);
        }
```

`setBodyWood()` writes the map and calls `applyWoodTexture(true)` when overlays are live; `clearBodyWood()` empties it and does the same.

- [ ] **Step 5: Run the block to verify it passes**

Run: `./build/RelWithDebInfo/gui_smoke.exe build/shots two-bodies-can-wear`
Expected: `FILTERED (0 failures, N checks)`.

- [ ] **Step 6: Commit**

```bash
git add src/OcctViewWidget.h src/OcctViewWidget.cpp tests/gui_smoke.cpp
git commit -m "A body can wear its own wood

refreshWoodOverlays() already built one overlay per body, each with its own
material - it was simply handed the same one every time. A body may now
carry its own, falling back to the single live material when it does not,
so the furniture editor sets no entries and is unchanged.

A scene is what needs this: the table in the wood it was saved with and the
chairs in theirs. Deliberately not generalised into per-body materials
inside one furniture - that is a feature with its own assignment gesture.

The per-body material writes its BSDF beside its PBR material, because
SetPBRMaterial() never writes one and the BSDF is the only description the
path tracer integrates.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: The hub's Scenes section

**Files:**
- Modify: `src/ui/SelectorWindow.h`, `src/ui/SelectorWindow.cpp`
- Test: `tests/gui_smoke.cpp` (new independent block)

**Interfaces:**
- Consumes: `FurnitureStore::listScenes()`, `createScene()` (Task 2).
- Produces: signals `void sceneChosen(const QString& id)` and `void createSceneRequested()`; accessors `int sceneCardCount() const` and `QWidget* sceneCardAt(int index) const`.

- [ ] **Step 1: Write the failing test**

Register `{ "the-hub-lists-scenes-in-their-own-section", false, true },` and add a block that: builds a `SelectorWindow` over a `RequiredTempDir`, checks `sceneCardCount() == 0`, clicks the scenes `+`, checks a scene was created in the store and `sceneCardCount() == 1`, clicks the card and checks `sceneChosen` fired with that id, and — the non-vacuity half — checks that creating a *furniture* leaves `sceneCardCount()` unchanged and vice versa, so the two sections genuinely cannot be confused.

- [ ] **Step 2: Run it to verify it fails**

Expected: FAIL to compile — `sceneCardCount` is not a member.

- [ ] **Step 3: Implement**

Add a second section below the furniture grid, built by the same card helper the furniture grid uses so the two look like one window. The window owns every `FurnitureStore` call, as its class comment already requires: the `+` creates the scene itself and then emits `sceneChosen(id)` for the id it just made, exactly as `createRequested()`/`furnitureChosen()` already pair.

- [ ] **Step 4: Run the block to verify it passes**

- [ ] **Step 5: Commit**

```bash
git add src/ui/SelectorWindow.h src/ui/SelectorWindow.cpp tests/gui_smoke.cpp
git commit -m "The hub lists scenes in their own section

Below the furniture, with its own + card. One grid holding both was
rejected: a card's click would mean two different things and the user would
have to know which kind they were looking at before they knew what a click
would do.

This window still owns every FurnitureStore call, as its class comment
requires - the + creates the scene and then emits sceneChosen() for the id
it just made, exactly as createRequested()/furnitureChosen() already pair.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: The handoff's third leg

**Files:**
- Modify: `src/EditorSelectorHandoff.h`, `src/EditorSelectorHandoff.cpp`, `src/main.cpp`
- Test: `tests/gui_smoke.cpp` (extend the existing handoff block)

**Interfaces:**
- Consumes: `SelectorWindow::sceneChosen` (Task 5), `SceneWindow` (Task 7).
- Produces: `void wire(MainWindow&, SelectorWindow&, SceneWindow&, Hooks)`; `Hooks` gains `std::function<void()> onOpenSceneMidpoint`.

Note: this task depends on Task 7's `SceneWindow` existing as a type. Implement Task 7 first and return here, or stub `SceneWindow` in Task 7's step 1.

- [ ] **Step 1: Write the failing test**

Extend the existing handoff block: choosing a scene shows the scene window **before** the selector hides (asserted through `onOpenSceneMidpoint`, exactly as `onOpenMidpoint` already asserts the order for furniture), and `File → Close scene` shows the selector before the scene window hides. Closing the scene window runs `hooks.quit`.

- [ ] **Step 2: Run it to verify it fails**

- [ ] **Step 3: Implement**

Add the leg following the existing two exactly — target shown first, source hidden second, every connection using its own window as context object so the wiring tears down if either is destroyed.

- [ ] **Step 4: Run the handoff block to verify it passes**

- [ ] **Step 5: Commit**

---

## Task 7: `SceneWindow` — the shell

**Files:**
- Create: `src/ui/SceneWindow.h`, `src/ui/SceneWindow.cpp`, `src/ui/ScenePiecesPanel.h`, `src/ui/ScenePiecesPanel.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/gui_smoke.cpp` (new independent block)

**Interfaces:**
- Consumes: `SceneModel` (1), `FurnitureStore` scene CRUD (2), `RenderStudio` (3).
- Produces: `SceneWindow(FurnitureStore* store, QWidget* parent)`; `bool openScene(const QString& id)`; `void closeScene()`; signals `closeRequested()`, `quitRequested()`; accessors `OcctViewWidget* view()`, `ScenePiecesPanel* piecesPanel()`, `RenderStudio* studio()`, `const SceneModel& scene()`.

- [ ] **Step 1: Write the failing test**

A block that constructs a `SceneWindow` over a temp library, opens a scene created through the store, and checks: the window shows, it holds an `OcctViewWidget`, it holds a `ScenePiecesPanel` with zero rows, the window title names the scene, and `findChildren<QDialog*>()` is empty.

- [ ] **Step 2: Run it to verify it fails**

- [ ] **Step 3: Implement the shell**

A `QMainWindow` with its own `OcctViewWidget` as central widget, its own `ViewportOverlay`, an `AppBar` carrying a File menu (New piece, Save, Close scene), a `ScenePiecesPanel` anchored `TopLeft`, and a `RenderStudio` over its viewport. No sketch, extrude, boolean, joint, mirror, version or compare action is created at all — a scene arranges furniture, it does not make it.

`ScenePiecesPanel` is `ItemsPanel`'s shape at a fraction of its size: one row per piece, each with the piece's name, an eye and a rename; `selectionRequested(int pieceId)`, `visibilityToggled(int pieceId)`, `renameCommitted(int pieceId, QString)`, `removeRequested(int pieceId)`.

- [ ] **Step 4: Run the block to verify it passes**

- [ ] **Step 5: Commit**

---

## Task 8: Adding a piece, and rendering it in its own wood

**Files:**
- Create: `src/ui/AddPieceCard.h`, `src/ui/AddPieceCard.cpp`
- Modify: `src/ui/SceneWindow.{h,cpp}`, `CMakeLists.txt`
- Test: `tests/gui_smoke.cpp` (extend Task 7's block)

**Interfaces:**
- Consumes: `FurnitureStore::listFurniture()`, `loadFurniture()`, `OcctViewWidget::setBodyWood()` (4), `SceneModel::addPiece()` (1).
- Produces: `AddPieceCard` with `void show(const QVector<FurnitureStore::FurnitureInfo>&)` and `signals: void chosen(QString furnitureId)`; `bool SceneWindow::addPiece(const QString& furnitureId)`.

- [ ] **Step 1: Write the failing test**

Extend Task 7's block: create two furniture through the store, each saved with a different wood; add both as pieces; check the panel has two rows, the viewport displays both pieces' bodies, each body carries its own `BodyWood` (`hasBodyWood()` true for every displayed body), and **adding the same furniture twice gives two rows that rename independently** — the Review Focus item.

- [ ] **Step 2: Run it to verify it fails**

- [ ] **Step 3: Implement**

`addPiece()` loads the furniture through the store into a scratch `DocumentModel`, adds a `SceneModel::Piece`, displays each of that document's bodies in the scene's viewport under a scene-local body id, records which piece each body belongs to, and pushes that furniture's own `MaterialLook` through `setBodyWood()` for each of them. A failed load is a Failure toast naming the furniture; the piece is still added, carrying its reason (Task 9's broken-reference row).

- [ ] **Step 4: Run the block to verify it passes**

- [ ] **Step 5: Commit**

---

## Task 9: Move and Rotate, with grid and floor snap

**Files:**
- Modify: `src/ui/SceneWindow.{h,cpp}`
- Test: `tests/gui_smoke.cpp` (extend Task 7's block)

**Interfaces:**
- Consumes: `TransformGizmo`'s `MoveGizmoRenderer`/`RotateGizmoRenderer`, `SceneModel::setPlacement()`.
- Produces: `void SceneWindow::setTool(Tool)` with `enum class Tool { Move, Rotate }`; `int SceneWindow::selectedPieceId() const`.

- [ ] **Step 1: Write the failing test**

Extend the block: select a piece, check the Move gizmo is up and **Scale is not offered anywhere** (no action, no chip, no renderer); drag an arm with Snap to Grid on and check the placement lands on a 10 mm step; switch to Rotate and check a drag lands on 15°; check a piece dropped anywhere sits on Z = 0, measured from its own measured box rather than a world bounding box; and check the two pieces naming one furniture move independently.

- [ ] **Step 2: Run it to verify it fails**

- [ ] **Step 3: Implement**

Reuse the existing gizmo renderers and their drag maths. The delta applies to the piece's `placement` through `SceneModel::setPlacement()`, which refuses a non-rigid result — so a bug that introduced scale is caught at the model rather than rendered.

Floor snap derives the lowest point from `ModelingOps::measuredBox()` of the piece's own shapes, never a world bounding box: this project's most repeated bug class is an oriented quantity measured in world terms.

- [ ] **Step 4: Run the block to verify it passes**

- [ ] **Step 5: Commit**

---

## Task 10: Render a scene, save it, reopen it

**Files:**
- Modify: `src/ui/SceneWindow.{h,cpp}`
- Test: `tests/gui_smoke.cpp` (extend Task 7's block)

**Interfaces:**
- Consumes: `RenderStudio` (3), `FurnitureStore::saveScene()`/`loadScene()` (2).
- Produces: `bool SceneWindow::save()`; `DocumentModel::Shot SceneWindow::currentShot(const QString& name) const`; `void SceneWindow::applyShot(const DocumentModel::Shot&)`.

- [ ] **Step 1: Write the failing test**

Extend the block:

- render mode in the scene shows **the same classes** the editor's does (`RenderSettingsPanel`, `RenderFrameGuides`)
- a shot saved in a scene restores the scene's own camera, aspect, perspective and light
- save, close, reopen: placements and names identical to the micron
- **a furniture deleted while the scene was closed** leaves its row with a reason, renders nothing, and saving preserves the dangling reference — the Review Focus item
- **an empty scene entering render mode** raises no crash, puts no studio floor up, and still exports an image — the Review Focus item:

```cpp
        // AN EMPTY SCENE still takes a picture. The studio floor is built from
        // the lowest DISPLAYED body and there is none, so this is the one
        // path where the floor is legitimately absent - it must be absent
        // rather than wrong, and the export must still produce a file.
        {
            RequiredTempDir emptyLib;
            FurnitureStore emptyStore(emptyLib.path());
            const QString emptyId = emptyStore.createScene(QStringLiteral("Nothing"));
            SceneWindow empty(&emptyStore);
            empty.setAttribute(Qt::WA_ShowWithoutActivating);
            empty.resize(900, 700);
            empty.show();
            settle(300);
            check(empty.openScene(emptyId), "an empty scene opens");
            check(empty.scene().pieces().empty(), "and it really is empty");
            empty.studio()->setEnabled(true);
            settle(400);
            check(empty.studio()->isEnabled(), "render mode turns on over nothing at all");
            const QString emptyPath = outDir + QStringLiteral("/empty-scene.png");
            check(empty.view()->saveSnapshot(emptyPath),
                  "and an export still writes a file rather than failing");
            check(!QImage(emptyPath).isNull(), "which loads back as an image");
            empty.close();
        }
```

- [ ] **Step 2: Run it to verify it fails**

- [ ] **Step 3: Implement**

`save()` writes the `SceneModel` plus a viewport thumbnail through `saveScene()`. Shots go through `currentShot()`/`applyShot()`, mirroring `MainWindow`'s pair exactly — one place each where the list of what a shot carries is written down. The unsaved dot and the close question reuse the editor's own `FurnitureNameMark` and `UnsavedCloseCard`.

- [ ] **Step 4: Run the block to verify it passes**

- [ ] **Step 5: Commit**

---

## Task 11: Documentation and the floor

**Files:**
- Modify: `CLAUDE.md`, `tests/gui_smoke.cpp`

- [ ] **Step 1: Add the vocabulary row and a Scene editor section to `CLAUDE.md`**

The row (`piece` | never instance, item, object, copy), a Scene editor section covering the reference-not-copy ruling, the rigid-placement refusal, `RenderStudio`'s extraction and its "identical check count" gate, per-body wood and its fallback, and the broken-reference rule. Add `src/SceneModel.{h,cpp}`, `src/ui/RenderStudio.{h,cpp}`, `src/ui/SceneWindow.{h,cpp}`, `src/ui/ScenePiecesPanel.{h,cpp}` and `src/ui/AddPieceCard.{h,cpp}` to the architecture table.

- [ ] **Step 2: Measure the floor twice**

```bash
./build/RelWithDebInfo/gui_smoke.exe build/shots1 > build/sweep1.txt 2>&1
./build/RelWithDebInfo/gui_smoke.exe build/shots2 > build/sweep2.txt 2>&1
tail -1 build/sweep1.txt; tail -1 build/sweep2.txt
```
Both must report 0 failures and the **same** check count. Measured, never computed.

- [ ] **Step 3: Raise `kCheckFloor` to the measured total and run once more to confirm the gate passes**

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md tests/gui_smoke.cpp
git commit -m "Scene editor: documentation, and ratchet the floor to the measured total

Two consecutive unfiltered runs agreeing to the digit. Measured, never
computed - a predicted floor is a number nobody has watched the suite
produce.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```
