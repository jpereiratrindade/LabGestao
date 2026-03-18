#include "app/ApplicationServices.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace labgestao::application {
namespace {
namespace fs = std::filesystem;

struct InferredProjectMetadata {
    std::string category;
    std::vector<std::string> tags;
};

std::string todayIsoDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf {};
    localtime_r(&t, &tm_buf);
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

std::optional<int> daysSinceEpoch(const std::string& isoDate) {
    if (isoDate.size() < 10) return std::nullopt;
    std::tm tm_buf {};
    std::istringstream ss(isoDate.substr(0, 10));
    ss >> std::get_time(&tm_buf, "%Y-%m-%d");
    if (ss.fail()) return std::nullopt;
    tm_buf.tm_hour = 12;
    tm_buf.tm_isdst = -1;
    std::time_t tt = std::mktime(&tm_buf);
    if (tt < 0) return std::nullopt;
    return static_cast<int>(tt / 86400);
}

std::optional<int> daysBetweenIso(const std::string& from, const std::string& to) {
    const auto a = daysSinceEpoch(from);
    const auto b = daysSinceEpoch(to);
    if (!a || !b) return std::nullopt;
    return *b - *a;
}

std::string shellEscapeSingleQuoted(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string runAndCaptureTrimmed(const std::string& cmd) {
    std::array<char, 512> buffer{};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    std::string out;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        out += buffer.data();
    }

    const int rc = pclose(pipe);
    if (rc != 0) return {};

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::optional<std::string> getLastGitCommitDate(const std::string& projectPath) {
    if (projectPath.empty()) return std::nullopt;
    const std::string cmd = "git -C " + shellEscapeSingleQuoted(projectPath) + " log -1 --format=%cs 2>/dev/null";
    const std::string out = runAndCaptureTrimmed(cmd);
    if (out.size() < 10) return std::nullopt;
    return out.substr(0, 10);
}

std::string getFirstGitCommitDate(const std::string& projectPath) {
    if (projectPath.empty()) return {};
    const std::string cmd = "git -C " + shellEscapeSingleQuoted(projectPath) + " log --max-parents=0 -1 --format=%cs 2>/dev/null";
    const std::string out = runAndCaptureTrimmed(cmd);
    if (out.size() < 10) return {};
    return out.substr(0, 10);
}

std::string isoFromSystemClock(std::chrono::system_clock::time_point tp) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf {};
    localtime_r(&tt, &tm_buf);
    char buf[11] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

std::string lastWriteTimeIso(const fs::path& p) {
    std::error_code ec;
    const auto ftime = fs::last_write_time(p, ec);
    if (ec) return {};
    const auto nowFile = fs::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    const auto sysTp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - nowFile + nowSys);
    return isoFromSystemClock(sysTp);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void appendTagUnique(std::vector<std::string>& tags, const std::string& tag) {
    if (tag.empty()) return;
    for (const auto& existing : tags) {
        if (toLower(existing) == toLower(tag)) return;
    }
    tags.push_back(tag);
}

std::string inferProjectCreatedAt(const fs::path& dir, const MonitoredRoot& root) {
    const std::string gitBirth = getFirstGitCommitDate(dir.string());
    if (!gitBirth.empty()) return gitBirth;

    std::string best = lastWriteTimeIso(dir);
    for (const auto& marker : root.markerFiles) {
        const fs::path markerPath = dir / marker;
        if (!fs::exists(markerPath)) continue;
        const std::string markerDate = lastWriteTimeIso(markerPath);
        if (markerDate.empty()) continue;
        if (best.empty() || markerDate < best) best = markerDate;
    }
    if (!best.empty()) return best;
    return todayIsoDate();
}

InferredProjectMetadata inferProjectMetadata(const fs::path& dir, const MonitoredRoot& root) {
    const auto has = [&](const char* name) {
        return fs::exists(dir / name);
    };
    const bool hasCpp = has("CMakeLists.txt") || has("meson.build") || has("compile_commands.json");
    const bool hasPython = has("pyproject.toml") || has("requirements.txt") || has("setup.py") || has("Pipfile");
    const bool hasRust = has("Cargo.toml");
    const bool hasGo = has("go.mod");
    const bool hasNode = has("package.json");
    const bool hasJava = has("pom.xml") || has("build.gradle") || has("build.gradle.kts");
    const bool hasDotnet = has(".sln");
    const bool hasGit = fs::exists(dir / ".git");

    std::string inferredCategory;
    if (hasCpp) inferredCategory = "C++";
    else if (hasPython) inferredCategory = "Python";
    else if (hasRust) inferredCategory = "Rust";
    else if (hasGo) inferredCategory = "Go";
    else if (hasNode) inferredCategory = "Node";
    else if (hasJava) inferredCategory = "Java";
    else if (hasDotnet) inferredCategory = ".NET";

    const std::string rootCategoryLower = toLower(root.category);
    const bool useInferredAsPrimary = root.category.empty() || rootCategoryLower == "workspace";

    InferredProjectMetadata out;
    out.category = useInferredAsPrimary
        ? (inferredCategory.empty() ? "Workspace" : inferredCategory)
        : root.category;

    for (const auto& t : root.tags) appendTagUnique(out.tags, t);
    if (!inferredCategory.empty()) appendTagUnique(out.tags, inferredCategory);
    if (hasGit) appendTagUnique(out.tags, "Git");
    if (hasCpp) appendTagUnique(out.tags, "CMake");
    if (hasPython) appendTagUnique(out.tags, "PyProject");
    if (hasRust) appendTagUnique(out.tags, "Cargo");
    if (hasGo) appendTagUnique(out.tags, "GoMod");
    if (hasNode) appendTagUnique(out.tags, "NPM");
    if (hasJava) appendTagUnique(out.tags, "JVM");
    if (hasDotnet) appendTagUnique(out.tags, "DotNet");
    if (out.tags.empty()) appendTagUnique(out.tags, "Auto");
    return out;
}

std::string canonicalPathOrRaw(const fs::path& p) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec) {
        ec.clear();
        canon = fs::absolute(p, ec);
    }
    if (ec) return p.lexically_normal().string();
    return canon.lexically_normal().string();
}

