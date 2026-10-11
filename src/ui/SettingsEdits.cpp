#include "SettingsEdits.h"

#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SettingsEdits {

namespace {

// The drawer's copy keeps the translation context it was written under.
QString tr(const char* text)
{
    return QCoreApplication::translate("AppearancePanel", text);
}

// A colour file is a spec and nothing else - the same string QSettings holds -
// so there is no size at which reading more of one is useful. A cap rather
// than a trusting readAll(): Load colours takes a path from a file dialog,
// which is to say from anywhere, and a serialised spec is a few hundred bytes.
constexpr qint64 kMaxColourFileBytes = 64 * 1024;

double Theme::Spec::*memberFor(const QString& id)
{
    if (id == QLatin1String("basePt")) return &Theme::Spec::basePt;
    if (id == QLatin1String("chipStrokePx")) return &Theme::Spec::chipStrokePx;
    if (id == QLatin1String("gridDensity")) return &Theme::Spec::gridDensity;
    if (id == QLatin1String("edgeWidthPx")) return &Theme::Spec::edgeWidthPx;
    if (id == QLatin1String("sketchLineWidthPx")) return &Theme::Spec::sketchLineWidthPx;
    if (id == QLatin1String("gizmoScale")) return &Theme::Spec::gizmoScale;
    return nullptr;
}

}  // namespace

const QVector<TabInfo>& tabs()
{
    static const QVector<TabInfo> table = {
        {Tab::Colours, tr("Colours"), tr("Every colour the app draws with")},
        {Tab::TextLines, tr("Text & lines"), tr("Text size, the font, and the app's line weights")},
        {Tab::Viewport, tr("Viewport"), tr("What the 3D area shows around your furniture")},
        {Tab::UnitsSaving, tr("Working"),
         tr("The unit sizes are written in, and how often this furniture is saved to disk")},
    };
    return table;
}

const QVector<NumberField>& numberFields()
{
    // In the order the Text & lines tab shows its five, then the Viewport
    // tab's two. Verbatim from the spin boxes they describe.
    static const QVector<NumberField> fields = {
        {QStringLiteral("basePt"), tr("Text size"), Theme::kMinBasePt, Theme::kMaxBasePt, 1.0, 0,
         tr(" pt"), tr("How big the app's text is — everything else in the type scale moves with it")},
        {QStringLiteral("edgeWidthPx"), tr("Edge lines"), Theme::kMinEdgeWidthPx, Theme::kMaxEdgeWidthPx,
         0.5, 1, tr(" px"),
         tr("How thick the lines along a body's own edges are — 0 leaves the shading with none at all")},
        {QStringLiteral("sketchLineWidthPx"), tr("Outline lines"), Theme::kMinSketchLineWidthPx,
         Theme::kMaxSketchLineWidthPx, 0.5, 1, tr(" px"),
         tr("How thick the line an outline draws is — while it is being drawn and once it is closed")},
        {QStringLiteral("chipStrokePx"), tr("Button border"), Theme::kMinChipStrokePx,
         Theme::kMaxChipStrokePx, 1.0, 0, tr(" px"),
         tr("How thick the line around the tool buttons is — 0 leaves only the fill")},
        // A bare "x": a multiplier, not a length, so never near Measure.
        {QStringLiteral("gridDensity"), tr("Grid detail"), Theme::kMinGridDensity, Theme::kMaxGridDensity,
         0.1, 1, QStringLiteral("x"),
         tr("How many lines the work-plane grid draws — higher packs more in, lower thins it out")},
        {QStringLiteral("gizmoScale"), tr("Gizmo size"), Theme::kMinGizmoScale, Theme::kMaxGizmoScale, 0.1,
         1, QStringLiteral("x"), tr("How large the Move, Rotate and Scale handles draw over a selected body")},
    };
    return fields;
}

const NumberField* numberField(const QString& id)
{
    for (const NumberField& field : numberFields())
        if (field.id == id) return &field;
    return nullptr;
}

double numberValue(const Theme::Spec& spec, const QString& id)
{
    double Theme::Spec::*member = memberFor(id);
    return member ? spec.*member : std::numeric_limits<double>::quiet_NaN();
}

bool setNumber(const QString& id, double value)
{
    const NumberField* field = numberField(id);
    double Theme::Spec::*member = memberFor(id);
    if (!field || !member || std::isnan(value)) return false;
    Theme::Spec next = Theme::spec();
    next.*member = std::clamp(value, field->min, field->max);
    Theme::setSpec(next);
    return true;
}

