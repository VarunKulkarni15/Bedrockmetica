#include "SchematicMenu.hpp"
#include "../Core/CaptureManager.hpp"
#include "../Core/SchematicDocument.hpp"
#include "../Hooks/SwapchainHook.hpp"
#include "../IO/BmetCodec.hpp"
#include "../IO/McStructureCodec.hpp"
#include "../SDK/BedrockGame.hpp"
#include "../Utils/ConfigManager.hpp"
#include "../Utils/SoundManager.hpp"
#include "../Utils/ToastManager.hpp"
#include "../Utils/Utils.hpp"
#include "UiAssets.hpp"
#include "UiDraw.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace SchematicMenu {

namespace {

struct MaterialReq {
  std::string name;
  int placed;
  int required;
  ImU32 swatchColor;
};

struct StructureEntry {
  std::string id;
  std::string name;
  std::string format;
  int w, h, d;
  int totalBlocks;
  UiAssets::ThumbId thumb;
  std::string filePath;
  bool isDiskFile = false;
  std::vector<MaterialReq> materials;
};

static std::vector<StructureEntry> g_Entries;
static int g_SelectedIndex = 0;
static int g_LoadedIndex = -1;
static char g_SearchBuf[96] = "";

static int g_RightTab = 0;
static int g_SliceMode = 2;
static int g_SliceLayer = 10;
static int g_OriginXYZ[3] = {-731, 132, -95};
static int g_NudgeStepIdx = 0;
static int g_RotationDeg = 0;
static int g_MirrorMode = 0;
static bool g_OriginLocked = true;
static bool g_MissingOnlyFilter = false;

static float g_PreviewYaw = 0.78f;
static float g_PreviewPitch = 0.55f;
static float g_PreviewZoom = 1.0f;

static bool g_ShowSettings = false;
static bool g_ShowUnloadConfirm = false;
static bool g_ShowSaveModal = false;
static bool g_ShowRenameModal = false;
static bool g_ShowImportModal = false;
static bool g_CaptureMode = false;
static bool g_DeleteConfirm = false;
static double g_DeleteConfirmTime = 0.0;
static char g_SaveNameBuf[96] = "My Structure";
static char g_RenameNameBuf[96] = "";
static float g_UiScale = 1.0f;
static int g_UiSizePreset = 1;
static int g_TextSizePct = 115;
static int g_GhostOpacityPct = 65;
static int g_SoundFxToggle = 0;
static int g_VerifyMode = 0;

void SaveCurrentConfig() {
  ConfigManager::ConfigData cfg = ConfigManager::Load();
  cfg.menuToggleKey = SwapchainHook::GetMenuToggleKey();
  cfg.uiSizePreset = g_UiSizePreset;
  cfg.textSizePct = g_TextSizePct;
  cfg.ghostOpacityPct = g_GhostOpacityPct;
  cfg.soundFxEnabled = (g_SoundFxToggle == 0);
  cfg.verifyMode = g_VerifyMode;
  ConfigManager::Save(cfg);
}

static float g_OpenAnim = 0.0f;
static float g_SettingsAnim = 0.0f;
static float g_UnloadModalAnim = 0.0f;
static float g_SaveModalAnim = 0.0f;
static float g_RenameModalAnim = 0.0f;
static float g_ImportModalAnim = 0.0f;

struct ImportCandidate {
  std::string filename;
  std::string displayName;
  std::string ext;
  std::string fullPath;
  std::string sourceLabel;
  int32_t w = 0;
  int32_t h = 0;
  int32_t d = 0;
  int32_t totalBlocks = 0;
};
static std::vector<ImportCandidate> g_ImportCandidates;

std::string ToLowerCopy(const std::string &s) {
  std::string out = s;
  for (char &c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string GetSchematicsDir() {
  HMODULE hMod = GetModuleHandleA("Bedrockmetica.dll");
  std::string base = Utils::GetModuleDir(hMod);
  std::vector<std::string> candidates = {
      base + "/schematics", base + "/../../schematics", "schematics"};
  char localAppData[MAX_PATH] = {};
  if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
    candidates.push_back(std::string(localAppData) +
                         "/Bedrockmetica/schematics");
  }
  for (const auto &c : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(c, ec))
      return c;
  }
  return candidates.front();
}

void SeedStarterLibraryIfEmpty() {
  if (!g_Entries.empty())
    return;

  g_Entries = {{"cherry_pagoda",
                "Cherry Pagoda",
                ".mcstructure",
                34,
                28,
                34,
                14820,
                UiAssets::ThumbId::Pagoda,
                "",
                false,
                {{"Stone Bricks", 420, 420, IM_COL32(118, 124, 138, 255)},
                 {"Spruce Planks", 284, 380, IM_COL32(126, 86, 52, 255)},
                 {"Red Nether Bricks", 190, 460, IM_COL32(176, 46, 54, 255)},
                 {"Calcite", 64, 310, IM_COL32(234, 228, 218, 255)},
                 {"Cherry Leaves", 0, 180, IM_COL32(246, 152, 184, 255)},
                 {"Lantern", 12, 48, IM_COL32(255, 214, 92, 255)},
                 {"Gold Block", 18, 32, IM_COL32(248, 192, 46, 255)},
                 {"Oak Fence", 0, 64, IM_COL32(166, 132, 82, 255)},
                 {"Moss Carpet", 24, 40, IM_COL32(78, 168, 86, 255)}}},
               {"iron_trading_hall",
                "Iron Trading Hall",
                ".mcstructure",
                42,
                14,
                28,
                6410,
                UiAssets::ThumbId::TradingHall,
                "",
                false,
                {{"Stone Bricks", 0, 2640, IM_COL32(118, 124, 138, 255)},
                 {"Oak Planks", 0, 1520, IM_COL32(182, 142, 86, 255)},
                 {"Glass Pane", 0, 890, IM_COL32(174, 224, 246, 255)},
                 {"Lectern", 0, 680, IM_COL32(158, 114, 64, 255)},
                 {"Sea Lantern", 0, 420, IM_COL32(172, 232, 220, 255)},
                 {"Hopper", 0, 260, IM_COL32(96, 102, 114, 255)}}},
               {"cubecraft_bridge",
                "CubeCraft Bridge",
                ".bmet",
                18,
                9,
                44,
                2190,
                UiAssets::ThumbId::Bridge,
                "",
                false,
                {{"Quartz Pillar", 0, 980, IM_COL32(232, 236, 244, 255)},
                 {"Red Terracotta", 0, 640, IM_COL32(186, 62, 58, 255)},
                 {"Dark Oak Stair", 0, 420, IM_COL32(84, 56, 36, 255)},
                 {"Glowstone", 0, 150, IM_COL32(245, 206, 84, 255)}}},
               {"redstone_vault",
                "Redstone Vault",
                ".mcstructure",
                24,
                16,
                24,
                5840,
                UiAssets::ThumbId::Vault,
                "",
                false,
                {{"Deepslate Tiles", 0, 3120, IM_COL32(72, 78, 94, 255)},
                 {"Redstone Block", 0, 920, IM_COL32(224, 48, 48, 255)},
                 {"Sticky Piston", 0, 640, IM_COL32(132, 178, 94, 255)},
                 {"Observer", 0, 580, IM_COL32(102, 108, 122, 255)},
                 {"Copper Grate", 0, 580, IM_COL32(204, 112, 74, 255)}}}};

  std::error_code ec;
  std::string dir = GetSchematicsDir();
  if (std::filesystem::exists(dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (!entry.is_regular_file())
        continue;
      std::string ext = ToLowerCopy(entry.path().extension().string());
      if (ext != ".mcstructure" && ext != ".bmet")
        continue;

      StructureEntry s;
      s.id = entry.path().filename().string();
      s.name = entry.path().stem().string();
      s.format = ext;
      s.w = 16;
      s.h = 16;
      s.d = 16;
      s.totalBlocks = 0;
      s.thumb = UiAssets::ThumbId::Pagoda;
      s.filePath = entry.path().string();
      s.isDiskFile = true;

      if (ext == ".bmet") {
        Bedrockmetica::BmetCodec::BmetSummary sum;
        if (Bedrockmetica::BmetCodec::ReadSummary(s.filePath, sum) &&
            sum.valid) {
          if (!sum.name.empty())
            s.name = sum.name;
          s.w = std::max(1, sum.width);
          s.h = std::max(1, sum.height);
          s.d = std::max(1, sum.depth);
          s.totalBlocks = sum.totalBlocks;
          for (const auto &m : sum.topMaterials) {
            s.materials.push_back({m.displayName, 0, m.required,
                                   static_cast<ImU32>(m.approxAbgr)});
          }
        }
      } else if (ext == ".mcstructure") {
        Bedrockmetica::BmetCodec::BmetSummary sum;
        if (Bedrockmetica::McStructureCodec::ReadSummary(s.filePath, sum) &&
            sum.valid) {
          if (!sum.name.empty())
            s.name = sum.name;
          s.w = std::max(1, sum.width);
          s.h = std::max(1, sum.height);
          s.d = std::max(1, sum.depth);
          s.totalBlocks = sum.totalBlocks;
          for (const auto &m : sum.topMaterials) {
            s.materials.push_back({m.displayName, 0, m.required,
                                   static_cast<ImU32>(m.approxAbgr)});
          }
        }
      }

      if (s.materials.empty()) {
        s.materials = {
            {"Pending 3D Mesh Scan", 0, 0, IM_COL32(56, 166, 240, 255)}};
      }
      g_Entries.insert(g_Entries.begin(), std::move(s));
    }
  }

  if (!g_Entries.empty()) {
    g_SelectedIndex = 0;
    g_SliceLayer = g_Entries[0].h;
  }
}

void SelectEntry(int idx) {
  if (idx < 0 || idx >= static_cast<int>(g_Entries.size()))
    return;
  if (g_SelectedIndex != idx) {
    g_SelectedIndex = idx;
    g_SliceLayer = g_Entries[idx].h;
    g_DeleteConfirm = false;
    SoundManager::Play(UISound::Toggle);
  }
}

void DrawIsometricGridPlaceholder(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize,
                                  const StructureEntry *sel, ImFont *fontBold,
                                  ImFont *fontReg, float alpha) {
  ImGui::SetCursorScreenPos(vpPos);
  ImGui::InvisibleButton("##ViewportOrbitArea", vpSize);
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    g_PreviewYaw += delta.x * 0.009f;
    g_PreviewPitch =
        std::clamp(g_PreviewPitch + delta.y * 0.006f, 0.22f, 1.15f);
  } else {
    float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    g_PreviewYaw += dt * 0.14f;
  }
  if (ImGui::IsItemHovered() && std::abs(ImGui::GetIO().MouseWheel) > 0.01f) {
    g_PreviewZoom = std::clamp(
        g_PreviewZoom + ImGui::GetIO().MouseWheel * 0.08f, 0.6f, 1.8f);
  }

  draw->PushClipRect(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y),
                     true);

  ImVec2 center(vpPos.x + vpSize.x * 0.5f, vpPos.y + vpSize.y * 0.54f);
  int aGlow = static_cast<int>(12.0f * alpha);
  for (int r = 3; r >= 1; --r) {
    draw->AddCircleFilled(center, (105.0f * r) * 0.85f,
                          IM_COL32(46, 178, 255, aGlow));
  }

  const int halfN = 9;
  const float cell = 24.0f * g_PreviewZoom;
  float cosY = std::cos(g_PreviewYaw);
  float sinY = std::sin(g_PreviewYaw);
  float sinP = std::sin(g_PreviewPitch);

  auto projectFloor = [&](float gx, float gz) -> ImVec2 {
    float rx = gx * cosY - gz * sinY;
    float rz = gx * sinY + gz * cosY;
    return ImVec2(center.x + rx * cell, center.y + rz * cell * sinP);
  };

  for (int i = -halfN; i <= halfN; ++i) {
    int lineA = (i % 4 == 0) ? static_cast<int>(130 * alpha)
                             : static_cast<int>(72 * alpha);
    ImU32 lineCol = (i % 4 == 0) ? IM_COL32(66, 124, 168, lineA)
                                 : IM_COL32(38, 54, 78, lineA);
    draw->AddLine(
        projectFloor(static_cast<float>(i), static_cast<float>(-halfN)),
        projectFloor(static_cast<float>(i), static_cast<float>(halfN)), lineCol,
        1.0f);
    draw->AddLine(
        projectFloor(static_cast<float>(-halfN), static_cast<float>(i)),
        projectFloor(static_cast<float>(halfN), static_cast<float>(i)), lineCol,
        1.0f);
  }

  ImVec2 d0 = projectFloor(0.0f, -1.2f);
  ImVec2 d1 = projectFloor(1.2f, 0.0f);
  ImVec2 d2 = projectFloor(0.0f, 1.2f);
  ImVec2 d3 = projectFloor(-1.2f, 0.0f);
  draw->AddQuadFilled(d0, d1, d2, d3,
                      IM_COL32(46, 200, 248, static_cast<int>(40 * alpha)));
  draw->AddQuad(d0, d1, d2, d3,
                IM_COL32(72, 224, 255, static_cast<int>(180 * alpha)), 2.0f);

  if (sel) {
    char infoBuf[96];
    snprintf(infoBuf, sizeof(infoBuf), "3D Stage Ready  (%d x %d x %d)", sel->w,
             sel->h, sel->d);
    UiDraw::DrawTextCentered(
        draw, fontBold, 18.0f, ImVec2(vpPos.x + 24.0f, center.y - 72.0f),
        ImVec2(vpPos.x + vpSize.x - 24.0f, center.y - 40.0f), infoBuf,
        IM_COL32(214, 242, 255, static_cast<int>(235 * alpha)),
        IM_COL32(8, 10, 16, static_cast<int>(240 * alpha)));
    UiDraw::DrawTextCentered(
        draw, fontReg, 15.0f, ImVec2(vpPos.x + 24.0f, center.y - 40.0f),
        ImVec2(vpPos.x + vpSize.x - 24.0f, center.y - 14.0f),
        "Drag to orbit floor grid - 3D mesh attaches in next stage",
        IM_COL32(156, 172, 204, static_cast<int>(210 * alpha)),
        IM_COL32(8, 10, 16, static_cast<int>(200 * alpha)), 1.0f);
  }

  draw->PopClipRect();
}

