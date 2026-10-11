#include "JointsModel.h"

#include "JointRows.h"

#include <QCoreApplication>

#include <algorithm>

namespace {
QVariantMap markMap(const JointRows::Mark& m)
{
    return {{QStringLiteral("at"), m.at}, {QStringLiteral("text"), m.text}};
}

QVariantMap rowMap(const JointRows::Row& r)
{
    QVariantList pieces;
    for (const JointRows::Piece& p : r.pieces) {
        QVariantList marks;
        for (const JointRows::Mark& m : p.marks) marks.push_back(markMap(m));
        pieces.push_back(QVariantMap{{QStringLiteral("name"), p.name},
                                     {QStringLiteral("depthText"), p.depthText},
                                     {QStringLiteral("band"), p.band},
                                     {QStringLiteral("marks"), marks},
                                     {QStringLiteral("bandStart"), markMap(p.bandStart)},
                                     {QStringLiteral("bandEnd"), markMap(p.bandEnd)},
                                     {QStringLiteral("writtenLine"), p.writtenLine}});
    }
    return {{QStringLiteral("id"), r.jointId},
            {QStringLiteral("broken"), r.broken},
            {QStringLiteral("selected"), r.selected},
            {QStringLiteral("expanded"), r.expanded},
            {QStringLiteral("fastener"), r.fastener},
            {QStringLiteral("edgeNamed"), r.edgeNamed},
            {QStringLiteral("nameA"), r.nameA},
            {QStringLiteral("nameB"), r.nameB},
            {QStringLiteral("kindLine"), r.kindLine},
            {QStringLiteral("caveat"), r.caveat},
            {QStringLiteral("edge"), r.edge},
            {QStringLiteral("unit"), r.unit},
            {QStringLiteral("sharedDrawn"), r.sharedDrawn},
            {QStringLiteral("sharedWritten"), r.sharedWritten},
            {QStringLiteral("pieces"), pieces}};
}
}  // namespace

JointsModel::JointsModel(Hooks hooks, QObject* parent) : QObject(parent), myHooks(std::move(hooks)) {}

QVariantMap JointsModel::words() const
{
    return {{QStringLiteral("title"), JointRows::title()},
            {QStringLiteral("empty"), JointRows::emptyText()},
            {QStringLiteral("deleteTip"), JointRows::deleteTooltip()},
            {QStringLiteral("close"), QCoreApplication::translate("PanelCloseButton", "Close this panel")}};
}

void JointsModel::setState(const DocumentModel* doc, std::vector<Joinery::Derivation> derivations,
                           int selectedJoint, bool live, bool enabled, bool shown)
{
    // A hidden drawer reads nothing (review I1 of Phase 3c): this runs on every
    // appStateChanged. Showing it reads again.
    myShown = shown;
    if (!shown) return;
    myDocument = doc;
    myDerivations = std::move(derivations);
    mySelected = selectedJoint;
    const bool flags = live != myLive || enabled != myEnabled;
    myLive = live;
    myEnabled = enabled;
    const QVariantList before = myRows;
    rebuild();
    if (flags || myRows != before) emit changed();
}

void JointsModel::rebuild()
{
    std::vector<JointRows::Row> rows;
    QSet<int> alive;
    if (myLive && myDocument) {
        const std::vector<DocumentModel::Joint> joints = myDocument->joints();
        const std::size_t count = std::min(joints.size(), myDerivations.size());
        for (std::size_t i = 0; i < count; ++i) {
            alive.insert(joints[i].id);
            rows.push_back(JointRows::content(*myDocument, joints[i], myDerivations[i], mySelected,
                                              myExpanded.contains(joints[i].id)));
        }
    }
    myExpanded.intersect(alive);
    // Broken joints first, each group in document order.
    std::stable_partition(rows.begin(), rows.end(), [](const JointRows::Row& r) { return r.broken; });
    myRows.clear();
    for (const JointRows::Row& r : rows) myRows.push_back(rowMap(r));
}

void JointsModel::activate(int jointId)
{
    if (myExpanded.contains(jointId))
        myExpanded.remove(jointId);
    else
        myExpanded.insert(jointId);
    // Selecting runs MainWindow's updateActions(), whose appStateChanged calls
    // setState() - which rebuilds with the new open set. The direct rebuild
    // covers a joint already selected, where no state moves.
    if (myHooks.select) myHooks.select(jointId);
    if (!myShown) return;
    const QVariantList before = myRows;
    rebuild();
    if (myRows != before) emit changed();
}

void JointsModel::remove(int jointId)
{
    // A hidden drawer deletes nothing, whatever the page last saw: a click in
    // flight as render mode hides the card must not reach the document
    // (review M3) - the gate is C++'s, VersionsModel's rule.
    if (myShown && myEnabled && myHooks.remove) myHooks.remove(jointId);
}

void JointsModel::close()
{
    if (myHooks.close) myHooks.close();
}
