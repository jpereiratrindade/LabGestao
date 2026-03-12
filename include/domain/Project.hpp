#pragma once
#include <string>
#include <vector>

namespace labgestao {

enum class ProjectStatus {
    Backlog,
    Doing,
    Review,
    Done,
    Paused
};

inline std::string statusToString(ProjectStatus s) {
    switch (s) {
        case ProjectStatus::Backlog: return "Backlog";
        case ProjectStatus::Doing:   return "Em Andamento";
        case ProjectStatus::Review:  return "Revisão";
        case ProjectStatus::Done:    return "Concluído";
        case ProjectStatus::Paused:  return "Pausado";
    }
    return "Backlog";
}

inline ProjectStatus statusFromString(const std::string& s) {
    if (s == "Doing" || s == "Em Andamento") return ProjectStatus::Doing;
    if (s == "Review" || s == "Revisão")     return ProjectStatus::Review;
    if (s == "Done"   || s == "Concluído")   return ProjectStatus::Done;
    if (s == "Paused" || s == "Pausado")     return ProjectStatus::Paused;
    return ProjectStatus::Backlog;
}

inline const char* statusKey(ProjectStatus s) {
    switch (s) {
        case ProjectStatus::Backlog: return "Backlog";
        case ProjectStatus::Doing:   return "Doing";
        case ProjectStatus::Review:  return "Review";
        case ProjectStatus::Done:    return "Done";
        case ProjectStatus::Paused:  return "Paused";
    }
    return "Backlog";
}

struct Project {
    std::string id;
    std::string name;
    std::string description;
    ProjectStatus status{ProjectStatus::Backlog};
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> connections; // IDs de projetos relacionados
    std::string created_at;
    float graph_x{0.f};
    float graph_y{0.f};
    float graph_vx{0.f}; // velocidade para layout força-direcionado
    float graph_vy{0.f};
    bool auto_discovered{false};
    std::string source_path;
    std::string source_root;
};

} // namespace labgestao
