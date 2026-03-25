#include "app/ApplicationServices.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

bool hasAnyFile(const fs::path& root, std::initializer_list<const char*> names) {
    std::error_code ec;
    for (const auto* name : names) {
        if (!name || !*name) continue;
        if (fs::exists(root / name, ec)) return true;
    }
    return false;
}

bool hasFileNameToken(const fs::path& root, const std::vector<std::string>& tokens, int maxDepth = 4) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return false;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) break;
        if (it.depth() > maxDepth) it.disable_recursion_pending();
        if (it->is_regular_file(ec)) {
            const std::string name = toLower(it->path().filename().string());
            for (const auto& token : tokens) {
                if (name.find(token) != std::string::npos) return true;
            }
        }
        ++it;
    }
    return false;
}

bool hasMarkdownAdrFile(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return false;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = toLower(entry.path().filename().string());
        if (entry.path().extension() == ".md" && (name.rfind("adr-", 0) == 0 || name.find("adr") != std::string::npos)) {
            return true;
        }
    }
    return false;
}

bool hasAdrEvidence(const fs::path& root) {
    if (hasMarkdownAdrFile(root / "adr")) return true;
    if (hasMarkdownAdrFile(root / "docs" / "adr")) return true;
    return hasFileNameToken(root / "docs", {"adr-"}, 4);
}

bool hasDddEvidence(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root / "ddd", ec) && fs::is_directory(root / "ddd", ec)) return true;
    if (fs::exists(root / "docs" / "ddd", ec) && fs::is_directory(root / "docs" / "ddd", ec)) return true;
    if (fs::exists(root / "docs" / "architecture", ec) && fs::is_directory(root / "docs" / "architecture", ec)) return true;
    return hasFileNameToken(root, {"ddd", "domain-model", "context-map", "bounded-context"}, 4);
}

bool hasDaiEvidence(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root / "dai", ec) && fs::is_directory(root / "dai", ec)) return true;
    if (fs::exists(root / "docs" / "dai", ec) && fs::is_directory(root / "docs" / "dai", ec)) return true;
    return hasFileNameToken(root, {"dai"}, 4);
}

bool hasPoliciesEvidence(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root / "policies", ec) && fs::is_directory(root / "policies", ec)) return true;
    return hasFileNameToken(root, {"policy", "policies", "approval_matrix", "safety_constraints"}, 4);
}

bool hasToolContractsEvidence(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root / "mcp" / "contracts", ec) && fs::is_directory(root / "mcp" / "contracts", ec)) return true;
    if (fs::exists(root / "mcp" / "tool_schema.json", ec)) return true;
    return hasFileNameToken(root, {"contract", "tool_schema", "agent_interface"}, 4);
}

bool hasApprovalPolicyEvidence(const fs::path& root) {
    if (hasAnyFile(root / "policies", {"approval_matrix.md", "approval-matrix.md"})) return true;
    return hasFileNameToken(root, {"approval_matrix", "approval-policy", "codeowners"}, 4);
}

bool hasAuditEvidence(const fs::path& root) {
    if (hasAnyFile(root / "examples", {"evidence_log.json", "audit_log.json"})) return true;
    if (hasAnyFile(root / "prompts", {"governance_task_packet.md", "task_template.md"})) return true;
    if (hasAnyFile(root / "scripts", {"validate_governance_repo.py", "audit_governance.sh"})) return true;
    return hasFileNameToken(root, {"evidence_log", "audit", "traceability", "governance_task_packet"}, 4);
}

