#include "ui/GraphView.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <filesystem>

namespace labgestao {
namespace fs = std::filesystem;

static const ImVec4 kNodeColors[] = {
    {0.45f, 0.25f, 0.75f, 1.f},  // Backlog
    {0.20f, 0.55f, 0.90f, 1.f},  // Doing
    {0.90f, 0.70f, 0.10f, 1.f},  // Review
    {0.20f, 0.80f, 0.40f, 1.f},  // Done
    {0.50f, 0.50f, 0.50f, 1.f},  // Paused
};

GraphView::GraphView(ProjectStore& store) : m_store(store) {}

namespace {
std::string canonicalPathOrRaw(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        ec.clear();
        canon = fs::absolute(fs::path(path), ec);
    }
    if (ec) return fs::path(path).lexically_normal().string();
    return canon.lexically_normal().string();
}

ImU32 scoreBorderColor(int scoreTotal) {
    if (scoreTotal >= 80) return IM_COL32(80, 220, 120, 255);   // good
    if (scoreTotal >= 60) return IM_COL32(240, 220, 90, 255);   // warning
    if (scoreTotal >= 40) return IM_COL32(240, 160, 70, 255);   // attention
    return IM_COL32(235, 90, 90, 255);                          // critical
}
} // namespace

void GraphView::setInventorySignals(const std::vector<InventorySignal>& signals) {
    m_inventoryByCanonicalPath.clear();
    m_inventoryByCanonicalPath.reserve(signals.size());
    for (const auto& signal : signals) {
        const std::string key = canonicalPathOrRaw(signal.path);
        if (key.empty()) continue;
        m_inventoryByCanonicalPath[key] = signal;
    }
}

const GraphView::InventorySignal* GraphView::findSignalForProject(const Project& p) const {
    if (p.source_path.empty()) return nullptr;
    const std::string key = canonicalPathOrRaw(p.source_path);
    if (key.empty()) return nullptr;
    const auto it = m_inventoryByCanonicalPath.find(key);
    if (it == m_inventoryByCanonicalPath.end()) return nullptr;
    return &it->second;
}

bool GraphView::isProjectVisible(const Project& p) const {
    const InventorySignal* signal = findSignalForProject(p);
    if (!signal) {
        if (m_onlyCritical) return false;
        return m_minScoreVisible <= 0;
    }
    if (signal->scoreTotal < m_minScoreVisible) return false;
    if (m_onlyCritical && signal->scoreTotal >= m_criticalThreshold) return false;
    return true;
}

float GraphView::nodeRadiusForProject(const Project& p, float baseRadius) const {
    const InventorySignal* signal = findSignalForProject(p);
    if (!signal) return baseRadius;
    if (signal->scoreTotal < m_criticalThreshold) return baseRadius * 1.25f;
    return baseRadius;
}

