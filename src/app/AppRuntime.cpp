#include "app/AppRuntime.hpp"

#include "domain/ProjectStore.hpp"
#include "ui/AppUI.hpp"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "json.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct MonitoredRoot {
    std::string path;
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> markerFiles;
};

struct AppSettings {
    std::string workspaceRoot;
    std::string dataDir;
    std::vector<MonitoredRoot> monitoredRoots;
};

struct InferredProjectMetadata {
    std::string category;
    std::vector<std::string> tags;
};

std::string findFontPath(const std::vector<const char*>& candidates) {
    for (const char* path : candidates) {
        if (fs::exists(path)) return path;
    }
    return {};
}

std::string findFontFromEnv(const char* envName) {
    const char* value = std::getenv(envName);
    if (value && *value && fs::exists(value)) return value;
    return {};
}

void loadFonts(ImGuiIO& io) {
    const float baseFontSize = 16.0f;

#if defined(_WIN32)
    const std::vector<const char*> baseCandidates = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "C:\\Windows\\Fonts\\seguiemj.ttf",
    };
#elif defined(__APPLE__)
    const std::vector<const char*> baseCandidates = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "/System/Library/Fonts/Apple Color Emoji.ttc",
    };
#else
    const std::vector<const char*> baseCandidates = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    };
    const std::vector<const char*> emojiCandidates = {
        "/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/TTF/NotoEmoji-Regular.ttf",
    };
#endif

    std::string basePath = findFontFromEnv("LABGESTAO_BASE_FONT");
    if (basePath.empty()) basePath = findFontPath(baseCandidates);

    ImFont* baseFont = nullptr;
    if (!basePath.empty()) {
        baseFont = io.Fonts->AddFontFromFileTTF(basePath.c_str(), baseFontSize);
    }
    if (!baseFont) {
        baseFont = io.Fonts->AddFontDefault();
        std::fprintf(stderr, "Font warning: using ImGui default bitmap font.\n");
    }
    io.FontDefault = baseFont;

    std::string emojiPath = findFontFromEnv("LABGESTAO_EMOJI_FONT");
    if (emojiPath.empty()) emojiPath = findFontPath(emojiCandidates);

    if (!emojiPath.empty()) {
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;

        static const ImWchar32 emojiRanges[] = {
            0x00A0, 0x00FF,
            0x2000, 0x3000,
            0x1F300, 0x1FAFF,
            0
        };

        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(emojiRanges);
        static ImVector<ImWchar> ranges;
        ranges.clear();
        builder.BuildRanges(&ranges);

        io.Fonts->AddFontFromFileTTF(emojiPath.c_str(), baseFontSize, &config, ranges.Data);
    }
}

std::string todayIsoDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

int daysSinceEpoch(const std::string& isoDate) {
    if (isoDate.size() < 10) return -1;
    std::tm tm_buf {};
    std::istringstream ss(isoDate.substr(0, 10));
    ss >> std::get_time(&tm_buf, "%Y-%m-%d");
    if (ss.fail()) return -1;
    tm_buf.tm_hour = 12;
    tm_buf.tm_isdst = -1;
    std::time_t tt = std::mktime(&tm_buf);
    if (tt < 0) return -1;
    return static_cast<int>(tt / 86400);
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

std::string runAndCaptureTrimmed(const std::string& cmd) {
    std::array<char, 512> buffer{};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    std::string out;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        out += buffer.data();
    }

    const int rc = pclose(pipe);
    if (rc != 0) return {};

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string getLastGitCommitDate(const std::string& projectPath) {
    if (projectPath.empty()) return {};
    const std::string cmd = "git -C " + shellEscapeSingleQuoted(projectPath) + " log -1 --format=%cs 2>/dev/null";
    const std::string out = runAndCaptureTrimmed(cmd);
    if (out.size() < 10) return {};
    return out.substr(0, 10);
}

std::string getFirstGitCommitDate(const std::string& projectPath) {
    if (projectPath.empty()) return {};
    const std::string cmd = "git -C " + shellEscapeSingleQuoted(projectPath) + " log --max-parents=0 -1 --format=%cs 2>/dev/null";
    const std::string out = runAndCaptureTrimmed(cmd);
    if (out.size() < 10) return {};
    return out.substr(0, 10);
}

std::string isoFromSystemClock(std::chrono::system_clock::time_point tp) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

std::string lastWriteTimeIso(const fs::path& p) {
    std::error_code ec;
    const auto ftime = fs::last_write_time(p, ec);
    if (ec) return {};
    const auto nowFile = fs::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    const auto sysTp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - nowFile + nowSys);
    return isoFromSystemClock(sysTp);
}

