#pragma once
// Colour tokens, the type scale, motion tokens and the application-wide
// stylesheet. Single source of truth for the shell's appearance - widgets ask
// Theme rather than hard-coding hex, a point size or an animation constant.
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

class QApplication;
class QPainter;

namespace Theme {

// --- the editable state behind every token -----------------------------------
//
// Since Milestone 2 the accessors below are not constants: each one reads a
// slot of THIS structure, and the Appearance panel writes it. Nothing else
// changed for a caller - Theme::accent() is still the only way to ask for the
// accent colour, and hex still lives nowhere else - but a colour is now a
// value that can move under a widget between two paints, which is why no
// widget may cache one across setSpec(). See notifier() below.
//
// defaultSpec() is today's Graphite palette byte for byte, so an app whose
// user has never opened the panel renders exactly as it did before.
struct Spec {
    QColor chrome;
    QColor panel;
    QColor chip;
    QColor chipHover;
    QColor chipActive;
    QColor accent;
    QColor text;
    QColor textMuted;
    QColor textDisabled;
    QColor border;
    QColor viewport;
    QColor gridMinor;
    QColor gridMajor;
    QColor axisX;
    QColor axisY;
    QColor sketchPointMarker;
    QColor danger;
    QColor focusRing;
    QColor focusRingMuted;
    // The two viewport highlight colours. They were OCCT's own named
    // constants until this task (Quantity_NOC_CYAN1 and Quantity_NOC_ORANGE)
    // rather than Theme tokens - which meant the two colours a user looks at
    // most while modelling were the two they could not change. Their defaults
    // are those constants' exact sRGB values, so nothing on screen moved when
    // they became editable.
    QColor highlightHover;
    QColor highlightSelected;

