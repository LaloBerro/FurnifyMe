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
class QWidget;

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
    // Milestone 5, item 7: the in-progress outline's own polyline and a
    // closed outline's line aspect - the same two display sites
    // sketchLineWidthPx already reaches. It was OCCT's own
    // Quantity_NOC_YELLOW until this task rather than a Theme token, and
    // deliberately NOT accent() - the two are unrelated surfaces that would
    // otherwise move together the instant either one was edited. The default
    // is that constant's exact sRGB value, so nothing on screen moves until
    // the user edits it (the same rule highlightHover/highlightSelected
    // already follow for their own former OCCT constants).
    QColor outlineLineColour;
    QColor danger;
    QColor focusRing;
    QColor focusRingMuted;
    // The GIZMO's three axis hues - Milestone 3, Task 5. Deliberately NOT
    // named axisX/axisY: those two already exist, for the ground grid's own
    // muted axis tint (see axisX()/axisY() below), and promoting the
    // AxisGizmo/manipulator hues under the same two names would silently
    // repaint the grid the moment a user edited what they thought was the
    // gizmo's colour, or vice versa - two unrelated surfaces sharing one
    // token by an accident of naming. gizmoAxisZ has no such collision (the
    // grid has no Z tint, since the ground plane's own two axes are X and Y),
    // but it is named for symmetry with the other two rather than living
    // under a different scheme.
    QColor gizmoAxisX;
    QColor gizmoAxisY;
    QColor gizmoAxisZ;
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
    // The border width every ToolChip strokes - the always-on border() ring
    // and the checked accent ring both. 0 is a legal value and means no ring
    // at all (the fill states still carry hover/pressed/checked); the default
    // is the 1px the family has always drawn.
    double chipStrokePx = 1.0;
    // Scales GridRenderer::minorStepFor()'s distance thresholds - see that
    // function's own comment for the exact mapping. HIGHER means MORE grid
    // lines: the finer 1mm/10mm bands persist to a greater camera distance
    // before coarsening, so a value above 1.0 shows a denser grid at any
    // given zoom and a value below 1.0 coarsens sooner. 1.0 is today's grid,
    // byte-identical - the same rule chipStrokePx's 1.0 default follows.
    double gridDensity = 1.0;
    // The width of the face-boundary edges displaySolid() draws on shaded
    // bodies - the GRAY30 crease lines that make a box's own edges readable
    // once it is shaded. 0 means no boundary lines at all
    // (SetFaceBoundaryDraw false), the same "0 removes the ring outright"
    // rule chipStrokePx's 0 already follows. 1.0 is today's hardcoded width,
    // byte-identical.
    double edgeWidthPx = 1.0;
    // The width of the live in-progress outline and a closed outline's own
    // line aspect (AIS_Shape::SetWidth) - both display sites share this one
    // value, because the two are the same outline at two moments of its
    // life. 2.0 is today's hardcoded width, byte-identical.
    double sketchLineWidthPx = 2.0;
    // Multiplies the drawn size of the body gizmos - all three renderers in
    // TransformGizmo.cpp read it (kArmPixels times this), so Space swaps
    // between tools at one consistent scale whatever the user set. 1.0 is
    // today's size, byte-identical - the same rule gridDensity's 1.0 default
    // follows.
    double gizmoScale = 1.0;
};

bool operator==(const Spec& a, const Spec& b);
inline bool operator!=(const Spec& a, const Spec& b) { return !(a == b); }

// The band the base size may be set to. The derived scale runs from
// basePt - 2 to basePt + 3, so this is 6pt..17pt of actual type.
constexpr double kMinBasePt = 8.0;
constexpr double kMaxBasePt = 14.0;

// The band the chip border width may be set to. 0 removes the rings outright;
// past 4 the border eats the 34px icon-only chip's face.
constexpr double kMinChipStrokePx = 0.0;
constexpr double kMaxChipStrokePx = 4.0;

// The band the grid density multiplier may be set to - see Spec::gridDensity.
// Half as fine as default at the bottom, twice as fine at the top; either end
// is already a visibly different grid without disappearing (0) or crowding
// into a solid wash (past a handful of times finer).
constexpr double kMinGridDensity = 0.5;
constexpr double kMaxGridDensity = 2.0;

