#pragma once
#include "Project.hpp"
#include <vector>
#include <string>
#include <optional>

namespace labgestao {

class ProjectStore {
public:
    ProjectStore() = default;

    // CRUD
    void add(Project p);
    void update(const Project& p);
    void remove(const std::string& id);

    std::vector<Project>& getAll();
    const std::vector<Project>& getAll() const;
    std::optional<Project*> findById(const std::string& id);

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
