#include "domain/ProjectStore.hpp"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace labgestao {

static std::string todayISO() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf {};
    localtime_r(&t, &tm_buf);
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

static int daysSinceEpoch(const std::string& isoDate) {
    if (isoDate.size() < 10) return -1;
    std::tm tm_buf {};
    std::istringstream ss(isoDate.substr(0, 10));
    ss >> std::get_time(&tm_buf, "%Y-%m-%d");
    if (ss.fail()) return -1;
    tm_buf.tm_hour = 12;
    tm_buf.tm_isdst = -1;
    std::time_t tt = std::mktime(&tm_buf);
    if (tt < 0) return -1;
    return static_cast<int>(tt / 86400);
}

static float daysDiff(const std::string& from, const std::string& to) {
    const int a = daysSinceEpoch(from);
    const int b = daysSinceEpoch(to);
    if (a < 0 || b < 0) return 0.f;
    return static_cast<float>(b - a);
}

static std::string trim(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

static std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::string normalizeProjectName(const std::string& value) {
    return lowerAscii(trim(value));
}

static std::string canonicalSourcePath(const std::string& rawPath) {
    if (rawPath.empty()) return {};
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(rawPath), ec);
    if (!ec) return canon.string();
    const fs::path normalized = fs::path(rawPath).lexically_normal();
    return normalized.string();
}

static bool sameCanonicalSourcePath(const std::string& lhs, const std::string& rhs) {
    const std::string a = canonicalSourcePath(lhs);
    const std::string b = canonicalSourcePath(rhs);
    if (a.empty() || b.empty()) return false;
    return a == b;
}

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
    if (duplicateSourcePathReason(p).has_value()) {
        return;
    }
    if (p.id.empty()) p.id = generateId();
    m_projects.push_back(std::move(p));
    m_dirty = true;
}

