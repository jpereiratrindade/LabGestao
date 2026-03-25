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
    struct AdrEntry {
        std::string id;
        std::string title;
        std::string context;
        std::string decision;
        std::string consequences;
        std::string created_at;
    };

    struct DaiEntry {
        std::string id;
        std::string kind; // "Decision", "Action", "Impediment"
        std::string title;
        std::string owner;
        std::string notes;
        std::string opened_at;
        std::string due_at;
        bool closed{false};
        std::string closed_at;
    };

    struct StatusTransition {
        std::string from;
        std::string to;
        std::string moved_at;
    };

    struct GovernanceProfile {
        bool analyzed{false};
        std::string analyzed_at;
        std::string analyzed_path;
        bool path_available{false};
        bool has_adr{false};
        bool has_ddd{false};
        bool has_dai{false};
        bool has_policies{false};
        bool has_tool_contracts{false};
        bool has_approval_policy{false};
        bool has_audit_evidence{false};
        int governance_signals{0}; // 0..8
        int maturity_score{0};     // 0..100
        int vibe_risk{0};          // 0..100
        std::string maturity_label;
        std::vector<std::string> next_actions;
    };

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

    std::vector<AdrEntry> adrs;
    std::vector<DaiEntry> dais;
    std::vector<StatusTransition> status_history;
    GovernanceProfile governance_profile;
};

} // namespace labgestao
