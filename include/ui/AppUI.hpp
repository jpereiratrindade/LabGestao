#pragma once
#include "ui/ListView.hpp"
#include "ui/KanbanView.hpp"
#include "ui/GraphView.hpp"
#include "domain/ProjectStore.hpp"
#include <string>

namespace labgestao {

class AppUI {
public:
    explicit AppUI(ProjectStore& store, const std::string& dataPath);
    void render();
    bool shouldExit() const { return m_requestExit; }

private:
    void applyTheme();
    void renderFlowMetricsTab();
    bool exportFlowMetricsCsv(int windowDays, std::string* outputPath, std::string* errorMsg) const;

    ProjectStore& m_store;
    std::string   m_dataPath;
    ListView      m_list;
    KanbanView    m_kanban;
    GraphView     m_graph;
    int           m_metricsPeriodIdx{0};
    std::string   m_metricsExportMessage;
    bool          m_themeApplied{false};
    bool          m_requestExit{false};
};

} // namespace labgestao
