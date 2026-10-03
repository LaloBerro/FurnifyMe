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
class FurnitureNameMark;
class ScenePiecesPanel;
class ToastHost;
class UnsavedCloseCard;
class ViewportOverlay;

class SceneWindow : public QMainWindow {
    Q_OBJECT

public:
    // THE TWO TOOLS, and there is deliberately no third. The user's fork was
    // "full move and rotate, and snapping" - Scale is not in it, and a scene
    // is where its absence matters most: a chair scaled to 1.4x is not a
    // chair any more, it is a drawing of one. A furniture is resized by
    // Re-measure, in the editor, where a size means something. So there is no
    // Tool::Scale, no action, no chip and no renderer - absent, not disabled.
    enum class Tool { Move, Rotate };

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
    // Takes a piece out of the scene, bodies and all. A scene has no undo, so
    // the gesture that reaches this asks twice.
    bool removePiece(int pieceId);
    // The scene-local body ids this piece put in the viewport, in order.
    std::vector<int> bodyIdsForPiece(int pieceId) const;
    // This piece's shapes WHERE THEY STAND - its saved geometry with its
    // placement applied. Built on demand rather than kept: it is wanted when
    // something is measured, which is on a commit, not on every drag step.
    std::vector<TopoDS_Shape> placedShapesForPiece(int pieceId) const;

    // Writes the scene and a thumbnail of what is on screen. False on a
    // refused write, REPORTED as a Failure rather than swallowed.
    bool save();
    // Unsaved changes, DERIVED from the scene's own revision against the one
    // last written - never a flag something has to remember to set.
    bool isSceneDirty() const { return myScene.revision() != mySavedRevision; }
    bool isAskingBeforeClose() const;

    // What a shot carries is RenderStudio's list, not a second copy of it.
    DocumentModel::Shot currentShot(const QString& name) const;
    void applyShot(const DocumentModel::Shot& shot);
    QString nextShotName() const;
    // Keeps a shot in the scene. The window's own route, so the dirty state
    // and the surfaces follow a shot exactly as they follow a placement.
    void addShotToScene(const DocumentModel::Shot& shot);
    bool removeShotFromScene(std::size_t index);

    void setTool(Tool tool);
    Tool tool() const { return myTool; }
    // Which piece is selected, derived from the live selection rather than
    // remembered - so a selection cleared anywhere answers 0 with nothing to
    // keep in step. Clicking ANY body of a piece selects the whole piece.
    int selectedPieceId() const { return mySelectedPiece; }

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
    // Which exit an answered close question carries out - the editor's own
    // CloseRoute split: the X quits, File -> Close scene goes to the library.
    // Declared before the members that take it, which a parameter type must be.
    enum class CloseRoute { None, Quit, Library };

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
    // Pushes a piece's placement onto every body it owns. The ONE place a
    // placement reaches the screen.
    void applyPlacement(int pieceId, const gp_Trsf& placement);
    // Drops `placement` so the piece's lowest point sits on Z = 0, measured
    // from its own MEASURED BOX rather than a world bounding box - a piece
    // turned on the floor has a world box taller than the furniture is, and
    // settling against that would leave it hovering.
    gp_Trsf settledOnFloor(int pieceId, const gp_Trsf& placement) const;
    // Selection -> piece -> gizmo, derived on every selection change.
    void refreshSelection();
    void showToolGizmo();
    // One end for both gestures: settle on the floor, re-stand the gizmo, push
    // the surfaces. `dragged` false means the press never moved, which is a
    // pick rather than a placement.
    void commitDrag(bool dragged);
    // The scene's own render settings and camera, pushed onto the viewport on
    // open and read back out on save. Without the pair they round-tripped to
    // disk and were discarded on every reload.
    void applySceneSettings();
    void captureSceneSettings();
    // Asks, or leaves straight away when there is nothing to lose. True when
    // the exit was handled here and the caller should not continue.
    bool askBeforeClosing(CloseRoute route);
    void finishClose(CloseRoute route);
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
    QAction* mySnapAction = nullptr;
    QAction* myMoveAction = nullptr;
    QAction* myRotateAction = nullptr;
    QAction* myScreenshotAction = nullptr;
    QAction* myFitAllAction = nullptr;
    QAction* mySaveAction = nullptr;
    UnsavedCloseCard* myCloseCard = nullptr;
    FurnitureNameMark* myNameMark = nullptr;
    // The revision last written to disk. Compared against the live one, so
    // "dirty" cannot drift out of step with what actually changed.
    std::size_t mySavedRevision = 0;
    CloseRoute myCloseRoute = CloseRoute::None;
    Tool myTool = Tool::Move;
    int mySelectedPiece = 0;
    // Guards the selection handler against the selection IT sets: picking one
    // body of a piece selects all of them, which emits selectionChanged()
    // again. The no-reentry discipline this app keeps everywhere.
    bool myApplyingSelection = false;
    // The placement a live drag started from. Frozen at the press, because
    // every delta the gizmo reports is measured from where the gesture began -
    // the custom gizmo's own rule.
    gp_Trsf myDragBase;
    bool myDragging = false;
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
