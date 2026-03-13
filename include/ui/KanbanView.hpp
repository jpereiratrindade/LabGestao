#pragma once
#include "domain/ProjectStore.hpp"

namespace labgestao {

class KanbanView {
public:
    explicit KanbanView(ProjectStore& store);
    void render();

private:
    void renderColumn(ProjectStatus col, float colWidth);
    bool tryMoveProjectToStatus(Project& p, ProjectStatus to);
    void renderRuleViolationPopup();

    ProjectStore& m_store;
    std::string m_dragPayloadId;
    std::string m_selectedId;
    std::string m_ruleViolationMessage;
};

} // namespace labgestao
