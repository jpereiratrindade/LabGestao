cmake_minimum_required(VERSION 3.20)
project(LabGestao VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
# nlohmann/json — header-only, vendored
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# ── Dear ImGui (vendored) ─────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Definido no imconfig.h para consistência e evitar warnings
# target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)

target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# ── Domain library ────────────────────────────────────────────────────────────
add_library(labgestao_domain STATIC
    src/domain/ProjectStore.cpp
    src/domain/ProjectScaffold.cpp
)

target_include_directories(labgestao_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(labgestao_domain PUBLIC nlohmann_json::nlohmann_json)

# ── Application library ───────────────────────────────────────────────────────
add_library(labgestao_application STATIC
    src/app/ApplicationServices.cpp
)

target_include_directories(labgestao_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(labgestao_application PUBLIC labgestao_domain)

# ── UI library ────────────────────────────────────────────────────────────────
add_library(labgestao_ui STATIC
    src/ui/AppUI.cpp
    src/ui/ListView.cpp
    src/ui/KanbanView.cpp
    src/ui/GraphView.cpp
)

target_include_directories(labgestao_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(labgestao_ui PUBLIC labgestao_domain labgestao_application imgui)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(LabGestao
    src/main.cpp
    src/app/AppRuntime.cpp
)

target_link_libraries(LabGestao PRIVATE labgestao_ui)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_executable(LabGestaoDomainTests
    tests/domain/DomainTests.cpp
)

target_link_libraries(LabGestaoDomainTests PRIVATE labgestao_domain)

add_test(
    NAME domain_tests
    COMMAND LabGestaoDomainTests
)
