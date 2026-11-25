#include "UiDraw.hpp"
#include "../Utils/SoundManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace UiDraw {

namespace {

struct ButtonAnimState {
    float hoverT = 0.0f;
    float pressT = 0.0f;
    float clickLatchTimer = 0.0f;
};

static std::unordered_map<ImGuiID, ButtonAnimState> g_ButtonAnims;
static std::unordered_map<ImGuiID, float> g_ScalarAnims;
static float g_TextScale = 1.15f;
static ImFont* g_DisplayFont = nullptr;

ImU32 ApplyAlpha(ImU32 col, float alpha) {
    uint32_t a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    uint32_t newA = static_cast<uint32_t>(std::clamp(a * alpha, 0.0f, 255.0f));
    return (col & ~(0xFFu << IM_COL32_A_SHIFT)) | (newA << IM_COL32_A_SHIFT);
}

ImU32 ScaleRgb(ImU32 col, float mult, float alpha) {
    uint32_t r = std::min<uint32_t>(255, static_cast<uint32_t>(((col >> IM_COL32_R_SHIFT) & 0xFF) * mult));
    uint32_t g = std::min<uint32_t>(255, static_cast<uint32_t>(((col >> IM_COL32_G_SHIFT) & 0xFF) * mult));
    uint32_t b = std::min<uint32_t>(255, static_cast<uint32_t>(((col >> IM_COL32_B_SHIFT) & 0xFF) * mult));
    uint32_t a = static_cast<uint32_t>(std::clamp(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha, 0.0f, 255.0f));
    return IM_COL32(r, g, b, a);
}

std::string FitEllipsis(ImFont* font, float fontSize, const char* text, float maxW) {
    if (!text || !text[0]) return "";
    if (!font) font = ImGui::GetFont();
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    if (sz.x <= maxW) return text;

    std::string s(text);
    while (s.size() > 1) {
        s.pop_back();
        std::string cand = s + "...";
        if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, cand.c_str()).x <= maxW) {
            return cand;
        }
    }
    return "...";
}

float ExpSmooth(float cur, float target, float speed) {
    float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.05f);
    float factor = 1.0f - std::exp(-speed * dt);
    return cur + (target - cur) * factor;
}

}

float StepAnimValue(ImGuiID id, float target, float speed) {
    auto it = g_ScalarAnims.find(id);
    if (it == g_ScalarAnims.end()) {
        g_ScalarAnims[id] = target;
        return target;
    }
    it->second = ExpSmooth(it->second, target, speed);
    return it->second;
}

void DrawChamferOutline(ImDrawList* draw, ImVec2 pos, ImVec2 size, float chamfer, ImU32 color, float thickness) {
    if (!draw || ((color >> IM_COL32_A_SHIFT) & 0xFF) == 0) return;
    float c = std::min(chamfer, std::min(size.x, size.y) * 0.4f);
    float x0 = pos.x, y0 = pos.y, x1 = pos.x + size.x, y1 = pos.y + size.y;
    ImVec2 pts[8] = {
        ImVec2(x0 + c, y0), ImVec2(x1 - c, y0),
        ImVec2(x1, y0 + c), ImVec2(x1, y1 - c),
        ImVec2(x1 - c, y1), ImVec2(x0 + c, y1),
        ImVec2(x0, y1 - c), ImVec2(x0, y0 + c)
    };
    draw->AddPolyline(pts, 8, color, ImDrawFlags_Closed, thickness);
}

