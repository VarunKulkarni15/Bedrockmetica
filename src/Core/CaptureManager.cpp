#include "CaptureManager.hpp"
#include "../IO/BmetCodec.hpp"
#include "../Utils/Logger.hpp"
#include "../Utils/SoundManager.hpp"
#include "../Utils/ToastManager.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Bedrockmetica::CaptureManager {

namespace {

constexpr int32_t kMaxScratchColumns = 256;
constexpr int32_t kMinWorldY = -64;
constexpr int32_t kMaxWorldY = 319;
constexpr int32_t kWorldHeight = 384;
constexpr int32_t kMaxHorizSpan = 512;
constexpr int64_t kMaxBoxVolume = 8000000LL;

struct CachedColumn {
  int32_t cx = 0;
  int32_t cz = 0;
  std::vector<uint16_t> blocks;
};

static std::mutex g_CapMutex;
static bool g_CaptureActive = false;
static CapturePhase g_Phase = CapturePhase::Idle;
static BedrockGame::BlockPos3i g_Pos1{};
static BedrockGame::BlockPos3i g_Pos2{};
static BedrockGame::BlockPos3i g_MinB{};
static BedrockGame::BlockPos3i g_MaxB{};
static int64_t g_Pos1SetMs = 0;
static int64_t g_Pos2SetMs = 0;

static SchematicDocument g_StagedDoc;
static std::unordered_set<uint64_t> g_CapturedCols;
static std::vector<uint64_t> g_PendingCols;
static int32_t g_TotalCols = 0;

static std::list<uint64_t> g_ScratchLru;
static std::unordered_map<uint64_t, CachedColumn> g_ScratchMap;
static std::unordered_map<const void *, uint16_t> g_BlockPtrCache;
static uint32_t g_TickCounter = 0;

uint16_t ResolveBlockPtrToPid(const void *blk, std::string &bName,
                              std::vector<BlockStateProp> &props) {
  if (!blk)
    return 0;
  auto it = g_BlockPtrCache.find(blk);
  if (it != g_BlockPtrCache.end())
    return it->second;
  if (!BedrockGame::ReadBlockSerialization(blk, bName, props)) {
    g_BlockPtrCache.emplace(blk, 0);
    return 0;
  }
  uint16_t pid = g_StagedDoc.InternBlock(bName, props);
  g_BlockPtrCache.emplace(blk, pid);
  return pid;
}

uint64_t PackCol(int32_t cx, int32_t cz) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
         static_cast<uint64_t>(static_cast<uint32_t>(cz));
}

void UnpackCol(uint64_t key, int32_t &cx, int32_t &cz) {
  cx = static_cast<int32_t>(key >> 32);
  cz = static_cast<int32_t>(key & 0xFFFFFFFFu);
}

void ClearScratch() {
  g_ScratchLru.clear();
  g_ScratchMap.clear();
  g_BlockPtrCache.clear();
}

void SnapshotColumnToScratch(void *bs, int32_t cx, int32_t cz) {
  uint64_t key = PackCol(cx, cz);
  if (g_ScratchMap.find(key) != g_ScratchMap.end())
    return;
  if (!BedrockGame::IsChunkColumnLoaded(bs, cx, cz))
    return;

  if (static_cast<int32_t>(g_ScratchLru.size()) >= kMaxScratchColumns) {
    uint64_t victim = g_ScratchLru.front();
    g_ScratchLru.pop_front();
    g_ScratchMap.erase(victim);
  }

  CachedColumn col;
  col.cx = cx;
  col.cz = cz;
  col.blocks.assign(static_cast<size_t>(16 * 16 * kWorldHeight), 0);

  std::string bName;
  std::vector<BlockStateProp> props;
  int32_t wx0 = cx << 4;
  int32_t wz0 = cz << 4;

  for (int32_t lz = 0; lz < 16; ++lz) {
    for (int32_t lx = 0; lx < 16; ++lx) {
      for (int32_t wy = kMinWorldY; wy <= kMaxWorldY; ++wy) {
        const void *blk = BedrockGame::GetBlockPtr(bs, wx0 + lx, wy, wz0 + lz);
        uint16_t pid = ResolveBlockPtrToPid(blk, bName, props);
        if (pid != 0) {
          size_t idx =
              static_cast<size_t>((wy - kMinWorldY) * 256 + lz * 16 + lx);
          col.blocks[idx] = pid;
        }
      }
    }
  }

  g_ScratchLru.push_back(key);
  g_ScratchMap.emplace(key, std::move(col));
}