// The band the gizmo size multiplier may be set to - see Spec::gizmoScale.
// Half size still leaves every handle grabbable at the 14px screen tolerance;
// double is already most of a small viewport's height.
constexpr double kMinGizmoScale = 0.5;
constexpr double kMaxGizmoScale = 2.0;

// The band the body edge-line width may be set to - see Spec::edgeWidthPx.
// 0 removes the boundary lines outright; past 4 the crease lines start to
// eat the shading they are meant to merely outline.
constexpr double kMinEdgeWidthPx = 0.0;
constexpr double kMaxEdgeWidthPx = 4.0;

// The band the outline line width may be set to - see
// Spec::sketchLineWidthPx. 1 is the thinnest a line can be and still read
// against the viewport background; past 6 it starts to obscure the corner
// it is meant to mark.
constexpr double kMinSketchLineWidthPx = 1.0;
constexpr double kMaxSketchLineWidthPx = 6.0;

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
//
// A `family=` naming a font this machine does not have is neither refused nor
// taken: it falls back to the default family. Refusing would throw away every
// colour in the string over a font, and taking it would silently substitute
// whatever Qt's matcher landed on while the panel's combo showed a family
// that is not installed - a spec that reads back as something other than what
// was stored. This is the one field where "the value is wrong" and "the
// string is corrupt" are different things.
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
QColor gizmoAxisX();    // AxisGizmo's X arm/tip - the vivid red the grid's
                        // own axisX() is deliberately NOT (see Spec)
QColor gizmoAxisY();    // AxisGizmo's Y arm/tip - vivid green
QColor gizmoAxisZ();    // AxisGizmo's Z arm/tip - vivid blue. The 3D body
                        // gizmos draw their handles UNLIT in these exact
                        // tokens too (TransformGizmo.cpp), which is what
                        // finally retired the AIS_Manipulator styling wall
QColor sketchPointMarker(); // in-progress sketch: the dot at each placed
                        // point and the ring on the first one - a hue none
                        // of the above already carries (not the yellow
                        // preview outline, the cyan hover or the orange
                        // selection tint), chosen to read against both the
                        // viewport background and a shaded grey body
QColor outlineLineColour(); // the outline's own line - the in-progress
                        // polyline and a closed outline item's line aspect,
                        // both - see Spec::outlineLineColour
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

double chipStrokePx();      // ToolChip border width - see Spec::chipStrokePx
double gridDensity();       // grid line density multiplier - see Spec::gridDensity
double edgeWidthPx();       // body boundary-line width - see Spec::edgeWidthPx
double sketchLineWidthPx(); // outline line width - see Spec::sketchLineWidthPx
double gizmoScale();        // body gizmo size multiplier - see Spec::gizmoScale

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

// The same four, as they would be under an arbitrary spec.
//
// For the one thing a widget cannot do with the live fonts alone: measure how
// much WIDER its content is than it was under the look the app shipped with.
// A card whose fixed size was chosen against the default scale (the items
// drawer's 240px is the case that needed this) reserves
// `shipped + (measured now - measured at defaultSpec())`, which is exactly
// the shipped number at the shipped scale and grows only by what the type
// change actually costs. Deriving that at the call site would mean copying
// the scale's offsets out of this file, which is the fifth copy the four-size
// law exists to prevent.
QFont titleFontFor(const Spec& s);
QFont bodyFontFor(const Spec& s);
QFont labelFontFor(const Spec& s);
QFont badgeFontFor(const Spec& s);

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