void DrawPlate9Slice(ImDrawList* draw, UiAssets::PlateId plate,
                     ImVec2 pos, ImVec2 size, float alpha,
                     bool withDropShadow, ImU32 tint) {
    if (!draw || size.x <= 1.0f || size.y <= 1.0f) return;

    if (withDropShadow) {
        ImU32 sh = ApplyAlpha(IM_COL32(4, 6, 10, 145), alpha);
        draw->AddRectFilled(
            ImVec2(pos.x + 2.0f, pos.y + 4.0f),
            ImVec2(pos.x + size.x + 1.0f, pos.y + size.y + 5.0f),
            sh, 4.0f
        );
    }

    ImTextureID tex = UiAssets::GetPlateTex(plate);
    if (tex != 0) {
        ImU32 col = ApplyAlpha(tint, alpha);
        const float cx = std::min(UiAssets::kPlateCornerPx, size.x * 0.45f);
        const float cy = std::min(UiAssets::kPlateCornerPx, size.y * 0.45f);
        const float u1 = 0.25f;
        const float u2 = 0.75f;
        const float us1 = 0.27f;
        const float us2 = 0.73f;

        const float xs[4] = { pos.x, pos.x + cx, pos.x + size.x - cx, pos.x + size.x };
        const float ys[4] = { pos.y, pos.y + cy, pos.y + size.y - cy, pos.y + size.y };

        const float us[3][2] = { { 0.0f, u1 }, { us1, us2 }, { u2, 1.0f } };
        const float vs[3][2] = { { 0.0f, u1 }, { us1, us2 }, { u2, 1.0f } };

        for (int row = 0; row < 3; ++row) {
            for (int colIdx = 0; colIdx < 3; ++colIdx) {
                ImVec2 p0(xs[colIdx], ys[row]);
                ImVec2 p1(xs[colIdx + 1], ys[row + 1]);
                if (p1.x <= p0.x || p1.y <= p0.y) continue;
                draw->AddImage(
                    tex, p0, p1,
                    ImVec2(us[colIdx][0], vs[row][0]),
                    ImVec2(us[colIdx][1], vs[row][1]),
                    col
                );
            }
        }
        return;
    }

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                        ApplyAlpha(IM_COL32(28, 34, 48, 245), alpha), 4.0f);
    DrawChamferOutline(draw, pos, size, 6.0f, ApplyAlpha(IM_COL32(12, 15, 22, 255), alpha), 2.0f);
}

void DrawSubtleGrid(ImDrawList* draw, ImVec2 pos, ImVec2 size, float step, float alpha) {
    if (!draw) return;
    ImU32 lineCol = ApplyAlpha(IM_COL32(255, 255, 255, 11), alpha);
    draw->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    for (float gx = pos.x + step; gx < pos.x + size.x - 4.0f; gx += step) {
        draw->AddLine(ImVec2(gx, pos.y + 4.0f), ImVec2(gx, pos.y + size.y - 4.0f), lineCol, 1.0f);
    }
    for (float gy = pos.y + step; gy < pos.y + size.y - 4.0f; gy += step) {
        draw->AddLine(ImVec2(pos.x + 4.0f, gy), ImVec2(pos.x + size.x - 4.0f, gy), lineCol, 1.0f);
    }
    draw->PopClipRect();
}

float DrawTextLeftEllipsis(ImDrawList* draw, ImFont* font, float fontSize,
                           float leftX, float centerY, float maxW,
                           const char* text, ImU32 color, ImU32 shadowColor,
                           float shadowOffset) {
    if (!draw || !text || !text[0]) return 0.0f;
    if (!font) font = ImGui::GetFont();
    float effSize = ScaleFontSize(font, fontSize);

    std::string fitted = FitEllipsis(font, effSize, text, std::max(16.0f, maxW));
    ImVec2 sz = font->CalcTextSizeA(effSize, FLT_MAX, 0.0f, fitted.c_str());
    float tx = std::round(leftX);
    float ty = std::round(centerY - sz.y * 0.5f);

    draw->PushClipRect(ImVec2(leftX - 2.0f, ty - 4.0f),
                       ImVec2(leftX + maxW + 2.0f, ty + sz.y + shadowOffset + 4.0f), true);
    if (shadowOffset > 0.0f && ((shadowColor >> IM_COL32_A_SHIFT) & 0xFF) > 0) {
        draw->AddText(font, effSize, ImVec2(tx + shadowOffset, ty + shadowOffset), shadowColor, fitted.c_str());
    }
    draw->AddText(font, effSize, ImVec2(tx, ty), color, fitted.c_str());
    draw->PopClipRect();
    return sz.x;
}

