#include "ShellBridge.h"

#include "session/ActionRegistry.h"
#include "session/ActionWatch.h"
#include "session/MenuModel.h"
#include "ui/ToolChip.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QWidget>

namespace {
// Glyphs are sent at 48 px: crisp for an 18 px icon at up to 2.67x.
constexpr int kGlyphPx = 48;

std::vector<MenuModel::Node> walk(const QMenu* menu)
{
    std::vector<MenuModel::Node> nodes;
    if (!menu) return nodes;
    for (QAction* a : menu->actions()) {
        MenuModel::Node node;
        if (a->isSeparator()) {
            node.separator = true;
        } else {
            node.title = a->text();
            node.action = a;
            if (QMenu* sub = a->menu()) {
                node.title = sub->title();
                node.children = walk(sub);
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

std::vector<MenuModel::Node> walkBar(const QList<QMenu*>& list)
{
    std::vector<MenuModel::Node> menus;
    for (QMenu* menu : list) {
        if (!menu) continue;
        MenuModel::Node node;
        node.title = menu->title();
        node.action = menu->menuAction();
        node.children = walk(menu);
        menus.push_back(std::move(node));
    }
    return menus;
}

QRect rectOf(const QVariant& v)
{
    const QVariantMap m = v.toMap();
    return QRect(m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
                 m.value(QStringLiteral("w")).toInt(), m.value(QStringLiteral("h")).toInt());
}
}  // namespace

ShellBridge::ShellBridge(ActionRegistry* registry, std::vector<Chip> rail, std::vector<Chip> view,
                         const QList<QMenu*>& menus, QWidget* window, QObject* parent, std::vector<Chip> back)
    : QObject(parent), myRegistry(registry), myRail(std::move(rail)), myView(std::move(view)),
      myBack(std::move(back)),
      myWindow(window)
{
    myMenu = new MenuModel(walkBar(menus), this);
    connect(myMenu, &MenuModel::openChanged, this, &ShellBridge::menuOpenChanged);
    // A QMenu closes when the application stops being the active one (an
    // Alt+Tab, a click in another program); so does this one.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) myMenu->close();
    });

    QStringList ids;
    for (const std::vector<Chip>* list : {&myRail, &myView, &myBack}) {
        for (const Chip& chip : *list) {
            if (!chip.action) continue;
            ids << ActionRegistry::idFor(chip.action->text());
            if (!myGlyphs.contains(chip.glyphName))
                myGlyphs.insert(chip.glyphName, IconSet::glyphMaskUrl(chip.glyph, kGlyphPx));
        }
    }
    myWatch = new ActionWatch(registry, this);
    myWatch->setIds(ids);
    connect(myWatch, &ActionWatch::changed, this, &ShellBridge::chipsChanged);
}

ShellBridge::~ShellBridge()
{
    if (myFiltering && qApp) qApp->removeEventFilter(this);
}

QVariantList ShellBridge::listOf(const std::vector<Chip>& chips) const
{
    QVariantList list;
    for (const Chip& chip : chips) {
        QVariantMap m;
        if (!chip.action) {
            m.insert(QStringLiteral("separator"), true);
        } else {
            m.insert(QStringLiteral("id"), ActionRegistry::idFor(chip.action->text()));
            m.insert(QStringLiteral("glyph"), chip.glyphName);
        }
        list.push_back(m);
    }
    return list;
}

QVariantList ShellBridge::rail() const { return listOf(myRail); }
QVariantList ShellBridge::view() const { return listOf(myView); }
QVariantList ShellBridge::back() const { return listOf(myBack); }

QVariantMap ShellBridge::chips() const
{
    QVariantMap map;
    for (const std::vector<Chip>* list : {&myRail, &myView, &myBack}) {
        for (const Chip& chip : *list) {
            QAction* a = chip.action;
            if (!a) continue;
            QVariantMap c;
            c.insert(QStringLiteral("label"), a->text().remove(QLatin1Char('&')));
            c.insert(QStringLiteral("tip"), ToolChip::iconOnlyTip(a));
            c.insert(QStringLiteral("enabled"), a->isEnabled());
            c.insert(QStringLiteral("checkable"), a->isCheckable());
            c.insert(QStringLiteral("checked"), a->isCheckable() && a->isChecked());
            map.insert(ActionRegistry::idFor(a->text()), c);
        }
    }
    return map;
}

bool ShellBridge::trigger(const QString& id)
{
    // Only the chips' own actions: the page's channel is not a way to run
    // any command in the app.
    bool ours = false;
    for (const std::vector<Chip>* list : {&myRail, &myView, &myBack})
        for (const Chip& chip : *list)
            if (chip.action && ActionRegistry::idFor(chip.action->text()) == id) ours = true;
    if (!ours || !myRegistry) return false;
    return myRegistry->trigger(id);
}

void ShellBridge::reportChipRects(const QVariantMap& rects)
{
    for (auto it = rects.cbegin(); it != rects.cend(); ++it) myChipRects.insert(it.key(), rectOf(it.value()));
}

void ShellBridge::reportPill(int width, int height, const QVariantList& titleRects)
{
    QVector<QRect> rects;
    for (const QVariant& v : titleRects) rects.push_back(rectOf(v));
    myTitleRects = rects;
    if (width > 0 && height > 0) emit pillReported(width, height);
}

void ShellBridge::menuOpenChanged(bool open)
{
    if (open == myFiltering || !qApp) return;
    myFiltering = open;
    if (open) qApp->installEventFilter(this);
    else qApp->removeEventFilter(this);
}

bool ShellBridge::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !myWindow || widget->window() != myWindow || !myMenu->isOpen())
        return QObject::eventFilter(watched, event);
    // Every key is the menu's while it is open: claimed from the shortcut
    // map, so Delete or Ctrl+Z cannot reach a window shortcut behind it.
    if (type == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (type == QEvent::KeyRelease) return true;
    auto* key = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
    switch (key->key()) {
        case Qt::Key_Up: myMenu->key(QStringLiteral("up")); return true;
        case Qt::Key_Down: myMenu->key(QStringLiteral("down")); return true;
        case Qt::Key_Left: myMenu->key(QStringLiteral("left")); return true;
        case Qt::Key_Right: myMenu->key(QStringLiteral("right")); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: myMenu->key(QStringLiteral("enter")); return true;
        case Qt::Key_Space: myMenu->key(QStringLiteral("space")); return true;
        case Qt::Key_Escape: myMenu->key(QStringLiteral("escape")); return true;
        case Qt::Key_Home: myMenu->key(QStringLiteral("home")); return true;
        case Qt::Key_End: myMenu->key(QStringLiteral("end")); return true;
        default: break;
    }
    // Alt+letter while a menu is open moves to that menu (QMenuBar's rule).
    if (mods == Qt::AltModifier && key->key() >= Qt::Key_A && key->key() <= Qt::Key_Z) {
        const int index = myMenu->menuForMnemonic(QChar(QLatin1Char(char('a' + (key->key() - Qt::Key_A)))));
        if (index >= 0) myMenu->openMenu(index, true);
        return true;
    }
    if ((mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0) {
        const QString text = key->text();
        if (text.size() == 1 && text.at(0).isLetterOrNumber()) myMenu->key(text.toLower());
    }
    return true;
}