void HarvestColumnIntoStaged(void *bs, int32_t cx, int32_t cz) {
  uint64_t key = PackCol(cx, cz);
  if (g_CapturedCols.find(key) != g_CapturedCols.end())
    return;

  int32_t x0 = std::max(g_MinB.x, cx << 4);
  int32_t x1 = std::min(g_MaxB.x, (cx << 4) + 15);
  int32_t z0 = std::max(g_MinB.z, cz << 4);
  int32_t z1 = std::min(g_MaxB.z, (cz << 4) + 15);
  if (x0 > x1 || z0 > z1)
    return;

  if (BedrockGame::IsChunkColumnLoaded(bs, cx, cz)) {
    std::string bName;
    std::vector<BlockStateProp> props;
    for (int32_t wz = z0; wz <= z1; ++wz) {
      for (int32_t wx = x0; wx <= x1; ++wx) {
        for (int32_t wy = g_MinB.y; wy <= g_MaxB.y; ++wy) {
          const void *blk = BedrockGame::GetBlockPtr(bs, wx, wy, wz);
          uint16_t pid = ResolveBlockPtrToPid(blk, bName, props);
          if (pid != 0) {
            g_StagedDoc.SetBlock(0, wx - g_MinB.x, wy - g_MinB.y, wz - g_MinB.z,
                                 pid);
          }
        }
      }
    }
    g_CapturedCols.insert(key);
    return;
  }

  auto scIt = g_ScratchMap.find(key);
  if (scIt != g_ScratchMap.end() && !scIt->second.blocks.empty()) {
    const auto &col = scIt->second;
    for (int32_t wz = z0; wz <= z1; ++wz) {
      int32_t lz = wz & 15;
      for (int32_t wx = x0; wx <= x1; ++wx) {
        int32_t lx = wx & 15;
        for (int32_t wy = g_MinB.y; wy <= g_MaxB.y; ++wy) {
          if (wy < kMinWorldY || wy > kMaxWorldY)
            continue;
          size_t idx =
              static_cast<size_t>((wy - kMinWorldY) * 256 + lz * 16 + lx);
          uint16_t pid = col.blocks[idx];
          if (pid != 0) {
            g_StagedDoc.SetBlock(0, wx - g_MinB.x, wy - g_MinB.y, wz - g_MinB.z,
                                 pid);
          }
        }
      }
    }
    g_CapturedCols.insert(key);
  }
}