void DrawBlockIconBox(ImDrawList *draw, ImVec2 minPos, ImVec2 maxPos,
                      const MaterialReq &m, float alpha) {
  draw->AddRectFilled(minPos, maxPos,
                      IM_COL32(12, 15, 22, static_cast<int>(240 * alpha)),
                      2.0f);
  ImTextureID iconTex =
      UiAssets::GetBlockIconTex(UiAssets::ResolveBlockIcon(m.name));
  if (iconTex != 0) {
    ImVec2 padMin(minPos.x + 2.0f, minPos.y + 2.0f);
    ImVec2 padMax(maxPos.x - 2.0f, maxPos.y - 2.0f);
    draw->AddImage(iconTex, padMin, padMax, ImVec2(0.0f, 0.0f),
                   ImVec2(1.0f, 1.0f),
                   IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
  } else {
    draw->AddRectFilled(ImVec2(minPos.x + 2.0f, minPos.y + 2.0f),
                        ImVec2(maxPos.x - 2.0f, maxPos.y - 2.0f), m.swatchColor,
                        1.0f);
  }
  draw->AddRect(minPos, maxPos,
                IM_COL32(8, 10, 16, static_cast<int>(255 * alpha)), 2.0f, 0,
                2.0f);
}

void DrawMaterialsPreviewView(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize,
                              const StructureEntry *sel, ImFont *fontBold,
                              ImFont *fontReg, float alpha) {
  draw->PushClipRect(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y),
                     true);

  float y = vpPos.y + 14.0f;
  float rowX = vpPos.x + 16.0f;
  float rowW = vpSize.x - 32.0f;

  if (!sel || sel->materials.empty()) {
    UiDraw::DrawTextCentered(
        draw, fontBold, 17.0f, vpPos,
        ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y),
        "No block breakdown loaded yet.",
        IM_COL32(160, 172, 198, static_cast<int>(255 * alpha)));
    draw->PopClipRect();
    return;
  }

  int maxShown = std::min<int>(static_cast<int>(sel->materials.size()), 9);
  for (int i = 0; i < maxShown; ++i) {
    if (y + 48.0f > vpPos.y + vpSize.y - 8.0f)
      break;
    const auto &m = sel->materials[i];
    ImVec2 rPos(rowX, y);
    ImVec2 rSize(rowW, 48.0f);
    UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlateCard, rPos, rSize,
                            alpha, false);

    ImVec2 swMin(rPos.x + 10.0f, rPos.y + 8.0f);
    ImVec2 swMax(rPos.x + 44.0f, rPos.y + 42.0f);
    DrawBlockIconBox(draw, swMin, swMax, m, alpha);

    float nameX = swMax.x + 12.0f;
    float rightColW = 224.0f;
    UiDraw::DrawTextLeftEllipsis(
        draw, fontBold, 18.0f, nameX, rPos.y + 24.0f,
        rowW - (nameX - rowX) - rightColW - 14.0f, m.name.c_str(),
        IM_COL32(238, 244, 255, static_cast<int>(255 * alpha)));

    char qtyBuf[48];
    if (m.required > 0) {
      int stacks = m.required / 64;
      int rem = m.required % 64;
      if (stacks > 0)
        snprintf(qtyBuf, sizeof(qtyBuf), "%d  (%d ST + %d)", m.required, stacks,
                 rem);
      else
        snprintf(qtyBuf, sizeof(qtyBuf), "%d blocks", m.required);
    } else {
      snprintf(qtyBuf, sizeof(qtyBuf), "--");
    }
    UiDraw::DrawStatusPill(
        draw, ImVec2(rPos.x + rowW - rightColW - 8.0f, rPos.y + 8.0f),
        ImVec2(rightColW, 32.0f), UiAssets::PlateId::DarkInset, fontReg, 16.0f,
        qtyBuf, IM_COL32(142, 236, 255, 255), IM_COL32(10, 12, 18, 255), alpha);

    y += 54.0f;
  }

  draw->PopClipRect();
}

void DrawSettingsModal(ImDrawList *draw, ImVec2 mPos, ImVec2 curSize,
                       ImFont *fontDisplay, ImFont *fontBold, ImFont *fontReg,
                       float ma) {
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, mPos,
                          curSize, ma, true);

  ImVec2 hdrPos(mPos.x + 14.0f, mPos.y + 14.0f);
  ImVec2 hdrSize(curSize.x - 28.0f, 58.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::BlueBanner, hdrPos, hdrSize,
                          ma, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 21.0f, hdrPos.x + 18.0f, hdrPos.y + 29.0f,
      hdrSize.x - 86.0f, "SETTINGS",
      IM_COL32(255, 255, 255, static_cast<int>(255 * ma)),
      IM_COL32(10, 54, 102, static_cast<int>(255 * ma)));

  if (UiDraw::DrawActionButton(
          draw, "##CloseSettings", "X",
          ImVec2(hdrPos.x + hdrSize.x - 48.0f, hdrPos.y + 9.0f),
          ImVec2(40.0f, 40.0f), UiAssets::PlateId::RedDanger, fontDisplay,
          18.0f, IM_COL32(255, 255, 255, 255), IM_COL32(107, 18, 22, 255), true,
          ma)) {
    SwapchainHook::CancelKeybindCapture();
    g_ShowSettings = false;
  }

  float y = mPos.y + 90.0f;
  float cx = mPos.x + 24.0f;
  float cw = curSize.x - 48.0f;

  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 18.0f, cx, y + 11.0f, cw, "CONTROLS & DISPLAY",
      IM_COL32(252, 198, 56, static_cast<int>(255 * ma)));
  y += 32.0f;

  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlateCard, ImVec2(cx, y),
                          ImVec2(cw, 54.0f), ma, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 18.0f, cx + 16.0f, y + 27.0f, cw - 180.0f,
      "Menu Open / Close Key",
      IM_COL32(238, 244, 255, static_cast<int>(255 * ma)));

  bool capturing = SwapchainHook::IsCapturingKeybind();
  std::string keyLbl =
      capturing ? "PRESS KEY..."
                : ("[ " + SwapchainHook::GetMenuToggleKeyName() + " ]");
  if (UiDraw::DrawActionButton(
          draw, "##RebindMenuKeyBtn", keyLbl.c_str(),
          ImVec2(cx + cw - 154.0f, y + 7.0f), ImVec2(142.0f, 40.0f),
          capturing ? UiAssets::PlateId::BlueBanner
                    : UiAssets::PlateId::GoldPill,
          fontBold, 17.0f, IM_COL32(255, 255, 255, 255),
          capturing ? IM_COL32(10, 54, 102, 255) : IM_COL32(88, 48, 4, 255),
          true, ma)) {
    if (capturing)
      SwapchainHook::CancelKeybindCapture();
    else
      SwapchainHook::BeginKeybindCapture();
  }
  y += 66.0f;

  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 18.0f, cx, y + 20.0f, 118.0f, "UI Size",
      IM_COL32(226, 234, 248, static_cast<int>(255 * ma)));
  const char *sizeLabels[] = {"COMPACT", "NORMAL", "LARGE"};
  if (UiDraw::DrawSegmentedToggle(draw, "##UiSizePresetToggle",
                                  ImVec2(cx + 122.0f, y),
                                  ImVec2(cw - 122.0f, 42.0f), sizeLabels, 3,
                                  g_UiSizePreset, fontDisplay, 16.0f, ma)) {
    static const float kPresetScales[3] = {0.90f, 1.00f, 1.12f};
    g_UiScale = kPresetScales[g_UiSizePreset];
    SaveCurrentConfig();
  }
  y += 54.0f;

  char txtSzLbl[48];
  snprintf(txtSzLbl, sizeof(txtSzLbl), "Text Size: %d%%", g_TextSizePct);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 17.0f, cx, y + 10.0f, cw, txtSzLbl,
      IM_COL32(226, 234, 248, static_cast<int>(255 * ma)));
  if (UiDraw::DrawSteppedSlider(draw, "##TextSizeSlider", ImVec2(cx, y + 26.0f),
                                ImVec2(cw, 24.0f), g_TextSizePct, 85, 155,
                                ma)) {
    UiDraw::SetTextScale(static_cast<float>(g_TextSizePct) / 100.0f);
    SaveCurrentConfig();
  }
  y += 60.0f;

  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 18.0f, cx, y + 11.0f, cw, "HOLOGRAM & AUDIO",
      IM_COL32(252, 198, 56, static_cast<int>(255 * ma)));
  y += 32.0f;

  char opLbl[48];
  snprintf(opLbl, sizeof(opLbl), "Ghost Render Opacity: %d%%",
           g_GhostOpacityPct);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 18.0f, cx, y + 11.0f, cw, opLbl,
      IM_COL32(226, 234, 248, static_cast<int>(255 * ma)));
  if (UiDraw::DrawSteppedSlider(draw, "##GhostOpacitySlider",
                                ImVec2(cx, y + 28.0f), ImVec2(cw, 26.0f),
                                g_GhostOpacityPct, 25, 100, ma)) {
    SaveCurrentConfig();
  }
  y += 64.0f;

  float halfRowW = (cw - 14.0f) * 0.5f;
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 17.0f, cx, y + 10.0f, halfRowW, "UI Sound FX",
      IM_COL32(212, 222, 244, static_cast<int>(255 * ma)));
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 17.0f, cx + halfRowW + 14.0f, y + 10.0f, halfRowW,
      "Block Verifier", IM_COL32(212, 222, 244, static_cast<int>(255 * ma)));
  y += 24.0f;

  const char *sfxLabels[] = {"ON", "OFF"};
  if (UiDraw::DrawSegmentedToggle(draw, "##UiSoundFxToggle", ImVec2(cx, y),
                                  ImVec2(halfRowW, 42.0f), sfxLabels, 2,
                                  g_SoundFxToggle, fontDisplay, 16.0f, ma)) {
    SoundManager::SetEnabled(g_SoundFxToggle == 0);
    SaveCurrentConfig();
  }

  const char *verifyLabels[] = {"EXACT", "RELAXED"};
  if (UiDraw::DrawSegmentedToggle(draw, "##VerifyModeToggle",
                                  ImVec2(cx + halfRowW + 14.0f, y),
                                  ImVec2(halfRowW, 42.0f), verifyLabels, 2,
                                  g_VerifyMode, fontDisplay, 16.0f, ma)) {
    SaveCurrentConfig();
  }

  float btnY = mPos.y + curSize.y - 64.0f;
  if (UiDraw::DrawActionButton(draw, "##ResetSettings", "RESET DEFAULTS",
                               ImVec2(cx, btnY), ImVec2(236.0f, 48.0f),
                               UiAssets::PlateId::SlateCard, fontDisplay, 17.0f,
                               IM_COL32(255, 255, 255, 255),
                               IM_COL32(10, 12, 18, 255), true, ma)) {
    SwapchainHook::SetMenuToggleKey('M');
    SwapchainHook::CancelKeybindCapture();
    g_UiSizePreset = 1;
    g_UiScale = 1.0f;
    g_TextSizePct = 115;
    UiDraw::SetTextScale(1.15f);
    g_GhostOpacityPct = 65;
    g_SoundFxToggle = 0;
    SoundManager::SetEnabled(true);
    g_VerifyMode = 0;
    SaveCurrentConfig();
    ToastManager::Show(
        ToastType::Info, "SETTINGS RESET",
        "Restored default keybind [M], UI size, and hologram settings.", "[M]");
  }

  if (UiDraw::DrawActionButton(
          draw, "##DoneSettings", "DONE", ImVec2(cx + cw - 164.0f, btnY),
          ImVec2(164.0f, 48.0f), UiAssets::PlateId::EmeraldCTA, fontDisplay,
          18.0f, IM_COL32(255, 255, 255, 255), IM_COL32(14, 64, 28, 255), true,
          ma)) {
    SwapchainHook::CancelKeybindCapture();
    g_ShowSettings = false;
  }
}

void DrawUnloadConfirmModal(ImDrawList *draw, ImVec2 mPos, ImVec2 curSize,
                            const StructureEntry *active, ImFont *fontDisplay,
                            ImFont *fontBold, ImFont *fontReg, float ma) {
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, mPos,
                          curSize, ma, true);

  ImVec2 hdrPos(mPos.x + 14.0f, mPos.y + 14.0f);
  ImVec2 hdrSize(curSize.x - 28.0f, 54.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::RedDanger, hdrPos, hdrSize,
                          ma, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 20.0f, hdrPos.x + 18.0f, hdrPos.y + 27.0f,
      hdrSize.x - 36.0f, "UNLOAD HOLOGRAM?",
      IM_COL32(255, 255, 255, static_cast<int>(255 * ma)),
      IM_COL32(107, 18, 22, static_cast<int>(255 * ma)));

  char bodyMsg[192];
  snprintf(bodyMsg, sizeof(bodyMsg),
           "Remove '%s' from the world?\nAll hologram layers, rotation, and "
           "offset position will be cleared.",
           active ? active->name.c_str() : "Active Build");
  float wrapW = curSize.x - 48.0f;
  draw->PushClipRect(ImVec2(mPos.x + 20.0f, mPos.y + 78.0f),
                     ImVec2(mPos.x + curSize.x - 20.0f, mPos.y + 190.0f), true);
  draw->AddText(fontReg, 19.0f, ImVec2(mPos.x + 24.0f, mPos.y + 88.0f),
                IM_COL32(228, 236, 250, static_cast<int>(255 * ma)), bodyMsg,
                nullptr, wrapW);
  draw->PopClipRect();

  float btnY = mPos.y + curSize.y - 66.0f;
  float btnW = (curSize.x - 58.0f) * 0.5f;
  if (UiDraw::DrawActionButton(
          draw, "##CancelUnload", "CANCEL", ImVec2(mPos.x + 22.0f, btnY),
          ImVec2(btnW, 48.0f), UiAssets::PlateId::SlateCard, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255), true, ma)) {
    g_ShowUnloadConfirm = false;
  }
  if (UiDraw::DrawActionButton(
          draw, "##ConfirmUnload", "CONFIRM UNLOAD",
          ImVec2(mPos.x + 36.0f + btnW, btnY), ImVec2(btnW, 48.0f),
          UiAssets::PlateId::RedDanger, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(107, 18, 22, 255), true, ma)) {
    std::string nm = active ? active->name : "Schematic";
    std::string kn = SwapchainHook::GetMenuToggleKeyName();
    g_LoadedIndex = -1;
    g_ShowUnloadConfirm = false;
    g_ShowSettings = false;
    SoundManager::Play(UISound::Error);
    ToastManager::Show(ToastType::Warning, "UNLOADED",
                       "Removed '" + nm + "' hologram. Press [" + kn +
                           "] to browse library.",
                       "[" + kn + "]");
  }
}