// ── Force-directed layout ─────────────────────────────────────────────────────
void GraphView::applyForceLayout(float dt) {
    auto& projs = m_store.getAll();
    if (projs.empty()) return;

    // Initialize positions randomly if first run
    if (!m_layoutInitialized) {
        std::mt19937 rng{42};
        std::uniform_real_distribution<float> distX(100.f, 700.f);
        std::uniform_real_distribution<float> distY(80.f,  500.f);
        for (auto& p : projs) {
            if (p.graph_x == 0.f && p.graph_y == 0.f) {
                p.graph_x = distX(rng);
                p.graph_y = distY(rng);
            }
            p.graph_vx = 0.f; p.graph_vy = 0.f;
        }
        m_layoutInitialized = true;
    }

    if (!m_layoutRunning) return;

    const float repulsion  = 8000.f;
    const float attraction = 0.04f;
    const float damping    = 0.85f;
    const float idealLen   = 160.f;

    // Reset forces
    for (auto& p : projs) { p.graph_vx = 0.f; p.graph_vy = 0.f; }

    // Repulsion between all pairs
    for (size_t i = 0; i < projs.size(); ++i) {
        for (size_t j = i + 1; j < projs.size(); ++j) {
            float dx = projs[j].graph_x - projs[i].graph_x;
            float dy = projs[j].graph_y - projs[i].graph_y;
            float d2 = dx * dx + dy * dy + 1.f;
            float force = repulsion / d2;
            float fx = force * dx / std::sqrt(d2);
            float fy = force * dy / std::sqrt(d2);
            projs[i].graph_vx -= fx;
            projs[i].graph_vy -= fy;
            projs[j].graph_vx += fx;
            projs[j].graph_vy += fy;
        }
    }

    // Attraction along edges
    for (size_t i = 0; i < projs.size(); ++i) {
        for (const auto& cid : projs[i].connections) {
            for (size_t j = 0; j < projs.size(); ++j) {
                if (projs[j].id != cid) continue;
                float dx   = projs[j].graph_x - projs[i].graph_x;
                float dy   = projs[j].graph_y - projs[i].graph_y;
                float dist = std::sqrt(dx*dx + dy*dy) + 0.01f;
                float force = attraction * (dist - idealLen);
                projs[i].graph_vx += force * dx / dist;
                projs[i].graph_vy += force * dy / dist;
                projs[j].graph_vx -= force * dx / dist;
                projs[j].graph_vy -= force * dy / dist;
            }
        }
    }

    // Integrate + dampen
    for (auto& p : projs) {
        p.graph_x += p.graph_vx * dt * damping;
        p.graph_y += p.graph_vy * dt * damping;
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────
void GraphView::drawGraph(ImDrawList* draw, ImVec2 origin, float scale, ImVec2 canvasPos) {
    auto& projs = m_store.getAll();

    // Edges
    for (const auto& p : projs) {
        if (!isProjectVisible(p)) continue;
        for (const auto& cid : p.connections) {
            for (const auto& q : projs) {
                if (q.id != cid) continue;
                if (!isProjectVisible(q)) continue;
                ImVec2 a = { canvasPos.x + origin.x + p.graph_x * scale,
                             canvasPos.y + origin.y + p.graph_y * scale };
                ImVec2 b = { canvasPos.x + origin.x + q.graph_x * scale,
                             canvasPos.y + origin.y + q.graph_y * scale };
                draw->AddLine(a, b, IM_COL32(100, 130, 180, 140), 1.5f);
            }
        }
    }

    // Nodes
    const float baseR = 22.f * scale;
    for (const auto& p : projs) {
        if (!isProjectVisible(p)) continue;
        ImVec2 center = {
            canvasPos.x + origin.x + p.graph_x * scale,
            canvasPos.y + origin.y + p.graph_y * scale
        };
        const float R = nodeRadiusForProject(p, baseR);
        int ci = static_cast<int>(p.status);
        ImVec4 cv = kNodeColors[ci];
        ImU32 col = IM_COL32(int(cv.x*255), int(cv.y*255), int(cv.z*255), 220);
        const InventorySignal* signal = findSignalForProject(p);
        ImU32 qualityBorder = signal ? scoreBorderColor(signal->scoreTotal) : IM_COL32(200, 200, 200, 80);
        ImU32 colBorder = (p.id == m_selectedId) ? IM_COL32(255, 255, 100, 255) : qualityBorder;

        draw->AddCircleFilled(center, R, col, 32);
        draw->AddCircle(center, R, colBorder, 32, 2.f);

        // Label
        ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
        float maxLabelW = 100.f;
        if (ts.x > maxLabelW) ts.x = maxLabelW;
        draw->AddText(
            nullptr, 0.f,
            { center.x - ts.x * 0.5f, center.y + R + 4.f },
            IM_COL32(220, 220, 240, 220),
            p.name.c_str(), p.name.c_str() + std::min(p.name.size(), size_t(24))
        );
    }
}

// ── Main render ───────────────────────────────────────────────────────────────
void GraphView::render() {
    auto& projs = m_store.getAll();

    // Toolbar
    ImGui::Checkbox("Layout automático", &m_layoutRunning);
    ImGui::SameLine();
    if (ImGui::Button("Reorganizar")) {
        m_layoutInitialized = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Scroll = zoom  |  Arrastar fundo = pan  |  Clique = detalhes");
    ImGui::SetNextItemWidth(120.f);
    ImGui::SliderInt("Total Min", &m_minScoreVisible, 0, 100);
    ImGui::SameLine();
    ImGui::Checkbox("So criticos", &m_onlyCritical);
    ImGui::TextDisabled("Borda por score total: verde>=80, amarelo>=60, laranja>=40, vermelho<40.");
    ImGui::TextDisabled("Nos criticos (<%d) aparecem maiores.", m_criticalThreshold);

    // Connection editor for selected node
    if (!m_selectedId.empty()) {
        auto opt = m_store.findById(m_selectedId);
        if (opt && isProjectVisible(**opt)) {
            Project& sel = **opt;
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.f, 1.f), "Nó: %s", sel.name.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("- conectar a:");

            const float itemSpacingX = 14.f;
            const float innerSpacingX = 4.f;
            const float lineStartX = ImGui::GetCursorScreenPos().x;
            const float lineMaxX = lineStartX + ImGui::GetContentRegionAvail().x;
            bool firstItem = true;
            for (auto& q : projs) {
                if (q.id == sel.id) continue;
                bool connected = std::find(sel.connections.begin(), sel.connections.end(), q.id) != sel.connections.end();
                const float checkboxW = ImGui::GetFrameHeight();
                const float labelW = ImGui::CalcTextSize(q.name.c_str()).x;
                const float itemW = checkboxW + innerSpacingX + labelW;

                if (!firstItem) {
                    const float curX = ImGui::GetCursorScreenPos().x;
                    if (curX + itemW > lineMaxX) {
                        ImGui::NewLine();
                    }
                }

                ImGui::BeginGroup();
                if (ImGui::Checkbox(("##conn_" + q.id).c_str(), &connected)) {
                    if (connected) {
                        sel.connections.push_back(q.id);
                        // bidirec
                        if (std::find(q.connections.begin(), q.connections.end(), sel.id) == q.connections.end())
                            q.connections.push_back(sel.id);
                    } else {
                        sel.connections.erase(std::remove(sel.connections.begin(), sel.connections.end(), q.id), sel.connections.end());
                        q.connections.erase(std::remove(q.connections.begin(), q.connections.end(), sel.id), q.connections.end());
                    }
                    m_store.update(sel);
                    m_store.update(q);
                }
                ImGui::SameLine(0.f, innerSpacingX);
                ImGui::TextUnformatted(q.name.c_str());
                ImGui::EndGroup();

                ImGui::SameLine(0.f, itemSpacingX);
                firstItem = false;
            }
            ImGui::NewLine();
        } else {
            m_selectedId.clear();
        }
    }

    ImGui::Separator();

    // Canvas
    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50.f || canvasSize.y < 50.f) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Background
    draw->AddRectFilled(canvasPos, { canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y },
        IM_COL32(10, 12, 18, 255));
    draw->AddRect(canvasPos, { canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y },
        IM_COL32(50, 60, 80, 200));

    // Subtle grid
    for (float gx = std::fmod(m_panX * m_scale, 60.f); gx < canvasSize.x; gx += 60.f)
        draw->AddLine({ canvasPos.x + gx, canvasPos.y },
                      { canvasPos.x + gx, canvasPos.y + canvasSize.y },
                      IM_COL32(30, 35, 48, 180));
    for (float gy = std::fmod(m_panY * m_scale, 60.f); gy < canvasSize.y; gy += 60.f)
        draw->AddLine({ canvasPos.x, canvasPos.y + gy },
                      { canvasPos.x + canvasSize.x, canvasPos.y + gy },
                      IM_COL32(30, 35, 48, 180));

    // Clip to canvas
    draw->PushClipRect(canvasPos, { canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y }, true);

    // Force layout step
    applyForceLayout(ImGui::GetIO().DeltaTime);

    // Draw
    ImVec2 origin = { m_panX, m_panY };
    drawGraph(draw, origin, m_scale, canvasPos);
    draw->PopClipRect();

    // Interaction: invisible button over canvas
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    // Pan
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_panX += delta.x / m_scale;
        m_panY += delta.y / m_scale;
    }

    // Zoom
    if (hovered && ImGui::GetIO().MouseWheel != 0.f) {
        float factor = (ImGui::GetIO().MouseWheel > 0) ? 1.1f : 0.9f;
        m_scale = std::clamp(m_scale * factor, 0.2f, 4.f);
    }

    // Click on node
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        m_selectedId.clear();
        for (const auto& p : projs) {
            if (!isProjectVisible(p)) continue;
            float px = canvasPos.x + origin.x + p.graph_x * m_scale;
            float py = canvasPos.y + origin.y + p.graph_y * m_scale;
            const float R = nodeRadiusForProject(p, 22.f * m_scale);
            float dx = mp.x - px, dy = mp.y - py;
            if (dx*dx + dy*dy < R*R) {
                m_selectedId = p.id;
                break;
            }
        }
    }

    // Tooltip on hover
    if (hovered) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        m_hoveredId.clear();
        for (const auto& p : projs) {
            if (!isProjectVisible(p)) continue;
            float px = canvasPos.x + origin.x + p.graph_x * m_scale;
            float py = canvasPos.y + origin.y + p.graph_y * m_scale;
            const float R = nodeRadiusForProject(p, 22.f * m_scale);
            float dx = mp.x - px, dy = mp.y - py;
            if (dx*dx + dy*dy < R*R) {
                m_hoveredId = p.id;
                ImGui::BeginTooltip();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.f, 1.f), "%s", p.name.c_str());
                ImGui::TextDisabled("%s", statusToString(p.status).c_str());
                if (const InventorySignal* signal = findSignalForProject(p)) {
                    ImGui::Separator();
                    ImGui::Text("Score total: %d", signal->scoreTotal);
                    ImGui::Text("Operacional: %d | Maturidade: %d | Confiabilidade: %d",
                        signal->scoreOperational, signal->scoreMaturity, signal->scoreReliability);
                    ImGui::Text("Artefatos: %d", signal->artifacts);
                    ImGui::TextDisabled("ADR:%s  DDD:%s  DAI:%s",
                        signal->hasAdr ? "OK" : "-",
                        signal->hasDdd ? "OK" : "-",
                        signal->hasDai ? "OK" : "-");
                    ImGui::TextDisabled("ASan/UB:%s  Static:%s  Leak:%s",
                        signal->hasAsanUbsan ? "OK" : "-",
                        signal->hasStaticAnalysis ? "OK" : "-",
                        signal->hasLeakCheck ? "OK" : "-");
                }
                if (!p.description.empty()) ImGui::TextWrapped("%s", p.description.c_str());
                ImGui::EndTooltip();
                break;
            }
        }
    }
}

} // namespace labgestao