void FinalizeBoxFromPos1Pos2(void *bs) {
  g_MinB.x = std::min(g_Pos1.x, g_Pos2.x);
  g_MinB.y = std::clamp(std::min(g_Pos1.y, g_Pos2.y), kMinWorldY, kMaxWorldY);
  g_MinB.z = std::min(g_Pos1.z, g_Pos2.z);
  g_MaxB.x = std::max(g_Pos1.x, g_Pos2.x);
  g_MaxB.y = std::clamp(std::max(g_Pos1.y, g_Pos2.y), kMinWorldY, kMaxWorldY);
  g_MaxB.z = std::max(g_Pos1.z, g_Pos2.z);

  int32_t w = g_MaxB.x - g_MinB.x + 1;
  int32_t h = g_MaxB.y - g_MinB.y + 1;
  int32_t d = g_MaxB.z - g_MinB.z + 1;
  int64_t vol = static_cast<int64_t>(w) * h * d;
  if (w > kMaxHorizSpan || d > kMaxHorizSpan || vol > kMaxBoxVolume) {
    ToastManager::Show(
        ToastType::Warning, "REGION TOO LARGE",
        "Selection exceeds 512x512 or 8M volume safety cap. Pick closer Pos2.");
    g_Phase = CapturePhase::HasPos1NeedsPos2;
    return;
  }

  g_StagedDoc.Reset("New Capture");
  g_StagedDoc.width = w;
  g_StagedDoc.height = h;
  g_StagedDoc.depth = d;
  g_StagedDoc.originX = g_MinB.x;
  g_StagedDoc.originY = g_MinB.y;
  g_StagedDoc.originZ = g_MinB.z;

  g_CapturedCols.clear();
  g_PendingCols.clear();

  int32_t cMinX = g_MinB.x >> 4;
  int32_t cMaxX = g_MaxB.x >> 4;
  int32_t cMinZ = g_MinB.z >> 4;
  int32_t cMaxZ = g_MaxB.z >> 4;
  g_TotalCols = (cMaxX - cMinX + 1) * (cMaxZ - cMinZ + 1);

  for (int32_t cz = cMinZ; cz <= cMaxZ; ++cz) {
    for (int32_t cx = cMinX; cx <= cMaxX; ++cx) {
      HarvestColumnIntoStaged(bs, cx, cz);
      uint64_t key = PackCol(cx, cz);
      if (g_CapturedCols.find(key) == g_CapturedCols.end()) {
        g_PendingCols.push_back(key);
      }
    }
  }

  ClearScratch();
  g_StagedDoc.RecomputeStats();
  g_Phase = CapturePhase::BoxLockedReadyToSave;

  Logger::info("Box locked: " + std::to_string(w) + "x" + std::to_string(h) +
                   "x" + std::to_string(d) +
                   " cols=" + std::to_string(g_CapturedCols.size()) + "/" +
                   std::to_string(g_TotalCols) +
                   " nonAir=" + std::to_string(g_StagedDoc.totalNonAirBlocks),
               "Capture");

  if (g_PendingCols.empty()) {
    ToastManager::Show(ToastType::Success, "BOX CAPTURED (100%)",
                       "Captured " + std::to_string(w) + "x" +
                           std::to_string(h) + "x" + std::to_string(d) + " (" +
                           std::to_string(g_StagedDoc.totalNonAirBlocks) +
                           " blocks). Open menu to SAVE.");
  } else {
    int pct = GetCapturedPercent();
    ToastManager::Show(ToastType::Warning,
                       "PARTIAL CAPTURE (" + std::to_string(pct) + "%)",
                       "Walk near amber chunk outlines to auto-fill unloaded "
                       "columns, or open menu to SAVE.");
  }
}

std::string MakeSafeFileStem(const std::string &name) {
  std::string out;
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_' || c == '-' || c == ' ') {
      out.push_back(c);
    }
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  while (!out.empty() && out.front() == ' ')
    out.erase(out.begin());
  if (out.empty())
    out = "capture";
  return out;
}

}

void SetActive(bool active) {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  g_CaptureActive = active;
  if (active && g_Phase == CapturePhase::Idle) {
    g_Phase = CapturePhase::ArmedNeedsPos1;
  } else if (!active) {
    g_Phase = CapturePhase::Idle;
    g_CapturedCols.clear();
    g_PendingCols.clear();
    g_TotalCols = 0;
    g_StagedDoc.Reset();
    ClearScratch();
  }
}

bool IsActive() {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  return g_CaptureActive;
}

void ResetCapture() {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  g_Phase = g_CaptureActive ? CapturePhase::ArmedNeedsPos1 : CapturePhase::Idle;
  g_CapturedCols.clear();
  g_PendingCols.clear();
  g_TotalCols = 0;
  g_StagedDoc.Reset();
  ClearScratch();
}

CapturePhase GetPhase() {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  return g_Phase;
}

bool HasCapturedBox() {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  return g_Phase == CapturePhase::BoxLockedReadyToSave;
}

