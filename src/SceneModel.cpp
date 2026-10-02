#include "SceneModel.h"

#include <algorithm>
#include <cmath>

namespace {
// How far from 1 a column's length, or the determinant, may sit before the
// transform stops being rigid. Generous enough to absorb the rounding a
// round trip through twelve decimal numbers in a file costs, tight enough
// that the smallest scale anything in this app can apply (5%, the transform
// gizmo's own floor) is refused by a wide margin.
constexpr double kRigidTolerance = 1.0e-6;
}  // namespace

SceneCheck SceneModel::checkPlacement(const gp_Trsf& placement)
{
    // The 3x3 rotation part, read straight out of the matrix. gp_Trsf::Value()
    // is 1-based and 3x4, the fourth column being the translation - which a
    // rigid motion may carry freely and is therefore not examined here.
    double m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) m[row][col] = placement.Value(row + 1, col + 1);
    }

    // Rigid means the columns are unit length and mutually perpendicular
    // (no scale, no shear) and the determinant is +1 (no mirror). Asked of
    // the matrix rather than of gp_Trsf::Form(), which reports how a
    // transform was BUILT rather than what it does - Form() is
    // gp_CompoundTrsf for the ordinary rotation-plus-translation a gizmo
    // produces and for non-rigid compounds alike.
    for (int col = 0; col < 3; ++col) {
        const double lengthSquared =
            m[0][col] * m[0][col] + m[1][col] * m[1][col] + m[2][col] * m[2][col];
        if (std::fabs(lengthSquared - 1.0) > kRigidTolerance) return SceneCheck::PlacementNotRigid;
    }
    for (int first = 0; first < 3; ++first) {
        for (int second = first + 1; second < 3; ++second) {
            const double dot = m[0][first] * m[0][second] + m[1][first] * m[1][second] +
                               m[2][first] * m[2][second];
            if (std::fabs(dot) > kRigidTolerance) return SceneCheck::PlacementNotRigid;
        }
    }
    const double determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::fabs(determinant - 1.0) > kRigidTolerance) return SceneCheck::PlacementNotRigid;

    return SceneCheck::Ok;
}

int SceneModel::addPiece(const std::string& furnitureId, const std::string& name)
{
    // A piece naming no furniture has nothing to render and nothing to
    // resolve. Refused rather than stored as a row that could never work.
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
    // Checked BEFORE the piece is found, so a refusal reads the same whether
    // the id was good or not: nothing was written either way.
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
    // The id counter restarts with the scene: a cleared scene is a new scene,
    // and nothing outside it holds a piece id across the clear.
    myNextPieceId = 1;
    ++myRevision;
}
