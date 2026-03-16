#include "ui/ListView.hpp"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <utility>
#include <cstdlib>
#include <filesystem>
#include <cctype>

namespace labgestao {
namespace fs = std::filesystem;

static const char* kStatusLabels[] = { "Backlog", "Em Andamento", "Revisão", "Concluído", "Pausado" };
static const char* kDaiKinds[] = { "Decision", "Action", "Impediment" };
static const ImVec4 kStatusColors[] = {
    {0.45f, 0.35f, 0.75f, 1.f}, // Backlog   — roxo
    {0.20f, 0.60f, 0.90f, 1.f}, // Doing     — azul
    {0.90f, 0.70f, 0.10f, 1.f}, // Review    — amarelo
    {0.20f, 0.80f, 0.40f, 1.f}, // Done      — verde
    {0.55f, 0.55f, 0.55f, 1.f}, // Paused    — cinza
};

static std::string nowISO() {
    auto t = std::time(nullptr);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

static std::string shellEscapeSingleQuoted(const std::string& s);
static bool launchDetachedShellCommand(const std::string& cmd);
static bool isCommandAvailable(const char* cmd);
static bool isFlatpakAppInstalled(const char* appId);
static std::string slugifyProjectName(const std::string& name);
static std::string resolveProjectPathForOpen(const Project& p, const ListView::CreationDefaults& defaults);

ListView::ListView(ProjectStore& store, CreationDefaults defaults)
    : m_store(store)
    , m_creationDefaults(std::move(defaults)) {
    const std::string& initialDir = m_creationDefaults.cppRoot.empty()
        ? m_creationDefaults.pythonRoot
        : m_creationDefaults.cppRoot;
    if (!initialDir.empty()) {
        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", initialDir.c_str());
    }
}

void ListView::resetCreateForm() {
    m_showCreate = true;
    m_formError.clear();
    m_formName[0] = '\0';
    m_formDesc[0] = '\0';
    m_formCategory[0] = '\0';
    m_formTags[0] = '\0';
    m_formStatus = 0;
    m_createOnDisk = true;
    if (m_createTemplate == static_cast<int>(ProjectTemplate::Cpp) && !m_creationDefaults.cppRoot.empty()) {
        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.cppRoot.c_str());
    } else if (m_createTemplate == static_cast<int>(ProjectTemplate::Python) && !m_creationDefaults.pythonRoot.empty()) {
        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.pythonRoot.c_str());
    }
}

