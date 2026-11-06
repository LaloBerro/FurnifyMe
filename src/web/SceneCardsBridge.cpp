#include "SceneCardsBridge.h"

#include "AddPieceCard.h"
#include "KeyClaim.h"

#include <QAction>

// --- the pieces card ----------------------------------------------------------

QVariantMap ScenePiecesModel::state() const
{
    QVariantList rows;
    for (const ScenePiecesPanel::Row& row : myRows) {
        rows.append(QVariantMap{{QStringLiteral("id"), row.pieceId},
                                {QStringLiteral("name"), row.name},
                                {QStringLiteral("visible"), row.visible},
                                {QStringLiteral("reason"), row.reason},
                                {QStringLiteral("selected"), row.pieceId == mySelected},
                                {QStringLiteral("armed"), row.pieceId == myArmed}});
    }
    QString addText = myAdd ? myAdd->text() : QString();
    addText.remove(QLatin1Char('&'));
    return {{QStringLiteral("title"), ScenePiecesPanel::titleText()},
            {QStringLiteral("empty"), ScenePiecesPanel::emptyText()},
            {QStringLiteral("add"), QVariantMap{{QStringLiteral("text"), addText},
                                                {QStringLiteral("tip"), myAdd ? myAdd->toolTip() : QString()},
                                                {QStringLiteral("enabled"), myAdd && myAdd->isEnabled()}}},
            {QStringLiteral("words"), QVariantMap{{QStringLiteral("removeTip"), ScenePiecesPanel::removeTipText()},
                                                  {QStringLiteral("armed"), ScenePiecesPanel::armedRemoveText()},
                                                  {QStringLiteral("armedTip"), ScenePiecesPanel::armedRemoveTipText()},
                                                  {QStringLiteral("eyeTip"), ScenePiecesPanel::eyeTipText()}}},
            {QStringLiteral("rows"), rows}};
}

void ScenePiecesModel::announce()
{
    const QVariantMap now = state();
    if (now == myLast) return;
    myLast = now;
    emit stateChanged(now);
}

void ScenePiecesModel::setRows(const QVector<ScenePiecesPanel::Row>& rows)
{
    myRows = rows;
    // An armed row that left the list is no longer armed.
    if (myArmed && !hasRow(myArmed)) myArmed = 0;
    announce();
}

void ScenePiecesModel::setSelected(int pieceId)
{
    if (mySelected == pieceId) return;
    mySelected = pieceId;
    announce();
}

void ScenePiecesModel::setAddAction(QAction* action)
{
    myAdd = action;
    if (action) connect(action, &QAction::changed, this, &ScenePiecesModel::announce);
    announce();
}

bool ScenePiecesModel::hasRow(int pieceId) const
{
    for (const ScenePiecesPanel::Row& row : myRows)
        if (row.pieceId == pieceId) return true;
    return false;
}

void ScenePiecesModel::select(int pieceId)
{
    if (hasRow(pieceId)) emit selectionRequested(pieceId);
}

void ScenePiecesModel::rename(int pieceId, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || !hasRow(pieceId)) return;
    emit renameCommitted(pieceId, trimmed);
}

void ScenePiecesModel::toggleVisible(int pieceId, bool visible)
{
    if (hasRow(pieceId)) emit visibilityToggled(pieceId, visible);
}

void ScenePiecesModel::remove(int pieceId)
{
    if (!hasRow(pieceId)) return;
    if (myArmed != pieceId) {
        // ASKS FIRST, as the Qt card: nothing is taken out until the second
        // click, and arming one row disarms any other.
        myArmed = pieceId;
        announce();
        return;
    }
    myArmed = 0;
    announce();
    emit removeRequested(pieceId);
}

void ScenePiecesModel::disarm()
{
    if (!myArmed) return;
    myArmed = 0;
    announce();
}

void ScenePiecesModel::addPiece()
{
    if (myAdd && myAdd->isEnabled()) myAdd->trigger();
}

// --- the add card -------------------------------------------------------------

QVariantMap AddPieceModel::state() const
{
    QVariantList entries;
    for (const FurnitureStore::FurnitureInfo& info : myEntries)
        entries.append(QVariantMap{{QStringLiteral("id"), info.id}, {QStringLiteral("name"), info.name}});
    return {{QStringLiteral("shown"), myShown},
            {QStringLiteral("seed"), mySeed},
            {QStringLiteral("title"), AddPieceCard::titleText()},
            {QStringLiteral("empty"), AddPieceCard::emptyLibraryText()},
            {QStringLiteral("library"), AddPieceCard::libraryButtonText()},
            {QStringLiteral("keyHint"), KeyClaim::closeHintText()},
            {QStringLiteral("entries"), entries}};
}

void AddPieceModel::announce()
{
    const QVariantMap now = state();
    if (now == myLast) return;
    myLast = now;
    emit stateChanged(now);
}

void AddPieceModel::showFor(const QVector<FurnitureStore::FurnitureInfo>& furniture)
{
    myEntries = furniture;
    myShown = true;
    ++mySeed;
    announce();
}

void AddPieceModel::setLibraryAction(QAction* action)
{
    myLibrary = action;
}

void AddPieceModel::choose(const QString& furnitureId)
{
    if (!myShown) return;
    bool listed = false;
    for (const FurnitureStore::FurnitureInfo& info : myEntries)
        listed = listed || info.id == furnitureId;
    if (!listed) return;
    // Closed BEFORE it reports, so whatever the choice puts on screen lands
    // on a clear viewport - the Qt card's order.
    myShown = false;
    announce();
    emit chosen(furnitureId);
}

void AddPieceModel::goToLibrary()
{
    close();
    if (myLibrary && myLibrary->isEnabled()) myLibrary->trigger();
}

void AddPieceModel::close()
{
    if (!myShown) return;
    myShown = false;
    announce();
}
