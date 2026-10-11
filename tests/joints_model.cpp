// What a joint's row says (JointRows) and the Joints drawer's model for the
// page (JointsModel) - migration Phase 3d, headless, on real boards and the
// real Joinery::derive().
//
// OCCT first, by the Handle() rule - DocumentModel.h carries Joinery.h.
#include "DocumentModel.h"
#include "Measure.h"
#include "session/JointRows.h"

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

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
