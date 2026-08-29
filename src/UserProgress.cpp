#include "UserProgress.h"

#include <cstdlib>

void UserProgress::record(const std::string& event)
{
    if (event.empty()) return;
    ++myCounts[event];
}

int UserProgress::count(const std::string& event) const
{
    const auto it = myCounts.find(event);
    return it == myCounts.end() ? 0 : it->second;
}

bool UserProgress::hasLearned(const std::string& event) const
{
    return count(event) >= kLearnedThreshold;
}

void UserProgress::reset()
{
    myCounts.clear();
}

std::string UserProgress::serialize() const
{
    std::string out;
    for (const auto& entry : myCounts) {
        if (!out.empty()) out.push_back(';');
        out += entry.first;
        out.push_back('=');
        out += std::to_string(entry.second);
    }
    return out;
}

void UserProgress::deserialize(const std::string& text)
{
    myCounts.clear();

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string pair =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);

        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos && equals > 0) {
            const std::string key = pair.substr(0, equals);
            const std::string value = pair.substr(equals + 1);
            // Accept only a well-formed non-negative integer; anything else is a
            // corrupted or foreign entry and is dropped rather than guessed at.
            bool digits = !value.empty();
            for (char c : value) {
                if (c < '0' || c > '9') { digits = false; break; }
            }
            if (digits) myCounts[key] = std::atoi(value.c_str());
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }
}