void ScanImportCandidates() {
  g_ImportCandidates.clear();
  std::vector<std::pair<std::string, std::string>> roots;
  char userProf[MAX_PATH] = {};
  if (GetEnvironmentVariableA("USERPROFILE", userProf, MAX_PATH) > 0) {
    std::string up(userProf);
    roots.push_back({up + "\\Downloads", "Downloads"});
    roots.push_back({up + "\\Desktop", "Desktop"});
  }
  roots.push_back({GetSchematicsDir(), "Schematics"});

  std::error_code ec;
  for (const auto &rt : roots) {
    if (!std::filesystem::exists(rt.first, ec))
      continue;
    for (const auto &entry : std::filesystem::directory_iterator(rt.first, ec)) {
      if (!entry.is_regular_file())
        continue;
      std::string ext = ToLowerCopy(entry.path().extension().string());
      if (ext != ".mcstructure" && ext != ".bmet")
        continue;

      ImportCandidate ic;
      ic.filename = entry.path().filename().string();
      ic.displayName = entry.path().stem().string();
      ic.ext = ext;
      ic.fullPath = entry.path().string();
      ic.sourceLabel = rt.second;

      Bedrockmetica::BmetCodec::BmetSummary sum;
      bool ok = (ext == ".bmet")
                    ? Bedrockmetica::BmetCodec::ReadSummary(ic.fullPath, sum)
                    : Bedrockmetica::McStructureCodec::ReadSummary(ic.fullPath,
                                                                   sum);
      if (ok && sum.valid) {
        if (!sum.name.empty())
          ic.displayName = sum.name;
        ic.w = sum.width;
        ic.h = sum.height;
        ic.d = sum.depth;
        ic.totalBlocks = sum.totalBlocks;
      }
      g_ImportCandidates.push_back(std::move(ic));
      if (g_ImportCandidates.size() >= 64)
        return;
    }
  }
}

void UpsertDiskEntryFromPath(const std::string &filePath) {
  std::filesystem::path p(filePath);
  std::string ext = ToLowerCopy(p.extension().string());
  StructureEntry s;
  s.id = p.filename().string();
  s.name = p.stem().string();
  s.format = ext;
  s.w = 16;
  s.h = 16;
  s.d = 16;
  s.totalBlocks = 0;
  s.thumb = UiAssets::ThumbId::Pagoda;
  s.filePath = filePath;
  s.isDiskFile = true;

  Bedrockmetica::BmetCodec::BmetSummary sum;
  bool ok = (ext == ".bmet")
                ? Bedrockmetica::BmetCodec::ReadSummary(filePath, sum)
                : Bedrockmetica::McStructureCodec::ReadSummary(filePath, sum);
  if (ok && sum.valid) {
    if (!sum.name.empty())
      s.name = sum.name;
    s.w = std::max(1, sum.width);
    s.h = std::max(1, sum.height);
    s.d = std::max(1, sum.depth);
    s.totalBlocks = sum.totalBlocks;
    for (const auto &m : sum.topMaterials) {
      s.materials.push_back(
          {m.displayName, 0, m.required, static_cast<ImU32>(m.approxAbgr)});
    }
  }
  if (s.materials.empty()) {
    s.materials = {{"Empty / Air Only", 0, 0, IM_COL32(130, 145, 170, 255)}};
  }

  for (size_t i = 0; i < g_Entries.size(); ++i) {
    if (g_Entries[i].isDiskFile &&
        ToLowerCopy(g_Entries[i].filePath) == ToLowerCopy(filePath)) {
      g_Entries[i] = std::move(s);
      SelectEntry(static_cast<int>(i));
      return;
    }
  }
  g_Entries.insert(g_Entries.begin(), std::move(s));
  SelectEntry(0);
}

void DrawImportModal(ImDrawList *draw, ImVec2 mPos, ImVec2 curSize,
                     ImFont *fontDisplay, ImFont *fontBold, ImFont *fontReg,
                     float ma) {
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, mPos,
                          curSize, ma, false);

  float hdrH = 54.0f;
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel,
                          ImVec2(mPos.x + 12.0f, mPos.y + 12.0f),
                          ImVec2(curSize.x - 24.0f, hdrH), ma, true);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 20.0f, mPos.x + 28.0f, mPos.y + 39.0f,
      curSize.x - 160.0f, "IMPORT SCHEMATIC",
      IM_COL32(255, 255, 255, static_cast<int>(255 * ma)));

  if (UiDraw::DrawActionButton(
          draw, "##ImportRescanBtn", "RESCAN",
          ImVec2(mPos.x + curSize.x - 188.0f, mPos.y + 20.0f),
          ImVec2(92.0f, 38.0f), UiAssets::PlateId::BlueBanner, fontDisplay,
          13.0f, IM_COL32(255, 255, 255, 255), IM_COL32(10, 54, 102, 255), true,
          ma)) {
    ScanImportCandidates();
  }
  if (UiDraw::DrawActionButton(
          draw, "##ImportCloseBtn", "X",
          ImVec2(mPos.x + curSize.x - 84.0f, mPos.y + 20.0f),
          ImVec2(58.0f, 38.0f), UiAssets::PlateId::RedDanger, fontDisplay,
          15.0f, IM_COL32(255, 255, 255, 255), IM_COL32(107, 18, 22, 255), true,
          ma)) {
    g_ShowImportModal = false;
  }

  UiDraw::DrawTextLeftEllipsis(
      draw, fontReg, 14.0f, mPos.x + 24.0f, mPos.y + 84.0f, curSize.x - 48.0f,
      "Select a .mcstructure or .bmet from Downloads, Desktop, or Schematics:",
      IM_COL32(180, 196, 224, static_cast<int>(255 * ma)), IM_COL32(0, 0, 0, 0),
      0.0f);

  float listX = mPos.x + 20.0f;
  float listY = mPos.y + 102.0f;
  float listW = curSize.x - 40.0f;
  float listH = curSize.y - 122.0f;
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel,
                          ImVec2(listX, listY), ImVec2(listW, listH), ma, true);

  ImGui::SetCursorScreenPos(ImVec2(listX + 8.0f, listY + 8.0f));
  ImGui::BeginChild("##ImportListScroll", ImVec2(listW - 16.0f, listH - 16.0f),
                    false, ImGuiWindowFlags_NoBackground);
  ImDrawList *cdraw = ImGui::GetWindowDrawList();

  if (g_ImportCandidates.empty()) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    UiDraw::DrawTextLeftEllipsis(
        cdraw, fontBold, 15.0f, p.x + 14.0f, p.y + 28.0f, listW - 44.0f,
        "No .mcstructure or .bmet files found in Downloads / Desktop / Schematics.",
        IM_COL32(165, 178, 205, static_cast<int>(255 * ma)),
        IM_COL32(0, 0, 0, 0), 0.0f);
  } else {
    for (size_t i = 0; i < g_ImportCandidates.size(); ++i) {
      const auto &ic = g_ImportCandidates[i];
      ImVec2 rp = ImGui::GetCursorScreenPos();
      float rowW = listW - 24.0f;
      float rowH = 54.0f;
      ImGui::PushID(static_cast<int>(i));
      UiDraw::DrawPlate9Slice(cdraw, UiAssets::PlateId::SlateCard, rp,
                              ImVec2(rowW, rowH), ma, true);
      UiDraw::DrawTextLeftEllipsis(
          cdraw, fontBold, 15.0f, rp.x + 14.0f, rp.y + 18.0f, rowW - 150.0f,
          ic.displayName.c_str(),
          IM_COL32(255, 255, 255, static_cast<int>(255 * ma)));

      char subBuf[128];
      snprintf(subBuf, sizeof(subBuf), "%s  |  %s  |  %dx%dx%d (%d blks)",
               ic.sourceLabel.c_str(), ic.ext.c_str(), ic.w, ic.h, ic.d,
               ic.totalBlocks);
      UiDraw::DrawTextLeftEllipsis(
          cdraw, fontReg, 13.0f, rp.x + 14.0f, rp.y + 37.0f, rowW - 150.0f,
          subBuf, IM_COL32(162, 180, 212, static_cast<int>(255 * ma)),
          IM_COL32(0, 0, 0, 0), 0.0f);

      if (UiDraw::DrawActionButton(
              cdraw, "##ImpRowBtn", "IMPORT",
              ImVec2(rp.x + rowW - 116.0f, rp.y + 9.0f), ImVec2(104.0f, 36.0f),
              UiAssets::PlateId::EmeraldCTA, fontDisplay, 13.5f,
              IM_COL32(255, 255, 255, 255), IM_COL32(12, 62, 26, 255), true,
              ma)) {
        std::string destDir = GetSchematicsDir();
        std::error_code ec;
        std::filesystem::create_directories(destDir, ec);
        std::filesystem::path srcPath(ic.fullPath);
        std::filesystem::path dstPath =
            std::filesystem::path(destDir) / srcPath.filename();
        if (ToLowerCopy(srcPath.string()) != ToLowerCopy(dstPath.string())) {
          std::filesystem::copy_file(
              srcPath, dstPath,
              std::filesystem::copy_options::overwrite_existing, ec);
        }
        UpsertDiskEntryFromPath(dstPath.string());
        g_ShowImportModal = false;
        ToastManager::Show(ToastType::Success, "IMPORTED",
                           "Added '" + ic.displayName + "' to library.");
      }
      ImGui::Dummy(ImVec2(rowW, rowH + 6.0f));
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void DrawSaveCaptureModal(ImDrawList *draw, ImVec2 mPos, ImVec2 curSize,
                          ImFont *fontDisplay, ImFont *fontBold,
                          ImFont *fontReg, float ma) {
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, mPos,
                          curSize, ma, true);

  ImVec2 hdrPos(mPos.x + 14.0f, mPos.y + 14.0f);
  ImVec2 hdrSize(curSize.x - 28.0f, 54.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::EmeraldCTA, hdrPos, hdrSize,
                          ma, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 20.0f, hdrPos.x + 18.0f, hdrPos.y + 27.0f,
      hdrSize.x - 36.0f, "SAVE CAPTURED STRUCTURE",
      IM_COL32(255, 255, 255, static_cast<int>(255 * ma)),
      IM_COL32(12, 62, 26, static_cast<int>(255 * ma)));

  int bw = Bedrockmetica::CaptureManager::GetBoxWidth();
  int bh = Bedrockmetica::CaptureManager::GetBoxHeight();
  int bd = Bedrockmetica::CaptureManager::GetBoxDepth();
  int blkCount = Bedrockmetica::CaptureManager::GetCapturedBlockCount();
  int pct = Bedrockmetica::CaptureManager::GetCapturedPercent();

  char infoBuf[160];
  snprintf(infoBuf, sizeof(infoBuf),
           "Bounds: %d x %d x %d   |   Non-Air: %d   |   Loaded: %d%%", bw, bh,
           bd, blkCount, pct);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontBold, 15.0f, mPos.x + 24.0f, mPos.y + 92.0f, curSize.x - 48.0f,
      infoBuf, IM_COL32(180, 214, 255, static_cast<int>(255 * ma)));

  ImVec2 boxPos(mPos.x + 24.0f, mPos.y + 116.0f);
  ImVec2 boxSize(curSize.x - 48.0f, 46.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::DarkInset, boxPos, boxSize,
                          ma, false);

  float fontSz = UiDraw::ScaleFontSize(fontReg, 16.0f);
  float padY = std::max(0.0f, std::round((boxSize.y - fontSz) * 0.5f));
  ImGui::SetCursorScreenPos(ImVec2(boxPos.x + 14.0f, boxPos.y));
  ImGui::PushItemWidth(boxSize.x - 28.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, padY));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        IM_COL32(236, 242, 255, static_cast<int>(255 * ma)));
  ImGui::PushFont(fontReg, fontSz);
  ImGui::InputTextWithHint("##SaveStructName", "Enter structure name...",
                           g_SaveNameBuf, sizeof(g_SaveNameBuf));
  ImGui::PopFont();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(1);
  ImGui::PopItemWidth();

  float btnY = mPos.y + curSize.y - 66.0f;
  float btnW = (curSize.x - 58.0f) * 0.5f;
  if (UiDraw::DrawActionButton(
          draw, "##CancelSaveModal", "CANCEL", ImVec2(mPos.x + 22.0f, btnY),
          ImVec2(btnW, 48.0f), UiAssets::PlateId::SlateCard, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255), true, ma)) {
    g_ShowSaveModal = false;
  }

  if (UiDraw::DrawActionButton(
          draw, "##ConfirmSaveModal", "SAVE .BMET",
          ImVec2(mPos.x + 36.0f + btnW, btnY), ImVec2(btnW, 48.0f),
          UiAssets::PlateId::EmeraldCTA, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(12, 62, 26, 255), true, ma)) {
    std::string finalName = g_SaveNameBuf;
    if (finalName.empty()) {
      finalName = "Capture";
    }
    std::string savedPath;
    Bedrockmetica::SchematicDocument doc;
    if (Bedrockmetica::CaptureManager::FinalizeAndSaveBmet(
            finalName, GetSchematicsDir(), savedPath, doc)) {
      StructureEntry s;
      s.id = std::filesystem::path(savedPath).filename().string();
      s.name = doc.name;
      s.format = ".bmet";
      s.w = std::max(1, doc.width);
      s.h = std::max(1, doc.height);
      s.d = std::max(1, doc.depth);
      s.totalBlocks = doc.totalNonAirBlocks;
      s.thumb = UiAssets::ThumbId::Pagoda;
      s.filePath = savedPath;
      s.isDiskFile = true;
      std::vector<Bedrockmetica::MaterialCount> mats = doc.BuildMaterialTally();
      for (size_t i = 0; i < mats.size() && i < 6; ++i) {
        s.materials.push_back({mats[i].displayName, 0, mats[i].required,
                               static_cast<ImU32>(mats[i].approxAbgr)});
      }
      if (s.materials.empty()) {
        s.materials.push_back(
            {"Stone", 0, s.totalBlocks, IM_COL32(138, 144, 152, 255)});
      }
      g_Entries.insert(g_Entries.begin(), std::move(s));
      g_SelectedIndex = 0;
      g_ShowSaveModal = false;
      g_CaptureMode = false;
      Bedrockmetica::CaptureManager::SetActive(false);
      SoundManager::Play(UISound::Success);
      ToastManager::Show(ToastType::Success, "SAVED .BMET",
                         "Saved '" + doc.name + "' (" +
                             std::to_string(doc.totalNonAirBlocks) +
                             " blocks).");
    } else {
      SoundManager::Play(UISound::Error);
      ToastManager::Show(ToastType::Error, "SAVE FAILED",
                         "Could not write .bmet file to schematics folder.");
    }
  }
}

