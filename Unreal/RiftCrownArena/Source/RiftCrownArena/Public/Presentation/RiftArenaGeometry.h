#pragma once
#include "Simulation/RiftSimulation.h"

// World-only margins around the authoritative tile field. Decoration and the
// enlarged model envelope must grow with the board without changing unit scale.
namespace RiftArenaGeometry
{
    constexpr float UnitsPerTile=100.f;
    constexpr int GroundSideMargin=5, GroundRearMargin=3;
    constexpr int GroundHalfWidth=rift::arena::HalfWidth+GroundSideMargin;
    constexpr int GroundHalfHeight=rift::arena::HalfHeight+GroundRearMargin;
    constexpr float ModelReach=3.5f*UnitsPerTile;
    constexpr float ModelHalfWidth=rift::arena::HalfWidth*UnitsPerTile+ModelReach;
    constexpr float ModelHalfHeight=rift::arena::HalfHeight*UnitsPerTile+ModelReach;
    constexpr float ModelHeight=620.f;
    constexpr float CoreHalfWidth=350.f;
    constexpr float CoreHalfDepth=(rift::arena::CoreDepth+3.7)*UnitsPerTile;
}