void ProjectStore::update(const Project& p) {
    for (auto& proj : m_projects) {
        if (proj.id == p.id) {
            if (const auto conflict = duplicateSourcePathReason(p); conflict.has_value()) {
                return;
            }
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

void ProjectStore::clear() {
    if (m_projects.empty()) return;
    m_projects.clear();
    m_dirty = true;
}

std::vector<Project>& ProjectStore::getAll() { return m_projects; }
const std::vector<Project>& ProjectStore::getAll() const { return m_projects; }

std::optional<Project*> ProjectStore::findById(const std::string& id) {
    for (auto& p : m_projects) {
        if (p.id == id) return &p;
    }
    return std::nullopt;
}

std::optional<std::string> ProjectStore::duplicateSourcePathReason(const Project& candidate) const {
    if (candidate.source_path.empty()) return std::nullopt;

    const std::string candidatePath = canonicalSourcePath(candidate.source_path);
    if (candidatePath.empty()) return std::nullopt;

    for (const auto& existing : m_projects) {
        if (!existing.id.empty() && existing.id == candidate.id) continue;
        if (existing.source_path.empty()) continue;
        if (!sameCanonicalSourcePath(existing.source_path, candidate.source_path)) continue;

        std::ostringstream oss;
        oss << "source_path duplicado com projeto '" << existing.name << "' (" << existing.id << ")";
        return oss.str();
    }
    return std::nullopt;
}

std::optional<std::string> ProjectStore::duplicateNormalizedNameReason(const Project& candidate) const {
    const std::string candidateName = normalizeProjectName(candidate.name);
    if (candidateName.empty()) return std::nullopt;

    for (const auto& existing : m_projects) {
        if (!existing.id.empty() && existing.id == candidate.id) continue;
        if (normalizeProjectName(existing.name) != candidateName) continue;

        std::ostringstream oss;
        oss << "nome duplicado com projeto '" << existing.name << "' (" << existing.id << ")";
        return oss.str();
    }
    return std::nullopt;
}

// ── JSON Helpers ─────────────────────────────────────────────────────────────
static json projectToJson(const Project& p) {
    auto adrToJson = [](const Project::AdrEntry& adr) {
        json j;
        j["id"] = adr.id;
        j["title"] = adr.title;
        j["context"] = adr.context;
        j["decision"] = adr.decision;
        j["consequences"] = adr.consequences;
        j["created_at"] = adr.created_at;
        return j;
    };

    auto daiToJson = [](const Project::DaiEntry& dai) {
        json j;
        j["id"] = dai.id;
        j["kind"] = dai.kind;
        j["title"] = dai.title;
        j["owner"] = dai.owner;
        j["notes"] = dai.notes;
        j["opened_at"] = dai.opened_at;
        j["due_at"] = dai.due_at;
        j["closed"] = dai.closed;
        j["closed_at"] = dai.closed_at;
        return j;
    };

    auto transitionToJson = [](const Project::StatusTransition& tr) {
        json j;
        j["from"] = tr.from;
        j["to"] = tr.to;
        j["moved_at"] = tr.moved_at;
        return j;
    };

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
    j["auto_discovered"] = p.auto_discovered;
    j["source_path"]     = p.source_path;
    j["source_root"]     = p.source_root;
    j["adrs"] = json::array();
    for (const auto& adr : p.adrs) j["adrs"].push_back(adrToJson(adr));
    j["dais"] = json::array();
    for (const auto& dai : p.dais) j["dais"].push_back(daiToJson(dai));
    j["status_history"] = json::array();
    for (const auto& tr : p.status_history) j["status_history"].push_back(transitionToJson(tr));
    return j;
}

static Project projectFromJson(const json& j) {
    auto adrFromJson = [](const json& v) {
        Project::AdrEntry adr;
        adr.id = v.value("id", "");
        adr.title = v.value("title", "");
        adr.context = v.value("context", "");
        adr.decision = v.value("decision", "");
        adr.consequences = v.value("consequences", "");
        adr.created_at = v.value("created_at", "");
        return adr;
    };

    auto daiFromJson = [](const json& v) {
        Project::DaiEntry dai;
        dai.id = v.value("id", "");
        dai.kind = v.value("kind", "Action");
        dai.title = v.value("title", "");
        dai.owner = v.value("owner", "");
        dai.notes = v.value("notes", "");
        dai.opened_at = v.value("opened_at", "");
        dai.due_at = v.value("due_at", "");
        dai.closed = v.value("closed", false);
        dai.closed_at = v.value("closed_at", "");
        return dai;
    };

    auto transitionFromJson = [](const json& v) {
        Project::StatusTransition tr;
        tr.from = v.value("from", "Backlog");
        tr.to = v.value("to", "Backlog");
        tr.moved_at = v.value("moved_at", "");
        return tr;
    };

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
    p.auto_discovered = j.value("auto_discovered", false);
    p.source_path     = j.value("source_path",     "");
    p.source_root     = j.value("source_root",     "");
    if (j.contains("adrs") && j["adrs"].is_array()) {
        for (const auto& v : j["adrs"]) p.adrs.push_back(adrFromJson(v));
    }
    if (j.contains("dais") && j["dais"].is_array()) {
        for (const auto& v : j["dais"]) p.dais.push_back(daiFromJson(v));
    }
    if (j.contains("status_history") && j["status_history"].is_array()) {
        for (const auto& v : j["status_history"]) p.status_history.push_back(transitionFromJson(v));
    }
    return p;
}

void ProjectStore::recordStatusChange(Project& p, ProjectStatus from, ProjectStatus to, const std::string& movedAt) {
    if (from == to) return;
    Project::StatusTransition tr;
    tr.from = statusKey(from);
    tr.to = statusKey(to);
    tr.moved_at = movedAt.empty() ? todayISO() : movedAt;
    p.status_history.push_back(std::move(tr));
    m_dirty = true;
}

bool ProjectStore::canMoveToStatus(const Project& p, ProjectStatus to, std::string* reason) const {
    const auto fail = [&reason](const char* msg) {
        if (reason) *reason = msg;
        return false;
    };

    if (to == ProjectStatus::Doing) {
        if (p.description.empty()) return fail("DoR: descricao obrigatoria.");
        if (p.category.empty()) return fail("DoR: categoria obrigatoria.");
        if (p.tags.empty()) return fail("DoR: ao menos uma tag obrigatoria.");
    }

    if (to == ProjectStatus::Done) {
        if (p.adrs.empty()) return fail("DoD: exige ao menos um ADR.");
        for (const auto& dai : p.dais) {
            if (!dai.closed) return fail("DoD: existem itens DAI em aberto.");
        }
    }

    if (reason) reason->clear();
    return true;
}

ProjectStore::ProjectFlowMetrics ProjectStore::computeProjectFlowMetrics(const Project& p, const std::string& todayIso) const {
    ProjectFlowMetrics m;
    const std::string today = todayIso.empty() ? todayISO() : todayIso;
    m.status_transitions = static_cast<int>(p.status_history.size());

    for (const auto& dai : p.dais) {
        if (!dai.closed) {
            m.open_dai_items++;
            if (dai.kind == "Impediment") m.open_impediments++;
        }
    }

    m.aging_days = daysDiff(p.created_at, today);

    std::string firstDoingDate;
    std::string doneDate;
    for (const auto& tr : p.status_history) {
        if (firstDoingDate.empty() && tr.to == "Doing") firstDoingDate = tr.moved_at;
        if (tr.to == "Done") doneDate = tr.moved_at;
    }

    if (!doneDate.empty()) {
        m.lead_time_days = daysDiff(p.created_at, doneDate);
        if (!firstDoingDate.empty()) m.cycle_time_days = daysDiff(firstDoingDate, doneDate);
    } else if (!firstDoingDate.empty()) {
        m.cycle_time_days = daysDiff(firstDoingDate, today);
    }

    return m;
}

ProjectStore::GlobalFlowMetrics ProjectStore::computeGlobalFlowMetrics(const std::string& todayIso, int throughputWindowDays) const {
    GlobalFlowMetrics g;
    g.throughput_window_days = throughputWindowDays > 0 ? throughputWindowDays : 7;
    const std::string today = todayIso.empty() ? todayISO() : todayIso;
    const int todayDays = daysSinceEpoch(today);
    float agingSum = 0.f;

    for (const auto& p : m_projects) {
        g.total_projects++;
        if (p.status == ProjectStatus::Done) g.done_projects++;
        if (p.status == ProjectStatus::Doing || p.status == ProjectStatus::Review) g.wip_projects++;

        const auto metrics = computeProjectFlowMetrics(p, today);
        agingSum += metrics.aging_days;
        g.open_impediments += metrics.open_impediments;

        for (const auto& tr : p.status_history) {
            if (tr.to != "Done") continue;
            const int trDay = daysSinceEpoch(tr.moved_at);
            if (trDay >= 0 && todayDays >= 0 && (todayDays - trDay) <= g.throughput_window_days) g.throughput_in_window++;
        }
    }

    if (g.total_projects > 0) g.avg_aging_days = agingSum / static_cast<float>(g.total_projects);
    return g;
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
