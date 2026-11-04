#include "RenderSettingsCopy.h"

#include <QCoreApplication>

namespace RenderSettingsCopy {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("RenderSettingsPanel", text); }
}  // namespace

QString title() { return tr("Render settings"); }

QString frame() { return tr("Frame"); }
QString shots() { return tr("Shots"); }
QString light() { return tr("Light"); }
QString material() { return tr("Material"); }
QString studio() { return tr("Studio"); }
QString camera() { return tr("Camera"); }
QString quality() { return tr("Quality"); }
QString exportHeading() { return tr("Export"); }

QString angle() { return tr("Angle"); }
QString strength() { return tr("Strength"); }
QString fov() { return tr("FOV"); }
QString background() { return tr("Background"); }

std::array<Choice, 5> aspects()
{
    return {{{tr("Free"), tr("The window's own shape - nothing is masked off")},
             {tr("1:1"), tr("A square picture")},
             {tr("4:5"), tr("Taller than wide - a single piece, standing")},
             {tr("3:2"), tr("A photograph's own shape")},
             {tr("16:9"), tr("Wide - a room, or a run of furniture")}}};
}

std::array<Choice, 3> guides()
{
    return {{{tr("Off"), tr("No lines inside the picture")},
             {tr("Thirds"), tr("Two lines each way - put what matters where they cross")},
             {tr("Centre"), tr("One line each way, through the middle")}}};
}

std::array<Choice, 3> qualities()
{
    return {{{tr("Simple"), tr("Instant frames with real shadows — for framing a shot or a quicker machine")},
             {tr("Balanced"), tr("Sharp the moment it appears, with no grain to wait out — the middle of the three")},
             {tr("Deep"), tr("The richest picture this machine reaches — slower, and it keeps polishing while you watch")}}};
}

std::array<Choice, 5> exportSizes()
{
    return {{{tr("Window"), tr("Twice the viewport's own pixels — what this app has always saved")},
             {tr("720"), tr("1280 x 720, near enough")},
             {tr("1080"), tr("Full HD")},
             {tr("1440"), tr("Quad HD")},
             {tr("4K"), tr("Ultra HD")}}};
}

Choice cutout()
{
    return {tr("Cut out"), tr("Save Screenshot writes a PNG with no floor and no background — just the furniture, "
                              "on transparency")};
}

Choice saveShot()
{
    return {tr("Save this view"), tr("Remember where the camera is, the picture's shape, the lens and the light — so "
                                     "the next image can be taken the same way")};
}

QString applyShotTip()
{
    return tr("Put the camera, the picture's shape, the lens and the light back the way they were");
}
QString removeShotTip() { return tr("Forget this shot"); }
QString noShots() { return tr("No shots saved yet"); }

QString materialNote() { return tr("Double-click a wood to set its colour, surface and grain"); }
QString backgroundTip() { return tr("Pick the render-mode backdrop colour"); }
Choice studioReset()
{
    return {tr("Studio"), tr("Put the backdrop back to the studio tone this render mode is calibrated for")};
}
QString colourDialogTitle() { return tr("Render background colour"); }

}  // namespace RenderSettingsCopy
