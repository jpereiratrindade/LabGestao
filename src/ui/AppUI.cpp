#include "ui/AppUI.hpp"
#include "app/ApplicationServices.hpp"
#include "imgui.h"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace labgestao {

namespace {
std::string todayISO() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf {};
    localtime_r(&t, &tm_buf);
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

std::string shellEscapeSingleQuoted(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

bool launchDetachedShellCommand(const std::string& cmd) {
    const std::string wrapped = cmd + " >/dev/null 2>&1 &";
    return std::system(wrapped.c_str()) == 0;
}

bool commandExists(const char* command) {
#if defined(_WIN32)
    (void) command;
    return false;
#else
    std::string cmd = "command -v ";
    cmd += command;
    cmd += " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
#endif
}

std::string escapeDoubleQuoted(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '$': out += "\\$"; break;
            case '`': out += "\\`"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool runPickerCommand(const std::string& command, std::string& selectedPath) {
    std::array<char, 512> buffer{};
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;

    selectedPath.clear();
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        selectedPath += buffer.data();
    }

    int rc = pclose(pipe);
    if (rc != 0) return false;

    while (!selectedPath.empty() && (selectedPath.back() == '\n' || selectedPath.back() == '\r')) {
        selectedPath.pop_back();
    }
    return !selectedPath.empty();
}

bool runCommandCollectLines(const std::string& command, std::vector<std::string>* lines) {
    if (!lines) return false;
    lines->clear();

    std::array<char, 1024> buffer{};
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;

    std::string current;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        current += buffer.data();
        while (true) {
            const std::size_t pos = current.find('\n');
            if (pos == std::string::npos) break;
            std::string line = current.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines->push_back(std::move(line));
            current.erase(0, pos + 1);
        }
    }
    if (!current.empty()) lines->push_back(std::move(current));

    const int rc = pclose(pipe);
    return rc == 0;
}

bool readSmallTextFile(const fs::path& path, std::string* out, std::size_t maxBytes = 1024 * 1024) {
    if (!out) return false;
    out->clear();
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) return false;
    const auto sz = fs::file_size(path, ec);
    if (!ec && sz > maxBytes) return false;
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

std::string selectDirectoryWithSystemDialog(const std::string& initialDir) {
#if defined(_WIN32)
    (void) initialDir;
    return {};
#elif defined(__APPLE__)
    std::string selected;
    const std::string cmd =
        "osascript -e 'set p to POSIX path of (choose folder with prompt \"Selecionar Workspace\")' 2>/dev/null";
    if (runPickerCommand(cmd, selected)) return selected;
    return {};
#else
    std::string selected;
    const std::string startPath = escapeDoubleQuoted(initialDir.empty() ? "." : initialDir);
    if (commandExists("kdialog")) {
        const std::string cmd = "kdialog --getexistingdirectory \"" + startPath + "\" 2>/dev/null";
        if (runPickerCommand(cmd, selected)) return selected;
    }
    if (commandExists("zenity")) {
        const std::string cmd =
            "zenity --file-selection --directory --title=\"Selecionar Workspace\" --filename=\"" + startPath + "/\" 2>/dev/null";
        if (runPickerCommand(cmd, selected)) return selected;
    }
    return {};
#endif
}

bool openPathInSystem(const std::string& path) {
    const std::string escaped = shellEscapeSingleQuoted(path);
#if defined(_WIN32)
    const std::string cmd = "start \"\" " + escaped;
#elif defined(__APPLE__)
    const std::string cmd = "open " + escaped;
#else
    const std::string cmd = "xdg-open " + escaped;
#endif
    return launchDetachedShellCommand(cmd);
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool pathEqualsBestEffort(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty() || rhs.empty()) return false;
    std::error_code ecA;
    std::error_code ecB;
    const fs::path a = fs::weakly_canonical(fs::path(lhs), ecA);
    const fs::path b = fs::weakly_canonical(fs::path(rhs), ecB);
    if (!ecA && !ecB) return a == b;
    return lhs == rhs;
}

bool hasAnyFile(const fs::path& root, std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (fs::exists(root / fs::path(name))) return true;
    }
    return false;
}

bool hasCiConfig(const fs::path& root) {
    if (fs::exists(root / ".gitlab-ci.yml")) return true;
    if (fs::exists(root / ".circleci" / "config.yml")) return true;
    const fs::path workflows = root / ".github" / "workflows";
    if (!fs::exists(workflows) || !fs::is_directory(workflows)) return false;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(workflows, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = toLowerAscii(entry.path().extension().string());
        if (ext == ".yml" || ext == ".yaml") return true;
    }
    return false;
}

std::string detectBuildSystem(const fs::path& root) {
    if (fs::exists(root / "CMakeLists.txt")) return "CMake";
    if (fs::exists(root / "meson.build")) return "Meson";
    if (fs::exists(root / "Makefile")) return "Make";
    if (fs::exists(root / "pyproject.toml")) return "Python";
    if (fs::exists(root / "package.json")) return "Node";
    return "Unknown";
}

bool hasTestsLayout(const fs::path& root) {
    if (fs::exists(root / "tests") && fs::is_directory(root / "tests")) return true;
    if (fs::exists(root / "test") && fs::is_directory(root / "test")) return true;
    if (fs::exists(root / "CTestTestfile.cmake")) return true;
    return false;
}

int countPotentialArtifacts(const fs::path& root) {
    const std::unordered_set<std::string> exts = {
        ".o", ".obj", ".so", ".a", ".dll", ".dylib", ".exe", ".spv", ".bin", ".zip", ".7z", ".gz", ".tar"
    };
    const std::unordered_set<std::string> dirs = {
        "build", "build-ninja", "dist", "out", "bin"
    };

    int count = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) break;
        const fs::path p = it->path();
        const std::string name = toLowerAscii(p.filename().string());
        if (it.depth() > 3) {
            it.disable_recursion_pending();
        }
        if (it->is_directory(ec)) {
            if (dirs.find(name) != dirs.end()) {
                count++;
                it.disable_recursion_pending();
            }
        } else if (it->is_regular_file(ec)) {
            const std::string ext = toLowerAscii(p.extension().string());
            if (exts.find(ext) != exts.end()) {
                count++;
            }
        }
        ++it;
    }
    return count;
}

bool pathContainsArtifactDir(const std::string& lowerPath) {
    static const std::array<std::string, 5> kDirs = {
        "build", "build-ninja", "dist", "out", "bin"
    };
    for (const auto& dir : kDirs) {
        const std::string marker = "/" + dir + "/";
        if (lowerPath.rfind(dir + "/", 0) == 0) return true;
        if (lowerPath.find(marker) != std::string::npos) return true;
    }
    return false;
}

bool pathHasArtifactExt(const std::string& lowerPath) {
    static const std::array<std::string, 13> kExts = {
        ".o", ".obj", ".so", ".a", ".dll", ".dylib", ".exe",
        ".spv", ".bin", ".zip", ".7z", ".gz", ".tar"
    };
    const fs::path p(lowerPath);
    const std::string ext = toLowerAscii(p.extension().string());
    for (const auto& candidate : kExts) {
        if (ext == candidate) return true;
    }
    return false;
}

int countTrackedArtifacts(const fs::path& root) {
    if (!fs::exists(root / ".git")) return -1;

    const std::string cmd = "git -C " + shellEscapeSingleQuoted(root.string()) + " ls-files 2>/dev/null";
    std::vector<std::string> tracked;
    if (!runCommandCollectLines(cmd, &tracked)) return -1;

    int count = 0;
    for (const auto& relPath : tracked) {
        if (relPath.empty()) continue;
        std::string lowerPath = toLowerAscii(relPath);
        std::replace(lowerPath.begin(), lowerPath.end(), '\\', '/');
        if (pathContainsArtifactDir(lowerPath) || pathHasArtifactExt(lowerPath)) count++;
    }
    return count;
}

int computeRepoHealthScore(
    bool hasReadme,
    bool hasCi,
    bool hasTests,
    bool hasBuildSystem,
    bool hasGitignore,
    bool hasLicense,
    int artifactCount
) {
    int score = 0;
    if (hasReadme) score += 20;
    if (hasCi) score += 20;
    if (hasTests) score += 15;
    if (hasBuildSystem) score += 20;
    if (hasGitignore) score += 10;
    if (hasLicense) score += 5;
    if (artifactCount == 0) score += 10;
    return std::clamp(score, 0, 100);
}

std::vector<std::string> listRepoFiles(const fs::path& root) {
    std::vector<std::string> files;
    if (fs::exists(root / ".git")) {
        const std::string cmd = "git -C " + shellEscapeSingleQuoted(root.string()) + " ls-files 2>/dev/null";
        if (runCommandCollectLines(cmd, &files)) return files;
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) break;
        if (it.depth() > 4) it.disable_recursion_pending();
        if (it->is_regular_file(ec)) {
            const fs::path rel = fs::relative(it->path(), root, ec);
            if (!ec) files.push_back(rel.generic_string());
            ec.clear();
        }
        ++it;
    }
    return files;
}

bool isReliabilityRelevantFile(const std::string& relPathLower) {
    if (relPathLower.rfind(".github/workflows/", 0) == 0) return true;
    if (relPathLower == ".gitlab-ci.yml") return true;
    if (relPathLower.find("cmakelists.txt") != std::string::npos) return true;
    if (relPathLower.find("meson.build") != std::string::npos) return true;
    if (relPathLower.find("makefile") != std::string::npos) return true;
    if (relPathLower.find("build.sh") != std::string::npos) return true;
    if (relPathLower.find(".clang-tidy") != std::string::npos) return true;
    if (relPathLower.find(".clang-format") != std::string::npos) return true;
    if (relPathLower.find("cppcheck") != std::string::npos) return true;
    if (relPathLower.find("valgrind") != std::string::npos) return true;
    if (relPathLower.find("sanitizer") != std::string::npos) return true;
    if (relPathLower.find("quality") != std::string::npos) return true;
    return false;
}

bool isCiFile(const std::string& relPathLower) {
    if (relPathLower.rfind(".github/workflows/", 0) == 0) return true;
    if (relPathLower == ".gitlab-ci.yml") return true;
    if (relPathLower.rfind(".circleci/", 0) == 0) return true;
    return false;
}

bool repoHasFileToken(const std::vector<std::string>& files, const std::vector<std::string>& tokens) {
    for (const auto& rel : files) {
        const std::string low = toLowerAscii(rel);
        for (const auto& token : tokens) {
            if (low.find(token) != std::string::npos) return true;
        }
    }
    return false;
}

