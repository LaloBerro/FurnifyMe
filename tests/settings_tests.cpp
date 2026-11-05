// The Settings drawer's model and its bridge (migration Phase 3b), on Qt's
// offscreen platform - no window, no GPU, no page: the tables both drawers
// read and the one edit path both drive.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "HtmlCardSlot.h"
#include "SettingsEdits.h"
#include "Themes.h"
#include "ThemeShell.h"
#include "IconSet.h"
#include "session/ActionRegistry.h"
#include "web/SettingsBridge.h"
#include "ViewportOverlay.h"
#include "Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QFontDatabase>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}
}  // namespace

// WCAG 2 contrast between two opaque colours.
double contrast(const QColor& a, const QColor& b)
{
    auto lum = [](const QColor& c) {
        auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    const double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Every pair at least `minimum` apart in RGB.
bool distinct(const std::vector<QColor>& colours, double minimum)
{
    for (size_t i = 0; i < colours.size(); ++i)
        for (size_t j = i + 1; j < colours.size(); ++j) {
            const double dr = colours[i].red() - colours[j].red(), dg = colours[i].green() - colours[j].green(),
                         db = colours[i].blue() - colours[j].blue();
            if (std::sqrt(dr * dr + dg * dg + db * db) < minimum) {
                std::printf("       (%s and %s are %.0f apart)\n", qPrintable(colours[i].name()),
                            qPrintable(colours[j].name()), std::sqrt(dr * dr + dg * dg + db * db));
                return false;
            }
        }
    return true;
}

void check(bool ok, const QByteArray& what) { check(ok, what.constData()); }

int main(int argc, char* argv[])
{
    // NEVER A DIALOG - see tests/session.cpp.
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::apply(app);

    // --- the tables ---------------------------------------------------------
    check(SettingsEdits::tabs().size() == 4 && SettingsEdits::tabs()[3].name == QStringLiteral("Working"),
          "four tabs, the last named Working");
    const SettingsEdits::NumberField* size = SettingsEdits::numberField(QStringLiteral("basePt"));
    check(size && size->min == Theme::kMinBasePt && size->max == Theme::kMaxBasePt &&
              size->suffix == QStringLiteral(" pt") && size->decimals == 0,
          "Text size is 8-14 pt, whole points");
    const SettingsEdits::NumberField* edge = SettingsEdits::numberField(QStringLiteral("edgeWidthPx"));
    check(edge && edge->step == 0.5 && edge->decimals == 1 && edge->suffix == QStringLiteral(" px"),
          "Edge lines step by 0.5 px");
    const SettingsEdits::NumberField* grid = SettingsEdits::numberField(QStringLiteral("gridDensity"));
    check(grid && grid->suffix == QStringLiteral("x"), "Grid detail is a bare multiplier");
    check(SettingsEdits::tokenName(QStringLiteral("gridMajor")) == QString::fromUtf8("Grid — every tenth line"),
          "token names are the user's words");
    check(SettingsEdits::groupHeadingFor(QStringLiteral("axisX")) == QStringLiteral("Axes and gizmo") &&
              SettingsEdits::groupHeadingFor(QStringLiteral("axisY")).isEmpty(),
          "a heading opens a group, and only there");
    check(SettingsEdits::isRetiredToken(QStringLiteral("focusRing")), "the focus-ring tokens are retired");

    // --- themes as data ----------------------------------------------------------
    check(Themes::all().size() == 5 && Themes::all().front().id == Themes::defaultId() &&
              Themes::defaultId() == QStringLiteral("violet-glass-float"),
          "five themes, the default first");
    check(Themes::baseSpec(Themes::graphiteId()) == Theme::defaultSpec(), "Graphite is the shipped spec itself");
    for (const Themes::Info& info : Themes::all()) {
        const Theme::Spec s = Themes::baseSpec(info.id);
        const QByteArray id = info.id.toUtf8();
        check(!info.name.isEmpty() && !info.fontFamily.isEmpty(), "every theme has a name and a typeface: " + id);
        check(contrast(s.text, s.panel) >= 4.5, "text reads on a panel: " + id);
        check(contrast(s.accentText, s.chipActive) >= 4.5, "accent words read on a pressed chip: " + id);
        // Every viewport ink against every other, and against the bodies they
        // sit on (review I2): DESIGN.md - distinct within the theme, checked
        // against its own body.
        check(distinct({s.highlightHover, s.highlightSelected, s.sizesOneBody, s.sizesGroup, s.dimensionLine}, 60),
              "hover, selection, both size inks and the outline line are told apart: " + id);
        check(distinct({s.gizmoAxisX, s.gizmoAxisY, s.gizmoAxisZ}, 60), "the gizmo arms are told apart: " + id);
        for (const QColor& ink : {s.highlightHover, s.highlightSelected, s.sizesOneBody, s.sizesGroup, s.dimensionLine})
            check(distinct({ink, s.body}, 80), "and each stands off the bodies: " + id + " " + ink.name().toUtf8());
        // The grid's own axis lines stay muted below the gizmo arms (review
        // I3), or an arm lying along an axis disappears into it.
        check(s.axisX.lightness() < s.gizmoAxisX.lightness() && s.axisY.lightness() < s.gizmoAxisY.lightness(),
              "the grid's axis lines are quieter than the gizmo arms: " + id);
        check(s.body.hsvSaturationF() < 0.1, "bodies stay grey: " + id);
        check(s.fontFamily == info.fontFamily, "the theme's typeface is its spec's: " + id);
        check(QFontDatabase::families().contains(info.fontFamily), "the theme's typeface is installed: " + id);
    }
    check(Themes::baseSpec(QStringLiteral("aurora-mesh-gradient")).highlightSelected !=
              Themes::baseSpec(Themes::graphiteId()).highlightSelected,
          "a theme may recolour the viewport's state inks");
    check(!Themes::exists(QStringLiteral("nope")) &&
              Themes::baseSpec(QStringLiteral("nope")) == Themes::baseSpec(Themes::defaultId()),
          "an unknown id answers the default");

    // --- switching themes ------------------------------------------------------------
    {
        QHash<QString, QString> saved;
        Themes::setStore({[&](const QString& id) { return saved.value(id); },
                          [&](const QString& id, const QString& spec) { saved.insert(id, spec); }});
        QString announced;
        QObject::connect(Themes::notifier(), &Themes::Notifier::currentChanged,
                         [&](const QString& id) { announced = id; });
        check(Themes::currentId() == Themes::graphiteId(), "a window starts on Graphite until a theme is chosen");
        check(Themes::select(QStringLiteral("nightshade-glass")) &&
                  Theme::spec() == Themes::baseSpec(QStringLiteral("nightshade-glass")),
              "choosing a theme installs its base");
        check(announced == QStringLiteral("nightshade-glass"), "and says so");
        SettingsEdits::setTokenColour(QStringLiteral("accent"), QColor(1, 2, 3));
        Themes::select(Themes::graphiteId());
        check(Theme::spec().accent != QColor(1, 2, 3), "another theme does not wear that edit");
        check(saved.contains(QStringLiteral("nightshade-glass")), "the theme left behind is stored");
        // Only what the user changed (review I1): a base fix in a later build
        // must still reach a theme the user has visited.
        check(saved.value(QStringLiteral("nightshade-glass")).contains(QStringLiteral("accent=")) &&
                  !saved.value(QStringLiteral("nightshade-glass")).contains(QStringLiteral("panel=")),
              "a theme keeps only the user's own changes, not a copy of its base");
        check(saved.value(Themes::graphiteId()).isEmpty(), "and a theme nobody changed keeps nothing at all");
        Themes::select(QStringLiteral("nightshade-glass"));
        check(Theme::spec().accent == QColor(1, 2, 3), "coming back brings the edit back (Review Focus 2)");
        SettingsEdits::reset();
        check(Theme::spec() == Themes::baseSpec(QStringLiteral("nightshade-glass")),
              "Reset puts back the theme's own base");
        announced.clear();
        check(!Themes::select(QStringLiteral("from-a-newer-build")) &&
                  Themes::currentId() == QStringLiteral("nightshade-glass") && announced.isEmpty(),
              "an unknown theme changes nothing (Review Focus 3)");
        check(Themes::select(QStringLiteral("nightshade-glass")) && announced.isEmpty(),
              "choosing the theme already on changes nothing either");
        // A colour picker open across a switch: it closes, and its Cancel
        // restore does not write into the theme that arrives (Review Focus 1).
        QWidget host;
        SettingsEdits::openColourPicker(QStringLiteral("accent"), &host);
        check(SettingsEdits::activeColourPicker() != nullptr, "a picker is open");
        Themes::select(QStringLiteral("aurora-mesh-gradient"));
        QCoreApplication::processEvents();
        check(SettingsEdits::activeColourPicker() == nullptr, "switching theme closes the picker");
        check(Theme::spec() == Themes::baseSpec(QStringLiteral("aurora-mesh-gradient")),
              "and its Cancel wrote nothing into the new theme");
        Themes::select(Themes::graphiteId());
        Themes::setStore({});
    }
    {
        // STARTUP: the user's stored Graphite edits must survive the first
        // choice of a theme - a select() from the window's starting Graphite
        // would store the untouched default over them first.
        QHash<QString, QString> saved;
        Theme::Spec mine = Theme::defaultSpec();
        mine.accent = QColor(9, 8, 7);
        saved.insert(Themes::graphiteId(), Theme::serializeSpec(mine));
        Themes::setStore({[&](const QString& id) { return saved.value(id); },
                          [&](const QString& id, const QString& spec) { saved.insert(id, spec); }});
        Themes::start(Themes::defaultId());
        check(Themes::currentId() == Themes::defaultId() && Theme::spec() == Themes::baseSpec(Themes::defaultId()),
              "starting a window puts its theme on");
        Themes::select(Themes::graphiteId());
        check(Theme::spec().accent == QColor(9, 8, 7), "and the user's Graphite edits are still theirs");
        Themes::start(QStringLiteral("not-a-theme"));
        check(Themes::currentId() == Themes::defaultId(), "a stored id that is not a theme starts the default");
        Themes::select(Themes::graphiteId());
        Themes::setStore({});
        SettingsEdits::reset();
    }

    // --- the edits ----------------------------------------------------------
    SettingsEdits::setNumber(QStringLiteral("basePt"), 99);
    check(Theme::spec().basePt == Theme::kMaxBasePt, "a number is clamped to its range");
    check(!SettingsEdits::setNumber(QStringLiteral("nonsense"), 1), "an unknown field is refused");
    check(std::isnan(SettingsEdits::numberValue(Theme::spec(), QStringLiteral("nonsense"))),
          "and has no value");
    SettingsEdits::setTokenColour(QStringLiteral("accent"), QColor(10, 20, 30));
    check(Theme::spec().accent == QColor(10, 20, 30), "a colour edit lands in the live spec");
    const auto families = SettingsEdits::fontFamilies();
    check(!families.isEmpty() && families.front().second == Theme::defaultSpec().fontFamily,
          "the bundled family is listed first");

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mine.furnifytheme"));
    check(SettingsEdits::saveColoursTo(path), "colours save to a file");
    SettingsEdits::reset();
    check(Theme::spec() == Theme::defaultSpec(), "reset puts the shipped spec back");
    check(SettingsEdits::loadColoursFrom(path) && Theme::spec().accent == QColor(10, 20, 30),
          "and loading the file brings the colours back");
    const QString junk = dir.filePath(QStringLiteral("junk.furnifytheme"));
    {
        QFile f(junk);
        if (!f.open(QIODevice::WriteOnly)) return 3;
        f.write("not a colour file");
    }
    const Theme::Spec before = Theme::spec();
    check(!SettingsEdits::loadColoursFrom(junk) && Theme::spec() == before,
          "a file that is not a colour file changes nothing");

    // --- a place in the overlay for an HTML card --------------------------------
    {
        QWidget viewport;
        viewport.resize(1000, 700);
        ViewportOverlay overlay(&viewport);
        QWidget top(&viewport);
        top.setFixedSize(100, 50);
        QWidget card(&viewport);
        card.setFixedSize(296, 380);
        overlay.addWidget(&top, ViewportOverlay::Anchor::TopRight);
        overlay.addWidget(&card, ViewportOverlay::Anchor::TopRight);
        viewport.show();
        overlay.relayout();
        const QRect was = card.geometry();
        HtmlCardSlot slot(QStringLiteral("settings"), QSize(296, 380), &viewport);
        int placed = 0;
        QObject::connect(&slot, &HtmlCardSlot::placed, [&] { ++placed; });
        check(overlay.replaceWidget(&card, &slot), "a card can be swapped for a slot");
        slot.show();
        overlay.relayout();
        check(slot.geometry() == was, "the slot stands exactly where the card stood");
        check(!card.isVisible(), "and the card it replaced is hidden");
        check(placed > 0, "a slot says when it is placed");
        bool listed = false;
        for (const QRect& r : overlay.occupiedRects()) listed = listed || r == slot.geometry();
        check(listed, "the slot is an obstacle the toast steps around");
        check(slot.testAttribute(Qt::WA_TransparentForMouseEvents), "and takes no click itself");
        check(slot.objectName() == QStringLiteral("htmlCardSlot_settings"), "a slot is found by its card's name");
        check(!overlay.replaceWidget(&card, &slot), "a widget that is not an entry is refused");
    }

    // --- a slot whose height the page sets (Phase 3c: the Versions drawer) ------
    {
        QWidget host;
        host.resize(800, 600);
        HtmlCardSlot slot(QStringLiteral("versions"), QSize(240, 160), &host);
        // Shown: a hidden widget is sent no resize until it shows, and a slot
        // only stands anywhere while it is shown (its show says placed()).
        host.show();
        slot.show();
        int grew = 0, placed = 0;
        QObject::connect(&slot, &HtmlCardSlot::contentHeightChanged, [&] { ++grew; });
        QObject::connect(&slot, &HtmlCardSlot::placed, [&] { ++placed; });
        slot.setContentHeight(301);
        check(slot.height() == Theme::wholeDevicePixels(301) && slot.width() == Theme::wholeDevicePixels(240),
              QStringLiteral("a page-set height takes whole device pixels and keeps the width (%1)").arg(slot.height()).toUtf8());
        check(grew == 1 && placed >= 1, "and says so twice: to the overlay and to the page");
        slot.setContentHeight(301);
        check(grew == 1, "the same height again says nothing");
        slot.setContentHeight(0);
        check(slot.height() >= 1, "a zero height never collapses the slot to nothing");
    }

    // --- the page's settings object ---------------------------------------------
    {
        ActionRegistry registry;
        QObject owner;
        auto* grid = new QAction(QStringLiteral("Show the &grid"), &owner);
        grid->setCheckable(true);
        auto* mm = new QAction(QStringLiteral("Millimetres"), &owner);
        auto* cm = new QAction(QStringLiteral("Centimetres"), &owner);
        for (QAction* unit : {mm, cm}) unit->setCheckable(true);
        auto* units = new QActionGroup(&owner);
        units->addAction(mm);
        units->addAction(cm);
        mm->setChecked(true);
        int cmTriggered = 0;
        QObject::connect(cm, &QAction::triggered, [&] { ++cmTriggered; });
        // An action the registry knows and no row names: the page must not
        // reach it.
        auto* deleteSelected = new QAction(QStringLiteral("Delete Selected"), &owner);
        int deletes = 0;
        QObject::connect(deleteSelected, &QAction::triggered, [&] { ++deletes; });
        registry.adoptChildren(&owner);
        using Row = SettingsEdits::Row;
        std::vector<Row> rows;
        rows.push_back(Row{Row::Kind::Toggle, SettingsEdits::Tab::Viewport, QStringLiteral("Show the grid"),
                           {grid}, {}, false, {}});
        rows.push_back(Row{Row::Kind::Number, SettingsEdits::Tab::Viewport, QStringLiteral("Gizmo size"), {}, {},
                           false, QStringLiteral("gizmoScale")});
        rows.push_back(Row{Row::Kind::Choice, SettingsEdits::Tab::UnitsSaving, QStringLiteral("Sizes in"),
                           {mm, cm}, {QStringLiteral("Millimetres"), QStringLiteral("Centimetres")}, false, {}});
        SettingsBridge bridge(&registry, rows);

        const QVariantList layoutRows = bridge.layout().value(QStringLiteral("rows")).toList();
        check(layoutRows.size() == 3 &&
                  layoutRows[2].toMap().value(QStringLiteral("actions")).toStringList() ==
                      QStringList({QStringLiteral("millimetres"), QStringLiteral("centimetres")}),
              "the layout names each row's actions by id");
        check(layoutRows[1].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("number") &&
                  layoutRows[1].toMap().value(QStringLiteral("number")).toString() == QStringLiteral("gizmoScale"),
              "a number row names its field");
        check(bridge.layout().value(QStringLiteral("tokens")).toList().size() ==
                  Theme::colourTokens().size() - 2,
              "every token but the two retired ones has a row");
        check(bridge.layout().value(QStringLiteral("numbers")).toList().size() == SettingsEdits::numberFields().size(),
              "every number field is described");
        check(bridge.spec().value(QStringLiteral("colours")).toMap().value(QStringLiteral("accent")).toString() ==
                  Theme::spec().accent.name(),
              "the spec carries the live colours");
        check(bridge.actions().size() == 3, "the watched actions are the rows' own");
        bridge.setNumber(QStringLiteral("gizmoScale"), 1.5);
        check(Theme::spec().gizmoScale == 1.5, "a number from the page lands through SettingsEdits");
        check(bridge.trigger(QStringLiteral("centimetres")) && cmTriggered == 1,
              "a trigger from the page runs the action");
        grid->setEnabled(false);
        check(!bridge.trigger(QStringLiteral("show-the-grid")), "a disabled action refuses the page's trigger");
        check(!bridge.trigger(QStringLiteral("delete-selected")) && deletes == 0,
              "an action no row names is not the page's to trigger");
        bridge.allowAction(QStringLiteral("delete-selected"));
        check(bridge.trigger(QStringLiteral("delete-selected")) && deletes == 1, "unless it is allowed by name");
        int specs = 0;
        QObject::connect(&bridge, &SettingsBridge::specChanged, [&] { ++specs; });
        SettingsEdits::setTokenColour(QStringLiteral("accent"), QColor(1, 2, 3));
        check(specs == 1, "an edit made anywhere else reaches the page");
        int actionChanges = 0;
        QObject::connect(&bridge, &SettingsBridge::actionsChanged, [&] { ++actionChanges; });
        grid->setChecked(true);
        QCoreApplication::processEvents();
        check(actionChanges == 1, "and so does an action's change");
    }

    // --- the rail, the pill and the menus on the page (Phase 3i) -----------
    {
        // R3: a theme's shell numbers travel as CSS custom properties, never
        // copied by hand into the page's CSS.
        const QVariantMap violet = Themes::shellCssTokens(Themes::shellOf(QStringLiteral("violet-glass-float")));
        check(violet.value(QStringLiteral("--shell-chip-on-top")).toString() == QStringLiteral("rgba(122, 28, 255, 1)"),
              "Violet's chip on-gradient top reaches the page as rgba(122, 28, 255, 1)");
        check(violet.value(QStringLiteral("--shell-chip-size")).toString() == QStringLiteral("42px") &&
                  violet.value(QStringLiteral("--shell-chip-radius")).toString() == QStringLiteral("14px"),
              "Violet's chip is 42 px at a 14 px radius");
        check(violet.value(QStringLiteral("--shell-chip-hover-edge")).toString() == QStringLiteral("rgba(6, 209, 255, 0.6)"),
              "a translucent colour keeps its alpha");
        check(violet.value(QStringLiteral("--shell-menu-weight")).toString() == QStringLiteral("700") &&
                  violet.value(QStringLiteral("--shell-menu-text")).toString() == QStringLiteral("13px") &&
                  violet.value(QStringLiteral("--shell-menu-wordmark-size")).toString() == QStringLiteral("17px"),
              "Violet's menu titles: weight 700 at 13 px, the wordmark 17 px");
        check(violet.value(QStringLiteral("--shell-card-radius")).toString() == QStringLiteral("26px"),
              "Violet's cards are 26 px round");
        bool anyFullRail = false;
        for (const Themes::Info& info : Themes::all())
            if (Themes::shellCssTokens(Themes::shellOf(info.id)).value(QStringLiteral("--shell-rail-full")) == QStringLiteral("1"))
                anyFullRail = true;
        check(anyFullRail, "a full-height rail theme says so (--shell-rail-full: 1)");
        const QVariantMap graphite = Themes::shellCssTokens(Themes::shellOf(Themes::graphiteId()));
        bool clean = true;
        for (auto it = graphite.cbegin(); it != graphite.cend(); ++it)
            if (it.key().startsWith(QStringLiteral("--shell-"))) clean = false;
        check(clean, "Graphite sends no --shell- key: the page's fallbacks are Graphite");

        // The slots (R9 and the rail): a height report keeps the width a
        // drawer was given, and a stretched slot still answers its natural size.
        {
            QWidget host;
            HtmlCardSlot drawer(QStringLiteral("t"), QSize(200, 100), &host);
            drawer.setFixedWidth(240);
            drawer.setContentHeight(300);
            check(drawer.width() == 240 && drawer.height() == 300,
                  QByteArray("a height report keeps the drawer's width (") + QByteArray::number(drawer.width()) + ")");
            HtmlCardSlot rail(QStringLiteral("r"), QSize(48, 400), &host);
            rail.setStretchable(true);
            rail.resize(48, 900);
            check(rail.height() == 900 && rail.sizeHint() == QSize(48, 400),
                  "a stretchable slot stretches, and its sizeHint stays the card's natural size");
            rail.setContentSize(QSize(52, 420));
            check(rail.minimumSize() == QSize(52, 420) && rail.height() >= 420,
                  "and takes a new natural size as its minimum");
        }

        // R2: one glyph source - IconSet paints the page's glyphs as masks.
        const QImage mask = IconSet::glyphMask(IconSet::Glyph::Undo, 48);
        bool inked = false;
        for (int y = 0; y < mask.height() && !inked; ++y)
            for (int x = 0; x < mask.width(); ++x)
                if (qAlpha(mask.pixel(x, y)) > 128) {
                    inked = true;
                    break;
                }
        check(mask.size() == QSize(48, 48) && inked, "a glyph mask is the size asked and carries ink");
        check(qAlpha(mask.pixel(0, 0)) == 0 && qAlpha(mask.pixel(47, 47)) == 0, "its corners are transparent");
        check(!IconSet::glyphMaskUrl(IconSet::Glyph::Redo, 48).isEmpty() &&
                  IconSet::glyphMaskUrl(IconSet::Glyph::Redo, 48).startsWith(QStringLiteral("data:image/png;base64,")),
              "and crosses to the page as a PNG data URL");
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
