#pragma once

// Theme::Spec for pages (migration Phase 2): the live colour tokens, the four
// font sizes and the family, as CSS custom properties. A page writes them onto
// :root (web/src/theme.ts) and again on every changed(), so an Appearance edit
// reaches HTML exactly as it reaches every Qt widget - and no page may cache a
// colour across it, the same rule the widgets keep.

#include <QObject>
#include <QVariantMap>

class ThemeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap tokens READ tokens NOTIFY changed)
public:
    explicit ThemeBridge(QObject* parent = nullptr);
    QVariantMap tokens() const;

signals:
    void changed();
};
