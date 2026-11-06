#pragma once
// THE STATUS LINE as state (migration Phase 7b): what the editor's and the
// scene window's bottom strip says, held here rather than in a QStatusBar and
// three widgets beside it. The page's strip draws it; the suite reads it.
//
// Two channels, one job each (the 2026-10 review's F9): the LEFT is what is
// selected and what just happened; the RIGHT is the mode and the next step,
// never a count. Between them, the open furniture's (or scene's) name and a
// dot while it has unsaved changes. Core only - no widgets.
#include <QObject>
#include <QString>

class StatusLine : public QObject
{
    Q_OBJECT

public:
    explicit StatusLine(QObject* parent = nullptr) : QObject(parent) {}

    // The left channel - QStatusBar::showMessage()/currentMessage()'s place.
    QString message() const { return myMessage; }
    void setMessage(const QString& message);
    void clearMessage() { setMessage(QString()); }

    // The right channel, whole (the strip elides it for itself).
    QString state() const { return myState; }
    void setState(const QString& state);

    // The name between them, and its unsaved dot. Empty with nothing open.
    QString name() const { return myName; }
    bool unsaved() const { return myUnsaved; }
    void setName(const QString& name, bool unsaved);

signals:
    // Any of the three moved.
    void changed();

private:
    QString myMessage;
    QString myState;
    QString myName;
    bool myUnsaved = false;
};
