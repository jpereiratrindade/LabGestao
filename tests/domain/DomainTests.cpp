#include "app/ApplicationServices.hpp"
#include "domain/Project.hpp"
#include "domain/ProjectScaffold.hpp"
#include "domain/ProjectStore.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using labgestao::Project;
using labgestao::ProjectStatus;
using labgestao::ProjectStore;
using labgestao::ProjectTemplate;
using labgestao::ScaffoldRequest;
using labgestao::application::MonitoredRoot;
using labgestao::application::analyzeProjectGovernance;
using labgestao::application::reclassifyKanbanAuto;
using labgestao::application::refreshProjectGovernance;
using labgestao::application::syncAutoDiscoveredProjects;

namespace {

int g_failures = 0;

void fail(const std::string& msg) {
    std::cerr << "[FAIL] " << msg << "\n";
    g_failures++;
}

void expect(bool cond, const std::string& msg) {
    if (!cond) fail(msg);
}

void expectEqInt(int got, int expected, const std::string& msg) {
    if (got != expected) {
        fail(msg + " (got=" + std::to_string(got) + ", expected=" + std::to_string(expected) + ")");
    }
}

void expectEqStr(const std::string& got, const std::string& expected, const std::string& msg) {
    if (got != expected) {
        fail(msg + " (got='" + got + "', expected='" + expected + "')");
    }
}

void expectNear(float got, float expected, float eps, const std::string& msg) {
    if (std::fabs(got - expected) > eps) {
        fail(msg + " (got=" + std::to_string(got) + ", expected=" + std::to_string(expected) + ")");
    }
}

void testStatusRules() {
    ProjectStore store;
    Project p;
    p.id = "p1";

    std::string reason;
    expect(!store.canMoveToStatus(p, ProjectStatus::Doing, &reason), "DoR should block project without required fields");
    expect(!reason.empty(), "DoR failure should provide reason");

    p.description = "desc";
    p.category = "C++";
    p.tags = {"tag"};
    expect(store.canMoveToStatus(p, ProjectStatus::Doing, &reason), "DoR should allow project with description/category/tags");

    expect(!store.canMoveToStatus(p, ProjectStatus::Done, &reason), "DoD should require at least one ADR");

    p.adrs.push_back(Project::AdrEntry{"adr-1", "Escolha", "ctx", "dec", "cons", "2026-03-10"});
    p.dais.push_back(Project::DaiEntry{"d1", "Action", "fazer", "time", "", "2026-03-10", "2026-03-11", false, ""});

    expect(!store.canMoveToStatus(p, ProjectStatus::Done, &reason), "DoD should block when there are open DAIs");

    p.dais[0].closed = true;
    p.dais[0].closed_at = "2026-03-11";
    expect(store.canMoveToStatus(p, ProjectStatus::Done, &reason), "DoD should pass with ADR and closed DAIs");
}

void testProjectMetrics() {
    ProjectStore store;
    Project p;
    p.id = "p-metrics";
    p.created_at = "2026-03-01";
    p.status_history.push_back({"Backlog", "Doing", "2026-03-03"});
    p.status_history.push_back({"Doing", "Done", "2026-03-10"});
    p.dais.push_back(Project::DaiEntry{"d1", "Impediment", "Bloqueio", "dev", "", "2026-03-04", "", false, ""});
    p.dais.push_back(Project::DaiEntry{"d2", "Action", "Implementar", "dev", "", "2026-03-05", "", true, "2026-03-06"});

    auto m = store.computeProjectFlowMetrics(p, "2026-03-12");
    expectEqInt(m.status_transitions, 2, "status transition count");
    expectEqInt(m.open_dai_items, 1, "open DAI count");
    expectEqInt(m.open_impediments, 1, "open impediment count");
    expectNear(m.aging_days, 11.0f, 0.01f, "aging days");
    expectNear(m.lead_time_days, 9.0f, 0.01f, "lead time days");
    expectNear(m.cycle_time_days, 7.0f, 0.01f, "cycle time days");
}

void testGlobalMetrics() {
    ProjectStore store;

    Project p1;
    p1.id = "p1";
    p1.created_at = "2026-03-01";
    p1.status = ProjectStatus::Doing;
    p1.dais.push_back(Project::DaiEntry{"d1", "Impediment", "i", "o", "", "2026-03-12", "", false, ""});

    Project p2;
    p2.id = "p2";
    p2.created_at = "2026-03-05";
    p2.status = ProjectStatus::Done;
    p2.status_history.push_back({"Review", "Done", "2026-03-15"});

    store.add(p1);
    store.add(p2);

    const auto g = store.computeGlobalFlowMetrics("2026-03-16", 7);
    expectEqInt(g.total_projects, 2, "global total projects");
    expectEqInt(g.wip_projects, 1, "global WIP projects");
    expectEqInt(g.done_projects, 1, "global done projects");
    expectEqInt(g.open_impediments, 1, "global open impediments");
    expectEqInt(g.throughput_in_window, 1, "throughput in 7-day window");
    expectNear(g.avg_aging_days, 13.0f, 0.01f, "average aging days");
}

void testJsonRoundtrip() {
    ProjectStore store;
    Project p;
    p.id = "persist-1";
    p.name = "Persist Project";
    p.description = "desc";
    p.status = ProjectStatus::Review;
    p.category = "C++";
    p.tags = {"core", "ui"};
    p.connections = {"x", "y"};
    p.created_at = "2026-03-01";
    p.graph_x = 10.0f;
    p.graph_y = 20.0f;
    p.auto_discovered = true;
    p.source_path = "/tmp/persist";
    p.source_root = "/tmp";
    p.adrs.push_back({"adr-1", "T", "C", "D", "K", "2026-03-10"});
    p.dais.push_back({"dai-1", "Action", "A", "owner", "notes", "2026-03-10", "2026-03-20", true, "2026-03-11"});
    p.status_history.push_back({"Backlog", "Review", "2026-03-09"});
    p.governance_profile.analyzed = true;
    p.governance_profile.analyzed_at = "2026-03-24";
    p.governance_profile.analyzed_path = "/tmp/persist";
    p.governance_profile.path_available = true;
    p.governance_profile.has_adr = true;
    p.governance_profile.governance_signals = 6;
    p.governance_profile.maturity_score = 82;
    p.governance_profile.vibe_risk = 24;
    p.governance_profile.maturity_label = "Governanca forte";
    p.governance_profile.next_actions = {"Manter baseline."};
    store.add(p);

    const fs::path tmpDir = fs::temp_directory_path() / ("labgestao-tests-" + ProjectStore::generateId());
    fs::create_directories(tmpDir);
    const fs::path jsonPath = tmpDir / "projects.json";

    expect(store.saveToJson(jsonPath.string()), "saveToJson should persist file");

    ProjectStore loaded;
    expect(loaded.loadFromJson(jsonPath.string()), "loadFromJson should read saved file");
    expectEqInt(static_cast<int>(loaded.getAll().size()), 1, "loaded project count");

    Project loadedProject = loaded.getAll().front();
    expectEqStr(loadedProject.id, "persist-1", "roundtrip id");
    expectEqStr(loadedProject.name, "Persist Project", "roundtrip name");
    expectEqStr(loadedProject.source_root, "/tmp", "roundtrip source root");
    expectEqInt(static_cast<int>(loadedProject.adrs.size()), 1, "roundtrip ADR count");
    expectEqInt(static_cast<int>(loadedProject.dais.size()), 1, "roundtrip DAI count");
    expectEqInt(static_cast<int>(loadedProject.status_history.size()), 1, "roundtrip status history count");
    expect(loadedProject.governance_profile.analyzed, "roundtrip governance profile analyzed");
    expectEqInt(loadedProject.governance_profile.maturity_score, 82, "roundtrip governance maturity score");
    expectEqInt(static_cast<int>(loadedProject.governance_profile.next_actions.size()), 1,
                "roundtrip governance next actions count");

    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}

void testProjectStoreSourcePathUniqueness() {
    ProjectStore store;
    const fs::path base = fs::temp_directory_path() / ("labgestao-unique-tests-" + ProjectStore::generateId());
    const fs::path repo = base / "repo";
    fs::create_directories(repo);

    Project p1;
    p1.id = "one";
    p1.name = "Projeto Um";
    p1.source_path = repo.string();
    store.add(p1);

    Project p2;
    p2.id = "two";
    p2.name = "Projeto Dois";
    p2.source_path = (base / "." / "repo").string();
    store.add(p2);

    expectEqInt(static_cast<int>(store.getAll().size()), 1, "duplicate canonical source_path should not be added");
    expect(store.duplicateSourcePathReason(p2).has_value(), "duplicate canonical source_path should expose a reason");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testProjectStoreDuplicateNameHelper() {
    ProjectStore store;
    const fs::path base = fs::temp_directory_path() / ("labgestao-name-tests-" + ProjectStore::generateId());
    const fs::path repo1 = base / "repo1";
    const fs::path repo2 = base / "repo2";
    fs::create_directories(repo1);
    fs::create_directories(repo2);

    Project p1;
    p1.id = "one";
    p1.name = "Projeto Alfa";
    p1.source_path = repo1.string();
    store.add(p1);

    Project p2;
    p2.id = "two";
    p2.name = " projeto alfa ";
    p2.source_path = repo2.string();
    store.add(p2);

    expectEqInt(static_cast<int>(store.getAll().size()), 2, "duplicate names without path collision should be allowed");
    expect(store.duplicateNormalizedNameReason(p2).has_value(), "duplicate normalized name should expose a reason");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testProjectStoreUpdateBlocksDuplicateSourcePath() {
    ProjectStore store;
    const fs::path base = fs::temp_directory_path() / ("labgestao-update-tests-" + ProjectStore::generateId());
    const fs::path repo1 = base / "repo1";
    const fs::path repo2 = base / "repo2";
    fs::create_directories(repo1);
    fs::create_directories(repo2);

    Project p1;
    p1.id = "one";
    p1.name = "Projeto Um";
    p1.source_path = repo1.string();
    store.add(p1);

    Project p2;
    p2.id = "two";
    p2.name = "Projeto Dois";
    p2.source_path = repo2.string();
    store.add(p2);

    p2.source_path = (base / "." / "repo1").string();
    store.update(p2);

    auto updated = store.findById("two");
    expect(updated.has_value(), "updated project should still exist");
    expectEqStr((*updated)->source_path, repo2.string(), "update should be blocked when source_path collides");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testProjectStoreLoadDeduplicatesLegacySourcePathCollisions() {
    const fs::path base = fs::temp_directory_path() / ("labgestao-load-dedup-tests-" + ProjectStore::generateId());
    const fs::path repo = base / "cre";
    fs::create_directories(repo);

    const fs::path jsonPath = base / "projects.json";
    std::ofstream out(jsonPath);
    out << R"([
  {
    "id": "manual-cre",
    "name": "CRE",
    "description": "manual",
    "status": "Backlog",
    "category": "Governado",
    "tags": ["Manual"],
    "connections": [],
    "created_at": "2026-03-19",
    "graph_x": 0.0,
    "graph_y": 0.0,
    "auto_discovered": false,
    "source_path": ")" << repo.string() << R"(",
    "source_root": ")" << base.string() << R"(",
    "adrs": [],
    "dais": [],
    "status_history": []
  },
  {
    "id": "auto-cre",
    "name": "cre",
    "description": "Projeto detectado automaticamente em pasta monitorada.",
    "status": "Doing",
    "category": "C++",
    "tags": ["Auto", "CMake"],
    "connections": [],
    "created_at": "2026-03-20",
    "graph_x": 10.0,
    "graph_y": 20.0,
    "auto_discovered": true,
    "source_path": ")" << (base / "." / "cre").string() << R"(",
    "source_root": ")" << base.string() << R"(",
    "adrs": [],
    "dais": [],
    "status_history": [{"from":"Backlog","to":"Doing","moved_at":"2026-03-20"}]
  }
])";
    out.close();

    ProjectStore store;
    expect(store.loadFromJson(jsonPath.string()), "legacy duplicate source_path json should load");
    expectEqInt(static_cast<int>(store.getAll().size()), 1,
                "load should deduplicate legacy entries with same source_path");

    const Project& merged = store.getAll().front();
    expectEqStr(merged.name, "CRE", "manual name should win during dedup");
    expect(merged.auto_discovered, "merged project should keep auto-discovered signal");
    expectEqInt(static_cast<int>(merged.status), static_cast<int>(ProjectStatus::Doing),
                "more advanced status should survive dedup");
    expectEqInt(static_cast<int>(merged.status_history.size()), 1,
                "status history should be preserved from duplicate");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testScaffold() {
    const fs::path base = fs::temp_directory_path() / ("labgestao-scaffold-tests-" + ProjectStore::generateId());
    fs::create_directories(base);

    {
        ScaffoldRequest req;
        req.templ = ProjectTemplate::Cpp;
        req.projectName = "Meu Projeto C++";
        req.baseDirectory = base.string();
        const auto res = labgestao::createProjectScaffold(req);
        expect(res.ok, "C++ scaffold should succeed");

        const fs::path dir(res.projectDir);
        expect(fs::exists(dir / "CMakeLists.txt"), "C++ scaffold should create CMakeLists.txt");
        expect(fs::exists(dir / "src/main.cpp"), "C++ scaffold should create src/main.cpp");
        expect(fs::exists(dir / "include"), "C++ scaffold should create include/");
        expect(fs::exists(dir / "tests/.gitkeep"), "C++ scaffold should create tests/.gitkeep");
    }

    {
        ScaffoldRequest req;
        req.templ = ProjectTemplate::Python;
        req.projectName = "Data Tool";
        req.baseDirectory = base.string();
        const auto res = labgestao::createProjectScaffold(req);
        expect(res.ok, "Python scaffold should succeed");

        const fs::path dir(res.projectDir);
        expect(fs::exists(dir / "pyproject.toml"), "Python scaffold should create pyproject.toml");
        expect(fs::exists(dir / "src/data_tool/main.py"), "Python scaffold should create package main.py");
        expect(fs::exists(dir / "tests/test_smoke.py"), "Python scaffold should create tests/test_smoke.py");
    }

    {
        ScaffoldRequest req;
        req.templ = ProjectTemplate::None;
        req.projectName = "Sem Template";
        req.baseDirectory = base.string();
        const auto res = labgestao::createProjectScaffold(req);
        expect(res.ok, "None template should be accepted");
    }

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testApplicationReclassifyFromDaiSignals() {
    ProjectStore store;

    Project p;
    p.id = "auto-1";
    p.name = "Auto One";
    p.status = ProjectStatus::Backlog;
    p.auto_discovered = true;
    p.description = "desc";
    p.category = "C++";
    p.tags = {"Auto"};
    p.created_at = "2026-03-01";
    p.dais.push_back(Project::DaiEntry{"d1", "Impediment", "Bloqueio", "dev", "", "2026-03-02", "", false, ""});
    store.add(p);

    const auto result = reclassifyKanbanAuto(store);
    expectEqInt(result.changed, 1, "application reclassify should change auto project with open impediment");
    expectEqInt(result.unchanged, 0, "application reclassify unchanged count");
    expectEqInt(result.skipped, 0, "application reclassify skipped count");

    auto updated = store.findById("auto-1");
    expect(updated.has_value(), "updated project should exist");
    expectEqInt(static_cast<int>((*updated)->status), static_cast<int>(ProjectStatus::Paused), "auto project should move to Paused");
    expectEqInt(static_cast<int>((*updated)->status_history.size()), 1, "status history should record one transition");
}

void testApplicationReclassifyRespectsInvariants() {
    ProjectStore store;

    Project p;
    p.id = "auto-2";
    p.name = "Auto Two";
    p.status = ProjectStatus::Backlog;
    p.auto_discovered = true;
    p.created_at = "2026-03-01";
    p.dais.push_back(Project::DaiEntry{"d1", "Action", "Implementar", "dev", "", "2026-03-02", "", false, ""});
    store.add(p);

    const auto result = reclassifyKanbanAuto(store);
    expectEqInt(result.changed, 0, "application reclassify should not force invalid transition");
    expectEqInt(result.unchanged, 0, "application reclassify unchanged count when transition is blocked");
    expectEqInt(result.skipped, 1, "application reclassify should skip blocked transition");

    auto updated = store.findById("auto-2");
    expect(updated.has_value(), "project should still exist");
    expectEqInt(static_cast<int>((*updated)->status), static_cast<int>(ProjectStatus::Backlog), "project should remain Backlog");
    expectEqInt(static_cast<int>((*updated)->status_history.size()), 0, "status history should remain empty");
}

void testApplicationSyncAutoDiscoveredProjects() {
    ProjectStore store;

    Project manual;
    manual.id = "manual-1";
    manual.name = "Manual";
    manual.status = ProjectStatus::Backlog;
    manual.auto_discovered = false;
    store.add(manual);

    Project staleAuto;
    staleAuto.id = "auto-stale";
    staleAuto.name = "Old Auto";
    staleAuto.status = ProjectStatus::Backlog;
    staleAuto.auto_discovered = true;
    staleAuto.source_path = "/tmp/does-not-exist";
    store.add(staleAuto);

    const fs::path base = fs::temp_directory_path() / ("labgestao-sync-tests-" + ProjectStore::generateId());
    const fs::path repo = base / "repo_one";
    fs::create_directories(repo);
    std::ofstream(repo / "CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n";

    MonitoredRoot root;
    root.path = base.string();
    root.category = "Workspace";
    root.tags = {"Auto"};
    root.markerFiles = {"CMakeLists.txt"};

    syncAutoDiscoveredProjects(store, {root});

    expectEqInt(static_cast<int>(store.getAll().size()), 2, "sync should keep manual project and replace stale auto project");

    bool foundManual = false;
    bool foundDetected = false;
    for (const auto& p : store.getAll()) {
        if (p.id == "manual-1") {
            foundManual = true;
            expect(!p.auto_discovered, "manual project must remain manual");
        }
        if (p.name == "repo_one") {
            foundDetected = true;
            expect(p.auto_discovered, "detected project must be auto_discovered");
            expectEqStr(p.source_root, base.string(), "detected project source_root");
        }
        expect(p.id != "auto-stale", "stale auto project should be removed");
    }

    expect(foundManual, "manual project should still exist after sync");
    expect(foundDetected, "new auto project should be discovered");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testApplicationSyncReusesManualProjectWithSamePath() {
    ProjectStore store;

    const fs::path base = fs::temp_directory_path() / ("labgestao-sync-manual-tests-" + ProjectStore::generateId());
    const fs::path repo = base / "cre";
    fs::create_directories(repo);
    std::ofstream(repo / "CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n";

    Project manual;
    manual.id = "manual-cre";
    manual.name = "CRE";
    manual.status = ProjectStatus::Backlog;
    manual.auto_discovered = false;
    manual.source_path = repo.string();
    manual.source_root = base.string();
    store.add(manual);

    MonitoredRoot root;
    root.path = base.string();
    root.category = "Workspace";
    root.tags = {"Auto"};
    root.markerFiles = {"CMakeLists.txt"};

    syncAutoDiscoveredProjects(store, {root});

    expectEqInt(static_cast<int>(store.getAll().size()), 1, "sync should reuse manual project when source_path already exists");

    auto updated = store.findById("manual-cre");
    expect(updated.has_value(), "manual project should still exist after sync");
    expect((*updated)->auto_discovered, "manual project should absorb auto-discovered metadata");
    expectEqStr((*updated)->source_path, repo.string(), "reused project should preserve source_path");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testAnalyzeProjectGovernance() {
    const fs::path base = fs::temp_directory_path() / ("labgestao-governance-tests-" + ProjectStore::generateId());
    fs::create_directories(base / "docs" / "adr");
    fs::create_directories(base / "docs" / "architecture");
    fs::create_directories(base / "docs" / "dai");
    fs::create_directories(base / "policies");
    fs::create_directories(base / "mcp" / "contracts");
    fs::create_directories(base / "examples");

    std::ofstream(base / "docs" / "adr" / "ADR-0001.md") << "# ADR\n";
    std::ofstream(base / "docs" / "architecture" / "DDD.md") << "# DDD\n";
    std::ofstream(base / "docs" / "dai" / "DAI.md") << "# DAI\n";
    std::ofstream(base / "policies" / "approval_matrix.md") << "# Approval\n";
    std::ofstream(base / "policies" / "ai_usage_policy.md") << "# Policy\n";
    std::ofstream(base / "mcp" / "tool_schema.json") << "{}\n";
    std::ofstream(base / "mcp" / "contracts" / "modify_code.contract.json") << "{}\n";
    std::ofstream(base / "examples" / "evidence_log.json") << "{}\n";
    std::ofstream(base / "CONTRIBUTING.md") << "# Contributing\n";
    std::ofstream(base / "CODEOWNERS") << "* @owner\n";

    Project p;
    p.id = "gov-1";
    p.name = "Gov";
    p.source_path = base.string();

    const auto profile = analyzeProjectGovernance(p);
    expect(profile.analyzed, "governance profile should mark analysis");
    expect(profile.path_available, "governance profile should see source path");
    expect(profile.has_adr, "governance profile should detect ADR");
    expect(profile.has_ddd, "governance profile should detect DDD");
    expect(profile.has_dai, "governance profile should detect DAI");
    expect(profile.has_policies, "governance profile should detect policies");
    expect(profile.has_tool_contracts, "governance profile should detect tool contracts");
    expect(profile.has_approval_policy, "governance profile should detect approval policy");
    expect(profile.has_audit_evidence, "governance profile should detect audit evidence");
    expectEqInt(profile.governance_signals, 6, "governance profile signals");
    expect(profile.maturity_score >= 90, "governance profile should produce high maturity");

    std::error_code ec;
    fs::remove_all(base, ec);
}

void testRefreshProjectGovernancePersistsProfile() {
    const fs::path base = fs::temp_directory_path() / ("labgestao-refresh-governance-tests-" + ProjectStore::generateId());
    fs::create_directories(base / "policies");
    std::ofstream(base / "policies" / "approval_matrix.md") << "# Approval\n";

    ProjectStore store;
    Project p;
    p.id = "gov-refresh";
    p.name = "Gov Refresh";
    p.source_path = base.string();
    store.add(p);

    expect(refreshProjectGovernance(store, "gov-refresh"), "refresh governance should report change");
    auto updated = store.findById("gov-refresh");
    expect(updated.has_value(), "updated governance project should exist");
    expect((*updated)->governance_profile.analyzed, "governance profile should persist on project");
    expect((*updated)->governance_profile.has_policies, "governance profile should persist detected policies");

    std::error_code ec;
    fs::remove_all(base, ec);
}

} // namespace

int main() {
    testStatusRules();
    testProjectMetrics();
    testGlobalMetrics();
    testJsonRoundtrip();
    testProjectStoreSourcePathUniqueness();
    testProjectStoreDuplicateNameHelper();
    testProjectStoreUpdateBlocksDuplicateSourcePath();
    testProjectStoreLoadDeduplicatesLegacySourcePathCollisions();
    testScaffold();
    testApplicationReclassifyFromDaiSignals();
    testApplicationReclassifyRespectsInvariants();
    testApplicationSyncAutoDiscoveredProjects();
    testApplicationSyncReusesManualProjectWithSamePath();
    testAnalyzeProjectGovernance();
    testRefreshProjectGovernancePersistsProfile();

    if (g_failures == 0) {
        std::cout << "All domain tests passed.\n";
        return 0;
    }

    std::cerr << g_failures << " test assertion(s) failed.\n";
    return 1;
}
