#include "app/ApplicationServices.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace labgestao::application {
namespace {

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

bool isAutoProject(const Project& p) {
    if (p.auto_discovered) return true;
    for (const auto& tag : p.tags) {
        if (tag == "Auto") return true;
    }
    return false;
}

} // namespace

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
