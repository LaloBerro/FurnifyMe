#include "MenuModel.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

MenuModel::MenuModel(std::vector<Node> menus, QObject* parent)
    : QObject(parent)
    , myMenus(std::move(menus))
{
    // Zero-interval and single-shot: every action change in one turn restarts
    // the same pending timeout, so the turn ends in one changed() - the
    // ActionWatch rule. updateActions() touches every action on every state
    // change; a menu that re-announced each one would re-send its whole open
    // state dozens of times a click.
    myPending.setSingleShot(true);
    myPending.setInterval(0);
    connect(&myPending, &QTimer::timeout, this, [this] {
        if (isOpen()) emit changed();
    });
    for (const Node& menu : myMenus) watch(menu.children);
}

void MenuModel::watch(const std::vector<Node>& nodes)
{
    for (const Node& node : nodes) {
        if (node.action) connect(node.action, &QAction::changed, &myPending, qOverload<>(&QTimer::start));
        watch(node.children);
    }
}

// ---- Mnemonics, QMenu's way: "&&" is a literal ampersand, "&x" marks x. ----

int MenuModel::mnemonicIndex(const QString& text)
{
    int plain = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('&')) {
                ++i;   // a literal '&', one plain character
            } else {
                return plain;
            }
        }
        ++plain;
    }
    return -1;
}

QChar MenuModel::mnemonicOf(const QString& text)
{
    const int at = mnemonicIndex(text);
    if (at < 0) return QChar();
    return plainText(text).at(at).toLower();
}

QString MenuModel::plainText(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&') && i + 1 < text.size()) {
            ++i;   // drop the marker; a doubled one keeps the second '&'
        }
        out += text.at(i);
    }
    return out;
}

// ---- What can be chosen ---------------------------------------------------

bool MenuModel::visible(const Node& node)
{
    if (node.separator) return true;
    return !node.action || node.action->isVisible();
}

bool MenuModel::live(const Node& node)
{
    if (node.separator || !node.action) return false;
    return node.action->isVisible() && node.action->isEnabled();
}

int MenuModel::step(const Level& level, int from, int delta) const
{
    const int n = int(level.entries.size());
    if (n == 0) return -1;
    int i = from;
    for (int tries = 0; tries < n; ++tries) {
        i = (i + delta + n) % n;
        if (live(*level.entries[std::size_t(i)])) return i;
    }
    return -1;
}

int MenuModel::firstLive(const Level& level) const { return step(level, -1, +1); }

int MenuModel::lastLive(const Level& level) const
{
    return step(level, int(level.entries.size()), -1);
}

// ---- The open state, for the page -----------------------------------------

QVariantList MenuModel::titles() const
{
    QVariantList list;
    for (const Node& menu : myMenus) {
        QVariantMap t;
        t.insert(QStringLiteral("title"), plainText(menu.title));
        t.insert(QStringLiteral("mnemonicAt"), mnemonicIndex(menu.title));
        t.insert(QStringLiteral("enabled"), !menu.action || menu.action->isEnabled());
        list.push_back(t);
    }
    return list;
}

int MenuModel::menuForMnemonic(QChar letter) const
{
    const QChar wanted = letter.toLower();
    for (int i = 0; i < int(myMenus.size()); ++i)
        if (mnemonicOf(myMenus[std::size_t(i)].title) == wanted) return i;
    return -1;
}

QVariantMap MenuModel::entryMap(const Node& node) const
{
    QVariantMap e;
    if (node.separator) {
        e.insert(QStringLiteral("separator"), true);
        return e;
    }
    QAction* a = node.action;
    const QString text = a ? a->text() : node.title;
    e.insert(QStringLiteral("separator"), false);
    e.insert(QStringLiteral("text"), plainText(text));
    e.insert(QStringLiteral("mnemonicAt"), mnemonicIndex(text));
    e.insert(QStringLiteral("submenu"), !node.children.empty());
    e.insert(QStringLiteral("enabled"), live(node));
    // QMenu shows the first shortcut, in the platform's own spelling.
    e.insert(QStringLiteral("shortcut"),
             a && node.children.empty() ? a->shortcut().toString(QKeySequence::NativeText) : QString());
    const bool checkable = a && a->isCheckable();
    e.insert(QStringLiteral("checkable"), checkable);
    e.insert(QStringLiteral("checked"), checkable && a->isChecked());
    e.insert(QStringLiteral("radio"), checkable && a->actionGroup() && a->actionGroup()->isExclusive());
    return e;
}

QVariantMap MenuModel::open() const
{
    QVariantMap map;
    map.insert(QStringLiteral("menu"), myMenu);
    map.insert(QStringLiteral("byKeyboard"), myByKeyboard);
    QVariantList levels;
    for (const Level& level : myLevels) {
        QVariantList entries;
        for (const Node* node : level.entries) entries.push_back(entryMap(*node));
        QVariantMap l;
        l.insert(QStringLiteral("entries"), entries);
        l.insert(QStringLiteral("current"), level.current);
        levels.push_back(l);
    }
    map.insert(QStringLiteral("levels"), levels);
    return map;
}

// ---- Gestures ---------------------------------------------------------------

void MenuModel::setOpen(int menu)
{
    const bool was = isOpen();
    myMenu = menu;
    if (menu < 0) {
        myLevels.clear();
        myByKeyboard = false;
    }
    emit changed();
    if (was != isOpen()) emit openChanged(isOpen());
}

namespace {
std::vector<const MenuModel::Node*> visibleOf(const std::vector<MenuModel::Node>& nodes)
{
    std::vector<const MenuModel::Node*> out;
    for (const MenuModel::Node& node : nodes)
        if (node.separator || !node.action || node.action->isVisible()) out.push_back(&node);
    return out;
}
}  // namespace

