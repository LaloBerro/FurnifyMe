#include "JointRows.h"

#include "Measure.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace JointRows {

namespace {
QString tr(const char* text) { return QCoreApplication::translate("JointsPanel", text); }
}  // namespace

QString lengthText(double mm) { return QString::fromStdString(Measure::formatLength(mm)); }

// Measure's own number with its unit taken off - a tick label, whose unit the
// shared line names once. Still Measure's rounding, separator and display unit:
// this removes the suffix Measure appended, it formats nothing itself.
QString numberOnly(double mm)
{
    QString text = lengthText(mm);
    const QString suffix = QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (text.endsWith(suffix)) text.chop(suffix.size());
    return text;
}

QString title() { return tr("Joints"); }

QString emptyText()
{
    return tr("No joints yet.\n\nSelect two pieces that meet and press J to "
              "plan one.");
}

QString deleteTooltip() { return tr("Delete this joint\nOne undo brings it back."); }

// A derivation's refusal, as the row says it. Joinery's own reasons are written
// for the user; a kernel exception's text is written for a log, and CLAUDE.md
// never shows one - it gets the plain sentence instead, and the sentence says
// what to do: the throw is OCCT's near-tangent weak spot, which a piece moved
// to meet the other squarely steps out of.
//
// EVERY REASON ENDS WITH A NEXT STEP (the 2026-10 review's F24). Joinery's own
// sentences stay Joinery's (the headless suite pins them, and the geometry
// library has no business writing UI copy); the step is added here, after an
// em dash, worded so it reads the same under a broken row and in a placement
// refusal.
QString reasonText(const std::string& error)
{
    if (error.rfind("contact:", 0) == 0)
        return tr("these two pieces can't be measured where they meet — move one so "
                  "they meet squarely");

    const QString reason = QString::fromStdString(error);
    QString step;
    if (error == "one of the pieces is missing")
        step = tr("undo to bring it back, or delete the joint");
    else if (error == "these two pieces don't meet")
        step = tr("move the pieces so they meet");
    else if (error == "the pieces aren't crossing")
        step = tr("move one so it crosses the other");
    else if (error == "the pieces overlap rather than meet")
        step = tr("move one so they touch without overlapping");
    else if (error == "the contact is too small for a joint")
        step = tr("move them so more of the two faces touch");
    else if (error.rfind("the contact is too narrow", 0) == 0)
        step = tr("choose another kind, or move them so more of the two faces touch");
    else if (error.find("curved region") != std::string::npos)
        step = tr("move them so they meet on flat faces");
    else
        // A reason this mapping has not met yet still gets a way forward
        // rather than a dead end - the step every contact refusal shares.
        step = tr("move the pieces so they meet squarely");
    return tr("%1 — %2").arg(reason, step);
}

namespace {
Mark markAt(double mm, double run)
{
    Mark m;
    m.mm = mm;
    m.at = run > 0.0 ? std::clamp(mm, 0.0, run) / run : 0.0;
    m.text = numberOnly(mm);
    return m;
}
}  // namespace

