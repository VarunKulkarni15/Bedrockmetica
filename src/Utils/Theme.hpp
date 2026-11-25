#pragma once

#include <imgui.h>

namespace Theme {
    // =========================================================================
    // Millida Launcher Design Tokens (Direct from github.com/millida/launcher)
    // =========================================================================
    inline constexpr ImVec4 M_Bg            = ImVec4(0.086f, 0.090f, 0.094f, 1.00f); // #161718 Base window bg
    inline constexpr ImVec4 M_Surface       = ImVec4(0.117f, 0.121f, 0.125f, 0.96f); // #1E1F20 Card / Rail surface
    inline constexpr ImVec4 M_Surface2      = ImVec4(0.149f, 0.153f, 0.165f, 0.96f); // #26272A Hovered surface
    inline constexpr ImVec4 M_Raised        = ImVec4(0.165f, 0.168f, 0.180f, 1.00f); // #2A2B2E Elevated modals
    inline constexpr ImVec4 M_Inset         = ImVec4(0.074f, 0.074f, 0.078f, 1.00f); // #131314 Input inset

    inline constexpr ImVec4 M_Border        = ImVec4(1.000f, 1.000f, 1.000f, 0.07f); // rgba(255,255,255,0.07)
    inline constexpr ImVec4 M_BorderStrong  = ImVec4(1.000f, 1.000f, 1.000f, 0.14f); // rgba(255,255,255,0.14)

    inline constexpr ImVec4 M_Fg            = ImVec4(0.929f, 0.937f, 0.933f, 1.00f); // #EDEFEE Text primary
    inline constexpr ImVec4 M_FgMuted       = ImVec4(0.929f, 0.937f, 0.933f, 0.80f); // 80% Text
    inline constexpr ImVec4 M_FgSubtle      = ImVec4(0.929f, 0.937f, 0.933f, 0.66f); // 66% Text
    inline constexpr ImVec4 M_FgFaint       = ImVec4(0.929f, 0.937f, 0.933f, 0.52f); // 52% Text

    // Millida Iconic Accents
    inline constexpr ImVec4 M_Accent        = ImVec4(0.368f, 0.776f, 0.302f, 1.00f); // #5EC64D Millida Emerald Green
    inline constexpr ImVec4 M_AccentHover   = ImVec4(0.439f, 0.835f, 0.372f, 1.00f); // #70D55F
    inline constexpr ImVec4 M_AccentSoft    = ImVec4(0.368f, 0.776f, 0.302f, 0.13f); // rgba(94,198,77,0.13)
    inline constexpr ImVec4 M_BlueprintCyan = ImVec4(0.000f, 0.705f, 0.847f, 1.00f); // #00B4D8 Blueprint Cyan
    inline constexpr ImVec4 M_Warning       = ImVec4(0.960f, 0.666f, 0.220f, 1.00f); // #F5AA38 Gold warning
    inline constexpr ImVec4 M_Danger        = ImVec4(1.000f, 0.420f, 0.368f, 1.00f); // #FF6B5E Soft Red
    inline constexpr ImVec4 M_Info          = ImVec4(0.415f, 0.662f, 0.914f, 1.00f); // #6AA9E9 Info blue

    // Millida Card Banner Colors (Direct from user design)
    inline constexpr ImVec4 M_BannerRed     = ImVec4(0.878f, 0.282f, 0.282f, 1.00f); // #E04848 Schematics Red
    inline constexpr ImVec4 M_BannerBlue    = ImVec4(0.008f, 0.533f, 0.820f, 1.00f); // #0288D1 World Anchor Blue
    inline constexpr ImVec4 M_BannerGreen   = ImVec4(0.133f, 0.773f, 0.369f, 1.00f); // #22C55E Layer Slicer Green

