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
    PlacementNotRigid      // the placement carries scale, shear or a mirror
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
    //
    // The name is the CALLER's. Four pieces all reading the furniture's own
    // name would leave the user unable to say which one they are about to
    // move, so the window seeds it from the furniture and the user renames it
    // from there.
    int addPiece(const std::string& furnitureId, const std::string& name);
    bool removePiece(int pieceId);
    const std::vector<Piece>& pieces() const { return myPieces; }
    const Piece* piece(int pieceId) const;

    // REFUSES a non-rigid placement rather than normalising it, and leaves the
    // piece exactly where it was.
    bool setPlacement(int pieceId, const gp_Trsf& placement);
    bool setPieceName(int pieceId, const std::string& name);

    // Rigid means rotation and translation only - no scale, no shear, no
    // mirror. Checked against the matrix itself (orthonormal columns,
    // determinant +1) rather than against gp_Trsf::Form(), because Form() is
    // OCCT's own bookkeeping about how a transform was BUILT: it reports
    // gp_CompoundTrsf for the ordinary rotation-plus-translation a gizmo
    // produces and for things that are not rigid alike, so it cannot answer
    // this question. The matrix can.
    //
    // Checked at all because a placement arrives from a FILE as readily as
    // from a gizmo, and a file is not a way around a bound the UI enforces.
    static SceneCheck checkPlacement(const gp_Trsf& placement);

    // --- what a picture is taken with -------------------------------------
    // The same list a furniture's render mode owns. DocumentModel::Shot is
    // reused whole rather than near-copied: a shot is "how a picture is
    // taken", and that does not change because several furniture are in it.
    const std::vector<DocumentModel::Shot>& shots() const { return myShots; }
    void addShot(const DocumentModel::Shot& shot);
    bool removeShot(std::size_t index);

    // Render settings this scene owns, mirroring what a furniture's render
    // mode persists. Plain values: the window reads and writes them, and
    // FurnitureStore round-trips them. Ints rather than the viewport's own
    // enums because this library has never seen those - the same seam
    // DocumentModel::Shot::aspect already uses.
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