bool repoHasAnyPatternInRelevantFiles(const fs::path& root, const std::vector<std::string>& files, const std::vector<std::string>& patterns) {
    for (const auto& rel : files) {
        std::string relLow = toLowerAscii(rel);
        std::replace(relLow.begin(), relLow.end(), '\\', '/');
        if (!isReliabilityRelevantFile(relLow)) continue;

        std::string content;
        if (!readSmallTextFile(root / fs::path(rel), &content)) continue;
        const std::string lowContent = toLowerAscii(content);
        for (const auto& pattern : patterns) {
            if (lowContent.find(pattern) != std::string::npos) return true;
        }
    }
    return false;
}

bool repoHasAnyPatternInCiFiles(const fs::path& root, const std::vector<std::string>& files, const std::vector<std::string>& patterns) {
    for (const auto& rel : files) {
        std::string relLow = toLowerAscii(rel);
        std::replace(relLow.begin(), relLow.end(), '\\', '/');
        if (!isCiFile(relLow)) continue;

        std::string content;
        if (!readSmallTextFile(root / fs::path(rel), &content)) continue;
        const std::string lowContent = toLowerAscii(content);
        for (const auto& pattern : patterns) {
            if (lowContent.find(pattern) != std::string::npos) return true;
        }
    }
    return false;
}

int computeReliabilityScore(
    bool hasAsanUbsan,
    bool hasLeakCheck,
    bool hasStaticAnalysis,
    bool hasStrictWarnings,
    bool hasComplexityGuard,
    bool hasCycleGuard,
    bool hasFormatLint
) {
    int score = 0;
    if (hasAsanUbsan) score += 15;
    if (hasLeakCheck) score += 10;
    if (hasStaticAnalysis) score += 20;
    if (hasStrictWarnings) score += 15;
    if (hasComplexityGuard) score += 15;
    if (hasCycleGuard) score += 10;
    if (hasFormatLint) score += 15;
    return std::clamp(score, 0, 100);
}

bool hasMarkdownAdrFile(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string filename = toLowerAscii(entry.path().filename().string());
        const std::string ext = toLowerAscii(entry.path().extension().string());
        if ((filename.rfind("adr-", 0) == 0 || filename.find("adr") != std::string::npos) && ext == ".md") return true;
    }
    return false;
}

bool hasFileNameToken(const fs::path& root, const std::vector<std::string>& tokens, int maxDepth = 4) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return false;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) break;
        if (it.depth() > maxDepth) it.disable_recursion_pending();
        if (it->is_regular_file(ec)) {
            const std::string name = toLowerAscii(it->path().filename().string());
            for (const auto& token : tokens) {
                if (name.find(token) != std::string::npos) return true;
            }
        }
        ++it;
    }
    return false;
}

bool hasAdrEvidence(const fs::path& root) {
    if (hasMarkdownAdrFile(root / "adr")) return true;
    if (hasMarkdownAdrFile(root / "docs" / "adr")) return true;
    return hasFileNameToken(root / "docs", {"adr-"}, 4);
}

bool hasDddEvidence(const fs::path& root) {
    if (fs::exists(root / "docs" / "ddd")) return true;
    if (fs::exists(root / "docs" / "domain")) return true;
    if (fs::exists(root / "docs" / "architecture")) return true;
    return hasFileNameToken(root / "docs", {"ddd", "domain-model", "context-map", "bounded-context"}, 4);
}

bool hasDaiEvidence(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root / "docs" / "dai", ec) && fs::is_directory(root / "docs" / "dai", ec)) return true;
    return hasFileNameToken(root, {"dai"}, 3);
}

int governanceSignalsCount(const fs::path& root) {
    int signals = 0;
    if (hasAnyFile(root, {"CONTRIBUTING.md", "contributing.md"})) signals++;
    if (hasAnyFile(root, {"CODEOWNERS"}) || hasAnyFile(root / ".github", {"CODEOWNERS"})) signals++;
    if (hasAnyFile(root, {"PULL_REQUEST_TEMPLATE.md", "pull_request_template.md"}) ||
        hasAnyFile(root / ".github", {"pull_request_template.md", "PULL_REQUEST_TEMPLATE.md"})) signals++;
    std::error_code ec;
    if ((fs::exists(root / ".github" / "ISSUE_TEMPLATE", ec) && fs::is_directory(root / ".github" / "ISSUE_TEMPLATE", ec)) ||
        hasAnyFile(root / ".github", {"issue_template.md", "ISSUE_TEMPLATE.md"})) {
        signals++;
    }
    return std::clamp(signals, 0, 4);
}

int computeMaturityScore(bool hasAdr, bool hasDdd, bool hasDai, int governanceSignals) {
    int score = 0;
    if (hasAdr) score += 25;
    if (hasDdd) score += 25;
    if (hasDai) score += 25;
    const int gov = std::clamp(governanceSignals, 0, 4);
    score += static_cast<int>(std::lround((static_cast<double>(gov) / 4.0) * 25.0));
    return std::clamp(score, 0, 100);
}

int computeCompositeScore(int operationalScore, int maturityScore, int reliabilityConfigScore, int reliabilityExecScore) {
    const double weighted = static_cast<double>(operationalScore) * 0.45
                          + static_cast<double>(maturityScore) * 0.30
                          + static_cast<double>(reliabilityConfigScore) * 0.15
                          + static_cast<double>(reliabilityExecScore) * 0.10;
    return std::clamp(static_cast<int>(std::lround(weighted)), 0, 100);
}

std::string escapeCsvField(const std::string& value) {
    std::string out = "\"";
    out.reserve(value.size() + 2);
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}
}

AppUI::AppUI(
    ProjectStore& store,
    const std::string& dataPath,
    const std::string& settingsPath,
    const std::string& workspaceRoot,
    const ListView::CreationDefaults& creationDefaults
)
    : m_store(store)
    , m_dataPath(dataPath)
    , m_settingsPath(settingsPath)
    , m_list(store, creationDefaults)
    , m_kanban(store)
    , m_graph(store)
{
    if (!workspaceRoot.empty()) {
        std::snprintf(m_workspaceRootBuf, sizeof(m_workspaceRootBuf), "%s", workspaceRoot.c_str());
        std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", workspaceRoot.c_str());
    } else {
        const std::string cwd = fs::current_path().string();
        std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", cwd.c_str());
    }
}

void AppUI::applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 5.f;
    s.PopupRounding     = 6.f;
    s.ScrollbarRounding = 4.f;
    s.GrabRounding      = 4.f;
    s.TabRounding       = 6.f;
    s.FramePadding      = ImVec2(8.f, 4.f);
    s.ItemSpacing       = ImVec2(8.f, 6.f);
    s.WindowPadding     = ImVec2(12.f, 10.f);
    s.ScrollbarSize     = 12.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.07f, 0.08f, 0.11f, 1.f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.09f, 0.11f, 0.15f, 1.f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.08f, 0.09f, 0.13f, 0.97f);
    c[ImGuiCol_Border]            = ImVec4(0.20f, 0.25f, 0.38f, 1.f);
    c[ImGuiCol_BorderShadow]      = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.12f, 0.14f, 0.20f, 1.f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.18f, 0.22f, 0.32f, 1.f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.22f, 0.28f, 0.40f, 1.f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.07f, 0.08f, 0.11f, 1.f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.14f, 0.22f, 1.f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.09f, 0.10f, 0.15f, 1.f);
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.07f, 0.08f, 0.11f, 1.f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.25f, 0.35f, 0.55f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.48f, 0.70f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.60f, 0.85f, 1.f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.35f, 0.70f, 1.f,  1.f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.35f, 0.65f, 0.95f, 1.f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.50f, 0.80f, 1.f,  1.f);
    c[ImGuiCol_Button]            = ImVec4(0.18f, 0.30f, 0.55f, 1.f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.28f, 0.45f, 0.75f, 1.f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.35f, 0.55f, 0.90f, 1.f);
    c[ImGuiCol_Header]            = ImVec4(0.18f, 0.27f, 0.48f, 1.f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.25f, 0.38f, 0.62f, 1.f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.30f, 0.45f, 0.72f, 1.f);
    c[ImGuiCol_Separator]         = ImVec4(0.20f, 0.26f, 0.38f, 1.f);
    c[ImGuiCol_Tab]               = ImVec4(0.10f, 0.14f, 0.22f, 1.f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.22f, 0.38f, 0.68f, 1.f);
    c[ImGuiCol_TabActive]         = ImVec4(0.16f, 0.30f, 0.60f, 1.f);
    c[ImGuiCol_TabUnfocused]      = ImVec4(0.09f, 0.11f, 0.17f, 1.f);
    c[ImGuiCol_TabUnfocusedActive]= ImVec4(0.13f, 0.22f, 0.42f, 1.f);
    c[ImGuiCol_Text]              = ImVec4(0.88f, 0.90f, 0.95f, 1.f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.50f, 0.60f, 1.f);
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.10f, 0.14f, 0.22f, 1.f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.28f, 0.42f, 1.f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.14f, 0.18f, 0.28f, 1.f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.09f, 0.11f, 0.16f, 1.f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(0.11f, 0.14f, 0.20f, 1.f);
    c[ImGuiCol_DragDropTarget]    = ImVec4(0.35f, 0.70f, 1.f,  0.90f);
    c[ImGuiCol_NavHighlight]      = ImVec4(0.35f, 0.70f, 1.f,  1.f);
}