void DrawRenameModal(ImDrawList *draw, ImVec2 mPos, ImVec2 curSize,
                     ImFont *fontDisplay, ImFont *fontBold, ImFont *fontReg,
                     float ma) {
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, mPos,
                          curSize, ma, true);

  ImVec2 hdrPos(mPos.x + 14.0f, mPos.y + 14.0f);
  ImVec2 hdrSize(curSize.x - 28.0f, 54.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::BlueBanner, hdrPos, hdrSize,
                          ma, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, 20.0f, hdrPos.x + 18.0f, hdrPos.y + 27.0f,
      hdrSize.x - 36.0f, "RENAME STRUCTURE",
      IM_COL32(255, 255, 255, static_cast<int>(255 * ma)),
      IM_COL32(10, 54, 102, static_cast<int>(255 * ma)));

  ImVec2 boxPos(mPos.x + 24.0f, mPos.y + 98.0f);
  ImVec2 boxSize(curSize.x - 48.0f, 46.0f);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::DarkInset, boxPos, boxSize,
                          ma, false);

  float fontSz = UiDraw::ScaleFontSize(fontReg, 16.0f);
  float padY = std::max(0.0f, std::round((boxSize.y - fontSz) * 0.5f));
  ImGui::SetCursorScreenPos(ImVec2(boxPos.x + 14.0f, boxPos.y));
  ImGui::PushItemWidth(boxSize.x - 28.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, padY));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        IM_COL32(236, 242, 255, static_cast<int>(255 * ma)));
  ImGui::PushFont(fontReg, fontSz);
  ImGui::InputTextWithHint("##RenameStructName", "New structure name...",
                           g_RenameNameBuf, sizeof(g_RenameNameBuf));
  ImGui::PopFont();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(1);
  ImGui::PopItemWidth();

  float btnY = mPos.y + curSize.y - 66.0f;
  float btnW = (curSize.x - 58.0f) * 0.5f;
  if (UiDraw::DrawActionButton(
          draw, "##CancelRenameModal", "CANCEL", ImVec2(mPos.x + 22.0f, btnY),
          ImVec2(btnW, 48.0f), UiAssets::PlateId::SlateCard, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255), true, ma)) {
    g_ShowRenameModal = false;
  }

  if (UiDraw::DrawActionButton(
          draw, "##ConfirmRenameModal", "APPLY RENAME",
          ImVec2(mPos.x + 36.0f + btnW, btnY), ImVec2(btnW, 48.0f),
          UiAssets::PlateId::EmeraldCTA, fontDisplay, 17.0f,
          IM_COL32(255, 255, 255, 255), IM_COL32(12, 62, 26, 255), true, ma)) {
    std::string newName = g_RenameNameBuf;
    if (!newName.empty() && g_SelectedIndex >= 0 &&
        g_SelectedIndex < static_cast<int>(g_Entries.size())) {
      StructureEntry &entry = g_Entries[g_SelectedIndex];
      bool ok = true;
      if (entry.isDiskFile && !entry.filePath.empty()) {
        std::string newPath;
        if (entry.format == ".bmet") {
          ok = Bedrockmetica::BmetCodec::RenameInFile(entry.filePath, newName,
                                                      newPath);
        } else {
          std::error_code ec;
          std::filesystem::path oldP(entry.filePath);
          std::filesystem::path dstP =
              oldP.parent_path() / (newName + entry.format);
          std::filesystem::rename(oldP, dstP, ec);
          ok = !ec;
          if (ok)
            newPath = dstP.string();
        }
        if (ok && !newPath.empty()) {
          entry.filePath = newPath;
          entry.id = std::filesystem::path(newPath).filename().string();
        }
      }
      if (ok) {
        entry.name = newName;
        g_ShowRenameModal = false;
        SoundManager::Play(UISound::Success);
        ToastManager::Show(ToastType::Success, "RENAMED",
                           "Structure renamed to '" + newName + "'.");
      } else {
        SoundManager::Play(UISound::Error);
        ToastManager::Show(ToastType::Error, "RENAME FAILED",
                           "Target filename already exists or is locked.");
      }
    }
  }
}

void DrawCollapsedCornerHud(ImDrawList *draw, float screenW, float screenH,
                            ImFont *fontDisplay, ImFont *fontBold,
                            ImFont *fontReg) {
  std::string kn = SwapchainHook::GetMenuToggleKeyName();
  if (g_CaptureMode) {
    ImVec2 capSize(std::min(620.0f, screenW - 40.0f), 58.0f);
    ImVec2 capPos(std::round((screenW - capSize.x) * 0.5f), 18.0f);
    Bedrockmetica::CaptureManager::CapturePhase phase =
        Bedrockmetica::CaptureManager::GetPhase();
    UiAssets::PlateId bannerId =
        (phase ==
         Bedrockmetica::CaptureManager::CapturePhase::BoxLockedReadyToSave)
            ? UiAssets::PlateId::EmeraldCTA
            : UiAssets::PlateId::BlueBanner;
    UiDraw::DrawPlate9Slice(draw, bannerId, capPos, capSize, 0.96f, true);
    std::string capTxt;
    if (phase == Bedrockmetica::CaptureManager::CapturePhase::ArmedNeedsPos1) {
      capTxt = "CAPTURE: RIGHT-CLICK POS 1  |  [" + kn + "] MENU";
    } else if (phase ==
               Bedrockmetica::CaptureManager::CapturePhase::HasPos1NeedsPos2) {
      capTxt = "POS 1 SET: RIGHT-CLICK POS 2  |  [" + kn + "] MENU";
    } else if (phase == Bedrockmetica::CaptureManager::CapturePhase::
                            BoxLockedReadyToSave) {
      int bw = Bedrockmetica::CaptureManager::GetBoxWidth();
      int bh = Bedrockmetica::CaptureManager::GetBoxHeight();
      int bd = Bedrockmetica::CaptureManager::GetBoxDepth();
      int blk = Bedrockmetica::CaptureManager::GetCapturedBlockCount();
      char buf[128];
      snprintf(buf, sizeof(buf), "BOX READY %dx%dx%d (%d BLKS)  |  [%s] SAVE",
               bw, bh, bd, blk, kn.c_str());
      capTxt = buf;
    } else {
      capTxt = "CAPTURE MODE ACTIVE  |  [" + kn + "] MENU";
    }
    UiDraw::DrawTextCentered(
        draw, fontDisplay, 17.5f, capPos,
        ImVec2(capPos.x + capSize.x, capPos.y + capSize.y - 2.0f),
        capTxt.c_str(), IM_COL32(255, 255, 255, 255),
        IM_COL32(10, 54, 102, 255));
  }

  if (g_LoadedIndex >= 0 &&
      g_LoadedIndex < static_cast<int>(g_Entries.size())) {
    const auto &s = g_Entries[g_LoadedIndex];

    int totalHave = 0, totalNeed = 0;
    std::string nextMiss = "All blocks complete!";
    for (const auto &m : s.materials) {
      totalHave += std::min(m.placed, m.required);
      totalNeed += m.required;
    }
    for (const auto &m : s.materials) {
      if (m.placed < m.required) {
        char nb[64];
        snprintf(nb, sizeof(nb), "Next: %s (%d/%d)", m.name.c_str(), m.placed,
                 m.required);
        nextMiss = nb;
        break;
      }
    }
    int pct = (totalNeed > 0) ? (totalHave * 100 / totalNeed) : 0;

    ImVec2 hudSize(440.0f, 102.0f);
    ImVec2 hudPos(22.0f, screenH - hudSize.y - 22.0f);
    UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, hudPos,
                            hudSize, 0.96f, true);
    UiDraw::DrawDiamondBadge(
        draw, UiAssets::BadgeId::Cube,
        ImVec2(hudPos.x + 36.0f, hudPos.y + hudSize.y * 0.5f), 42.0f);

    char topLine[96];
    snprintf(topLine, sizeof(topLine), "%s  -  Layer %d / %d (%d%%)",
             s.name.c_str(), g_SliceLayer, s.h, pct);
    UiDraw::DrawTextLeftEllipsis(draw, fontBold, 19.0f, hudPos.x + 68.0f,
                                 hudPos.y + 33.0f, hudSize.x - 82.0f, topLine,
                                 IM_COL32(128, 255, 168, 255));

    std::string subLine = nextMiss + "  |  [" + kn + "]";
    UiDraw::DrawTextLeftEllipsis(draw, fontReg, 17.0f, hudPos.x + 68.0f,
                                 hudPos.y + 66.0f, hudSize.x - 82.0f,
                                 subLine.c_str(), IM_COL32(204, 214, 238, 255),
                                 IM_COL32(0, 0, 0, 0), 0.0f);
  }
}

