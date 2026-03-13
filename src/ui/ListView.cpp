#include "ui/ListView.hpp"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <chrono>
#include <ctime>

namespace labgestao {

static const char* kStatusLabels[] = { "Backlog", "Em Andamento", "Revisão", "Concluído", "Pausado" };
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

ListView::ListView(ProjectStore& store) : m_store(store) {}

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
        m_showCreate = true;
        m_formError.clear();
        m_formName[0]     = '\0';
        m_formDesc[0]     = '\0';
        m_formCategory[0] = '\0';
        m_formTags[0]     = '\0';
        m_formStatus      = 0;
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

// ── Create Modal ──────────────────────────────────────────────────────────────
void ListView::renderCreateModal() {
    if (!m_showCreate) return;
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
