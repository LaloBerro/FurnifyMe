#pragma once

// THE MENU BAR'S MODEL (migration Phase 3i): the editor's menus as the chrome
// page draws them - which menu is open, which submenus, the current entry at
// each level - and QMenu's keyboard rules, worked out here once and tested
// headless. The page draws and sends gestures; it never decides which entry is
// live or what a key does.
//
// The menus are a tree of QActions (MainWindow's menu bar, walked by the
// bridge into Nodes). Entries are addressed by position - the level and the
// index in it - never by action id: Help's "Show tips again" is an action no
// registry holds (ruling R4 of the plan).
//
// A trigger closes the menu FIRST and runs the action on the next turn, if it
// is still enabled then: an action may open a modal file dialog, which must
// not run inside the page's channel call, and the menu must not stand over
// whatever the action opens.
//
// Core + Gui (furnify_session); tested headless by menu_model_tests.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

class QAction;

class MenuModel : public QObject
{
    Q_OBJECT
    // [{title, mnemonicAt, enabled}] - the top-level titles, '&' removed,
    // mnemonicAt the index of the mnemonic letter in title (-1 when none).
    Q_PROPERTY(QVariantList titles READ titles NOTIFY changed)
    // {menu (-1: none open), byKeyboard, levels: [{entries: [{text,
    // mnemonicAt, shortcut, enabled, checkable, checked, radio, submenu,
    // separator}], current}]}. Hidden actions are left out.
    Q_PROPERTY(QVariantMap open READ open NOTIFY changed)

public:
    struct Node
    {
        QString title;               // a submenu's title, or a top-level menu's
        QPointer<QAction> action;    // the entry's action, or a submenu's menuAction()
        bool separator = false;
        std::vector<Node> children;  // non-empty: a submenu
    };

    explicit MenuModel(std::vector<Node> menus, QObject* parent = nullptr);

    QVariantList titles() const;
    QVariantMap open() const;
    bool isOpen() const { return myMenu >= 0; }
    int openIndex() const { return myMenu; }
    int menuCount() const { return int(myMenus.size()); }
    // The index of the top-level menu whose mnemonic is `letter`, or -1.
    int menuForMnemonic(QChar letter) const;

    // "&File" -> 'f'; "E&xport" -> 'x'; "&&" is a literal ampersand; no '&'
    // gives QChar() - a menu entry with no mnemonic has none (QMenu's rule).
    static QChar mnemonicOf(const QString& text);
    static QString plainText(const QString& text);
    static int mnemonicIndex(const QString& text);

public slots:
    // Opens a top-level menu. By keyboard, its first live entry is current;
    // by mouse, none is.
    void openMenu(int index, bool byKeyboard);
    // The pointer is over an entry: it becomes current, deeper levels close,
    // and a live submenu opens (with nothing current in it).
    void hover(int level, int entry);
    // up, down, left, right, enter, space, escape, home, end - or one letter.
    void key(const QString& name);
    // A click on an entry: a live submenu opens, a live action is triggered.
    void choose(int level, int entry);
    void close();

signals:
    void changed();
    // A menu opened (true) or the last one closed (false).
    void openChanged(bool open);

private:
    // One open level: its VISIBLE entries (hidden actions left out, so an
    // index here is the index the page drew), and the current one.
    struct Level
    {
        std::vector<const Node*> entries;
        int current = -1;
    };

    static bool visible(const Node& node);
    static bool live(const Node& node);
    int step(const Level& level, int from, int delta) const;
    int firstLive(const Level& level) const;
    int lastLive(const Level& level) const;
    void openSubmenu(int level, int entry, bool byKeyboard);
    void activate(int level, int entry);
    void trigger(QAction* action);
    void setOpen(int menu);
    void watch(const std::vector<Node>& nodes);
    QVariantMap entryMap(const Node& node) const;

    std::vector<Node> myMenus;
    int myMenu = -1;
    bool myByKeyboard = false;
    std::vector<Level> myLevels;
    // Every watched action's change, folded into one changed() per turn.
    QTimer myPending;
};
