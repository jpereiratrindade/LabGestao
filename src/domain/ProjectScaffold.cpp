#include "domain/ProjectScaffold.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace labgestao {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string slugify(const std::string& raw, bool snakeCase) {
    std::string out;
    out.reserve(raw.size());
    bool lastUnderscore = false;
    for (unsigned char c : raw) {
        const char lc = static_cast<char>(std::tolower(c));
        const bool isAlnum = std::isalnum(c) != 0;
        if (isAlnum) {
            out.push_back(lc);
            lastUnderscore = false;
            continue;
        }
        if (lc == '-' || lc == '_' || std::isspace(c)) {
            if (!lastUnderscore && !out.empty()) {
                out.push_back('_');
                lastUnderscore = true;
            }
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) return out;
    if (!std::isalpha(static_cast<unsigned char>(out.front()))) out.insert(out.begin(), 'p');
    if (!snakeCase) {
        std::replace(out.begin(), out.end(), '_', '-');
    }
    return out;
}

bool writeTextFile(const fs::path& path, const std::string& contents, std::string* err) {
    std::ofstream out(path);
    if (!out.is_open()) {
        if (err) *err = "nao foi possivel abrir arquivo para escrita: " + path.string();
        return false;
    }
    out << contents;
    if (!out.good()) {
        if (err) *err = "erro ao escrever arquivo: " + path.string();
        return false;
    }
    return true;
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
            continue;
        }
        quoted.push_back(ch);
    }
    quoted += "'";
    return quoted;
}

fs::path resolveGovernanceScript(std::string* err) {
    if (const char* overridePath = std::getenv("LABGESTAO_AI_GOV_SCRIPT")) {
        const fs::path overrideCandidate = fs::path(overridePath);
        if (fs::exists(overrideCandidate) && fs::is_regular_file(overrideCandidate)) {
            return overrideCandidate;
        }
        if (err) *err = "script configurado em LABGESTAO_AI_GOV_SCRIPT nao foi encontrado";
        return {};
    }

    const fs::path cwd = fs::current_path();
    const std::array<fs::path, 5> candidates = {
        cwd / "init_ai_governance.sh",
        cwd / "../init_ai_governance.sh",
        cwd / "../../init_ai_governance.sh",
        cwd / "../../../init_ai_governance.sh",
        cwd / "../../../../init_ai_governance.sh"
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        const fs::path normalized = fs::weakly_canonical(candidate, ec);
        if (!ec && fs::exists(normalized) && fs::is_regular_file(normalized)) {
            return normalized;
        }
    }

    if (err) *err = "init_ai_governance.sh nao foi encontrado; defina LABGESTAO_AI_GOV_SCRIPT se necessario";
    return {};
}

ScaffoldResult createGovernedCppProject(const fs::path& projectDir) {
    ScaffoldResult result;

    std::string err;
    const fs::path scriptPath = resolveGovernanceScript(&err);
    if (scriptPath.empty()) {
        result.message = err;
        return result;
    }

    const std::string command = "bash " + shellQuote(scriptPath.string()) + " " + shellQuote(projectDir.string());
    const int rc = std::system(command.c_str());
    if (rc != 0) {
        result.message = "script de bootstrap governado falhou com codigo " + std::to_string(rc);
        return result;
    }

    result.ok = true;
    result.projectDir = projectDir.string();
    result.message = "projeto governado criado com sucesso";
    return result;
}

std::string cppReadme(const std::string& projectName) {
    return "# " + projectName + "\n\nProjeto C++ gerado pelo LabGestao.\n";
}

std::string cppCmake(const std::string& projectSlug) {
    return "cmake_minimum_required(VERSION 3.20)\n"
           "project(" + projectSlug + " VERSION 0.1.0 LANGUAGES CXX)\n\n"
           "set(CMAKE_CXX_STANDARD 20)\n"
           "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
           "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
           "add_executable(" + projectSlug + "\n"
           "    src/main.cpp\n"
           ")\n\n"
           "target_include_directories(" + projectSlug + " PRIVATE include)\n";
}

std::string cppMain(const std::string& projectName) {
    return "#include <iostream>\n\n"
           "int main() {\n"
           "    std::cout << \"Hello from " + projectName + "!\" << std::endl;\n"
           "    return 0;\n"
           "}\n";
}

std::string cppGitignore() {
    return "build/\n"
           "build-*/\n"
           ".vscode/\n"
           ".idea/\n"
           "*.o\n"
           "*.obj\n"
           "*.exe\n"
           "*.out\n";
}

std::string pyReadme(const std::string& projectName) {
    return "# " + projectName + "\n\nProjeto Python gerado pelo LabGestao.\n";
}

