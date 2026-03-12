#include "domain/ProjectStore.hpp"
#include "json.hpp"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace labgestao {

// ── ID generation ───────────────────────────────────────────────────────────
std::string ProjectStore::generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << dist(rng);
    return oss.str();
}

// ── CRUD ─────────────────────────────────────────────────────────────────────
void ProjectStore::add(Project p) {
    if (p.id.empty()) p.id = generateId();
    m_projects.push_back(std::move(p));
    m_dirty = true;
}

void ProjectStore::update(const Project& p) {
    for (auto& proj : m_projects) {
        if (proj.id == p.id) {
            proj = p;
            m_dirty = true;
            return;
        }
    }
}

void ProjectStore::remove(const std::string& id) {
    auto it = std::remove_if(m_projects.begin(), m_projects.end(),
        [&id](const Project& p){ return p.id == id; });
    if (it != m_projects.end()) {
        m_projects.erase(it, m_projects.end());
        m_dirty = true;
    }
}

std::vector<Project>& ProjectStore::getAll() { return m_projects; }
const std::vector<Project>& ProjectStore::getAll() const { return m_projects; }

std::optional<Project*> ProjectStore::findById(const std::string& id) {
    for (auto& p : m_projects) {
        if (p.id == id) return &p;
    }
    return std::nullopt;
}

// ── JSON Helpers ─────────────────────────────────────────────────────────────
static json projectToJson(const Project& p) {
    json j;
    j["id"]          = p.id;
    j["name"]        = p.name;
    j["description"] = p.description;
    j["status"]      = statusKey(p.status);
    j["category"]    = p.category;
    j["tags"]        = p.tags;
    j["connections"] = p.connections;
    j["created_at"]  = p.created_at;
    j["graph_x"]     = p.graph_x;
    j["graph_y"]     = p.graph_y;
    return j;
}

static Project projectFromJson(const json& j) {
    Project p;
    p.id          = j.value("id",          "");
    p.name        = j.value("name",        "");
    p.description = j.value("description", "");
    p.status      = statusFromString(j.value("status", "Backlog"));
    p.category    = j.value("category",    "");
    p.tags        = j.value("tags",        std::vector<std::string>{});
    p.connections = j.value("connections", std::vector<std::string>{});
    p.created_at  = j.value("created_at",  "");
    p.graph_x     = j.value("graph_x",     0.f);
    p.graph_y     = j.value("graph_y",     0.f);
    return p;
}

// ── Persistência ─────────────────────────────────────────────────────────────
bool ProjectStore::loadFromJson(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json data = json::parse(f);
        m_projects.clear();
        for (const auto& j : data) {
            m_projects.push_back(projectFromJson(j));
        }
        m_dirty = false;
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectStore::saveToJson(const std::string& path) const {
    json data = json::array();
    for (const auto& p : m_projects)
        data.push_back(projectToJson(p));
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << data.dump(2);
    return f.good();
}

} // namespace labgestao