std::string makeAutoProjectId(const std::string& canonicalPath) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : canonicalPath) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << "auto-" << std::hex << h;
    return oss.str();
}

bool samePathBestEffort(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    return canonicalPathOrRaw(fs::path(a)) == canonicalPathOrRaw(fs::path(b));
}

bool looksLikeProjectFolder(const fs::path& dir, const MonitoredRoot& root) {
    if (fs::exists(dir / ".git")) return true;
    for (const auto& marker : root.markerFiles) {
        if (fs::exists(dir / marker)) return true;
    }
    return false;
}

std::vector<Project> scanProjectsFromRoots(const std::vector<MonitoredRoot>& roots) {
    std::vector<Project> discovered;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root.path, ec) || !fs::is_directory(root.path, ec)) continue;

        fs::directory_iterator it(root.path, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::end(it); it.increment(ec)) {
            const fs::directory_entry entry = *it;
            if (!entry.is_directory(ec)) continue;

            const fs::path dir = entry.path();
            const std::string folderName = dir.filename().string();
            if (folderName.empty() || folderName[0] == '.') continue;
            if (!looksLikeProjectFolder(dir, root)) continue;

            const std::string canonicalPath = canonicalPathOrRaw(dir);

            Project p;
            p.id = makeAutoProjectId(canonicalPath);
            p.name = folderName;
            p.status = ProjectStatus::Backlog;
            const InferredProjectMetadata metadata = inferProjectMetadata(dir, root);
            p.category = metadata.category;
            p.description = "Projeto detectado automaticamente em pasta monitorada.";
            p.tags = metadata.tags;
            p.created_at = inferProjectCreatedAt(dir, root);
            p.auto_discovered = true;
            p.source_path = canonicalPath;
            p.source_root = root.path;
            discovered.push_back(std::move(p));
        }
    }

    std::sort(discovered.begin(), discovered.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return discovered;
}

