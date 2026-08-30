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

// Strokes a `width`px border whose OUTER edge is exactly the outer edge of
// `rect`, aligned so the stroke lands on whole device pixels.
//
// This exists because the same bug was found three times in one phase. An
// antialiased pen is centred on the path it is given, so a 1px pen on a path
// with integer edges covers half of the pixel either side of it and paints
// two rows at half intensity - a soft smudge where a hairline was intended.
// The app bar's bottom rule was measured at #2b2b2f instead of border(); the
// rail's card at #2e2e33 and #2a2a2f down its two sides; every chip and bar
// button drew its border, its checked ring and its focus ring the same way.
// Each was being fixed with a different local idiom. Offsetting the path
// inward by half the pen width is the whole rule, and it lives here now.
void drawCrispBorder(QPainter& p, const QRectF& rect, const QColor& colour,
                     double radius, double width = 1.0);

// The same rule for a straight 1px line: whichever coordinate is constant is
// snapped to a half-integer so the line fills exactly one row or column.
void drawCrispRule(QPainter& p, const QPointF& from, const QPointF& to, const QColor& colour);

// The one implementation of the floating-surface family: fills `rect` with
// `ground` FIRST - opaque, covering the widget's full rect, corners included -
// then fills a rounded panel() rect on top and strokes a crisp 1px border()
// around it, corners rounded to `radius`. Every floating card in the shell
// (WalkthroughPanel, HintBalloon, Toast, ShortcutSheet, ExtrudePreview,
// ToolCluster's rail, ItemsPanel's drawer, AxisGizmo) calls this for its
// background instead of hand-rolling its own; a chip's body counts too,
// painted over before its state colour and content. Three cards each keep
// one thing of their own painted on TOP of this shared base rather than
// folding it in here: WalkthroughPanel's unconditional accent() outline,
// Toast's kind-tinted left stripe, and ExtrudePreview's danger() outline
// while its field's text is invalid - each is a single card's own accent,
// not something every floating surface needs, so it stays out of the one
// shared implementation.
//
// The `ground` fill is what a rounded card's corners rest on. A rounded rect
// does not cover the area outside itself and inside the widget's rect - the
// four small triangles at each corner - and every other call this file makes
// paints only the rounded shape, never that corner area. Over an ordinary
// widget that is fine, because whatever sits behind (a parent's background)
// shows through. Over OCCT's on-screen GL surface there IS nothing behind a
// Qt child in its own backing store, so an unpainted pixel there is not
// transparent but whatever the driver left, which is black - the drawer
// showed it worst, and the gizmo dodged the whole question at radius 0 as a
// stopgap. Filling `rect` with an opaque ground before the rounded panel
// settles it family-wide: the default is viewport(), near-invisible against
// the real viewport behind every card that floats directly over the GL
// surface; a card that instead sits on the chrome bar (AppBar's buttons, if
// they ever route through this rather than painting their own body) passes
// chrome() so its corners do not read as a viewport-grey nub on chrome.
//
// It still paints NO shadow, and that is a rule rather than a simplification.
// CLAUDE.md's probe result is that Qt composites plain OPAQUE children over
// OCCT's GL surface correctly on Windows and that translucency is the
// unreliable variant. A drop shadow is translucent pixels by definition, and
// the ground fill above does not change that - it is opaque, not blended. On
// this ground the family is carried by the 1px border() anyway, which is
// what the mockup's near-invisible rgba-on-dark shadows amounted to.
void paintSurface(QPainter& p, const QRect& rect, int radius = 8,
                  const QColor& ground = Theme::viewport());

// Zero. Kept as a function rather than deleted so every caller's
// grow-by-this-much / inset-by-this-much arithmetic, and the sibling-geometry
// sync that hangs off it (WalkthroughPanel's skip pill, Toast's Undo pill,
// ExtrudePreview's field), still reads as one coherent scheme - it now adds
// nothing. See paintSurface() above for why there is no shadow to reserve
// room for. A widget's painted card and its widget rect are therefore the
// same rectangle.
int surfaceShadowMargin();   // 0

// Installs the palette, the bundled font and the stylesheet. Call once, before
// any window is built.
void apply(QApplication& app);

// The bundled UI font family once apply() has run, or an empty string if the
// font failed to load and the platform default is in use.
QString fontFamily();

}  // namespace Theme
