#include "ItemsKeys.h"

#include <QCoreApplication>
#include <QStringList>

namespace ItemsKeys {

const std::vector<Key>& table()
{
    // The labels say what the KEY does, in the sheet's own register.
    static const std::vector<Key> keys = {
        {Command::Step, {QKeyCombination(Qt::Key_Up), QKeyCombination(Qt::Key_Down)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Select the row above or below"), false},
        {Command::ExtendStep,
         {QKeyCombination(Qt::ShiftModifier, Qt::Key_Up), QKeyCombination(Qt::ShiftModifier, Qt::Key_Down)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Add the row above or below to the selection"), false},
        {Command::FirstOrLast, {QKeyCombination(Qt::Key_Home), QKeyCombination(Qt::Key_End)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Select the first or the last row"), false},
        {Command::FoldOrParent, {QKeyCombination(Qt::Key_Left)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Close the folder, or go to the folder the row is in"), false},
        {Command::UnfoldOrChild, {QKeyCombination(Qt::Key_Right)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Open the folder, or go to its first row"), false},
        {Command::Rename, {QKeyCombination(Qt::Key_F2)}, QT_TRANSLATE_NOOP("ItemsPanel", "Rename the row"),
         false},
        {Command::MoveUp, {QKeyCombination(Qt::AltModifier, Qt::Key_Up)}, QT_TRANSLATE_NOOP("ItemsPanel", "Move up"),
         false},
        {Command::MoveDown, {QKeyCombination(Qt::AltModifier, Qt::Key_Down)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Move down"), false},
        {Command::MoveOutOfFolder, {QKeyCombination(Qt::AltModifier, Qt::Key_Left)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Move out of its folder"), false},
        {Command::MoveIntoFolderAbove, {QKeyCombination(Qt::AltModifier, Qt::Key_Right)},
         QT_TRANSLATE_NOOP("ItemsPanel", "Move into the folder above"), false},
        {Command::EnterList, {QKeyCombination(Qt::Key_Down), QKeyCombination(Qt::Key_Return)},
         QT_TRANSLATE_NOOP("ItemsPanel", "From the filter field, go to the list"), true},
    };
    return keys;
}

const Key* forKey(QKeyCombination combination, bool fromFilterField)
{
    // Keypad Enter is the same key as Return to the hand that presses it.
    if (combination.key() == Qt::Key_Enter)
        combination = QKeyCombination(combination.keyboardModifiers(), Qt::Key_Return);
    for (const Key& entry : table()) {
        if (entry.fromFilterField != fromFilterField) continue;
        for (const QKeyCombination& key : entry.keys)
            if (key == combination) return &entry;
    }
    return nullptr;
}

QString label(const Key& entry) { return QCoreApplication::translate("ItemsPanel", entry.label); }

QString text(const Key& entry)
{
    QStringList keys;
    for (QKeyCombination key : entry.keys) {
        // "Enter", the app's word for the key (KeyClaim's "Enter applies"),
        // never NativeText's "Return" - one key, one name on every surface.
        if (key.key() == Qt::Key_Return) key = QKeyCombination(key.keyboardModifiers(), Qt::Key_Enter);
        keys << QKeySequence(key).toString(QKeySequence::NativeText);
    }
    return keys.join(QStringLiteral(", "));
}

QKeySequence sequence(Command command)
{
    for (const Key& entry : table())
        if (entry.command == command && !entry.keys.empty()) return QKeySequence(entry.keys.front());
    return QKeySequence();
}

namespace {
struct Named {
    Command command;
    const char* name;
};
constexpr Named kNames[] = {
    {Command::Step, "step"},
    {Command::ExtendStep, "extendStep"},
    {Command::FirstOrLast, "firstOrLast"},
    {Command::FoldOrParent, "foldOrParent"},
    {Command::UnfoldOrChild, "unfoldOrChild"},
    {Command::Rename, "rename"},
    {Command::MoveUp, "moveUp"},
    {Command::MoveDown, "moveDown"},
    {Command::MoveOutOfFolder, "moveOutOfFolder"},
    {Command::MoveIntoFolderAbove, "moveIntoFolderAbove"},
    {Command::EnterList, "enterList"},
};
}  // namespace

QString commandName(Command command)
{
    for (const Named& n : kNames)
        if (n.command == command) return QString::fromLatin1(n.name);
    return QString();
}

bool commandFromName(const QString& name, Command* out)
{
    for (const Named& n : kNames) {
        if (name == QLatin1String(n.name)) {
            if (out) *out = n.command;
            return true;
        }
    }
    return false;
}

}  // namespace ItemsKeys