void AppUI::render() {
    if (!m_themeApplied) { applyTheme(); m_themeApplied = true; }
    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        m_focusProjectsTab = true;
        m_requestCreateFromTools = true;
    }

    // Main Menu Bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Arquivo")) {
            if (ImGui::MenuItem("Sair", "Alt+F4")) {
                m_requestExit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            const fs::path dataDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
            if (ImGui::MenuItem("Selecionar Workspace...")) {
                m_showWorkspaceModal = true;
            }
            if (ImGui::MenuItem("Reclassificar Kanban (Auto)")) {
                reclassifyKanbanAuto();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Abrir pasta de dados")) {
                std::error_code ec;
                fs::create_directories(dataDir, ec);
                if (openPathInSystem(dataDir.string())) {
                    m_toolsMessage = "Abrindo pasta de dados.";
                } else {
                    m_toolsMessage = "Falha ao abrir pasta de dados.";
                }
            }
            if (ImGui::MenuItem("Abrir lista de projetos")) {
                std::error_code ec;
                fs::create_directories(dataDir, ec);
                if (!fs::exists(m_dataPath)) {
                    std::ofstream out(m_dataPath);
                }
                if (openPathInSystem(m_dataPath)) {
                    m_toolsMessage = "Abrindo lista de projetos.";
                } else {
                    m_toolsMessage = "Falha ao abrir lista de projetos.";
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Criar Projeto", "Ctrl+N")) {
                m_focusProjectsTab = true;
                m_requestCreateFromTools = true;
                m_toolsMessage.clear();
            }
            if (ImGui::MenuItem("Limpar Projetos")) {
                m_showClearProjectsConfirm = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Full-viewport window
    float menuBarHeight = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menuBarHeight));
    ImGui::Begin("##MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar);

    // Title bar area
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 1.f, 1.f));
    ImGui::Text("  [LabGestao]");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("- Gestao de Projetos");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160.f);
    ImGui::TextDisabled("%zu projeto(s)", m_store.getAll().size());
    if (!m_toolsMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_toolsMessage.c_str());
    }
    const fs::path activeDataDir = fs::path(m_dataPath).parent_path().empty() ? fs::path(".") : fs::path(m_dataPath).parent_path();
    ImGui::TextDisabled("Data ativo: %s", activeDataDir.string().c_str());

    ImGui::Separator();

    // Tab bar
    if (ImGui::BeginTabBar("##MainTabs", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags projectsTabFlags = m_focusProjectsTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        m_focusProjectsTab = false;
        if (ImGui::BeginTabItem("  [Projetos]  ", nullptr, projectsTabFlags)) {
            if (m_requestCreateFromTools) {
                m_list.requestCreateProject();
                m_requestCreateFromTools = false;
            }
            ImGui::Spacing();
            m_list.render();
            if (const std::string createdId = m_list.takeLastCreatedProjectId(); !createdId.empty()) {
                m_planningProjectId = createdId;
                m_focusPlanningTab = true;
            }
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags planningTabFlags = m_focusPlanningTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        m_focusPlanningTab = false;
        if (ImGui::BeginTabItem("  [Planejamento]  ", nullptr, planningTabFlags)) {
            ImGui::Spacing();
            renderPlanningTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Kanban]  ")) {
            ImGui::Spacing();
            if (ImGui::Button("Reclassificar (Auto)")) {
                reclassifyKanbanAuto();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Reaplica regras automáticas por atividade/DAI.");
            ImGui::Separator();
            m_kanban.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Grafo]  ")) {
            std::vector<GraphView::InventorySignal> signals;
            signals.reserve(m_repoInventory.size());
            for (const auto& item : m_repoInventory) {
                GraphView::InventorySignal s;
                s.path = item.path;
                s.scoreTotal = item.scoreTotal;
                s.scoreOperational = item.scoreOperational;
                s.scoreMaturity = item.scoreMaturity;
                s.scoreReliability = item.scoreReliability;
                s.scoreReliabilityExec = item.scoreReliabilityExec;
                s.artifacts = item.detectedArtifacts;
                s.hasAdr = item.hasAdr;
                s.hasDdd = item.hasDdd;
                s.hasDai = item.hasDai;
                s.hasAsanUbsan = item.hasAsanUbsan;
                s.hasStaticAnalysis = item.hasStaticAnalysis;
                s.hasLeakCheck = item.hasLeakCheck;
                signals.push_back(std::move(s));
            }
            m_graph.setInventorySignals(signals);
            ImGui::Spacing();
            m_graph.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Metricas]  ")) {
            ImGui::Spacing();
            renderFlowMetricsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Inventario]  ")) {
            ImGui::Spacing();
            renderInventoryTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (m_showClearProjectsConfirm) {
        ImGui::OpenPopup("Limpar Projetos");
    }
    if (ImGui::BeginPopupModal("Limpar Projetos", &m_showClearProjectsConfirm, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Remover todos os projetos do arquivo atual?");
        ImGui::TextWrapped("Essa acao nao pode ser desfeita.");
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.10f, 0.10f, 1.f));
        if (ImGui::Button("Limpar", ImVec2(110.f, 0.f))) {
            m_store.clear();
            m_metricsExportMessage.clear();
            m_toolsMessage = "Projetos removidos.";
            m_showClearProjectsConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110.f, 0.f))) {
            m_showClearProjectsConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_showWorkspaceModal) {
        ImGui::OpenPopup("Selecionar Workspace");
    }
    if (ImGui::BeginPopupModal("Selecionar Workspace", &m_showWorkspaceModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Informe a pasta raiz dos projetos a administrar.");
        ImGui::SetNextItemWidth(430.f);
        ImGui::InputTextWithHint("##workspace_root", "/caminho/do/workspace", m_workspaceRootBuf, sizeof(m_workspaceRootBuf));
        ImGui::SameLine();
        if (ImGui::Button("Selecionar...")) {
            const std::string start = std::strlen(m_workspaceRootBuf) > 0
                ? std::string(m_workspaceRootBuf)
                : fs::current_path().string();
            std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", start.c_str());
            m_showWorkspacePickerModal = true;
            m_toolsMessage = "Abrindo seletor interno.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Sistema...")) {
            const std::string selected = selectDirectoryWithSystemDialog(m_workspaceRootBuf);
            if (!selected.empty()) {
                std::snprintf(m_workspaceRootBuf, sizeof(m_workspaceRootBuf), "%s", selected.c_str());
                m_toolsMessage = "Workspace selecionado pelo dialogo do sistema.";
            } else {
                m_toolsMessage = "Dialogo do sistema indisponivel neste ambiente.";
            }
        }
        if (m_showWorkspacePickerModal) {
            ImGui::Separator();
            ImGui::TextDisabled("Seletor interno");
            ImGui::SetNextItemWidth(560.f);
            ImGui::InputText("##picker_path_inline", m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf));
            if (ImGui::Button("Subir")) {
                std::error_code ec;
                fs::path p = fs::path(m_workspacePickerPathBuf).parent_path();
                if (!p.empty() && fs::exists(p, ec) && fs::is_directory(p, ec)) {
                    std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", p.string().c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Home")) {
                const char* home = std::getenv("HOME");
                if (home && *home) {
                    std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", home);
                }
            }

            ImGui::BeginChild("##picker_dirs_inline", ImVec2(620.f, 220.f), true);
            {
                std::error_code ec;
                const fs::path base(m_workspacePickerPathBuf);
                std::vector<fs::path> dirs;
                if (fs::exists(base, ec) && fs::is_directory(base, ec)) {
                    for (const auto& entry : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
                        if (entry.is_directory(ec)) dirs.push_back(entry.path());
                    }
                    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
                        return a.filename().string() < b.filename().string();
                    });
                } else {
                    ImGui::TextDisabled("Pasta nao disponivel.");
                }

                for (const auto& d : dirs) {
                    const std::string label = d.filename().string().empty() ? d.string() : d.filename().string();
                    if (ImGui::Selectable(label.c_str(), false)) {
                        std::snprintf(m_workspacePickerPathBuf, sizeof(m_workspacePickerPathBuf), "%s", d.string().c_str());
                        std::snprintf(m_workspaceRootBuf, sizeof(m_workspaceRootBuf), "%s", d.string().c_str());
                    }
                }
            }
            ImGui::EndChild();

            if (ImGui::Button("Usar pasta atual", ImVec2(150.f, 0.f))) {
                std::error_code ec;
                const fs::path chosen(m_workspacePickerPathBuf);
                if (fs::exists(chosen, ec) && fs::is_directory(chosen, ec)) {
                    std::snprintf(m_workspaceRootBuf, sizeof(m_workspaceRootBuf), "%s", chosen.string().c_str());
                    m_toolsMessage = "Workspace selecionado no seletor interno.";
                } else {
                    m_toolsMessage = "Pasta invalida no seletor interno.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Ocultar Seletor", ImVec2(150.f, 0.f))) {
                m_showWorkspacePickerModal = false;
            }
        }
        ImGui::TextDisabled("Ao aplicar, o app gravara data em <workspace>/data e ajustara roots monitoradas.");
        ImGui::Separator();
        if (ImGui::Button("Aplicar", ImVec2(110.f, 0.f))) {
            std::string workspace = m_workspaceRootBuf;
            if ((workspace.empty() || !fs::is_directory(fs::path(workspace))) && std::strlen(m_workspacePickerPathBuf) > 0) {
                workspace = m_workspacePickerPathBuf;
            }

            std::error_code ec;
            if (workspace.empty() || !fs::exists(fs::path(workspace), ec) || !fs::is_directory(fs::path(workspace), ec)) {
                m_toolsMessage = "Workspace invalido. Selecione uma pasta existente.";
                ImGui::EndPopup();
                return;
            }
            std::snprintf(m_workspaceRootBuf, sizeof(m_workspaceRootBuf), "%s", workspace.c_str());
            const fs::path workspaceDataDir = fs::path(workspace) / "data";

            const fs::path settingsPath = m_settingsPath.empty() ? fs::path("data/settings.json") : fs::path(m_settingsPath);
            fs::create_directories(settingsPath.parent_path(), ec);

            json cfg = json::object();
            std::ifstream in(settingsPath);
            if (in.is_open()) {
                try { cfg = json::parse(in); } catch (...) { cfg = json::object(); }
            }

            cfg["workspace_root"] = workspace;
            if (!workspace.empty()) {
                fs::create_directories(workspaceDataDir, ec);
                cfg["data_dir"] = workspaceDataDir.string();
                cfg["monitored_roots"] = json::array({
                    {
                        {"path", workspace},
                        {"category", "Workspace"},
                        {"tags", json::array({"Auto", "Workspace"})},
                        {"marker_files", json::array({"CMakeLists.txt", "meson.build", "Makefile", "compile_commands.json", "pyproject.toml", "requirements.txt", "setup.py", "Pipfile"})}
                    }
                });
            }

            std::ofstream out(settingsPath);
            if (!out.is_open()) {
                m_toolsMessage = "Falha ao salvar workspace.";
            } else {
                out << cfg.dump(2);
                if (out.good()) {
                    const fs::path activeProjects = workspaceDataDir / "projects.json";
                    m_dataPath = activeProjects.string();
                    m_store.saveToJson(m_dataPath);
                    m_store.clearDirty();
                    scanWorkspaceInventory();
                    m_toolsMessage = "Workspace salvo e aplicado.";
                } else {
                    m_toolsMessage = "Falha ao gravar workspace.";
                }
            }
            m_showWorkspaceModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(110.f, 0.f))) {
            m_showWorkspaceModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    // Auto-save
    if (m_store.isDirty()) {
        m_store.saveToJson(m_dataPath);
        m_store.clearDirty();
    }
}

void AppUI::reclassifyKanbanAuto() {
    const auto result = application::reclassifyKanbanAuto(m_store);

    std::ostringstream msg;
    msg << "Reclassificacao auto: " << result.changed << " alterado(s), "
        << result.unchanged << " sem mudanca, "
        << result.skipped << " ignorado(s).";
    m_toolsMessage = msg.str();
}

