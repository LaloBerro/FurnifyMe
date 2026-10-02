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

#include <map>
#include <vector>

#include "SceneModel.h"

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

class AddPieceCard;
class AppBar;
class FurnitureStore;
class OcctViewWidget;
class RenderStudio;
class ScenePiecesPanel;
class ToastHost;
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

    // Puts one furniture into this scene. False when the furniture could not
    // be read - and the piece is STILL added in that case, carrying its reason
    // instead of a name: a reference silently dropped is a reference the user
    // cannot see is broken, and a scene that quietly forgets a piece on open
    // is worse than one that says so.
    bool addPiece(const QString& furnitureId);
    // A piece's name is its own, never the furniture's - two pieces may point
    // at one furniture and must rename apart.
    bool renamePiece(int pieceId, const QString& name);
    // The scene-local body ids this piece put in the viewport, in order.
    std::vector<int> bodyIdsForPiece(int pieceId) const;

    OcctViewWidget* view() const { return myView; }
    RenderStudio* studio() const { return myStudio; }
    ScenePiecesPanel* piecesPanel() const { return myPieces; }
    AddPieceCard* addPieceCard() const { return myAddPiece; }

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
    // Displays one piece's bodies and pushes that furniture's own wood onto
    // each of them. The ONE place a piece reaches the viewport.
    void displayPiece(int pieceId, const DocumentModel& furniture, const gp_Trsf& placement);
    // Where a new piece lands: clear of everything already in the scene along
    // X. Deliberately not the origin - two whole furniture dropped on one
    // spot interpenetrate, and a scene whose pieces start inside each other
    // is unusable. This is not improvements item 15's automatic nudge, which
    // was about a DUPLICATE of one body, where the copy's own position is the
    // thing the user is about to set.
    gp_Trsf placementForNewPiece(const DocumentModel& furniture) const;
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
    QAction* myNewPieceAction = nullptr;
    AddPieceCard* myAddPiece = nullptr;
    ToastHost* myToasts = nullptr;

    // Scene-local body ids, handed out by this window and meaning nothing
    // outside it. A piece's bodies are NOT the furniture's own ids: two
    // pieces may point at one furniture, so those ids are not unique here.
    int myNextBodyId = 1;
    std::map<int, std::vector<int>> myPieceBodies;   // pieceId -> body ids
    std::map<int, int> myBodyPiece;                 // body id -> pieceId
    // Each piece's shapes as SAVED, untransformed. Kept so a placement change
    // can re-display them without going back to disk.
    std::map<int, std::vector<TopoDS_Shape>> myPieceShapes;
    // Why a piece has no geometry. Its row prints this WHERE THE NAME GOES.
    std::map<int, QString> myBrokenPieces;
    // Every scene-local body whose source body was saved across the grain.
    std::vector<int> myGrainAcrossBodies;
};