int governanceSignalsCount(const fs::path& root) {
    int signals = 0;
    if (hasAnyFile(root, {"CONTRIBUTING.md", "contributing.md"})) signals++;
    if (hasAnyFile(root, {"CODEOWNERS"}) || hasAnyFile(root / ".github", {"CODEOWNERS"})) signals++;
    if (hasAnyFile(root, {"PULL_REQUEST_TEMPLATE.md", "pull_request_template.md"}) ||
        hasAnyFile(root / ".github", {"pull_request_template.md", "PULL_REQUEST_TEMPLATE.md"})) {
        signals++;
    }
    std::error_code ec;
    if ((fs::exists(root / ".github" / "ISSUE_TEMPLATE", ec) && fs::is_directory(root / ".github" / "ISSUE_TEMPLATE", ec)) ||
        hasAnyFile(root / ".github", {"issue_template.md", "ISSUE_TEMPLATE.md"})) {
        signals++;
    }
    if (hasPoliciesEvidence(root)) signals++;
    if (hasToolContractsEvidence(root)) signals++;
    if (hasApprovalPolicyEvidence(root)) signals++;
    if (hasAuditEvidence(root)) signals++;
    return std::clamp(signals, 0, 8);
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
            p.governance_profile = analyzeProjectGovernance(p);
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

Project::GovernanceProfile analyzeProjectGovernance(const Project& project) {
    Project::GovernanceProfile profile;
    profile.analyzed = true;
    profile.analyzed_at = todayIsoDate();
    profile.analyzed_path = project.source_path;
    profile.path_available = !project.source_path.empty() && fs::exists(project.source_path);

    if (profile.path_available) {
        const fs::path root(project.source_path);
        profile.has_adr = !project.adrs.empty() || hasAdrEvidence(root);
        profile.has_ddd = hasDddEvidence(root);
        profile.has_dai = !project.dais.empty() || hasDaiEvidence(root);
        profile.has_policies = hasPoliciesEvidence(root);
        profile.has_tool_contracts = hasToolContractsEvidence(root);
        profile.has_approval_policy = hasApprovalPolicyEvidence(root);
        profile.has_audit_evidence = hasAuditEvidence(root);
        profile.governance_signals = governanceSignalsCount(root);
    } else {
        profile.has_adr = !project.adrs.empty();
        profile.has_dai = !project.dais.empty();
    }

    if (profile.has_adr) profile.maturity_score += 25;
    if (profile.has_ddd) profile.maturity_score += 25;
    if (profile.has_dai) profile.maturity_score += 25;
    profile.maturity_score += static_cast<int>(std::lround((static_cast<double>(profile.governance_signals) / 8.0) * 25.0));
    profile.maturity_score = std::clamp(profile.maturity_score, 0, 100);

    profile.vibe_risk = 100 - profile.maturity_score;
    if (!profile.has_policies) profile.vibe_risk += 8;
    if (!profile.has_tool_contracts) profile.vibe_risk += 10;
    if (!profile.has_approval_policy) profile.vibe_risk += 8;
    if (!profile.has_audit_evidence) profile.vibe_risk += 8;
    if (!profile.path_available) profile.vibe_risk += 6;
    profile.vibe_risk = std::clamp(profile.vibe_risk, 0, 100);

    if (profile.maturity_score >= 80) profile.maturity_label = "Governanca forte";
    else if (profile.maturity_score >= 55) profile.maturity_label = "Governanca intermediaria";
    else profile.maturity_label = "Governanca incipiente";

    if (!profile.has_adr) profile.next_actions.push_back("Registrar ao menos um ADR estrutural do projeto.");
    if (!profile.has_ddd) profile.next_actions.push_back("Explicitar contexto, limites e linguagem ubiqua em um artefato DDD.");
    if (!profile.has_dai) profile.next_actions.push_back("Abrir backlog DAI para decisoes, acoes e impedimentos.");
    if (!profile.has_policies) profile.next_actions.push_back("Versionar politicas de uso de IA, seguranca e fronteiras de contexto.");
    if (!profile.has_tool_contracts) profile.next_actions.push_back("Definir contratos executaveis para ferramentas/agentes.");
    if (!profile.has_approval_policy) profile.next_actions.push_back("Formalizar matriz de aprovacao proporcional ao risco.");
    if (!profile.has_audit_evidence) profile.next_actions.push_back("Padronizar task packet e evidence log para auditoria.");
    if (!profile.path_available) profile.next_actions.push_back("Associar o projeto a um caminho de repositorio para leitura automatica de governanca.");
    if (profile.next_actions.empty()) {
        profile.next_actions.push_back("Manter o baseline atual e revisar regressao de governanca a cada mudanca relevante.");
    }

    return profile;
}

bool refreshProjectGovernance(ProjectStore& store, const std::string& projectId) {
    auto current = store.findById(projectId);
    if (!current) return false;

    Project updated = **current;
    const Project::GovernanceProfile profile = analyzeProjectGovernance(updated);
    if (updated.governance_profile.analyzed == profile.analyzed &&
        updated.governance_profile.analyzed_at == profile.analyzed_at &&
        updated.governance_profile.analyzed_path == profile.analyzed_path &&
        updated.governance_profile.path_available == profile.path_available &&
        updated.governance_profile.has_adr == profile.has_adr &&
        updated.governance_profile.has_ddd == profile.has_ddd &&
        updated.governance_profile.has_dai == profile.has_dai &&
        updated.governance_profile.has_policies == profile.has_policies &&
        updated.governance_profile.has_tool_contracts == profile.has_tool_contracts &&
        updated.governance_profile.has_approval_policy == profile.has_approval_policy &&
        updated.governance_profile.has_audit_evidence == profile.has_audit_evidence &&
        updated.governance_profile.governance_signals == profile.governance_signals &&
        updated.governance_profile.maturity_score == profile.maturity_score &&
        updated.governance_profile.vibe_risk == profile.vibe_risk &&
        updated.governance_profile.maturity_label == profile.maturity_label &&
        updated.governance_profile.next_actions == profile.next_actions) {
        return false;
    }

    updated.governance_profile = profile;
    store.update(updated);
    return true;
}

int refreshAllProjectGovernance(ProjectStore& store) {
    int changed = 0;
    std::vector<std::string> ids;
    ids.reserve(store.getAll().size());
    for (const auto& project : store.getAll()) ids.push_back(project.id);
    for (const auto& id : ids) {
        if (refreshProjectGovernance(store, id)) changed++;
    }
    return changed;
}

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
                if (samePathBestEffort(existing.source_path, detected.source_path)) {
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
        if (!detected.governance_profile.analyzed_path.empty() &&
            updated.governance_profile.analyzed_path != detected.governance_profile.analyzed_path) {
            updated.governance_profile = detected.governance_profile;
            changed = true;
        }
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
