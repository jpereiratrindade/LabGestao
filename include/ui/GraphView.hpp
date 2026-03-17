#pragma once
#include "domain/ProjectStore.hpp"
#include "imgui.h"
#include <unordered_map>
#include <vector>

namespace labgestao {

class GraphView {
public:
    struct InventorySignal {
        std::string path;
        int scoreTotal{0};
        int scoreOperational{0};
        int scoreMaturity{0};
        int scoreReliability{0};
        int artifacts{0};
        bool hasAdr{false};
        bool hasDdd{false};
        bool hasDai{false};
        bool hasAsanUbsan{false};
        bool hasStaticAnalysis{false};
        bool hasLeakCheck{false};
    };

    explicit GraphView(ProjectStore& store);
    void setInventorySignals(const std::vector<InventorySignal>& signals);
    void render();

private:
    void applyForceLayout(float dt);
    void drawGraph(ImDrawList* draw, ImVec2 origin, float scale, ImVec2 canvasPos);
    const InventorySignal* findSignalForProject(const Project& p) const;
    bool isProjectVisible(const Project& p) const;
    float nodeRadiusForProject(const Project& p, float baseRadius) const;

    ProjectStore& m_store;
    std::unordered_map<std::string, InventorySignal> m_inventoryByCanonicalPath;

    float m_panX{0.f}, m_panY{0.f};
    float m_scale{1.f};
    std::string m_hoveredId;
    std::string m_selectedId;
    int   m_minScoreVisible{0};
    bool  m_onlyCritical{false};
    int   m_criticalThreshold{40};
    bool  m_layoutInitialized{false};
    bool  m_layoutRunning{true};
};

} // namespace labgestao