void DrawTextCentered(ImDrawList* draw, ImFont* font, float fontSize,
                      ImVec2 boxMin, ImVec2 boxMax, const char* text,
                      ImU32 color, ImU32 shadowColor, float shadowOffset, float padX) {
    if (!draw || !text || !text[0]) return;
    if (!font) font = ImGui::GetFont();
    float effSize = ScaleFontSize(font, fontSize);

    float boxW = boxMax.x - boxMin.x;
    float boxH = boxMax.y - boxMin.y;
    std::string fitted = FitEllipsis(font, effSize, text, std::max(12.0f, boxW - padX * 2.0f));
    ImVec2 sz = font->CalcTextSizeA(effSize, FLT_MAX, 0.0f, fitted.c_str());

    float tx = std::round(boxMin.x + (boxW - sz.x) * 0.5f);
    float ty = std::round(boxMin.y + (boxH - sz.y) * 0.5f);

    draw->PushClipRect(boxMin, boxMax, true);
    if (shadowOffset > 0.0f && ((shadowColor >> IM_COL32_A_SHIFT) & 0xFF) > 0) {
        draw->AddText(font, effSize, ImVec2(tx + shadowOffset, ty + shadowOffset), shadowColor, fitted.c_str());
    }
    draw->AddText(font, effSize, ImVec2(tx, ty), color, fitted.c_str());
    draw->PopClipRect();
}

bool DrawActionButton(ImDrawList* draw, const char* id, const char* label,
                      ImVec2 pos, ImVec2 size, UiAssets::PlateId plate,
                      ImFont* font, float fontSize,
                      ImU32 textColor, ImU32 shadowColor, bool enabled, float globalAlpha,
                      UiAssets::BadgeId badge, bool showBadge) {
    if (!draw) return false;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    ImGuiID itemId = ImGui::GetItemID();
    bool hovered = enabled && ImGui::IsItemHovered();
    bool active  = enabled && ImGui::IsItemActive();
    bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);

    float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.05f);
    ButtonAnimState& st = g_ButtonAnims[itemId];
    if (clicked) {
        st.clickLatchTimer = 0.085f;
    } else if (st.clickLatchTimer > 0.0f) {
        st.clickLatchTimer = std::max(0.0f, st.clickLatchTimer - dt);
    }

    bool pressedVisual = active || (st.clickLatchTimer > 0.0f);
    st.hoverT = ExpSmooth(st.hoverT, hovered ? 1.0f : 0.0f, 22.0f);
    st.pressT = ExpSmooth(st.pressT, pressedVisual ? 1.0f : 0.0f, 34.0f);

    float yOffset = -1.0f * st.hoverT + 3.5f * st.pressT;
    ImVec2 drawPos(pos.x, pos.y + yOffset);
    ImVec2 drawSize(size.x, size.y);

    float brightness = enabled ? (0.94f + 0.08f * st.hoverT - 0.12f * st.pressT) : 0.65f;
    ImU32 tint = ScaleRgb(IM_COL32(255, 255, 255, 255), brightness, globalAlpha);
    DrawPlate9Slice(draw, plate, drawPos, drawSize, globalAlpha, st.pressT < 0.45f, tint);

    if (st.hoverT > 0.02f && enabled && st.pressT < 0.35f) {
        ImU32 rimCol = ApplyAlpha(IM_COL32(255, 255, 255, static_cast<int>(52 * st.hoverT)), globalAlpha);
        DrawChamferOutline(draw, ImVec2(drawPos.x + 2.0f, drawPos.y + 2.0f),
                           ImVec2(drawSize.x - 4.0f, drawSize.y - 4.0f), 6.0f, rimCol, 1.5f);
    }

    if (showBadge && label && label[0]) {
        if (!font) font = ImGui::GetFont();
        float effSize = ScaleFontSize(font, fontSize);
        ImVec2 txtSz = font->CalcTextSizeA(effSize, FLT_MAX, 0.0f, label);
        float iconSz = std::round(std::min(drawSize.y * 0.58f, 24.0f));
        float iconGap = std::round(std::min(drawSize.x * 0.04f, 10.0f));
        float totalW = iconSz + iconGap + txtSz.x;
        float startX = std::round(drawPos.x + (drawSize.x - totalW) * 0.5f);
        float centerY = std::round(drawPos.y + drawSize.y * 0.5f - 1.0f);

        DrawDiamondBadge(draw, badge, ImVec2(startX + iconSz * 0.5f, centerY), iconSz, globalAlpha);
        float textY = std::round(centerY - txtSz.y * 0.5f);
        float textX = startX + iconSz + iconGap;
        if (shadowColor && ((shadowColor >> IM_COL32_A_SHIFT) & 0xFF) > 0) {
            draw->AddText(font, effSize, ImVec2(textX + 1.5f, textY + 1.5f), ApplyAlpha(shadowColor, globalAlpha), label);
        }
        draw->AddText(font, effSize, ImVec2(textX, textY), ApplyAlpha(textColor, globalAlpha), label);
    } else if (label && label[0]) {
        DrawTextCentered(draw, font, fontSize,
                         drawPos, ImVec2(drawPos.x + drawSize.x, drawPos.y + drawSize.y - 2.0f),
                         label, ApplyAlpha(textColor, globalAlpha), ApplyAlpha(shadowColor, globalAlpha), 1.5f, 8.0f);
    }

    if (clicked) {
        SoundManager::Play(UISound::Click);
    }
    return clicked;
}

