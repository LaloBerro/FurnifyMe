#include "ThemeBridge.h"

#include "Theme.h"

#include <QFont>

namespace {
// A Qt point is 1/72 in at the logical 96 dpi every widget is laid out at;
// CSS pixels are the same logical unit, so the two scales agree.
QString px(const QFont& f)
{
    return QStringLiteral("%1px").arg(f.pointSizeF() * 96.0 / 72.0, 0, 'f', 3);
}
}  // namespace

ThemeBridge::ThemeBridge(QObject* parent)
    : QObject(parent)
{
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ThemeBridge::changed);
}

QVariantMap ThemeBridge::tokens() const
{
    QVariantMap t;
    t.insert(QStringLiteral("--chrome"), Theme::chrome().name());
    t.insert(QStringLiteral("--panel"), Theme::panel().name());
    t.insert(QStringLiteral("--chip"), Theme::chip().name());
    t.insert(QStringLiteral("--chip-hover"), Theme::chipHover().name());
    t.insert(QStringLiteral("--chip-active"), Theme::chipActive().name());
    t.insert(QStringLiteral("--accent"), Theme::accent().name());
    t.insert(QStringLiteral("--accent-text"), Theme::accentText().name());
    t.insert(QStringLiteral("--danger"), Theme::danger().name());
    t.insert(QStringLiteral("--danger-text"), Theme::dangerText().name());
    t.insert(QStringLiteral("--caution"), Theme::caution().name());
    t.insert(QStringLiteral("--text"), Theme::text().name());
    t.insert(QStringLiteral("--text-muted"), Theme::textMuted().name());
    t.insert(QStringLiteral("--text-disabled"), Theme::textDisabled().name());
    t.insert(QStringLiteral("--border"), Theme::border().name());
    t.insert(QStringLiteral("--viewport"), Theme::viewport().name());
    t.insert(QStringLiteral("--font-badge"), px(Theme::badgeFont()));
    t.insert(QStringLiteral("--font-label"), px(Theme::labelFont()));
    t.insert(QStringLiteral("--font-body"), px(Theme::bodyFont()));
    t.insert(QStringLiteral("--font-title"), px(Theme::titleFont()));
    // The family the widgets use; the page bundles DM Sans itself, so the
    // name resolves there even though Qt registered the face from its own
    // resources.
    t.insert(QStringLiteral("--font-family"),
             QStringLiteral("'%1'").arg(Theme::bodyFont().family()));
    return t;
}
