#pragma once

// Canvas dimensions shared by the authored HUD and its world-camera safe area.
// Keep these in Slate units; viewport DPI and the player's UI scale are applied
// once by the camera, so model silhouettes stay clear on every display size.
namespace RiftBattleLayout
{
    constexpr float HeaderBottom=68.f;
    constexpr float HandWidth=640.f;
    constexpr float HandHeight=156.f;
    constexpr float HandBottomGutter=12.f;
    constexpr float HandTop=HandHeight+HandBottomGutter;
    constexpr float NormalLeftSidebar=268.f;
    constexpr float NormalRightSidebar=268.f;
    constexpr float DeveloperRightSidebar=326.f;
    constexpr float ReplayHeaderBottom=84.f;
    constexpr float ReplayDockTop=158.f;
    constexpr float WorldGutter=14.f;
    constexpr float SideGutter=12.f;
}