std::string pyProjectToml(const std::string& projectSlug, const std::string& packageName) {
    return "[build-system]\n"
           "requires = [\"setuptools>=68\", \"wheel\"]\n"
           "build-backend = \"setuptools.build_meta\"\n\n"
           "[project]\n"
           "name = \"" + projectSlug + "\"\n"
           "version = \"0.1.0\"\n"
           "description = \"Projeto Python criado pelo LabGestao\"\n"
           "readme = \"README.md\"\n"
           "requires-python = \">=3.10\"\n"
           "dependencies = []\n\n"
           "[project.scripts]\n"
           + projectSlug + " = \"" + packageName + ".main:main\"\n\n"
           "[tool.setuptools]\n"
           "package-dir = {\"\" = \"src\"}\n\n"
           "[tool.setuptools.packages.find]\n"
           "where = [\"src\"]\n";
}

std::string pyInit() {
    return "__all__ = []\n";
}

std::string pyMain() {
    return "def main() -> None:\n"
           "    print(\"Hello from LabGestao scaffold\")\n\n"
           "if __name__ == \"__main__\":\n"
           "    main()\n";
}

std::string pyTest() {
    return "def test_smoke() -> None:\n"
           "    assert True\n";
}

std::string pyGitignore() {
    return "__pycache__/\n"
           "*.pyc\n"
           ".pytest_cache/\n"
           ".venv/\n"
           "dist/\n"
           "build/\n"
           ".mypy_cache/\n";
}

} // namespace

std::string projectTemplateLabel(ProjectTemplate templ) {
    switch (templ) {
        case ProjectTemplate::Cpp: return "C++";
        case ProjectTemplate::Python: return "Python";
        case ProjectTemplate::GovernedCpp: return "C++ Governado";
        case ProjectTemplate::None: break;
    }
    return "Nenhum";
}

ScaffoldResult createProjectScaffold(const ScaffoldRequest& req) {
    ScaffoldResult result;
    const std::string projectName = trim(req.projectName);
    if (projectName.empty()) {
        result.message = "nome do projeto vazio";
        return result;
    }
    if (req.templ == ProjectTemplate::None) {
        result.ok = true;
        result.message = "template desabilitado";
        return result;
    }
    if (trim(req.baseDirectory).empty()) {
        result.message = "diretorio base vazio";
        return result;
    }

    const std::string folderSlug = slugify(projectName, true);
    if (folderSlug.empty()) {
        result.message = "nome do projeto invalido para pasta";
        return result;
    }

    const fs::path baseDir = fs::path(req.baseDirectory);
    const fs::path projectDir = baseDir / folderSlug;
    std::error_code ec;
    fs::create_directories(baseDir, ec);
    if (ec) {
        result.message = "falha ao criar diretorio base";
        return result;
    }
    if (fs::exists(projectDir)) {
        result.message = "diretorio de projeto ja existe: " + projectDir.string();
        return result;
    }
    std::string err;
    const std::string projectSlug = slugify(projectName, false);
    const std::string packageName = slugify(projectName, true);

    if (req.templ == ProjectTemplate::GovernedCpp) {
        return createGovernedCppProject(projectDir);
    }

    fs::create_directories(projectDir, ec);
    if (ec) {
        result.message = "falha ao criar diretorio do projeto";
        return result;
    }

    if (req.templ == ProjectTemplate::Cpp) {
        fs::create_directories(projectDir / "src", ec);
        fs::create_directories(projectDir / "include", ec);
        fs::create_directories(projectDir / "tests", ec);
        if (ec) {
            result.message = "falha ao criar estrutura C++";
            return result;
        }
        if (!writeTextFile(projectDir / "README.md", cppReadme(projectName), &err) ||
            !writeTextFile(projectDir / "CMakeLists.txt", cppCmake(projectSlug), &err) ||
            !writeTextFile(projectDir / ".gitignore", cppGitignore(), &err) ||
            !writeTextFile(projectDir / "src/main.cpp", cppMain(projectName), &err) ||
            !writeTextFile(projectDir / "tests/.gitkeep", "", &err)) {
            result.message = err;
            return result;
        }
    }

    if (req.templ == ProjectTemplate::Python) {
        fs::create_directories(projectDir / "src" / packageName, ec);
        fs::create_directories(projectDir / "tests", ec);
        if (ec) {
            result.message = "falha ao criar estrutura Python";
            return result;
        }
        if (!writeTextFile(projectDir / "README.md", pyReadme(projectName), &err) ||
            !writeTextFile(projectDir / "pyproject.toml", pyProjectToml(projectSlug, packageName), &err) ||
            !writeTextFile(projectDir / ".gitignore", pyGitignore(), &err) ||
            !writeTextFile(projectDir / "src" / packageName / "__init__.py", pyInit(), &err) ||
            !writeTextFile(projectDir / "src" / packageName / "main.py", pyMain(), &err) ||
            !writeTextFile(projectDir / "tests/test_smoke.py", pyTest(), &err)) {
            result.message = err;
            return result;
        }
    }

    result.ok = true;
    result.projectDir = projectDir.string();
    result.message = "estrutura criada com sucesso";
    return result;
}

} // namespace labgestao
