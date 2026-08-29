//
// Headless tests for the learning counter. This is the machinery that makes
// "hints recede" a checkable promise rather than a decorative claim, so it is
// Qt-free and tested without a window like the rest of furnify_geometry.
//
#include "UserProgress.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("%-6s %s\n", condition ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void checkEq(const std::string& actual, const std::string& expected, const std::string& what)
{
    const bool ok = actual == expected;
    std::printf("%-6s %s (got \"%s\", expected \"%s\")\n", ok ? "[ ok ]" : "[FAIL]",
                what.c_str(), actual.c_str(), expected.c_str());
    if (!ok) ++g_failures;
}
}  // namespace

int main()
{
    // --- counting -------------------------------------------------------------
    {
        UserProgress p;
        check(p.count("boolean.completed") == 0, "an unseen event counts zero");
        check(!p.hasLearned("boolean.completed"), "an unseen event is not learned");

        p.record("boolean.completed");
        p.record("boolean.completed");
        check(p.count("boolean.completed") == 2, "two records count two");
        check(!p.hasLearned("boolean.completed"), "two is not yet learned");

        p.record("boolean.completed");
        check(p.count("boolean.completed") == 3, "three records count three");
        check(p.hasLearned("boolean.completed"), "three crosses the threshold");

        p.record("boolean.completed");
        check(p.hasLearned("boolean.completed"), "past the threshold stays learned");
        check(!p.hasLearned("extrude.completed"), "learning one event teaches nothing else");
    }

    // --- reset ----------------------------------------------------------------
    {
        UserProgress p;
        p.record("a"); p.record("a"); p.record("a"); p.record("b");
        p.reset();
        check(p.count("a") == 0 && p.count("b") == 0, "reset clears every counter");
        check(!p.hasLearned("a"), "nothing is learned after a reset");
        checkEq(p.serialize(), "", "a reset store serializes to nothing");
    }

    // --- serialize round trip -------------------------------------------------
    {
        UserProgress p;
        checkEq(p.serialize(), "", "an empty store serializes to an empty string");

        p.record("extrude.completed");
        p.record("boolean.completed");
        p.record("boolean.completed");
        // Ordering must be stable, or the stored value churns between runs even
        // when nothing was learned.
        checkEq(p.serialize(), "boolean.completed=2;extrude.completed=1",
                "events serialize in sorted order");

        UserProgress restored;
        restored.deserialize(p.serialize());
        check(restored.count("boolean.completed") == 2 &&
              restored.count("extrude.completed") == 1,
              "deserialize restores every counter");
        checkEq(restored.serialize(), p.serialize(), "a round trip is byte-identical");
    }

    // --- malformed input ------------------------------------------------------
    {
        // Stored settings can be corrupted, hand-edited, or written by an older
        // build. None of that may crash the app or leave an unusable object.
        for (const std::string& junk : {std::string("garbage"), std::string("a=b"),
                                        std::string("a="), std::string("=1"),
                                        std::string(";;;"), std::string("a=1;;b=2;"),
                                        std::string("a=-4")}) {
            UserProgress p;
            p.deserialize(junk);
            p.record("sane.event");
            if (p.count("sane.event") != 1) {
                check(false, "a store stayed usable after malformed input: " + junk);
                break;
            }
        }
        check(true, "malformed input never leaves the store unusable");

        UserProgress negative;
        negative.deserialize("a=-4");
        check(negative.count("a") >= 0, "a negative stored count never survives as negative");
    }

    // --- deserialize replaces rather than merges -------------------------------
    {
        UserProgress p;
        p.record("old.event");
        p.deserialize("new.event=1");
        check(p.count("old.event") == 0, "deserialize replaces the previous contents");
        check(p.count("new.event") == 1, "deserialize installs the new contents");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
