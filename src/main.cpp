#include "ui/AppUI.hpp"
#include "domain/ProjectStore.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <cstdio>
#include <string>
#include <filesystem>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

// ── Font Helpers ─────────────────────────────────────────────────────────────
static std::string FindFontPath(const std::vector<const char*>& candidates) {
    for (const char* path : candidates) {
        if (fs::exists(path)) {
            return path;
        }
    }
    return {};
}

static void LoadFonts(ImGuiIO& io) {
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
    };
    const std::vector<const char*> emojiCandidates = {
        "/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/TTF/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/google-noto-color-emoji-fonts/Noto-COLRv1.ttf",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    };
#endif

    std::string basePath = FindFontPath(baseCandidates);
    ImFont* baseFont = nullptr;
    if (!basePath.empty()) {
        baseFont = io.Fonts->AddFontFromFileTTF(basePath.c_str(), baseFontSize);
    }
    if (!baseFont) {
        baseFont = io.Fonts->AddFontDefault();
    }
    io.FontDefault = baseFont;

    std::string emojiPath = FindFontPath(emojiCandidates);
    if (!emojiPath.empty()) {
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;

        static const ImWchar32 emojiRanges[] = {
            0x00A0, 0x00FF,   // Latin-1 Supplement (Accents)
            0x2000, 0x3000,   // General Punctuation, Symbols, Dingbats, Technical
            0x1F300, 0x1FAFF, // Emoji ranges
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

// ── Sample data seed ─────────────────────────────────────────────────────────
static void seedSampleData(labgestao::ProjectStore& store) {
    using P = labgestao::Project;
    using S = labgestao::ProjectStatus;

    auto mk = [&](const char* name, S st, const char* cat, const char* desc,
                  std::vector<std::string> tags) {
        P p;
        p.id          = labgestao::ProjectStore::generateId();
        p.name        = name;
        p.status      = st;
        p.category    = cat;
        p.description = desc;
        p.tags        = std::move(tags);
        p.created_at  = "2026-03-01";
        store.add(std::move(p));
    };

    mk("SIGMA",        S::Doing,   "Ciência", "Sistema de ingestão e análise científica.", {"C++", "NLP", "IA"});
    mk("SisterSTRATA", S::Review,  "GeoSim",  "Engine de simulação estratigráfica.",       {"Vulkan", "GLSL"});
    mk("LabGestao",    S::Doing,   "Gestão",  "App de gestão de projetos (este!).",        {"ImGui","SDL2"});
    mk("FocinhoTraker",S::Backlog, "CV",      "Rastreamento facial com ML.",               {"Python","CV"});
    mk("GeoHexBatch",  S::Done,    "GeoSim",  "Processamento batch de hexágonos geo.",     {"C++","PSQL"});
    mk("PCTG",         S::Paused,  "Rede",    "Protocolo de comunicação TCP genérico.",    {"C++","Net"});
    mk("LabecoDA",     S::Done,    "DataAnl", "Análise de dados ecofuncional.",            {"R","Stats"});
    mk("IdeaWalker",   S::Backlog, "Misc",    "Explorador de ideias em grafo.",            {"ImGui","Graph"});

    // Some connections
    auto& all = store.getAll();
    if (all.size() >= 3) {
        all[0].connections.push_back(all[2].id); // SIGMA <-> LabGestao
        all[2].connections.push_back(all[0].id);
        all[1].connections.push_back(all[4].id); // SisterSTRATA <-> GeoHexBatch
        all[4].connections.push_back(all[1].id);
        all[2].connections.push_back(all[7].id); // LabGestao <-> IdeaWalker
        all[7].connections.push_back(all[2].id);
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    // Resolve data path relative to executable
    std::string dataPath = "data/projects.json";

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL 3.3 core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "LabGestao — Gestão de Projetos",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 760,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) { fprintf(stderr, "Window error: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glCtx);
    SDL_GL_SetSwapInterval(1);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // no imgui.ini

    ImGui_ImplSDL2_InitForOpenGL(window, glCtx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Load nicer fonts with emoji support
    LoadFonts(io);

    // Domain
    labgestao::ProjectStore store;
    if (!store.loadFromJson(dataPath)) {
        // First run — seed sample projects
        seedSampleData(store);
        fs::create_directories("data");
        store.saveToJson(dataPath);
        store.clearDirty();
    }

    labgestao::AppUI app(store, dataPath);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.render();
        if (app.shouldExit()) running = false;

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.07f, 0.08f, 0.11f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Final save
    store.saveToJson(dataPath);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
