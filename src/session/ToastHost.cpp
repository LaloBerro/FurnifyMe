#include "ToastHost.h"

#include <QTimer>

ToastHost::ToastHost(QObject* parent) : QObject(parent), myTimer(new QTimer(this))
{
    myTimer->setSingleShot(true);
    connect(myTimer, &QTimer::timeout, this, &ToastHost::dismiss);
}

void ToastHost::show(const QString& text, Toast::Kind kind, bool undo, int documentStamp)
{
    // Dropped rather than shown-and-hidden: no timer is armed and the stamp is
    // left alone, so a live message the document is about to dismiss is not
    // replaced by one that was never shown.
    if (kind == Toast::Kind::Note && !myNotesEnabled) return;
    myText = text;
    myKind = kind;
    myUndo = undo;
    myStamp = documentStamp;
    myShowing = true;
    if (!myShownTexts.contains(text)) myShownTexts << text;
    myTimer->start(kind == Toast::Kind::Failure ? kFailureMs : kNoteMs);
    emit shown(text, kind, undo, documentStamp);
    if (kind == Toast::Kind::Failure) emit failureShown(text);
}

void ToastHost::documentMovedTo(int documentStamp)
{
    emit documentMoved(documentStamp);
    if (myStamp < 0 || documentStamp == myStamp) return;
    myStamp = -1;
    if (myShowing) dismiss();
}

void ToastHost::dismiss()
{
    myTimer->stop();
    if (!myShowing) return;
    myShowing = false;
    emit dismissed();
}

int ToastHost::remainingMs() const
{
    return myShowing ? myTimer->remainingTime() : -1;
}

void ToastHost::requestUndo()
{
    if (!myShowing || !myUndo || !myUndoEnabled) return;
    emit undoRequested();
}

QStringList ToastHost::paintedTexts() const
{
    QStringList texts{tr("Undo")};
    texts << myShownTexts;
    return texts;
}
