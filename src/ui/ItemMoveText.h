#pragma once
// THE ONE AUTHOR of every sentence the Items drawer says about a move (T-063):
// the present tense the status bar's right channel shows while a drag is live,
// the past tense the Note says after it lands, and the reason a Failure gives.
// They read ONE DocumentModel::MoveResult, so the line under the hand and the
// toast after the drop cannot describe two different moves.
#include "DocumentModel.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace ItemMoveText {

enum class Tense { Present, Past };

// A row's own name: a folder's, an outline's or a body's. Empty when unknown.
QString nameOf(const DocumentModel& doc, int id);
// "Side left", "Drawer and Side left", "3 bodies", "2 bodies and 1 folder".
QString subject(const DocumentModel& doc, const std::vector<int>& carried);
// Present: "Drop to move 3 bodies into Doors"; Past: "Moved 3 bodies to Doors".
QString sentence(const DocumentModel& doc, const DocumentModel::MoveResult& result, Tense tense);
// The status line during a drag: the present sentence, then "— Esc cancels".
QString dragStatus(const DocumentModel& doc, const DocumentModel::MoveResult& result);
// The Failure for IntoItself or Unknown.
QString refusal(const DocumentModel& doc, const DocumentModel::MoveResult& result);
QString escHint();
QString releaseToCancel();
QString filteredDropHint();
QString noTargetHint();
// Every fixed fragment above, for the vocabulary sweep.
QStringList paintedTexts();

}  // namespace ItemMoveText
