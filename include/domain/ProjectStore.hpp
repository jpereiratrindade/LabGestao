#pragma once
#include "Project.hpp"
#include <vector>
#include <string>
#include <optional>

namespace labgestao {

class ProjectStore {
public:
    struct ProjectFlowMetrics {
        float lead_time_days{0.f};
        float cycle_time_days{0.f};
        float aging_days{0.f};
        int status_transitions{0};
        int open_dai_items{0};
        int open_impediments{0};
    };

    struct GlobalFlowMetrics {
        int total_projects{0};
        int wip_projects{0};
        int done_projects{0};
        int throughput_in_window{0};
        int throughput_window_days{7};
        float avg_aging_days{0.f};
        int open_impediments{0};
    };

    ProjectStore() = default;

    // CRUD
    void add(Project p);
    void update(const Project& p);
    void remove(const std::string& id);
    void clear();

    std::vector<Project>& getAll();
    const std::vector<Project>& getAll() const;
    std::optional<Project*> findById(const std::string& id);

    void recordStatusChange(Project& p, ProjectStatus from, ProjectStatus to, const std::string& movedAt = "");
    bool canMoveToStatus(const Project& p, ProjectStatus to, std::string* reason = nullptr) const;
    ProjectFlowMetrics computeProjectFlowMetrics(const Project& p, const std::string& todayIso = "") const;
    GlobalFlowMetrics computeGlobalFlowMetrics(const std::string& todayIso = "", int throughputWindowDays = 7) const;

    // Persistência
    bool loadFromJson(const std::string& path);
    bool saveToJson(const std::string& path) const;

    bool isDirty() const { return m_dirty; }
    void clearDirty()    { m_dirty = false; }

    // ID único
    static std::string generateId();

private:
    std::vector<Project> m_projects;
    bool m_dirty{false};
};

} // namespace labgestao
