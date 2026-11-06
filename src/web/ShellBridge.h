#pragma once

// THE SHELL ON THE PAGE (migration Phase 3i): the page's `shell` object - the
// rail's and the view cluster's chips, the pill's wordmark and the menus.
//
// Nothing here holds state of its own. A chip is a QAction (by its registry
// id) and a glyph; what it shows - enabled, checked, label, tooltip - is read
// off the action on every change (one ActionWatch, one chipsChanged() per
// turn), and a click goes back through ActionRegistry::trigger(), which obeys
// isEnabled(). The menus are a MenuModel built from the window's own QMenuBar
// (registered beside this as `menu`). Glyphs are IconSet's, sent once as
// alpha masks the page tints (ruling R2).
//
// What the page measures comes back here - each chip's rectangle (the
// Add-shape flyout stands beside its chip) and the pill's size and title
// rectangles (the slot takes the size; the window's caption hit test reads
// the titles).
//
// An open menu takes the keyboard through an application-wide filter for its
// window (ExtrudePreview's shape): every key is claimed from the shortcut map
// and the menu's own go to the model, so a field elsewhere keeps its focus
// and gets its keys back the moment the menu closes.

#include "ui/IconSet.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <vector>

class ActionRegistry;
class ActionWatch;
class MenuModel;
class QAction;
class QMenu;
class QMenuBar;
class QWidget;

class ShellBridge : public QObject
{
    Q_OBJECT
    // [{id, glyph} | {separator: true}], top to bottom.
    Q_PROPERTY(QVariantList rail READ rail CONSTANT)
    Q_PROPERTY(QVariantList view READ view CONSTANT)
    // Render mode's way out (Phase 7a): one chip, in the Back chip's place.
    Q_PROPERTY(QVariantList back READ back CONSTANT)
    // glyph name -> PNG data URL (an alpha mask, white on transparent).
    Q_PROPERTY(QVariantMap glyphs READ glyphs CONSTANT)
    // id -> {label, tip, enabled, checkable, checked}.
    Q_PROPERTY(QVariantMap chips READ chips NOTIFY chipsChanged)
    Q_PROPERTY(QString wordmark READ wordmark CONSTANT)

public:
    struct Chip
    {
        QAction* action = nullptr;   // null: a separator
        IconSet::Glyph glyph = IconSet::Glyph::Items;
        QString glyphName;
    };

    ShellBridge(ActionRegistry* registry, std::vector<Chip> rail, std::vector<Chip> view, QMenuBar* menus,
                QWidget* window, QObject* parent = nullptr, std::vector<Chip> back = {});
    ~ShellBridge() override;

    QVariantList rail() const;
    QVariantList view() const;
    QVariantList back() const;
    QVariantMap glyphs() const { return myGlyphs; }
    QVariantMap chips() const;
    QString wordmark() const { return QStringLiteral("FurnifyMe"); }
    MenuModel* menu() const { return myMenu; }
    // The menu bar's top-level menus, whose menuAction()s the pill's slot
    // carries so every shortcut fires while the HTML pill shows (R5).
    QList<QAction*> menuActions() const;

    // Where the page last said a chip stands, in the viewport's coordinates;
    // empty until it has said.
    QRect chipRect(const QString& id) const { return myChipRects.value(id); }
    // The pill's titles, in the viewport's coordinates.
    QVector<QRect> titleRects() const { return myTitleRects; }

public slots:
    // False, and nothing done, for an unknown or disabled action.
    bool trigger(const QString& id);
    // id -> {x, y, w, h}, viewport coordinates.
    void reportChipRects(const QVariantMap& rects);
    // The pill's natural size and its titles' rectangles (viewport coords).
    void reportPill(int width, int height, const QVariantList& titleRects);

signals:
    void chipsChanged();
    void pillReported(int width, int height);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QVariantList listOf(const std::vector<Chip>& chips) const;
    void menuOpenChanged(bool open);

    ActionRegistry* myRegistry = nullptr;
    std::vector<Chip> myRail;
    std::vector<Chip> myView;
    std::vector<Chip> myBack;
    QPointer<QMenuBar> myMenuBar;
    QPointer<QWidget> myWindow;
    MenuModel* myMenu = nullptr;
    ActionWatch* myWatch = nullptr;
    QVariantMap myGlyphs;
    QHash<QString, QRect> myChipRects;
    QVector<QRect> myTitleRects;
    bool myFiltering = false;
};