void AppUI::renderPlanningTab() {
    if (m_store.getAll().empty()) {
        ImGui::TextDisabled("Nenhum projeto cadastrado ainda.");
        ImGui::TextDisabled("Crie um projeto na aba [Projetos] para iniciar o planejamento.");
        return;
    }

    auto pickNewestProjectId = [&]() -> std::string {
        const auto& all = m_store.getAll();
        if (all.empty()) return {};
        const Project* best = &all.front();
        for (const auto& p : all) {
            if (p.created_at > best->created_at) {
                best = &p;
                continue;
            }
            if (p.created_at == best->created_at && p.name > best->name) {
                best = &p;
            }
        }
        return best->id;
    };

    bool validSelection = false;
    if (!m_planningProjectId.empty()) {
        validSelection = m_store.findById(m_planningProjectId).has_value();
    }
    if (!validSelection) {
        m_planningProjectId = pickNewestProjectId();
    }

    std::string comboLabel = "Selecione um projeto...";
    if (auto opt = m_store.findById(m_planningProjectId); opt) {
        comboLabel = (*opt)->name + " (" + (*opt)->created_at + ")";
    }

    ImGui::SetNextItemWidth(420.f);
    if (ImGui::BeginCombo("Projeto em planejamento", comboLabel.c_str())) {
        for (const auto& p : m_store.getAll()) {
            const bool selected = (p.id == m_planningProjectId);
            std::string label = p.name + " (" + p.created_at + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_planningProjectId = p.id;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    auto selected = m_store.findById(m_planningProjectId);
    if (!selected || !(*selected)) {
        ImGui::TextDisabled("Projeto selecionado nao encontrado.");
        return;
    }
    Project* current = *selected;

    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), "Projeto: %s", current->name.c_str());
    ImGui::TextDisabled("Categoria: %s | Status: %s", current->category.c_str(), statusToString(current->status).c_str());
    ImGui::TextDisabled("Criado em: %s", current->created_at.c_str());
    ImGui::Separator();

    ImGui::TextWrapped("%s", current->description.empty() ? "Sem descricao inicial." : current->description.c_str());
    if (!current->tags.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Tags:");
        for (const auto& tag : current->tags) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", tag.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Gerar backlog semente")) {
        std::vector<std::string> defaults = {
            "Configurar pipeline CI",
            "Criar base de testes automatizados",
            "Adicionar static analysis e lint",
            "Definir primeiros ADRs"
        };
        int added = 0;
        for (const auto& title : defaults) {
            bool exists = false;
            for (const auto& dai : current->dais) {
                if (dai.title == title) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            Project::DaiEntry dai;
            dai.id = ProjectStore::generateId();
            dai.kind = "Action";
            dai.title = title;
            dai.owner = "";
            dai.notes = "Gerado automaticamente no kick-off de planejamento.";
            dai.opened_at = todayISO();
            dai.due_at = "";
            dai.closed = false;
            dai.closed_at = "";
            current->dais.push_back(std::move(dai));
            added++;
        }
        m_store.update(*current);
        m_toolsMessage = added > 0
            ? ("Backlog semente criado (" + std::to_string(added) + " item(ns)).")
            : "Backlog semente ja existente.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Adicionar ADR inicial")) {
        bool hasKickoffAdr = false;
        for (const auto& adr : current->adrs) {
            if (adr.title == "ADR: Direcao Inicial do Projeto") {
                hasKickoffAdr = true;
                break;
            }
        }
        if (!hasKickoffAdr) {
            Project::AdrEntry adr;
            adr.id = ProjectStore::generateId();
            adr.title = "ADR: Direcao Inicial do Projeto";
            adr.context = "Criacao do projeto com definicao de escopo inicial.";
            adr.decision = "Adotar kickoff leve com backlog semente e governanca basica.";
            adr.consequences = "Maior previsibilidade nas primeiras entregas.";
            adr.created_at = todayISO();
            current->adrs.push_back(std::move(adr));
            m_store.update(*current);
            m_toolsMessage = "ADR inicial adicionado.";
        } else {
            m_toolsMessage = "ADR inicial ja existe.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Continuar em Projetos")) {
        m_focusProjectsTab = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Checkpoints de Planejamento");
    ImGui::BulletText("Escopo inicial registrado na descricao");
    ImGui::BulletText("Backlog semente com acoes tecnicas");
    ImGui::BulletText("ADR inicial de direcao");
    ImGui::BulletText("Pronto para detalhamento na aba [Projetos]");
}