void RenderScreen1Library(float screenWidth, float screenHeight, float easeOut,
                          float alpha, ImFont *fontDisplay, ImFont *fontBold,
                          ImFont *fontReg) {
  ImDrawList *bgDraw = ImGui::GetBackgroundDrawList();
  bgDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screenWidth, screenHeight),
                        IM_COL32(6, 8, 14, static_cast<int>(155 * alpha)));

  float fitScale = std::min((screenWidth - 40.0f) / 1440.0f,
                            (screenHeight - 40.0f) / 840.0f);
  float baseScale = std::clamp(fitScale * g_UiScale, 0.72f, 1.05f);
  float animScale = baseScale * (0.97f + 0.03f * easeOut);

  const float winW = std::round(1440.0f * animScale);
  const float winH = std::round(840.0f * animScale);
  const float slideY = (1.0f - easeOut) * 10.0f;
  const ImVec2 winPos(std::round((screenWidth - winW) * 0.5f),
                      std::round((screenHeight - winH) * 0.5f + slideY));
  const ImVec2 winSize(winW, winH);

  auto FsDisp = [&](float base) {
    return std::max(18.0f, std::round((base + 3.0f) * baseScale));
  };
  auto FsBold = [&](float base) {
    return std::max(17.0f, std::round((base + 1.5f) * baseScale));
  };
  auto FsReg = [&](float base) {
    return std::max(16.0f, std::round((base + 1.5f) * baseScale));
  };

  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!SwapchainHook::IsMenuOpen())
    hostFlags |= ImGuiWindowFlags_NoInputs;

  ImGui::SetNextWindowPos(winPos);
  ImGui::SetNextWindowSize(winSize);
  ImGui::Begin("##MainLibraryHost", nullptr, hostFlags);

  const bool modalActive = (g_ShowSettings || g_ShowUnloadConfirm);
  if (modalActive)
    ImGui::BeginDisabled(true);

  ImDrawList *draw = ImGui::GetWindowDrawList();
  float s = animScale;

  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, winPos,
                          winSize, alpha, true);
  UiDraw::DrawSubtleGrid(draw, winPos, winSize, 26.0f * s, alpha);

  const float pad = std::round(20.0f * s);
  const ImVec2 hdrPos(winPos.x + pad, winPos.y + std::round(18.0f * s));
  const ImVec2 hdrSize(winW - pad * 2.0f, std::round(74.0f * s));
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::GoldHeader, hdrPos, hdrSize,
                          alpha, true);

  UiDraw::DrawDiamondBadge(
      draw, UiAssets::BadgeId::Cube,
      ImVec2(hdrPos.x + 40.0f * s, hdrPos.y + hdrSize.y * 0.5f), 48.0f * s,
      alpha);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(25.0f), hdrPos.x + 78.0f * s,
      hdrPos.y + hdrSize.y * 0.5f, 400.0f * s, "BEDROCKMETICA",
      IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)),
      IM_COL32(104, 58, 4, static_cast<int>(255 * alpha)), 2.5f);

  float hBtnH = std::round(48.0f * s);
  float hBtnY = hdrPos.y + (hdrSize.y - hBtnH) * 0.5f;
  float rx = hdrPos.x + hdrSize.x - 14.0f * s;

  if (UiDraw::DrawActionButton(
          draw, "##CloseMenuBtn", "X", ImVec2(rx - 54.0f * s, hBtnY),
          ImVec2(54.0f * s, hBtnH), UiAssets::PlateId::RedDanger, fontDisplay,
          FsDisp(17.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(107, 18, 22, 255), true, alpha)) {
    SwapchainHook::SetMenuOpen(false);
  }
  rx -= (54.0f + 10.0f) * s;

  if (UiDraw::DrawActionButton(
          draw, "##SettingsBtn", "SETTINGS", ImVec2(rx - 186.0f * s, hBtnY),
          ImVec2(186.0f * s, hBtnH), UiAssets::PlateId::SlateCard, fontDisplay,
          FsDisp(16.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 12, 18, 255), true, alpha)) {
    g_ShowSettings = !g_ShowSettings;
  }
  rx -= (186.0f + 10.0f) * s;

  if (UiDraw::DrawActionButton(
          draw, "##CaptureHeaderBtn",
          g_CaptureMode ? "CAPTURE: ON" : "CAPTURE [B]",
          ImVec2(rx - 228.0f * s, hBtnY), ImVec2(228.0f * s, hBtnH),
          g_CaptureMode ? UiAssets::PlateId::EmeraldCTA
                        : UiAssets::PlateId::BlueBanner,
          fontDisplay, FsDisp(16.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 56, 106, 255), true, alpha)) {
    g_CaptureMode = !g_CaptureMode;
    Bedrockmetica::CaptureManager::SetActive(g_CaptureMode);
    std::string kn = SwapchainHook::GetMenuToggleKeyName();
    ToastManager::Show(g_CaptureMode ? ToastType::Info : ToastType::Warning,
                       "CAPTURE MODE",
                       g_CaptureMode ? ("Capture mode enabled! Close menu [" +
                                        kn + "] and select Pos1 / Pos2.")
                                     : "Capture mode paused.",
                       "[" + kn + "]");
  }
  rx -= (228.0f + 10.0f) * s;

  if (Bedrockmetica::CaptureManager::HasCapturedBox()) {
    if (UiDraw::DrawActionButton(draw, "##SaveCaptureHeaderBtn", "SAVE CAPTURE",
                                 ImVec2(rx - 210.0f * s, hBtnY),
                                 ImVec2(210.0f * s, hBtnH),
                                 UiAssets::PlateId::EmeraldCTA, fontDisplay,
                                 FsDisp(15.5f), IM_COL32(255, 255, 255, 255),
                                 IM_COL32(12, 62, 26, 255), true, alpha)) {
      snprintf(g_SaveNameBuf, sizeof(g_SaveNameBuf), "Capture %dx%dx%d",
               Bedrockmetica::CaptureManager::GetBoxWidth(),
               Bedrockmetica::CaptureManager::GetBoxHeight(),
               Bedrockmetica::CaptureManager::GetBoxDepth());
      g_ShowSaveModal = true;
    }
  }

  const float bodyY = hdrPos.y + hdrSize.y + std::round(16.0f * s);
  const float bodyH = (winPos.y + winH - pad) - bodyY;
  const float leftW = std::round(484.0f * s);
  const ImVec2 leftPos(winPos.x + pad, bodyY);
  const ImVec2 leftSize(leftW, bodyH);

  const float botBarH = std::round(54.0f * s);
  const float ctaY = winPos.y + winH - pad - std::round(14.0f * s) - botBarH;

  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel, leftPos,
                          leftSize, alpha, true);

  ImVec2 lRibPos(leftPos.x + 14.0f * s, leftPos.y + 14.0f * s);
  ImVec2 lRibSize(leftW - 28.0f * s, std::round(48.0f * s));
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::BlueBanner, lRibPos,
                          lRibSize, alpha, false);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(16.0f), lRibPos.x + 16.0f * s,
      lRibPos.y + lRibSize.y * 0.5f, 220.0f * s, "STRUCTURES",
      IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)),
      IM_COL32(10, 54, 102, static_cast<int>(255 * alpha)));

  char countBuf[32];
  snprintf(countBuf, sizeof(countBuf), "%d Saved",
           static_cast<int>(g_Entries.size()));
  UiDraw::DrawStatusPill(
      draw, ImVec2(lRibPos.x + lRibSize.x - 124.0f * s, lRibPos.y + 7.0f * s),
      ImVec2(114.0f * s, lRibSize.y - 14.0f * s), UiAssets::PlateId::DarkInset,
      fontBold, FsBold(15.0f), countBuf, IM_COL32(224, 244, 255, 255),
      IM_COL32(10, 12, 18, 255), alpha);

  ImVec2 searchPos(leftPos.x + 14.0f * s, lRibPos.y + lRibSize.y + 10.0f * s);
  ImVec2 searchSize(leftW - 28.0f * s, std::round(42.0f * s));
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::DarkInset, searchPos,
                          searchSize, alpha, false);

  float searchFontSz = std::min(searchSize.y - 10.0f,
                                UiDraw::ScaleFontSize(fontReg, FsReg(14.5f)));
  float padY = std::max(0.0f, std::round((searchSize.y - searchFontSz) * 0.5f));
  ImGui::SetCursorScreenPos(ImVec2(searchPos.x + 12.0f * s, searchPos.y));
  ImGui::PushItemWidth(searchSize.x - 52.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, padY));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        IM_COL32(236, 242, 255, static_cast<int>(255 * alpha)));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                        IM_COL32(150, 166, 198, static_cast<int>(220 * alpha)));
  ImGui::PushFont(fontReg, searchFontSz);
  ImGui::InputTextWithHint("##StructSearch", "Search saved structures...",
                           g_SearchBuf, sizeof(g_SearchBuf));
  ImGui::PopFont();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(1);
  ImGui::PopItemWidth();

  if (g_SearchBuf[0] != '\0') {
    if (UiDraw::DrawActionButton(draw, "##ClearSearch", "X",
                                 ImVec2(searchPos.x + searchSize.x - 34.0f * s,
                                        searchPos.y + 7.0f * s),
                                 ImVec2(28.0f * s, 28.0f * s),
                                 UiAssets::PlateId::SlateCard, fontDisplay,
                                 12.0f, IM_COL32(255, 255, 255, 255),
                                 IM_COL32(10, 12, 18, 255), true, alpha)) {
      g_SearchBuf[0] = '\0';
    }
  }

  float halfBtnW = (leftW - 28.0f * s - 10.0f * s) * 0.5f;
  if (UiDraw::DrawActionButton(
          draw, "##ImportBtn", "+ IMPORT", ImVec2(leftPos.x + 14.0f * s, ctaY),
          ImVec2(halfBtnW, botBarH), UiAssets::PlateId::BlueBanner, fontDisplay,
          FsDisp(15.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 54, 102, 255), true, alpha)) {
    ScanImportCandidates();
    g_ShowImportModal = true;
  }
  if (UiDraw::DrawActionButton(
          draw, "##OpenFolderBtn", "FOLDER",
          ImVec2(leftPos.x + 14.0f * s + halfBtnW + 10.0f * s, ctaY),
          ImVec2(halfBtnW, botBarH), UiAssets::PlateId::SlateCard, fontDisplay,
          FsDisp(15.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 12, 18, 255), true, alpha)) {
    std::string dir = GetSchematicsDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr,
                  SW_SHOWDEFAULT);
    ToastManager::Show(ToastType::Success, "FOLDER",
                       "Opened local schematics directory.");
  }

  ImVec2 listPos(leftPos.x + 14.0f * s, searchPos.y + searchSize.y + 12.0f * s);
  ImVec2 listSize(leftW - 28.0f * s,
                  std::max(80.0f, ctaY - 12.0f * s - listPos.y));

  std::string qLower = ToLowerCopy(g_SearchBuf);
  std::vector<int> filteredIndices;
  for (int i = 0; i < static_cast<int>(g_Entries.size()); ++i) {
    const auto &item = g_Entries[i];
    if (qLower.empty() ||
        ToLowerCopy(item.name).find(qLower) != std::string::npos ||
        ToLowerCopy(item.format).find(qLower) != std::string::npos) {
      filteredIndices.push_back(i);
    }
  }

  const float cardH = std::max(106.0f, std::round(112.0f * s));
  const float cardGap = std::round(10.0f * s);
  float totalContentH =
      static_cast<float>(filteredIndices.size()) * (cardH + cardGap);
  bool needsScroll = totalContentH > listSize.y;

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(14, 17, 25, 160));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(96, 114, 152, 220));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                        IM_COL32(252, 198, 56, 240));
  ImGui::SetCursorScreenPos(listPos);
  ImGui::BeginChild("##StructuresScroll", listSize, false,
                    ImGuiWindowFlags_NoBackground);
  ImDrawList *cDraw = ImGui::GetWindowDrawList();

  float scrollY = ImGui::GetScrollY();
  float availW = listSize.x - (needsScroll ? 12.0f : 0.0f);
  float curY = listPos.y - scrollY;

  for (size_t fi = 0; fi < filteredIndices.size(); ++fi) {
    int i = filteredIndices[fi];
    const auto &item = g_Entries[i];

    ImVec2 cPos(listPos.x, curY);
    ImVec2 cSize(availW, cardH);

    ImGui::SetCursorScreenPos(cPos);
    char cardId[64];
    snprintf(cardId, sizeof(cardId), "##StructCard_%d", i);
    ImGui::InvisibleButton(cardId, cSize);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    bool isSelected = (i == g_SelectedIndex);

    float hoverAnim =
        UiDraw::StepAnimValue(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 18.0f);
    ImVec2 drawCPos(cPos.x + hoverAnim * 2.0f, cPos.y);

    UiAssets::PlateId plate =
        isSelected ? UiAssets::PlateId::GoldCard : UiAssets::PlateId::SlateCard;
    UiDraw::DrawPlate9Slice(
        cDraw, plate, drawCPos, cSize, alpha, isSelected,
        IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));

    if (hoverAnim > 0.02f && !isSelected) {
      UiDraw::DrawChamferOutline(
          cDraw, ImVec2(drawCPos.x + 2.0f, drawCPos.y + 2.0f),
          ImVec2(cSize.x - 4.0f, cSize.y - 4.0f), 5.0f,
          IM_COL32(255, 255, 255, static_cast<int>(55 * hoverAnim * alpha)),
          1.5f);
    }

    float thSize = std::round(74.0f * s);
    ImVec2 thMin(drawCPos.x + 12.0f * s, drawCPos.y + (cardH - thSize) * 0.5f);
    ImVec2 thMax(thMin.x + thSize, thMin.y + thSize);
    ImTextureID thTex = UiAssets::GetThumbTex(item.thumb);
    if (thTex != 0) {
      cDraw->AddImage(thTex, thMin, thMax, ImVec2(0, 0), ImVec2(1, 1),
                      IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
    } else {
      cDraw->AddRectFilled(thMin, thMax,
                           IM_COL32(13, 17, 25, static_cast<int>(255 * alpha)),
                           3.0f);
    }

    float txL = thMax.x + 12.0f * s;
    float txMaxW = (drawCPos.x + cSize.x - 12.0f * s) - txL;
    ImU32 nameCol =
        isSelected ? IM_COL32(255, 224, 110, static_cast<int>(255 * alpha))
                   : IM_COL32(244, 248, 255, static_cast<int>(255 * alpha));
    UiDraw::DrawTextLeftEllipsis(cDraw, fontBold, FsBold(21.0f), txL,
                                 drawCPos.y + 24.0f * s, txMaxW,
                                 item.name.c_str(), nameCol);

    char dimBuf[32], blkBuf[32];
    snprintf(dimBuf, sizeof(dimBuf), "%dx%dx%d", item.w, item.h, item.d);
    snprintf(blkBuf, sizeof(blkBuf), "%d blks", item.totalBlocks);

    float p1W = std::round(112.0f * s);
    float p2W = std::round(126.0f * s);
    float pillH = std::max(26.0f, std::round(28.0f * s));
    UiDraw::DrawStatusPill(
        cDraw, ImVec2(txL, drawCPos.y + 44.0f * s), ImVec2(p1W, pillH),
        UiAssets::PlateId::BlueBanner, fontBold, FsBold(15.0f), dimBuf,
        IM_COL32(226, 248, 255, 255), IM_COL32(10, 48, 86, 255), alpha);
    UiDraw::DrawStatusPill(
        cDraw, ImVec2(txL + p1W + 8.0f * s, drawCPos.y + 44.0f * s),
        ImVec2(p2W, pillH), UiAssets::PlateId::DarkInset, fontBold,
        FsBold(15.0f), blkBuf, IM_COL32(224, 232, 246, 255),
        IM_COL32(10, 12, 18, 255), alpha);

    float badgeW = isSelected ? 106.0f * s : 0.0f;
    UiDraw::DrawTextLeftEllipsis(
        cDraw, fontReg, FsReg(16.0f), txL, drawCPos.y + 88.0f * s,
        txMaxW - badgeW - 6.0f * s, item.format.c_str(),
        IM_COL32(174, 188, 218, static_cast<int>(255 * alpha)),
        IM_COL32(0, 0, 0, 0), 0.0f);

    if (isSelected) {
      UiDraw::DrawStatusPill(
          cDraw,
          ImVec2(drawCPos.x + cSize.x - 110.0f * s, drawCPos.y + 74.0f * s),
          ImVec2(98.0f * s, std::max(25.0f, std::round(26.0f * s))),
          UiAssets::PlateId::GoldPill, fontBold, FsBold(15.0f), "Selected",
          IM_COL32(255, 255, 255, 255), IM_COL32(88, 48, 4, 255), alpha);
    }

    if (clicked) {
      SelectEntry(i);
    }

    curY += cardH + cardGap;
  }

  if (filteredIndices.empty()) {
    UiDraw::DrawTextCentered(
        cDraw, fontBold, FsBold(16.0f), listPos,
        ImVec2(listPos.x + listSize.x, listPos.y + 120.0f * s),
        "No structures match search filter.",
        IM_COL32(146, 158, 188, static_cast<int>(255 * alpha)));
  } else {
    ImGui::Dummy(ImVec2(1.0f, std::max(0.0f, totalContentH - cardGap)));
  }
  ImGui::EndChild();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);

  const float rightX = leftPos.x + leftSize.x + std::round(18.0f * s);
  const float rightW = (winPos.x + winW - pad) - rightX;
  const StructureEntry *sel =
      (g_SelectedIndex >= 0 &&
       g_SelectedIndex < static_cast<int>(g_Entries.size()))
          ? &g_Entries[g_SelectedIndex]
          : nullptr;

  const float vpH = ctaY - std::round(14.0f * s) - bodyY;
  const ImVec2 vpPos(rightX, bodyY);
  const ImVec2 vpSize(rightW, vpH);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::DarkInset, vpPos, vpSize,
                          alpha, true);

  const float tbH = std::round(58.0f * s);
  draw->AddRectFilled(ImVec2(vpPos.x + 4.0f, vpPos.y + 4.0f),
                      ImVec2(vpPos.x + vpSize.x - 4.0f, vpPos.y + tbH),
                      IM_COL32(22, 27, 39, static_cast<int>(255 * alpha)));
  draw->AddLine(ImVec2(vpPos.x + 4.0f, vpPos.y + tbH),
                ImVec2(vpPos.x + vpSize.x - 4.0f, vpPos.y + tbH),
                IM_COL32(46, 56, 80, static_cast<int>(255 * alpha)), 2.0f);

  const char *viewTabs[] = {"3D VIEW", "MATERIALS"};
  UiDraw::DrawSegmentedToggle(
      draw, "##RightViewTabs", ImVec2(vpPos.x + 14.0f * s, vpPos.y + 10.0f * s),
      ImVec2(326.0f * s, std::round(40.0f * s)), viewTabs, 2, g_RightTab,
      fontDisplay, FsDisp(15.0f), alpha);

  if (sel) {
    char vpTitle[96];
    snprintf(vpTitle, sizeof(vpTitle), "%s  -  %d x %d x %d", sel->name.c_str(),
             sel->w, sel->h, sel->d);
    UiDraw::DrawTextCentered(
        draw, fontBold, FsBold(17.0f),
        ImVec2(vpPos.x + 346.0f * s, vpPos.y + 8.0f * s),
        ImVec2(vpPos.x + vpSize.x - 204.0f * s, vpPos.y + tbH - 4.0f), vpTitle,
        IM_COL32(224, 234, 252, static_cast<int>(255 * alpha)),
        IM_COL32(0, 0, 0, 0), 0.0f);

    char matCntBuf[48];
    snprintf(matCntBuf, sizeof(matCntBuf), "%d Block Types",
             static_cast<int>(sel->materials.size()));
    UiDraw::DrawStatusPill(
        draw, ImVec2(vpPos.x + vpSize.x - 196.0f * s, vpPos.y + 10.0f * s),
        ImVec2(182.0f * s, std::round(40.0f * s)), UiAssets::PlateId::GoldPill,
        fontBold, FsBold(15.0f), matCntBuf, IM_COL32(255, 255, 255, 255),
        IM_COL32(88, 48, 4, 255), alpha);
  }

  ImVec2 contentVpPos(vpPos.x + 4.0f, vpPos.y + tbH + 2.0f);
  ImVec2 contentVpSize(vpSize.x - 8.0f, vpSize.y - tbH - 6.0f);

  if (g_RightTab == 0) {
    DrawIsometricGridPlaceholder(draw, contentVpPos, contentVpSize, sel,
                                 fontBold, fontReg, alpha);

    UiDraw::DrawTextLeftEllipsis(
        draw, fontReg, FsReg(15.0f), vpPos.x + 18.0f * s,
        vpPos.y + vpH - 28.0f * s, 360.0f * s,
        "Drag to orbit preview  |  Scroll to zoom",
        IM_COL32(158, 172, 204, static_cast<int>(235 * alpha)),
        IM_COL32(8, 10, 15, static_cast<int>(220 * alpha)), 1.0f);

    if (UiDraw::DrawActionButton(
            draw, "##ResetOrbitBtn", "RESET",
            ImVec2(vpPos.x + vpSize.x - 160.0f * s, vpPos.y + vpH - 50.0f * s),
            ImVec2(144.0f * s, std::round(38.0f * s)),
            UiAssets::PlateId::SlateCard, fontDisplay, FsDisp(14.0f),
            IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255), true,
            alpha)) {
      g_PreviewYaw = 0.78f;
      g_PreviewPitch = 0.55f;
      g_PreviewZoom = 1.0f;
      ToastManager::Show(ToastType::Info, "VIEWPORT",
                         "Camera orbit reset to default isometric angle.");
    }
  } else {
    DrawMaterialsPreviewView(draw, contentVpPos, contentVpSize, sel, fontBold,
                             fontReg, alpha);
  }

  if (g_DeleteConfirm && (ImGui::GetTime() - g_DeleteConfirmTime) > 3.5) {
    g_DeleteConfirm = false;
  }

  float delW = std::round(rightW * 0.21f);
  float renW = std::round(rightW * 0.21f);
  float expW = std::round(rightW * 0.21f);
  float loadW = rightW - delW - renW - expW - 30.0f * s;

  if (UiDraw::DrawActionButton(
          draw, "##DeleteStructBtn", g_DeleteConfirm ? "CONFIRM?" : "DELETE",
          ImVec2(rightX, ctaY), ImVec2(delW, botBarH),
          UiAssets::PlateId::RedDanger, fontDisplay, FsDisp(14.5f),
          IM_COL32(255, 255, 255, 255), IM_COL32(107, 18, 22, 255),
          sel != nullptr, alpha)) {
    if (!g_DeleteConfirm) {
      g_DeleteConfirm = true;
      g_DeleteConfirmTime = ImGui::GetTime();
      ToastManager::Show(
          ToastType::Warning, "CONFIRM DELETE",
          "Click DELETE again within 3s to remove from library.");
    } else if (sel) {
      std::string removedName = sel->name;
      if (sel->isDiskFile && !sel->filePath.empty()) {
        std::error_code ec;
        std::filesystem::remove(sel->filePath, ec);
      }
      g_Entries.erase(g_Entries.begin() + g_SelectedIndex);
      g_SelectedIndex =
          std::clamp(g_SelectedIndex, 0,
                     std::max(0, static_cast<int>(g_Entries.size()) - 1));
      g_DeleteConfirm = false;
      ToastManager::Show(ToastType::Error, "REMOVED",
                         "Removed '" + removedName + "' from library.");
    }
  }

  if (UiDraw::DrawActionButton(
          draw, "##RenameStructBtn", "RENAME",
          ImVec2(rightX + delW + 10.0f * s, ctaY), ImVec2(renW, botBarH),
          UiAssets::PlateId::SlateCard, fontDisplay, FsDisp(14.5f),
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255),
          sel != nullptr, alpha)) {
    if (sel) {
      snprintf(g_RenameNameBuf, sizeof(g_RenameNameBuf), "%s",
               sel->name.c_str());
      g_ShowRenameModal = true;
    }
  }

  if (UiDraw::DrawActionButton(
          draw, "##ExportStructBtn", "EXPORT",
          ImVec2(rightX + delW + renW + 20.0f * s, ctaY), ImVec2(expW, botBarH),
          UiAssets::PlateId::BlueBanner, fontDisplay, FsDisp(14.5f),
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 54, 102, 255),
          sel != nullptr, alpha)) {
    if (sel && sel->isDiskFile && !sel->filePath.empty()) {
      Bedrockmetica::SchematicDocument doc;
      bool loaded =
          (sel->format == ".bmet")
              ? Bedrockmetica::BmetCodec::LoadFromFile(sel->filePath, doc)
              : Bedrockmetica::McStructureCodec::LoadFile(sel->filePath, doc);
      if (loaded) {
        std::filesystem::path outPath =
            std::filesystem::path(GetSchematicsDir()) /
            (std::filesystem::path(sel->filePath).stem().string() +
             ".mcstructure");
        if (Bedrockmetica::McStructureCodec::SaveFile(outPath.string(), doc)) {
          UpsertDiskEntryFromPath(outPath.string());
          if (doc.width > 64 || doc.height > 384 || doc.depth > 64) {
            ToastManager::Show(
                ToastType::Info, "EXPORTED (.MCSTRUCTURE)",
                "Saved '" + outPath.filename().string() +
                    "' (exceeds Vanilla 64x384x64 Structure Block cap).");
          } else {
            ToastManager::Show(ToastType::Success, "EXPORTED",
                               "Saved '" + outPath.filename().string() +
                                   "' to schematics folder.");
          }
        } else {
          ToastManager::Show(ToastType::Error, "EXPORT FAILED",
                             "Could not write .mcstructure file.");
        }
      } else {
        ToastManager::Show(ToastType::Error, "EXPORT FAILED",
                           "Could not read source schematic data.");
      }
    } else if (sel) {
      ToastManager::Show(ToastType::Info, "EXPORT",
                         "Starter demo presets cannot be exported. Capture or import a build first.");
    }
  }

  ImVec2 heroPos(rightX + delW + renW + expW + 30.0f * s, ctaY);
  ImVec2 heroSize(loadW, botBarH);
  ImU32 heroShadow = IM_COL32(12, 62, 26, 255);

  if (UiDraw::DrawActionButton(
          draw, "##LoadAtFeetCTA", "LOAD", heroPos, heroSize,
          UiAssets::PlateId::EmeraldCTA, fontDisplay, FsDisp(20.0f),
          IM_COL32(255, 255, 255, 255), heroShadow, sel != nullptr, alpha,
          UiAssets::BadgeId::Cube, true)) {
    if (sel) {
      g_LoadedIndex = g_SelectedIndex;
      g_SliceLayer = sel->h;
      g_SliceMode = 0;
      g_RotationDeg = 0;
      g_MirrorMode = 0;
      g_OriginLocked = false;
      g_ShowSettings = false;
      g_ShowUnloadConfirm = false;
      std::string kn = SwapchainHook::GetMenuToggleKeyName();
      SoundManager::Play(UISound::Success);
      ToastManager::Show(ToastType::Success, "LOADED",
                         "Loaded '" + sel->name + "' at your feet. Press [" +
                             kn + "] anytime for hologram controls & items.",
                         "[" + kn + "]");
      SwapchainHook::SetMenuOpen(false);
    }
  }

  if (modalActive)
    ImGui::EndDisabled();

  ImGui::End();
}

