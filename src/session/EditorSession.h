#pragma once

// The editor session (migration Phase 1, slice A): the document and
// everything about it that is not a widget. QtCore and QtGui only - the
// furnify_session target links nothing from Widgets, so a widget include here
// fails to compile. MainWindow owns one and renders what it reports; the HTML
// layer will subscribe to the same signals.

#include "DocumentModel.h"
#include "session/ActionRegistry.h"
#include "session/Outcome.h"

#include <QObject>

class EditorSession : public QObject
{
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    DocumentModel& document() { return myDocument; }
    const DocumentModel& document() const { return myDocument; }
    // Every command by id - see ActionRegistry.
    ActionRegistry& actions() { return myActions; }

    // The undo checkpoint every document change takes. Emits
    // aboutToChangeDocument() FIRST - "any document-changing action leaves
    // render mode first" is a listener on that signal, and the order is the
    // contract - then checkpoints.
    void checkpoint();
    bool undo();
    bool redo();
    // The status line an undo or a redo leaves, plural written out.
    static QString undoneStatusText(int bodies);
    static QString redoneStatusText(int bodies);

    // THE outcome channel. Every toast and every status line the editor
    // writes goes through one of these and arrives as one reported() signal,
    // delivered before the call returns.
    //
    // A Note that also leaves its sentence on the status bar (the ordinary
    // success report).
    void reportNote(const QString& text, bool offersUndo = false, int documentStamp = -1);
    // A Note that does NOT touch the status bar - the one raw toast that never
    // did, kept exactly as it was.
    void reportToastOnly(const QString& text, bool offersUndo = false, int documentStamp = -1);
    void reportFailure(const QString& text, bool offersUndo = false, int documentStamp = -1);
    void reportStatus(const QString& text);
    // Status-bar copy takes no trailing period; a toast's sentence may end in
    // one, and the bar is where the same words have to read as a status line.
    static QString statusLineFor(const QString& sentence);

signals:
    void reported(const Outcome& outcome);
    void aboutToChangeDocument();

private:
    DocumentModel myDocument;
    ActionRegistry myActions{this};
};
