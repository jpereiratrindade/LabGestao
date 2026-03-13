#pragma once

#include <string>

namespace labgestao {

enum class ProjectTemplate {
    None = 0,
    Cpp,
    Python
};

struct ScaffoldRequest {
    ProjectTemplate templ{ProjectTemplate::None};
    std::string projectName;
    std::string baseDirectory;
};

struct ScaffoldResult {
    bool ok{false};
    std::string projectDir;
    std::string message;
};

ScaffoldResult createProjectScaffold(const ScaffoldRequest& req);
std::string projectTemplateLabel(ProjectTemplate templ);

} // namespace labgestao