void RenderScreen2ActiveSidebar(float screenWidth, float screenHeight,
                                float easeOut, float alpha, ImFont *fontDisplay,
                                ImFont *fontBold, ImFont *fontReg) {
  StructureEntry &active = g_Entries[g_LoadedIndex];

  float fitScale =
      std::min((screenHeight - 28.0f) / 916.0f, (screenWidth * 0.36f) / 476.0f);
  float baseScale = std::clamp(fitScale * g_UiScale, 0.85f, 1.08f);
  float s = baseScale;

  const float sideW = std::min(screenWidth * 0.38f, std::round(476.0f * s));
  const float sideH = std::min(screenHeight - 28.0f, std::round(920.0f * s));
  const float slideX = (1.0f - easeOut) * (sideW + 24.0f);
  const ImVec2 sidePos(std::round(screenWidth - sideW - 16.0f + slideX),
                       std::round((screenHeight - sideH) * 0.5f));
  const ImVec2 sideSize(sideW, sideH);

  auto FsDisp = [&](float base) {
    return std::max(16.0f, std::round((base + 2.0f) * s));
  };
  auto FsBold = [&](float base) {
    return std::max(16.0f, std::round((base + 1.5f) * s));
  };
  auto FsReg = [&](float base) {
    return std::max(15.0f, std::round((base + 1.0f) * s));
  };

  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!SwapchainHook::IsMenuOpen())
    hostFlags |= ImGuiWindowFlags_NoInputs;

  ImGui::SetNextWindowPos(sidePos);
  ImGui::SetNextWindowSize(sideSize);
  ImGui::Begin("##ActiveSidebarHost", nullptr, hostFlags);

  const bool modalActive = (g_ShowSettings || g_ShowUnloadConfirm);
  if (modalActive)
    ImGui::BeginDisabled(true);

  ImDrawList *draw = ImGui::GetWindowDrawList();

  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::ObsidianWindow, sidePos,
                          sideSize, alpha, true);
  UiDraw::DrawSubtleGrid(draw, sidePos, sideSize, 26.0f * s, alpha);

  const float pad = std::round(14.0f * s);
  ImVec2 hdrPos(sidePos.x + pad, sidePos.y + pad);
  ImVec2 hdrSize(sideW - pad * 2.0f, std::round(58.0f * s));
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::GoldHeader, hdrPos, hdrSize,
                          alpha, true);

  UiDraw::DrawDiamondBadge(
      draw, UiAssets::BadgeId::Cube,
      ImVec2(hdrPos.x + 28.0f * s, hdrPos.y + hdrSize.y * 0.5f), 36.0f * s,
      alpha);
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(16.0f), hdrPos.x + 54.0f * s,
      hdrPos.y + hdrSize.y * 0.5f, hdrSize.x - 110.0f * s, active.name.c_str(),
      IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)),
      IM_COL32(104, 58, 4, static_cast<int>(255 * alpha)), 2.0f);

  if (UiDraw::DrawActionButton(
          draw, "##CollapseSidebarBtn", ">",
          ImVec2(hdrPos.x + hdrSize.x - 46.0f * s, hdrPos.y + 8.0f * s),
          ImVec2(38.0f * s, hdrSize.y - 16.0f * s),
          UiAssets::PlateId::SlateCard, fontDisplay, FsDisp(16.0f),
          IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255), true,
          alpha)) {
    SwapchainHook::SetMenuOpen(false);
  }

  const float innerX = sidePos.x + pad;
  const float innerW = sideW - pad * 2.0f;
  const float posY0 = hdrPos.y + hdrSize.y + 12.0f * s;
  const float posH = std::round(186.0f * s);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel,
                          ImVec2(innerX, posY0), ImVec2(innerW, posH), alpha,
                          true);

  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(14.0f), innerX + 14.0f * s, posY0 + 23.0f * s,
      120.0f * s, "POSITION",
      IM_COL32(252, 198, 56, static_cast<int>(255 * alpha)));

  char xyzBuf[48];
  snprintf(xyzBuf, sizeof(xyzBuf), "%d, %d, %d", g_OriginXYZ[0], g_OriginXYZ[1],
           g_OriginXYZ[2]);
  UiDraw::DrawStatusPill(draw, ImVec2(innerX + 126.0f * s, posY0 + 9.0f * s),
                         ImVec2(innerW - 234.0f * s, 28.0f * s),
                         UiAssets::PlateId::DarkInset, fontReg, FsReg(14.0f),
                         xyzBuf, IM_COL32(142, 232, 255, 255),
                         IM_COL32(10, 12, 18, 255), alpha);

  if (UiDraw::DrawActionButton(
          draw, "##LockOriginBtn", g_OriginLocked ? "Locked" : "Unlocked",
          ImVec2(innerX + innerW - 102.0f * s, posY0 + 9.0f * s),
          ImVec2(90.0f * s, 28.0f * s),
          g_OriginLocked ? UiAssets::PlateId::EmeraldCTA
                         : UiAssets::PlateId::SlateCard,
          fontBold, FsBold(13.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(14, 64, 28, 255), true, alpha)) {
    g_OriginLocked = !g_OriginLocked;
    ToastManager::Show(ToastType::Info, "ORIGIN LOCK",
                       g_OriginLocked
                           ? "Origin locked against accidental nudges."
                           : "Origin unlocked.");
  }

  static const int kStepVals[3] = {1, 5, 16};
  int stepAmt = kStepVals[g_NudgeStepIdx];

  float grpW = (innerW - 24.0f * s - 16.0f * s) / 3.0f;
  float halfN = (grpW - 4.0f * s) * 0.5f;
  float ny0 = posY0 + 46.0f * s;
  float nH = std::round(40.0f * s);

  struct AxisNudge {
    const char *minusLbl;
    const char *plusLbl;
    int axisIdx;
  };
  const AxisNudge axes[3] = {{"X-", "X+", 0}, {"Z-", "Z+", 2}, {"Y-", "Y+", 1}};

  for (int a = 0; a < 3; ++a) {
    float gx = innerX + 12.0f * s + a * (grpW + 8.0f * s);
    char bId1[32], bId2[32], l1[16], l2[16];
    snprintf(bId1, sizeof(bId1), "##NudgeM_%d", a);
    snprintf(bId2, sizeof(bId2), "##NudgeP_%d", a);
    snprintf(l1, sizeof(l1), "%s%d", axes[a].minusLbl, stepAmt);
    snprintf(l2, sizeof(l2), "%s%d", axes[a].plusLbl, stepAmt);

    if (UiDraw::DrawActionButton(
            draw, bId1, l1, ImVec2(gx, ny0), ImVec2(halfN, nH),
            UiAssets::PlateId::SlateCard, fontBold, FsBold(13.0f),
            IM_COL32(255, 255, 255, 255), IM_COL32(10, 12, 18, 255),
            !g_OriginLocked, alpha)) {
      g_OriginXYZ[axes[a].axisIdx] -= stepAmt;
    }
    if (UiDraw::DrawActionButton(
            draw, bId2, l2, ImVec2(gx + halfN + 4.0f * s, ny0),
            ImVec2(halfN, nH), UiAssets::PlateId::SlateCard, fontBold,
            FsBold(13.0f), IM_COL32(255, 255, 255, 255),
            IM_COL32(10, 12, 18, 255), !g_OriginLocked, alpha)) {
      g_OriginXYZ[axes[a].axisIdx] += stepAmt;
    }
  }

  float trY = ny0 + nH + 10.0f * s;
  float trH = std::round(40.0f * s);
  float trW = (innerW - 24.0f * s - 12.0f * s) / 3.0f;

  char rotLbl[32];
  snprintf(rotLbl, sizeof(rotLbl), "ROT: %d", g_RotationDeg);
  if (UiDraw::DrawActionButton(
          draw, "##SideRotBtn", rotLbl, ImVec2(innerX + 12.0f * s, trY),
          ImVec2(trW, trH), UiAssets::PlateId::BlueBanner, fontBold,
          FsBold(14.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 54, 102, 255), true, alpha)) {
    g_RotationDeg = (g_RotationDeg + 90) % 360;
  }

  const char *mirShort[] = {"MIR: OFF", "MIR: X", "MIR: Z"};
  if (UiDraw::DrawActionButton(
          draw, "##SideMirBtn", mirShort[g_MirrorMode],
          ImVec2(innerX + 12.0f * s + trW + 6.0f * s, trY), ImVec2(trW, trH),
          g_MirrorMode == 0 ? UiAssets::PlateId::SlateCard
                            : UiAssets::PlateId::EmeraldCTA,
          fontBold, FsBold(14.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 12, 18, 255), true, alpha)) {
    g_MirrorMode = (g_MirrorMode + 1) % 3;
  }

  if (UiDraw::DrawActionButton(
          draw, "##SideFeetBtn", "TO FEET",
          ImVec2(innerX + 12.0f * s + (trW + 6.0f * s) * 2.0f, trY),
          ImVec2(trW, trH), UiAssets::PlateId::GoldPill, fontBold,
          FsBold(14.0f), IM_COL32(255, 255, 255, 255), IM_COL32(88, 48, 4, 255),
          !g_OriginLocked, alpha)) {
    ToastManager::Show(ToastType::Info, "ANCHOR",
                       "Snapped hologram anchor back to player position.");
  }

  float stY = trY + trH + 10.0f * s;
  UiDraw::DrawTextLeftEllipsis(
      draw, fontReg, FsReg(14.0f), innerX + 14.0f * s, stY + 14.0f * s,
      100.0f * s,
      "Nudge Step:", IM_COL32(170, 182, 212, static_cast<int>(255 * alpha)),
      IM_COL32(0, 0, 0, 0), 0.0f);
  const char *stepLabels[] = {"1 Blk", "5 Blk", "16 Blk"};
  UiDraw::DrawSegmentedToggle(
      draw, "##StepToggle", ImVec2(innerX + 120.0f * s, stY),
      ImVec2(innerW - 132.0f * s, 28.0f * s), stepLabels, 3, g_NudgeStepIdx,
      fontBold, FsBold(13.0f), alpha);

  const float ly0 = posY0 + posH + 12.0f * s;
  const float lyH = std::round(154.0f * s);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel,
                          ImVec2(innerX, ly0), ImVec2(innerW, lyH), alpha,
                          true);

  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(14.0f), innerX + 14.0f * s, ly0 + 22.0f * s,
      220.0f * s, "LAYER SLICE",
      IM_COL32(252, 198, 56, static_cast<int>(255 * alpha)));

  const char *sliceLabels[] = {"ALL", "SINGLE", "UP TO"};
  UiDraw::DrawSegmentedToggle(
      draw, "##SideSliceMode", ImVec2(innerX + 12.0f * s, ly0 + 37.0f * s),
      ImVec2(innerW - 24.0f * s, std::round(38.0f * s)), sliceLabels, 3,
      g_SliceMode, fontDisplay, FsDisp(13.0f), alpha);

  int maxL = std::max(1, active.h);
  float stepRowY = ly0 + 80.0f * s;
  float stepBtnW = std::round(44.0f * s);
  float stepRowH = std::round(30.0f * s);

  if (UiDraw::DrawActionButton(
          draw, "##LayerDown", "-", ImVec2(innerX + 12.0f * s, stepRowY),
          ImVec2(stepBtnW, stepRowH), UiAssets::PlateId::SlateCard, fontDisplay,
          FsDisp(16.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 12, 18, 255), true, alpha)) {
    g_SliceLayer = std::max(1, g_SliceLayer - 1);
    if (g_SliceMode == 0)
      g_SliceMode = 2;
  }

  char layerMidBuf[64];
  snprintf(layerMidBuf, sizeof(layerMidBuf), "Layer %d / %d  (Y %d)",
           g_SliceLayer, maxL, g_OriginXYZ[1] + g_SliceLayer);
  UiDraw::DrawStatusPill(draw, ImVec2(innerX + 16.0f * s + stepBtnW, stepRowY),
                         ImVec2(innerW - 32.0f * s - stepBtnW * 2.0f, stepRowH),
                         UiAssets::PlateId::GoldPill, fontBold, FsBold(14.0f),
                         layerMidBuf, IM_COL32(255, 255, 255, 255),
                         IM_COL32(88, 48, 4, 255), alpha);

  if (UiDraw::DrawActionButton(
          draw, "##LayerUp", "+",
          ImVec2(innerX + innerW - 12.0f * s - stepBtnW, stepRowY),
          ImVec2(stepBtnW, stepRowH), UiAssets::PlateId::SlateCard, fontDisplay,
          FsDisp(16.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 12, 18, 255), true, alpha)) {
    g_SliceLayer = std::min(maxL, g_SliceLayer + 1);
    if (g_SliceMode == 0)
      g_SliceMode = 2;
  }

  if (UiDraw::DrawSteppedSlider(
          draw, "##SideLayerSlider",
          ImVec2(innerX + 14.0f * s, ly0 + lyH - 34.0f * s),
          ImVec2(innerW - 28.0f * s, 22.0f * s), g_SliceLayer, 1, maxL,
          alpha)) {
    if (g_SliceMode == 0)
      g_SliceMode = 2;
  }

  const float ftH = std::round(48.0f * s);
  const float ftY = sidePos.y + sideH - pad - ftH;
  const float ftHalf = (innerW - 8.0f * s) * 0.5f;

  if (UiDraw::DrawActionButton(draw, "##SideSettingsBtn", "SETTINGS",
                               ImVec2(innerX, ftY), ImVec2(ftHalf, ftH),
                               UiAssets::PlateId::SlateCard, fontDisplay,
                               FsDisp(14.0f), IM_COL32(255, 255, 255, 255),
                               IM_COL32(10, 12, 18, 255), true, alpha)) {
    g_ShowSettings = !g_ShowSettings;
  }
  if (UiDraw::DrawActionButton(draw, "##SideUnloadBtn", "UNLOAD",
                               ImVec2(innerX + ftHalf + 8.0f * s, ftY),
                               ImVec2(ftHalf, ftH),
                               UiAssets::PlateId::RedDanger, fontDisplay,
                               FsDisp(14.0f), IM_COL32(255, 255, 255, 255),
                               IM_COL32(107, 18, 22, 255), true, alpha)) {
    g_ShowUnloadConfirm = true;
  }

  const float mlY0 = ly0 + lyH + 12.0f * s;
  const float mlH = std::max(140.0f * s, ftY - 12.0f * s - mlY0);
  UiDraw::DrawPlate9Slice(draw, UiAssets::PlateId::SlatePanel,
                          ImVec2(innerX, mlY0), ImVec2(innerW, mlH), alpha,
                          true);

  std::vector<int> matIndices;
  int sumHave = 0, sumNeed = 0;
  for (int i = 0; i < static_cast<int>(active.materials.size()); ++i) {
    const auto &m = active.materials[i];
    sumHave += std::min(m.placed, m.required);
    sumNeed += m.required;
    if (!g_MissingOnlyFilter || m.placed < m.required) {
      matIndices.push_back(i);
    }
  }
  int donePct = (sumNeed > 0) ? (sumHave * 100 / sumNeed) : 0;

  char itemsHdr[32];
  snprintf(itemsHdr, sizeof(itemsHdr), "ITEMS (%d)",
           static_cast<int>(active.materials.size()));
  UiDraw::DrawTextLeftEllipsis(
      draw, fontDisplay, FsDisp(15.0f), innerX + 14.0f * s, mlY0 + 24.0f * s,
      136.0f * s, itemsHdr,
      IM_COL32(252, 198, 56, static_cast<int>(255 * alpha)));

  float pillH = std::max(28.0f, std::round(30.0f * s));
  float filterW = std::max(114.0f, std::round(126.0f * s));
  float doneW = std::max(94.0f, std::round(106.0f * s));

  char pctBuf[32];
  snprintf(pctBuf, sizeof(pctBuf), "%d%% Done", donePct);
  UiDraw::DrawStatusPill(
      draw,
      ImVec2(innerX + innerW - filterW - doneW - 20.0f * s, mlY0 + 9.0f * s),
      ImVec2(doneW, pillH), UiAssets::PlateId::EmeraldCTA, fontBold,
      FsBold(14.0f), pctBuf, IM_COL32(255, 255, 255, 255),
      IM_COL32(8, 36, 16, 255), alpha);

  if (UiDraw::DrawActionButton(
          draw, "##MissingFilterBtn",
          g_MissingOnlyFilter ? "All Items" : "Missing Only",
          ImVec2(innerX + innerW - filterW - 12.0f * s, mlY0 + 9.0f * s),
          ImVec2(filterW, pillH),
          g_MissingOnlyFilter ? UiAssets::PlateId::GoldPill
                              : UiAssets::PlateId::BlueBanner,
          fontBold, FsBold(14.0f), IM_COL32(255, 255, 255, 255),
          IM_COL32(10, 44, 86, 255), true, alpha)) {
    g_MissingOnlyFilter = !g_MissingOnlyFilter;
  }

  ImVec2 mlListPos(innerX + 10.0f * s, mlY0 + std::max(44.0f, 46.0f * s));
  ImVec2 mlListSize(innerW - 18.0f * s,
                    std::max(64.0f, (mlY0 + mlH - 10.0f * s) - mlListPos.y));
  const float rowH = std::max(48.0f, std::round(50.0f * s));
  const float rowGap = std::round(7.0f * s);
  float totalMatH = static_cast<float>(matIndices.size()) * (rowH + rowGap);
  bool matScroll = totalMatH > mlListSize.y;

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(14, 17, 25, 160));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(245, 184, 46, 220));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                        IM_COL32(255, 214, 72, 255));
  ImGui::SetCursorScreenPos(mlListPos);
  ImGui::BeginChild("##MaterialsChecklistScroll", mlListSize, false,
                    ImGuiWindowFlags_NoBackground);
  ImDrawList *mDraw = ImGui::GetWindowDrawList();

  float scrollY = ImGui::GetScrollY();
  float rowW = mlListSize.x - (matScroll ? 12.0f : 0.0f);
  float ry = mlListPos.y - scrollY;

  for (size_t mi = 0; mi < matIndices.size(); ++mi) {
    const auto &m = active.materials[matIndices[mi]];
    ImVec2 rPos(mlListPos.x, ry);
    ImVec2 rSize(rowW, rowH);

    bool isDone = (m.placed >= m.required && m.required > 0);
    bool isMiss = (m.placed == 0 && m.required > 0);
    UiAssets::PlateId rowPlate = isDone
                                     ? UiAssets::PlateId::ToastSuccess
                                     : (isMiss ? UiAssets::PlateId::ToastError
                                               : UiAssets::PlateId::SlateCard);

    UiDraw::DrawPlate9Slice(mDraw, rowPlate, rPos, rSize, alpha, false);

    float swSz = std::max(30.0f, std::round(32.0f * s));
    ImVec2 swMin(rPos.x + 8.0f * s, rPos.y + (rowH - swSz) * 0.5f - 2.0f);
    ImVec2 swMax(swMin.x + swSz, swMin.y + swSz);
    DrawBlockIconBox(mDraw, swMin, swMax, m, alpha);

    float cntW = std::max(104.0f, std::round(112.0f * s));
    float cntH = std::max(26.0f, rowH - 18.0f * s);
    ImVec2 cntPos(rPos.x + rowW - cntW - 8.0f * s,
                  rPos.y + (rowH - cntH) * 0.5f - 2.0f);
    char cntTxt[32];
    snprintf(cntTxt, sizeof(cntTxt), "%d / %d", m.placed, m.required);
    ImU32 cntCol = isDone ? IM_COL32(128, 255, 168, 255)
                          : (isMiss ? IM_COL32(255, 146, 146, 255)
                                    : IM_COL32(236, 244, 255, 255));
    UiDraw::DrawStatusPill(
        mDraw, cntPos, ImVec2(cntW, cntH), UiAssets::PlateId::DarkInset,
        fontBold, FsBold(14.0f), cntTxt, cntCol, IM_COL32(0, 0, 0, 0), alpha);

    float nameX = swMax.x + 10.0f * s;
    float nameMaxW = (cntPos.x - 6.0f * s) - nameX;
    UiDraw::DrawTextLeftEllipsis(
        mDraw, fontBold, FsBold(16.0f), nameX, rPos.y + rowH * 0.5f - 2.0f,
        nameMaxW, m.name.c_str(),
        isDone ? IM_COL32(196, 255, 212, static_cast<int>(255 * alpha))
               : IM_COL32(244, 248, 255, static_cast<int>(255 * alpha)));

    float frac = (m.required > 0)
                     ? std::clamp(static_cast<float>(m.placed) /
                                      static_cast<float>(m.required),
                                  0.0f, 1.0f)
                     : 0.0f;
    float barX0 = rPos.x + 8.0f * s;
    float barX1 = rPos.x + rowW - 8.0f * s;
    float barY0 = rPos.y + rowH - 6.0f;
    mDraw->AddRectFilled(ImVec2(barX0, barY0), ImVec2(barX1, barY0 + 3.0f),
                         IM_COL32(12, 15, 22, static_cast<int>(220 * alpha)));
    if (frac > 0.0f) {
      ImU32 barCol =
          isDone ? IM_COL32(66, 220, 112, static_cast<int>(255 * alpha))
                 : IM_COL32(48, 198, 248, static_cast<int>(255 * alpha));
      mDraw->AddRectFilled(ImVec2(barX0, barY0),
                           ImVec2(barX0 + (barX1 - barX0) * frac, barY0 + 3.0f),
                           barCol);
    }

    ry += rowH + rowGap;
  }

  if (matIndices.empty()) {
    UiDraw::DrawTextCentered(
        mDraw, fontBold, FsBold(15.0f), mlListPos,
        ImVec2(mlListPos.x + mlListSize.x, mlListPos.y + 80.0f * s),
        "All materials complete!",
        IM_COL32(128, 255, 168, static_cast<int>(255 * alpha)));
  } else {
    ImGui::Dummy(ImVec2(1.0f, std::max(0.0f, totalMatH - rowGap)));
  }
  ImGui::EndChild();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);

  if (modalActive)
    ImGui::EndDisabled();

  ImGui::End();
}

