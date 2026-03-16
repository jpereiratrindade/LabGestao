#pragma once
#include "ui/ListView.hpp"
#include "ui/KanbanView.hpp"
#include "ui/GraphView.hpp"
#include "domain/ProjectStore.hpp"
#include <string>

namespace labgestao {

class AppUI {
public:
    explicit AppUI(
        ProjectStore& store,
        const std::string& dataPath,
        const std::string& settingsPath,
        const std::string& workspaceRoot = {},
        const ListView::CreationDefaults& creationDefaults = {}
    );
    void render();
    bool shouldExit() const { return m_requestExit; }

private:
    void applyTheme();
    void reclassifyKanbanAuto();
    void renderFlowMetricsTab();
    bool exportFlowMetricsCsv(int windowDays, std::string* outputPath, std::string* errorMsg) const;

    ProjectStore& m_store;
    std::string   m_dataPath;
    std::string   m_settingsPath;
    ListView      m_list;
    KanbanView    m_kanban;
    GraphView     m_graph;
    int           m_metricsPeriodIdx{0};
    std::string   m_metricsExportMessage;
    std::string   m_toolsMessage;
    bool          m_focusProjectsTab{false};
    bool          m_requestCreateFromTools{false};
    bool          m_showClearProjectsConfirm{false};
    bool          m_showWorkspaceModal{false};
    bool          m_showWorkspacePickerModal{false};
    char          m_workspaceRootBuf[512]{};
    char          m_workspacePickerPathBuf[512]{};
    bool          m_themeApplied{false};
    bool          m_requestExit{false};
};

} // namespace labgestao
