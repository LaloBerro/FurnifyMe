// The render settings panel's edit path and its bridge to the page (migration
// Phase 3e), on Qt's offscreen platform - no window, no GPU, no page. The Qt
// panel stays the state and the one edit path both panels drive, so what is
// pinned here is that each edit slot does exactly what a click on its own chip
// does, that every change is announced, and that the words come from one table.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "RenderSettingsCopy.h"
#include "RenderSettingsPanel.h"
#include "Theme.h"
#include "web/RenderBridge.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

// Counts one signal's emissions - QSignalSpy is not in this Qt.
struct Counter {
    int count = 0;
    void operator()() { ++count; }
};
}  // namespace

using Panel = RenderSettingsPanel;

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::apply(app);

    // A real image for the wood tiles, the way the materials folder holds one.
    QTemporaryDir dir;
    const QString oakPath = dir.filePath(QStringLiteral("oak-natural.png"));
    const QString walnutPath = dir.filePath(QStringLiteral("walnut.png"));
    {
        QImage image(256, 160, QImage::Format_RGB32);
        image.fill(QColor(150, 110, 70));
        image.save(oakPath);
        image.fill(QColor(80, 50, 30));
        image.save(walnutPath);
    }

    // --- the words, once ------------------------------------------------------
    check(RenderSettingsCopy::title() == QStringLiteral("Render settings"), "the title is the panel's own");
    check(RenderSettingsCopy::studio() == QStringLiteral("Studio"), "the studio section is Studio, never Scene");
    check(RenderSettingsCopy::aspects().size() == 5 && RenderSettingsCopy::aspects()[1].label == QStringLiteral("1:1") &&
              RenderSettingsCopy::aspects()[1].tip == QStringLiteral("A square picture"),
          "the five shapes, with their tips");
    check(RenderSettingsCopy::qualities()[0].label == QStringLiteral("Simple") &&
              RenderSettingsCopy::qualities()[2].label == QStringLiteral("Deep"),
          "quality reads Simple, Balanced, Deep, ascending");
    check(RenderSettingsCopy::exportSizes()[0].label == QStringLiteral("Window") &&
              RenderSettingsCopy::exportSizes()[4].label == QStringLiteral("4K"),
          "the five export sizes");
    check(RenderSettingsCopy::noShots() == QStringLiteral("No shots saved yet"), "the empty shot line");

    Panel panel;
    QAction shoot(QStringLiteral("Save &Screenshot..."));
    panel.setShutterAction(&shoot);

    Counter changed;
    QObject::connect(&panel, &Panel::stateChanged, [&] { changed(); });

    // --- each edit slot is its chip's click -----------------------------------
    {
        int emitted = 0;
        Panel::Aspect last = Panel::Aspect::Free;
        QObject::connect(&panel, &Panel::aspectChanged, [&](Panel::Aspect a) { ++emitted; last = a; });
        panel.chooseAspect(Panel::Aspect::Square);
        check(emitted == 1 && last == Panel::Aspect::Square && panel.aspect() == Panel::Aspect::Square,
              "chooseAspect sets the shape and says so");
        panel.chooseAspect(Panel::Aspect::Square);
        check(emitted == 1, "choosing the current shape says nothing");
    }
    {
        int emitted = 0;
        QObject::connect(&panel, &Panel::guidesChanged, [&](Panel::Guides) { ++emitted; });
        panel.chooseGuides(Panel::Guides::Centre);
        panel.chooseGuides(Panel::Guides::Centre);
        check(emitted == 1 && panel.guides() == Panel::Guides::Centre, "chooseGuides, once for one change");
    }
    {
        int emitted = 0;
        QObject::connect(&panel, &Panel::exportSizeChanged, [&](Panel::ExportSize) { ++emitted; });
        panel.chooseExportSize(Panel::ExportSize::Height1080);
        panel.chooseExportSize(Panel::ExportSize::Height1080);
        check(emitted == 1 && panel.exportSize() == Panel::ExportSize::Height1080, "chooseExportSize, once");
    }
    {
        int quality = 0, quick = 0;
        bool lastQuick = false;
        QObject::connect(&panel, &Panel::qualityChanged, [&](Panel::Quality) { ++quality; });
        QObject::connect(&panel, &Panel::quickChanged, [&](bool q) { ++quick; lastQuick = q; });
        panel.chooseQuality(Panel::Quality::Simple);
        check(quality == 1 && quick == 1 && lastQuick && panel.quality() == Panel::Quality::Simple,
              "chooseQuality emits both signals, as the chip does");
        panel.chooseQuality(Panel::Quality::Simple);
        check(quality == 1, "choosing the current quality says nothing");
        panel.chooseQuality(Panel::Quality::Deep);
        check(quality == 2 && !lastQuick, "and back to Deep");
    }
    {
        int emitted = 0;
        bool last = false;
        QObject::connect(&panel, &Panel::cutoutChanged, [&](bool c) { ++emitted; last = c; });
        panel.toggleCutout();
        check(emitted == 1 && last && panel.cutout(), "toggleCutout turns it on");
        panel.toggleCutout();
        check(emitted == 2 && !last && !panel.cutout(), "and off");
    }
    {
        int save = 0, apply = 0, drop = 0, lastApply = -1, lastDrop = -1;
        QObject::connect(&panel, &Panel::shotSaveRequested, [&] { ++save; });
        QObject::connect(&panel, &Panel::shotApplied, [&](int i) { ++apply; lastApply = i; });
        QObject::connect(&panel, &Panel::shotRemoved, [&](int i) { ++drop; lastDrop = i; });
        panel.saveShot();
        check(save == 1, "saveShot asks for the view to be saved");
        panel.setShots({QStringLiteral("Front"), QStringLiteral("Corner")});
        panel.applyShot(1);
        panel.removeShot(0);
        check(apply == 1 && lastApply == 1 && drop == 1 && lastDrop == 0, "applyShot and removeShot name their row");
        panel.applyShot(2);
        panel.removeShot(-1);
        check(apply == 1 && drop == 1, "an index with no row is refused");
    }
    {
        int resets = 0;
        QObject::connect(&panel, &Panel::backgroundResetRequested, [&] { ++resets; });
        panel.resetBackground();
        check(resets == 1, "resetBackground asks for the studio tone back");
        panel.setBackgroundSilently(QColor(40, 80, 120));
        panel.pickBackground();
        QColorDialog* dialog = panel.activeColourDialog();
        check(dialog && !dialog->isModal(), "pickBackground opens the one modeless picker");
        panel.pickBackground();
        check(panel.activeColourDialog() != dialog || !dialog, "a second pick replaces the first picker");
        if (QColorDialog* open = panel.activeColourDialog()) open->close();
    }

    // --- the woods --------------------------------------------------------------
    panel.addTextureMaterials({{QStringLiteral("Oak natural"), oakPath}, {QStringLiteral("Walnut"), walnutPath}});
    {
        int chosen = 0, edits = 0;
        QString chosenName, chosenPath, editName;
        QObject::connect(&panel, &Panel::woodTextureChosen, [&](const QString& n, const QString& p) {
            ++chosen;
            chosenName = n;
            chosenPath = p;
        });
        QObject::connect(&panel, &Panel::materialEditRequested, [&](const QString& n) {
            ++edits;
            editName = n;
        });
        panel.chooseWood(QStringLiteral("Walnut"));
        check(chosen == 1 && chosenName == QStringLiteral("Walnut") && chosenPath == walnutPath && panel.wood() &&
                  panel.woodSelection() == QStringLiteral("Walnut"),
              "chooseWood wears that wood and names its file");
        panel.chooseWood(QStringLiteral("Walnut"));
        check(chosen == 1, "the wood already worn says nothing");
        panel.chooseWood(QStringLiteral("Mahogany"));
        check(chosen == 1, "a wood with no tile is refused");
        panel.editMaterial(QStringLiteral("Oak natural"));
        panel.editMaterial(QStringLiteral("Mahogany"));
        check(edits == 1 && editName == QStringLiteral("Oak natural"), "editMaterial opens a tile's own card, only a tile's");
        const QImage thumb = panel.materialThumbnail(QStringLiteral("Walnut"));
        check(!thumb.isNull() && thumb.width() >= 66 && thumb.pixelColor(thumb.width() / 2, 4).red() < 100,
              "a tile's thumbnail is its own image, decoded once");
        check(panel.materialThumbnail(QStringLiteral("Mahogany")).isNull(), "and a name with no tile has none");
    }

    // --- every change is announced, silent ones too ------------------------------
    {
        const auto announced = [&](auto&& change, const char* what) {
            const int before = changed.count;
            change();
            check(changed.count > before, what);
        };
        announced([&] { panel.setAspect(Panel::Aspect::ThreeTwo); }, "stateChanged: a silent aspect");
        announced([&] { panel.setGuides(Panel::Guides::Off); }, "stateChanged: silent guides");
        announced([&] { panel.setExportSize(Panel::ExportSize::Height2160); }, "stateChanged: a silent export size");
        announced([&] { panel.setExportNote(QStringLiteral("3840 x 2160")); }, "stateChanged: the export note");
        announced([&] { panel.setCutout(true); }, "stateChanged: a silent cut-out");
        announced([&] { panel.setQuality(Panel::Quality::Balanced); }, "stateChanged: a silent quality");
        announced([&] { panel.setWoodSelection(QStringLiteral("Oak natural")); }, "stateChanged: the wood worn");
        announced([&] { panel.setShots({QStringLiteral("Only")}); }, "stateChanged: the shot names");
        announced([&] { panel.setTierStatus(QStringLiteral("Path tracing"), 0.4); }, "stateChanged: the tier line");
        announced([&] { panel.setBackgroundSilently(QColor(10, 20, 30)); }, "stateChanged: a silent background");
        announced([&] { panel.setLightAngle(90.0); }, "stateChanged: the light angle");
        announced([&] { panel.setValuesSilently(0, 0, 10, 1.5, QColor(), 60); }, "stateChanged: the values pushed in");
        announced([&] { shoot.setEnabled(false); }, "stateChanged: the shutter's action");
        shoot.setEnabled(true);
        const int before = changed.count;
        panel.setTierStatus(QStringLiteral("Path tracing"), 0.4);
        check(changed.count == before, "an unchanged tier line is not announced");
    }

    // --- readouts and the footer, as the panel formats them ----------------------
    panel.setLightAngle(0.0);
    panel.setLightStrength(2.0);
    panel.setFov(45.0);
    check(panel.readout(Panel::Dial::LightAngle) == QString::fromUtf8("0°"), "the angle reads 0°");
    check(panel.readout(Panel::Dial::LightStrength) == QStringLiteral("200%"), "the strength reads 200%");
    check(panel.readout(Panel::Dial::Fov) == QString::fromUtf8("45°"), "the lens reads 45°");
    check(panel.dialValue(Panel::Dial::LightStrength) == 200 && panel.dialRange(Panel::Dial::LightStrength) == QPoint(20, 400),
          "a dial's value and range are the slider's own");
    panel.setTierStatus(QStringLiteral("Shadows"), -1.0);
    check(panel.tierName() == QStringLiteral("Shadows") && panel.tierProgress() < 0.0, "the tier line and no polish bar");
    check(panel.exportNote() == QStringLiteral("3840 x 2160"), "the export note");

    // --- the bridge: what the page reads, and what it may do ----------------------
    {
        RenderBridge bridge(&panel);
        const QVariantMap layout = bridge.layout();
        check(layout.value(QStringLiteral("title")).toString() == RenderSettingsCopy::title(), "layout: the title");
        const QVariantMap sections = layout.value(QStringLiteral("sections")).toMap();
        check(sections.value(QStringLiteral("studio")).toString() == QStringLiteral("Studio") &&
                  sections.value(QStringLiteral("export")).toString() == QStringLiteral("Export"),
              "layout: the section headings");
        const QVariantList aspects = layout.value(QStringLiteral("aspects")).toList();
        check(aspects.size() == 5 && aspects[4].toMap().value(QStringLiteral("label")).toString() == QStringLiteral("16:9") &&
                  !aspects[4].toMap().value(QStringLiteral("tip")).toString().isEmpty(),
              "layout: the five shapes with their tips");
        check(layout.value(QStringLiteral("qualities")).toList().size() == 3 &&
                  layout.value(QStringLiteral("exportSizes")).toList().size() == 5 &&
                  layout.value(QStringLiteral("guides")).toList().size() == 3,
              "layout: the quality, export and guide chips");
        const QVariantMap dials = layout.value(QStringLiteral("dials")).toMap();
        check(dials.value(QStringLiteral("lightStrength")).toMap().value(QStringLiteral("max")).toInt() == 400 &&
                  dials.value(QStringLiteral("fov")).toMap().value(QStringLiteral("min")).toInt() == 20,
              "layout: each dial's range is the slider's");
        const QVariantList tiles = layout.value(QStringLiteral("tiles")).toList();
        check(tiles.size() == 2 && tiles[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Walnut") &&
                  tiles[1].toMap().value(QStringLiteral("thumb")).toString().startsWith(QStringLiteral("data:image/png;base64,")),
              "layout: each tile carries its thumbnail as a data URL");

        int layoutEvents = 0, stateEvents = 0;
        QObject::connect(&bridge, &RenderBridge::layoutChanged, [&] { ++layoutEvents; });
        QObject::connect(&bridge, &RenderBridge::stateChanged, [&] { ++stateEvents; });

        panel.setAspect(Panel::Aspect::FourFive);
        panel.setShots({QStringLiteral("Front"), QStringLiteral("<b>Back</b>")});
        panel.setBackgroundSilently(QColor(0x12, 0x34, 0x56));
        panel.setLightStrength(1.5);
        panel.setTierStatus(QStringLiteral("Path tracing"), 0.5);
        QVariantMap state = bridge.state();
        check(stateEvents > 0, "state: the panel's changes reach the page");
        check(state.value(QStringLiteral("aspect")).toInt() == 2, "state: the shape");
        check(state.value(QStringLiteral("shots")).toStringList() ==
                  QStringList({QStringLiteral("Front"), QStringLiteral("<b>Back</b>")}),
              "state: the shot names, as the user wrote them");
        check(state.value(QStringLiteral("background")).toString() == QStringLiteral("#123456"), "state: the background");
        const QVariantMap strength =
            state.value(QStringLiteral("dials")).toMap().value(QStringLiteral("lightStrength")).toMap();
        check(strength.value(QStringLiteral("value")).toInt() == 150 &&
                  strength.value(QStringLiteral("readout")).toString() == QStringLiteral("150%"),
              "state: a dial's value and its readout");
        check(state.value(QStringLiteral("tierName")).toString() == QStringLiteral("Path tracing") &&
                  std::abs(state.value(QStringLiteral("tierProgress")).toDouble() - 0.5) < 1e-9,
              "state: the tier line and the polish");
        const QVariantMap shutter = state.value(QStringLiteral("shutter")).toMap();
        check(shutter.value(QStringLiteral("label")).toString() == QStringLiteral("Save Screenshot") &&
                  shutter.value(QStringLiteral("enabled")).toBool(),
              "state: the shutter reads the action's words, without & and ...");

        // Each slot reaches the panel, by the panel's own rules.
        bridge.aspect(4);
        check(panel.aspect() == Panel::Aspect::SixteenNine, "aspect(4) chooses 16:9");
        bridge.aspect(9);
        bridge.aspect(-1);
        check(panel.aspect() == Panel::Aspect::SixteenNine, "an aspect with no chip is refused");
        bridge.guides(1);
        bridge.exportSize(3);
        bridge.quality(0);
        check(panel.guides() == Panel::Guides::Thirds && panel.exportSize() == Panel::ExportSize::Height1440 &&
                  panel.quality() == Panel::Quality::Simple,
              "guides, export size and quality by index");
        const bool wasCut = panel.cutout();
        bridge.cutout();
        check(panel.cutout() != wasCut, "cutout() toggles");
        bridge.setDial(QStringLiteral("lightAngle"), 270);
        bridge.setDial(QStringLiteral("lightStrength"), 320);
        bridge.setDial(QStringLiteral("fov"), 70);
        check(panel.dialValue(Panel::Dial::LightAngle) == 270 && std::abs(panel.lightStrength() - 3.2) < 1e-9 &&
                  panel.dialValue(Panel::Dial::Fov) == 70,
              "setDial moves each dial in slider units");
        bridge.setDial(QStringLiteral("nothing"), 5);
        check(panel.dialValue(Panel::Dial::Fov) == 70, "an unknown dial is refused");
        int woods = 0;
        QObject::connect(&panel, &Panel::woodTextureChosen, [&](const QString&, const QString&) { ++woods; });
        bridge.wood(QStringLiteral("Walnut"));
        check(woods == 1 && panel.woodSelection() == QStringLiteral("Walnut"), "wood() chooses a tile");
        int applied = -1, removed = -1;
        QObject::connect(&panel, &Panel::shotApplied, [&](int i) { applied = i; });
        QObject::connect(&panel, &Panel::shotRemoved, [&](int i) { removed = i; });
        bridge.applyShot(1, QStringLiteral("<b>Back</b>"));
        check(applied == 1, "applyShot names its row");
        applied = -1;
        bridge.applyShot(1, QStringLiteral("Front"));
        bridge.removeShot(0, QStringLiteral("Corner"));
        check(applied == -1 && removed == -1,
              "a row whose name is not the one the page saw is refused - a stale index never hits a neighbour (review I1)");
        bridge.removeShot(0, QStringLiteral("Front"));
        check(removed == 0, "and the row the page saw is forgotten");

        int shots = 0;
        QObject::connect(&shoot, &QAction::triggered, [&] { ++shots; });
        shoot.setEnabled(false);
        check(!bridge.state().value(QStringLiteral("shutter")).toMap().value(QStringLiteral("enabled")).toBool(),
              "state: a disabled Save Screenshot disables the shutter");
        bridge.shoot();
        check(shots == 0, "shoot() refuses while Save Screenshot is disabled");
        shoot.setEnabled(true);
        bridge.shoot();
        check(shots == 1, "and triggers it while enabled");

        // A tile added after the page read its layout reaches it.
        const QString cherryPath = dir.filePath(QStringLiteral("cherry.png"));
        QImage(64, 64, QImage::Format_RGB32).save(cherryPath);
        panel.addTextureMaterials({{QStringLiteral("Cherry"), cherryPath}});
        check(layoutEvents == 1 && bridge.layout().value(QStringLiteral("tiles")).toList().size() == 3,
              "layout: a tile added later is announced and listed");

        int readies = 0;
        QObject::connect(&bridge, &RenderBridge::readyChanged, [&](bool r) { if (r) ++readies; });
        check(!bridge.isReady(), "the page is not ready until it says so");
        bridge.ready();
        bridge.ready();
        check(bridge.isReady() && readies == 1, "ready() says so once");
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