void RenderModalHost(float screenWidth, float screenHeight, float alpha,
                     ImFont *fontDisplay, ImFont *fontBold, ImFont *fontReg) {
  bool showSettings = (g_SettingsAnim > 0.02f);
  bool showUnload = (g_UnloadModalAnim > 0.02f);
  bool showSave = (g_SaveModalAnim > 0.02f);
  bool showRename = (g_RenameModalAnim > 0.02f);
  bool showImport = (g_ImportModalAnim > 0.02f);
  if (!showSettings && !showUnload && !showSave && !showRename && !showImport)
    return;

  float dimAnim =
      showUnload
          ? g_UnloadModalAnim
          : (showSave
                 ? g_SaveModalAnim
                 : (showRename
                        ? g_RenameModalAnim
                        : (showImport ? g_ImportModalAnim : g_SettingsAnim)));
  float ma = alpha * dimAnim;

  ImGuiWindowFlags bgFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!SwapchainHook::IsMenuOpen())
    bgFlags |= ImGuiWindowFlags_NoInputs;

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));
  ImGui::Begin("##ModalBackdropHost", nullptr, bgFlags);
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(0.0f, 0.0f), ImVec2(screenWidth, screenHeight),
      IM_COL32(4, 6, 10, static_cast<int>(170 * ma)));
  ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
  if (ImGui::InvisibleButton("##ModalBackdropBlocker",
                             ImVec2(screenWidth, screenHeight))) {
    SwapchainHook::CancelKeybindCapture();
    g_ShowSettings = false;
    g_ShowUnloadConfirm = false;
    g_ShowSaveModal = false;
    g_ShowRenameModal = false;
    g_ShowImportModal = false;
  }
  ImGui::End();

  ImVec2 baseSize = showUnload
                        ? ImVec2(580.0f, 276.0f)
                        : (showSave
                               ? ImVec2(560.0f, 260.0f)
                               : (showRename
                                      ? ImVec2(540.0f, 240.0f)
                                      : (showImport
                                             ? ImVec2(680.0f, 520.0f)
                                             : ImVec2(650.0f, 568.0f))));
  float scaleIn = 0.94f + 0.06f * dimAnim;
  ImVec2 curSize(std::round(baseSize.x * scaleIn),
                 std::round(baseSize.y * scaleIn));
  ImVec2 mPos(std::round((screenWidth - curSize.x) * 0.5f),
              std::round((screenHeight - curSize.y) * 0.5f));

  ImGuiWindowFlags dlgFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
  if (!SwapchainHook::IsMenuOpen())
    dlgFlags |= ImGuiWindowFlags_NoInputs;
  else
    ImGui::SetNextWindowFocus();

  ImGui::SetNextWindowPos(mPos);
  ImGui::SetNextWindowSize(curSize);
  ImGui::Begin("##ModalDialogHost", nullptr, dlgFlags);

  ImDrawList *draw = ImGui::GetWindowDrawList();
  const StructureEntry *active =
      (g_LoadedIndex >= 0 && g_LoadedIndex < static_cast<int>(g_Entries.size()))
          ? &g_Entries[g_LoadedIndex]
          : nullptr;

  if (showUnload) {
    DrawUnloadConfirmModal(draw, mPos, curSize, active, fontDisplay, fontBold,
                           fontReg, alpha * g_UnloadModalAnim);
  } else if (showSave) {
    DrawSaveCaptureModal(draw, mPos, curSize, fontDisplay, fontBold, fontReg,
                         alpha * g_SaveModalAnim);
  } else if (showRename) {
    DrawRenameModal(draw, mPos, curSize, fontDisplay, fontBold, fontReg,
                    alpha * g_RenameModalAnim);
  } else if (showImport) {
    DrawImportModal(draw, mPos, curSize, fontDisplay, fontBold, fontReg,
                    alpha * g_ImportModalAnim);
  } else if (showSettings) {
    DrawSettingsModal(draw, mPos, curSize, fontDisplay, fontBold, fontReg,
                      alpha * g_SettingsAnim);
  }

  ImGui::End();
}