int GetCapturedPercent() {
  if (g_TotalCols <= 0)
    return 100;
  return static_cast<int>(
      (static_cast<int64_t>(g_CapturedCols.size()) * 100) / g_TotalCols);
}

int32_t GetBoxWidth() { return g_StagedDoc.width; }
int32_t GetBoxHeight() { return g_StagedDoc.height; }
int32_t GetBoxDepth() { return g_StagedDoc.depth; }
int32_t GetCapturedBlockCount() { return g_StagedDoc.totalNonAirBlocks; }

void GetCornerMarkers(CornerMarkerInfo &outPos1, CornerMarkerInfo &outPos2) {
  outPos1 = {};
  outPos2 = {};
  std::lock_guard<std::mutex> lock(g_CapMutex);
  if (!g_CaptureActive)
    return;
  if (g_Phase == CapturePhase::HasPos1NeedsPos2 ||
      g_Phase == CapturePhase::BoxLockedReadyToSave) {
    outPos1.valid = true;
    outPos1.num = 1;
    outPos1.wx = static_cast<float>(g_Pos1.x) + 0.5f;
    outPos1.wy = static_cast<float>(g_Pos1.y) + 1.15f;
    outPos1.wz = static_cast<float>(g_Pos1.z) + 0.5f;
    outPos1.setMs = g_Pos1SetMs;
  }
  if (g_Phase == CapturePhase::BoxLockedReadyToSave) {
    outPos2.valid = true;
    outPos2.num = 2;
    outPos2.wx = static_cast<float>(g_Pos2.x) + 0.5f;
    outPos2.wy = static_cast<float>(g_Pos2.y) + 1.15f;
    outPos2.wz = static_cast<float>(g_Pos2.z) + 0.5f;
    outPos2.setMs = g_Pos2SetMs;
  }
}

void GetSelectionBoxes(std::vector<SelectionBoxVisual> &outBoxes) {
  outBoxes.clear();
  std::lock_guard<std::mutex> lock(g_CapMutex);
  if (!g_CaptureActive)
    return;

  if (g_Phase == CapturePhase::ArmedNeedsPos1) {
    BedrockGame::HitInfo hit;
    if (BedrockGame::GetAimedHit(hit) && hit.isBlockHit()) {
      float x = static_cast<float>(hit.blockPos.x);
      float y = static_cast<float>(hit.blockPos.y);
      float z = static_cast<float>(hit.blockPos.z);
      outBoxes.push_back(
          {x - 0.006f, y - 0.006f, z - 0.006f, x + 1.006f, y + 1.006f,
           z + 1.006f, 0xDCE0E038u, 3.2f});
    }
    return;
  }

  if (g_Phase == CapturePhase::HasPos1NeedsPos2) {
    float p1x = static_cast<float>(g_Pos1.x);
    float p1y = static_cast<float>(g_Pos1.y);
    float p1z = static_cast<float>(g_Pos1.z);
    outBoxes.push_back(
        {p1x - 0.008f, p1y - 0.008f, p1z - 0.008f, p1x + 1.008f, p1y + 1.008f,
         p1z + 1.008f, 0xEB40D1FFu, 3.5f});

    BedrockGame::HitInfo hit;
    if (BedrockGame::GetAimedHit(hit) && hit.isBlockHit()) {
      float bx0 = static_cast<float>(std::min(g_Pos1.x, hit.blockPos.x));
      float by0 = static_cast<float>(std::min(g_Pos1.y, hit.blockPos.y));
      float bz0 = static_cast<float>(std::min(g_Pos1.z, hit.blockPos.z));
      float bx1 = static_cast<float>(std::max(g_Pos1.x, hit.blockPos.x) + 1);
      float by1 = static_cast<float>(std::max(g_Pos1.y, hit.blockPos.y) + 1);
      float bz1 = static_cast<float>(std::max(g_Pos1.z, hit.blockPos.z) + 1);
      outBoxes.push_back(
          {bx0 - 0.01f, by0 - 0.01f, bz0 - 0.01f, bx1 + 0.01f, by1 + 0.01f,
           bz1 + 0.01f, 0xEBFFE040u, 3.8f});
    }
    return;
  }

  if (g_Phase == CapturePhase::BoxLockedReadyToSave) {
    float bx0 = static_cast<float>(g_MinB.x);
    float by0 = static_cast<float>(g_MinB.y);
    float bz0 = static_cast<float>(g_MinB.z);
    float bx1 = static_cast<float>(g_MaxB.x + 1);
    float by1 = static_cast<float>(g_MaxB.y + 1);
    float bz1 = static_cast<float>(g_MaxB.z + 1);
    outBoxes.push_back(
        {bx0 - 0.01f, by0 - 0.01f, bz0 - 0.01f, bx1 + 0.01f, by1 + 0.01f,
         bz1 + 0.01f, 0xF5A3F248u, 4.0f});

    for (uint64_t key : g_PendingCols) {
      int32_t cx = 0, cz = 0;
      UnpackCol(key, cx, cz);
      float cx0 = static_cast<float>(std::max(g_MinB.x, cx << 4));
      float cx1 = static_cast<float>(std::min(g_MaxB.x, (cx << 4) + 15) + 1);
      float cz0 = static_cast<float>(std::max(g_MinB.z, cz << 4));
      float cz1 = static_cast<float>(std::min(g_MaxB.z, (cz << 4) + 15) + 1);
      outBoxes.push_back({cx0, by0, cz0, cx1, by1, cz1, 0xC82EADFFu, 3.0f});
    }
  }
}