QString tokenName(const QString& id)
{
    // The user's words, never the code's. `gridMinor` is a member name;
    // "Grid lines" is what somebody choosing a colour is looking for.
    static const QHash<QString, QString> names = {
        {QStringLiteral("viewport"), QObject::tr("Viewport")},
        // What the bodies are shaded in while modelling - render mode dresses
        // them in their own wood instead, which is why the row says so.
        {QStringLiteral("body"), QObject::tr("Bodies while modelling")},
        {QStringLiteral("gridMinor"), QObject::tr("Grid lines")},
        {QStringLiteral("gridMajor"), QObject::tr("Grid — every tenth line")},
        {QStringLiteral("axisX"), QObject::tr("X axis")},
        {QStringLiteral("axisY"), QObject::tr("Y axis")},
        {QStringLiteral("gizmoAxisX"), QObject::tr("Orientation gizmo — X")},
        {QStringLiteral("gizmoAxisY"), QObject::tr("Orientation gizmo — Y")},
        {QStringLiteral("gizmoAxisZ"), QObject::tr("Orientation gizmo — Z")},
        {QStringLiteral("highlightHover"), QObject::tr("Hover highlight")},
        {QStringLiteral("highlightSelected"), QObject::tr("Selection highlight")},
        {QStringLiteral("dimensionLine"), QObject::tr("Measuring line")},
        {QStringLiteral("sizesOneBody"), QObject::tr("Sizes — one body")},
        {QStringLiteral("sizesGroup"), QObject::tr("Sizes — several bodies")},
        // Named by what happens to the wood, not by the operation - the same
        // pair serves Subtract, Union and Intersect.
        {QStringLiteral("booleanOut"), QObject::tr("Material coming out")},
        {QStringLiteral("booleanStay"), QObject::tr("Material staying")},
        {QStringLiteral("sketchPointMarker"), QObject::tr("Outline points")},
        {QStringLiteral("outlineLineColour"), QObject::tr("Outline lines — colour")},
        {QStringLiteral("chrome"), QObject::tr("Top bar and status bar")},
        {QStringLiteral("panel"), QObject::tr("Panels")},
        {QStringLiteral("border"), QObject::tr("Borders")},
        {QStringLiteral("chip"), QObject::tr("Buttons")},
        {QStringLiteral("chipHover"), QObject::tr("Buttons — hovered")},
        {QStringLiteral("chipActive"), QObject::tr("Buttons — pressed")},
        {QStringLiteral("text"), QObject::tr("Text")},
        {QStringLiteral("textMuted"), QObject::tr("Quieter text")},
        {QStringLiteral("textDisabled"), QObject::tr("Unavailable text")},
        {QStringLiteral("accent"), QObject::tr("Accent")},
        // The accent's own ink for words - readable ON an accent fill.
        {QStringLiteral("accentText"), QObject::tr("Accent text")},
        // Failures, not warnings: a refusal that already happened.
        {QStringLiteral("danger"), QObject::tr("Failures")},
        {QStringLiteral("dangerText"), QObject::tr("Failure text")},
        // A caution is not a failure: a fact worth seeing that refuses nothing.
        {QStringLiteral("caution"), QObject::tr("Caution")},
    };
    return names.value(id);
}

QString groupHeadingFor(const QString& id)
{
    // Keyed on the token id rather than a position, so a token added to
    // Theme::Spec lands in the run it belongs to.
    static const QHash<QString, QString> openers = {
        {QStringLiteral("viewport"), tr("Viewport")},
        {QStringLiteral("axisX"), tr("Axes and gizmo")},
        {QStringLiteral("highlightHover"), tr("Selection and state")},
        {QStringLiteral("dimensionLine"), tr("Annotations")},
        {QStringLiteral("chrome"), tr("Panels and chips")},
    };
    return openers.value(id);
}

bool isRetiredToken(const QString& id)
{
    return id == QLatin1String("focusRing") || id == QLatin1String("focusRingMuted");
}

void setTokenColour(const QString& id, const QColor& colour)
{
    if (!colour.isValid()) return;
    Theme::Spec next = Theme::spec();
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        if (token.id != id) continue;
        next.*(token.member) = colour;
        Theme::setSpec(next);
        return;
    }
}

void setFontFamily(const QString& family)
{
    Theme::Spec next = Theme::spec();
    next.fontFamily = family;
    Theme::setSpec(next);
}

QString familyDisplayName(const QString& family)
{
    // A trailing optical size - " 14pt", " 9 pt" - is the name of a CUT, not
    // of the family.
    static const QRegularExpression opticalSize(QStringLiteral("\\s+\\d+(\\.\\d+)?\\s*pt$"),
                                                QRegularExpression::CaseInsensitiveOption);
    QString shown = family;
    shown.remove(opticalSize);
    return shown.isEmpty() ? family : shown;
}

QVector<QPair<QString, QString>> fontFamilies()
{
    // The bundled family first, because it is the default; then everything
    // installed. EACH SHOWN NAME ONCE: the bundled file registers "DM Sans"
    // beside the cut Theme applies ("DM Sans 14pt"), the same face - the first
    // listed wins, which puts the bundled cut first.
    QStringList families;
    const QString bundled = Theme::defaultSpec().fontFamily;
    if (!bundled.isEmpty()) families << bundled;
    for (const QString& family : QFontDatabase::families())
        if (family != bundled) families << family;
    QVector<QPair<QString, QString>> out;
    QSet<QString> shownNames;
    for (const QString& family : families) {
        const QString shown = familyDisplayName(family);
        if (shownNames.contains(shown)) continue;
        shownNames.insert(shown);
        out.push_back({shown, family});
    }
    return out;
}

void reset()
{
    Theme::setSpec(Theme::defaultSpec());
}

bool saveColoursTo(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    const QByteArray payload = Theme::serializeSpec().toUtf8();
    // Both halves checked: a write can come up short on a full disk without
    // the open having failed.
    if (file.write(payload) != payload.size()) return false;
    return file.flush();
}

bool loadColoursFrom(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(file.read(kMaxColourFileBytes));
    // deserializeSpec() leaves `loaded` untouched when it refuses, so half a
    // bad file can never be applied.
    Theme::Spec loaded;
    if (!Theme::deserializeSpec(text, loaded)) return false;
    Theme::setSpec(loaded);
    return true;
}

QString colourFileSuffix() { return QStringLiteral(".furnifytheme"); }

QString colourFileFilter()
{
    return tr("FurnifyMe colours (*%1)").arg(colourFileSuffix());
}

QString resetLabel() { return tr("Reset colours and sizes"); }
QString resetArmedLabel() { return tr("Reset — click again"); }

}  // namespace SettingsEdits
