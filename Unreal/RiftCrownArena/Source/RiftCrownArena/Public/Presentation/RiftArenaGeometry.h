#pragma once
#include "Simulation/RiftSimulation.h"

// World-only margins around the authoritative tile field. Decoration and the
// enlarged model envelope must grow with the board without changing unit scale.
namespace RiftArenaGeometry
{
    constexpr float AuthoredUnitsPerTile=100.f;
    constexpr float UnitsPerTile=125.f;
    constexpr float WorldScale=UnitsPerTile/AuthoredUnitsPerTile;
    constexpr int GroundSideMargin=5, GroundRearMargin=3;
    constexpr int GroundHalfWidth=rift::arena::HalfWidth+GroundSideMargin;
    constexpr int GroundHalfHeight=rift::arena::HalfHeight+GroundRearMargin;
    constexpr float ModelReach=3.5f*UnitsPerTile;
    constexpr float ModelHalfWidth=rift::arena::HalfWidth*UnitsPerTile+ModelReach;
    constexpr float ModelHalfHeight=rift::arena::HalfHeight*UnitsPerTile+ModelReach;
    constexpr float ModelHeight=620.f*WorldScale;
    constexpr float CoreHalfWidth=350.f*WorldScale;
    constexpr float CoreHalfDepth=(rift::arena::CoreDepth+3.7)*UnitsPerTile;
}
