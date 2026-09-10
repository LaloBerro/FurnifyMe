#pragma once
// The ONE implementation of the application-wide Enter/Escape claim that
// every modeless chip with a live gesture installs (ExtrudePreview,
// PullArrow, BevelArrow, MoveTool - the branch review found the four
// hand-kept copies this replaces). The shape is ExtrudePreview's, and it is
// not optional: the press that starts a drag or an orbit goes to the
// viewport, which takes focus, so a filter on the chip's own field stops
// working at exactly the moment it is needed - the claim must be
// application-wide, and must answer QEvent::ShortcutOverride as well as the
// KeyPress or QShortcutMap takes the key first.
//
// What stays at the CALL SITE is what the keys DO - commit() and cancel()
// are each chip's own - and the visible-gate is read off the owner here so
// a hidden chip can never claim a key.
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QWidget>

namespace KeyClaim {

// Runs the shared half of the claim for `owner`'s filter. Returns true when
// the event belongs to the claim and the caller's eventFilter() must return
// true; on the KeyPress half *pressedKey receives the key so the caller
// acts on it (it stays 0 on the ShortcutOverride half, which only accepts
// the event to claim the key back from QShortcutMap).
//
// `wantEnter`: whether Return/Enter is claimed alongside Escape - true for
// the chips whose field commits on Enter, false for a chip with no field
// half (MoveTool).
//
// `exemptLineEdits`: never steal a keystroke out of a focused text field.
// TRUE for a chip with no field of its own, whose Escape must not outrank
// "abandon the rename" in an InlineRename field; FALSE for the chips that
// own a field - Enter IN that field is exactly what the claim exists to
// route to commit().
inline bool claim(const QWidget* owner, QObject* watched, QEvent* event, bool wantEnter,
                  bool exemptLineEdits, int* pressedKey)
{
    *pressedKey = 0;
    if (!owner->isVisible()) return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) return false;

    // Application-wide means every window in this process - gui_smoke builds
    // several at once - so only keys headed for the owner's own window count.
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != owner->window()) return false;
    if (exemptLineEdits && qobject_cast<QLineEdit*>(widget)) return false;

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier) return false;

    const int key = keyEvent->key();
    const bool commits = wantEnter && (key == Qt::Key_Return || key == Qt::Key_Enter);
    const bool cancels = key == Qt::Key_Escape;
    if (!commits && !cancels) return false;

    if (type == QEvent::ShortcutOverride) {
        event->accept();   // claims the key back from QShortcutMap
        return true;
    }
    *pressedKey = key;
    return true;
}

}  // namespace KeyClaim
