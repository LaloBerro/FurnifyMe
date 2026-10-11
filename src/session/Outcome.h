#pragma once

#include <QString>

// One thing the app has to tell the user, as a value. EditorSession emits it;
// MainWindow renders it as a toast or a status line, and the HTML layer will
// render the same value. Kinds follow CLAUDE.md's taxonomy: a Note reports a
// success and may be silenced, a Failure carries a reason and never is, and a
// Status is a line for the status bar's left channel alone.
struct Outcome
{
    enum class Kind { Note, Failure, Status };
    Kind kind = Kind::Status;
    QString text;
    bool offersUndo = false;
    int documentStamp = -1;
    // A Note normally also leaves its sentence on the status bar; the one raw
    // toast that never did keeps not doing so (reportToastOnly()).
    bool writesStatus = true;
};
