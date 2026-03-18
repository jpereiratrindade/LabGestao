#pragma once

#include "domain/ProjectStore.hpp"

namespace labgestao::application {

struct ReclassifyKanbanAutoResult {
    int changed{0};
    int unchanged{0};
    int skipped{0};
};

ReclassifyKanbanAutoResult reclassifyKanbanAuto(ProjectStore& store);

} // namespace labgestao::application
