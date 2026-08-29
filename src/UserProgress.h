#pragma once
//
// Counts what the user has actually done, so hints can fall silent once they
// have served their purpose. Qt-free and free of any storage concern: the app
// layer persists serialize() and hands deserialize() back at startup, which is
// what lets the test suite run with a store the developer's own usage cannot
// contaminate.
//
#include <map>
#include <string>

class UserProgress {
public:
    // Three completions of an action is the point at which its hint stops.
    static constexpr int kLearnedThreshold = 3;

    void record(const std::string& event);
    int count(const std::string& event) const;
    bool hasLearned(const std::string& event) const;
    void reset();

    // "boolean.completed=2;extrude.completed=1" - sorted, so a round trip is
    // byte-identical and the stored value does not churn between runs.
    std::string serialize() const;

    // Replaces the contents wholesale. Malformed input is discarded pair by
    // pair rather than throwing: stored settings can be corrupted or written by
    // an older build, and neither may stop the app from starting.
    void deserialize(const std::string& text);

private:
    std::map<std::string, int> myCounts;   // std::map iterates sorted, which is why
};                                          // serialize() is stable for free
