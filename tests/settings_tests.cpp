// The Settings drawer's model and its bridge (migration Phase 3b), on Qt's
// offscreen platform - no window, no GPU, no page: the tables both drawers
// read and the one edit path both drive.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "SettingsEdits.h"
#include "Theme.h"

#include <QApplication>
#include <QFile>
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

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}
