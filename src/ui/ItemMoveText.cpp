#include "ItemMoveText.h"

#include <QCoreApplication>

namespace ItemMoveText {
namespace {

QString t(const char* text) { return QCoreApplication::translate("ItemMoveText", text); }

// Where the block lands, in the order the rules are asked:
//   into a folder                            -> "into Doors" / "to Doors"
//   up out of the one folder it all came from -> "out of Carcass"
//   in front of a sibling                    -> "above Top"
//   the end of the parent it was already in  -> "to the end of Carcass"
//   the end of another folder                -> "into Doors" / "to Doors"
//   the end of the top level                 -> "to the end of the list"
QString where(const DocumentModel& doc, const DocumentModel::MoveResult& r, Tense tense)
{
    const QString parentName = nameOf(doc, r.parent);
    if (r.beforeId == DocumentModel::kIntoFolder)
        return (tense == Tense::Present ? t("into %1") : t("to %1")).arg(parentName);
    if (r.fromFolder > 0 && r.fromFolder != r.parent && doc.groupOf(r.fromFolder) == r.parent)
        return t("out of %1").arg(nameOf(doc, r.fromFolder));
    if (r.beforeId > 0) return t("above %1").arg(nameOf(doc, r.beforeId));
    if (r.fromFolder == r.parent)
        return r.parent == 0 ? t("to the end of the list") : t("to the end of %1").arg(parentName);
    if (r.parent != 0)
        return (tense == Tense::Present ? t("into %1") : t("to %1")).arg(parentName);
    return t("to the end of the list");
}

}  // namespace

QString nameOf(const DocumentModel& doc, int id)
{
    if (doc.groupExists(id)) return QString::fromStdString(doc.groupNameOf(id));
    if (doc.containsOutline(id)) return QString::fromStdString(doc.outlineNameOf(id));
    if (doc.contains(id)) return QString::fromStdString(doc.nameOf(id));
    return QString();
}

QString subject(const DocumentModel& doc, const std::vector<int>& carried)
{
    if (carried.empty()) return QString();
    if (carried.size() == 1) return nameOf(doc, carried[0]);
    int bodies = 0;
    int outlines = 0;
    int folders = 0;
    for (int id : carried) {
        if (doc.groupExists(id)) ++folders;
        else if (doc.containsOutline(id)) ++outlines;
        else ++bodies;
    }
    const int kinds = (bodies > 0 ? 1 : 0) + (outlines > 0 ? 1 : 0) + (folders > 0 ? 1 : 0);
    // Two things of different kinds are named ("Drawer and Side left"); two
    // or more of one kind are counted ("2 bodies") - the spec's own pair.
    if (kinds > 1 && carried.size() == 2)
        return t("%1 and %2").arg(nameOf(doc, carried[0]), nameOf(doc, carried[1]));
    QStringList parts;
    if (bodies > 0) parts << (bodies == 1 ? t("1 body") : t("%1 bodies").arg(bodies));
    if (outlines > 0) parts << (outlines == 1 ? t("1 outline") : t("%1 outlines").arg(outlines));
    if (folders > 0) parts << (folders == 1 ? t("1 folder") : t("%1 folders").arg(folders));
    if (parts.size() == 1) return parts.front();
    const QString last = parts.takeLast();
    return t("%1 and %2").arg(parts.join(QStringLiteral(", ")), last);
}

QString sentence(const DocumentModel& doc, const DocumentModel::MoveResult& r, Tense tense)
{
    const QString what = subject(doc, r.carried);
    const QString to = where(doc, r, tense);
    if (tense == Tense::Present) return t("Drop to move %1 %2").arg(what, to);
    QString line = t("Moved %1 %2").arg(what, to);
    if (r.twins.size() == 1) line += t(" — and 1 mirrored twin");
    else if (r.twins.size() > 1) line += t(" — and %1 mirrored twins").arg(r.twins.size());
    return line;
}

QString dragStatus(const DocumentModel& doc, const DocumentModel::MoveResult& r)
{
    return t("%1 — %2").arg(sentence(doc, r, Tense::Present), escHint());
}

QString refusal(const DocumentModel& doc, const DocumentModel::MoveResult& r)
{
    if (r.check == DocumentModel::MoveCheck::IntoItself) {
        const QString folder = nameOf(doc, r.offendingFolder);
        if (r.parent == r.offendingFolder)
            return t("%1 can't go inside itself — drop it beside it instead").arg(folder);
        return t("%1 can't go inside a folder it holds — drop it beside it instead").arg(folder);
    }
    return t("That row is no longer in the list — nothing was moved");
}

QString escHint() { return t("Esc cancels"); }
QString releaseToCancel() { return t("Release to cancel"); }
QString filteredDropHint() { return t("Drop on a folder while the list is filtered — Esc cancels"); }
QString noTargetHint() { return t("Drop on a row, a folder or the Items title — Esc cancels"); }
QString filteredReorderRefusal()
{
    return t("Clear the filter to move rows up or down — it is hiding where they would land");
}

QString filteredIntoRefusal()
{
    return t("Clear the filter to move rows into a folder — it is hiding where they would land");
}
QString noFolderAboveRefusal()
{
    return t("Nothing to move into — the row above is not a folder");
}

QStringList paintedTexts()
{
    return {t("into %1"), t("to %1"), t("out of %1"), t("above %1"),
            t("to the end of the list"), t("to the end of %1"), t("%1 and %2"), t("1 body"),
            t("%1 bodies"), t("1 outline"), t("%1 outlines"), t("1 folder"), t("%1 folders"),
            t("Drop to move %1 %2"), t("Moved %1 %2"), t(" — and 1 mirrored twin"),
            t(" — and %1 mirrored twins"),
            t("%1 can't go inside itself — drop it beside it instead"),
            t("%1 can't go inside a folder it holds — drop it beside it instead"),
            t("That row is no longer in the list — nothing was moved"), escHint(),
            releaseToCancel(), filteredDropHint(), noTargetHint(), filteredReorderRefusal(),
            filteredIntoRefusal(), noFolderAboveRefusal()};
}

}  // namespace ItemMoveText
