#pragma once
#include "ui/ListView.hpp"
#include "ui/KanbanView.hpp"
#include "ui/GraphView.hpp"
#include "domain/ProjectStore.hpp"
#include <string>
#include <vector>

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
    struct RepoInventoryEntry {
        std::string name;
        std::string path;
        std::string buildSystem;
        int scoreOperational{0}; // 0..100
        int scoreMaturity{0};    // 0..100
        int scoreReliability{0}; // 0..100
        int scoreTotal{0};       // 0..100 (50/30/20)
        int detectedArtifacts{0};
        bool hasReadme{false};
        bool hasCi{false};
        bool hasTests{false};
        bool hasGitignore{false};
        bool hasLicense{false};
        bool hasAdr{false};
        bool hasDdd{false};
        bool hasDai{false};
        bool hasGovernance{false};
        int governanceSignals{0}; // 0..4
        bool hasAsanUbsan{false};
        bool hasLeakCheck{false};
        bool hasStaticAnalysis{false};
        bool hasStrictWarnings{false};
        bool hasComplexityGuard{false};
        bool hasCycleGuard{false};
        bool hasFormatLint{false};
        bool integratedWithLabGestao{false};
    };

    void applyTheme();
    void reclassifyKanbanAuto();
    void renderFlowMetricsTab();
    void renderInventoryTab();
    void scanWorkspaceInventory();
    std::vector<const RepoInventoryEntry*> topCriticalRepos(std::size_t limit) const;
    bool loadLatestInventorySnapshotMetrics(float* avgTotal, std::string* generatedAt, std::string* errorMsg) const;
    bool exportExecutiveReport(std::string* outputPath, std::string* errorMsg) const;
    std::vector<std::string> generateActionPlanForRepo(const RepoInventoryEntry& repo) const;
    bool exportInventoryCsv(std::string* outputPath, std::string* errorMsg) const;
    bool exportInventoryJson(std::string* outputPath, std::string* errorMsg) const;
    bool exportFlowMetricsCsv(int windowDays, std::string* outputPath, std::string* errorMsg) const;

    ProjectStore& m_store;
    std::string   m_dataPath;
    std::string   m_settingsPath;
    ListView      m_list;
    KanbanView    m_kanban;
    GraphView     m_graph;
    int           m_metricsPeriodIdx{0};
    std::string   m_metricsExportMessage;
    std::string   m_metricsActionMessage;
    std::vector<std::string> m_metricsTopCriticalPlan;
    std::string   m_toolsMessage;
    bool          m_focusProjectsTab{false};
    bool          m_requestCreateFromTools{false};
    bool          m_showClearProjectsConfirm{false};
    bool          m_showWorkspaceModal{false};
    bool          m_showWorkspacePickerModal{false};
    char          m_workspaceRootBuf[512]{};
    char          m_workspacePickerPathBuf[512]{};
    char          m_repoSearchBuf[256]{};
    int           m_repoMinScore{0};
    bool          m_repoOnlyIntegrated{false};
    std::vector<RepoInventoryEntry> m_repoInventory;
    std::string   m_repoInventoryRoot;
    std::string   m_repoInventoryMessage;
    std::string   m_repoSelectedPath;
    std::string   m_repoActionPlanPath;
    std::vector<std::string> m_repoActionPlan;
    bool          m_themeApplied{false};
    bool          m_requestExit{false};
};

} // namespace labgestao
