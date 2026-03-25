#include "ui/ListView.hpp"
#include "app/ApplicationServices.hpp"
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
static bool projectMatchesSearchQuery(const Project& p, const ProjectStore& store, const std::string& rawQuery);

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
    if ((m_createTemplate == static_cast<int>(ProjectTemplate::Cpp) ||
         m_createTemplate == static_cast<int>(ProjectTemplate::GovernedCpp)) &&
        !m_creationDefaults.cppRoot.empty()) {
        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.cppRoot.c_str());
    } else if (m_createTemplate == static_cast<int>(ProjectTemplate::Python) && !m_creationDefaults.pythonRoot.empty()) {
        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.pythonRoot.c_str());
    }
}

void ListView::requestCreateProject() {
    resetCreateForm();
}

std::string ListView::takeLastCreatedProjectId() {
    std::string out = m_lastCreatedProjectId;
    m_lastCreatedProjectId.clear();
    return out;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void badgeStatus(ProjectStatus s) {
    int idx = static_cast<int>(s);
    ImGui::PushStyleColor(ImGuiCol_Text, kStatusColors[idx]);
    ImGui::TextUnformatted(kStatusLabels[idx]);
    ImGui::PopStyleColor();
}

static std::string toLowerAscii(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static bool containsLowered(const std::string& loweredHaystack, const std::string& loweredNeedle) {
    if (loweredNeedle.empty()) return true;
    return loweredHaystack.find(loweredNeedle) != std::string::npos;
}

static std::vector<std::string> splitSearchTerms(const std::string& query) {
    std::vector<std::string> terms;
    std::istringstream ss(query);
    std::string term;
    while (ss >> term) {
        if (!term.empty()) terms.push_back(term);
    }
    return terms;
}

static std::string joinedConnectionNamesLower(const Project& p, const ProjectStore& store) {
    std::string out;
    for (const auto& cid : p.connections) {
        for (const auto& candidate : store.getAll()) {
            if (candidate.id != cid) continue;
            if (!out.empty()) out.push_back(' ');
            out += toLowerAscii(candidate.name);
            break;
        }
    }
    return out;
}

static bool projectMatchesSearchTerm(const Project& p, const ProjectStore& store, const std::string& loweredTerm) {
    const size_t sepPos = loweredTerm.find(':');
    const bool hasField = sepPos != std::string::npos && sepPos > 0 && (sepPos + 1) < loweredTerm.size();

    const std::string lowName = toLowerAscii(p.name);
    const std::string lowDesc = toLowerAscii(p.description);
    const std::string lowCategory = toLowerAscii(p.category);
    const std::string lowPath = toLowerAscii(p.source_path);
    const std::string lowRoot = toLowerAscii(p.source_root);
    const std::string lowStatus = toLowerAscii(statusKey(p.status));
    const std::string lowStatusPt = toLowerAscii(statusToString(p.status));

    std::string lowTags;
    for (const auto& tag : p.tags) {
        if (!lowTags.empty()) lowTags.push_back(' ');
        lowTags += toLowerAscii(tag);
    }

    std::string lowConnectionIds;
    for (const auto& cid : p.connections) {
        if (!lowConnectionIds.empty()) lowConnectionIds.push_back(' ');
        lowConnectionIds += toLowerAscii(cid);
    }
    const std::string lowConnectionNames = joinedConnectionNamesLower(p, store);
    const std::string lowIntegration = p.auto_discovered ? "auto discovered" : "manual";

    if (hasField) {
        const std::string field = loweredTerm.substr(0, sepPos);
        const std::string value = loweredTerm.substr(sepPos + 1);
        if (value.empty()) return true;

        if (field == "name" || field == "nome") return containsLowered(lowName, value);
        if (field == "desc" || field == "descricao") return containsLowered(lowDesc, value);
        if (field == "cat" || field == "categoria") return containsLowered(lowCategory, value);
        if (field == "tag" || field == "tags") return containsLowered(lowTags, value);
        if (field == "path" || field == "source" || field == "src") return containsLowered(lowPath, value);
        if (field == "root" || field == "workspace") return containsLowered(lowRoot, value);
        if (field == "status") return containsLowered(lowStatus, value) || containsLowered(lowStatusPt, value);
        if (field == "conn" || field == "conexao" || field == "rel") {
            return containsLowered(lowConnectionIds, value) || containsLowered(lowConnectionNames, value);
        }
        if (field == "integracao" || field == "integration" || field == "auto") {
            return containsLowered(lowIntegration, value);
        }

        return false;
    }

    std::string globalHaystack;
    globalHaystack.reserve(lowName.size() + lowDesc.size() + lowCategory.size() + lowTags.size() +
                           lowPath.size() + lowRoot.size() + lowStatus.size() + lowStatusPt.size() +
                           lowConnectionIds.size() + lowConnectionNames.size() + lowIntegration.size() + 16);
    globalHaystack += lowName + " " + lowDesc + " " + lowCategory + " " + lowTags + " " +
                     lowPath + " " + lowRoot + " " + lowStatus + " " + lowStatusPt + " " +
                     lowConnectionIds + " " + lowConnectionNames + " " + lowIntegration;
    return containsLowered(globalHaystack, loweredTerm);
}

static bool projectMatchesSearchQuery(const Project& p, const ProjectStore& store, const std::string& rawQuery) {
    const std::string query = toLowerAscii(rawQuery);
    const auto terms = splitSearchTerms(query);
    if (terms.empty()) return true;

    for (const auto& term : terms) {
        if (!projectMatchesSearchTerm(p, store, term)) return false;
    }
    return true;
}

// ── Main render ──────────────────────────────────────────────────────────────
void ListView::render() {
    // Toolbar
    ImGui::SetNextItemWidth(240.f);
    ImGui::InputTextWithHint("##search", "Buscar (nome tag: path: conn: status:)...", m_searchBuf, sizeof(m_searchBuf));
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
    auto joinTags = [](const Project& p) {
        std::string out;
        for (std::size_t i = 0; i < p.tags.size(); i++) {
            if (i) out += "|";
            out += p.tags[i];
        }
        return toLowerAscii(out);
    };

    if (ImGui::BeginTable("##projects", 5,
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nome",      ImGuiTableColumnFlags_DefaultSort, 2.0f);
        ImGui::TableSetupColumn("Status",    0,                                 1.0f);
        ImGui::TableSetupColumn("Categoria", 0,                                 1.2f);
        ImGui::TableSetupColumn("Tags",      0,                                 1.5f);
        ImGui::TableSetupColumn("Criado em", 0,                                 1.0f);
        ImGui::TableHeadersRow();

        std::vector<Project*> rows;
        rows.reserve(m_store.getAll().size());
        for (auto& p : m_store.getAll()) {
            if (m_filterStatus >= 0 && static_cast<int>(p.status) != m_filterStatus)
                continue;
            if (!projectMatchesSearchQuery(p, m_store, m_searchBuf)) continue;
            rows.push_back(&p);
        }

        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs spec = sortSpecs->Specs[0];
                std::stable_sort(rows.begin(), rows.end(), [&](const Project* a, const Project* b) {
                    int cmp = 0;
                    if (spec.ColumnIndex == 0) cmp = a->name.compare(b->name);
                    if (spec.ColumnIndex == 1) cmp = static_cast<int>(a->status) - static_cast<int>(b->status);
                    if (spec.ColumnIndex == 2) cmp = a->category.compare(b->category);
                    if (spec.ColumnIndex == 3) cmp = joinTags(*a).compare(joinTags(*b));
                    if (spec.ColumnIndex == 4) cmp = a->created_at.compare(b->created_at);
                    if (cmp == 0) cmp = a->name.compare(b->name);
                    if (spec.SortDirection == ImGuiSortDirection_Descending) cmp = -cmp;
                    return cmp < 0;
                });
            }
        }

        for (Project* row : rows) {
            Project& p = *row;
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
    renderGovernanceProfileSection(p);
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

void ListView::renderGovernanceProfileSection(const Project& p) {
    if (!ImGui::CollapsingHeader("Governance Profile", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static std::string s_lastAutoRefreshKey;
    const std::string refreshKey = p.id + "|" + p.source_path;
    if (!p.source_path.empty() &&
        (!p.governance_profile.analyzed || p.governance_profile.analyzed_path != p.source_path) &&
        s_lastAutoRefreshKey != refreshKey) {
        labgestao::application::refreshProjectGovernance(m_store, p.id);
        s_lastAutoRefreshKey = refreshKey;
    }

    auto refreshed = m_store.findById(p.id);
    if (!refreshed) return;
    const Project& current = **refreshed;
    const auto& profile = current.governance_profile;
    const ImVec4 maturityColor =
        profile.maturity_score >= 80 ? ImVec4(0.25f, 0.80f, 0.45f, 1.f) :
        profile.maturity_score >= 55 ? ImVec4(0.90f, 0.72f, 0.18f, 1.f) :
                                       ImVec4(0.92f, 0.40f, 0.32f, 1.f);

    ImGui::TextDisabled("Path analisado:");
    ImGui::TextWrapped("%s", profile.path_available ? profile.analyzed_path.c_str() : "(nao disponivel)");
    ImGui::Spacing();
    ImGui::Text("Maturidade: %d/100", profile.maturity_score);
    ImGui::Text("Vibe Risk: %d/100", profile.vibe_risk);
    ImGui::Text("Governanca operacional: %d/8", profile.governance_signals);
    ImGui::TextColored(maturityColor, "%s", profile.maturity_label.empty() ? "Sem analise" : profile.maturity_label.c_str());
    if (ImGui::Button("Atualizar Governance Profile")) {
        labgestao::application::refreshProjectGovernance(m_store, current.id);
        s_lastAutoRefreshKey.clear();
    }
    if (!profile.analyzed) {
        ImGui::TextDisabled("Nenhuma analise persistida ainda.");
    } else if (!profile.analyzed_at.empty()) {
        ImGui::TextDisabled("Atualizado em: %s", profile.analyzed_at.c_str());
    }

    ImGui::Separator();
    ImGui::TextDisabled("Sinais principais");
    ImGui::BulletText("ADR: %s", profile.has_adr ? "OK" : "-");
    ImGui::BulletText("DDD: %s", profile.has_ddd ? "OK" : "-");
    ImGui::BulletText("DAI: %s", profile.has_dai ? "OK" : "-");
    ImGui::BulletText("Policies: %s", profile.has_policies ? "OK" : "-");
    ImGui::BulletText("Tool Contracts: %s", profile.has_tool_contracts ? "OK" : "-");
    ImGui::BulletText("Approval Matrix: %s", profile.has_approval_policy ? "OK" : "-");
    ImGui::BulletText("Audit/Evidence: %s", profile.has_audit_evidence ? "OK" : "-");

    ImGui::Separator();
    ImGui::TextDisabled("Leitura sintetica");
    if (profile.maturity_score >= 80 && profile.vibe_risk < 30) {
        ImGui::TextWrapped("Projeto com governanca operacional robusta. A prioridade aqui e manter coerencia entre politicas, contratos e evidencias.");
    } else if (profile.maturity_score >= 55) {
        ImGui::TextWrapped("Projeto com base documental razoavel, mas ainda com lacunas de enforcement ou auditabilidade.");
    } else {
        ImGui::TextWrapped("Projeto ainda exposto a improviso institucional. A recomendacao e fortalecer fronteiras, aprovacao e trilha de evidencia.");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Proximos passos");
    for (std::size_t i = 0; i < profile.next_actions.size(); ++i) {
        const std::string line = std::to_string(i + 1) + ". " + profile.next_actions[i];
        ImGui::TextWrapped("%s", line.c_str());
    }
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

static std::string trimCopy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static std::string previewScaffoldPath(const char* baseDir, const char* projectName) {
    const std::string base = trimCopy(baseDir ? baseDir : "");
    const std::string name = trimCopy(projectName ? projectName : "");
    if (base.empty() || name.empty()) return {};

    const std::string slug = slugifyProjectName(name);
    if (slug.empty()) return {};

    return (fs::path(base) / slug).string();
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
        ProjectTemplate::Python,
        ProjectTemplate::GovernedCpp
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
        ImGui::SetNextItemWidth(240.f);
        if (ImGui::BeginCombo("Tipo de projeto / bootstrap", projectTemplateLabel(selectedTemplate).c_str())) {
            for (int i = 0; i < static_cast<int>(sizeof(kTemplates) / sizeof(kTemplates[0])); i++) {
                const bool selected = (m_createTemplate == i);
                if (ImGui::Selectable(projectTemplateLabel(kTemplates[i]).c_str(), selected)) {
                    m_createTemplate = i;
                    if ((kTemplates[i] == ProjectTemplate::Cpp ||
                         kTemplates[i] == ProjectTemplate::GovernedCpp) &&
                        !m_creationDefaults.cppRoot.empty()) {
                        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.cppRoot.c_str());
                    }
                    if (kTemplates[i] == ProjectTemplate::Python && !m_creationDefaults.pythonRoot.empty()) {
                        std::snprintf(m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir), "%s", m_creationDefaults.pythonRoot.c_str());
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (selectedTemplate == ProjectTemplate::Cpp ||
            selectedTemplate == ProjectTemplate::Python ||
            selectedTemplate == ProjectTemplate::GovernedCpp) {
            ImGui::Checkbox("Criar estrutura base no disco", &m_createOnDisk);
            if (m_createOnDisk) {
                ImGui::SetNextItemWidth(340.f);
                ImGui::InputTextWithHint("##base_dir", "Diretorio base*", m_scaffoldBaseDir, sizeof(m_scaffoldBaseDir));
                const std::string previewPath = previewScaffoldPath(m_scaffoldBaseDir, m_formName);
                if (trimCopy(m_scaffoldBaseDir).empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.45f, 1.f));
                    ImGui::TextUnformatted("Informe um diretorio base para criar a estrutura no disco.");
                    ImGui::PopStyleColor();
                } else if (!previewPath.empty()) {
                    ImGui::TextDisabled("Diretorio final previsto: %s", previewPath.c_str());
                }
                if (selectedTemplate == ProjectTemplate::GovernedCpp) {
                    ImGui::TextDisabled("Usa o script canônico init_ai_governance.sh para gerar o bootstrap.");
                }
            }
        } else {
            m_createOnDisk = false;
        }

        Project draft;
        draft.id = "__draft__";
        draft.name = m_formName;
        if (m_createOnDisk && selectedTemplate != ProjectTemplate::None) {
            draft.source_path = previewScaffoldPath(m_scaffoldBaseDir, m_formName);
        }

        const auto duplicateSourceReason = m_store.duplicateSourcePathReason(draft);
        const auto duplicateNameReason = m_store.duplicateNormalizedNameReason(draft);

        ImGui::Checkbox("Abrir aba de planejamento apos criar", &m_openPlanningAfterCreate);
        ImGui::TextDisabled("Status inicial recomendado: Backlog. A mudanca de status continua manual depois da criacao.");
        if (duplicateNameReason.has_value()) {
            ImGui::TextDisabled("Aviso: %s", duplicateNameReason->c_str());
        }
        if (duplicateSourceReason.has_value()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.45f, 1.f));
            ImGui::TextWrapped("Conflito: %s", duplicateSourceReason->c_str());
            ImGui::PopStyleColor();
        }

        if (!m_formError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.45f, 1.f));
            ImGui::TextWrapped("%s", m_formError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        const bool hasName = strlen(m_formName) > 0;
        const bool needsBaseDir = m_createOnDisk && selectedTemplate != ProjectTemplate::None;
        const bool hasBaseDir = !trimCopy(m_scaffoldBaseDir).empty();
        const bool hasDuplicateSource = duplicateSourceReason.has_value();
        const bool canCreate = hasName && (!needsBaseDir || hasBaseDir) && !hasDuplicateSource;
        if (!canCreate) ImGui::BeginDisabled();
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
            } else if (selectedTemplate == ProjectTemplate::GovernedCpp) {
                if (p.category.empty()) p.category = "C++ Governado";
                if (p.tags.empty()) p.tags = {"C++", "Governanca", "Template"};
            }

            if (duplicateSourceReason.has_value()) {
                m_formError = "Conflito de cadastro: " + *duplicateSourceReason;
                if (!canCreate) ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }

            if (m_createOnDisk && selectedTemplate != ProjectTemplate::None) {
                ScaffoldRequest req;
                req.templ = selectedTemplate;
                req.projectName = p.name;
                req.baseDirectory = m_scaffoldBaseDir;
                const auto scaffold = createProjectScaffold(req);
                if (!scaffold.ok) {
                    m_formError = "Falha no scaffold: " + scaffold.message;
                    if (!canCreate) ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                p.source_path = scaffold.projectDir;
                p.source_root = req.baseDirectory;
            }
            if (!p.source_path.empty()) {
                p.governance_profile = labgestao::application::analyzeProjectGovernance(p);
            }

            std::string reason;
            if (!m_store.canMoveToStatus(p, p.status, &reason)) {
                m_formError = reason.empty() ? "Regra de fluxo violada." : reason;
                if (!canCreate) ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            if (p.status != ProjectStatus::Backlog) {
                m_store.recordStatusChange(p, ProjectStatus::Backlog, p.status);
            }
            const std::string createdId = p.id;
            m_store.add(std::move(p));
            m_selectedId = createdId;
            if (m_openPlanningAfterCreate) {
                m_lastCreatedProjectId = createdId;
            }
            m_showCreate = false;
            m_formError.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!canCreate) ImGui::EndDisabled();
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
            if (const auto conflict = m_store.duplicateSourcePathReason(updated); conflict.has_value()) {
                m_formError = "Conflito de cadastro: " + *conflict;
                if (!ok) ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
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