std::string inferProjectCreatedAt(const fs::path& dir, const MonitoredRoot& root) {
    const std::string gitBirth = getFirstGitCommitDate(dir.string());
    if (!gitBirth.empty()) return gitBirth;

    std::string best = lastWriteTimeIso(dir);
    for (const auto& marker : root.markerFiles) {
        const fs::path markerPath = dir / marker;
        if (!fs::exists(markerPath)) continue;
        const std::string markerDate = lastWriteTimeIso(markerPath);
        if (markerDate.empty()) continue;
        if (best.empty() || markerDate < best) best = markerDate;
    }
    if (!best.empty()) return best;
    return todayIsoDate();
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void appendTagUnique(std::vector<std::string>& tags, const std::string& tag) {
    if (tag.empty()) return;
    for (const auto& existing : tags) {
        if (toLower(existing) == toLower(tag)) return;
    }
    tags.push_back(tag);
}

InferredProjectMetadata inferProjectMetadata(const fs::path& dir, const MonitoredRoot& root) {
    const auto has = [&](const char* name) {
        return fs::exists(dir / name);
    };
    const bool hasCpp = has("CMakeLists.txt") || has("meson.build") || has("compile_commands.json");
    const bool hasPython = has("pyproject.toml") || has("requirements.txt") || has("setup.py") || has("Pipfile");
    const bool hasRust = has("Cargo.toml");
    const bool hasGo = has("go.mod");
    const bool hasNode = has("package.json");
    const bool hasJava = has("pom.xml") || has("build.gradle") || has("build.gradle.kts");
    const bool hasDotnet = has(".sln");
    const bool hasGit = fs::exists(dir / ".git");

    std::string inferredCategory;
    if (hasCpp) inferredCategory = "C++";
    else if (hasPython) inferredCategory = "Python";
    else if (hasRust) inferredCategory = "Rust";
    else if (hasGo) inferredCategory = "Go";
    else if (hasNode) inferredCategory = "Node";
    else if (hasJava) inferredCategory = "Java";
    else if (hasDotnet) inferredCategory = ".NET";

    const std::string rootCategoryLower = toLower(root.category);
    const bool useInferredAsPrimary = root.category.empty() || rootCategoryLower == "workspace";

    InferredProjectMetadata out;
    out.category = useInferredAsPrimary
        ? (inferredCategory.empty() ? "Workspace" : inferredCategory)
        : root.category;

    for (const auto& t : root.tags) appendTagUnique(out.tags, t);
    if (!inferredCategory.empty()) appendTagUnique(out.tags, inferredCategory);
    if (hasGit) appendTagUnique(out.tags, "Git");
    if (hasCpp) appendTagUnique(out.tags, "CMake");
    if (hasPython) appendTagUnique(out.tags, "PyProject");
    if (hasRust) appendTagUnique(out.tags, "Cargo");
    if (hasGo) appendTagUnique(out.tags, "GoMod");
    if (hasNode) appendTagUnique(out.tags, "NPM");
    if (hasJava) appendTagUnique(out.tags, "JVM");
    if (hasDotnet) appendTagUnique(out.tags, "DotNet");
    if (out.tags.empty()) appendTagUnique(out.tags, "Auto");
    return out;
}

void reclassifyKanbanAuto(labgestao::ProjectStore& store) {
    const std::string today = todayIsoDate();

    auto isAutoProject = [](const labgestao::Project& p) {
        if (p.auto_discovered) return true;
        for (const auto& tag : p.tags) {
            if (tag == "Auto") return true;
        }
        return false;
    };

    std::vector<std::string> ids;
    ids.reserve(store.getAll().size());
    for (const auto& p : store.getAll()) {
        if (isAutoProject(p)) ids.push_back(p.id);
    }

    for (const auto& id : ids) {
        auto opt = store.findById(id);
        if (!opt || !(*opt)) continue;
        labgestao::Project* current = *opt;
        if (!current) continue;

        int openDai = 0;
        int openImpediments = 0;
        for (const auto& dai : current->dais) {
            if (dai.closed) continue;
            openDai++;
            if (dai.kind == "Impediment") openImpediments++;
        }

        labgestao::ProjectStatus target = current->status;
        bool hasSignal = false;
        if (openImpediments > 0) {
            target = labgestao::ProjectStatus::Paused;
            hasSignal = true;
        } else if (openDai > 0) {
            target = labgestao::ProjectStatus::Doing;
            hasSignal = true;
        } else {
            const std::string lastCommit = getLastGitCommitDate(current->source_path);
            if (!lastCommit.empty()) {
                const int age = daysSinceEpoch(today) - daysSinceEpoch(lastCommit);
                if (age >= 0) {
                    hasSignal = true;
                    if (age <= 7) target = labgestao::ProjectStatus::Doing;
                    else if (age <= 30) target = labgestao::ProjectStatus::Review;
                    else if (age <= 120) target = labgestao::ProjectStatus::Paused;
                    else target = labgestao::ProjectStatus::Backlog;
                }
            }
        }

        if (!hasSignal || target == current->status) continue;

        std::string reason;
        if (!store.canMoveToStatus(*current, target, &reason)) continue;

        labgestao::Project updated = *current;
        const labgestao::ProjectStatus from = updated.status;
        updated.status = target;
        store.recordStatusChange(updated, from, updated.status, today);
        store.update(updated);
    }
}

std::string canonicalPathOrRaw(const fs::path& p) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec) {
        ec.clear();
        canon = fs::absolute(p, ec);
    }
    if (ec) return p.lexically_normal().string();
    return canon.lexically_normal().string();
}

