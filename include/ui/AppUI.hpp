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
        int scoreReliability{0};     // 0..100 (Config)
        int scoreReliabilityExec{0}; // 0..100 (Exec)
        int scoreEngineeringDiscipline{0}; // 0..100
        int scoreVibeRisk{0};              // 0..100
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
        bool hasPolicies{false};
        bool hasToolContracts{false};
        bool hasApprovalPolicy{false};
        bool hasAuditEvidence{false};
        int governanceSignals{0}; // 0..8
        bool hasAsanUbsan{false};
        bool hasLeakCheck{false};
        bool hasStaticAnalysis{false};
        bool hasStrictWarnings{false};
        bool hasComplexityGuard{false};
        bool hasCycleGuard{false};
        bool hasFormatLint{false};
        bool hasAsanUbsanExec{false};
        bool hasLeakCheckExec{false};
        bool hasStaticAnalysisExec{false};
        bool hasStrictWarningsExec{false};
        bool hasComplexityGuardExec{false};
        bool hasCycleGuardExec{false};
        bool hasFormatLintExec{false};
        bool integratedWithLabGestao{false};
    };

    void applyTheme();
    void reclassifyKanbanAuto();
    void refreshGovernanceProfiles();
    void processPendingGovernanceRefresh();
    void processPendingInventoryScan();
    void renderPlanningTab();
    void renderFlowMetricsTab();
    void renderInventoryTab();
    RepoInventoryEntry buildRepoInventoryEntry(const std::string& repoPath) const;
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
    bool          m_focusPlanningTab{false};
    std::string   m_planningProjectId;
    bool          m_requestCreateFromTools{false};
    bool          m_showClearProjectsConfirm{false};
    bool          m_showWorkspaceModal{false};
    bool          m_showWorkspacePickerModal{false};
    char          m_workspaceRootBuf[512]{};
    char          m_workspacePickerPathBuf[512]{};
    char          m_repoSearchBuf[256]{};
    int           m_repoMinScore{0};
    bool          m_repoOnlyIntegrated{false};
    int           m_repoSortMode{0}; // 0=Total, 1=Disciplina, 2=Vibe, 3=Operacional, 4=Maturidade, 5=ConfiabCfg, 6=ConfiabExec
    bool          m_repoSortDesc{true};
    std::vector<RepoInventoryEntry> m_repoInventory;
    bool          m_repoInventoryScanRunning{false};
    std::vector<std::string> m_pendingRepoInventoryCandidates;
    std::vector<RepoInventoryEntry> m_pendingRepoInventoryEntries;
    std::size_t   m_pendingRepoInventoryIndex{0};
    std::string   m_repoInventoryRoot;
    std::string   m_repoInventoryMessage;
    std::string   m_repoSelectedPath;
    std::string   m_repoActionPlanPath;
    std::vector<std::string> m_repoActionPlan;
    bool          m_governanceRefreshRunning{false};
    std::vector<std::string> m_pendingGovernanceProjectIds;
    std::size_t   m_pendingGovernanceIndex{0};
    int           m_pendingGovernanceChanged{0};
    bool          m_themeApplied{false};
    bool          m_requestExit{false};
};

} // namespace labgestao
