#include "ui/AppUI.hpp"
#include "imgui.h"
#include "json.hpp"
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
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

std::optional<int> daysSinceEpoch(const std::string& isoDate) {
    if (isoDate.size() < 10) return std::nullopt;
    std::tm tm_buf {};
    std::istringstream ss(isoDate.substr(0, 10));
    ss >> std::get_time(&tm_buf, "%Y-%m-%d");
    if (ss.fail()) return std::nullopt;
    tm_buf.tm_hour = 12;
    tm_buf.tm_isdst = -1;
    std::time_t tt = std::mktime(&tm_buf);
    if (tt < 0) return std::nullopt;
    return static_cast<int>(tt / 86400);
}

std::optional<int> daysBetweenIso(const std::string& from, const std::string& to) {
    const auto a = daysSinceEpoch(from);
    const auto b = daysSinceEpoch(to);
    if (!a || !b) return std::nullopt;
    return *b - *a;
}

std::optional<std::string> getLastGitCommitDate(const std::string& projectPath) {
    if (projectPath.empty()) return std::nullopt;
    const std::string cmd = "git -C " + shellEscapeSingleQuoted(projectPath) + " log -1 --format=%cs 2>/dev/null";
    std::string out;
    if (!runPickerCommand(cmd, out)) return std::nullopt;
    if (out.size() < 10) return std::nullopt;
    return out.substr(0, 10);
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Kanban]  ")) {
            ImGui::Spacing();
            m_kanban.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Grafo]  ")) {
            ImGui::Spacing();
            m_graph.render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  [Metricas]  ")) {
            ImGui::Spacing();
            renderFlowMetricsTab();
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
    const std::string today = todayISO();

    auto isAutoProject = [](const Project& p) {
        if (p.auto_discovered) return true;
        for (const auto& tag : p.tags) {
            if (tag == "Auto") return true;
        }
        return false;
    };

    std::vector<std::string> candidateIds;
    candidateIds.reserve(m_store.getAll().size());
    for (const auto& p : m_store.getAll()) {
        if (isAutoProject(p)) candidateIds.push_back(p.id);
    }

    int changed = 0;
    int unchanged = 0;
    int skipped = 0;

    for (const auto& id : candidateIds) {
        auto opt = m_store.findById(id);
        if (!opt || !(*opt)) {
            skipped++;
            continue;
        }
        Project* current = *opt;
        if (!current) {
            skipped++;
            continue;
        }

        int openDai = 0;
        int openImpediments = 0;
        for (const auto& dai : current->dais) {
            if (dai.closed) continue;
            openDai++;
            if (dai.kind == "Impediment") openImpediments++;
        }

        std::optional<ProjectStatus> target;
        if (openImpediments > 0) {
            target = ProjectStatus::Paused;
        } else if (openDai > 0) {
            target = ProjectStatus::Doing;
        } else if (const auto lastCommit = getLastGitCommitDate(current->source_path); lastCommit) {
            if (const auto age = daysBetweenIso(*lastCommit, today); age) {
                if (*age <= 7) target = ProjectStatus::Doing;
                else if (*age <= 30) target = ProjectStatus::Review;
                else if (*age <= 120) target = ProjectStatus::Paused;
                else target = ProjectStatus::Backlog;
            }
        }

        if (!target || *target == current->status) {
            unchanged++;
            continue;
        }

        std::string reason;
        if (!m_store.canMoveToStatus(*current, *target, &reason)) {
            skipped++;
            continue;
        }

        Project updated = *current;
        const ProjectStatus from = updated.status;
        updated.status = *target;
        m_store.recordStatusChange(updated, from, updated.status, today);
        m_store.update(updated);
        changed++;
    }

    std::ostringstream msg;
    msg << "Reclassificacao auto: " << changed << " alterado(s), "
        << unchanged << " sem mudanca, "
        << skipped << " ignorado(s).";
    m_toolsMessage = msg.str();
}

void AppUI::renderFlowMetricsTab() {
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
