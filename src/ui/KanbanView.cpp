#include "ui/KanbanView.hpp"
#include "imgui.h"
#include <algorithm>

namespace labgestao {

static const char* kColLabels[] = { "Backlog", "Em Andamento", "Revisão", "Concluído", "Pausado" };
static const ImVec4 kColHeaderColors[] = {
    {0.35f, 0.25f, 0.60f, 1.f},
    {0.10f, 0.45f, 0.80f, 1.f},
    {0.70f, 0.55f, 0.05f, 1.f},
    {0.10f, 0.65f, 0.30f, 1.f},
    {0.40f, 0.40f, 0.40f, 1.f},
};
static const ImVec4 kColBgColors[] = {
    {0.18f, 0.14f, 0.28f, 1.f},
    {0.10f, 0.18f, 0.30f, 1.f},
    {0.28f, 0.22f, 0.05f, 1.f},
    {0.10f, 0.24f, 0.14f, 1.f},
    {0.18f, 0.18f, 0.18f, 1.f},
};

KanbanView::KanbanView(ProjectStore& store) : m_store(store) {}

void KanbanView::render() {
    renderRuleViolationPopup();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float colWidth = (avail.x - 40.f) / 5.f;

    for (int col = 0; col < 5; col++) {
        if (col > 0) ImGui::SameLine();
        renderColumn(static_cast<ProjectStatus>(col), colWidth);
    }
}

bool KanbanView::tryMoveProjectToStatus(Project& p, ProjectStatus to) {
    if (p.status == to) return true;
    std::string reason;
    if (!m_store.canMoveToStatus(p, to, &reason)) {
        m_ruleViolationMessage = reason.empty() ? "Movimento bloqueado por regra." : reason;
        ImGui::OpenPopup("Regra de Fluxo");
        return false;
    }
    const ProjectStatus from = p.status;
    p.status = to;
    m_store.recordStatusChange(p, from, to);
    m_store.update(p);
    return true;
}

void KanbanView::renderRuleViolationPopup() {
    if (ImGui::BeginPopupModal("Regra de Fluxo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_ruleViolationMessage.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(140.f, 0.f))) {
            m_ruleViolationMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void KanbanView::renderColumn(ProjectStatus col, float colWidth) {
    int idx = static_cast<int>(col);
    std::string colId = "##kancol_" + std::to_string(idx);

    // Count cards
    int count = 0;
    for (const auto& p : m_store.getAll())
        if (p.status == col) count++;

    // Column background
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColBgColors[idx]);
    ImGui::BeginChild(colId.c_str(), ImVec2(colWidth, 0.f), true);
    ImGui::PopStyleColor();

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, kColHeaderColors[idx]);
    ImGui::TextUnformatted(kColLabels[idx]);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", count);
    ImGui::Separator();

    // Drop target for column (empty area)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KANBAN_CARD")) {
            std::string droppedId(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            auto opt = m_store.findById(droppedId);
            if (opt && (*opt)->status != col) {
                tryMoveProjectToStatus(**opt, col);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Cards
    for (auto& p : m_store.getAll()) {
        if (p.status != col) continue;

        std::string cardId = "##card_" + p.id;
        bool sel = (m_selectedId == p.id);

        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            sel ? ImVec4(0.25f, 0.35f, 0.55f, 1.f)
                : ImVec4(0.13f, 0.13f, 0.18f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
        ImGui::BeginChild(cardId.c_str(), ImVec2(colWidth - 16.f, 80.f), true);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // Card content
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 1.f, 1.f));
        ImGui::TextWrapped("%s", p.name.c_str());
        ImGui::PopStyleColor();

        if (!p.category.empty()) ImGui::TextDisabled("%s", p.category.c_str());

        if (!p.tags.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColHeaderColors[idx]);
            for (size_t i = 0; i < p.tags.size() && i < 3; i++) {
                ImGui::TextUnformatted(p.tags[i].c_str());
                if (i + 1 < p.tags.size() && i + 1 < 3) ImGui::SameLine(0, 4);
            }
            ImGui::PopStyleColor();
        }

        // Click = select
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_selectedId = (sel ? "" : p.id);
        }

        // Drag source
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGui::SetNextFrameWantCaptureKeyboard(false);
        }

        ImGui::EndChild();

        // Drag & drop source on the child window rect
        ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
        ImGui::InvisibleButton(("##drag_" + p.id).c_str(), ImGui::GetItemRectSize());
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            std::string pid = p.id;
            ImGui::SetDragDropPayload("KANBAN_CARD", pid.c_str(), pid.size() + 1);
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drop on card = reorder (accept same col drop)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KANBAN_CARD")) {
                std::string droppedId(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                auto opt = m_store.findById(droppedId);
                if (opt && (*opt)->status != col) {
                    tryMoveProjectToStatus(**opt, col);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Spacing();
    }
    ImGui::EndChild();
}

} // namespace labgestao