bool HandleBareHandRightClick() {
  BedrockGame::HitInfo hit;
  if (!BedrockGame::GetAimedHit(hit) || !hit.isBlockHit())
    return false;

  std::lock_guard<std::mutex> lock(g_CapMutex);
  if (!g_CaptureActive)
    return false;

  if (g_Phase == CapturePhase::Idle || g_Phase == CapturePhase::ArmedNeedsPos1 ||
      g_Phase == CapturePhase::BoxLockedReadyToSave) {
    g_Pos1 = hit.blockPos;
    g_Pos1SetMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
    g_Phase = CapturePhase::HasPos1NeedsPos2;
    g_CapturedCols.clear();
    g_PendingCols.clear();
    g_TotalCols = 0;
    g_StagedDoc.Reset("New Capture");
    ClearScratch();
    SoundManager::Play(UISound::Click);
    Logger::info("Pos1 set at (" + std::to_string(g_Pos1.x) + ", " +
                     std::to_string(g_Pos1.y) + ", " +
                     std::to_string(g_Pos1.z) + ")",
                 "Capture");
    ToastManager::Show(ToastType::Info, "POS 1 SET",
                       "Corner 1 locked at (" + std::to_string(g_Pos1.x) +
                           ", " + std::to_string(g_Pos1.y) + ", " +
                           std::to_string(g_Pos1.z) +
                           "). Right-click Corner 2.");
    return true;
  }

  if (g_Phase == CapturePhase::HasPos1NeedsPos2) {
    g_Pos2 = hit.blockPos;
    g_Pos2SetMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
    void *bs = BedrockGame::GetBlockSource();
    SoundManager::Play(UISound::Success);
    FinalizeBoxFromPos1Pos2(bs);
    return true;
  }

  return false;
}

