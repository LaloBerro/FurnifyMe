#include "SettingsBridge.h"

#include "session/ActionRegistry.h"
#include "session/ActionWatch.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QFileDialog>
#include <QWidget>

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("AppearancePanel", text);
}

QString kindName(SettingsEdits::Row::Kind kind)
{
    switch (kind) {
    case SettingsEdits::Row::Kind::Toggle: return QStringLiteral("toggle");
    case SettingsEdits::Row::Kind::Choice: return QStringLiteral("choice");
    case SettingsEdits::Row::Kind::Binary: return QStringLiteral("binary");
    case SettingsEdits::Row::Kind::Number: return QStringLiteral("number");
    }
    return QString();
}

}  // namespace

SettingsBridge::SettingsBridge(ActionRegistry* registry, const std::vector<SettingsEdits::Row>& rows,
                               QObject* parent)
    : QObject(parent), myRegistry(registry)
{
    QVariantList tabs;
    for (const SettingsEdits::TabInfo& tab : SettingsEdits::tabs())
        tabs.push_back(QVariantMap{{QStringLiteral("name"), tab.name}, {QStringLiteral("tip"), tab.tip}});

    QVariantList numbers;
    for (const SettingsEdits::NumberField& f : SettingsEdits::numberFields()) {
        numbers.push_back(QVariantMap{{QStringLiteral("id"), f.id},       {QStringLiteral("label"), f.label},
                                      {QStringLiteral("min"), f.min},     {QStringLiteral("max"), f.max},
                                      {QStringLiteral("step"), f.step},   {QStringLiteral("decimals"), f.decimals},
                                      {QStringLiteral("suffix"), f.suffix}, {QStringLiteral("tip"), f.tip}});
    }

    QVariantList tokens;
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        if (SettingsEdits::isRetiredToken(token.id)) continue;
        tokens.push_back(QVariantMap{{QStringLiteral("id"), token.id},
                                     {QStringLiteral("name"), SettingsEdits::tokenName(token.id)},
                                     {QStringLiteral("group"), SettingsEdits::groupHeadingFor(token.id)}});
    }

    QVariantList families;
    for (const auto& [shown, family] : SettingsEdits::fontFamilies())
        families.push_back(QVariantMap{{QStringLiteral("shown"), shown}, {QStringLiteral("family"), family}});

    QVariantList rowList;
    QStringList watched;
    for (const SettingsEdits::Row& row : rows) {
        QStringList ids;
        for (const QAction* action : row.actions) {
            if (!action) continue;
            const QString id = ActionRegistry::idFor(action->text());
            ids << id;
            watched << id;
            myAllowed.insert(id);
        }
        rowList.push_back(QVariantMap{{QStringLiteral("kind"), kindName(row.kind)},
                                      {QStringLiteral("tab"), static_cast<int>(row.tab)},
                                      {QStringLiteral("label"), row.label},
                                      {QStringLiteral("actions"), ids},
                                      {QStringLiteral("options"), row.optionLabels},
                                      {QStringLiteral("stacked"), row.stacked},
                                      {QStringLiteral("number"), row.numberId}});
    }

    // The drawer's own words, verbatim from the Qt drawer.
    const QVariantMap words{
        {QStringLiteral("title"), tr("Settings")},
        {QStringLiteral("close"), QCoreApplication::translate("PanelCloseButton", "Close this panel")},
        {QStringLiteral("save"), tr("Save colours...")},
        {QStringLiteral("saveTip"),
         tr("Write the colours and text size to a file\nKeep colours you like, or hand them to somebody else.")},
        {QStringLiteral("load"), tr("Load colours...")},
        {QStringLiteral("loadTip"), tr("Read colours back out of a file\nApplied as soon as it is opened.")},
        {QStringLiteral("reset"), SettingsEdits::resetLabel()},
        {QStringLiteral("resetArmed"), SettingsEdits::resetArmedLabel()},
        {QStringLiteral("resetTip"), tr("Put every colour, the text size and font, the line widths, "
                                        "Grid detail and Gizmo size back the way they shipped — click twice")},
        {QStringLiteral("pickTip"), tr("Pick a colour for %1")},
        {QStringLiteral("fontLabel"), tr("Font")},
        {QStringLiteral("fontTip"), tr("The typeface the whole app is set in")},
        {QStringLiteral("resetMs"), SettingsEdits::kResetConfirmMs},
    };

    myLayout = QVariantMap{{QStringLiteral("tabs"), tabs},       {QStringLiteral("numbers"), numbers},
                           {QStringLiteral("tokens"), tokens},   {QStringLiteral("families"), families},
                           {QStringLiteral("rows"), rowList},    {QStringLiteral("words"), words}};

    myWatch = new ActionWatch(registry, this);
    myWatch->setIds(watched);
    connect(myWatch, &ActionWatch::changed, this, &SettingsBridge::actionsChanged);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &SettingsBridge::specChanged);
}

QVariantMap SettingsBridge::spec() const
{
    const Theme::Spec& live = Theme::spec();
    QVariantMap colours;
    for (const Theme::ColourToken& token : Theme::colourTokens())
        colours.insert(token.id, (live.*(token.member)).name());
    QVariantMap numbers;
    for (const SettingsEdits::NumberField& f : SettingsEdits::numberFields())
        numbers.insert(f.id, SettingsEdits::numberValue(live, f.id));
    return QVariantMap{{QStringLiteral("colours"), colours},
                       {QStringLiteral("numbers"), numbers},
                       {QStringLiteral("fontFamily"), live.fontFamily},
                       {QStringLiteral("fontShown"), SettingsEdits::familyDisplayName(live.fontFamily)}};
}

QJsonArray SettingsBridge::actions() const
{
    return myWatch ? myWatch->snapshot() : QJsonArray();
}

void SettingsBridge::allowAction(const QString& id)
{
    myAllowed.insert(id);
}

void SettingsBridge::setNumber(const QString& id, double value)
{
    SettingsEdits::setNumber(id, value);
}

void SettingsBridge::setColour(const QString& id, const QString& hex)
{
    SettingsEdits::setTokenColour(id, QColor(hex));
}

void SettingsBridge::setFontFamily(const QString& family)
{
    SettingsEdits::setFontFamily(family);
}

void SettingsBridge::pickColour(const QString& id)
{
    SettingsEdits::openColourPicker(id, myDialogParent.data());
}

void SettingsBridge::reset()
{
    SettingsEdits::reset();
}

void SettingsBridge::saveColours()
{
    // A NATIVE dialog - the operating system's question, asked in its own
    // surface, as the Qt drawer and Save Screenshot ask it.
    const QString path = QFileDialog::getSaveFileName(myDialogParent.data(), tr("Save colours"), QString(),
                                                      SettingsEdits::colourFileFilter());
    if (path.isEmpty()) return;   // cancelled - not a failure
    if (!SettingsEdits::saveColoursTo(path)) emit colourSaveFailed(path);
}

void SettingsBridge::loadColours()
{
    const QString path = QFileDialog::getOpenFileName(myDialogParent.data(), tr("Load colours"), QString(),
                                                      SettingsEdits::colourFileFilter());
    if (path.isEmpty()) return;
    if (!SettingsEdits::loadColoursFrom(path)) emit colourLoadRefused(path);
}

bool SettingsBridge::trigger(const QString& actionId)
{
    if (!myAllowed.contains(actionId) || !myRegistry) return false;
    return myRegistry->trigger(actionId);
}
