// The session's "no Widgets" rule, made structural (the slice A review's
// finding). Linking only Qt6::Core and Qt6::Gui is not enough on its own:
// those targets export include/Qt6, so <QtWidgets/QWidget> still resolves,
// and a static library is never linked by itself to catch a Widgets symbol.
//
// This reads every file under src/session as text, follows the project's own
// quoted includes from there, and fails on any include that names QtWidgets/
// or resolves to a header in the QtWidgets include directory. It links
// nothing - no Qt, no OCCT - like headless_colour_literals.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main()
{
    const fs::path src = FURNIFY_SOURCE_DIR;
    const fs::path widgets = FURNIFY_QTWIDGETS_INCLUDE_DIR;
    if (!fs::is_directory(src / "session") || !fs::is_directory(widgets)) {
        std::printf("FAIL  cannot see %s or %s\n", (src / "session").string().c_str(),
                    widgets.string().c_str());
        return 1;
    }

    std::vector<fs::path> queue;
    for (const auto& entry : fs::directory_iterator(src / "session"))
        if (entry.is_regular_file()) queue.push_back(entry.path());

    const std::regex include(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"])");
    std::set<std::string> seen;
    int files = 0, violations = 0;
    while (!queue.empty()) {
        const fs::path file = queue.back();
        queue.pop_back();
        if (!seen.insert(fs::weakly_canonical(file).string()).second) continue;
        ++files;
        std::ifstream in(file);
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            std::smatch m;
            if (!std::regex_search(line, m, include)) continue;
            const std::string name = m[2];
            const bool viaWidgets = name.rfind("QtWidgets/", 0) == 0 ||
                                    fs::exists(widgets / name);
            if (viaWidgets) {
                ++violations;
                std::printf("FAIL  %s:%d includes %s - a Widgets header in the session\n",
                            file.string().c_str(), lineNo, name.c_str());
            }
            // Follow this project's own headers, wherever the session reaches
            // them from: a widget include one header down is the same breach.
            if (m[1] == "\"") {
                for (const fs::path& root : {file.parent_path(), src}) {
                    const fs::path candidate = root / name;
                    if (fs::exists(candidate)) {
                        queue.push_back(candidate);
                        break;
                    }
                }
            }
        }
    }

    std::printf("%d files read, %d Widgets includes\n", files, violations);
    return (violations == 0 && files > 0) ? 0 : 1;
}
