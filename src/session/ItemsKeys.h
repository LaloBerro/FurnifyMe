#pragma once
// THE ITEMS LIST'S KEYS, in ONE table (ruling D19 of T-063, moved here in
// Phase 3h so the HTML drawer reads it too). Every reader looks a key up here:
// the Qt drawer's claimsKey() and keyPressEvent(), its filter field, the row
// menu's shortcut column, the ShortcutSheet's "Items list" group, ItemsModel
// (which sends it to the page) and the editor chrome's list hold (which claims
// exactly these keys while the page's list is focused). So a key the list
// answers is a key the sheet lists, and neither can be written twice.
// Core + Gui only.
#include <QKeyCombination>
#include <QKeySequence>
#include <QString>

#include <vector>

namespace ItemsKeys {

enum class Command {
    Step,                 // Up / Down
    ExtendStep,           // Shift+Up / Shift+Down
    FirstOrLast,          // Home / End
    FoldOrParent,         // Left
    UnfoldOrChild,        // Right
    Rename,               // F2, over a live row only
    MoveUp,               // Alt+Up
    MoveDown,             // Alt+Down
    MoveOutOfFolder,      // Alt+Left
    MoveIntoFolderAbove,  // Alt+Right (ruling D1)
    EnterList,            // Down / Enter in the filter field (ruling D2)
};

struct Key {
    Command command;
    std::vector<QKeyCombination> keys;
    const char* label;          // untranslated; label() translates
    bool fromFilterField;       // answered by the filter field, not the list
};

// One row of the ShortcutSheet's "Items list" group per entry, in this order.
const std::vector<Key>& table();
// The entry a key belongs to, in the list's scope or the filter field's -
// null when it is not one of the drawer's keys. `combination` without Keypad;
// keypad Enter is read as Return.
const Key* forKey(QKeyCombination combination, bool fromFilterField);
// The entry's label, translated (tr context "ItemsPanel").
QString label(const Key& entry);
// "Up, Down" - every combination, in the platform's own spelling, Return
// written "Enter" (the app's one name for the key).
QString text(const Key& entry);
// The first combination of `command`, for a menu's shortcut column.
QKeySequence sequence(Command command);
// A stable name for each command, for the page (ItemsModel::keys).
QString commandName(Command command);
bool commandFromName(const QString& name, Command* out);

}  // namespace ItemsKeys
