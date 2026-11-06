#include "StatusLine.h"

void StatusLine::setMessage(const QString& message)
{
    if (myMessage == message) return;
    myMessage = message;
    emit changed();
}

void StatusLine::setState(const QString& state)
{
    if (myState == state) return;
    myState = state;
    emit changed();
}

void StatusLine::setName(const QString& name, bool unsaved)
{
    const bool dot = unsaved && !name.isEmpty();
    if (myName == name && myUnsaved == dot) return;
    myName = name;
    myUnsaved = dot;
    emit changed();
}