void ListView::requestCreateProject() {
    resetCreateForm();
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void badgeStatus(ProjectStatus s) {
    int idx = static_cast<int>(s);
    ImGui::PushStyleColor(ImGuiCol_Text, kStatusColors[idx]);
    ImGui::TextUnformatted(kStatusLabels[idx]);
    ImGui::PopStyleColor();
}

// ── Main render ──────────────────────────────────────────────────────────────
void ListView::render() {
    // Toolbar
    ImGui::SetNextItemWidth(240.f);
    ImGui::InputTextWithHint("##search", "Buscar projeto...", m_searchBuf, sizeof(m_searchBuf));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::BeginCombo("##filterstatus", m_filterStatus < 0 ? "Todos status" : kStatusLabels[m_filterStatus])) {
        if (ImGui::Selectable("Todos status", m_filterStatus < 0)) m_filterStatus = -1;
        for (int i = 0; i < 5; i++) {
            ImGui::PushStyleColor(ImGuiCol_Text, kStatusColors[i]);
            if (ImGui::Selectable(kStatusLabels[i], m_filterStatus == i)) m_filterStatus = i;
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Novo Projeto")) {
        resetCreateForm();
    }

    ImGui::Separator();

    // Two-column layout: table | detail
    float detailWidth = m_selectedId.empty() ? 0.f : 300.f;
    float tableWidth  = ImGui::GetContentRegionAvail().x - detailWidth - (detailWidth > 0 ? 8.f : 0.f);

    ImGui::BeginChild("##TableRegion", ImVec2(tableWidth, 0.f), false);
    renderTable();
    ImGui::EndChild();

    if (!m_selectedId.empty()) {
        ImGui::SameLine();
        ImGui::BeginChild("##DetailRegion", ImVec2(detailWidth, 0.f), true);
        renderDetailPanel();
        ImGui::EndChild();
    }

    // Modals
    renderCreateModal();
    renderEditModal();
    renderDeleteConfirm();
}

// ── Table ─────────────────────────────────────────────────────────────────────
void ListView::renderTable() {
    std::string srch = m_searchBuf;
    std::transform(srch.begin(), srch.end(), srch.begin(), ::tolower);

    if (ImGui::BeginTable("##projects", 5,
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nome",      ImGuiTableColumnFlags_DefaultSort, 2.0f);
        ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_NoSort,      1.0f);
        ImGui::TableSetupColumn("Categoria", 0,                                 1.2f);
        ImGui::TableSetupColumn("Tags",      ImGuiTableColumnFlags_NoSort,      1.5f);
        ImGui::TableSetupColumn("Criado em", 0,                                 1.0f);
        ImGui::TableHeadersRow();

        for (auto& p : m_store.getAll()) {
            // Filter
            if (m_filterStatus >= 0 && static_cast<int>(p.status) != m_filterStatus)
                continue;
            if (!srch.empty()) {
                std::string low = p.name; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (low.find(srch) == std::string::npos) continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            bool sel = (m_selectedId == p.id);
            if (ImGui::Selectable(("##row_" + p.id).c_str(), sel,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, 20))) {
                m_selectedId = (sel ? "" : p.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(p.name.c_str());

            ImGui::TableSetColumnIndex(1); badgeStatus(p.status);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(p.category.c_str());
            ImGui::TableSetColumnIndex(3);
            for (const auto& tag : p.tags) {
                ImGui::TextDisabled("[%s]", tag.c_str());
                ImGui::SameLine(0, 3);
            }
            ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(p.created_at.c_str());
        }
        ImGui::EndTable();
    }
}

// ── Detail Panel ─────────────────────────────────────────────────────────────
void ListView::renderDetailPanel() {
    auto opt = m_store.findById(m_selectedId);
    if (!opt) { m_selectedId.clear(); return; }
    Project& p = **opt;

    // Close button
    float bw = ImGui::CalcTextSize("X").x + 8.f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - bw);
    if (ImGui::SmallButton("X")) { m_selectedId.clear(); return; }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.f, 1.f));
    ImGui::TextWrapped("%s", p.name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::TextDisabled("Status:"); ImGui::SameLine(); badgeStatus(p.status);
    ImGui::TextDisabled("Categoria:"); ImGui::SameLine(); ImGui::TextUnformatted(p.category.c_str());
    ImGui::TextDisabled("Criado em:"); ImGui::SameLine(); ImGui::TextUnformatted(p.created_at.c_str());

    ImGui::Spacing();
    ImGui::TextDisabled("Descrição:");
    ImGui::TextWrapped("%s", p.description.empty() ? "-" : p.description.c_str());

    ImGui::Spacing();
    if (!p.tags.empty()) {
        ImGui::TextDisabled("Tags:");
        for (const auto& t : p.tags) {
            ImGui::TextDisabled("  - %s", t.c_str());
        }
    }

    if (!p.connections.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Conexões:");
        for (const auto& cid : p.connections) {
            auto copt = m_store.findById(cid);
            std::string label = copt ? (*copt)->name : cid;
            ImGui::TextDisabled("  <-> %s", label.c_str());
        }
    }

    ImGui::Spacing();
    renderOpenInEditorSection(p);
    ImGui::Spacing();
    renderAdrSection(p);
    ImGui::Spacing();
    renderDaiSection(p);
    ImGui::Spacing();
    renderMetricsSection(p);

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Editar")) {
        m_formError.clear();
        snprintf(m_formName,     sizeof(m_formName),     "%s", p.name.c_str());
        snprintf(m_formDesc,     sizeof(m_formDesc),     "%s", p.description.c_str());
        snprintf(m_formCategory, sizeof(m_formCategory), "%s", p.category.c_str());
        // tags as comma list
        std::string tags;
        for (size_t i = 0; i < p.tags.size(); ++i) { if (i) tags += ", "; tags += p.tags[i]; }
        snprintf(m_formTags, sizeof(m_formTags), "%s", tags.c_str());
        m_formStatus = static_cast<int>(p.status);
        m_showEdit = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.f));
    if (ImGui::Button("Excluir")) m_showDeleteConfirm = true;
    ImGui::PopStyleColor();
}

