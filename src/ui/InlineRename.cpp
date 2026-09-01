#include "InlineRename.h"

#include <QKeySequence>
#include <QLineEdit>
#include <QPointer>
#include <QShortcut>

#include <memory>

namespace InlineRename {

void beginRename(QWidget* host, const QRect& cellRect, const QString& current,
                 std::function<void(QString)> commit)
{
    if (!host) return;

    // A QPointer, not a bare QLineEdit*: `commit` is the caller's, and a
    // caller that rebuilds its rows synchronously in response to a rename
    // (InitScreen::refresh() does exactly that) can tear `host` - and so
    // `edit`, its child - down before this function's own lambda finishes
    // running. The bare pointer would then be dangling for the
    // deleteLater() call right after it.
    QPointer<QLineEdit> edit = new QLineEdit(host);
    edit->setGeometry(cellRect);
    edit->setText(current);
    edit->selectAll();
    edit->show();
    edit->raise();
    edit->setFocus(Qt::MouseFocusReason);

    // Guards against running the outcome twice: QLineEdit::editingFinished()
    // fires for BOTH Enter and an ordinary focus-out, and the Escape
    // shortcut below can itself cause a focus-out on its way to deleting the
    // widget. Whichever of the two branches runs first wins; the other is a
    // no-op rather than a second, contradictory commit.
    auto settled = std::make_shared<bool>(false);

    QObject::connect(edit, &QLineEdit::editingFinished, edit,
                     [edit, commit, settled]() mutable {
                         if (*settled || edit.isNull()) return;
                         *settled = true;
                         const QString text = edit->text().trimmed();
                         // Empty/whitespace: refused silently, old name stands.
                         if (!text.isEmpty()) commit(text);
                         if (edit) edit->deleteLater();
                     });

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), edit);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, edit, [edit, settled]() mutable {
        if (*settled || edit.isNull()) return;
        *settled = true;
        edit->deleteLater();   // no commit at all - Escape cancels outright
    });
}

}  // namespace InlineRename