    // Empty means "whatever apply() managed to load", which is the bundled DM
    // Sans when the resource is present and the platform default when it is
    // not. defaultSpec() fills it in with the real family name once apply()
    // has run, so spec() == defaultSpec() holds at startup.
    QString fontFamily;
    // The BODY size. The other three are derived from it and are not stored -
    // a stored derived value is a second source of truth, and the four-size
    // law is only a law while the four cannot drift apart.
    double basePt = 10.0;
};

bool operator==(const Spec& a, const Spec& b);
inline bool operator!=(const Spec& a, const Spec& b) { return !(a == b); }

// The band the base size may be set to. The derived scale runs from
// basePt - 2 to basePt + 3, so this is 6pt..17pt of actual type.
constexpr double kMinBasePt = 8.0;
constexpr double kMaxBasePt = 14.0;

const Spec& spec();
Spec defaultSpec();

// Installs `next` as the live spec: re-applies the palette, the application
// stylesheet and the application font, then announces the change through
// notifier(). A no-op when `next` is already what spec() returns, so a slider
// dragged across a value it already holds does not re-polish every widget in
// the application.
void setSpec(const Spec& next);

// One string for QSettings, and its tolerant inverse. deserializeSpec()
// starts from defaultSpec() and overwrites only the tokens the string names,
// so a spec written by an older build gains this build's new tokens at their
// defaults instead of failing. It returns false - leaving `out` UNTOUCHED -
// for an empty string, for a fragment with no `=`, for a colour that QColor
// cannot parse and for a base size outside kMinBasePt..kMaxBasePt. An
// unrecognised KEY is ignored rather than refused: that is the half a future
// build needs in order to remove a token without stranding everyone's stored
// appearance.
QString serializeSpec();
QString serializeSpec(const Spec& s);
bool deserializeSpec(const QString& text, Spec& out);

// The editable colour tokens, in the order the panel lists them. `id` is the
// serialisation key and is NOT user-facing copy - AppearancePanel owns the
// user's words for each token, because those are copy and copy lives in the
// UI layer where the vocabulary sweep can reach it. The member pointer is
// what makes both the panel and the persistence loop data-driven: a token
// added to Spec and to this table is editable and persisted with no third
// place to remember.
struct ColourToken {
    QString id;
    QColor Spec::*member;
};
const QVector<ColourToken>& colourTokens();

// The broadcast setSpec() makes.
//
// Almost nothing needs it: every widget in this shell asks Theme for its
// colours inside paintEvent(), so `update()` is all a repaint takes and
// MainWindow's own updateActions()/appStateChanged() already reaches every
// one of them. The subscribers are the handful of places that genuinely
// CANNOT re-derive at paint time - a QIcon rasterised once at construction, a
// per-widget stylesheet's font-size, a card pinned with setFixedSize(), a
// layout's reserved spacing. Each of those is a cached appearance value, and
// this signal is how it stops being stale.
class Notifier : public QObject {
    Q_OBJECT

public:
    // setSpec() calls this. Nothing else should.
    void announce() { emit changed(); }

signals:
    void changed();
};

Notifier* notifier();

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
QColor highlightHover();    // the viewport's hover tint
QColor highlightSelected(); // and its selection tint

// The whole app's type scale: four sizes, and every widget that paints text
// reads one of them - a fifth size anywhere is a smell, not a design choice.
//
// All four are DERIVED from Spec::basePt, at fixed offsets: badge = base - 2,
// label = base - 1, body = base, title = base + 3. Storing four independent
// sizes would let the user set two of them equal, which is not a smaller
// scale but a broken one - and would let the type-scale sweep pass while the
// scale it is checking had collapsed to three sizes.
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
// surface. The `ground` parameter exists for a future caller painted on a
// non-viewport ground - a card sitting on the chrome bar would pass chrome()
// so its corners read as flat chrome-grey rather than a viewport-grey nub.
// No such caller exists today: AppBar's own buttons paint their own body
// directly (see BarButton::paintEvent()) rather than routing through this.
//
// A card whose logical size does not land on a whole number of DEVICE pixels
// cannot be saved by anything this function does - see wholeDevicePixels()
// below, which is the other half of the same rule and belongs at the caller's
// setFixedSize(), not here.
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

// Rounds a floating card's logical size UP to one that covers a WHOLE number
// of device pixels at every display scale Windows offers.
//
// paintSurface() above fills the card's whole rect and still cannot reach
// every pixel Qt flushes for it. Widget geometry is logical and the backing
// store is device-sized, so a card 93 logical rows tall at 150% scaling
// occupies 139.5 device rows; Qt flushes 140 and the paint event's clip -
// logical too - stops the widget's own painter at 139. Nothing the widget
// paints can cross its own clip and the parent cannot paint underneath a
// child, so the leftover row keeps whatever the backing store held. Over
// OCCT's GL surface that is not transparent: the round/flatten chip's first
// magnified capture carried an exact 0,0,0 hairline 264 device pixels wide
// along its bottom edge, which is the corner-nub failure one scale down.
//
// The only cure is to not have a fractional row, so a card asks for its size
// through this. Windows scales in quarter steps (100/125/150/175/200/225/250%)
// and a multiple of four is whole at every one of them, which is why this is
// display-ratio-INDEPENDENT: reading devicePixelRatioF() at construction would
// go stale the moment the window moved to another monitor, and a size that is
// only right on the machine it was written on is the shape of bug this rule
// exists to end. It grows a card by at most three pixels per side.
//
// EXISTING cards were not audited against this - PullArrow's 176x80 happens to
// be whole, and the rest size themselves from content and from layouts. A
// magnified capture that shows a black hairline along any card's edge is this,
// and this is the fix.
int wholeDevicePixels(int logical);
QSize wholeDevicePixels(const QSize& logical);

// The other half of the same rule: a card's POSITION.
//
// wholeDevicePixels() above makes a card's extent whole, which puts its far
// edge on a whole device pixel only if its near edge already was. A card that
// follows a projected 3D point does not - it is moved to whatever pixel the
// projection returned - so `frac(top x dpr)` survives to the bottom edge and
// the unpainted row comes straight back at a card whose size is beyond
// reproach. `offsetToWindow` is the card's parent's own origin inside the
// window, because the backing store is the WINDOW's and a viewport sitting at
// a fractional offset under the app bar would otherwise put every child on a
// fractional row however carefully the child was placed.
//
// Unlike the size rule this DOES read the live ratio, and the asymmetry is
// deliberate: a size is fixed once at construction, where reading a ratio that
// can change when the window moves to another monitor would go stale, while a
// position is recomputed on every camera move and cannot. Reading it also buys
// the finest legal step - 2 logical pixels at 150% rather than the 4 a
// ratio-blind rule would have to assume everywhere - and a value chip that
// tracks an arrow through an orbit in 4-pixel jumps is a visible cost for a
// precision only the odd ratios need.
int snapToDevicePixels(int value, int offsetToWindow, double devicePixelRatio);

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