// The one implementation of the floating-surface family: fills a rounded
// panel() rect over `rect` and strokes a crisp 1px border() around it,
// corners rounded to `radius`. Every floating card in the shell
// (WalkthroughPanel, HintBalloon, Toast, ShortcutSheet, ExtrudePreview,
// ToolCluster's rail, ItemsPanel's drawer, AxisGizmo, VersionsPanel and its
// row cards, RenderSettingsPanel and its shutter, MainWindow's CompareBadge
// and MirrorPlacementChip) calls this for its background instead of
// hand-rolling its own; a chip's body counts too, painted over before its
// state colour and content. Three cards each keep one thing of their own
// painted on TOP of this shared base rather than folding it in here:
// WalkthroughPanel's unconditional accent() outline, Toast's kind-tinted
// left stripe, and ExtrudePreview's danger() outline while its field's text
// is invalid - each is a single card's own accent, not something every
// floating surface needs, so it stays out of the one shared implementation.
//
// Outside the rounded shape - the four small triangles at each corner of
// `rect` a rounded rect does not cover - this function paints NOTHING. That
// is the point, not an oversight: see makeSurfaceTransparent() below for why
// a corner left unpainted here now composites as the genuine pixel behind
// the widget, GL scene included, instead of the flat opaque square a `ground`
// argument used to fill in for.
//
// (Historical: this function used to take a third `ground` colour and fill
// `rect` with it before the rounded panel, because a Qt child painted
// directly over OCCT's native-window GL surface had nothing behind it in
// Qt's own backing store - an unpainted pixel there read as whatever the
// driver last left, which is black. The QOpenGLWidget migration retired that
// architecture; see CLAUDE.md's "One opaque paint family" section, now a
// tombstone, for the full history and makeSurfaceTransparent()'s role in
// finishing the job.)
//
// A card whose logical size does not land on a whole number of DEVICE pixels
// cannot be saved by anything this function does - see wholeDevicePixels()
// below, which is the other half of the same rule and belongs at the caller's
// setFixedSize(), not here.
//
// It still paints NO shadow. A drop shadow is translucent pixels, and this
// family carries its separation with the 1px border() instead - which is
// what the mockup's near-invisible rgba-on-dark shadows amounted to anyway.
void paintSurface(QPainter& p, const QRect& rect, int radius = 8);

// Rounds a floating card's logical size UP to one that covers a WHOLE number
// of device pixels at every display scale Windows offers.
//
// paintSurface() above cannot reach every pixel Qt flushes for a card whose
// logical size does not land on a whole device pixel. Widget geometry is
// logical and the backing store is device-sized, so a card 93 logical rows
// tall at 150% scaling occupies 139.5 device rows; Qt flushes 140 and the
// paint event's clip - logical too - stops the widget's own painter at 139,
// leaving one leftover device row this widget's own paintEvent() genuinely
// never touches. (Historical: the round/flatten chip's first magnified
// capture, taken while the viewport was still a native window Qt's own
// backing store knew nothing about, carried an exact 0,0,0 hairline 264
// device pixels wide along that row - the corner-nub failure one scale
// down. Since the QOpenGLWidget migration the same leftover row instead
// shows whatever is genuinely behind the card there, the same as an
// unpainted corner does - see makeSurfaceTransparent() - which is a much
// smaller defect than a black hairline but still a defect: the rule below
// exists so no card ever has a row to leave unpainted in the first place.)
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

// Stops the app-wide `QMainWindow, QWidget { background-color: @chrome }`
// stylesheet rule from stamping `w`'s full rect opaque before its own
// paintEvent() runs - a per-widget stylesheet always wins over that rule
// regardless of selector specificity (ToolChip::applyTheme() found the
// mechanism first, fixing its own corners the same way before this task
// existed; that call site is unrelated to this one and stays as it is).
//
// Every member of the paintSurface() family calls this once, in its own
// constructor, right alongside the `Qt::WA_NoSystemBackground` it already
// sets. Without it, a corner paintSurface() leaves unpainted (see that
// function's own comment - it paints only the rounded shape, nothing
// outside it any more) would risk showing the QSS rule's own flat
// chrome()-square instead of genuinely compositing through to whatever
// widget - or, over the viewport, whatever live GL frame - actually sits
// behind it. This is the mechanism the "install a window mask" answer
// (`installCardMask`, deleted with this task - see CLAUDE.md's tombstone)
// used to stand in for: a mask stopped Qt from painting those pixels at
// all, which was the only way to get a genuinely see-through corner while
// the viewport was a native window with nothing Qt's own backing store knew
// about behind it. Now that the viewport is a QOpenGLWidget and Qt's
// compositor genuinely holds every frame's pixels, an unpainted corner is
// already correct - this call only has to keep the QSS rule from painting
// something there in the first place.
//
// AxisGizmo folds the identical "background: transparent" fragment into its
// own applyTheme() stylesheet string instead of calling this separately -
// documented at that call site - because it already rebuilds its whole
// per-widget stylesheet there on every theme change, and a second
// setStyleSheet() call would simply replace the first rather than add to it.
void makeSurfaceTransparent(QWidget* w);

// Installs the palette, the bundled font and the stylesheet. Call once, before
// any window is built.
void apply(QApplication& app);

// The bundled UI font family once apply() has run, or an empty string if the
// font failed to load and the platform default is in use.
QString fontFamily();

}  // namespace Theme