std::string makeAutoProjectId(const std::string& canonicalPath) {
    // Stable FNV-1a 64-bit hash (deterministic across runs/compilers).
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : canonicalPath) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << "auto-" << std::hex << h;
    return oss.str();
}

bool samePathBestEffort(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    return canonicalPathOrRaw(fs::path(a)) == canonicalPathOrRaw(fs::path(b));
}

bool looksLikeProjectFolder(const fs::path& dir, const MonitoredRoot& root) {
    if (fs::exists(dir / ".git")) return true;
    for (const auto& marker : root.markerFiles) {
        if (fs::exists(dir / marker)) return true;
    }
    return false;
}

std::vector<labgestao::Project> scanProjectsFromRoots(const std::vector<MonitoredRoot>& roots) {
    std::vector<labgestao::Project> discovered;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root.path, ec) || !fs::is_directory(root.path, ec)) continue;

        fs::directory_iterator it(root.path, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::end(it); it.increment(ec)) {
            const fs::directory_entry entry = *it;
            if (!entry.is_directory(ec)) continue;

            const fs::path dir = entry.path();
            const std::string folderName = dir.filename().string();
            if (folderName.empty() || folderName[0] == '.') continue;
            if (!looksLikeProjectFolder(dir, root)) continue;

            const std::string canonicalPath = canonicalPathOrRaw(dir);

            labgestao::Project p;
            p.id = makeAutoProjectId(canonicalPath);
            p.name = folderName;
            p.status = labgestao::ProjectStatus::Backlog;
            const InferredProjectMetadata metadata = inferProjectMetadata(dir, root);
            p.category = metadata.category;
            p.description = "Projeto detectado automaticamente em pasta monitorada.";
            p.tags = metadata.tags;
            p.created_at = inferProjectCreatedAt(dir, root);
            p.auto_discovered = true;
            p.source_path = canonicalPath;
            p.source_root = root.path;
            discovered.push_back(std::move(p));
        }
    }

    std::sort(discovered.begin(), discovered.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return discovered;
}

