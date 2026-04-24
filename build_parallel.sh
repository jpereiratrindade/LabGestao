#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="${BUILD_DIR:-build-codex}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if command -v nproc >/dev/null 2>&1; then
    DEFAULT_JOBS="$(nproc)"
elif command -v getconf >/dev/null 2>&1; then
    DEFAULT_JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
else
    DEFAULT_JOBS=4
fi

JOBS="${JOBS:-$DEFAULT_JOBS}"

require_command() {
    local cmd="$1"
    local hint="$2"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Erro: comando obrigatorio ausente: $cmd"
        if [ -n "$hint" ]; then
            echo "$hint"
        fi
        exit 1
    fi
}

require_command cmake "Instale o CMake e um compilador C++ antes de compilar o LabGestao."
require_command c++ "Instale um compilador C++ antes de compilar o LabGestao."

if command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists sdl2; then
    echo "Erro: SDL2 de desenvolvimento nao foi encontrado via pkg-config."
    echo "No Fedora Silverblue, prefira instalar em um toolbox:"
    echo "  toolbox create"
    echo "  toolbox enter"
    echo "  sudo dnf install cmake gcc-c++ pkgconf-pkg-config SDL2-devel mesa-libGL-devel curl ninja-build"
    exit 1
fi

GENERATOR_ARGS=()
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)"
    if [ -n "$CACHED_GENERATOR" ]; then
        GENERATOR_ARGS=(-G "$CACHED_GENERATOR")
    fi
elif command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

echo "==> Configurando CMake em $BUILD_DIR ($BUILD_TYPE)"
cmake -S . -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "==> Compilando LabGestao com $JOBS jobs"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo "Build concluido."
echo "   Executar: $BUILD_DIR/LabGestao"
