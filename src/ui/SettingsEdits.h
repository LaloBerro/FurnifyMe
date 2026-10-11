#pragma once

// THE SETTINGS DRAWER'S MODEL (migration Phase 3b), carved out of
// AppearancePanel so two drawers - the Qt card gui_smoke gates and the HTML
// card the editor shows in HTML-chrome mode - read one set of tables and drive
// one set of edits. No widgets here: what the tabs are called, which rows the
// drawer has and in what order, what each number field's range, step and
// words are, what each colour token is called and which group it opens, and
// every write to Theme::Spec. AppearancePanel's own public setters are now
// one-line delegations here (the suite still calls them), and the HTML card's
// SettingsBridge calls the same functions - so an edit cannot mean two
// things depending on which drawer made it.
//
// Every Theme::Spec write goes through Theme::setSpec(), which broadcasts on
// Theme::notifier(); both drawers re-read on that broadcast and neither
// caches a value across it. Copy here is the drawer's copy, verbatim, and it
// is swept for banned words through both drawers' painted texts.

#include "Theme.h"

#include <QColor>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

class QAction;
class QColorDialog;
class QWidget;

namespace SettingsEdits {

// The four tabs, in the order they are drawn - an index on both drawers.
// UnitsSaving is "Working" on screen (the 2026-10 review's F22).
enum class Tab { Colours = 0, TextLines = 1, Viewport = 2, UnitsSaving = 3 };
constexpr int kTabCount = 4;
struct TabInfo {
    Tab tab;
    QString name;
    QString tip;
};
const QVector<TabInfo>& tabs();

// A Theme::Spec number the drawer edits in a field: Text size, the three line
// widths, Grid detail, Gizmo size. `suffix` is shown after the value and
// stripped from typed input; a value is shown with `decimals` places.
struct NumberField {
    QString id;   // basePt, edgeWidthPx, sketchLineWidthPx, chipStrokePx, gridDensity, gizmoScale
    QString label;
    double min = 0.0;
    double max = 0.0;
    double step = 1.0;
    int decimals = 0;
    QString suffix;
    QString tip;
};
const QVector<NumberField>& numberFields();
// Null for an unknown id.
const NumberField* numberField(const QString& id);
// The field's value in `spec`, or NaN for an unknown id.
double numberValue(const Theme::Spec& spec, const QString& id);
// Clamped to the field's range and written through Theme::setSpec(). False,
// and nothing written, for an unknown id.
bool setNumber(const QString& id, double value);

// One row of the Viewport and Working tabs, composed by MainWindow in the
// order the drawer shows them. Toggle: one checkable action. Choice: a set of
// exclusive actions, `optionLabels` beside them, `stacked` one per line.
// Binary: ONE checkable action drawn as two chips, optionLabels = {off, on}.
// Number: a Theme::Spec field, `numberId` naming it.
struct Row {
    enum class Kind { Toggle, Choice, Binary, Number };
    Kind kind = Kind::Toggle;
    Tab tab = Tab::Viewport;
    QString label;
    QVector<QAction*> actions;
    QStringList optionLabels;
    bool stacked = false;
    QString numberId;
};

// The user's word for a colour token (`gridMinor` -> "Grid lines"); empty for
// an id with no name.
QString tokenName(const QString& id);
// The heading that opens a run of tokens, above the token that starts it;
// empty for every other token.
QString groupHeadingFor(const QString& id);
// Kept in Theme::Spec so old colour files still load, painted by nothing, so
// given no row.
bool isRetiredToken(const QString& id);

void setTokenColour(const QString& id, const QColor& colour);
void setFontFamily(const QString& family);
// A family's name without a trailing optical size ("DM Sans 14pt" -> "DM Sans").
QString familyDisplayName(const QString& family);
// (shown name, real family): the bundled family first, then every installed
// one, each shown name once.
QVector<QPair<QString, QString>> fontFamilies();
// THE colour picker, for a token, over `parent`: Qt's QColorDialog, MODELESS
// (show(), never open() - open() forces window modality, and nothing in this
// app blocks), live on every move inside it, Cancel putting back the colour
// it opened on. One picker at a time across both drawers: a second call
// closes the first. Returns the picker.
QColorDialog* openColourPicker(const QString& id, QWidget* parent);
// The picker that is open now, or null.
QColorDialog* activeColourPicker();

// Back to Theme::defaultSpec() - every colour, the text size and font, the
// line widths, Grid detail and Gizmo size.
void reset();

// A look as a file: the same string QSettings stores. loadColoursFrom()
// changes nothing when it refuses (Theme::deserializeSpec()'s contract).
bool saveColoursTo(const QString& path);
bool loadColoursFrom(const QString& path);
QString colourFileSuffix();   // ".furnifytheme"
QString colourFileFilter();

// Reset takes two clicks: the first arms it for kResetConfirmMs and changes
// nothing (version Delete's confirm).
constexpr int kResetConfirmMs = 4000;
QString resetLabel();
QString resetArmedLabel();

}  // namespace SettingsEdits