void DrawShieldCrestWaypoint(
    ImDrawList *draw, ImFont *fontDisplay, float screenW, float screenH,
    const Bedrockmetica::CaptureManager::CornerMarkerInfo &m, int64_t nowMs) {
  if (!draw || !fontDisplay || !m.valid)
    return;

  float sx = 0.0f, sy = 0.0f, dist = 0.0f;
  if (!BedrockGame::WorldToScreen(m.wx, m.wy, m.wz, screenW, screenH, sx, sy,
                                  dist))
    return;

  float elapsed = static_cast<float>(std::max<int64_t>(0, nowMs - m.setMs));
  float t = std::clamp(elapsed / 280.0f, 0.0f, 1.0f);
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  float tm1 = t - 1.0f;
  float easeBack = 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
  float animScale = std::clamp(0.22f + 0.78f * easeBack, 0.22f, 1.12f);
  float fadeAlpha = std::clamp(elapsed / 140.0f, 0.0f, 1.0f);
  int a = static_cast<int>(255.0f * fadeAlpha);
  if (a <= 2)
    return;

  float distScale = std::clamp(1.12f - dist * 0.008f, 0.75f, 1.12f);
  float s = distScale * animScale * g_UiScale;

  float w = std::round(64.0f * s);
  float h = std::round(72.0f * s);
  float ch = std::round(7.0f * s);
  float bw = std::max(2.0f, std::round(3.0f * s));

  float x0 = std::round(sx - w * 0.5f);
  float x1 = x0 + w;
  float y1 = std::round(sy - 4.0f);
  float y0 = y1 - h;
  float ySideBot = std::round(y0 + h * 0.72f);

  bool isPos1 = (m.num == 1);
  ImU32 borderCol = isPos1 ? IM_COL32(11, 51, 98, a) : IM_COL32(108, 63, 4, a);
  ImU32 topCol = isPos1 ? IM_COL32(62, 166, 249, a) : IM_COL32(252, 194, 52, a);
  ImU32 botCol = isPos1 ? IM_COL32(29, 132, 224, a) : IM_COL32(234, 156, 22, a);
  ImU32 hiCol = isPos1 ? IM_COL32(160, 220, 255, a) : IM_COL32(255, 236, 155, a);
  ImU32 insetCol = isPos1 ? IM_COL32(17, 82, 152, a) : IM_COL32(166, 102, 12, a);

  ImVec2 ptsOut[7] = {
      ImVec2(x0 + ch, y0),      ImVec2(x1 - ch, y0),
      ImVec2(x1, y0 + ch),      ImVec2(x1, ySideBot),
      ImVec2(sx, y1),           ImVec2(x0, ySideBot),
      ImVec2(x0, y0 + ch),
  };
  draw->AddConvexPolyFilled(ptsOut, 7, borderCol);

  float ix0 = x0 + bw;
  float ix1 = x1 - bw;
  float iy0 = y0 + bw;
  float iy1 = y1 - bw * 1.35f;
  float iySideBot = ySideBot - bw * 0.35f;
  float ich = std::max(2.0f, ch - bw * 0.5f);

  ImVec2 ptsIn[7] = {
      ImVec2(ix0 + ich, iy0),    ImVec2(ix1 - ich, iy0),
      ImVec2(ix1, iy0 + ich),    ImVec2(ix1, iySideBot),
      ImVec2(sx, iy1),           ImVec2(ix0, iySideBot),
      ImVec2(ix0, iy0 + ich),
  };
  draw->AddConvexPolyFilled(ptsIn, 7, botCol);

  float iyMid = std::min(iySideBot, std::round((iy0 + iy1) * 0.52f));
  ImVec2 ptsTop[6] = {
      ImVec2(ix0 + ich, iy0), ImVec2(ix1 - ich, iy0),
      ImVec2(ix1, iy0 + ich), ImVec2(ix1, iyMid),
      ImVec2(ix0, iyMid),     ImVec2(ix0, iy0 + ich),
  };
  draw->AddConvexPolyFilled(ptsTop, 6, topCol);

  draw->AddRectFilled(ImVec2(ix0 + ich, iy0),
                      ImVec2(ix1 - ich, iy0 + std::max(2.0f, 2.5f * s)), hiCol);

  float tagW = std::round(44.0f * s);
  float tagH = std::round(15.0f * s);
  float tagX0 = std::round(sx - tagW * 0.5f);
  float tagY0 = std::round(iy0 + 6.0f * s);
  draw->AddRectFilled(ImVec2(tagX0, tagY0), ImVec2(tagX0 + tagW, tagY0 + tagH),
                      insetCol);
  draw->AddRect(ImVec2(tagX0, tagY0), ImVec2(tagX0 + tagW, tagY0 + tagH),
                borderCol, 0.0f, 0, std::max(1.0f, 1.5f * s));

  const char *tagStr = isPos1 ? "POS 1" : "POS 2";
  float tagFontSz = std::max(9.0f, 10.5f * s);
  ImVec2 tagSz = fontDisplay->CalcTextSizeA(tagFontSz, FLT_MAX, 0.0f, tagStr);
  ImVec2 tagPos(std::round(sx - tagSz.x * 0.5f),
                std::round(tagY0 + (tagH - tagSz.y) * 0.5f));
  draw->AddText(fontDisplay, tagFontSz,
                ImVec2(tagPos.x + 1.0f, tagPos.y + 1.0f), borderCol, tagStr);
  draw->AddText(fontDisplay, tagFontSz, tagPos, IM_COL32(255, 255, 255, a),
                tagStr);

  const char *numStr = isPos1 ? "1" : "2";
  float numFontSz = std::max(18.0f, 29.0f * s);
  ImVec2 numSz = fontDisplay->CalcTextSizeA(numFontSz, FLT_MAX, 0.0f, numStr);
  float numAreaTop = tagY0 + tagH + 2.0f * s;
  float numAreaBot = iySideBot;
  ImVec2 numPos(
      std::round(sx - numSz.x * 0.5f),
      std::round((numAreaTop + numAreaBot) * 0.5f - numSz.y * 0.5f));
  float shOff = std::max(1.5f, 2.2f * s);
  draw->AddText(fontDisplay, numFontSz,
                ImVec2(numPos.x + shOff, numPos.y + shOff), borderCol, numStr);
  draw->AddText(fontDisplay, numFontSz, numPos, IM_COL32(255, 255, 255, a),
                numStr);
}

}

void Initialize() {
  ConfigManager::ConfigData cfg = ConfigManager::Load();
  static const float kPresetScales[3] = {0.90f, 1.00f, 1.12f};
  g_UiSizePreset = cfg.uiSizePreset;
  g_UiScale = kPresetScales[g_UiSizePreset];
  g_TextSizePct = cfg.textSizePct;
  g_GhostOpacityPct = cfg.ghostOpacityPct;
  g_SoundFxToggle = cfg.soundFxEnabled ? 0 : 1;
  g_VerifyMode = cfg.verifyMode;
  SoundManager::SetEnabled(cfg.soundFxEnabled);
  UiDraw::SetTextScale(static_cast<float>(g_TextSizePct) / 100.0f);
  SeedStarterLibraryIfEmpty();
}

bool IsCaptureModeActive() { return g_CaptureMode; }

void SetCaptureModeActive(bool active) {
  g_CaptureMode = active;
  Bedrockmetica::CaptureManager::SetActive(active);
}

void TriggerCaptureClickNotice() {
  ToastManager::Show(
      ToastType::Info, "CAPTURE",
      "Selection point recorded (3D outline plugs in next stage).");
}

bool HasOpenModal() {
  return g_ShowSettings || g_ShowUnloadConfirm || g_ShowSaveModal ||
         g_ShowRenameModal || g_ShowImportModal;
}

void CloseActiveModal() {
  SwapchainHook::CancelKeybindCapture();
  g_ShowSettings = false;
  g_ShowUnloadConfirm = false;
  g_ShowSaveModal = false;
  g_ShowRenameModal = false;
  g_ShowImportModal = false;
}

void Render(float screenWidth, float screenHeight, bool menuOpen) {
  SeedStarterLibraryIfEmpty();

  ImDrawList *bgDraw = ImGui::GetBackgroundDrawList();
  if (!bgDraw)
    return;

  ImFont *fontDisplay = SwapchainHook::GetFontMinecraft();
  ImFont *fontBold = SwapchainHook::GetFontBold();
  ImFont *fontReg = SwapchainHook::GetFontRegular();
  if (!fontReg)
    fontReg = ImGui::GetFont();
  if (!fontBold)
    fontBold = fontReg;
  if (!fontDisplay)
    fontDisplay = fontBold;

  UiDraw::SetDisplayFont(fontDisplay);
  UiDraw::SetTextScale(static_cast<float>(g_TextSizePct) / 100.0f);

  float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.05f);
  float animSpeed = menuOpen ? 24.0f : 26.0f;
  g_OpenAnim += ((menuOpen ? 1.0f : 0.0f) - g_OpenAnim) *
                (1.0f - std::exp(-animSpeed * dt));
  if (!menuOpen && g_OpenAnim < 0.02f)
    g_OpenAnim = 0.0f;
  if (menuOpen && g_OpenAnim > 0.995f)
    g_OpenAnim = 1.0f;

  g_SettingsAnim +=
      (((g_ShowSettings && menuOpen) ? 1.0f : 0.0f) - g_SettingsAnim) *
      (1.0f - std::exp(-24.0f * dt));
  g_UnloadModalAnim +=
      (((g_ShowUnloadConfirm && menuOpen) ? 1.0f : 0.0f) - g_UnloadModalAnim) *
      (1.0f - std::exp(-24.0f * dt));
  g_SaveModalAnim +=
      (((g_ShowSaveModal && menuOpen) ? 1.0f : 0.0f) - g_SaveModalAnim) *
      (1.0f - std::exp(-24.0f * dt));
  g_RenameModalAnim +=
      (((g_ShowRenameModal && menuOpen) ? 1.0f : 0.0f) - g_RenameModalAnim) *
      (1.0f - std::exp(-24.0f * dt));
  g_ImportModalAnim +=
      (((g_ShowImportModal && menuOpen) ? 1.0f : 0.0f) - g_ImportModalAnim) *
      (1.0f - std::exp(-24.0f * dt));

  if (g_OpenAnim <= 0.01f) {
    Bedrockmetica::CaptureManager::CornerMarkerInfo m1{}, m2{};
    Bedrockmetica::CaptureManager::GetCornerMarkers(m1, m2);
    if (m1.valid || m2.valid) {
      int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
      DrawShieldCrestWaypoint(bgDraw, fontDisplay, screenWidth, screenHeight,
                              m1, nowMs);
      DrawShieldCrestWaypoint(bgDraw, fontDisplay, screenWidth, screenHeight,
                              m2, nowMs);
    }
    DrawCollapsedCornerHud(bgDraw, screenWidth, screenHeight, fontDisplay,
                           fontBold, fontReg);
    return;
  }

  float easeOut = 1.0f - std::pow(1.0f - g_OpenAnim, 3.0f);
  float alpha = std::clamp(easeOut * 1.15f, 0.0f, 1.0f);

  if (g_LoadedIndex >= 0 &&
      g_LoadedIndex < static_cast<int>(g_Entries.size())) {
    RenderScreen2ActiveSidebar(screenWidth, screenHeight, easeOut, 1.0f,
                               fontDisplay, fontBold, fontReg);
  } else {
    RenderScreen1Library(screenWidth, screenHeight, easeOut, alpha, fontDisplay,
                         fontBold, fontReg);
  }

  RenderModalHost(screenWidth, screenHeight, alpha, fontDisplay, fontBold,
                  fontReg);
}

} // namespace SchematicMenu