void syncAutoDiscoveredProjects(labgestao::ProjectStore& store, const std::vector<MonitoredRoot>& roots) {
    const auto scanned = scanProjectsFromRoots(roots);
    std::unordered_set<std::string> currentAutoIds;
    currentAutoIds.reserve(scanned.size());

    for (const auto& detected : scanned) {
        labgestao::Project* target = nullptr;

        if (auto opt = store.findById(detected.id); opt) {
            target = *opt;
        }

        // Keep status/history if ID changes but source path is the same project.
        if (!target && !detected.source_path.empty()) {
            for (auto& existing : store.getAll()) {
                if (existing.auto_discovered && samePathBestEffort(existing.source_path, detected.source_path)) {
                    target = &existing;
                    break;
                }
            }
        }

        if (!target) {
            store.add(detected);
            currentAutoIds.insert(detected.id);
            continue;
        }

        labgestao::Project updated = *target;
        bool changed = false;

        if (!updated.auto_discovered) { updated.auto_discovered = true; changed = true; }
        if (updated.name != detected.name) { updated.name = detected.name; changed = true; }
        if (updated.category != detected.category) { updated.category = detected.category; changed = true; }
        if (updated.tags != detected.tags) { updated.tags = detected.tags; changed = true; }
        if (updated.source_path != detected.source_path) { updated.source_path = detected.source_path; changed = true; }
        if (updated.source_root != detected.source_root) { updated.source_root = detected.source_root; changed = true; }
        if (updated.created_at.empty() || (!detected.created_at.empty() && detected.created_at < updated.created_at)) {
            updated.created_at = detected.created_at;
            changed = true;
        }
        if (updated.description.empty()) { updated.description = detected.description; changed = true; }

        if (changed) store.update(updated);
        currentAutoIds.insert(updated.id);
    }

    std::vector<std::string> staleAutoProjects;
    for (const auto& p : store.getAll()) {
        if (p.auto_discovered && !currentAutoIds.contains(p.id)) staleAutoProjects.push_back(p.id);
    }
    for (const auto& id : staleAutoProjects) store.remove(id);
}

std::vector<MonitoredRoot> defaultMonitoredRoots() {
    return {};
}

std::string settingsPath() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return (fs::path(home) / ".config" / "labgestao" / "settings.json").string();
    }
    return "data/settings.json";
}

fs::path inferDataDirFromRoots(const std::vector<MonitoredRoot>& roots) {
    std::vector<fs::path> dirs;
    dirs.reserve(roots.size());
    for (const auto& root : roots) {
        if (root.path.empty()) continue;
        std::error_code ec;
        fs::path p = fs::weakly_canonical(root.path, ec);
        if (ec) {
            ec.clear();
            p = fs::absolute(root.path, ec);
        }
        if (!ec) dirs.push_back(p.lexically_normal());
    }

    if (dirs.empty()) return fs::path("data");

    fs::path common = dirs.front();
    for (std::size_t i = 1; i < dirs.size(); i++) {
        const fs::path& cur = dirs[i];
        fs::path nextCommon;
        auto itA = common.begin();
        auto itB = cur.begin();
        while (itA != common.end() && itB != cur.end() && *itA == *itB) {
            nextCommon /= *itA;
            ++itA;
            ++itB;
        }
        common = nextCommon;
        if (common.empty()) break;
    }

    if (common.empty() || common == common.root_path()) return fs::path("data");
    return common / "data";
}

json monitoredRootToJson(const MonitoredRoot& root) {
    json j;
    j["path"] = root.path;
    j["category"] = root.category;
    j["tags"] = root.tags;
    j["marker_files"] = root.markerFiles;
    return j;
}

bool monitoredRootFromJson(const json& v, MonitoredRoot* out) {
    if (!out || !v.is_object()) return false;
    const std::string path = v.value("path", "");
    if (path.empty()) return false;
    out->path = path;
    out->category = v.value("category", "");
    out->tags = v.value("tags", std::vector<std::string>{});
    out->markerFiles = v.value("marker_files", std::vector<std::string>{});
    return true;
}

bool saveSettings(const AppSettings& settings, const std::string& path) {
    json j;
    j["workspace_root"] = settings.workspaceRoot;
    j["data_dir"] = settings.dataDir;
    j["monitored_roots"] = json::array();
    for (const auto& root : settings.monitoredRoots) {
        j["monitored_roots"].push_back(monitoredRootToJson(root));
    }

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << j.dump(2);
    return out.good();
}