void DrawStatusPill(ImDrawList* draw, ImVec2 pos, ImVec2 size, UiAssets::PlateId plate,
                    ImFont* font, float fontSize, const char* text,
                    ImU32 textColor, ImU32 shadowColor, float alpha) {
    DrawPlate9Slice(draw, plate, pos, size, alpha, false);
    DrawTextCentered(draw, font, fontSize, pos, ImVec2(pos.x + size.x, pos.y + size.y - 1.0f),
                     text, ApplyAlpha(textColor, alpha), ApplyAlpha(shadowColor, alpha), 1.5f, 6.0f);
}

bool DrawSegmentedToggle(ImDrawList* draw, const char* idPrefix, ImVec2 pos, ImVec2 size,
                         const char* const* labels, int count, int& activeIdx,
                         ImFont* font, float fontSize, float alpha) {
    if (!draw || count <= 0) return false;
    bool changed = false;
    const float gap = 6.0f;
    float segW = (size.x - gap * (count - 1)) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        ImVec2 sPos(pos.x + i * (segW + gap), pos.y);
        ImVec2 sSize(segW, size.y);
        bool isOn = (i == activeIdx);

        char btnId[64];
        snprintf(btnId, sizeof(btnId), "%s_seg_%d", idPrefix, i);

        ImGui::SetCursorScreenPos(sPos);
        ImGui::InvisibleButton(btnId, sSize);
        ImGuiID segId = ImGui::GetItemID();
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ButtonAnimState& st = g_ButtonAnims[segId];
        st.hoverT = ExpSmooth(st.hoverT, hovered ? 1.0f : 0.0f, 18.0f);
        st.pressT = ExpSmooth(st.pressT, active  ? 1.0f : 0.0f, 26.0f);

        float onT = StepAnimValue(segId ^ 0xA55Au, isOn ? 1.0f : 0.0f, 16.0f);

        float yOffset = -1.0f * st.hoverT + 2.0f * st.pressT;
        ImVec2 dp(sPos.x, sPos.y + yOffset);
        UiAssets::PlateId plate = isOn ? UiAssets::PlateId::EmeraldCTA : UiAssets::PlateId::SlateCard;
        float mult = 0.93f + 0.07f * st.hoverT - 0.06f * st.pressT;

        DrawPlate9Slice(draw, plate, dp, sSize, alpha, st.pressT < 0.5f, ScaleRgb(IM_COL32(255, 255, 255, 255), mult, alpha));

        ImU32 txtCol = isOn ? IM_COL32(255, 255, 255, 255) : IM_COL32(204, 214, 236, 255);
        ImU32 shCol  = isOn ? IM_COL32(14, 64, 28, 255)   : IM_COL32(12, 15, 22, 255);
        DrawTextCentered(draw, font, fontSize, dp, ImVec2(dp.x + sSize.x, dp.y + sSize.y - (isOn ? 5.0f : 2.0f)),
                         labels[i], ApplyAlpha(txtCol, alpha), ApplyAlpha(shCol, alpha), 1.5f, 4.0f);

        if (onT > 0.04f) {
            float barW = sSize.x * 0.38f * onT;
            float bx0 = dp.x + (sSize.x - barW) * 0.5f;
            float by = dp.y + sSize.y - 6.0f;
            draw->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + barW, by + 2.5f),
                                IM_COL32(255, 255, 255, static_cast<int>(255 * onT * alpha)));
        }

        if (clicked && i != activeIdx) {
            activeIdx = i;
            changed = true;
            SoundManager::Play(UISound::Toggle);
        }
    }
    return changed;
}

