#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStringList>
#include <QWidget>

#include <cmath>
#include <utility>

// Deliberately at global scope. Q_INIT_RESOURCE declares the initialiser as an
// extern at block scope, which binds to the innermost enclosing namespace - so
// calling it from inside `namespace Theme` would look for
// Theme::qInitResources_resources and fail to link.
static void furnifyInitResources()
{
    // A Qt resource compiled into a STATIC library is discarded by the linker
    // unless something references its initialiser. Nothing else does, so the
    // font silently would not exist at runtime.
    Q_INIT_RESOURCE(resources);
}

namespace Theme {

namespace {

// The bundled family apply() managed to load, or empty if it could not. Kept
// apart from the live spec because it is what defaultSpec() means by "the
// default family" - resetting the panel must go back to DM Sans, not to
// whatever the user last chose.
QString g_bundledFamily;

// The type scale's OFFSETS from Spec::basePt. The four sizes were literals
// here until Milestone 2; they are relationships now, and there are still
// exactly four of them.
constexpr double kBadgeOffset = -2.0;
constexpr double kLabelOffset = -1.0;
constexpr double kBodyOffset = 0.0;
constexpr double kTitleOffset = 3.0;

// The Graphite palette. The ONE place these nineteen-plus-two values exist -
// defaultSpec() hands out a copy and every accessor reads the live spec, so a
// hex literal still appears nowhere else in the application.
Spec graphite()
{
    Spec s;
    s.chrome            = QColor("#1b1b1d");
    s.panel             = QColor("#232326");
    s.chip              = QColor("#2c2c31");
    s.chipHover         = QColor("#34343a");
    s.chipActive        = QColor("#3d3d45");
    s.accent            = QColor("#3d7eff");
    s.text              = QColor("#f0f0f0");
    s.textMuted         = QColor("#9a9aa2");
    s.textDisabled      = QColor("#5c5c64");
    s.border            = QColor("#3a3a40");
    s.viewport          = QColor("#45454b");
    s.gridMinor         = QColor("#3e3e44");
    s.gridMajor         = QColor("#4d4d55");
    s.axisX             = QColor("#7a4a4a");   // muted red
    s.axisY             = QColor("#4a7a4a");   // muted green
    // The gizmo's own three hues - Unity's convention, byte-identical to what
    // AxisGizmo carried as a local hardcoded array before this task. See
    // Theme.h for why these are not named axisX/axisY/axisZ.
    s.gizmoAxisX        = QColor("#e0564a");   // vivid red
    s.gizmoAxisY        = QColor("#7fc84e");   // vivid green
    s.gizmoAxisZ        = QColor("#4a80e0");   // vivid blue
    s.sketchPointMarker = QColor("#ff4fc3");   // magenta - unclaimed by any
                                               // other viewport hue
    // Quantity_NOC_YELLOW, which is what displayOutline()/setPreview() drew
    // before this task rather than a token - written as hex for the same
    // reason the two highlight colours below are: pixel-identical to the
    // OCCT constant it replaces, so nothing on screen moves until edited.
    s.outlineLineColour = QColor("#ffff00");
    s.danger            = QColor("#e0564a");   // invalid input, failures
    s.focusRing         = QColor("#ffca4a");   // amber - distinct from
                                               // accent(), which already marks
                                               // the checked state
    s.focusRingMuted    = QColor("#9f7e2e");   // same hue, dimmed for an
                                               // inactive window
    // Quantity_NOC_CYAN1 and Quantity_NOC_ORANGE, which is what
    // OcctViewWidget passed to the highlight drawers before these were
    // tokens. Written as hex here so the whole palette reads as one table;
    // pixel-identical to the OCCT constants they replace.
    s.highlightHover    = QColor("#00ffff");
    s.highlightSelected = QColor("#ffa500");
    s.basePt = 10.0;
    s.chipStrokePx = 1.0;
    s.gridDensity = 1.0;
    s.edgeWidthPx = 1.0;
    s.sketchLineWidthPx = 2.0;
    return s;
}

// Function-local rather than a namespace-scope object: ViewportOverlay.cpp
// calls into this file from a static initialiser of its own, and a global
// with a dynamic initialiser here would be a static-order question nobody
// should have to answer.
Spec& mutableSpec()
{
    static Spec live = graphite();
    return live;
}

QFont scaledFontFor(const Spec& s, double offset, bool bold)
{
    QFont f;
    if (!s.fontFamily.isEmpty()) f.setFamily(s.fontFamily);
    f.setPointSizeF(s.basePt + offset);
    f.setBold(bold);
    return f;
}

QFont scaledFont(double offset, bool bold) { return scaledFontFor(spec(), offset, bold); }

// Palette + stylesheet + application font, from whatever spec() currently
// holds. apply() runs it once at startup and setSpec() runs it again on every
// edit, so there is one description of what "wearing this spec" means rather
// than a startup path and a live-edit path that can drift.
void install(QApplication& app)
{
    // The application default becomes bodyFont(): every widget that never
    // calls setFont()/setStyleSheet() for itself - including Qt's own
    // internals, like a status bar's temporary message label, that this code
    // never gets a pointer to - inherits this through ordinary Qt font
    // propagation, no stylesheet cascade involved. Widgets that need a
    // different scale size (titleFont(), labelFont(), badgeFont()) set it
    // explicitly at their own call site.
    app.setFont(bodyFont());

    QPalette palette;
    palette.setColor(QPalette::Window, chrome());
    palette.setColor(QPalette::WindowText, text());
    palette.setColor(QPalette::Base, panel());
    palette.setColor(QPalette::AlternateBase, chip());
    palette.setColor(QPalette::Text, text());
    palette.setColor(QPalette::Button, chip());
    palette.setColor(QPalette::ButtonText, text());
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled());
    app.setPalette(palette);