AppSettings loadSettingsOrDefault(const std::string& path) {
    AppSettings settings;
    settings.workspaceRoot.clear();
    settings.dataDir.clear();
    settings.monitoredRoots = defaultMonitoredRoots();

    std::ifstream in(path);
    if (!in.is_open()) {
        // Legacy compatibility: migrate old settings from local data/settings.json.
        std::ifstream legacy("data/settings.json");
        if (legacy.is_open()) {
            std::ostringstream oss;
            oss << legacy.rdbuf();
            std::ofstream migrated(path);
            if (migrated.is_open()) {
                migrated << oss.str();
            }
            in.close();
            in.open(path);
        }
    }
    if (!in.is_open()) {
        saveSettings(settings, path);
        return settings;
    }

    try {
        const json parsed = json::parse(in);
        if (!parsed.is_object() || !parsed.contains("monitored_roots") || !parsed["monitored_roots"].is_array()) {
            std::fprintf(stderr, "Settings warning: invalid settings.json format, using defaults.\n");
            return settings;
        }
        settings.workspaceRoot = parsed.value("workspace_root", "");
        settings.dataDir = parsed.value("data_dir", "");

        std::vector<MonitoredRoot> loaded;
        for (const auto& item : parsed["monitored_roots"]) {
            MonitoredRoot root;
            if (monitoredRootFromJson(item, &root)) loaded.push_back(std::move(root));
        }

        // If key exists, honor user choice (including empty list).
        settings.monitoredRoots = std::move(loaded);
        return settings;
    } catch (...) {
        std::fprintf(stderr, "Settings warning: failed to parse settings.json, using defaults.\n");
        return settings;
    }
}

labgestao::ListView::CreationDefaults makeCreationDefaults(const std::vector<MonitoredRoot>& roots) {
    labgestao::ListView::CreationDefaults defaults;
    for (const auto& root : roots) {
        if (defaults.cppRoot.empty() && root.category == "C++") defaults.cppRoot = root.path;
        if (defaults.pythonRoot.empty() && root.category == "Python") defaults.pythonRoot = root.path;
    }
    return defaults;
}

} // namespace

namespace labgestao {

int runApp() {
    const std::string appSettingsPath = settingsPath();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "LabGestao - Gestao de Projetos",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 760,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        std::fprintf(stderr, "Window error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glCtx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplSDL2_InitForOpenGL(window, glCtx);
    ImGui_ImplOpenGL3_Init("#version 330");
    loadFonts(io);

    fs::create_directories(fs::path(appSettingsPath).parent_path());

    AppSettings settings = loadSettingsOrDefault(appSettingsPath);
    const std::vector<MonitoredRoot> monitoredRoots = settings.monitoredRoots;
    if (!settings.workspaceRoot.empty()) {
        settings.dataDir = (fs::path(settings.workspaceRoot) / "data").string();
        saveSettings(settings, appSettingsPath);
    }
    if (settings.dataDir.empty()) {
        settings.dataDir = inferDataDirFromRoots(monitoredRoots).string();
        saveSettings(settings, appSettingsPath);
    }

    const fs::path effectiveDataDir = settings.dataDir.empty() ? fs::path("data") : fs::path(settings.dataDir);
    std::error_code dataDirEc;
    fs::create_directories(effectiveDataDir, dataDirEc);
    const std::string dataPath = (effectiveDataDir / "projects.json").string();

    // Keep a copy of active settings next to the effective data directory for transparency.
    saveSettings(settings, (effectiveDataDir / "settings.json").string());

    ProjectStore store;
    store.loadFromJson(dataPath);

    if (!monitoredRoots.empty()) {
        syncAutoDiscoveredProjects(store, monitoredRoots);
        reclassifyKanbanAuto(store);
    }

    AppUI app(store, dataPath, appSettingsPath, settings.workspaceRoot, makeCreationDefaults(monitoredRoots));
    auto nextFolderSync = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    bool running = true;
    while (running) {
        const auto now = std::chrono::steady_clock::now();
        if (!monitoredRoots.empty() && now >= nextFolderSync) {
            syncAutoDiscoveredProjects(store, monitoredRoots);
            nextFolderSync = now + std::chrono::seconds(5);
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.render();
        if (app.shouldExit()) running = false;

        ImGui::Render();
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        glClearColor(0.07f, 0.08f, 0.11f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    store.saveToJson(dataPath);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace labgestao
