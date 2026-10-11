#include "Themes.h"

#include "SettingsEdits.h"

#include <QColorDialog>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace Themes {

namespace {

struct Data {
    Info info;
    QJsonObject spec;
    QVariantMap page;
};

const QStringList& order()
{
    static const QStringList ids = {QStringLiteral("violet-glass-float"), QStringLiteral("aurora-mesh-gradient"),
                                    QStringLiteral("nightshade-glass"), QStringLiteral("ultraviolet-grid"),
                                    QStringLiteral("graphite")};
    return ids;
}

const QHash<QString, Data>& data()
{
    static const QHash<QString, Data> table = [] {
        QHash<QString, Data> out;
        const QString bundled = Theme::defaultSpec().fontFamily;
        for (const QString& id : order()) {
            QFile file(QStringLiteral(":/themes/%1.json").arg(id));
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
            Data d;
            d.info.id = id;
            d.info.name = o.value(QStringLiteral("name")).toString();
            // The bundled DM Sans registers as its optical-size cut, which is
            // what Theme installs; a theme naming DM Sans means that cut.
            const QString family = o.value(QStringLiteral("fontFamily")).toString();
            d.info.fontFamily = (family.isEmpty() || SettingsEdits::familyDisplayName(bundled) == family) ? bundled : family;
            d.spec = o.value(QStringLiteral("spec")).toObject();
            d.page = o.value(QStringLiteral("page")).toObject().toVariantMap();
            out.insert(id, d);
        }
        return out;
    }();
    return table;
}

}  // namespace

const QVector<Info>& all()
{
    static const QVector<Info> list = [] {
        QVector<Info> out;
        for (const QString& id : order())
            if (data().contains(id)) out.push_back(data().value(id).info);
        return out;
    }();
    return list;
}

QString defaultId() { return QStringLiteral("violet-glass-float"); }
QString graphiteId() { return QStringLiteral("graphite"); }

bool exists(const QString& id) { return data().contains(id); }

Theme::Spec baseSpec(const QString& id)
{
    const QString key = exists(id) ? id : defaultId();
    Theme::Spec s = Theme::defaultSpec();
    const Data d = data().value(key);
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        const QJsonValue v = d.spec.value(token.id);
        if (v.isString()) {
            const QColor c(v.toString());
            if (c.isValid()) s.*(token.member) = c;
        }
    }
    if (!d.info.fontFamily.isEmpty()) s.fontFamily = d.info.fontFamily;
    return s;
}

QVariantMap pageTokens(const QString& id)
{
    return data().value(exists(id) ? id : defaultId()).page;
}

namespace {
Store& store()
{
    static Store s;
    return s;
}
QString& current()
{
    static QString id = graphiteId();
    return id;
}
}  // namespace

namespace {
void install(const QString& id);
}

void setStore(Store s)
{
    store() = std::move(s);
}

QString currentId()
{
    return current();
}

void storeCurrent()
{
    if (store().save) store().save(current(), Theme::serializeSpec());
}

bool select(const QString& id)
{
    if (!exists(id)) return false;
    if (id == current()) return true;
    // The picker first: closing it runs its Cancel, which restores the colour
    // it opened on - into the theme being LEFT, which is then stored with it.
    if (QColorDialog* picker = SettingsEdits::activeColourPicker()) picker->close();
    storeCurrent();
    install(id);
    return true;
}

namespace {
// Installs `id`'s stored edits, or its base, and announces it.
void install(const QString& id)
{
    Theme::Spec next = baseSpec(id);
    if (store().load) {
        Theme::Spec stored;
        const QString text = store().load(id);
        if (!text.isEmpty() && Theme::deserializeSpec(text, stored)) next = stored;
    }
    current() = id;
    Theme::setSpec(next);
    emit notifier()->currentChanged(id);
}
}  // namespace

void start(const QString& id)
{
    install(exists(id) ? id : defaultId());
}

Notifier* notifier()
{
    static Notifier* n = new Notifier();
    return n;
}

}  // namespace Themes
