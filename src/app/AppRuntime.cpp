#include "app/AppRuntime.hpp"

#include "domain/ProjectStore.hpp"
#include "ui/AppUI.hpp"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct MonitoredRoot {
    std::string path;
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> markerFiles;
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
    const std::size_t h = std::hash<std::string>{}(canonicalPath);
    std::ostringstream oss;
    oss << "auto-" << std::hex << h;
    return oss.str();
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
    const std::string today = todayIsoDate();

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
            p.category = root.category;
            p.description = "Projeto detectado automaticamente em pasta monitorada.";
            p.tags = root.tags;
            p.created_at = today;
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

    std::unordered_map<std::string, labgestao::Project> byId;
    byId.reserve(scanned.size());
    for (const auto& p : scanned) byId.emplace(p.id, p);

    std::unordered_set<std::string> currentAutoIds;
    currentAutoIds.reserve(byId.size());
    for (const auto& [id, _] : byId) currentAutoIds.insert(id);

    for (const auto& [id, detected] : byId) {
        auto opt = store.findById(id);
        if (!opt) {
            store.add(detected);
            continue;
        }

        labgestao::Project updated = **opt;
        bool changed = false;

        if (!updated.auto_discovered) { updated.auto_discovered = true; changed = true; }
        if (updated.name != detected.name) { updated.name = detected.name; changed = true; }
        if (updated.category != detected.category) { updated.category = detected.category; changed = true; }
        if (updated.tags != detected.tags) { updated.tags = detected.tags; changed = true; }
        if (updated.source_path != detected.source_path) { updated.source_path = detected.source_path; changed = true; }
        if (updated.source_root != detected.source_root) { updated.source_root = detected.source_root; changed = true; }
        if (updated.created_at.empty()) { updated.created_at = detected.created_at; changed = true; }
        if (updated.description.empty()) { updated.description = detected.description; changed = true; }

        if (changed) store.update(updated);
    }

    std::vector<std::string> staleAutoProjects;
    for (const auto& p : store.getAll()) {
        if (p.auto_discovered && !currentAutoIds.contains(p.id)) staleAutoProjects.push_back(p.id);
    }
    for (const auto& id : staleAutoProjects) store.remove(id);
}

std::vector<MonitoredRoot> defaultMonitoredRoots() {
    return {
        {
            "/run/media/jpereiratrindade/labeco10T/dev/cpp",
            "C++",
            {"Auto", "C++"},
            {"CMakeLists.txt", "meson.build", "Makefile", "compile_commands.json"}
        },
        {
            "/run/media/jpereiratrindade/labeco10T/dev/python",
            "Python",
            {"Auto", "Python"},
            {"pyproject.toml", "requirements.txt", "setup.py", "Pipfile"}
        }
    };
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
    const std::string dataPath = "data/projects.json";

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

    ProjectStore store;
    if (!store.loadFromJson(dataPath)) {
        fs::create_directories("data");
    }

    const std::vector<MonitoredRoot> monitoredRoots = defaultMonitoredRoots();
    syncAutoDiscoveredProjects(store, monitoredRoots);

    AppUI app(store, dataPath, makeCreationDefaults(monitoredRoots));
    auto nextFolderSync = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    bool running = true;
    while (running) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextFolderSync) {
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
