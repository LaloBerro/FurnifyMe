// Hex lives only in Theme.cpp - asserted, not merely written down.
//
// CLAUDE.md's rule and the 2026-10 review's F19: every colour the app paints
// is either a user-editable Theme token or one of Theme's FIXED colours, and
// the only file allowed to spell a colour as a literal is src/ui/Theme.cpp.
// The review found a hex hub colour, a material tile's whole palette, a
// material card's default and three OCCT greys outside it - each a colour the
// rule said could not exist - so the rule is a test now.
//
// What counts as a literal, after comments are stripped (a comment may QUOTE
// a colour; Theme.h's history notes do):
//   - "#rrggbb" anywhere in a string,
//   - QColor(...) / QColor name(...) / QColor name{...} / QColor{...} whose
//     first argument is a number,
//   - Quantity_Color(...) / Quantity_Color name(...) whose first argument is
//     a number,
//   - any Quantity_NOC_* named colour.
//
// A line that genuinely is not a UI colour - a calibrated render-material
// reflectance, an argument OCCT's signature demands for an edge it never
// draws - carries the marker NOT-A-UI-COLOUR in a comment on that same line,
// and the test prints every exemption so none of them is silent.
//
// Headless and Qt-free on purpose: it reads text and needs no window, so it
// runs in CI with the geometry tests. FURNIFY_SOURCE_DIR is the src/ tree,
// set by CMake.
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef FURNIFY_SOURCE_DIR
#error "FURNIFY_SOURCE_DIR must name the src/ tree"
#endif

namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if (!condition) ++g_failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
}

// Blanks every comment while keeping line breaks and string/char literal
// CONTENTS, so a line number in the stripped text is the line number in the
// file and a "#rrggbb" inside a string is still seen. Raw strings
// R"delim( ... )delim" are kept whole - Theme.cpp's stylesheet is one, and a
// future one elsewhere must not hide a colour from this test.
std::string stripComments(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    const std::size_t n = in.size();
    while (i < n) {
        const char c = in[i];
        // A raw string literal: R"delim( ... )delim"
        if (c == 'R' && i + 1 < n && in[i + 1] == '"' &&
            (i == 0 || !(std::isalnum(static_cast<unsigned char>(in[i - 1])) || in[i - 1] == '_'))) {
            const std::size_t open = in.find('(', i + 2);
            if (open != std::string::npos) {
                const std::string delim = in.substr(i + 2, open - (i + 2));
                const std::string close = ")" + delim + "\"";
                const std::size_t end = in.find(close, open + 1);
                const std::size_t stop = end == std::string::npos ? n : end + close.size();
                out.append(in, i, stop - i);
                i = stop;
                continue;
            }
        }
        // A digit separator (60'000) is not a character literal.
        if (c == '\'' && i > 0 && i + 1 < n &&
            std::isxdigit(static_cast<unsigned char>(in[i - 1])) &&
            std::isxdigit(static_cast<unsigned char>(in[i + 1]))) {
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            out.push_back(c);
            ++i;
            while (i < n && in[i] != c) {
                if (in[i] == '\\' && i + 1 < n) {
                    out.push_back(in[i]);
                    ++i;
                }
                if (in[i] == '\n') break;   // malformed; do not run away
                out.push_back(in[i]);
                ++i;
            }
            if (i < n) {
                out.push_back(in[i]);
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && in[i + 1] == '/') {
            while (i < n && in[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(in[i] == '*' && in[i + 1] == '/')) {
                if (in[i] == '\n') out.push_back('\n');
                ++i;
            }
            i += 2;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

struct Finding {
    std::string file;
    int line = 0;
    std::string text;
};

}  // namespace

int main()
{
    const fs::path root(FURNIFY_SOURCE_DIR);
    check(fs::is_directory(root), "the src/ tree is where CMake said it is (" + root.string() + ")");
    if (!fs::is_directory(root)) return 1;

    const std::regex hex(R"(#[0-9a-fA-F]{6}\b)");
    const std::regex qcolor(R"(\bQColor\s*(\w+\s*)?[\(\{]\s*[0-9])");
    const std::regex occtColour(R"(\bQuantity_Color\s*(\w+\s*)?\(\s*[0-9.])");
    const std::regex occtNamed(R"(\bQuantity_NOC_\w+)");

    std::vector<Finding> offenders;
    std::vector<Finding> exemptions;
    int filesRead = 0;
    bool sawTheme = false;
    bool themeHasHex = false;

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& path = entry.path();
        const std::string ext = path.extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;

        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string raw = buffer.str();
        ++filesRead;

        const std::string relative = fs::relative(path, root).generic_string();
        const bool isTheme = relative == "ui/Theme.cpp";
        if (isTheme) {
            sawTheme = true;
            themeHasHex = std::regex_search(raw, hex);
            continue;
        }

        const std::vector<std::string> rawLines = splitLines(raw);
        const std::vector<std::string> codeLines = splitLines(stripComments(raw));
        for (std::size_t i = 0; i < codeLines.size(); ++i) {
            const std::string& code = codeLines[i];
            const bool hit = std::regex_search(code, hex) || std::regex_search(code, qcolor) ||
                             std::regex_search(code, occtColour) ||
                             std::regex_search(code, occtNamed);
            if (!hit) continue;
            const std::string& original = i < rawLines.size() ? rawLines[i] : code;
            Finding finding{relative, static_cast<int>(i + 1), original};
            if (original.find("NOT-A-UI-COLOUR") != std::string::npos)
                exemptions.push_back(finding);
            else
                offenders.push_back(finding);
        }
    }

    // Non-vacuity: a scan that read nothing, or never found the one file that
    // is allowed colours, proves nothing about the rest.
    check(filesRead > 50, "the scan read the whole tree (" + std::to_string(filesRead) + " files)");
    check(sawTheme && themeHasHex,
          "and found Theme.cpp, where the hex is supposed to live, carrying it");

    for (const Finding& f : exemptions)
        std::printf("  exempt  %s:%d  %s\n", f.file.c_str(), f.line, f.text.c_str());
    for (const Finding& f : offenders)
        std::printf("  LITERAL %s:%d  %s\n", f.file.c_str(), f.line, f.text.c_str());

    check(offenders.empty(),
          "no colour literal outside Theme.cpp - hex, numeric QColor, numeric "
          "Quantity_Color or Quantity_NOC_* (" + std::to_string(offenders.size()) + " found)");
    // The exemptions are few and each one names its reason on the line; a
    // count that grows is a reason to look, so it is printed rather than
    // pinned to a number nobody chose.
    std::printf("  %d exemption%s marked NOT-A-UI-COLOUR\n", static_cast<int>(exemptions.size()),
                exemptions.size() == 1 ? "" : "s");

    std::printf("\n%s (%d failure%s, %d checks)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s", g_checks);
    return g_failures == 0 ? 0 : 1;
}
