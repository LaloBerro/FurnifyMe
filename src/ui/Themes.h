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

#include <QString>
#include <QVariantMap>
#include <QVector>

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

}  // namespace Themes
