#pragma once
#include "domain/ProjectStore.hpp"
#include "imgui.h"

namespace labgestao {

class GraphView {
public:
    explicit GraphView(ProjectStore& store);
    void render();

private:
    void applyForceLayout(float dt);
    void drawGraph(ImDrawList* draw, ImVec2 origin, float scale, ImVec2 canvasPos);

    ProjectStore& m_store;

    float m_panX{0.f}, m_panY{0.f};
    float m_scale{1.f};
    std::string m_hoveredId;
    std::string m_selectedId;
    bool  m_layoutInitialized{false};
    bool  m_layoutRunning{true};
};

} // namespace labgestao
