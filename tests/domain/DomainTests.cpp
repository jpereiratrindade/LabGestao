#include "domain/Project.hpp"
#include "domain/ProjectScaffold.hpp"
#include "domain/ProjectStore.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using labgestao::Project;
using labgestao::ProjectStatus;
using labgestao::ProjectStore;
using labgestao::ProjectTemplate;
using labgestao::ScaffoldRequest;

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

    std::error_code ec;
    fs::remove_all(tmpDir, ec);
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

} // namespace

int main() {
    testStatusRules();
    testProjectMetrics();
    testGlobalMetrics();
    testJsonRoundtrip();
    testScaffold();

    if (g_failures == 0) {
        std::cout << "All domain tests passed.\n";
        return 0;
    }

    std::cerr << g_failures << " test assertion(s) failed.\n";
    return 1;
}
