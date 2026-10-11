#pragma once

// THE THEMES (T-072): five whole styles - DESIGN.md is the contract. A theme
// is DATA, `:/themes/<id>.json` (assets/themes): its name, its typeface, the
// Theme::Spec colours it sets, and its page-only tokens (shapes, surfaces,
// a type extra). No hex in src/ outside Theme.cpp - the JSON carries it.
//
// A theme's base spec is Theme::defaultSpec() with that theme's colours and
// typeface over it, and GRAPHITE sets none: Graphite IS defaultSpec(), so a
// window built with persistence off (gui_smoke) and every colour file saved
// before themes existed mean exactly what they always did.

#include "Theme.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace Themes {

struct Info {
    QString id;
    QString name;
    QString fontFamily;   // as installed (the bundled DM Sans registers as its cut)
};

// In menu order, the default first.
const QVector<Info>& all();
QString defaultId();    // "violet-glass-float"
QString graphiteId();   // "graphite"
bool exists(const QString& id);
// defaultSpec() + the theme's colours and typeface; an unknown id answers the
// default theme's.
Theme::Spec baseSpec(const QString& id);
// The page-only tokens (CSS custom properties without the leading "--").
QVariantMap pageTokens(const QString& id);

// --- the theme in use ---------------------------------------------------------
//
// Where each theme's own edits live: QSettings in the app ("appearance/<id>"),
// a map in the tests, nothing at all in a window built with persistence off -
// an empty store simply keeps nothing, so every theme comes back at its base.
// The same injection UserProgress uses, for the same reason: a suite whose
// result depended on how the developer last left the app would not be a suite.
struct Store {
    std::function<QString(const QString& id)> load;
    std::function<void(const QString& id, const QString& spec)> save;
};
void setStore(Store store);
// Graphite until select() runs - so a window that never chooses (gui_smoke)
// wears exactly defaultSpec().
QString currentId();
// Puts `id` on: closes an open colour picker FIRST (its Cancel restores into
// the theme being left, never the one arriving), stores the leaving theme's
// live spec, installs the arriving theme's stored edits or its base, and
// announces it. False, and nothing changed, for an unknown id; true and
// nothing changed for the theme already on.
bool select(const QString& id);
// The window's FIRST choice: puts `id` on (an unknown id puts the default on)
// WITHOUT storing the theme being left - at startup that is the untouched
// default spec, and storing it would overwrite the user's own Graphite edits.
void start(const QString& id);
// Stores the live spec as the current theme's edits (the debounced save).
// Only what differs from the theme's base is kept (themes review I1): a base
// fixed in a later build still reaches a theme the user has visited, and a
// token added later starts at the theme's value, not Graphite's. A theme
// nobody changed stores nothing.
void storeCurrent();
// The same for any theme and spec - how a build before themes files its
// colours as Graphite's edits.
void storeEdits(const QString& id, const Theme::Spec& spec);
// `id`'s stored edits laid over its CURRENT base.
Theme::Spec editedSpec(const QString& id);

class Notifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void currentChanged(const QString& id);
};
Notifier* notifier();

}  // namespace Themes