bool isAutoProject(const Project& p) {
    if (p.auto_discovered) return true;
    for (const auto& tag : p.tags) {
        if (tag == "Auto") return true;
    }
    return false;
}

} // namespace

void syncAutoDiscoveredProjects(ProjectStore& store, const std::vector<MonitoredRoot>& roots) {
    const auto scanned = scanProjectsFromRoots(roots);
    std::unordered_set<std::string> currentAutoIds;
    currentAutoIds.reserve(scanned.size());

    for (const auto& detected : scanned) {
        Project* target = nullptr;

        if (auto opt = store.findById(detected.id); opt) {
            target = *opt;
        }

        if (!target && !detected.source_path.empty()) {
            for (auto& existing : store.getAll()) {
                if (existing.auto_discovered && samePathBestEffort(existing.source_path, detected.source_path)) {
                    target = &existing;
                    break;
                }
            }
        }

        if (!target) {
            store.add(detected);
            currentAutoIds.insert(detected.id);
            continue;
        }

        Project updated = *target;
        bool changed = false;

        if (!updated.auto_discovered) { updated.auto_discovered = true; changed = true; }
        if (updated.name != detected.name) { updated.name = detected.name; changed = true; }
        if (updated.category != detected.category) { updated.category = detected.category; changed = true; }
        if (updated.tags != detected.tags) { updated.tags = detected.tags; changed = true; }
        if (updated.source_path != detected.source_path) { updated.source_path = detected.source_path; changed = true; }
        if (updated.source_root != detected.source_root) { updated.source_root = detected.source_root; changed = true; }
        if (updated.created_at.empty() || (!detected.created_at.empty() && detected.created_at < updated.created_at)) {
            updated.created_at = detected.created_at;
            changed = true;
        }
        if (updated.description.empty()) { updated.description = detected.description; changed = true; }

        if (changed) store.update(updated);
        currentAutoIds.insert(updated.id);
    }

    std::vector<std::string> staleAutoProjects;
    for (const auto& p : store.getAll()) {
        if (p.auto_discovered && !currentAutoIds.contains(p.id)) staleAutoProjects.push_back(p.id);
    }
    for (const auto& id : staleAutoProjects) store.remove(id);
}

ReclassifyKanbanAutoResult reclassifyKanbanAuto(ProjectStore& store) {
    const std::string today = todayIsoDate();

    std::vector<std::string> candidateIds;
    candidateIds.reserve(store.getAll().size());
    for (const auto& p : store.getAll()) {
        if (isAutoProject(p)) candidateIds.push_back(p.id);
    }

    ReclassifyKanbanAutoResult result;
    for (const auto& id : candidateIds) {
        auto opt = store.findById(id);
        if (!opt || !(*opt)) {
            result.skipped++;
            continue;
        }
        Project* current = *opt;
        if (!current) {
            result.skipped++;
            continue;
        }

        int openDai = 0;
        int openImpediments = 0;
        for (const auto& dai : current->dais) {
            if (dai.closed) continue;
            openDai++;
            if (dai.kind == "Impediment") openImpediments++;
        }

        std::optional<ProjectStatus> target;
        if (openImpediments > 0) {
            target = ProjectStatus::Paused;
        } else if (openDai > 0) {
            target = ProjectStatus::Doing;
        } else if (const auto lastCommit = getLastGitCommitDate(current->source_path); lastCommit) {
            if (const auto age = daysBetweenIso(*lastCommit, today); age) {
                if (*age <= 7) target = ProjectStatus::Doing;
                else if (*age <= 30) target = ProjectStatus::Review;
                else if (*age <= 120) target = ProjectStatus::Paused;
                else target = ProjectStatus::Backlog;
            }
        }

        if (!target || *target == current->status) {
            result.unchanged++;
            continue;
        }

        std::string reason;
        if (!store.canMoveToStatus(*current, *target, &reason)) {
            result.skipped++;
            continue;
        }

        Project updated = *current;
        const ProjectStatus from = updated.status;
        updated.status = *target;
        store.recordStatusChange(updated, from, updated.status, today);
        store.update(updated);
        result.changed++;
    }

    return result;
}

} // namespace labgestao::application
