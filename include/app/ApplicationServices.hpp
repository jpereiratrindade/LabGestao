#pragma once

#include "domain/ProjectStore.hpp"

#include <string>
#include <vector>

namespace labgestao::application {

struct MonitoredRoot {
    std::string path;
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> markerFiles;
};

struct ReclassifyKanbanAutoResult {
    int changed{0};
    int unchanged{0};
    int skipped{0};
};

Project::GovernanceProfile analyzeProjectGovernance(const Project& project);
bool refreshProjectGovernance(ProjectStore& store, const std::string& projectId);
int refreshAllProjectGovernance(ProjectStore& store);
void syncAutoDiscoveredProjects(ProjectStore& store, const std::vector<MonitoredRoot>& roots);
ReclassifyKanbanAutoResult reclassifyKanbanAuto(ProjectStore& store);

} // namespace labgestao::application
