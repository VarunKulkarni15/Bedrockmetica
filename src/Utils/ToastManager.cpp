#include "ToastManager.hpp"
#include "../Hooks/SwapchainHook.hpp"
#include "../UI/UiAssets.hpp"
#include "../UI/UiDraw.hpp"
#include "SoundManager.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ToastManager {
static std::vector<ToastNotification> g_Toasts;
static std::mutex g_ToastMutex;

void Initialize() {
  std::lock_guard<std::mutex> lock(g_ToastMutex);
  g_Toasts.clear();
}

void Show(ToastType type, const std::string &title, const std::string &message,
          const std::string &badge, float duration) {
  {
    std::lock_guard<std::mutex> lock(g_ToastMutex);
    if (g_Toasts.size() >= 4) {
      g_Toasts.erase(g_Toasts.begin());
    }
    g_Toasts.push_back({type, title, message, badge, std::max(1.0f, duration),
                        std::max(1.0f, duration)});
  }

  if (type == ToastType::Error) {
    SoundManager::Play(UISound::Error);
  } else if (type == ToastType::Warning) {
    SoundManager::Play(UISound::Toggle);
  } else {
    SoundManager::Play(UISound::Success);
  }
}

void Clear() {
  std::lock_guard<std::mutex> lock(g_ToastMutex);
  g_Toasts.clear();
}