    // Aliases for compatibility
    inline constexpr ImVec4 SlateBackground   = M_Bg;
    inline constexpr ImVec4 PanelBackground   = M_Surface;
    inline constexpr ImVec4 BlueprintBlue     = M_BlueprintCyan;
    inline constexpr ImVec4 BlueprintBlueHover= ImVec4(0.180f, 0.780f, 0.920f, 1.00f);
    inline constexpr ImVec4 BlueprintBlueActive=ImVec4(0.000f, 0.600f, 0.740f, 1.00f);
    inline constexpr ImVec4 AccentGold        = M_Warning;
    inline constexpr ImVec4 GhostFace         = ImVec4(0.000f, 0.862f, 1.000f, 0.35f);
    inline constexpr ImVec4 ErrorRed          = M_Danger;
    inline constexpr ImVec4 MatchGreen        = M_Accent;
    inline constexpr ImVec4 TextPrimary       = M_Fg;
    inline constexpr ImVec4 TextMuted         = M_FgSubtle;

    inline void ApplyMillidaTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Base surfaces
        colors[ImGuiCol_WindowBg]             = M_Surface;
        colors[ImGuiCol_ChildBg]              = M_Inset;
        colors[ImGuiCol_PopupBg]              = M_Raised;
        colors[ImGuiCol_Border]               = M_Border;
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Text
        colors[ImGuiCol_Text]                 = M_Fg;
        colors[ImGuiCol_TextDisabled]         = M_FgFaint;

        // Headers
        colors[ImGuiCol_Header]               = M_Surface2;
        colors[ImGuiCol_HeaderHovered]        = M_AccentSoft;
        colors[ImGuiCol_HeaderActive]         = M_Accent;

        // Buttons (Millida pill / rounded cards)
        colors[ImGuiCol_Button]               = M_Surface2;
        colors[ImGuiCol_ButtonHovered]        = M_AccentSoft;
        colors[ImGuiCol_ButtonActive]         = M_Accent;

        // Frames / Inputs
        colors[ImGuiCol_FrameBg]              = M_Inset;
        colors[ImGuiCol_FrameBgHovered]       = M_Surface2;
        colors[ImGuiCol_FrameBgActive]        = M_Surface;

        // Tabs
        colors[ImGuiCol_Tab]                  = M_Surface;
        colors[ImGuiCol_TabHovered]           = M_Surface2;
        colors[ImGuiCol_TabSelected]          = M_Accent;
        colors[ImGuiCol_TabDimmed]            = M_Inset;
        colors[ImGuiCol_TabDimmedSelected]    = M_Surface2;

        // Title bar
        colors[ImGuiCol_TitleBg]              = M_Inset;
        colors[ImGuiCol_TitleBgActive]        = M_Surface;
        colors[ImGuiCol_TitleBgCollapsed]     = M_Bg;

        // Sliders & Controls
        colors[ImGuiCol_CheckMark]            = M_Accent;
        colors[ImGuiCol_SliderGrab]           = M_Accent;
        colors[ImGuiCol_SliderGrabActive]     = M_AccentHover;

        // Separators & Scrollbars
        colors[ImGuiCol_Separator]            = M_Border;
        colors[ImGuiCol_SeparatorHovered]     = M_BorderStrong;
        colors[ImGuiCol_SeparatorActive]      = M_Accent;
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_ScrollbarGrab]        = M_BorderStrong;
        colors[ImGuiCol_ScrollbarGrabHovered] = M_FgSubtle;
        colors[ImGuiCol_ScrollbarGrabActive]  = M_Accent;

        // Millida Corner Radii (Clean modern card curvature)
        style.WindowRounding    = 14.0f; // --m-r-card: 14px
        style.ChildRounding     = 12.0f; // --m-r-ctl: 12px
        style.FrameRounding     = 10.0f;
        style.PopupRounding     = 12.0f;
        style.ScrollbarRounding = 999.0f; // --m-r-pill: 999px
        style.GrabRounding      = 6.0f;
        style.TabRounding       = 10.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.TabBorderSize     = 0.0f;

        style.WindowPadding     = ImVec2(16.0f, 16.0f);
        style.FramePadding      = ImVec2(14.0f, 8.0f);
        style.ItemSpacing       = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    }

    inline void ApplyOreUITheme() {
        ApplyMillidaTheme();
    }
}
