// The menu bar's model for the page (MenuModel) - migration Phase 3i,
// headless, on real QActions. Every rule here is QMenu's own, as the Qt menu
// bar behaves today; the page has to behave the same.
#include "session/MenuModel.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeySequence>

#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const QString& what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
    if (!ok) ++gFailures;
}

void pump()
{
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

QVariantList levels(const MenuModel& m) { return m.open().value(QStringLiteral("levels")).toList(); }
int depth(const MenuModel& m) { return int(levels(m).size()); }
int current(const MenuModel& m, int level = -1)
{
    const QVariantList l = levels(m);
    if (l.isEmpty()) return -2;
    return l.at(level < 0 ? l.size() - 1 : level).toMap().value(QStringLiteral("current")).toInt();
}
QVariantMap entry(const MenuModel& m, int level, int index)
{
    const QVariantList l = levels(m);
    if (level < 0 || level >= l.size()) return {};
    const QVariantList e = l.at(level).toMap().value(QStringLiteral("entries")).toList();
    return index < e.size() ? e.at(index).toMap() : QVariantMap();
}
QString currentText(const MenuModel& m)
{
    return entry(m, depth(m) - 1, current(m)).value(QStringLiteral("text")).toString();
}

struct Bar
{
    QObject owner;
    QAction* newA = nullptr;
    QAction* save = nullptr;
    QAction* autosaveMenu = nullptr;
    QAction* off = nullptr;
    QAction* after = nullptr;
    QAction* exportA = nullptr;
    QAction* hiddenA = nullptr;
    QAction* closeA = nullptr;
    QAction* undo = nullptr;
    QAction* redo = nullptr;
    QAction* del = nullptr;
    QAction* dup = nullptr;
    QAction* grid = nullptr;
    QAction* unitsMenu = nullptr;
    QAction* mm = nullptr;
    QAction* cm = nullptr;
    QAction* themeMenu = nullptr;
    QAction* violet = nullptr;
    int triggers = 0;
    QStringList fired;

    QAction* make(const QString& text, bool enabled = true)
    {
        auto* a = new QAction(text, &owner);
        a->setEnabled(enabled);
        QObject::connect(a, &QAction::triggered, &owner, [this, a] {
            ++triggers;
            fired << MenuModel::plainText(a->text());
        });
        return a;
    }

    std::vector<MenuModel::Node> nodes()
    {
        newA = make(QStringLiteral("&New furniture..."));
        newA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
        save = make(QStringLiteral("&Save"), false);
        autosaveMenu = make(QStringLiteral("&Autosave"));
        auto* group = new QActionGroup(&owner);
        off = make(QStringLiteral("&Off"));
        off->setCheckable(true);
        off->setChecked(true);
        group->addAction(off);
        after = make(QStringLiteral("A&fter every change"));
        after->setCheckable(true);
        group->addAction(after);
        exportA = make(QStringLiteral("E&xport STEP..."));
        hiddenA = make(QStringLiteral("&Hidden"));
        hiddenA->setVisible(false);
        closeA = make(QStringLiteral("&Close furniture"));
        undo = make(QStringLiteral("&Undo"), false);
        redo = make(QStringLiteral("&Redo"));
        del = make(QStringLiteral("&Delete Selected"));
        dup = make(QStringLiteral("&Duplicate"));
        grid = make(QStringLiteral("Show &Grid"));
        grid->setCheckable(true);
        grid->setChecked(true);
        unitsMenu = make(QStringLiteral("Units"));
        auto* units = new QActionGroup(&owner);
        mm = make(QStringLiteral("&Millimetres"));
        mm->setCheckable(true);
        mm->setChecked(true);
        units->addAction(mm);
        cm = make(QStringLiteral("&Centimetres"));
        cm->setCheckable(true);
        units->addAction(cm);
        themeMenu = make(QStringLiteral("&Theme"), false);
        violet = make(QStringLiteral("Violet"));

        auto leaf = [](QAction* a) {
            MenuModel::Node n;
            n.title = a->text();
            n.action = a;
            return n;
        };
        MenuModel::Node sep;
        sep.separator = true;

        MenuModel::Node file;
        file.title = QStringLiteral("&File");
        MenuModel::Node autosave = leaf(autosaveMenu);
        autosave.children = {leaf(off), leaf(after)};
        file.children = {leaf(newA), leaf(save), sep, autosave, leaf(exportA), leaf(hiddenA), leaf(closeA)};

        MenuModel::Node edit;
        edit.title = QStringLiteral("&Edit");
        edit.children = {leaf(undo), leaf(redo), sep, leaf(del), leaf(dup)};

        MenuModel::Node view;
        view.title = QStringLiteral("&View");
        MenuModel::Node unitsNode = leaf(unitsMenu);
        unitsNode.children = {leaf(mm), leaf(cm)};
        MenuModel::Node themeNode = leaf(themeMenu);
        themeNode.children = {leaf(violet)};
        view.children = {leaf(grid), unitsNode, themeNode};
        return {file, edit, view};
    }
};
}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // Mnemonics, QMenu's way.
    check(MenuModel::mnemonicOf(QStringLiteral("&File")) == QLatin1Char('f'), "\"&File\" has the mnemonic f");
    check(MenuModel::mnemonicOf(QStringLiteral("E&xport STEP...")) == QLatin1Char('x'),
          "\"E&xport\" has the mnemonic x");
    check(MenuModel::mnemonicOf(QStringLiteral("A&&B")).isNull(), "a doubled && is a literal ampersand, no mnemonic");
    check(MenuModel::mnemonicOf(QStringLiteral("Units")).isNull(), "no & means no mnemonic - not the first letter");
    check(MenuModel::plainText(QStringLiteral("A&&B &Save")) == QStringLiteral("A&B Save"),
          "plain text drops the mnemonic marker and keeps a literal &");
    check(MenuModel::mnemonicIndex(QStringLiteral("E&xport")) == 1, "the mnemonic's index is in the plain text");
    check(MenuModel::mnemonicIndex(QStringLiteral("A&&B &Save")) == 4,
          "the index counts a literal & as one character");

    Bar bar;
    MenuModel model(bar.nodes());
    int changes = 0;
    QList<bool> openSignals;
    QObject::connect(&model, &MenuModel::changed, [&] { ++changes; });
    QObject::connect(&model, &MenuModel::openChanged, [&](bool on) { openSignals << on; });

    // Titles.
    const QVariantList titles = model.titles();
    check(titles.size() == 3, "three titles");
    check(titles.value(0).toMap().value(QStringLiteral("title")) == QStringLiteral("File") &&
              titles.value(0).toMap().value(QStringLiteral("mnemonicAt")).toInt() == 0,
          "File's title is plain, its mnemonic at 0");
    check(model.menuForMnemonic(QLatin1Char('e')) == 1 && model.menuForMnemonic(QLatin1Char('V')) == 2 &&
              model.menuForMnemonic(QLatin1Char('q')) == -1,
          "Alt+E is Edit, Alt+V is View (case-blind), q is nothing");
    check(!model.isOpen() && model.open().value(QStringLiteral("menu")).toInt() == -1, "nothing is open at first");

    // Opening by keyboard and by mouse.
    model.openMenu(0, true);
    check(model.isOpen() && model.openIndex() == 0 && depth(model) == 1, "File opens at one level");
    check(openSignals == QList<bool>{true}, "openChanged(true) once");
    check(model.open().value(QStringLiteral("byKeyboard")).toBool(), "opened by keyboard");
    check(currentText(model) == QStringLiteral("New furniture..."), "by keyboard, the first live entry is current");
    const QVariantList fileEntries = levels(model).value(0).toMap().value(QStringLiteral("entries")).toList();
    check(fileEntries.size() == 6, "the hidden action is left out (6 entries, separator included)");
    check(entry(model, 0, 0).value(QStringLiteral("shortcut")).toString() ==
              QKeySequence(Qt::CTRL | Qt::Key_N).toString(QKeySequence::NativeText),
          "an entry carries its shortcut's text");
    check(!entry(model, 0, 1).value(QStringLiteral("enabled")).toBool(), "Save is disabled");
    check(entry(model, 0, 2).value(QStringLiteral("separator")).toBool(), "the third entry is a separator");
    check(entry(model, 0, 3).value(QStringLiteral("submenu")).toBool(), "Autosave is a submenu");
    check(entry(model, 0, 4).value(QStringLiteral("mnemonicAt")).toInt() == 1, "Export's mnemonic sits at 1");
    model.openMenu(1, false);
    check(model.openIndex() == 1 && current(model) == -1 && !model.open().value(QStringLiteral("byKeyboard")).toBool(),
          "by mouse, nothing is current");
    check(openSignals == QList<bool>{true}, "switching menus is not a close and reopen");

    // Up and down skip what cannot be chosen, and wrap.
    model.openMenu(0, true);
    model.key(QStringLiteral("down"));
    check(currentText(model) == QStringLiteral("Autosave"), "Down skips the disabled Save and the separator");
    model.key(QStringLiteral("down"));
    model.key(QStringLiteral("down"));
    check(currentText(model) == QStringLiteral("Close furniture"), "Down passes Export to Close");
    model.key(QStringLiteral("down"));
    check(currentText(model) == QStringLiteral("New furniture..."), "Down wraps to the top");
    model.key(QStringLiteral("up"));
    check(currentText(model) == QStringLiteral("Close furniture"), "Up wraps to the bottom");
    model.key(QStringLiteral("home"));
    check(currentText(model) == QStringLiteral("New furniture..."), "Home is the first live entry");
    model.key(QStringLiteral("end"));
    check(currentText(model) == QStringLiteral("Close furniture"), "End is the last live entry");

    // Right and Left: into a submenu and out, or across the bar.
    model.key(QStringLiteral("home"));
    model.key(QStringLiteral("down"));
    model.key(QStringLiteral("right"));
    check(depth(model) == 2 && currentText(model) == QStringLiteral("Off"),
          "Right on Autosave opens it at its first live entry");
    check(entry(model, 1, 0).value(QStringLiteral("radio")).toBool() &&
              entry(model, 1, 0).value(QStringLiteral("checked")).toBool() &&
              !entry(model, 1, 1).value(QStringLiteral("checked")).toBool(),
          "an exclusive group's entries are radios, Off checked");
    model.key(QStringLiteral("down"));
    check(currentText(model) == QStringLiteral("After every change"), "Down moves inside the submenu");
    model.key(QStringLiteral("left"));
    check(depth(model) == 1 && currentText(model) == QStringLiteral("Autosave"),
          "Left closes the submenu, its title current again");
    model.key(QStringLiteral("down"));
    model.key(QStringLiteral("right"));
    check(model.openIndex() == 1 && depth(model) == 1 && currentText(model) == QStringLiteral("Redo"),
          "Right on a plain entry moves to the next menu, at its first LIVE entry (Undo is disabled)");
    model.key(QStringLiteral("left"));
    check(model.openIndex() == 0, "Left at the top level moves to the previous menu");
    model.key(QStringLiteral("left"));
    check(model.openIndex() == 2, "Left from the first menu wraps to the last");
    model.key(QStringLiteral("right"));
    check(model.openIndex() == 0, "Right from the last menu wraps to the first");

    // A disabled submenu is not live.
    model.openMenu(2, true);
    check(currentText(model) == QStringLiteral("Show Grid"), "View opens on Show Grid");
    check(entry(model, 0, 0).value(QStringLiteral("checkable")).toBool() &&
              entry(model, 0, 0).value(QStringLiteral("checked")).toBool() &&
              !entry(model, 0, 0).value(QStringLiteral("radio")).toBool(),
          "a checkable entry outside a group is a check, not a radio");
    model.key(QStringLiteral("down"));
    model.key(QStringLiteral("down"));
    check(currentText(model) == QStringLiteral("Show Grid"), "Down skips the disabled Theme submenu");
    model.hover(0, 2);
    check(depth(model) == 1 && current(model) == -1, "hovering a disabled submenu opens nothing and selects nothing");
    model.hover(0, 1);
    check(depth(model) == 2 && current(model, 0) == 1 && current(model, 1) == -1,
          "hovering a live submenu opens it with nothing current");
    model.hover(0, 0);
    check(depth(model) == 1 && current(model) == 0, "hovering a plain entry closes the submenu");

    // Enter triggers - after the menu has closed, on the next turn.
    model.openMenu(1, true);
    bar.triggers = 0;
    bar.fired.clear();
    openSignals.clear();
    model.key(QStringLiteral("enter"));
    check(!model.isOpen(), "Enter closes the menu at once");
    check(openSignals == QList<bool>{false}, "openChanged(false) once");
    check(bar.triggers == 0, "...and has not run the action yet");
    pump();
    check(bar.fired == QStringList{QStringLiteral("Redo")}, "the action runs once, on the next turn");

    // A click on a disabled entry does nothing and leaves the menu open.
    model.openMenu(1, false);
    bar.triggers = 0;
    model.choose(0, 0);
    pump();
    check(bar.triggers == 0 && model.isOpen(), "choosing the disabled Undo runs nothing and keeps the menu open");
    model.choose(0, 2);
    pump();
    check(bar.triggers == 0 && model.isOpen(), "choosing a separator runs nothing");

    // An action disabled between the choice and the next turn does not run.
    bar.triggers = 0;
    model.choose(0, 1);
    bar.redo->setEnabled(false);
    pump();
    check(bar.triggers == 0, "an action disabled before its turn does not run");
    bar.redo->setEnabled(true);

    // Letters: one match runs it, several step through them, a submenu opens.
    model.openMenu(1, true);
    model.key(QStringLiteral("d"));
    check(model.isOpen() && currentText(model) == QStringLiteral("Delete Selected"),
          "a letter two entries share moves to the first, runs nothing");
    model.key(QStringLiteral("D"));
    check(model.isOpen() && currentText(model) == QStringLiteral("Duplicate"), "the same letter again steps to the next");
    bar.fired.clear();
    model.key(QStringLiteral("r"));
    pump();
    check(!model.isOpen() && bar.fired == QStringList{QStringLiteral("Redo")}, "a letter one entry owns runs it");
    model.openMenu(1, true);
    bar.fired.clear();
    model.key(QStringLiteral("u"));
    pump();
    check(model.isOpen() && bar.fired.isEmpty(), "a disabled entry's letter runs nothing");
    model.openMenu(0, true);
    model.key(QStringLiteral("a"));
    check(depth(model) == 2 && currentText(model) == QStringLiteral("Off"), "a submenu's letter opens it");
    bar.fired.clear();
    model.key(QStringLiteral("f"));
    pump();
    check(bar.fired == QStringList{QStringLiteral("After every change")}, "a letter inside the submenu runs its entry");
    check(bar.after->isChecked() && !bar.off->isChecked(), "the radio moved");

    // Escape closes one level at a time.
    model.openMenu(0, true);
    model.key(QStringLiteral("down"));
    model.key(QStringLiteral("enter"));
    check(depth(model) == 2, "Enter on a submenu opens it");
    model.key(QStringLiteral("escape"));
    check(model.isOpen() && depth(model) == 1, "Escape closes the submenu only");
    openSignals.clear();
    model.key(QStringLiteral("escape"));
    check(!model.isOpen() && openSignals == QList<bool>{false}, "the second Escape closes the menu");
    model.close();
    check(openSignals == QList<bool>{false}, "close() on a closed menu says nothing");

    // State follows the actions, one changed() per turn.
    model.openMenu(1, true);
    pump();
    changes = 0;
    bar.undo->setEnabled(true);
    bar.del->setEnabled(false);
    pump();
    check(changes == 1, QStringLiteral("two actions changing in one turn is one changed() (got %1)").arg(changes));
    check(entry(model, 0, 0).value(QStringLiteral("enabled")).toBool() &&
              !entry(model, 0, 3).value(QStringLiteral("enabled")).toBool(),
          "the entries read the actions' new state");
    model.close();
    pump();
    changes = 0;
    bar.undo->setEnabled(false);
    pump();
    check(changes == 0, "a closed menu does not re-announce action changes");

    // Opening an out-of-range menu is refused.
    model.openMenu(7, true);
    check(!model.isOpen(), "an unknown menu index opens nothing");

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