void AppUI::renderFlowMetricsTab() {
    if (m_repoInventory.empty()) {
        scanWorkspaceInventory();
    }

    static constexpr std::array<int, 3> kWindows = {7, 14, 30};
    static const char* kWindowLabels[] = {"7 dias", "14 dias", "30 dias"};
    const int periodDays = kWindows[static_cast<std::size_t>(m_metricsPeriodIdx)];

    ImGui::SetNextItemWidth(130.f);
    ImGui::TextUnformatted("Periodo:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##metric_period", kWindowLabels[m_metricsPeriodIdx])) {
        for (int i = 0; i < static_cast<int>(kWindows.size()); i++) {
            if (ImGui::Selectable(kWindowLabels[i], m_metricsPeriodIdx == i)) {
                m_metricsPeriodIdx = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Exportar CSV")) {
        std::string outPath;
        std::string err;
        if (exportFlowMetricsCsv(periodDays, &outPath, &err)) {
            m_metricsExportMessage = "CSV salvo em: " + outPath;
        } else {
            m_metricsExportMessage = "Falha ao exportar CSV: " + err;
        }
    }
    if (!m_metricsExportMessage.empty()) {
        ImGui::TextWrapped("%s", m_metricsExportMessage.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Exportar Relatorio Executivo")) {
        std::string outPath;
        std::string err;
        if (exportExecutiveReport(&outPath, &err)) {
            m_metricsActionMessage = "Relatorio executivo salvo em: " + outPath;
        } else {
            m_metricsActionMessage = "Falha ao exportar relatorio executivo: " + err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Gerar Plano Top 10 Criticos")) {
        m_metricsTopCriticalPlan.clear();
        const auto top10 = topCriticalRepos(10);
        for (const auto* repo : top10) {
            if (!repo) continue;
            const auto plan = generateActionPlanForRepo(*repo);
            m_metricsTopCriticalPlan.push_back(repo->name + " (score " + std::to_string(repo->scoreTotal) + ")");
            std::size_t maxItems = std::min<std::size_t>(2, plan.size());
            for (std::size_t i = 0; i < maxItems; i++) {
                m_metricsTopCriticalPlan.push_back(" - " + plan[i]);
            }
        }
        m_metricsActionMessage = m_metricsTopCriticalPlan.empty()
            ? "Nao ha repos para gerar plano."
            : "Plano Top 10 criticos atualizado.";
    }
    if (!m_metricsActionMessage.empty()) {
        ImGui::TextWrapped("%s", m_metricsActionMessage.c_str());
    }

    const auto gm = m_store.computeGlobalFlowMetrics("", periodDays);
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), "Indicadores Globais de Fluxo");
    ImGui::Separator();

    ImGui::Text("Projetos totais: %d", gm.total_projects);
    ImGui::Text("Projetos em WIP: %d", gm.wip_projects);
    ImGui::Text("Projetos concluidos: %d", gm.done_projects);
    ImGui::Text("Throughput (%d dias): %d", periodDays, gm.throughput_in_window);
    ImGui::Text("Aging medio: %.1f dias", gm.avg_aging_days);
    ImGui::Text("Impedimentos abertos: %d", gm.open_impediments);

    ImGui::Spacing();
    ImGui::SeparatorText("Distribuicao por Status");
    int counts[5] = {0, 0, 0, 0, 0};
    for (const auto& p : m_store.getAll()) {
        const int idx = static_cast<int>(p.status);
        if (idx >= 0 && idx < 5) counts[idx]++;
    }
    const char* labels[5] = {"Backlog", "Em Andamento", "Revisao", "Concluido", "Pausado"};
    for (int i = 0; i < 5; i++) {
        ImGui::BulletText("%s: %d", labels[i], counts[i]);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Saude Tecnica");
    if (m_repoInventory.empty()) {
        ImGui::TextDisabled("Sem dados de inventario.");
    } else {
        float sumTotal = 0.f, sumOper = 0.f, sumMatur = 0.f, sumConfiabCfg = 0.f, sumConfiabExec = 0.f;
        for (const auto& repo : m_repoInventory) {
            sumTotal += static_cast<float>(repo.scoreTotal);
            sumOper += static_cast<float>(repo.scoreOperational);
            sumMatur += static_cast<float>(repo.scoreMaturity);
            sumConfiabCfg += static_cast<float>(repo.scoreReliability);
            sumConfiabExec += static_cast<float>(repo.scoreReliabilityExec);
        }
        const float denom = static_cast<float>(m_repoInventory.size());
        ImGui::Text("Media Total: %.1f", sumTotal / denom);
        ImGui::Text("Media Operacional: %.1f", sumOper / denom);
        ImGui::Text("Media Maturidade: %.1f", sumMatur / denom);
        ImGui::Text("Media Confiab (Config): %.1f", sumConfiabCfg / denom);
        ImGui::Text("Media Confiab (Exec): %.1f", sumConfiabExec / denom);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Cobertura de Boas Praticas");
    if (!m_repoInventory.empty()) {
        const float total = static_cast<float>(m_repoInventory.size());
        auto pct = [total](int count) {
            if (total <= 0.f) return 0.f;
            return (static_cast<float>(count) * 100.f) / total;
        };

        int cCi = 0, cTests = 0, cAsan = 0, cStatic = 0, cFormat = 0;
        for (const auto& repo : m_repoInventory) {
            if (repo.hasCi) cCi++;
            if (repo.hasTests) cTests++;
            if (repo.hasAsanUbsan) cAsan++;
            if (repo.hasStaticAnalysis) cStatic++;
            if (repo.hasFormatLint) cFormat++;
        }
        ImGui::Text("CI: %.1f%%", pct(cCi));
        ImGui::Text("Testes: %.1f%%", pct(cTests));
        ImGui::Text("ASan/UBSan: %.1f%%", pct(cAsan));
        ImGui::Text("Static Analysis: %.1f%%", pct(cStatic));
        ImGui::Text("Format/Lint: %.1f%%", pct(cFormat));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Top 5 Criticos");
    const auto top5 = topCriticalRepos(5);
    if (top5.empty()) {
        ImGui::TextDisabled("Nenhum repositorio no inventario.");
    } else {
        for (const auto* repo : top5) {
            if (!repo) continue;
            ImGui::BulletText("%s | Total=%d | Artefatos=%d | CI=%s | Testes=%s",
                repo->name.c_str(),
                repo->scoreTotal,
                repo->detectedArtifacts,
                repo->hasCi ? "OK" : "-",
                repo->hasTests ? "OK" : "-");
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Tendencia");
    float snapshotAvgTotal = 0.f;
    std::string snapshotDate;
    std::string snapshotErr;
    if (loadLatestInventorySnapshotMetrics(&snapshotAvgTotal, &snapshotDate, &snapshotErr) && !m_repoInventory.empty()) {
        float currentAvgTotal = 0.f;
        for (const auto& repo : m_repoInventory) currentAvgTotal += static_cast<float>(repo.scoreTotal);
        currentAvgTotal /= static_cast<float>(m_repoInventory.size());
        const float delta = currentAvgTotal - snapshotAvgTotal;
        ImGui::Text("Media Total atual: %.1f", currentAvgTotal);
        ImGui::Text("Ultimo snapshot (%s): %.1f", snapshotDate.c_str(), snapshotAvgTotal);
        ImGui::Text("Variacao: %+0.1f", delta);
    } else {
        ImGui::TextDisabled("Sem snapshot de inventario exportado para comparar tendencia.");
    }

    if (!m_metricsTopCriticalPlan.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Plano Top 10 Criticos");
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& line : m_metricsTopCriticalPlan) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::PopTextWrapPos();
    }
}

std::vector<const AppUI::RepoInventoryEntry*> AppUI::topCriticalRepos(std::size_t limit) const {
    std::vector<const RepoInventoryEntry*> rows;
    rows.reserve(m_repoInventory.size());
    for (const auto& repo : m_repoInventory) rows.push_back(&repo);
    std::stable_sort(rows.begin(), rows.end(), [](const RepoInventoryEntry* a, const RepoInventoryEntry* b) {
        if (a->scoreTotal != b->scoreTotal) return a->scoreTotal < b->scoreTotal;
        if (a->detectedArtifacts != b->detectedArtifacts) return a->detectedArtifacts > b->detectedArtifacts;
        return a->name < b->name;
    });
    if (rows.size() > limit) rows.resize(limit);
    return rows;
}

bool AppUI::loadLatestInventorySnapshotMetrics(float* avgTotal, std::string* generatedAt, std::string* errorMsg) const {
    if (avgTotal) *avgTotal = 0.f;
    if (generatedAt) generatedAt->clear();
    if (errorMsg) errorMsg->clear();

    const fs::path outDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
    std::error_code ec;
    if (!fs::exists(outDir, ec) || !fs::is_directory(outDir, ec)) {
        if (errorMsg) *errorMsg = "diretorio de dados indisponivel";
        return false;
    }

    fs::path latest;
    fs::file_time_type latestTs{};
    bool found = false;
    for (const auto& entry : fs::directory_iterator(outDir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("inventory-", 0) != 0 || entry.path().extension() != ".json") continue;
        const auto ts = fs::last_write_time(entry.path(), ec);
        if (ec) continue;
        if (!found || ts > latestTs) {
            latestTs = ts;
            latest = entry.path();
            found = true;
        }
    }
    if (!found) {
        if (errorMsg) *errorMsg = "nenhum snapshot inventory-*.json encontrado";
        return false;
    }

    std::ifstream in(latest);
    if (!in.is_open()) {
        if (errorMsg) *errorMsg = "falha ao abrir snapshot";
        return false;
    }

    json data;
    try {
        data = json::parse(in);
    } catch (...) {
        if (errorMsg) *errorMsg = "snapshot invalido";
        return false;
    }
    if (!data.is_object() || !data.contains("repos") || !data["repos"].is_array()) {
        if (errorMsg) *errorMsg = "snapshot sem repos";
        return false;
    }
    float sum = 0.f;
    int count = 0;
    for (const auto& repo : data["repos"]) {
        if (!repo.is_object()) continue;
        if (!repo.contains("score_total")) continue;
        sum += repo.value("score_total", 0.0f);
        count++;
    }
    if (count == 0) {
        if (errorMsg) *errorMsg = "snapshot sem score_total";
        return false;
    }
    if (avgTotal) *avgTotal = sum / static_cast<float>(count);
    if (generatedAt) *generatedAt = data.value("generated_at", latest.filename().string());
    return true;
}

bool AppUI::exportExecutiveReport(std::string* outputPath, std::string* errorMsg) const {
    const fs::path outDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (errorMsg) *errorMsg = "nao foi possivel criar diretorio de saida";
        return false;
    }

    const std::string today = todayISO();
    const fs::path outFile = outDir / ("executive-report-" + today + ".md");
    std::ofstream out(outFile);
    if (!out.is_open()) {
        if (errorMsg) *errorMsg = "nao foi possivel abrir relatorio para escrita";
        return false;
    }

    float avgTotal = 0.f, avgOper = 0.f, avgMatur = 0.f, avgConfiabCfg = 0.f, avgConfiabExec = 0.f;
    if (!m_repoInventory.empty()) {
        for (const auto& repo : m_repoInventory) {
            avgTotal += static_cast<float>(repo.scoreTotal);
            avgOper += static_cast<float>(repo.scoreOperational);
            avgMatur += static_cast<float>(repo.scoreMaturity);
            avgConfiabCfg += static_cast<float>(repo.scoreReliability);
            avgConfiabExec += static_cast<float>(repo.scoreReliabilityExec);
        }
        const float n = static_cast<float>(m_repoInventory.size());
        avgTotal /= n; avgOper /= n; avgMatur /= n; avgConfiabCfg /= n; avgConfiabExec /= n;
    }

    out << "# Relatorio Executivo - LabGestao\n\n";
    out << "- Data: " << today << "\n";
    out << "- Repos no inventario: " << m_repoInventory.size() << "\n";
    out << "- Media Total: " << std::fixed << std::setprecision(1) << avgTotal << "\n";
    out << "- Media Operacional: " << std::fixed << std::setprecision(1) << avgOper << "\n";
    out << "- Media Maturidade: " << std::fixed << std::setprecision(1) << avgMatur << "\n";
    out << "- Media Confiab (Config): " << std::fixed << std::setprecision(1) << avgConfiabCfg << "\n";
    out << "- Media Confiab (Exec): " << std::fixed << std::setprecision(1) << avgConfiabExec << "\n\n";

    out << "## Top 10 Criticos\n\n";
    const auto top10 = topCriticalRepos(10);
    if (top10.empty()) {
        out << "- Nenhum repositorio encontrado.\n";
    } else {
        for (const auto* repo : top10) {
            if (!repo) continue;
            out << "- " << repo->name
                << " | Total=" << repo->scoreTotal
                << " | Artefatos=" << repo->detectedArtifacts
                << " | CI=" << (repo->hasCi ? "OK" : "-")
                << " | Testes=" << (repo->hasTests ? "OK" : "-") << "\n";
            const auto plan = generateActionPlanForRepo(*repo);
            const std::size_t maxItems = std::min<std::size_t>(3, plan.size());
            for (std::size_t i = 0; i < maxItems; i++) {
                out << "  - " << plan[i] << "\n";
            }
        }
    }

    if (!out.good()) {
        if (errorMsg) *errorMsg = "erro ao escrever relatorio";
        return false;
    }
    if (outputPath) *outputPath = outFile.string();
    if (errorMsg) errorMsg->clear();
    return true;
}

void AppUI::scanWorkspaceInventory() {
    const std::string workspace = std::strlen(m_workspaceRootBuf) > 0
        ? std::string(m_workspaceRootBuf)
        : fs::current_path().string();
    if (workspace.empty()) {
        m_repoInventoryMessage = "Workspace vazio.";
        return;
    }

    const fs::path root(workspace);
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        m_repoInventoryMessage = "Workspace invalido para inventario.";
        return;
    }

    m_repoInventory.clear();
    m_repoInventoryRoot = root.string();

    std::vector<fs::path> candidates;
    if (fs::exists(root / ".git")) candidates.push_back(root);
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_directory(ec)) continue;
        const fs::path candidate = entry.path();
        if (fs::exists(candidate / ".git")) candidates.push_back(candidate);
    }

    for (const auto& repoPath : candidates) {
        RepoInventoryEntry item;
        item.path = repoPath.string();
        item.name = repoPath.filename().string().empty() ? repoPath.string() : repoPath.filename().string();
        const std::vector<std::string> repoFiles = listRepoFiles(repoPath);
        item.hasReadme = hasAnyFile(repoPath, {"README.md", "README", "readme.md"});
        item.hasCi = hasCiConfig(repoPath);
        item.hasTests = hasTestsLayout(repoPath);
        item.hasGitignore = fs::exists(repoPath / ".gitignore");
        item.hasLicense = hasAnyFile(repoPath, {"LICENSE", "LICENSE.md", "COPYING"});
        item.buildSystem = detectBuildSystem(repoPath);
        item.detectedArtifacts = countTrackedArtifacts(repoPath);
        if (item.detectedArtifacts < 0) {
            item.detectedArtifacts = countPotentialArtifacts(repoPath);
        }

        const bool hasBuild = item.buildSystem != "Unknown";
        item.scoreOperational = computeRepoHealthScore(
            item.hasReadme,
            item.hasCi,
            item.hasTests,
            hasBuild,
            item.hasGitignore,
            item.hasLicense,
            item.detectedArtifacts
        );
        item.hasAdr = hasAdrEvidence(repoPath);
        item.hasDdd = hasDddEvidence(repoPath);
        item.hasDai = hasDaiEvidence(repoPath);
        item.governanceSignals = governanceSignalsCount(repoPath);
        item.hasGovernance = item.governanceSignals > 0;
        item.scoreMaturity = computeMaturityScore(item.hasAdr, item.hasDdd, item.hasDai, item.governanceSignals);

        const bool hasAddressSan = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-fsanitize=address"});
        const bool hasUndefSan = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-fsanitize=undefined"});
        const bool hasCombinedSan = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {
            "-fsanitize=address,undefined", "-fsanitize=undefined,address", "address,undefined", "undefined,address"
        });
        item.hasAsanUbsan = hasCombinedSan || (hasAddressSan && hasUndefSan);
        item.hasLeakCheck = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"valgrind", "lsan_options", "detect_leaks=1", "asan_options=detect_leaks=1"});
        item.hasStaticAnalysis = fs::exists(repoPath / ".clang-tidy")
            || repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"clang-tidy", "cppcheck"});
        const bool hasWall = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-wall"});
        const bool hasWextra = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-wextra"});
        const bool hasWpedantic = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-wpedantic"});
        const bool hasWerror = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"-werror"});
        item.hasStrictWarnings = hasWall && hasWextra && (hasWpedantic || hasWerror);
        item.hasComplexityGuard = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"lizard", "oclint", "function-size", "cyclomatic", "cognitive complexity"});
        item.hasCycleGuard = repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"include-cycle", "dependency cycle", "cycle check", "deptrac", "module deps"});
        item.hasFormatLint = fs::exists(repoPath / ".clang-format")
            || repoHasAnyPatternInRelevantFiles(repoPath, repoFiles, {"clang-format", "format-check"});
        item.scoreReliability = computeReliabilityScore(
            item.hasAsanUbsan,
            item.hasLeakCheck,
            item.hasStaticAnalysis,
            item.hasStrictWarnings,
            item.hasComplexityGuard,
            item.hasCycleGuard,
            item.hasFormatLint
        );

        item.hasAsanUbsanExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {
            "-fsanitize=address,undefined", "-fsanitize=undefined,address", "-fsanitize=address", "-fsanitize=undefined"
        });
        item.hasLeakCheckExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {
            "valgrind", "detect_leaks=1", "lsan_options"
        });
        item.hasStaticAnalysisExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"clang-tidy", "cppcheck"});
        const bool hasWallExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"-wall"});
        const bool hasWextraExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"-wextra"});
        const bool hasWpedanticExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"-wpedantic"});
        const bool hasWerrorExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"-werror"});
        item.hasStrictWarningsExec = hasWallExec && hasWextraExec && (hasWpedanticExec || hasWerrorExec);
        item.hasComplexityGuardExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"lizard", "oclint", "cyclomatic", "cognitive complexity"});
        item.hasCycleGuardExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"include-cycle", "dependency cycle", "cycle check", "deptrac", "module deps"});
        item.hasFormatLintExec = repoHasAnyPatternInCiFiles(repoPath, repoFiles, {"clang-format", "format-check"});
        item.scoreReliabilityExec = computeReliabilityScore(
            item.hasAsanUbsanExec,
            item.hasLeakCheckExec,
            item.hasStaticAnalysisExec,
            item.hasStrictWarningsExec,
            item.hasComplexityGuardExec,
            item.hasCycleGuardExec,
            item.hasFormatLintExec
        );

        item.scoreTotal = computeCompositeScore(
            item.scoreOperational,
            item.scoreMaturity,
            item.scoreReliability,
            item.scoreReliabilityExec
        );

        for (const auto& p : m_store.getAll()) {
            if (pathEqualsBestEffort(p.source_path, item.path)) {
                item.integratedWithLabGestao = true;
                break;
            }
        }

        m_repoInventory.push_back(std::move(item));
    }

    std::sort(m_repoInventory.begin(), m_repoInventory.end(), [](const RepoInventoryEntry& a, const RepoInventoryEntry& b) {
        if (a.scoreTotal != b.scoreTotal) return a.scoreTotal > b.scoreTotal;
        if (a.scoreOperational != b.scoreOperational) return a.scoreOperational > b.scoreOperational;
        return a.name < b.name;
    });

    if (!m_repoSelectedPath.empty()) {
        bool found = false;
        for (const auto& item : m_repoInventory) {
            if (item.path == m_repoSelectedPath) {
                found = true;
                break;
            }
        }
        if (!found) m_repoSelectedPath.clear();
    }

    std::ostringstream msg;
    msg << "Inventario atualizado: " << m_repoInventory.size() << " repo(s) em " << m_repoInventoryRoot;
    m_repoInventoryMessage = msg.str();
}

