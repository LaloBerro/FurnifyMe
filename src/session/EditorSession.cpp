#include "session/EditorSession.h"

namespace {
Outcome make(Outcome::Kind kind, const QString& text, bool offersUndo, int stamp, bool writesStatus)
{
    Outcome o;
    o.kind = kind;
    o.text = text;
    o.offersUndo = offersUndo;
    o.documentStamp = stamp;
    o.writesStatus = writesStatus;
    return o;
}
}

EditorSession::EditorSession(QObject* parent)
    : QObject(parent)
{
}

void EditorSession::checkpoint()
{
    emit aboutToChangeDocument();
    myDocument.checkpoint();
}

bool EditorSession::undo() { return myDocument.undo(); }
bool EditorSession::redo() { return myDocument.redo(); }

QString EditorSession::undoneStatusText(int bodies)
{
    return bodies == 1 ? tr("Undone — 1 body in the document")
                       : tr("Undone — %1 bodies in the document").arg(bodies);
}

QString EditorSession::redoneStatusText(int bodies)
{
    return bodies == 1 ? tr("Redone — 1 body in the document")
                       : tr("Redone — %1 bodies in the document").arg(bodies);
}

void EditorSession::reportNote(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Note, text, offersUndo, documentStamp, true));
}

void EditorSession::reportToastOnly(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Note, text, offersUndo, documentStamp, false));
}

void EditorSession::reportFailure(const QString& text, bool offersUndo, int documentStamp)
{
    emit reported(make(Outcome::Kind::Failure, text, offersUndo, documentStamp, false));
}

void EditorSession::reportStatus(const QString& text)
{
    emit reported(make(Outcome::Kind::Status, text, false, -1, true));
}

QString EditorSession::statusLineFor(const QString& sentence)
{
    QString line = sentence.trimmed();
    while (line.endsWith(QLatin1Char('.'))) line.chop(1);
    return line;
}