void MenuModel::openMenu(int index, bool byKeyboard)
{
    if (index < 0 || index >= int(myMenus.size())) return;
    myByKeyboard = byKeyboard;
    myLevels.clear();
    Level top;
    top.entries = visibleOf(myMenus[std::size_t(index)].children);
    top.current = byKeyboard ? firstLive(top) : -1;
    myLevels.push_back(std::move(top));
    setOpen(index);
}

void MenuModel::openSubmenu(int level, int entry, bool byKeyboard)
{
    myLevels.resize(std::size_t(level) + 1);
    myLevels[std::size_t(level)].current = entry;
    Level sub;
    sub.entries = visibleOf(myLevels[std::size_t(level)].entries[std::size_t(entry)]->children);
    sub.current = byKeyboard ? firstLive(sub) : -1;
    myLevels.push_back(std::move(sub));
    if (byKeyboard) myByKeyboard = true;
    emit changed();
}

void MenuModel::hover(int level, int entry)
{
    if (!isOpen() || level < 0 || level >= int(myLevels.size())) return;
    Level& l = myLevels[std::size_t(level)];
    if (entry < 0 || entry >= int(l.entries.size())) return;
    const Node& node = *l.entries[std::size_t(entry)];
    if (!live(node)) {
        // QMenu highlights nothing over a separator or a disabled entry, and
        // what was open below the level closes.
        myLevels.resize(std::size_t(level) + 1);
        myLevels[std::size_t(level)].current = -1;
        emit changed();
        return;
    }
    if (!node.children.empty()) {
        // Already open on this entry: nothing to do (a pointer moving across
        // the same row must not close what the user is reaching for).
        if (l.current == entry && int(myLevels.size()) > level + 1) return;
        openSubmenu(level, entry, false);
        return;
    }
    myLevels.resize(std::size_t(level) + 1);
    myLevels[std::size_t(level)].current = entry;
    emit changed();
}

void MenuModel::choose(int level, int entry)
{
    if (!isOpen() || level < 0 || level >= int(myLevels.size())) return;
    const Level& l = myLevels[std::size_t(level)];
    if (entry < 0 || entry >= int(l.entries.size())) return;
    if (!live(*l.entries[std::size_t(entry)])) return;
    activate(level, entry);
}

void MenuModel::activate(int level, int entry)
{
    const Node& node = *myLevels[std::size_t(level)].entries[std::size_t(entry)];
    if (!node.children.empty()) {
        openSubmenu(level, entry, true);
        return;
    }
    trigger(node.action);
}

void MenuModel::trigger(QAction* action)
{
    // Closed FIRST: whatever the action opens (a file dialog, a card) must
    // not find a menu standing over it. Run on the next turn, and only if it
    // is still enabled then - updateActions() may have moved in between, and
    // QAction::trigger() itself does not consult isEnabled().
    setOpen(-1);
    QPointer<QAction> guarded(action);
    QTimer::singleShot(0, this, [guarded] {
        if (guarded && guarded->isEnabled() && guarded->isVisible()) guarded->trigger();
    });
}

void MenuModel::close()
{
    if (!isOpen()) return;
    setOpen(-1);
}

void MenuModel::key(const QString& name)
{
    if (!isOpen() || myLevels.empty()) return;
    const int level = int(myLevels.size()) - 1;
    Level& l = myLevels.back();
    const int n = int(myMenus.size());

    if (name == QLatin1String("down") || name == QLatin1String("up")) {
        const int delta = name == QLatin1String("down") ? 1 : -1;
        const int from = l.current >= 0 ? l.current : (delta > 0 ? -1 : int(l.entries.size()));
        const int next = step(l, from, delta);
        if (next >= 0) l.current = next;
        myByKeyboard = true;
        emit changed();
    } else if (name == QLatin1String("home")) {
        l.current = firstLive(l);
        emit changed();
    } else if (name == QLatin1String("end")) {
        l.current = lastLive(l);
        emit changed();
    } else if (name == QLatin1String("right")) {
        if (l.current >= 0 && !l.entries[std::size_t(l.current)]->children.empty() &&
            live(*l.entries[std::size_t(l.current)])) {
            openSubmenu(level, l.current, true);
        } else {
            openMenu((myMenu + 1) % n, true);
        }
    } else if (name == QLatin1String("left")) {
        if (level > 0) {
            myLevels.pop_back();
            emit changed();
        } else {
            openMenu((myMenu - 1 + n) % n, true);
        }
    } else if (name == QLatin1String("enter") || name == QLatin1String("space")) {
        if (l.current >= 0 && live(*l.entries[std::size_t(l.current)])) activate(level, l.current);
    } else if (name == QLatin1String("escape")) {
        if (level > 0) {
            myLevels.pop_back();
            emit changed();
        } else {
            close();
        }
    } else if (name.size() == 1) {
        // A letter: the entries whose mnemonic it is. One runs (or opens);
        // several take turns being current, running none (QMenu's rule).
        const QChar letter = name.at(0).toLower();
        std::vector<int> matches;
        for (int i = 0; i < int(l.entries.size()); ++i) {
            const Node& node = *l.entries[std::size_t(i)];
            if (!live(node)) continue;
            const QString text = node.action ? node.action->text() : node.title;
            if (mnemonicOf(text) == letter) matches.push_back(i);
        }
        if (matches.empty()) return;
        if (matches.size() == 1) {
            activate(level, matches.front());
            return;
        }
        int next = matches.front();
        for (int m : matches) {
            if (m > l.current) {
                next = m;
                break;
            }
        }
        l.current = next;
        myByKeyboard = true;
        emit changed();
    }
}
