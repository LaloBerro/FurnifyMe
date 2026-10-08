#include "ItemsFilterField.h"

#include "Theme.h"

#include <QKeyEvent>
#include <QPalette>

ItemsFilterField::ItemsFilterField(QWidget* parent) : QLineEdit(parent)
{
    setPlaceholderText(placeholder());
    setAccessibleName(placeholder());
    setFocusPolicy(Qt::StrongFocus);
    setClearButtonEnabled(false);
    // Restyled only when the field goes IN or OUT of use: a stylesheet is
    // re-polished on every set, and a keystroke must stay inside its budget.
    connect(this, &QLineEdit::textChanged, this, [this] {
        if (inUse() != myInUse) applyTheme();
    });
    applyTheme();
}

QString ItemsFilterField::placeholder() { return tr("Filter by name"); }

void ItemsFilterField::applyTheme()
{
    myInUse = inUse();
    // The underline is a thin stroke marking a field that holds WORDS, so it
    // wears accentText() rather than the accent fill (Global Constraint 21 and
    // the spec's contrast rule - ruling P7), and only while the field is in
    // use; at rest a quiet chipActive rule says "this is a field".
    const QColor rule = myInUse ? Theme::accentText() : Theme::chipActive();
    setStyleSheet(QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; "
                                 "border-bottom: 2px solid %2; padding: 2px 6px; "
                                 "font-size: %3pt; }")
                      .arg(Theme::text().name(), rule.name())
                      .arg(Theme::bodyFont().pointSizeF()));
    QPalette p = palette();
    p.setColor(QPalette::PlaceholderText, Theme::textMuted());
    setPalette(p);
}

bool ItemsFilterField::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape &&
            (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            // A query to clear: Escape is the field's.
            if (!text().isEmpty()) {
                event->accept();
                return true;
            }
            // Nothing to clear (ruling R7c): the field lets go of the focus
            // and does NOT claim the key, so whatever owns Escape elsewhere -
            // Cancel Sketch, a live gesture - still gets it. An empty field
            // that swallowed Escape would leave the keyboard parked in it.
            releaseFocus();
        }
    }
    return QLineEdit::event(event);
}

void ItemsFilterField::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (text().isEmpty()) {
            // Reached only when no shortcut took the key: let go of the focus
            // and let the press travel on to the widgets behind this one.
            releaseFocus();
            event->ignore();
            return;
        }
        emit escapePressed();
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void ItemsFilterField::releaseFocus()
{
    // The WINDOW's focus widget, not hasFocus(): hasFocus() is false in any
    // window that is not the active one, and the focus it would leave parked
    // here is still the window's.
    if (window()->focusWidget() != this) return;
    // Back to the nearest enclosing widget that takes focus. That is the
    // Items list itself (ItemsPanel, Qt::ClickFocus), not the viewport: the
    // list holds the keys a moment after, and Escape - which this field did
    // not claim - goes on to the shortcut map and whatever owns it there.
    for (QWidget* up = parentWidget(); up; up = up->parentWidget()) {
        if (up->focusPolicy() != Qt::NoFocus && up->isEnabled() && up->isVisible()) {
            up->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    clearFocus();
}

void ItemsFilterField::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    if (inUse() != myInUse) applyTheme();
}

void ItemsFilterField::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    if (inUse() != myInUse) applyTheme();
}