Row content(const DocumentModel& doc, const DocumentModel::Joint& joint, const Joinery::Derivation& d,
            int selectedJoint, bool expanded)
{
    Row m;
    m.jointId = joint.id;
    m.kind = static_cast<int>(joint.kind);
    m.broken = !d.ok;
    m.selected = joint.id == selectedJoint;
    m.expanded = expanded;
    const Joinery::Family family = Joinery::familyOf(joint.kind);
    m.fastener = family == Joinery::Family::Fasteners;
    m.nameA = QString::fromStdString(doc.nameOf(joint.bodyA));
    m.nameB = QString::fromStdString(doc.nameOf(joint.bodyB));
    m.unit = QString::fromStdString(Measure::unitSuffix());
    const QString kind = QString::fromStdString(Joinery::kindName(joint.kind));

    if (!d.ok) {
        // No readout, no numbers, no caveat - a derivation that refused carries
        // none of them, and nothing here reaches back for the last good one.
        m.kindLine = tr("Broken — %1").arg(reasonText(d.error));
        return m;
    }

    const Joinery::Readout& r = d.readout;
    m.run = d.contact.runLength();
    m.inset = r.insetMm;
    m.depthA = r.depthAMm;
    m.depthB = r.depthBMm;
    m.width = r.widthMm;
    m.edge = QString::fromStdString(r.referenceEdgeA);
    m.edgeNamed = r.referenceEdgeNamed;
    // THE caveat channel, kind and contact together - not regionShortfallCaveat()
    // directly: a housing or a mortise on a contact that names no end-on piece
    // carries a caveat of its own, and asking the one composed function is what
    // keeps this row and the placement message showing the same set.
    m.caveat = QString::fromStdString(Joinery::caveatsFor(joint.kind, d.contact));

    if (m.fastener) {
        const double size = d.items.empty() ? joint.params.sizeMm : d.items.front().sizeMm;
        m.kindLine = tr("%1, %2 × %3").arg(kind, QString::number(d.items.size()), lengthText(size));
        // Both pieces are drilled; a piece with no depth has nothing to mark.
        const auto drilled = [&](const QString& name, double depth) {
            Piece piece;
            piece.name = name;
            piece.depthText = tr("drill %1").arg(lengthText(depth));
            for (double mm : r.alongMm) piece.marks.push_back(markAt(mm, m.run));
            m.pieces.push_back(piece);
        };
        if (r.depthAMm > 0.0) drilled(m.nameA, r.depthAMm);
        if (r.depthBMm > 0.0) drilled(m.nameB, r.depthBMm);
    } else {
        m.kindLine = tr("%1, %2 × %3").arg(kind, numberOnly(r.widthMm), lengthText(r.depthAMm));
        // The band is the item's own span along the run: the channel for a
        // housing, the tenon between its shoulders, the whole lap.
        const bool alongU = d.contact.runsAlongU();
        const double centre = r.alongMm.empty() ? m.run / 2.0 : r.alongMm.front();
        const double span =
            d.items.empty() ? m.run : (alongU ? d.items.front().spanUMm : d.items.front().spanVMm);
        const auto cut = [&](const QString& name, double depth) {
            Piece piece;
            piece.name = name;
            piece.depthText = tr("%1 × %2").arg(numberOnly(r.widthMm), lengthText(depth));
            piece.band = true;
            piece.bandStart = markAt(centre - span / 2.0, m.run);
            piece.bandEnd = markAt(centre + span / 2.0, m.run);
            m.pieces.push_back(piece);
        };
        if (r.depthAMm > 0.0) cut(m.nameA, r.depthAMm);
        // A housing's housed piece is not cut (depthB 0) and gets no strip.
        if (r.depthBMm > 0.0) cut(m.nameB, r.depthBMm);
    }

    // The numbers each piece carries, and the line a row writes when it cannot
    // draw a ruler: no single edge to put at zero, or labels that collide even
    // staggered. The bare numbers keep Measure's form; the band keeps its unit.
    for (Piece& piece : m.pieces) {
        if (piece.band) {
            piece.numbers = tr("%1 to %2").arg(numberOnly(piece.bandStart.mm), lengthText(piece.bandEnd.mm));
        } else {
            QStringList parts;
            for (const Mark& mark : piece.marks) parts << mark.text;
            piece.numbers = parts.join(QStringLiteral(" · ")) + QLatin1Char(' ') + m.unit;
        }
        piece.writtenLine = m.edgeNamed ? tr("from the %1 edge: %2").arg(m.edge, piece.numbers) : piece.numbers;
    }

    // The numbers this row carries, whether or not it is open - "a broken joint
    // has none" reads this, so it must not depend on a click.
    QStringList readout;
    for (const Piece& piece : m.pieces) readout << piece.depthText << piece.numbers;
    readout << m.edge;
    const QString inset = tr("inset %1 from the face").arg(lengthText(m.inset));
    if (m.fastener) readout << inset;
    m.readoutText = readout.join(QLatin1Char('\n'));

    // The one line shared by both pieces: under the rulers it names the unit
    // and the edge; under written numbers the edge is already on every line,
    // or its absence already explained, so only the inset is left.
    m.sharedDrawn = m.fastener ? tr("%1 from the %2 edge · %3").arg(m.unit, m.edge, inset)
                               : tr("%1 from the %2 edge").arg(m.unit, m.edge);
    m.sharedWritten = m.fastener ? inset : QString();
    return m;
}

}  // namespace JointRows