bool DrawSteppedSlider(ImDrawList* draw, const char* id, ImVec2 pos, ImVec2 size,
                       int& value, int minVal, int maxVal, float alpha) {
    if (!draw || maxVal <= minVal) return false;
    bool changed = false;

    ImGui::SetCursorScreenPos(ImVec2(pos.x - 6.0f, pos.y));
    ImGui::InvisibleButton(id, ImVec2(size.x + 12.0f, size.y));
    ImGuiID sliderId = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (active) {
        float frac = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / size.x, 0.0f, 1.0f);
        int nv = minVal + static_cast<int>(std::round(frac * (maxVal - minVal)));
        if (nv != value) {
            value = nv;
            changed = true;
        }
    }

    float targetFrac = static_cast<float>(value - minVal) / static_cast<float>(maxVal - minVal);
    float smoothFrac = StepAnimValue(sliderId, std::clamp(targetFrac, 0.0f, 1.0f), 22.0f);
    float hoverBoost = StepAnimValue(sliderId ^ 0x1337u, (hovered || active) ? 1.0f : 0.0f, 16.0f);

    float trackH = 10.0f;
    float ty0 = pos.y + (size.y - trackH) * 0.5f;
    ImVec2 tMin(pos.x, ty0);
    ImVec2 tMax(pos.x + size.x, ty0 + trackH);
    draw->AddRectFilled(tMin, tMax, ApplyAlpha(IM_COL32(10, 13, 20, 255), alpha));
    draw->AddRect(tMin, tMax, ApplyAlpha(IM_COL32(56, 66, 92, 255), alpha), 0.0f, 0, 2.0f);

    float fillW = smoothFrac * size.x;
    if (fillW > 2.0f) {
        draw->AddRectFilled(ImVec2(tMin.x + 2.0f, tMin.y + 2.0f),
                            ImVec2(tMin.x + fillW - 1.0f, tMax.y - 2.0f),
                            ApplyAlpha(IM_COL32(48, 198, 248, 255), alpha));
    }

    float thumbW = 18.0f + hoverBoost * 2.0f;
    float thumbH = 22.0f + hoverBoost * 3.0f;
    float tx = std::clamp(pos.x + fillW - thumbW * 0.5f, pos.x - 4.0f, pos.x + size.x - thumbW + 4.0f);
    float ty = pos.y + (size.y - thumbH) * 0.5f;
    DrawPlate9Slice(draw, UiAssets::PlateId::GoldHeader, ImVec2(tx, ty), ImVec2(thumbW, thumbH), alpha, true);
    return changed;
}

void DrawDiamondBadge(ImDrawList* draw, UiAssets::BadgeId badge, ImVec2 center, float size, float alpha) {
    if (!draw) return;
    ImTextureID tex = UiAssets::GetBadgeTex(badge);
    float half = size * 0.5f;
    ImVec2 p0(center.x - half, center.y - half);
    ImVec2 p1(center.x + half, center.y + half);
    if (tex != 0) {
        draw->AddImage(tex, p0, p1, ImVec2(0, 0), ImVec2(1, 1), ApplyAlpha(IM_COL32(255, 255, 255, 255), alpha));
    } else {
        draw->AddQuadFilled(
            ImVec2(center.x, center.y - half),
            ImVec2(center.x + half, center.y),
            ImVec2(center.x, center.y + half),
            ImVec2(center.x - half, center.y),
            ApplyAlpha(IM_COL32(245, 184, 46, 255), alpha)
        );
    }
}

void SetTextScale(float scale) {
    g_TextScale = std::clamp(scale, 0.75f, 1.75f);
}

float GetTextScale() {
    return g_TextScale;
}

void SetDisplayFont(ImFont* font) {
    g_DisplayFont = font;
}

float ScaleFontSize(ImFont* font, float baseSize) {
    float boost = (g_DisplayFont != nullptr && font == g_DisplayFont) ? 1.18f : 1.0f;
    return std::round(baseSize * g_TextScale * boost);
}

}
