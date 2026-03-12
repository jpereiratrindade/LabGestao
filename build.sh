#!/usr/bin/env bash
# ── LabGestao Build Script ────────────────────────────────────────────────────
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

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
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando..."
cmake --build build -- -j$(nproc)

echo ""
echo "✅  Build concluído!"
echo "   Executar: cd build && ./LabGestao"
