#include "InlineRename.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPointer>
#include <QShortcut>

#include <memory>

namespace InlineRename {
namespace {

// QLineEdit's own Return/Enter handling does not consume the key event - it
// fires returnPressed()/editingFinished() and then lets the event carry on
// propagating to the parent widget, which is why "Enter in a text field
// inside a dialog also activates the dialog's default button" is ordinary
// Qt behaviour rather than a bug anyone fixed. Here the parent is a card
// whose OWN keyPressEvent binds Return to "open" (see InitCardWidget in
// InitScreen.cpp) - so committing a rename with Enter would, one event
// later, also open the very card being renamed. An event filter installed
// on the edit intercepts Return/Enter BEFORE QLineEdit ever sees it, runs
// the commit directly, and swallows the event outright (returns true) so
// nothing downstream - not QLineEdit's own signals, not the parent - gets
// a look at it. A plain QObject: eventFilter() overrides a virtual QObject
// already declares, which needs no Q_OBJECT/moc of its own.
class ReturnSwallower : public QObject {
public:
    ReturnSwallower(std::function<void()> onReturn, QObject* parent)
        : QObject(parent), myOnReturn(std::move(onReturn))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                myOnReturn();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> myOnReturn;
};

}  // namespace

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

    auto doCommit = [edit, commit, settled]() mutable {
        if (*settled || edit.isNull()) return;
        *settled = true;
        const QString text = edit->text().trimmed();
        // Empty/whitespace: refused silently, old name stands.
        if (!text.isEmpty()) commit(text);
        if (edit) edit->deleteLater();
    };

    // Enter, via the swallower above - it calls doCommit() itself and never
    // lets QLineEdit's own Return handling run at all.
    edit->installEventFilter(new ReturnSwallower(doCommit, edit));

    // An ordinary focus-out (clicking away) - the Enter case above never
    // reaches this signal, since the filter swallows that key event before
    // QLineEdit can raise it from its own Return handling.
    QObject::connect(edit, &QLineEdit::editingFinished, edit, doCommit);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), edit);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, edit, [edit, settled]() mutable {
        if (*settled || edit.isNull()) return;
        *settled = true;
        edit->deleteLater();   // no commit at all - Escape cancels outright
    });
}

}  // namespace InlineRename
