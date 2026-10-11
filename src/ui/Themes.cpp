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
    Shell shell;
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
            d.shell = parseShell(o.value(QStringLiteral("shell")).toObject());
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

const Shell& shellOf(const QString& id)
{
    static const Shell none;
    const auto it = data().find(id);
    return it == data().end() ? none : it->shell;
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

const Shell& shell() { return shellOf(currentId()); }

namespace {
// "key=value" pieces of a serialized spec, in order.
QStringList pieces(const Theme::Spec& spec)
{
    return Theme::serializeSpec(spec).split(QLatin1Char(';'), Qt::SkipEmptyParts);
}
}  // namespace

void storeEdits(const QString& id, const Theme::Spec& spec)
{
    if (!store().save || !exists(id)) return;
    const QStringList base = pieces(baseSpec(id));
    QStringList changed;
    for (const QString& piece : pieces(spec))
        if (!base.contains(piece)) changed << piece;
    store().save(id, changed.join(QLatin1Char(';')));
}

void storeCurrent()
{
    storeEdits(current(), Theme::spec());
}

Theme::Spec editedSpec(const QString& id)
{
    const Theme::Spec base = baseSpec(id);
    if (!store().load) return base;
    const QString edits = store().load(id);
    if (edits.isEmpty()) return base;
    // The base written out first and the edits after it: deserializeSpec()
    // applies pieces in order, so each edit lands over the base it belongs
    // to. A full snapshot (an older store) is simply all edits.
    Theme::Spec out;
    if (!Theme::deserializeSpec(Theme::serializeSpec(base) + QLatin1Char(';') + edits, out)) return base;
    return out;
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
    const Theme::Spec next = editedSpec(id);
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
