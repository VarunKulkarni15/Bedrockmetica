#pragma once

#include "UiAssets.hpp"
#include <imgui.h>
#include <string>

namespace UiDraw {

void DrawPlate9Slice(ImDrawList* draw, UiAssets::PlateId plate,
                     ImVec2 pos, ImVec2 size, float alpha = 1.0f,
                     bool withDropShadow = false, ImU32 tint = IM_COL32(255, 255, 255, 255));

void DrawChamferOutline(ImDrawList* draw, ImVec2 pos, ImVec2 size, float chamfer, ImU32 color, float thickness = 2.0f);

void DrawSubtleGrid(ImDrawList* draw, ImVec2 pos, ImVec2 size, float step = 26.0f, float alpha = 1.0f);

float DrawTextLeftEllipsis(ImDrawList* draw, ImFont* font, float fontSize,
                           float leftX, float centerY, float maxW,
                           const char* text, ImU32 color, ImU32 shadowColor = IM_COL32(10, 12, 18, 255),
                           float shadowOffset = 2.0f);

void DrawTextCentered(ImDrawList* draw, ImFont* font, float fontSize,
                      ImVec2 boxMin, ImVec2 boxMax, const char* text,
                      ImU32 color, ImU32 shadowColor = IM_COL32(10, 12, 18, 255),
                      float shadowOffset = 2.0f, float padX = 8.0f);

bool DrawActionButton(ImDrawList* draw, const char* id, const char* label,
                      ImVec2 pos, ImVec2 size, UiAssets::PlateId plate,
                      ImFont* font, float fontSize,
                      ImU32 textColor = IM_COL32(255, 255, 255, 255),
                      ImU32 shadowColor = IM_COL32(10, 12, 18, 255),
                      bool enabled = true, float globalAlpha = 1.0f,
                      UiAssets::BadgeId badge = UiAssets::BadgeId::Cube,
                      bool showBadge = false);

void DrawStatusPill(ImDrawList* draw, ImVec2 pos, ImVec2 size, UiAssets::PlateId plate,
                    ImFont* font, float fontSize, const char* text,
                    ImU32 textColor = IM_COL32(255, 255, 255, 255),
                    ImU32 shadowColor = IM_COL32(10, 12, 18, 255), float alpha = 1.0f);

bool DrawSegmentedToggle(ImDrawList* draw, const char* idPrefix, ImVec2 pos, ImVec2 size,
                         const char* const* labels, int count, int& activeIdx,
                         ImFont* font, float fontSize, float alpha = 1.0f);

bool DrawSteppedSlider(ImDrawList* draw, const char* id, ImVec2 pos, ImVec2 size,
                       int& value, int minVal, int maxVal, float alpha = 1.0f);

void DrawDiamondBadge(ImDrawList* draw, UiAssets::BadgeId badge, ImVec2 center, float size, float alpha = 1.0f);

float StepAnimValue(ImGuiID id, float target, float speed);

void SetTextScale(float scale);
float GetTextScale();
void SetDisplayFont(ImFont* font);
float ScaleFontSize(ImFont* font, float baseSize);

}