void TickWorldUpdate() {
  BedrockGame::UpdateCameraForward();
  void *bs = BedrockGame::GetBlockSource();
  if (!bs)
    return;

  std::lock_guard<std::mutex> lock(g_CapMutex);
  g_TickCounter++;
  if ((g_TickCounter % 6) != 0)
    return;

  if (g_Phase == CapturePhase::HasPos1NeedsPos2) {
    BedrockGame::Vec3f cam = BedrockGame::GetCameraOrigin();
    int32_t pcx = static_cast<int32_t>(std::floor(cam.x)) >> 4;
    int32_t pcz = static_cast<int32_t>(std::floor(cam.z)) >> 4;
    int cachedThisTick = 0;
    for (int32_t dz = -2; dz <= 2 && cachedThisTick < 1; ++dz) {
      for (int32_t dx = -2; dx <= 2 && cachedThisTick < 1; ++dx) {
        int32_t cx = pcx + dx;
        int32_t cz = pcz + dz;
        uint64_t key = PackCol(cx, cz);
        if (g_ScratchMap.find(key) == g_ScratchMap.end() &&
            BedrockGame::IsChunkColumnLoaded(bs, cx, cz)) {
          SnapshotColumnToScratch(bs, cx, cz);
          cachedThisTick++;
        }
      }
    }
  } else if (g_Phase == CapturePhase::BoxLockedReadyToSave &&
             !g_PendingCols.empty()) {
    int filled = 0;
    for (auto it = g_PendingCols.begin();
         it != g_PendingCols.end() && filled < 2;) {
      int32_t cx = 0, cz = 0;
      UnpackCol(*it, cx, cz);
      if (BedrockGame::IsChunkColumnLoaded(bs, cx, cz)) {
        HarvestColumnIntoStaged(bs, cx, cz);
        it = g_PendingCols.erase(it);
        filled++;
      } else {
        ++it;
      }
    }
    if (filled > 0) {
      g_StagedDoc.RecomputeStats();
      if (g_PendingCols.empty()) {
        SoundManager::Play(UISound::Success);
        ToastManager::Show(
            ToastType::Success, "CAPTURE COMPLETE (100%)",
            "All unloaded chunk columns filled! Open menu to SAVE.");
      }
    }
  }
}

void DrawDualOutlineBox3D(float minX, float minY, float minZ, float maxX,
                          float maxY, float maxZ, float outerThick,
                          float innerThick, float r, float g, float b,
                          float outerAlpha) {
  float gap = outerThick * 1.25f;
  BedrockGame::DrawThickBox3D(minX - gap, minY - gap, minZ - gap, maxX + gap,
                              maxY + gap, maxZ + gap, outerThick * 0.55f, r, g,
                              b, outerAlpha);
  BedrockGame::DrawThickBox3D(minX, minY, minZ, maxX, maxY, maxZ,
                              innerThick * 1.25f, 1.0f, 1.0f, 1.0f, 0.98f);
}

void RenderWorldVisuals() {
  if (!BedrockGame::IsRenderReady())
    return;

  std::lock_guard<std::mutex> lock(g_CapMutex);
  if (!g_CaptureActive)
    return;
}

bool FinalizeAndSaveBmet(const std::string &name, const std::string &dirPath,
                         std::string &outSavedPath,
                         SchematicDocument &outDoc) {
  std::lock_guard<std::mutex> lock(g_CapMutex);
  if (g_Phase != CapturePhase::BoxLockedReadyToSave)
    return false;

  std::string cleanName = name.empty() ? "Captured Build" : name;
  g_StagedDoc.name = cleanName;
  g_StagedDoc.RecomputeStats();

  std::string stem = MakeSafeFileStem(cleanName);
  std::filesystem::path dir(dirPath);
  std::filesystem::path target = dir / (stem + ".bmet");
  std::error_code ec;
  if (std::filesystem::exists(target, ec)) {
    for (int idx = 2; idx < 1000; ++idx) {
      std::filesystem::path cand =
          dir / (stem + "_" + std::to_string(idx) + ".bmet");
      if (!std::filesystem::exists(cand, ec)) {
        target = cand;
        break;
      }
    }
  }

  if (!BmetCodec::SaveToFile(target.string(), g_StagedDoc))
    return false;

  outSavedPath = target.string();
  g_StagedDoc.sourcePath = outSavedPath;
  outDoc = g_StagedDoc;

  g_Phase = g_CaptureActive ? CapturePhase::ArmedNeedsPos1 : CapturePhase::Idle;
  g_CapturedCols.clear();
  g_PendingCols.clear();
  g_TotalCols = 0;
  ClearScratch();
  return true;
}

}
