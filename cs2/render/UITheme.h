#pragma once

#include "../OS-ImGui/imgui/imgui.h"

namespace UITheme
{
    // === Core Colors ===
    constexpr ImVec4 Background       = ImVec4(0.059f, 0.059f, 0.078f, 1.00f);
    constexpr ImVec4 Surface          = ImVec4(0.086f, 0.086f, 0.118f, 1.00f);
    constexpr ImVec4 SurfaceHover     = ImVec4(0.118f, 0.118f, 0.165f, 1.00f);
    constexpr ImVec4 Border           = ImVec4(0.165f, 0.165f, 0.227f, 0.50f);
    constexpr ImVec4 BorderSubtle     = ImVec4(0.118f, 0.118f, 0.180f, 0.30f);

    // === Accent Colors ===
    constexpr ImVec4 Accent           = ImVec4(0.486f, 0.361f, 0.988f, 1.00f);
    constexpr ImVec4 AccentHover      = ImVec4(0.608f, 0.490f, 1.00f, 1.00f);
    constexpr ImVec4 AccentActive     = ImVec4(0.420f, 0.298f, 0.910f, 1.00f);
    constexpr ImVec4 AccentSubtle     = ImVec4(0.486f, 0.361f, 0.988f, 0.15f);
    constexpr ImVec4 AccentBg60       = ImVec4(0.486f, 0.361f, 0.988f, 0.60f);
    constexpr ImVec4 AccentBg80       = ImVec4(0.486f, 0.361f, 0.988f, 0.80f);
    constexpr ImVec4 AccentBg40       = ImVec4(0.486f, 0.361f, 0.988f, 0.40f);
    constexpr ImVec4 AccentBg20       = ImVec4(0.486f, 0.361f, 0.988f, 0.20f);
    constexpr ImVec4 AccentBg90       = ImVec4(0.486f, 0.361f, 0.988f, 0.90f);

    // === Semantic Colors ===
    constexpr ImVec4 Success          = ImVec4(0.290f, 0.871f, 0.502f, 1.00f);
    constexpr ImVec4 Warning          = ImVec4(0.984f, 0.749f, 0.141f, 1.00f);
    constexpr ImVec4 Danger           = ImVec4(0.973f, 0.443f, 0.443f, 1.00f);
    constexpr ImVec4 Info             = ImVec4(0.376f, 0.647f, 0.980f, 1.00f);

    constexpr ImVec4 DangerBg80       = ImVec4(0.973f, 0.443f, 0.443f, 0.80f);
    constexpr ImVec4 DangerBg90       = ImVec4(0.973f, 0.443f, 0.443f, 0.90f);

    // === Text Colors ===
    constexpr ImVec4 TextPrimary      = ImVec4(0.910f, 0.910f, 0.941f, 1.00f);
    constexpr ImVec4 TextSecondary    = ImVec4(0.533f, 0.533f, 0.627f, 1.00f);
    constexpr ImVec4 TextDisabled     = ImVec4(0.333f, 0.333f, 0.439f, 1.00f);
    constexpr ImVec4 TextAccent       = ImVec4(0.608f, 0.490f, 1.00f, 1.00f);
    constexpr ImVec4 TextWhite        = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr ImVec4 TextMuted        = ImVec4(0.40f, 0.40f, 0.50f, 1.0f);

    // === Frame Colors ===
    constexpr ImVec4 FrameBg          = ImVec4(0.086f, 0.086f, 0.118f, 1.00f);
    constexpr ImVec4 FrameBgHovered   = ImVec4(0.118f, 0.118f, 0.165f, 1.00f);
    constexpr ImVec4 FrameBgActive    = ImVec4(0.165f, 0.165f, 0.227f, 1.00f);

    // === Spacing ===
    constexpr float SpacingXS = 4.0f;
    constexpr float SpacingSM = 8.0f;
    constexpr float SpacingMD = 12.0f;
    constexpr float SpacingLG = 16.0f;
    constexpr float SpacingXL = 24.0f;

    constexpr ImVec2 WindowPadding    = ImVec2(8, 8);
    constexpr ImVec2 FramePadding     = ImVec2(6, 4);
    constexpr ImVec2 ItemSpacing      = ImVec2(8, 6);
    constexpr ImVec2 ItemInnerSpacing = ImVec2(4, 4);
    constexpr ImVec2 ContentPadding   = ImVec2(10, 6);

    // === Rounding ===
    constexpr float RadiusSM  = 4.0f;
    constexpr float RadiusMD  = 6.0f;
    constexpr float RadiusLG  = 8.0f;
    constexpr float RadiusXL  = 12.0f;

    // === Font Sizes ===
    constexpr float FontSizeSM   = 13.0f;
    constexpr float FontSizeBase = 15.0f;
    constexpr float FontSizeLG   = 17.0f;
    constexpr float FontSizeXL   = 20.0f;

    // === Overlay Colors ===
    constexpr ImU32 OverlayBg        = IM_COL32(10, 10, 18, 210);
    constexpr ImU32 OverlayBorder    = IM_COL32(124, 92, 252, 80);
    constexpr ImU32 OverlayBorderHi  = IM_COL32(155, 125, 255, 120);

    // === Toggle Switch Colors (MyCheckBox) ===
    namespace Toggle
    {
        constexpr ImVec4 TrackOff        = ImVec4(0.165f, 0.165f, 0.227f, 1.00f);
        constexpr ImVec4 TrackOffHover   = ImVec4(0.220f, 0.220f, 0.290f, 1.00f);
        constexpr ImVec4 TrackOn         = ImVec4(0.486f, 0.361f, 0.988f, 1.00f);
        constexpr ImVec4 TrackOnHover    = ImVec4(0.608f, 0.490f, 1.00f, 1.00f);
        constexpr ImU32   Knob           = IM_COL32(255, 255, 255, 255);
        constexpr ImU32   KnobShadow     = IM_COL32(0, 0, 0, 60);
    }
}
