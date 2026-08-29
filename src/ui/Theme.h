#pragma once
// Colour tokens, the type scale, motion tokens and the application-wide
// stylesheet. Single source of truth for the shell's appearance - widgets ask
// Theme rather than hard-coding hex, a point size or an animation constant.
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QRect>
#include <QString>

class QApplication;
class QPainter;

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
QColor gridMinor();     // ground grid, minor lines
QColor gridMajor();     // ground grid, major lines
QColor axisX();         // ground grid, X axis tint (muted red)
QColor axisY();         // ground grid, Y axis tint (muted green)
QColor sketchPointMarker(); // in-progress sketch: the dot at each placed
                        // point and the ring on the first one - a hue none
                        // of the above already carries (not the yellow
                        // preview outline, the cyan hover or the orange
                        // selection tint), chosen to read against both the
                        // viewport background and a shaded grey body
QColor danger();        // invalid input, failure accents - NOT the same
                        // concept as axisX(), which is a grid-axis tint that
                        // happens to be red; this is the semantic "something
                        // is wrong" colour
QColor focusRing();     // visible keyboard focus outline
QColor focusRingMuted(); // same outline, dimmed - a focused widget in a
                        // window that is not the OS-active one (the user has
                        // moved on to another application) still shows a
                        // ring, just not one that keeps shouting for
                        // attention

// The whole app's type scale: four sizes, and every widget that paints text
// reads one of them - a fifth size anywhere is a smell, not a design choice.
QFont titleFont();      // panel and sheet titles
QFont bodyFont();       // everything the user reads
QFont labelFont();      // chip labels, status bar
QFont badgeFont();      // shortcut badges

// Motion tokens for ordinary UI transitions - hover, focus, a panel
// appearing. NOT for the viewport camera: OcctViewWidget::animateTo() keeps
// its own 250 ms, deliberately not this value (see the comment at that
// constant).
int motionMs();               // 160
QEasingCurve motionCurve();   // OutCubic

// The one implementation of the floating-surface family: fills `rect` with
// panel(), strokes a 1px border(), rounds the corners to `radius`, and paints
// a soft shadow ring in the margin around `rect` - never outside it, since a
// widget composited over OcctViewWidget's own GL surface cannot paint past
// its own bounds. Every floating card in the shell (WalkthroughPanel,
// HintBalloon, Toast, ShortcutSheet) calls this for its background instead
// of hand-rolling its own; a chip's body counts too, painted over before its
// state colour and content. `rect` is the surface itself - callers reserve
// surfaceShadowMargin() px around it first (see below) so the shadow has
// somewhere to paint.
void paintSurface(QPainter& p, const QRect& rect, int radius = 8);

// How many pixels of margin a widget must reserve around its content for the
// soft shadow paintSurface() paints. A caller grows its own size by this much
// per side and paints its surface rect inset by the same amount - see
// paintSurface() above.
int surfaceShadowMargin();   // 3

// Installs the palette, the bundled font and the stylesheet. Call once, before
// any window is built.
void apply(QApplication& app);

// The bundled UI font family once apply() has run, or an empty string if the
// font failed to load and the platform default is in use.
QString fontFamily();

}  // namespace Theme
