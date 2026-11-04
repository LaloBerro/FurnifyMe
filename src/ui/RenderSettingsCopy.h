#pragma once

// EVERY WORD THE RENDER SETTINGS PANEL PAINTS, ONCE (migration Phase 3e). The
// Qt panel and the HTML panel's bridge both read this, so the two panels
// cannot word a row differently - the SettingsEdits / JointRows precedent.
// The tr() context stays "RenderSettingsPanel", so an existing translation
// still applies.

#include <QString>

#include <array>

namespace RenderSettingsCopy {

// A chip's words: what it says and what its tooltip adds.
struct Choice {
    QString label;
    QString tip;
};

QString title();

// The section headings, top to bottom.
QString frame();
QString shots();
QString light();
QString material();
QString studio();   // never "Scene" - the vocabulary table's word for the 3D area
QString camera();
QString quality();
QString exportHeading();   // pinned with the export chips, below the scroll

// The dial rows' labels.
QString angle();
QString strength();
QString fov();
QString background();

// Free, 1:1, 4:5, 3:2, 16:9 - RenderSettingsPanel::Aspect's order.
std::array<Choice, 5> aspects();
// Off, Thirds, Centre - ::Guides' order.
std::array<Choice, 3> guides();
// Simple, Balanced, Deep - ascending, ::Quality's order.
std::array<Choice, 3> qualities();
// Window, 720, 1080, 1440, 4K - ::ExportSize's order.
std::array<Choice, 5> exportSizes();

Choice cutout();
Choice saveShot();
QString applyShotTip();
QString removeShotTip();
QString noShots();

QString materialNote();
QString backgroundTip();
Choice studioReset();
QString colourDialogTitle();

}  // namespace RenderSettingsCopy
