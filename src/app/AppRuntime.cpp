#include "app/AppRuntime.hpp"
#include "app/ApplicationServices.hpp"

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
#include <sstream>
#include <string>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

using MonitoredRoot = labgestao::application::MonitoredRoot;

struct AppSettings {
    std::string workspaceRoot;
    std::string dataDir;
    std::vector<MonitoredRoot> monitoredRoots;
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
        application::syncAutoDiscoveredProjects(store, monitoredRoots);
        application::reclassifyKanbanAuto(store);
    }

    AppUI app(store, dataPath, appSettingsPath, settings.workspaceRoot, makeCreationDefaults(monitoredRoots));
    auto nextFolderSync = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    bool running = true;
    while (running) {
        const auto now = std::chrono::steady_clock::now();
        if (!monitoredRoots.empty() && now >= nextFolderSync) {
            application::syncAutoDiscoveredProjects(store, monitoredRoots);
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