void ListView::renderOpenInEditorSection(const Project& p) {
    if (!ImGui::CollapsingHeader("Abrir no Editor", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static const char* kEditorLabels[] = {
        "VSCodium",
        "VS Code",
        "Sistema (xdg-open)",
        "Custom"
    };

    ImGui::SetNextItemWidth(220.f);
    if (ImGui::BeginCombo("Editor", kEditorLabels[m_editorChoice])) {
        for (int i = 0; i < 4; i++) {
            if (ImGui::Selectable(kEditorLabels[i], m_editorChoice == i)) {
                m_editorChoice = i;
            }
        }
        ImGui::EndCombo();
    }

    if (m_editorChoice == 3) {
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputTextWithHint("##custom_editor", "Comando custom (use {path})", m_customEditorCmd, sizeof(m_customEditorCmd));
    }

    const std::string path = resolveProjectPathForOpen(p, m_creationDefaults);
    const bool hasPath = !path.empty() && fs::exists(path);

    ImGui::TextDisabled("Path: %s", hasPath ? path.c_str() : "(nao disponivel)");
    if (!m_editorMessage.empty()) {
        ImGui::TextWrapped("%s", m_editorMessage.c_str());
    }

    if (!hasPath) ImGui::BeginDisabled();
    if (ImGui::Button("Abrir Projeto")) {
        const std::string escapedPath = shellEscapeSingleQuoted(path);
        std::string cmd;
        std::string effectiveLabel = kEditorLabels[m_editorChoice];

        if (m_editorChoice == 0) {
            if (isCommandAvailable("codium")) {
                cmd = "codium " + escapedPath;
            } else if (isFlatpakAppInstalled("com.vscodium.codium")) {
                cmd = "flatpak run com.vscodium.codium " + escapedPath;
                effectiveLabel = "VSCodium (flatpak)";
            } else if (isCommandAvailable("code")) {
                cmd = "code " + escapedPath;
                effectiveLabel = "VS Code (fallback)";
            } else if (isFlatpakAppInstalled("com.visualstudio.code")) {
                cmd = "flatpak run com.visualstudio.code " + escapedPath;
                effectiveLabel = "VS Code (flatpak fallback)";
            } else {
                cmd = "xdg-open " + escapedPath;
                effectiveLabel = "Sistema (fallback)";
            }
        }
        if (m_editorChoice == 1) {
            if (isCommandAvailable("code")) {
                cmd = "code " + escapedPath;
            } else if (isFlatpakAppInstalled("com.visualstudio.code")) {
                cmd = "flatpak run com.visualstudio.code " + escapedPath;
                effectiveLabel = "VS Code (flatpak)";
            } else if (isCommandAvailable("codium")) {
                cmd = "codium " + escapedPath;
                effectiveLabel = "VSCodium (fallback)";
            } else if (isFlatpakAppInstalled("com.vscodium.codium")) {
                cmd = "flatpak run com.vscodium.codium " + escapedPath;
                effectiveLabel = "VSCodium (flatpak fallback)";
            } else {
                cmd = "xdg-open " + escapedPath;
                effectiveLabel = "Sistema (fallback)";
            }
        }
        if (m_editorChoice == 2) cmd = "xdg-open " + escapedPath;
        if (m_editorChoice == 3) {
            std::string custom = m_customEditorCmd;
            if (custom.empty()) {
                m_editorMessage = "Comando custom vazio.";
                if (!hasPath) ImGui::EndDisabled();
                return;
            }
            const std::string token = "{path}";
            const size_t pos = custom.find(token);
            if (pos != std::string::npos) {
                custom.replace(pos, token.size(), escapedPath);
            } else {
                custom += " " + escapedPath;
            }
            cmd = custom;
        }

        if (launchDetachedShellCommand(cmd)) {
            m_editorMessage = "Abrindo com " + effectiveLabel + ": " + cmd;
        } else {
            m_editorMessage = "Falha ao executar comando do editor. Verifique se o comando existe no PATH.";
        }
    }
    if (!hasPath) ImGui::EndDisabled();
}

void ListView::renderAdrSection(Project& p) {
    if (!ImGui::CollapsingHeader("ADR", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (p.adrs.empty()) {
        ImGui::TextDisabled("Sem ADR registrado.");
    } else {
        for (const auto& adr : p.adrs) {
            ImGui::PushID(adr.id.c_str());
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), "%s", adr.title.c_str());
            ImGui::TextDisabled("Data: %s", adr.created_at.c_str());
            if (!adr.context.empty()) ImGui::TextWrapped("Contexto: %s", adr.context.c_str());
            if (!adr.decision.empty()) ImGui::TextWrapped("Decisao: %s", adr.decision.c_str());
            if (!adr.consequences.empty()) ImGui::TextWrapped("Impacto: %s", adr.consequences.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::InputTextWithHint("##adr_title", "Novo ADR: titulo", m_adrTitle, sizeof(m_adrTitle));
    ImGui::InputTextWithHint("##adr_context", "Contexto", m_adrContext, sizeof(m_adrContext));
    ImGui::InputTextWithHint("##adr_decision", "Decisao", m_adrDecision, sizeof(m_adrDecision));
    ImGui::InputTextWithHint("##adr_conseq", "Impacto/consequencias", m_adrConsequences, sizeof(m_adrConsequences));
    if (ImGui::Button("Adicionar ADR")) {
        if (strlen(m_adrTitle) > 0) {
            Project::AdrEntry adr;
            adr.id = ProjectStore::generateId();
            adr.title = m_adrTitle;
            adr.context = m_adrContext;
            adr.decision = m_adrDecision;
            adr.consequences = m_adrConsequences;
            adr.created_at = nowISO();
            p.adrs.push_back(std::move(adr));
            m_store.update(p);
            m_adrTitle[0] = '\0';
            m_adrContext[0] = '\0';
            m_adrDecision[0] = '\0';
            m_adrConsequences[0] = '\0';
        }
    }
}

void ListView::renderDaiSection(Project& p) {
    if (!ImGui::CollapsingHeader("DAI", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (p.dais.empty()) {
        ImGui::TextDisabled("Sem itens DAI.");
    } else {
        for (auto& dai : p.dais) {
            ImGui::PushID(dai.id.c_str());
            const ImVec4 stColor = dai.closed ? ImVec4(0.35f, 0.75f, 0.35f, 1.f) : ImVec4(0.95f, 0.65f, 0.25f, 1.f);
            ImGui::TextColored(stColor, "[%s] %s", dai.kind.c_str(), dai.title.c_str());
            ImGui::TextDisabled("Owner: %s | Abertura: %s", dai.owner.empty() ? "-" : dai.owner.c_str(),
                                dai.opened_at.empty() ? "-" : dai.opened_at.c_str());
            if (!dai.due_at.empty()) ImGui::TextDisabled("Prazo: %s", dai.due_at.c_str());
            if (!dai.notes.empty()) ImGui::TextWrapped("Notas: %s", dai.notes.c_str());
            if (!dai.closed) {
                if (ImGui::SmallButton("Fechar")) {
                    dai.closed = true;
                    dai.closed_at = nowISO();
                    m_store.update(p);
                }
            } else {
                ImGui::TextDisabled("Fechado em: %s", dai.closed_at.c_str());
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::SetNextItemWidth(140.f);
    if (ImGui::BeginCombo("Tipo", kDaiKinds[m_daiKind])) {
        for (int i = 0; i < 3; i++) {
            if (ImGui::Selectable(kDaiKinds[i], m_daiKind == i)) m_daiKind = i;
        }
        ImGui::EndCombo();
    }
    ImGui::InputTextWithHint("##dai_title", "Novo item DAI", m_daiTitle, sizeof(m_daiTitle));
    ImGui::InputTextWithHint("##dai_owner", "Responsavel", m_daiOwner, sizeof(m_daiOwner));
    ImGui::InputTextWithHint("##dai_due", "Prazo (YYYY-MM-DD)", m_daiDueAt, sizeof(m_daiDueAt));
    ImGui::InputTextWithHint("##dai_notes", "Notas", m_daiNotes, sizeof(m_daiNotes));
    if (ImGui::Button("Adicionar DAI")) {
        if (strlen(m_daiTitle) > 0) {
            Project::DaiEntry dai;
            dai.id = ProjectStore::generateId();
            dai.kind = kDaiKinds[m_daiKind];
            dai.title = m_daiTitle;
            dai.owner = m_daiOwner;
            dai.notes = m_daiNotes;
            dai.opened_at = nowISO();
            dai.due_at = m_daiDueAt;
            p.dais.push_back(std::move(dai));
            m_store.update(p);
            m_daiTitle[0] = '\0';
            m_daiOwner[0] = '\0';
            m_daiDueAt[0] = '\0';
            m_daiNotes[0] = '\0';
            m_daiKind = 1;
        }
    }
}

void ListView::renderMetricsSection(const Project& p) {
    if (!ImGui::CollapsingHeader("Metricas de Fluxo", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto m = m_store.computeProjectFlowMetrics(p);
    ImGui::Text("Lead time: %.1f dias", m.lead_time_days);
    ImGui::Text("Cycle time: %.1f dias", m.cycle_time_days);
    ImGui::Text("Aging: %.1f dias", m.aging_days);
    ImGui::Text("Transicoes: %d", m.status_transitions);
    ImGui::Text("DAI em aberto: %d", m.open_dai_items);
    ImGui::Text("Impedimentos: %d", m.open_impediments);
}

// ── Form helpers ──────────────────────────────────────────────────────────────
static void renderStatusCombo(int& cur) {
    if (ImGui::BeginCombo("Status", kStatusLabels[cur])) {
        for (int i = 0; i < 5; i++) {
            ImGui::PushStyleColor(ImGuiCol_Text, kStatusColors[i]);
            if (ImGui::Selectable(kStatusLabels[i], cur == i)) cur = i;
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
}

static std::vector<std::string> splitTags(const char* s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ',')) {
        t.erase(0, t.find_first_not_of(" \t"));
        t.erase(t.find_last_not_of(" \t") + 1);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

static std::string shellEscapeSingleQuoted(const std::string& s) {
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

static bool launchDetachedShellCommand(const std::string& cmd) {
    const std::string wrapped = cmd + " >/dev/null 2>&1 &";
    return std::system(wrapped.c_str()) == 0;
}

static bool isCommandAvailable(const char* cmd) {
    if (!cmd || !*cmd) return false;
    const std::string probe = std::string("command -v ") + cmd + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

static bool isFlatpakAppInstalled(const char* appId) {
    if (!appId || !*appId) return false;
    if (!isCommandAvailable("flatpak")) return false;
    const std::string probe = std::string("flatpak info ") + appId + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

static std::string slugifyProjectName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    bool lastSep = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            lastSep = false;
        } else if ((std::isspace(c) || c == '-' || c == '_') && !lastSep) {
            out.push_back('_');
            lastSep = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

static std::string resolveProjectPathForOpen(const Project& p, const ListView::CreationDefaults& defaults) {
    if (!p.source_path.empty() && fs::exists(p.source_path)) return p.source_path;

    std::vector<fs::path> roots;
    if (!p.source_root.empty()) roots.emplace_back(p.source_root);
    if (!defaults.cppRoot.empty()) roots.emplace_back(defaults.cppRoot);
    if (!defaults.pythonRoot.empty()) roots.emplace_back(defaults.pythonRoot);

    std::vector<std::string> candidates;
    if (!p.name.empty()) candidates.push_back(p.name);
    const std::string slug = slugifyProjectName(p.name);
    if (!slug.empty() && std::find(candidates.begin(), candidates.end(), slug) == candidates.end()) {
        candidates.push_back(slug);
    }

    for (const auto& root : roots) {
        if (!fs::exists(root) || !fs::is_directory(root)) continue;
        for (const auto& c : candidates) {
            const fs::path path = root / c;
            if (fs::exists(path) && fs::is_directory(path)) return path.string();
        }
    }
    return {};
}

// ── Create Modal ──────────────────────────────────────────────────────────────
void ListView::renderCreateModal() {
    if (!m_showCreate) return;
    static const ProjectTemplate kTemplates[] = {
        ProjectTemplate::None,
        ProjectTemplate::Cpp,
        ProjectTemplate::Python
    };

    ImGui::OpenPopup("Novo Projeto");
    if (ImGui::BeginPopupModal("Novo Projeto", &m_showCreate, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputTextWithHint("##name", "Nome do projeto*", m_formName, sizeof(m_formName));
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputTextWithHint("##cat",  "Categoria",       m_formCategory, sizeof(m_formCategory));
        renderStatusCombo(m_formStatus);
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputTextWithHint("##tags", "Tags (vírgula)",  m_formTags, sizeof(m_formTags));
        ImGui::InputTextMultiline("##desc", m_formDesc, sizeof(m_formDesc), ImVec2(340.f, 100.f));

        const ProjectTemplate selectedTemplate = kTemplates[m_createTemplate];
        ImGui::SetNextItemWidth(200.f);
        if (ImGui::BeginCombo("Template", projectTemplateLabel(selectedTemplate).c_str())) {
            for (int i = 0; i < 3; i++) {
                const bool selected = (m_createTemplate == i);
                if (ImGui::Selectable(projectTemplateLabel(kTemplates[i]).c_str(), selected)) {
                    m_createTemplate = i;
                    if (kTemplates[i] == ProjectTemplate::Cpp && !m_creationDefaults.cppRoot.empty()) {
                        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.cppRoot.c_str());
                    }
                    if (kTemplates[i] == ProjectTemplate::Python && !m_creationDefaults.pythonRoot.empty()) {
                        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.pythonRoot.c_str());
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (selectedTemplate == ProjectTemplate::Cpp || selectedTemplate == ProjectTemplate::Python) {
            ImGui::Checkbox("Criar estrutura base no disco", &m_createOnDisk);
            if (m_createOnDisk) {
                ImGui::SetNextItemWidth(340.f);
                ImGui::InputTextWithHint("##base_dir", "Diretorio base", m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir));
            }
        } else {
            m_createOnDisk = false;
        }

        if (!m_formError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.45f, 1.f));
            ImGui::TextWrapped("%s", m_formError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        bool ok = strlen(m_formName) > 0;
        if (!ok) ImGui::BeginDisabled();
        if (ImGui::Button("Criar", ImVec2(100.f, 0))) {
            Project p;
            p.id          = ProjectStore::generateId();
            p.name        = m_formName;
            p.description = m_formDesc;
            p.status      = static_cast<ProjectStatus>(m_formStatus);
            p.category    = m_formCategory;
            p.tags        = splitTags(m_formTags);
            p.created_at  = nowISO();

            if (selectedTemplate == ProjectTemplate::Cpp) {
                if (p.category.empty()) p.category = "C++";
                if (p.tags.empty()) p.tags = {"C++", "Template"};
            } else if (selectedTemplate == ProjectTemplate::Python) {
                if (p.category.empty()) p.category = "Python";
                if (p.tags.empty()) p.tags = {"Python", "Template"};
            }

            if (m_createOnDisk && selectedTemplate != ProjectTemplate::None) {
                ScaffoldRequest req;
                req.templ = selectedTemplate;
                req.projectName = p.name;
                req.baseDirectory = m_scaffoldBaseDir;
                const auto scaffold = createProjectScaffold(req);
                if (!scaffold.ok) {
                    m_formError = "Falha no scaffold: " + scaffold.message;
                    if (!ok) ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                p.source_path = scaffold.projectDir;
                p.source_root = req.baseDirectory;
            }

            std::string reason;
            if (!m_store.canMoveToStatus(p, p.status, &reason)) {
                m_formError = reason.empty() ? "Regra de fluxo violada." : reason;
                if (!ok) ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            if (p.status != ProjectStatus::Backlog) {
                m_store.recordStatusChange(p, ProjectStatus::Backlog, p.status);
            }
            m_store.add(std::move(p));
            m_showCreate = false;
            m_formError.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!ok) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(100.f, 0))) {
            m_formError.clear();
            m_showCreate = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Edit Modal ────────────────────────────────────────────────────────────────
void ListView::renderEditModal() {
    if (!m_showEdit) return;
    ImGui::OpenPopup("Editar Projeto");
    if (ImGui::BeginPopupModal("Editar Projeto", &m_showEdit, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto opt = m_store.findById(m_selectedId);
        if (!opt) { m_showEdit = false; ImGui::CloseCurrentPopup(); return; }
        Project& p = **opt;

        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("Nome*", m_formName, sizeof(m_formName));
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("Categoria", m_formCategory, sizeof(m_formCategory));
        renderStatusCombo(m_formStatus);
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputTextWithHint("##tags", "Tags (vírgula)", m_formTags, sizeof(m_formTags));
        ImGui::InputTextMultiline("Descrição", m_formDesc, sizeof(m_formDesc), ImVec2(340.f, 100.f));
        if (!m_formError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.45f, 1.f));
            ImGui::TextWrapped("%s", m_formError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        bool ok = strlen(m_formName) > 0;
        if (!ok) ImGui::BeginDisabled();
        if (ImGui::Button("Salvar", ImVec2(100.f, 0))) {
            Project updated = p;
            updated.name        = m_formName;
            updated.description = m_formDesc;
            updated.status      = static_cast<ProjectStatus>(m_formStatus);
            updated.category    = m_formCategory;
            updated.tags        = splitTags(m_formTags);
            if (updated.status != p.status) {
                std::string reason;
                if (!m_store.canMoveToStatus(updated, updated.status, &reason)) {
                    m_formError = reason.empty() ? "Regra de fluxo violada." : reason;
                    if (!ok) ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                m_store.recordStatusChange(updated, p.status, updated.status);
            }
            m_store.update(updated);
            m_showEdit = false;
            m_formError.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!ok) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(100.f, 0))) {
            m_formError.clear();
            m_showEdit = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── Delete Confirm ────────────────────────────────────────────────────────────
void ListView::renderDeleteConfirm() {
    if (!m_showDeleteConfirm) return;
    ImGui::OpenPopup("Confirmar Exclusão");
    if (ImGui::BeginPopupModal("Confirmar Exclusão", &m_showDeleteConfirm, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto opt = m_store.findById(m_selectedId);
        if (opt) {
            ImGui::TextWrapped("Excluir projeto \"%s\"?\nEsta ação não pode ser desfeita.", (*opt)->name.c_str());
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.f));
        if (ImGui::Button("Excluir", ImVec2(120.f, 0))) {
            m_store.remove(m_selectedId);
            m_selectedId.clear();
            m_showDeleteConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120.f, 0))) {
            m_showDeleteConfirm = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace labgestao
