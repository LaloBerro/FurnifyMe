#pragma once
//
// THE SCENE EDITOR: several furniture arranged in one picture, and a studio to
// photograph them in.
//
// A third top-level window beside the editor and the hub, holding its own
// OcctViewWidget, its own SceneModel and its own RenderStudio. It was NOT
// built as a second mode inside MainWindow, and that is the design rather than
// a preference: MainWindow is ten thousand lines whose updateActions() is this
// app's single place availability is decided, and every term in it would have
// gained "...and we are not in a scene" - which is how that file reached its
// size. The render layer came out into RenderStudio instead, and this window
// is its second caller.
//
// WHAT IT DOES NOT HAVE is as much the point as what it does. No sketching, no
// extrude, no booleans, no joints, no mirror, no linked copies, no versions,
// no compare - not disabled, ABSENT, because an action that exists and refuses
// reads as broken. A scene arranges furniture; it does not make it.
//
// SELECTION IS PER PIECE, never per body. Clicking any body of a piece selects
// the whole piece, and the furniture editor's auto-selection - edges and faces
// competing under the cursor - is deliberately not switched on: there is
// nothing in a scene a face or an edge could be used for, and leaving it on
// would raise gizmos for operations this window does not offer.
#include <QMainWindow>
#include <QString>

#include "SceneModel.h"

class AppBar;
class FurnitureStore;
class OcctViewWidget;
class RenderStudio;
class ScenePiecesPanel;
class ViewportOverlay;

class SceneWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SceneWindow(FurnitureStore* store, QWidget* parent = nullptr);

    // Loads `id` from the store and shows it. False - with a failure reported
    // rather than swallowed - when the scene cannot be read; the window stays
    // open on whatever it was already showing.
    bool openScene(const QString& id);
    QString sceneId() const { return mySceneId; }
    const SceneModel& scene() const { return myScene; }

    OcctViewWidget* view() const { return myView; }
    RenderStudio* studio() const { return myStudio; }
    ScenePiecesPanel* piecesPanel() const { return myPieces; }

signals:
    // File -> Close scene: the way back to the hub. REPORTED, never acted on -
    // this window knows nothing about the hub, exactly as SelectorWindow knows
    // nothing about quitting. EditorSelectorHandoff is what turns this into
    // the swap, and its show-the-target-first rule is why that matters.
    void closeRequested();
    // The window's own X. Quitting is a deliberate gesture, as closing the
    // editor is since Milestone 5 - the X is not a route back to the hub.
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void buildOverlay();
    // Pushes live state onto every surface that follows it. This window's own
    // appStateChanged, under a plainer name because there is far less of it.
    void refreshSurfaces();

    FurnitureStore* myStore = nullptr;
    SceneModel myScene;
    QString mySceneId;
    QString mySceneName;

    OcctViewWidget* myView = nullptr;
    ViewportOverlay* myOverlay = nullptr;
    AppBar* myAppBar = nullptr;
    ScenePiecesPanel* myPieces = nullptr;
    RenderStudio* myStudio = nullptr;
    QAction* myRenderModeAction = nullptr;
    QAction* myCloseSceneAction = nullptr;
};
