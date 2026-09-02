#pragma once
//
// The one row-rename gesture, shared by every place a row carries a name a
// user can edit: the init screen's furniture card (this task) and the Items
// drawer's rows (Task 5). Rather than each owning its own QLineEdit-popping
// logic - which is exactly how two places end up with two different rules
// for what an empty commit does - this is the single implementation both
// call into.
//
#include <QRect>
#include <QString>

#include <functional>

class QWidget;

namespace InlineRename {

// Opens an inline QLineEdit over `cellRect` (in `host`'s own coordinates),
// pre-filled with `current` and with the text selected so typing replaces
// it outright. The edit is a child of `host`, shown and focused immediately;
// nothing about it needs cleaning up afterward - it destroys itself in every
// exit path below.
//
// Enter commits - QLineEdit::editingFinished() covers both that and the
// ordinary case of clicking away to something else, so a user who starts a
// rename and then clicks elsewhere gets the same result as pressing Enter
// rather than a rename silently abandoned. Escape cancels outright, through
// a WidgetWithChildrenShortcut rather than a subclass: the shortcut owns the
// key without needing a moc'd QLineEdit subclass just for one keystroke.
//
// A commit whose trimmed text is empty is refused SILENTLY - the old name
// stands, with no toast and no shake - because a rename the user backed out
// of by clearing the field is not a failure to report, it is a mind changed.
void beginRename(QWidget* host, const QRect& cellRect, const QString& current,
                  std::function<void(QString)> commit);

}  // namespace InlineRename