void AppUI::renderInventoryTab() {
    if (m_repoInventory.empty() && m_repoInventoryMessage.empty()) {
        scanWorkspaceInventory();
    }

    ImGui::TextDisabled("Workspace: %s", m_repoInventoryRoot.empty() ? "(nao definido)" : m_repoInventoryRoot.c_str());
    if (ImGui::Button("Reescanear")) {
        scanWorkspaceInventory();
    }
    ImGui::SameLine();
    if (ImGui::Button("Exportar CSV")) {
        std::string outPath;
        std::string err;
        if (exportInventoryCsv(&outPath, &err)) {
            m_repoInventoryMessage = "Inventario CSV salvo em: " + outPath;
        } else {
            m_repoInventoryMessage = "Falha ao exportar inventario CSV: " + err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Exportar JSON")) {
        std::string outPath;
        std::string err;
        if (exportInventoryJson(&outPath, &err)) {
            m_repoInventoryMessage = "Inventario JSON salvo em: " + outPath;
        } else {
            m_repoInventoryMessage = "Falha ao exportar inventario JSON: " + err;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.f);
    ImGui::InputTextWithHint("##repo_search", "Filtrar repo...", m_repoSearchBuf, sizeof(m_repoSearchBuf));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.f);
    ImGui::SliderInt("Total Min", &m_repoMinScore, 0, 100);
    ImGui::SameLine();
    ImGui::Checkbox("So integrados", &m_repoOnlyIntegrated);
    ImGui::SameLine();
    static const char* kSortLabels[] = {"Total", "Operacional", "Maturidade", "Confiab(Config)", "Confiab(Exec)"};
    ImGui::SetNextItemWidth(150.f);
    if (ImGui::BeginCombo("Ordenar", kSortLabels[m_repoSortMode])) {
        for (int i = 0; i < 5; i++) {
            if (ImGui::Selectable(kSortLabels[i], m_repoSortMode == i)) m_repoSortMode = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Desc", &m_repoSortDesc);

    if (!m_repoInventoryMessage.empty()) {
        ImGui::TextWrapped("%s", m_repoInventoryMessage.c_str());
    }
    ImGui::Separator();

    const std::string search = toLowerAscii(m_repoSearchBuf);
    int shown = 0;
    std::vector<const RepoInventoryEntry*> rows;
    rows.reserve(m_repoInventory.size());
    for (const auto& item : m_repoInventory) {
        if (item.scoreTotal < m_repoMinScore) continue;
        if (m_repoOnlyIntegrated && !item.integratedWithLabGestao) continue;
        if (!search.empty()) {
            const std::string candidate = toLowerAscii(item.name + " " + item.path + " " + item.buildSystem);
            if (candidate.find(search) == std::string::npos) continue;
        }
        rows.push_back(&item);
    }

    float detailWidth = m_repoSelectedPath.empty() ? 0.f : 360.f;
    float tableWidth = ImGui::GetContentRegionAvail().x - detailWidth - (detailWidth > 0 ? 8.f : 0.f);

    ImGui::BeginChild("##InventoryTableRegion", ImVec2(tableWidth, 0.f), false);
    enum InventorySortKey : ImGuiID {
        kSortRepo = 1,
        kSortTotal,
        kSortOper,
        kSortMatur,
        kSortConfCfg,
        kSortConfExec,
        kSortBuild,
        kSortAdr,
        kSortDdd,
        kSortDai,
        kSortGov,
        kSortAsanUb,
        kSortLeak,
        kSortStatic,
        kSortWarn,
        kSortComplex,
        kSortCycles,
        kSortFormat,
        kSortReadme,
        kSortCi,
        kSortTests,
        kSortArtifacts
    };
    if (ImGui::BeginTable("##repo_inventory", 22,
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Repo",      ImGuiTableColumnFlags_DefaultSort, 2.2f, kSortRepo);
        ImGui::TableSetupColumn("Total(Escore)", 0,                             0.8f, kSortTotal);
        ImGui::TableSetupColumn("Oper",      0,                                 0.8f, kSortOper);
        ImGui::TableSetupColumn("Matur",     0,                                 0.8f, kSortMatur);
        ImGui::TableSetupColumn("ConfCfg",   0,                                 0.8f, kSortConfCfg);
        ImGui::TableSetupColumn("ConfExec",  0,                                 0.8f, kSortConfExec);
        ImGui::TableSetupColumn("Build",     0,                                 1.0f, kSortBuild);
        ImGui::TableSetupColumn("ADR",       0,                                 0.6f, kSortAdr);
        ImGui::TableSetupColumn("DDD",       0,                                 0.6f, kSortDdd);
        ImGui::TableSetupColumn("DAI",       0,                                 0.6f, kSortDai);
        ImGui::TableSetupColumn("Gov",       0,                                 0.7f, kSortGov);
        ImGui::TableSetupColumn("ASan/UB",   0,                                 0.7f, kSortAsanUb);
        ImGui::TableSetupColumn("Leak",      0,                                 0.6f, kSortLeak);
        ImGui::TableSetupColumn("Static",    0,                                 0.7f, kSortStatic);
        ImGui::TableSetupColumn("Warn",      0,                                 0.6f, kSortWarn);
        ImGui::TableSetupColumn("Complex",   0,                                 0.8f, kSortComplex);
        ImGui::TableSetupColumn("Cycles",    0,                                 0.7f, kSortCycles);
        ImGui::TableSetupColumn("Format",    0,                                 0.7f, kSortFormat);
        ImGui::TableSetupColumn("README",    0,                                 0.7f, kSortReadme);
        ImGui::TableSetupColumn("CI",        0,                                 0.6f, kSortCi);
        ImGui::TableSetupColumn("Testes",    0,                                 0.8f, kSortTests);
        ImGui::TableSetupColumn("Artefatos", 0,                                 0.9f, kSortArtifacts);
        ImGui::TableHeadersRow();

        const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        auto compareEntryByKey = [&](const RepoInventoryEntry* a, const RepoInventoryEntry* b, ImGuiID key) {
            switch (key) {
                case kSortRepo: return a->name.compare(b->name);
                case kSortTotal: return a->scoreTotal - b->scoreTotal;
                case kSortOper: return a->scoreOperational - b->scoreOperational;
                case kSortMatur: return a->scoreMaturity - b->scoreMaturity;
                case kSortConfCfg: return a->scoreReliability - b->scoreReliability;
                case kSortConfExec: return a->scoreReliabilityExec - b->scoreReliabilityExec;
                case kSortBuild: return a->buildSystem.compare(b->buildSystem);
                case kSortAdr: return static_cast<int>(a->hasAdr) - static_cast<int>(b->hasAdr);
                case kSortDdd: return static_cast<int>(a->hasDdd) - static_cast<int>(b->hasDdd);
                case kSortDai: return static_cast<int>(a->hasDai) - static_cast<int>(b->hasDai);
                case kSortGov: return a->governanceSignals - b->governanceSignals;
                case kSortAsanUb: return static_cast<int>(a->hasAsanUbsan) - static_cast<int>(b->hasAsanUbsan);
                case kSortLeak: return static_cast<int>(a->hasLeakCheck) - static_cast<int>(b->hasLeakCheck);
                case kSortStatic: return static_cast<int>(a->hasStaticAnalysis) - static_cast<int>(b->hasStaticAnalysis);
                case kSortWarn: return static_cast<int>(a->hasStrictWarnings) - static_cast<int>(b->hasStrictWarnings);
                case kSortComplex: return static_cast<int>(a->hasComplexityGuard) - static_cast<int>(b->hasComplexityGuard);
                case kSortCycles: return static_cast<int>(a->hasCycleGuard) - static_cast<int>(b->hasCycleGuard);
                case kSortFormat: return static_cast<int>(a->hasFormatLint) - static_cast<int>(b->hasFormatLint);
                case kSortReadme: return static_cast<int>(a->hasReadme) - static_cast<int>(b->hasReadme);
                case kSortCi: return static_cast<int>(a->hasCi) - static_cast<int>(b->hasCi);
                case kSortTests: return static_cast<int>(a->hasTests) - static_cast<int>(b->hasTests);
                case kSortArtifacts: return a->detectedArtifacts - b->detectedArtifacts;
                default: return a->scoreTotal - b->scoreTotal;
            }
        };

        if (sortSpecs && sortSpecs->SpecsCount > 0) {
            if (sortSpecs->SpecsDirty) {
                const ImGuiTableColumnSortSpecs& primary = sortSpecs->Specs[0];
                switch (primary.ColumnUserID) {
                    case kSortTotal: m_repoSortMode = 0; break;
                    case kSortOper: m_repoSortMode = 1; break;
                    case kSortMatur: m_repoSortMode = 2; break;
                    case kSortConfCfg: m_repoSortMode = 3; break;
                    case kSortConfExec: m_repoSortMode = 4; break;
                    default: break;
                }
                m_repoSortDesc = (primary.SortDirection == ImGuiSortDirection_Descending);
                const_cast<ImGuiTableSortSpecs*>(sortSpecs)->SpecsDirty = false;
            }

            std::stable_sort(rows.begin(), rows.end(), [&](const RepoInventoryEntry* a, const RepoInventoryEntry* b) {
                for (int n = 0; n < sortSpecs->SpecsCount; ++n) {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[n];
                    int cmp = compareEntryByKey(a, b, spec.ColumnUserID);
                    if (cmp == 0) continue;
                    return (spec.SortDirection == ImGuiSortDirection_Ascending) ? (cmp < 0) : (cmp > 0);
                }
                return a->name < b->name;
            });
        } else {
            std::stable_sort(rows.begin(), rows.end(), [&](const RepoInventoryEntry* a, const RepoInventoryEntry* b) {
                int cmp = 0;
                switch (m_repoSortMode) {
                    case 0: cmp = a->scoreTotal - b->scoreTotal; break;
                    case 1: cmp = a->scoreOperational - b->scoreOperational; break;
                    case 2: cmp = a->scoreMaturity - b->scoreMaturity; break;
                    case 3: cmp = a->scoreReliability - b->scoreReliability; break;
                    case 4: cmp = a->scoreReliabilityExec - b->scoreReliabilityExec; break;
                    default: cmp = a->scoreTotal - b->scoreTotal; break;
                }
                if (cmp == 0) cmp = a->name.compare(b->name);
                if (!m_repoSortDesc) cmp = -cmp;
                return cmp > 0;
            });
        }

        shown = static_cast<int>(rows.size());
        for (const RepoInventoryEntry* item : rows) {
            if (!item) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = (m_repoSelectedPath == item->path);
            if (ImGui::Selectable(("##repo_row_" + item->path).c_str(), selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, 20))) {
                m_repoSelectedPath = selected ? "" : item->path;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(item->name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", item->scoreTotal);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", item->scoreOperational);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", item->scoreMaturity);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", item->scoreReliability);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d", item->scoreReliabilityExec);
            ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(item->buildSystem.c_str());
            ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(item->hasAdr ? "OK" : "-");
            ImGui::TableSetColumnIndex(8); ImGui::TextUnformatted(item->hasDdd ? "OK" : "-");
            ImGui::TableSetColumnIndex(9); ImGui::TextUnformatted(item->hasDai ? "OK" : "-");
            ImGui::TableSetColumnIndex(10); ImGui::Text("%d/4", item->governanceSignals);
            ImGui::TableSetColumnIndex(11); ImGui::TextUnformatted(item->hasAsanUbsan ? "OK" : "-");
            ImGui::TableSetColumnIndex(12); ImGui::TextUnformatted(item->hasLeakCheck ? "OK" : "-");
            ImGui::TableSetColumnIndex(13); ImGui::TextUnformatted(item->hasStaticAnalysis ? "OK" : "-");
            ImGui::TableSetColumnIndex(14); ImGui::TextUnformatted(item->hasStrictWarnings ? "OK" : "-");
            ImGui::TableSetColumnIndex(15); ImGui::TextUnformatted(item->hasComplexityGuard ? "OK" : "-");
            ImGui::TableSetColumnIndex(16); ImGui::TextUnformatted(item->hasCycleGuard ? "OK" : "-");
            ImGui::TableSetColumnIndex(17); ImGui::TextUnformatted(item->hasFormatLint ? "OK" : "-");
            ImGui::TableSetColumnIndex(18); ImGui::TextUnformatted(item->hasReadme ? "OK" : "-");
            ImGui::TableSetColumnIndex(19); ImGui::TextUnformatted(item->hasCi ? "OK" : "-");
            ImGui::TableSetColumnIndex(20); ImGui::TextUnformatted(item->hasTests ? "OK" : "-");
            ImGui::TableSetColumnIndex(21); ImGui::Text("%d", item->detectedArtifacts);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!m_repoSelectedPath.empty()) {
        const RepoInventoryEntry* selected = nullptr;
        for (const auto& item : m_repoInventory) {
            if (item.path == m_repoSelectedPath) {
                selected = &item;
                break;
            }
        }
        if (selected) {
            if (m_repoActionPlanPath != selected->path) {
                m_repoActionPlanPath.clear();
                m_repoActionPlan.clear();
            }
            ImGui::SameLine();
            ImGui::BeginChild("##InventoryDetailRegion", ImVec2(detailWidth, 0.f), true);
            if (ImGui::SmallButton("X")) m_repoSelectedPath.clear();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), "%s", selected->name.c_str());
            ImGui::TextDisabled("%s", selected->path.c_str());
            ImGui::Separator();
            ImGui::Text("Total: %d", selected->scoreTotal);
            ImGui::Text("Operacional: %d", selected->scoreOperational);
            ImGui::Text("Maturidade: %d", selected->scoreMaturity);
            ImGui::Text("Confiab (Config): %d", selected->scoreReliability);
            ImGui::Text("Confiab (Exec): %d", selected->scoreReliabilityExec);
            ImGui::Text("Build: %s", selected->buildSystem.c_str());
            ImGui::Text("Artefatos: %d", selected->detectedArtifacts);
            ImGui::Text("Integrado: %s", selected->integratedWithLabGestao ? "Sim" : "Nao");
            ImGui::Separator();
            ImGui::TextDisabled("Maturidade");
            ImGui::BulletText("ADR: %s", selected->hasAdr ? "OK" : "-");
            ImGui::BulletText("DDD: %s", selected->hasDdd ? "OK" : "-");
            ImGui::BulletText("DAI: %s", selected->hasDai ? "OK" : "-");
            ImGui::BulletText("Governanca: %d/4", selected->governanceSignals);
            ImGui::Separator();
            ImGui::TextDisabled("Confiabilidade");
            ImGui::BulletText("ASan/UBSan: %s", selected->hasAsanUbsan ? "OK" : "-");
            ImGui::BulletText("Leak check: %s", selected->hasLeakCheck ? "OK" : "-");
            ImGui::BulletText("Static analysis: %s", selected->hasStaticAnalysis ? "OK" : "-");
            ImGui::BulletText("Warnings estritos: %s", selected->hasStrictWarnings ? "OK" : "-");
            ImGui::BulletText("Complexidade guard: %s", selected->hasComplexityGuard ? "OK" : "-");
            ImGui::BulletText("Cycle guard: %s", selected->hasCycleGuard ? "OK" : "-");
            ImGui::BulletText("Format/lint: %s", selected->hasFormatLint ? "OK" : "-");
            ImGui::Separator();
            ImGui::TextDisabled("Confiabilidade (Exec no CI)");
            ImGui::BulletText("ASan/UBSan exec: %s", selected->hasAsanUbsanExec ? "OK" : "-");
            ImGui::BulletText("Leak check exec: %s", selected->hasLeakCheckExec ? "OK" : "-");
            ImGui::BulletText("Static analysis exec: %s", selected->hasStaticAnalysisExec ? "OK" : "-");
            ImGui::BulletText("Warnings estritos exec: %s", selected->hasStrictWarningsExec ? "OK" : "-");
            ImGui::BulletText("Complexidade exec: %s", selected->hasComplexityGuardExec ? "OK" : "-");
            ImGui::BulletText("Cycle guard exec: %s", selected->hasCycleGuardExec ? "OK" : "-");
            ImGui::BulletText("Format/lint exec: %s", selected->hasFormatLintExec ? "OK" : "-");
            ImGui::Separator();
            ImGui::TextDisabled("Diagnostico");
            if (selected->scoreTotal >= 80) ImGui::TextWrapped("Projeto em zona saudavel. Prioridade: manter padroes e monitorar regressao.");
            else if (selected->scoreTotal >= 60) ImGui::TextWrapped("Projeto em zona de atencao. Prioridade: subir confiabilidade e governanca.");
            else if (selected->scoreTotal >= 40) ImGui::TextWrapped("Projeto em risco moderado. Prioridade: CI + testes + checks de confiabilidade.");
            else ImGui::TextWrapped("Projeto critico. Prioridade: baseline operacional e eliminacao de riscos tecnicos.");

            ImGui::Separator();
            if (ImGui::Button("Gerar Plano de Acao")) {
                m_repoActionPlan = generateActionPlanForRepo(*selected);
                m_repoActionPlanPath = selected->path;
            }
            if (m_repoActionPlanPath == selected->path && !m_repoActionPlan.empty()) {
                ImGui::TextDisabled("Plano sugerido");
                ImGui::PushTextWrapPos(0.0f);
                for (std::size_t i = 0; i < m_repoActionPlan.size(); i++) {
                    const std::string line = std::to_string(i + 1) + ". " + m_repoActionPlan[i];
                    ImGui::TextWrapped("%s", line.c_str());
                }
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
        } else {
            m_repoSelectedPath.clear();
        }
    }

    ImGui::TextDisabled("Repos exibidos: %d / %zu", shown, m_repoInventory.size());
    ImGui::TextDisabled("Operacional: README(20)+CI(20)+Testes(15)+Build(20)+Gitignore(10)+License(5)+SemArtefatos(10)");
    ImGui::TextDisabled("Maturidade: ADR(25)+DDD(25)+DAI(25)+Governanca(0..25 via 0..4 sinais)");
    ImGui::TextDisabled("Confiabilidade (Config/Exec): ASan/UBSan(15)+Leak(10)+Static(20)+Warnings(15)+Complex(15)+Cycles(10)+Format(15)");
    ImGui::TextDisabled("Total: Operacional*0.45 + Maturidade*0.30 + ConfiabCfg*0.15 + ConfiabExec*0.10");
}

std::vector<std::string> AppUI::generateActionPlanForRepo(const RepoInventoryEntry& repo) const {
    std::vector<std::string> plan;
    plan.reserve(12);

    if (!repo.hasReadme) plan.push_back("Criar README com objetivo, build, execucao e troubleshooting minimo.");
    if (!repo.hasCi) plan.push_back("Configurar pipeline CI com build + testes no branch principal.");
    if (!repo.hasTests) plan.push_back("Adicionar smoke test inicial e gatilho no CI.");
    if (!repo.hasGitignore) plan.push_back("Padronizar .gitignore para impedir artefatos de build/versionamento.");
    if (!repo.hasLicense) plan.push_back("Definir e versionar LICENSE alinhada ao uso do projeto.");
    if (repo.detectedArtifacts > 0) plan.push_back("Remover artefatos rastreados e reforcar bloqueio via .gitignore.");

    if (!repo.hasAdr) plan.push_back("Registrar ao menos 1 ADR sobre decisoes estruturais atuais.");
    if (!repo.hasDdd) plan.push_back("Criar documento DDD basico (contexto, limites, linguagem ubíqua).");
    if (!repo.hasDai) plan.push_back("Abrir backlog DAI com donos e status para riscos/acoes.");
    if (repo.governanceSignals == 0) plan.push_back("Adicionar governanca minima: CONTRIBUTING, templates PR/Issue e CODEOWNERS.");

    if (!repo.hasAsanUbsan) plan.push_back("Adicionar job com ASan/UBSan para detectar memoria/comportamento indefinido.");
    if (!repo.hasLeakCheck) plan.push_back("Adicionar leak check (ASan leak detector ou valgrind) em CI noturno.");
    if (!repo.hasStaticAnalysis) plan.push_back("Ativar clang-tidy/cppcheck no pipeline com baseline inicial.");
    if (!repo.hasStrictWarnings) plan.push_back("Ativar -Wall -Wextra e evoluir para pedantic/Werror gradualmente.");
    if (!repo.hasComplexityGuard) plan.push_back("Definir limite de complexidade (ex.: lizard) para evitar spaghetti growth.");
    if (!repo.hasCycleGuard) plan.push_back("Adicionar verificador de dependencias ciclicas entre modulos.");
    if (!repo.hasFormatLint) plan.push_back("Padronizar clang-format e format-check no CI.");

    if (plan.empty()) {
        plan.push_back("Manter padrao atual e monitorar regressao semanal via Inventario/Grafo.");
        plan.push_back("Elevar cobertura de confiabilidade executando checks reais (fase 2) no CI.");
    }
    return plan;
}

bool AppUI::exportInventoryCsv(std::string* outputPath, std::string* errorMsg) const {
    const fs::path outDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (errorMsg) *errorMsg = "nao foi possivel criar diretorio de saida";
        return false;
    }

    const std::string today = todayISO();
    const fs::path outFile = outDir / ("inventory-" + today + ".csv");
    std::ofstream out(outFile);
    if (!out.is_open()) {
        if (errorMsg) *errorMsg = "nao foi possivel abrir arquivo para escrita";
        return false;
    }

    out << "repo,path,score_total,score_operational,score_maturity,score_reliability_config,score_reliability_exec,build,adr,ddd,dai,governance_signals,asan_ubsan_cfg,leak_check_cfg,static_analysis_cfg,strict_warnings_cfg,complexity_guard_cfg,cycle_guard_cfg,format_lint_cfg,asan_ubsan_exec,leak_check_exec,static_analysis_exec,strict_warnings_exec,complexity_guard_exec,cycle_guard_exec,format_lint_exec,readme,ci,tests,gitignore,license,artifacts,integrated\n";
    for (const auto& item : m_repoInventory) {
        out << escapeCsvField(item.name) << ","
            << escapeCsvField(item.path) << ","
            << item.scoreTotal << ","
            << item.scoreOperational << ","
            << item.scoreMaturity << ","
            << item.scoreReliability << ","
            << item.scoreReliabilityExec << ","
            << escapeCsvField(item.buildSystem) << ","
            << (item.hasAdr ? "1" : "0") << ","
            << (item.hasDdd ? "1" : "0") << ","
            << (item.hasDai ? "1" : "0") << ","
            << item.governanceSignals << ","
            << (item.hasAsanUbsan ? "1" : "0") << ","
            << (item.hasLeakCheck ? "1" : "0") << ","
            << (item.hasStaticAnalysis ? "1" : "0") << ","
            << (item.hasStrictWarnings ? "1" : "0") << ","
            << (item.hasComplexityGuard ? "1" : "0") << ","
            << (item.hasCycleGuard ? "1" : "0") << ","
            << (item.hasFormatLint ? "1" : "0") << ","
            << (item.hasAsanUbsanExec ? "1" : "0") << ","
            << (item.hasLeakCheckExec ? "1" : "0") << ","
            << (item.hasStaticAnalysisExec ? "1" : "0") << ","
            << (item.hasStrictWarningsExec ? "1" : "0") << ","
            << (item.hasComplexityGuardExec ? "1" : "0") << ","
            << (item.hasCycleGuardExec ? "1" : "0") << ","
            << (item.hasFormatLintExec ? "1" : "0") << ","
            << (item.hasReadme ? "1" : "0") << ","
            << (item.hasCi ? "1" : "0") << ","
            << (item.hasTests ? "1" : "0") << ","
            << (item.hasGitignore ? "1" : "0") << ","
            << (item.hasLicense ? "1" : "0") << ","
            << item.detectedArtifacts << ","
            << (item.integratedWithLabGestao ? "1" : "0") << "\n";
    }

    if (!out.good()) {
        if (errorMsg) *errorMsg = "erro durante escrita do CSV";
        return false;
    }
    if (outputPath) *outputPath = outFile.string();
    if (errorMsg) errorMsg->clear();
    return true;
}

bool AppUI::exportInventoryJson(std::string* outputPath, std::string* errorMsg) const {
    const fs::path outDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (errorMsg) *errorMsg = "nao foi possivel criar diretorio de saida";
        return false;
    }

    const std::string today = todayISO();
    const fs::path outFile = outDir / ("inventory-" + today + ".json");
    json root;
    root["generated_at"] = today;
    root["workspace"] = m_repoInventoryRoot;
    root["repos"] = json::array();
    for (const auto& item : m_repoInventory) {
        root["repos"].push_back({
            {"repo", item.name},
            {"path", item.path},
            {"score_total", item.scoreTotal},
            {"score_operational", item.scoreOperational},
            {"score_maturity", item.scoreMaturity},
            {"score_reliability_config", item.scoreReliability},
            {"score_reliability_exec", item.scoreReliabilityExec},
            {"build", item.buildSystem},
            {"adr", item.hasAdr},
            {"ddd", item.hasDdd},
            {"dai", item.hasDai},
            {"governance_signals", item.governanceSignals},
            {"asan_ubsan_cfg", item.hasAsanUbsan},
            {"leak_check_cfg", item.hasLeakCheck},
            {"static_analysis_cfg", item.hasStaticAnalysis},
            {"strict_warnings_cfg", item.hasStrictWarnings},
            {"complexity_guard_cfg", item.hasComplexityGuard},
            {"cycle_guard_cfg", item.hasCycleGuard},
            {"format_lint_cfg", item.hasFormatLint},
            {"asan_ubsan_exec", item.hasAsanUbsanExec},
            {"leak_check_exec", item.hasLeakCheckExec},
            {"static_analysis_exec", item.hasStaticAnalysisExec},
            {"strict_warnings_exec", item.hasStrictWarningsExec},
            {"complexity_guard_exec", item.hasComplexityGuardExec},
            {"cycle_guard_exec", item.hasCycleGuardExec},
            {"format_lint_exec", item.hasFormatLintExec},
            {"readme", item.hasReadme},
            {"ci", item.hasCi},
            {"tests", item.hasTests},
            {"gitignore", item.hasGitignore},
            {"license", item.hasLicense},
            {"artifacts", item.detectedArtifacts},
            {"integrated", item.integratedWithLabGestao}
        });
    }

    std::ofstream out(outFile);
    if (!out.is_open()) {
        if (errorMsg) *errorMsg = "nao foi possivel abrir arquivo para escrita";
        return false;
    }
    out << root.dump(2);
    if (!out.good()) {
        if (errorMsg) *errorMsg = "erro durante escrita do JSON";
        return false;
    }
    if (outputPath) *outputPath = outFile.string();
    if (errorMsg) errorMsg->clear();
    return true;
}

bool AppUI::exportFlowMetricsCsv(int windowDays, std::string* outputPath, std::string* errorMsg) const {
    const fs::path outDir = fs::path(m_dataPath).parent_path().empty() ? fs::path("data") : fs::path(m_dataPath).parent_path();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (errorMsg) *errorMsg = "nao foi possivel criar diretorio de saida";
        return false;
    }

    const std::string today = todayISO();
    const fs::path outFile = outDir / ("metrics-" + today + "-" + std::to_string(windowDays) + "d.csv");
    std::ofstream out(outFile);
    if (!out.is_open()) {
        if (errorMsg) *errorMsg = "nao foi possivel abrir arquivo para escrita";
        return false;
    }

    const auto gm = m_store.computeGlobalFlowMetrics(today, windowDays);
    out << "section,key,value\n";
    out << "global,total_projects," << gm.total_projects << "\n";
    out << "global,wip_projects," << gm.wip_projects << "\n";
    out << "global,done_projects," << gm.done_projects << "\n";
    out << "global,throughput_" << windowDays << "d," << gm.throughput_in_window << "\n";
    out << "global,avg_aging_days," << std::fixed << std::setprecision(2) << gm.avg_aging_days << "\n";
    out << "global,open_impediments," << gm.open_impediments << "\n";

    out << "\nprojects,id,name,status,lead_time_days,cycle_time_days,aging_days,status_transitions,open_dai_items,open_impediments\n";
    for (const auto& p : m_store.getAll()) {
        const auto m = m_store.computeProjectFlowMetrics(p, today);
        out << "project,"
            << p.id << ","
            << "\"" << p.name << "\"" << ","
            << statusKey(p.status) << ","
            << std::fixed << std::setprecision(2) << m.lead_time_days << ","
            << std::fixed << std::setprecision(2) << m.cycle_time_days << ","
            << std::fixed << std::setprecision(2) << m.aging_days << ","
            << m.status_transitions << ","
            << m.open_dai_items << ","
            << m.open_impediments << "\n";
    }

    if (!out.good()) {
        if (errorMsg) *errorMsg = "erro durante escrita do CSV";
        return false;
    }

    if (outputPath) *outputPath = outFile.string();
    if (errorMsg) errorMsg->clear();
    return true;
}

} // namespace labgestao
