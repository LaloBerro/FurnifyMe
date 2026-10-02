// SceneModel: the scene document, headless. No Qt, no GPU - it links
// furnify_geometry alone, exactly as every other headless test here does.
#include "SceneModel.h"

#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
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
    // A rotation AND a translation together is the ordinary case a gizmo
    // produces, and gp_Trsf calls that form gp_CompoundTrsf - which must not
    // be mistaken for a non-rigid one.
    gp_Trsf turnedAndMoved = turned;
    turnedAndMoved.SetTranslationPart(gp_Vec(10.0, 20.0, 30.0));
    check(SceneModel::checkPlacement(turnedAndMoved) == SceneCheck::Ok,
          "and so is a rotation with a translation - the ordinary gizmo result");
    check(!scene.setPlacement(a, scaled), "and setPlacement REFUSES one");
    check(std::fabs(scene.pieces()[0].placement.TranslationPart().X() - 100.0) < 1e-9,
          "leaving the piece exactly where it was");

    // --- the revision moves on every change ------------------------------
    const std::size_t before = scene.revision();
    scene.setPlacement(b, movedBy(-120.0, 0.0, 0.0));
    check(scene.revision() > before, "a placement moves the revision");

    // --- an empty furniture id is refused --------------------------------
    check(scene.addPiece("", "Nameless") == 0,
          "a piece naming no furniture is refused - there is nothing to render");

    check(scene.removePiece(a), "a piece is removed");
    check(scene.pieces().size() == 1 && scene.pieces()[0].name == "Chair right",
          "and the other one survives it");
    check(!scene.removePiece(a), "removing it twice refuses");

    std::printf(g_failures == 0 ? "\nPASS (%d checks)\n" : "\nFAIL (%d failures of %d)\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
