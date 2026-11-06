#pragma once
// THE TOAST'S RULES as state (migration Phase 7b). One outcome at a time,
// over the viewport instead of a modal dialog; the page draws it, and this
// keeps what it says and how long it lives. A second show() replaces the
// first and restarts the countdown - a stack of toasts is a dialog with
// extra steps.
//
// The page keeps its own fade, rise and hover pause (web/src/editor/
// Toast.svelte); this keeps the lifetime the suite reads (a Note 4 s, a
// Failure 8 s), the Undo stamp, and the Notes switch. Core only - no widgets.
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Toast {
enum class Kind { Note, Failure };
}

class ToastHost : public QObject
{
    Q_OBJECT

public:
    explicit ToastHost(QObject* parent = nullptr);

    // A Kind::Note is DROPPED while notesEnabled() is false; a Kind::Failure
    // is shown whatever it says. This app has no modal dialogs and no error
    // log, so a refusal that reports nowhere is a silent failure.
    //
    // `documentStamp` is the document's revision as the message was composed,
    // or -1 for a message that describes no change. A stamped toast is
    // dismissed the moment the document moves on (documentMovedTo()), so its
    // Undo can never pop a different change from the one it names.
    void show(const QString& text, Toast::Kind kind, bool undo, int documentStamp = -1);
    void documentMovedTo(int documentStamp);
    void dismiss();

    // Whether the Undo a message offers is allowed at all - pushed from the
    // window's updateActions(), the one place that decides what is available.
    void setUndoEnabled(bool enabled) { myUndoEnabled = enabled; }
    bool undoEnabled() const { return myUndoEnabled; }

    // View -> Show notifications. Silences Notes only - see show().
    void setNotesEnabled(bool enabled) { myNotesEnabled = enabled; }
    bool notesEnabled() const { return myNotesEnabled; }

    // The live message; empty / false when nothing is showing.
    QString currentText() const { return myShowing ? myText : QString(); }
    bool isShowing() const { return myShowing; }
    Toast::Kind kind() const { return myKind; }
    bool hasUndo() const { return myShowing && myUndo; }
    int stamp() const { return myStamp; }
    // The live countdown in ms, or -1 when nothing is showing.
    int remainingMs() const;

    // The Undo pill: answered only while a message offering Undo is showing
    // and Undo is allowed - the pill is not a fourth entry point.
    void requestUndo();

    // "Undo" and every message shown this run, for the banned-word sweep.
    QStringList paintedTexts() const;

    static constexpr int kNoteMs = 4000;
    static constexpr int kFailureMs = 8000;

signals:
    void shown(const QString& text, Toast::Kind kind, bool undo, int documentStamp);
    void dismissed();
    void documentMoved(int documentStamp);
    void undoRequested();
    // A Failure was shown; the window writes its reason into the status
    // line's left channel, where it outlives the toast.
    void failureShown(const QString& text);

private:
    QTimer* myTimer = nullptr;
    QString myText;
    Toast::Kind myKind = Toast::Kind::Note;
    bool myUndo = false;
    bool myShowing = false;
    int myStamp = -1;
    bool myUndoEnabled = true;
    bool myNotesEnabled = true;
    QStringList myShownTexts;
};