void UpdateAndRender(float screenWidth, float screenHeight, float deltaTime) {
  std::vector<ToastNotification> snapshot;
  {
    std::lock_guard<std::mutex> lock(g_ToastMutex);
    if (g_Toasts.empty())
      return;

    const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
    for (auto it = g_Toasts.begin(); it != g_Toasts.end();) {
      it->timeRemaining -= dt;
      if (it->timeRemaining <= 0.0f) {
        it = g_Toasts.erase(it);
      } else {
        ++it;
      }
    }
    snapshot = g_Toasts;
  }

  if (snapshot.empty())
    return;

  ImDrawList *draw = ImGui::GetForegroundDrawList();
  if (!draw)
    return;

  ImFont *fontTitle = SwapchainHook::GetFontMinecraft();
  ImFont *fontBold = SwapchainHook::GetFontBold();
  ImFont *fontReg = SwapchainHook::GetFontRegular();
  if (!fontReg)
    fontReg = ImGui::GetFont();
  if (!fontBold)
    fontBold = fontReg;
  if (!fontTitle)
    fontTitle = fontBold;

  const float marginX = 24.0f;
  const float marginY = 24.0f;
  const float gapY = 10.0f;
  const float maxW =
      std::min(510.0f, std::max(320.0f, screenWidth - marginX * 2.0f));
  const float minW = std::min(340.0f, maxW);

  const float leftPad = 16.0f;
  const float badgeSize = 36.0f;
  const float badgeGap = 12.0f;
  const float textLeft = leftPad + badgeSize + badgeGap;
  const float rightPad = 18.0f;
  const float padTop = 13.0f;
  const float padBot = 15.0f;
  const float maxTextW = std::max(100.0f, maxW - textLeft - rightPad);

  float currentBottom = screenHeight - marginY;

  for (int i = static_cast<int>(snapshot.size()) - 1; i >= 0; --i) {
    const auto &t = snapshot[i];

    float elapsed = t.totalDuration - t.timeRemaining;
    float fadeIn = std::clamp(elapsed / 0.20f, 0.0f, 1.0f);
    float fadeOut = std::clamp(t.timeRemaining / 0.25f, 0.0f, 1.0f);
    float alpha = std::min(fadeIn, fadeOut);
    float inv = 1.0f - alpha;
    float slideX = inv * inv * inv * 42.0f;

    UiAssets::PlateId plate = UiAssets::PlateId::ToastInfo;
    UiAssets::BadgeId badge = UiAssets::BadgeId::Info;
    ImU32 titleCol = IM_COL32(118, 224, 255, static_cast<int>(alpha * 255));
    ImU32 barCol = IM_COL32(56, 166, 240, static_cast<int>(alpha * 255));
    const char *defaultTitle = "INFO";

    switch (t.type) {
    case ToastType::Success:
      plate = UiAssets::PlateId::ToastSuccess;
      badge = UiAssets::BadgeId::Check;
      titleCol = IM_COL32(126, 255, 166, static_cast<int>(alpha * 255));
      barCol = IM_COL32(66, 220, 112, static_cast<int>(alpha * 255));
      defaultTitle = "SUCCESS";
      break;
    case ToastType::Warning:
      plate = UiAssets::PlateId::ToastWarning;
      badge = UiAssets::BadgeId::Star;
      titleCol = IM_COL32(255, 218, 96, static_cast<int>(alpha * 255));
      barCol = IM_COL32(248, 186, 42, static_cast<int>(alpha * 255));
      defaultTitle = "WARNING";
      break;
    case ToastType::Error:
      plate = UiAssets::PlateId::ToastError;
      badge = UiAssets::BadgeId::Alert;
      titleCol = IM_COL32(255, 146, 146, static_cast<int>(alpha * 255));
      barCol = IM_COL32(236, 70, 70, static_cast<int>(alpha * 255));
      defaultTitle = "ERROR";
      break;
    default:
      break;
    }

    std::string titleStr = t.title.empty() ? defaultTitle : t.title;
    std::string msgStr = t.message;

    const float titleFontSize = UiDraw::ScaleFontSize(fontTitle, 17.0f);
    const float msgFontSize = UiDraw::ScaleFontSize(fontReg, 17.0f);

    ImVec2 titleSz = fontTitle->CalcTextSizeA(titleFontSize, FLT_MAX, maxTextW,
                                              titleStr.c_str());
    ImVec2 msgSz =
        fontReg->CalcTextSizeA(msgFontSize, FLT_MAX, maxTextW, msgStr.c_str());

    float contentW = std::max(titleSz.x, msgSz.x);
    float cardW = std::clamp(textLeft + contentW + rightPad, minW, maxW);
    float textBlockH = titleSz.y + 4.0f + msgSz.y;
    float cardH = std::max(64.0f, padTop + textBlockH + padBot);

    float posX = std::round(screenWidth - cardW - marginX + slideX);
    float posY = std::round(currentBottom - cardH);

    ImVec2 pMin(posX, posY);

    UiDraw::DrawPlate9Slice(draw, plate, pMin, ImVec2(cardW, cardH), alpha,
                            true);

    ImVec2 badgeCenter(posX + leftPad + badgeSize * 0.5f,
                       posY + (cardH - 4.0f) * 0.5f);
    UiDraw::DrawDiamondBadge(draw, badge, badgeCenter, badgeSize, alpha);

    ImVec2 textClipMin(posX + textLeft, posY + 6.0f);
    ImVec2 textClipMax(posX + cardW - rightPad + 2.0f, posY + cardH - 8.0f);
    draw->PushClipRect(textClipMin, textClipMax, true);

    float textY = posY + std::max(padTop, (cardH - 4.0f - textBlockH) * 0.5f);
    ImU32 shadowCol = IM_COL32(8, 10, 14, static_cast<int>(alpha * 255));
    ImU32 msgCol = IM_COL32(236, 242, 252, static_cast<int>(alpha * 255));

    draw->AddText(fontTitle, titleFontSize,
                  ImVec2(posX + textLeft + 1.5f, textY + 1.5f), shadowCol,
                  titleStr.c_str(), nullptr, maxTextW);
    draw->AddText(fontTitle, titleFontSize, ImVec2(posX + textLeft, textY),
                  titleCol, titleStr.c_str(), nullptr, maxTextW);

    float msgY = textY + titleSz.y + 4.0f;
    draw->AddText(fontReg, msgFontSize,
                  ImVec2(posX + textLeft + 1.0f, msgY + 1.0f), shadowCol,
                  msgStr.c_str(), nullptr, maxTextW);
    draw->AddText(fontReg, msgFontSize, ImVec2(posX + textLeft, msgY), msgCol,
                  msgStr.c_str(), nullptr, maxTextW);

    draw->PopClipRect();

    float pct = std::clamp(t.timeRemaining / t.totalDuration, 0.0f, 1.0f);
    float barX0 = posX + 12.0f;
    float barX1 = posX + cardW - 12.0f;
    float barY0 = posY + cardH - 6.0f;
    draw->AddRectFilled(ImVec2(barX0, barY0),
                        ImVec2(barX0 + (barX1 - barX0) * pct, barY0 + 3.0f),
                        barCol);

    currentBottom -= (cardH + gapY);
  }
}
} // namespace ToastManager
