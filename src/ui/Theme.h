#pragma once
// Colour tokens and the application-wide stylesheet. Single source of truth for
// the shell's appearance - widgets ask Theme rather than hard-coding hex.
#include <QColor>
#include <QString>

class QApplication;

namespace Theme {

QColor chrome();        // menu bar, status bar, window background
QColor panel();         // items panel
QColor chip();          // chip background
QColor chipHover();
QColor chipActive();    // pressed or checked
QColor accent();
QColor text();
QColor textMuted();     // shortcut badges
QColor textDisabled();
QColor border();
QColor viewport();      // OCCT background

// Installs the palette, the bundled font and the stylesheet. Call once, before
// any window is built.
void apply(QApplication& app);

// The bundled UI font family once apply() has run, or an empty string if the
// font failed to load and the platform default is in use.
QString fontFamily();

}  // namespace Theme
