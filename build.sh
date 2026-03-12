#!/usr/bin/env bash
# ── LabGestao Build Script ────────────────────────────────────────────────────
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

require_command() {
    local cmd="$1"
    local pkg_hint="$2"

    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Erro: comando obrigatorio ausente: $cmd"
        if [ -n "$pkg_hint" ]; then
            echo "Instale no Silverblue com:"
            echo "  toolbox enter"
            echo "  sudo dnf install $pkg_hint"
        fi
        exit 1
    fi
}

require_command cmake "cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl"
require_command c++ "cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl"
require_command curl "cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl"

if command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists sdl2; then
    echo "Erro: SDL2 de desenvolvimento nao foi encontrado via pkg-config."
    echo "No Fedora Silverblue, prefira instalar em um toolbox:"
    echo "  toolbox create"
    echo "  toolbox enter"
    echo "  sudo dnf install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl"
    exit 1
fi

BUILD_DIR="build"
CMAKE_GENERATOR_ARGS=()
CACHED_GENERATOR=""
CACHED_MAKE_PROGRAM=""

if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)"
    CACHED_MAKE_PROGRAM="$(sed -n 's/^CMAKE_MAKE_PROGRAM:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)"
fi

if [ -n "$CACHED_MAKE_PROGRAM" ] && [ ! -x "$CACHED_MAKE_PROGRAM" ] && command -v ninja >/dev/null 2>&1; then
    echo "==> Cache antigo do CMake referencia um gerador indisponivel: $CACHED_MAKE_PROGRAM"
    echo "==> Usando diretorio limpo com Ninja em build-ninja/"
    BUILD_DIR="build-ninja"
    CACHED_GENERATOR=""
fi

if [ -n "$CACHED_GENERATOR" ]; then
    CMAKE_GENERATOR_ARGS=(-G "$CACHED_GENERATOR")
elif command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G Ninja)
elif command -v make >/dev/null 2>&1 || command -v gmake >/dev/null 2>&1; then
    CMAKE_GENERATOR_ARGS=(-G "Unix Makefiles")
else
    echo "Erro: nenhum gerador de build encontrado."
    echo "Instale um destes pacotes no Silverblue:"
    echo "  toolbox enter"
    echo "  sudo dnf install ninja-build"
    echo "ou"
    echo "  sudo dnf install make"
    exit 1
fi

echo "==> Verificando ImGui vendor..."
if [ ! -f "vendor/imgui/imgui.h" ]; then
    echo "==> Baixando Dear ImGui v1.91.9b..."
    mkdir -p vendor/imgui/backends
    BASE="https://raw.githubusercontent.com/ocornut/imgui/v1.91.9b"
    curl -sSfL -o vendor/imgui/imgui.h              "$BASE/imgui.h"
    curl -sSfL -o vendor/imgui/imgui.cpp            "$BASE/imgui.cpp"
    curl -sSfL -o vendor/imgui/imgui_internal.h     "$BASE/imgui_internal.h"
    curl -sSfL -o vendor/imgui/imgui_draw.cpp       "$BASE/imgui_draw.cpp"
    curl -sSfL -o vendor/imgui/imgui_tables.cpp     "$BASE/imgui_tables.cpp"
    curl -sSfL -o vendor/imgui/imgui_widgets.cpp    "$BASE/imgui_widgets.cpp"
    curl -sSfL -o vendor/imgui/imconfig.h           "$BASE/imconfig.h"
    curl -sSfL -o vendor/imgui/imstb_rectpack.h     "$BASE/imstb_rectpack.h"
    curl -sSfL -o vendor/imgui/imstb_textedit.h     "$BASE/imstb_textedit.h"
    curl -sSfL -o vendor/imgui/imstb_truetype.h     "$BASE/imstb_truetype.h"
    curl -sSfL -o vendor/imgui/backends/imgui_impl_sdl2.h   "$BASE/backends/imgui_impl_sdl2.h"
    curl -sSfL -o vendor/imgui/backends/imgui_impl_sdl2.cpp "$BASE/backends/imgui_impl_sdl2.cpp"
    curl -sSfL -o vendor/imgui/backends/imgui_impl_opengl3.h   "$BASE/backends/imgui_impl_opengl3.h"
    curl -sSfL -o vendor/imgui/backends/imgui_impl_opengl3.cpp "$BASE/backends/imgui_impl_opengl3.cpp"
    curl -sSfL -o vendor/imgui/backends/imgui_impl_opengl3_loader.h "$BASE/backends/imgui_impl_opengl3_loader.h"
    echo "==> ImGui baixado com sucesso."
else
    echo "==> ImGui já presente, pulando download."
fi

echo "==> Configurando CMake..."
cmake -S . -B "$BUILD_DIR" "${CMAKE_GENERATOR_ARGS[@]}" -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "Build concluido."
echo "   Executar: cd $BUILD_DIR && ./LabGestao"