    // Compiled in as a raw string rather than a .qrc file: it avoids adding
    // AUTORCC and a resource tree for one asset, and is just as much "in the
    // binary" as a resource would be.
    //
    // Every colour is a NAMED placeholder substituted from the live spec
    // below, not a hex literal: this sheet used to be the one place a token's
    // value was written a second time, and a stylesheet that kept Graphite
    // while the accessors moved would have repainted half the shell and left
    // the other half behind. Named rather than positional (%1, %2) on
    // purpose - twenty-odd positional arguments in one string is a defect
    // waiting for the next token.
    //
    // font-size on QStatusBar/QStatusBar QLabel is the one place this
    // stylesheet carries a type-scale size: it is how labelFont() reaches
    // QStatusBar's own internal message label, which showMessage() creates
    // privately and this code never gets a pointer to. Read from labelFont()
    // rather than hand-typed, so it cannot drift from the scale in Theme.h.
    QString sheet = QStringLiteral(R"(
QMainWindow, QWidget       { background-color: @chrome; color: @text; }
QMenuBar                   { background-color: @chrome; color: @text;
                             border-bottom: 1px solid @border; padding: 2px; }
QMenuBar::item             { background: transparent; padding: 6px 12px;
                             border-radius: 4px; }
QMenuBar::item:selected    { background-color: @chipHover; }
QMenu                      { background-color: @panel; color: @text;
                             border: 1px solid @border; padding: 4px; }
QMenu::item                { padding: 6px 24px 6px 12px; border-radius: 4px; }
QMenu::item:selected       { background-color: @chipHover; }
QMenu::item:disabled       { color: @textDisabled; }
QMenu::separator           { height: 1px; background: @border; margin: 4px 8px; }
QStatusBar                 { background-color: @chrome; color: @textMuted;
                             border-top: 1px solid @border; font-size: @labelPt; }
QStatusBar QLabel          { color: @textMuted; font-size: @labelPt; }
QSplitter::handle          { background-color: @border; width: 1px; }
QToolTip                   { background-color: @panel; color: @text;
                             border: 1px solid @border; padding: 4px; }
QScrollBar:vertical        { background: @panel; width: 10px; margin: 0; }
QScrollBar::handle:vertical{ background: @border; border-radius: 5px;
                             min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");
    // Longest keys first: "@chipHover" contains "@chip", and a naive pass in
    // declaration order would rewrite the prefix and leave "Hover" stranded.
    const std::pair<const char*, QString> substitutions[] = {
        {"@chipHover", chipHover().name()},
        {"@textDisabled", textDisabled().name()},
        {"@textMuted", textMuted().name()},
        {"@chrome", chrome().name()},
        {"@border", border().name()},
        {"@panel", panel().name()},
        {"@chip", chip().name()},
        {"@text", text().name()},
        {"@labelPt", QString::number(labelFont().pointSizeF()) + QStringLiteral("pt")},
    };
    for (const auto& entry : substitutions)
        sheet.replace(QLatin1String(entry.first), entry.second);
    app.setStyleSheet(sheet);
}

}  // namespace

bool operator==(const Spec& a, const Spec& b)
{
    for (const ColourToken& token : colourTokens()) {
        if (a.*(token.member) != b.*(token.member)) return false;
    }
    return a.fontFamily == b.fontFamily &&
           std::fabs(a.basePt - b.basePt) < 1.0e-9 &&
           std::fabs(a.chipStrokePx - b.chipStrokePx) < 1.0e-9 &&
           std::fabs(a.gridDensity - b.gridDensity) < 1.0e-9 &&
           std::fabs(a.edgeWidthPx - b.edgeWidthPx) < 1.0e-9 &&
           std::fabs(a.sketchLineWidthPx - b.sketchLineWidthPx) < 1.0e-9;
}

const QVector<ColourToken>& colourTokens()
{
    // Ordered as the panel reads top to bottom: the 3D area first, because
    // that is what the user is looking at; then the shell's surfaces; then
    // text; then the accents that mark state.
    static const QVector<ColourToken> tokens = {
        {QStringLiteral("viewport"), &Spec::viewport},
        {QStringLiteral("gridMinor"), &Spec::gridMinor},
        {QStringLiteral("gridMajor"), &Spec::gridMajor},
        {QStringLiteral("axisX"), &Spec::axisX},
        {QStringLiteral("axisY"), &Spec::axisY},
        {QStringLiteral("gizmoAxisX"), &Spec::gizmoAxisX},
        {QStringLiteral("gizmoAxisY"), &Spec::gizmoAxisY},
        {QStringLiteral("gizmoAxisZ"), &Spec::gizmoAxisZ},
        {QStringLiteral("highlightHover"), &Spec::highlightHover},
        {QStringLiteral("highlightSelected"), &Spec::highlightSelected},
        {QStringLiteral("sketchPointMarker"), &Spec::sketchPointMarker},
        {QStringLiteral("outlineLineColour"), &Spec::outlineLineColour},
        {QStringLiteral("chrome"), &Spec::chrome},
        {QStringLiteral("panel"), &Spec::panel},
        {QStringLiteral("border"), &Spec::border},
        {QStringLiteral("chip"), &Spec::chip},
        {QStringLiteral("chipHover"), &Spec::chipHover},
        {QStringLiteral("chipActive"), &Spec::chipActive},
        {QStringLiteral("text"), &Spec::text},
        {QStringLiteral("textMuted"), &Spec::textMuted},
        {QStringLiteral("textDisabled"), &Spec::textDisabled},
        {QStringLiteral("accent"), &Spec::accent},
        {QStringLiteral("danger"), &Spec::danger},
        {QStringLiteral("focusRing"), &Spec::focusRing},
        {QStringLiteral("focusRingMuted"), &Spec::focusRingMuted},
    };
    return tokens;
}

Notifier* notifier()
{
    static Notifier instance;
    return &instance;
}

const Spec& spec() { return mutableSpec(); }

Spec defaultSpec()
{
    // The user's own shipped look — assets/defaultcolors.furnifytheme, baked
    // in as the default 2026-09-04 (Milestone 5, item 1). Graphite stays
    // above as the base it was derived from; the six assignments below are
    // exactly the deltas the user's file carries, so the diff against
    // graphite() IS the user's taste, readable at a glance: a near-black
    // viewport with grids to match, a violet accent, a tinted hover cyan,
    // and chips stroked at 2px.
    Spec s = graphite();
    s.viewport       = QColor("#1c1c1e");
    s.gridMinor      = QColor("#2d2d31");
    s.gridMajor      = QColor("#35353b");
    s.accent         = QColor("#6a00ff");
    s.highlightHover = QColor("#06d1ff");
    s.chipStrokePx   = 2.0;
    s.fontFamily = g_bundledFamily;
    return s;
}

void setSpec(const Spec& next)
{
    // A no-op set must not re-polish every widget in the application: a
    // colour dialog emits currentColorChanged for every mouse move inside its
    // wheel, and most of those land on the colour already installed.
    if (next == mutableSpec()) return;
    mutableSpec() = next;

    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        install(*app);

    notifier()->announce();
}

QString serializeSpec() { return serializeSpec(spec()); }

QString serializeSpec(const Spec& s)
{
    QStringList parts;
    for (const ColourToken& token : colourTokens())
        parts << token.id + QLatin1Char('=') + (s.*(token.member)).name();
    // A family name with a separator in it would produce a string this file
    // cannot read back. No installed family has one, but the guard costs a
    // line and the alternative is a stored appearance that silently refuses
    // to load.
    if (!s.fontFamily.contains(QLatin1Char(';')) && !s.fontFamily.contains(QLatin1Char('=')))
        parts << QStringLiteral("family=") + s.fontFamily;
    parts << QStringLiteral("base=") + QString::number(s.basePt);
    parts << QStringLiteral("chipStroke=") + QString::number(s.chipStrokePx);
    parts << QStringLiteral("gridDensity=") + QString::number(s.gridDensity);
    parts << QStringLiteral("edgeWidth=") + QString::number(s.edgeWidthPx);
    parts << QStringLiteral("sketchLineWidth=") + QString::number(s.sketchLineWidthPx);
    return parts.join(QLatin1Char(';'));
}

bool deserializeSpec(const QString& text, Spec& out)
{
    if (text.trimmed().isEmpty()) return false;

    Spec parsed = defaultSpec();
    bool sawSomething = false;

    for (const QString& piece : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int equals = piece.indexOf(QLatin1Char('='));
        if (equals <= 0) return false;   // not a key=value fragment at all
        const QString key = piece.left(equals).trimmed();
        const QString value = piece.mid(equals + 1).trimmed();

        if (key == QLatin1String("family")) {
            // An empty family is legitimate - it means "the platform
            // default", which is what an app whose bundled font failed to
            // load has been using all along.
            //
            // Anything else is checked against what this machine actually
            // has. A family that was uninstalled, or a spec carried to
            // another machine, otherwise sailed straight through: Qt's
            // matcher would quietly substitute something, the app would be
            // set in a font nobody chose, and the panel's combo would name a
            // family that is not in its own list. Falling back is not the
            // same as refusing - see the header: a missing font must not cost
            // the user every colour in the string.
            parsed.fontFamily = value.isEmpty() || QFontDatabase::families().contains(value)
                                    ? value
                                    : defaultSpec().fontFamily;
            sawSomething = true;
            continue;
        }
        if (key == QLatin1String("base")) {
            bool ok = false;
            const double pt = value.toDouble(&ok);
            if (!ok || pt < kMinBasePt || pt > kMaxBasePt) return false;
            parsed.basePt = pt;
            sawSomething = true;
            continue;
        }
        if (key == QLatin1String("chipStroke")) {
            bool ok = false;
            const double px = value.toDouble(&ok);
            if (!ok || px < kMinChipStrokePx || px > kMaxChipStrokePx) return false;
            parsed.chipStrokePx = px;
            sawSomething = true;
            continue;
        }
        if (key == QLatin1String("gridDensity")) {
            bool ok = false;
            const double density = value.toDouble(&ok);
            if (!ok || density < kMinGridDensity || density > kMaxGridDensity) return false;
            parsed.gridDensity = density;
            sawSomething = true;
            continue;
        }
        if (key == QLatin1String("edgeWidth")) {
            bool ok = false;
            const double px = value.toDouble(&ok);
            if (!ok || px < kMinEdgeWidthPx || px > kMaxEdgeWidthPx) return false;
            parsed.edgeWidthPx = px;
            sawSomething = true;
            continue;
        }
        if (key == QLatin1String("sketchLineWidth")) {
            bool ok = false;
            const double px = value.toDouble(&ok);
            if (!ok || px < kMinSketchLineWidthPx || px > kMaxSketchLineWidthPx) return false;
            parsed.sketchLineWidthPx = px;
            sawSomething = true;
            continue;
        }

        QColor Spec::*member = nullptr;
        for (const ColourToken& token : colourTokens()) {
            if (token.id == key) { member = token.member; break; }
        }
        // Tolerant on the KEY - see the header. A token this build has never
        // heard of is somebody else's, not corruption.
        if (!member) continue;

        const QColor colour(value);
        if (!colour.isValid()) return false;
        parsed.*member = colour;
        sawSomething = true;
    }

    if (!sawSomething) return false;
    out = parsed;
    return true;
}

QColor chrome()       { return spec().chrome; }
QColor panel()        { return spec().panel; }
QColor chip()         { return spec().chip; }
QColor chipHover()    { return spec().chipHover; }
QColor chipActive()   { return spec().chipActive; }
QColor accent()       { return spec().accent; }
QColor text()         { return spec().text; }
QColor textMuted()    { return spec().textMuted; }
QColor textDisabled() { return spec().textDisabled; }
QColor border()       { return spec().border; }
QColor viewport()     { return spec().viewport; }
QColor gridMinor()    { return spec().gridMinor; }
QColor gridMajor()    { return spec().gridMajor; }
QColor axisX()        { return spec().axisX; }
QColor axisY()        { return spec().axisY; }
QColor gizmoAxisX()   { return spec().gizmoAxisX; }
QColor gizmoAxisY()   { return spec().gizmoAxisY; }
QColor gizmoAxisZ()   { return spec().gizmoAxisZ; }
QColor sketchPointMarker() { return spec().sketchPointMarker; }
QColor outlineLineColour() { return spec().outlineLineColour; }
QColor danger()       { return spec().danger; }
QColor focusRing()    { return spec().focusRing; }
QColor focusRingMuted() { return spec().focusRingMuted; }
QColor highlightHover()    { return spec().highlightHover; }
QColor highlightSelected() { return spec().highlightSelected; }

double chipStrokePx() { return spec().chipStrokePx; }
double gridDensity()  { return spec().gridDensity; }
double edgeWidthPx()  { return spec().edgeWidthPx; }
double sketchLineWidthPx() { return spec().sketchLineWidthPx; }

QString fontFamily() { return spec().fontFamily; }

QFont titleFont() { return scaledFont(kTitleOffset, /*bold=*/true); }
QFont bodyFont()  { return scaledFont(kBodyOffset, /*bold=*/false); }
QFont labelFont() { return scaledFont(kLabelOffset, /*bold=*/false); }
QFont badgeFont() { return scaledFont(kBadgeOffset, /*bold=*/false); }

QFont titleFontFor(const Spec& s) { return scaledFontFor(s, kTitleOffset, /*bold=*/true); }
QFont bodyFontFor(const Spec& s)  { return scaledFontFor(s, kBodyOffset, /*bold=*/false); }
QFont labelFontFor(const Spec& s) { return scaledFontFor(s, kLabelOffset, /*bold=*/false); }
QFont badgeFontFor(const Spec& s) { return scaledFontFor(s, kBadgeOffset, /*bold=*/false); }

int motionMs() { return 160; }
QEasingCurve motionCurve() { return QEasingCurve(QEasingCurve::OutCubic); }

int surfaceShadowMargin() { return 0; }

void drawCrispBorder(QPainter& p, const QRectF& rect, const QColor& colour,
                     double radius, double width)
{
    // Half the pen width inward, so the stroke's OUTER edge lands on the
    // outer edge of `rect` and the stroke itself covers whole pixels. The
    // radius follows the path in, or the corners would bulge by the same
    // half-pixel the sides just lost.
    const double inset = width / 2.0;
    const double r = radius > inset ? radius - inset : 0.0;

    QPainterPath path;
    path.addRoundedRect(rect.adjusted(inset, inset, -inset, -inset), r, r);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(colour, width));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

void drawCrispRule(QPainter& p, const QPointF& from, const QPointF& to, const QColor& colour)
{
    QPointF a = from;
    QPointF b = to;
    // Only the constant coordinate can be snapped: snapping the other one
    // would shorten the rule by half a pixel at each end for no benefit.
    if (std::abs(from.y() - to.y()) < 0.001) {
        const double y = std::floor(from.y()) + 0.5;
        a.setY(y);
        b.setY(y);
    }
    if (std::abs(from.x() - to.x()) < 0.001) {
        const double x = std::floor(from.x()) + 0.5;
        a.setX(x);
        b.setX(x);
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(colour, 1.0));
    p.drawLine(a, b);
    p.restore();
}

void paintSurface(QPainter& p, const QRect& rect, int radius)
{
    // The rounded panel, opaque, no shadow - see the header for why
    // translucent pixels have no place in this family. The border is then
    // stroked crisply along the panel's outer edge, so the outermost row and
    // column of the ROUNDED shape are its border rather than a half-covered
    // blend of border and fill. Outside that shape, within `rect`, nothing is
    // painted at all any more - see the header for what used to fill that
    // area and why it does not any more.
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath surface;
    surface.addRoundedRect(rect, radius, radius);
    p.fillPath(surface, panel());
    p.restore();

    drawCrispBorder(p, QRectF(rect), border(), radius);
}

int wholeDevicePixels(int logical)
{
    // Up to the next multiple of four - whole at every quarter-step display
    // scale Windows offers, and therefore not a function of the ratio this
    // machine happens to run. See Theme.h for what the leftover row does over
    // the GL surface, and why nothing paintSurface() can do reaches it.
    constexpr int kStep = 4;
    if (logical <= 0) return logical;
    const int remainder = logical % kStep;
    return remainder == 0 ? logical : logical + (kStep - remainder);
}

QSize wholeDevicePixels(const QSize& logical)
{
    return QSize(wholeDevicePixels(logical.width()), wholeDevicePixels(logical.height()));
}

int snapToDevicePixels(int value, int offsetToWindow, double devicePixelRatio)
{
    // The smallest step whose device extent is whole at this ratio. 1 covers
    // every integer ratio, 2 covers 1.5 and 2.5, 4 covers the quarter steps -
    // and 4 is the fallback for anything stranger, which is what
    // wholeDevicePixels() assumes unconditionally.
    int step = 4;
    for (const int candidate : {1, 2}) {
        const double device = candidate * devicePixelRatio;
        if (std::fabs(device - std::round(device)) < 1.0e-9) {
            step = candidate;
            break;
        }
    }
    if (step <= 1) return value;

    // Snapped in WINDOW coordinates - see the header - and downward, so a card
    // already clamped inside the viewport's edges cannot be pushed back out.
    const int inWindow = value + offsetToWindow;
    const int remainder = ((inWindow % step) + step) % step;   // never negative
    return inWindow - remainder - offsetToWindow;
}

void makeSurfaceTransparent(QWidget* w)
{
    if (!w) return;
    // Plain "background: transparent", no selector: a per-widget stylesheet
    // needs none, since it only ever applies to the widget it is set on, and
    // it wins over the app-wide `QWidget { background-color: @chrome }` rule
    // for this widget regardless of that rule's own selector specificity -
    // see the header for the mechanism and ToolChip::applyTheme(), which
    // found it first.
    w->setStyleSheet(QStringLiteral("background: transparent;"));
}

void apply(QApplication& app)
{
    // DM Sans, compiled in as a Qt resource. If it cannot be loaded we keep the
    // platform default rather than falling back to something arbitrary - a
    // missing font should not change the layout in a way nobody chose.
    furnifyInitResources();

    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) g_bundledFamily = families.first();
    }

    // Now that the bundled family is known, the live spec IS the default one -
    // which is what makes `spec() == defaultSpec()` true for a user who has
    // never opened the Appearance panel, and what any later reset goes back
    // to. A persisted spec is installed by MainWindow after this, through
    // setSpec(), so there is one path that applies a spec and no second one.
    mutableSpec() = defaultSpec();
    install(app);
}

}  // namespace Theme
