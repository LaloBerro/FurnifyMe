// What a joint's row says (JointRows) and the Joints drawer's model for the
// page (JointsModel) - migration Phase 3d, headless, on real boards and the
// real Joinery::derive().
//
// OCCT first, by the Handle() rule - DocumentModel.h carries Joinery.h.
#include "DocumentModel.h"
#include "Measure.h"
#include "session/JointRows.h"
#include "session/JointsModel.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Trsf.hxx>

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const QString& what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    if (!ok) ++gFailures;
}

Joinery::Derivation deriveOf(const DocumentModel& doc, const DocumentModel::Joint& joint)
{
    return Joinery::derive(joint.kind, joint.params, joint.adjustments, doc.shapeOf(joint.bodyA),
                           doc.shapeOf(joint.bodyB));
}

const QRegularExpression kDigit(QStringLiteral("\\d"));
}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // A side panel and a shelf meeting in a T - the joinery suite's own probe.
    DocumentModel doc;
    const int panel = doc.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
    const int shelf = doc.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(18.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
    const int dowelId = doc.addJoint(Joinery::Kind::Dowel, panel, shelf, Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
    check(dowelId > 0, "a dowel joint between the panel and the shelf");
    const DocumentModel::Joint dowel = doc.joints().front();
    const Joinery::Derivation d = deriveOf(doc, dowel);
    check(d.ok, "and it derives");

    // --- JointRows: a fastener -----------------------------------------------
    const JointRows::Row row = JointRows::content(doc, dowel, d, dowelId, true);
    const QString size = QString::fromStdString(
        Measure::formatLength(d.items.empty() ? dowel.params.sizeMm : d.items.front().sizeMm));
    check(row.kindLine == QStringLiteral("Dowel, %1 × %2").arg(d.items.size()).arg(size),
          "a fastener's kind line is its kind, count and size - \"" + row.kindLine + "\"");
    check(!row.broken && row.selected && row.expanded && row.fastener, "the row's state is carried as given");
    check(row.pieces.size() == 2, QStringLiteral("both pieces are drilled (%1)").arg(row.pieces.size()));
    bool drill = true, fractions = true, labels = true;
    for (const JointRows::Piece& piece : row.pieces) {
        drill = drill && piece.depthText.startsWith(QStringLiteral("drill ")) && !piece.band;
        for (const JointRows::Mark& m : piece.marks) {
            fractions = fractions && m.at >= 0.0 && m.at <= 1.0 && std::abs(m.at - m.mm / row.run) < 1e-9;
            labels = labels && m.text == JointRows::numberOnly(m.mm);
        }
        fractions = fractions && piece.marks.size() == d.items.size();
    }
    check(drill, "each piece says drill and a depth");
    check(fractions, "every mark sits at its fraction of the run, one per dowel");
    check(labels, "and is labelled with its number, its unit left to the shared line");
    check(!row.edge.isEmpty() &&
              row.sharedDrawn == QStringLiteral("%1 from the %2 edge · inset %3 from the face")
                                     .arg(row.unit, row.edge, JointRows::lengthText(row.inset)),
          "the shared line under the rulers - \"" + row.sharedDrawn + "\"");
    check(row.sharedWritten == QStringLiteral("inset %1 from the face").arg(JointRows::lengthText(row.inset)),
          "and, when the numbers are written, only the inset is left - \"" + row.sharedWritten + "\"");
    check(row.pieces.front().writtenLine.startsWith(QStringLiteral("from the %1 edge: ").arg(row.edge)) &&
              row.pieces.front().writtenLine.endsWith(QLatin1Char(' ') + row.unit),
          "a written line names the edge and ends in the unit - \"" + row.pieces.front().writtenLine + "\"");
    check(row.readoutText.contains(row.pieces.front().depthText), "the readout carries the depths");

    // --- JointRows: a housing --------------------------------------------------
    {
        DocumentModel::Joint dado = dowel;
        dado.kind = Joinery::Kind::Dado;
        dado.params = Joinery::defaultsForContact(Joinery::Kind::Dado, d.contact);
        const Joinery::Derivation dd = deriveOf(doc, dado);
        check(dd.ok, "a dado on the same T derives");
        const JointRows::Row housing = JointRows::content(doc, dado, dd, 0, false);
        bool bands = !housing.pieces.empty();
        for (const JointRows::Piece& piece : housing.pieces)
            bands = bands && piece.band && piece.bandStart.at < piece.bandEnd.at &&
                    piece.depthText.contains(QStringLiteral(" × ")) && piece.marks.empty() &&
                    piece.writtenLine.contains(QStringLiteral(" to "));
        check(bands, QStringLiteral("a housing's pieces carry a band, \"W × D\" and a written span (%1 pieces)")
                         .arg(housing.pieces.size()));
        check(!housing.fastener &&
                  housing.sharedDrawn == QStringLiteral("%1 from the %2 edge").arg(housing.unit, housing.edge) &&
                  housing.sharedWritten.isEmpty(),
              "and its shared line names the edge alone - no inset");
    }

    // --- JointRows: broken ------------------------------------------------------
    {
        DocumentModel apart;
        const int a = apart.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 0.0), 18.0, 300.0, 800.0).Shape());
        const int b = apart.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(68.0, 0.0, 400.0), 600.0, 300.0, 18.0).Shape());
        apart.addJoint(Joinery::Kind::Dowel, a, b, Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
        const DocumentModel::Joint j = apart.joints().front();
        const JointRows::Row broken = JointRows::content(apart, j, deriveOf(apart, j), 0, true);
        check(broken.broken && broken.pieces.empty() && broken.caveat.isEmpty() && broken.readoutText.isEmpty() &&
                  broken.sharedDrawn.isEmpty(),
              "a broken joint carries no pieces, no caveat, no numbers");
        check(broken.kindLine == QStringLiteral("Broken — these two pieces don't meet — move the pieces so they meet"),
              "and says why and what to do - \"" + broken.kindLine + "\"");
        check(!kDigit.match(broken.kindLine).hasMatch(), "with no digit in it");
    }

    // --- reasonText ---------------------------------------------------------------
    check(JointRows::reasonText("contact: BRep_API: command not done") ==
              QStringLiteral("these two pieces can't be measured where they meet — move one so they meet squarely"),
          "a kernel exception becomes a sentence a woodworker can act on");
    check(JointRows::reasonText("the pieces aren't crossing").endsWith(QStringLiteral("move one so it crosses the other")),
          "a Joinery reason keeps its words and gains a next step");
    check(JointRows::title() == QStringLiteral("Joints") &&
              JointRows::emptyText() ==
                  QStringLiteral("No joints yet.\n\nSelect two pieces that meet and press J to plan one.") &&
              JointRows::deleteTooltip() == QStringLiteral("Delete this joint\nOne undo brings it back."),
          "the drawer's words, verbatim");

    // --- JointsModel: the drawer's model for the page ----------------------------
    {
        // A far piece with a joint to the panel: broken from the start.
        const int far = doc.addSolid(BRepPrimAPI_MakeBox(gp_Pnt(900.0, 0.0, 0.0), 18.0, 300.0, 300.0).Shape());
        const int brokenId = doc.addJoint(Joinery::Kind::Dowel, panel, far, Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
        auto derivations = [&] {
            std::vector<Joinery::Derivation> out;
            for (const DocumentModel::Joint& j : doc.joints()) out.push_back(deriveOf(doc, j));
            return out;
        };

        QList<int> selected, removed;
        int closes = 0;
        bool removeAnswer = true;
        JointsModel::Hooks hooks;
        hooks.select = [&](int id) { selected << id; };
        hooks.remove = [&](int id) { removed << id; return removeAnswer; };
        hooks.close = [&] { ++closes; };
        JointsModel model(hooks);
        int changes = 0;
        QObject::connect(&model, &JointsModel::changed, [&] { ++changes; });
        const auto idAt = [&](int i) { return model.rows().value(i).toMap().value("id").toInt(); };
        const auto rowOf = [&](int id) {
            for (const QVariant& v : model.rows())
                if (v.toMap().value("id").toInt() == id) return v.toMap();
            return QVariantMap();
        };

        model.setState(&doc, derivations(), 0, true, true, true);
        check(model.rows().size() == 2 && idAt(0) == brokenId && idAt(1) == dowelId,
              "broken joints first, each group in document order");
        check(model.live() && model.enabled(), "a live, enabled drawer");
        const QVariantMap first = model.rows().value(1).toMap();
        check(first.value("kindLine").toString() == row.kindLine &&
                  first.value("sharedDrawn").toString() == row.sharedDrawn &&
                  first.value("pieces").toList().size() == 2,
              "a row carries JointRows' content");
        const QVariantMap mark =
            first.value("pieces").toList().value(0).toMap().value("marks").toList().value(0).toMap();
        check(mark.contains("at") && mark.contains("text") && mark.value("text").toString() == row.pieces[0].marks[0].text,
              "and each mark crosses as its fraction and its label");
        check(rowOf(brokenId).value("broken").toBool() && rowOf(brokenId).value("pieces").toList().isEmpty(),
              "a broken row crosses with no pieces");

        // Open by a click, by id.
        model.activate(dowelId);
        check(model.isExpanded(dowelId) && selected == QList<int>{dowelId} &&
                  rowOf(dowelId).value("expanded").toBool(),
              "a click opens the row and selects its joint");
        model.setState(&doc, derivations(), dowelId, true, true, true);
        check(rowOf(dowelId).value("selected").toBool(), "the selected joint reads selected");

        // The dowel's shelf moves away: it breaks, moves up the list, and stays open.
        gp_Trsf away;
        away.SetTranslation(gp_Vec(0.0, 0.0, 600.0));
        doc.replaceSolid(shelf, BRepBuilderAPI_Transform(doc.shapeOf(shelf), away, true).Shape());
        model.setState(&doc, derivations(), dowelId, true, true, true);
        check(rowOf(dowelId).value("broken").toBool() && rowOf(dowelId).value("expanded").toBool() &&
                  !rowOf(brokenId).value("expanded").toBool(),
              "a joint that breaks keeps ITS row open, by id, as it moves (Review Focus 3)");
        away.SetTranslation(gp_Vec(0.0, 0.0, -600.0));
        doc.replaceSolid(shelf, BRepBuilderAPI_Transform(doc.shapeOf(shelf), away, true).Shape());

        model.activate(dowelId);
        check(!model.isExpanded(dowelId) && selected.size() == 2, "a second click closes it, and selects again");

        model.remove(dowelId);
        check(removed == QList<int>{dowelId}, "delete reaches the window's hook");
        model.setState(&doc, derivations(), 0, true, /*enabled=*/false, true);
        model.remove(dowelId);
        check(removed.size() == 1, "and not while the drawer is shut for sketching or render mode");

        // A gone joint leaves the open set.
        model.setState(&doc, derivations(), 0, true, true, true);
        model.activate(brokenId);
        doc.removeJoint(brokenId);
        model.setState(&doc, derivations(), 0, true, true, true);
        check(!model.isExpanded(brokenId) && model.rows().size() == 1, "a deleted joint leaves the drawer, and the open set");

        // Hidden: nothing is read (Review Focus 5).
        const QVariantList before = model.rows();
        model.setState(&doc, derivations(), 0, true, true, false);
        const int hiddenChanges = changes;
        doc.addJoint(Joinery::Kind::Dowel, panel, far, Joinery::defaultsFor(Joinery::Kind::Dowel, 18.0));
        model.setState(&doc, derivations(), 0, true, true, false);
        check(model.rows() == before && changes == hiddenChanges, "a hidden drawer reads nothing and says nothing");
        model.setState(&doc, derivations(), 0, true, true, true);
        check(model.rows().size() == 2 && changes == hiddenChanges + 1, "showing it reads the joints again, once");
        model.setState(&doc, derivations(), 0, true, true, true);
        check(changes == hiddenChanges + 1, "an identical state says nothing");

        model.setState(&doc, derivations(), 0, /*live=*/false, false, true);
        check(model.rows().isEmpty() && !model.live(), "at the library the drawer lists nothing");

        model.close();
        check(closes == 1, "the close x reaches the window");
        const QVariantMap w = model.words();
        check(w.value("title") == QStringLiteral("Joints") && w.value("empty") == JointRows::emptyText() &&
                  w.value("deleteTip") == JointRows::deleteTooltip() &&
                  w.value("close") == QStringLiteral("Close this panel"),
              "the drawer's words, verbatim");
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
