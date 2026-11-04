#include "RenderBridge.h"

#include "RenderSettingsCopy.h"
#include "RenderSettingsPanel.h"

#include <QAction>
#include <QBuffer>
#include <QImage>
#include <QVariantList>

namespace {

QVariantMap choice(const RenderSettingsCopy::Choice& c)
{
    return {{QStringLiteral("label"), c.label}, {QStringLiteral("tip"), c.tip}};
}

template <std::size_t N>
QVariantList choices(const std::array<RenderSettingsCopy::Choice, N>& list)
{
    QVariantList out;
    for (const RenderSettingsCopy::Choice& c : list) out.push_back(choice(c));
    return out;
}

QString dataUrl(const QImage& image)
{
    if (image.isNull()) return QString();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

using Dial = RenderSettingsPanel::Dial;

}  // namespace

RenderBridge::RenderBridge(RenderSettingsPanel* panel, QObject* parent) : QObject(parent), myPanel(panel)
{
    rebuildLayout();
    if (myPanel) {
        connect(myPanel, &RenderSettingsPanel::stateChanged, this, &RenderBridge::stateChanged);
        connect(myPanel, &RenderSettingsPanel::materialsChanged, this, [this] {
            rebuildLayout();
            emit layoutChanged();
        });
    }
}

void RenderBridge::rebuildLayout()
{
    namespace C = RenderSettingsCopy;
    QVariantMap layout;
    layout.insert(QStringLiteral("title"), C::title());
    layout.insert(QStringLiteral("sections"),
                  QVariantMap{{QStringLiteral("frame"), C::frame()},
                              {QStringLiteral("shots"), C::shots()},
                              {QStringLiteral("light"), C::light()},
                              {QStringLiteral("material"), C::material()},
                              {QStringLiteral("studio"), C::studio()},
                              {QStringLiteral("camera"), C::camera()},
                              {QStringLiteral("quality"), C::quality()},
                              {QStringLiteral("export"), C::exportHeading()}});
    layout.insert(QStringLiteral("rows"), QVariantMap{{QStringLiteral("angle"), C::angle()},
                                                      {QStringLiteral("strength"), C::strength()},
                                                      {QStringLiteral("fov"), C::fov()},
                                                      {QStringLiteral("background"), C::background()}});
    layout.insert(QStringLiteral("aspects"), choices(C::aspects()));
    layout.insert(QStringLiteral("guides"), choices(C::guides()));
    layout.insert(QStringLiteral("qualities"), choices(C::qualities()));
    layout.insert(QStringLiteral("exportSizes"), choices(C::exportSizes()));
    layout.insert(QStringLiteral("cutout"), choice(C::cutout()));
    layout.insert(QStringLiteral("saveShot"), choice(C::saveShot()));
    layout.insert(QStringLiteral("studioReset"), choice(C::studioReset()));
    layout.insert(QStringLiteral("applyShotTip"), C::applyShotTip());
    layout.insert(QStringLiteral("removeShotTip"), C::removeShotTip());
    layout.insert(QStringLiteral("noShots"), C::noShots());
    layout.insert(QStringLiteral("materialNote"), C::materialNote());
    layout.insert(QStringLiteral("backgroundTip"), C::backgroundTip());

    QVariantMap dials;
    QVariantList tiles;
    if (myPanel) {
        const auto range = [this](Dial dial) {
            const QPoint r = myPanel->dialRange(dial);
            return QVariantMap{{QStringLiteral("min"), r.x()}, {QStringLiteral("max"), r.y()}};
        };
        dials.insert(QStringLiteral("lightAngle"), range(Dial::LightAngle));
        dials.insert(QStringLiteral("lightStrength"), range(Dial::LightStrength));
        dials.insert(QStringLiteral("fov"), range(Dial::Fov));
        for (const QString& name : myPanel->materialTileNames())
            tiles.push_back(QVariantMap{{QStringLiteral("name"), name},
                                        {QStringLiteral("thumb"), dataUrl(myPanel->materialThumbnail(name))}});
    }
    layout.insert(QStringLiteral("dials"), dials);
    layout.insert(QStringLiteral("tiles"), tiles);
    myLayout = layout;
}

QVariantMap RenderBridge::state() const
{
    QVariantMap state;
    if (!myPanel) return state;
    const auto dial = [this](Dial d) {
        return QVariantMap{{QStringLiteral("value"), myPanel->dialValue(d)},
                           {QStringLiteral("readout"), myPanel->readout(d)}};
    };
    state.insert(QStringLiteral("aspect"), static_cast<int>(myPanel->aspect()));
    state.insert(QStringLiteral("guides"), static_cast<int>(myPanel->guides()));
    state.insert(QStringLiteral("exportSize"), static_cast<int>(myPanel->exportSize()));
    state.insert(QStringLiteral("quality"), static_cast<int>(myPanel->quality()));
    state.insert(QStringLiteral("cutout"), myPanel->cutout());
    state.insert(QStringLiteral("dials"), QVariantMap{{QStringLiteral("lightAngle"), dial(Dial::LightAngle)},
                                                      {QStringLiteral("lightStrength"), dial(Dial::LightStrength)},
                                                      {QStringLiteral("fov"), dial(Dial::Fov)}});
    state.insert(QStringLiteral("wood"), myPanel->wood());
    state.insert(QStringLiteral("woodName"), myPanel->woodSelection());
    state.insert(QStringLiteral("background"), myPanel->background().name());
    state.insert(QStringLiteral("shots"), myPanel->shots());
    state.insert(QStringLiteral("exportNote"), myPanel->exportNote());
    state.insert(QStringLiteral("tierName"), myPanel->tierName());
    state.insert(QStringLiteral("tierProgress"), myPanel->tierProgress());

    QVariantMap shutter;
    if (RenderShutterButton* button = myPanel->shutter()) {
        if (QAction* action = button->action()) {
            const QString label = QString(action->text()).remove(QLatin1Char('&')).remove(QStringLiteral("..."));
            QString tip = action->toolTip();
            if (tip.isEmpty() || tip == action->text()) tip = label;
            shutter = {{QStringLiteral("label"), label},
                       {QStringLiteral("tip"), QString(tip).remove(QLatin1Char('&'))},
                       {QStringLiteral("enabled"), action->isEnabled()}};
        }
    }
    state.insert(QStringLiteral("shutter"), shutter);
    return state;
}

void RenderBridge::aspect(int index)
{
    if (myPanel && index >= 0 && index < 5) myPanel->chooseAspect(static_cast<RenderSettingsPanel::Aspect>(index));
}

void RenderBridge::guides(int index)
{
    if (myPanel && index >= 0 && index < 3) myPanel->chooseGuides(static_cast<RenderSettingsPanel::Guides>(index));
}

void RenderBridge::exportSize(int index)
{
    if (myPanel && index >= 0 && index < 5)
        myPanel->chooseExportSize(static_cast<RenderSettingsPanel::ExportSize>(index));
}

void RenderBridge::quality(int index)
{
    if (myPanel && index >= 0 && index < 3)
        myPanel->chooseQuality(static_cast<RenderSettingsPanel::Quality>(index));
}

void RenderBridge::cutout()
{
    if (myPanel) myPanel->toggleCutout();
}

void RenderBridge::setDial(const QString& id, int value)
{
    if (!myPanel) return;
    if (id == QLatin1String("lightAngle")) myPanel->setLightAngle(value);
    else if (id == QLatin1String("lightStrength")) myPanel->setLightStrength(value / 100.0);
    else if (id == QLatin1String("fov")) myPanel->setFov(value);
}

void RenderBridge::wood(const QString& name)
{
    if (myPanel) myPanel->chooseWood(name);
}

void RenderBridge::editMaterial(const QString& name)
{
    if (myPanel) myPanel->editMaterial(name);
}

void RenderBridge::saveShot()
{
    if (myPanel) myPanel->saveShot();
}

void RenderBridge::applyShot(int index)
{
    if (myPanel) myPanel->applyShot(index);
}

void RenderBridge::removeShot(int index)
{
    if (myPanel) myPanel->removeShot(index);
}

void RenderBridge::pickBackground()
{
    if (myPanel) myPanel->pickBackground();
}

void RenderBridge::resetBackground()
{
    if (myPanel) myPanel->resetBackground();
}

void RenderBridge::shoot()
{
    if (!myPanel || !myPanel->shutter()) return;
    QAction* action = myPanel->shutter()->action();
    if (action && action->isEnabled()) action->trigger();
}

void RenderBridge::ready()
{
    if (myReady) return;
    myReady = true;
    emit readyChanged(true);
}
