#include "BedrockGame.hpp"
#include "../Utils/Logger.hpp"
#include "../Utils/SignatureAndOffsetManager.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <windows.h>

namespace BedrockGame {

namespace {

using TessBeginFn = void(__fastcall *)(void *tess, uint64_t unused, uint8_t mode,
                                       int32_t maxVerts, bool buildFaceData);
using TessColorFn = void(__fastcall *)(void *tess, float r, float g, float b,
                                       float a);
using TessVertexFn = void(__fastcall *)(void *tess, float x, float y, float z);
using RenderMeshImmFn = void(__fastcall *)(void *scn, void *tess, void *mat,
                                           void *texVariant);

static TessBeginFn g_TessBegin = nullptr;
static TessColorFn g_TessColor = nullptr;
static TessVertexFn g_TessVertex = nullptr;
static RenderMeshImmFn g_RenderMeshImm = nullptr;

static std::mutex g_StateMutex;
static void *g_LevelRenderer = nullptr;
static void *g_LevelRendererPlayer = nullptr;
static void *g_ClientInstance = nullptr;
static void *g_BlockSource = nullptr;
static void *g_ScreenContext = nullptr;
static Vec3f g_CameraOrigin{};
static Vec3f g_CameraForward{0.0f, 0.0f, -1.0f};
static float g_FovDeg = 70.0f;

static void LoadFovFromOptions() {
  char appData[MAX_PATH] = {};
  if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) == 0)
    return;
  std::filesystem::path usersDir =
      std::filesystem::path(appData) / "Minecraft Bedrock" / "Users";
  std::error_code ec;
  if (!std::filesystem::exists(usersDir, ec))
    return;
  std::filesystem::file_time_type bestTime{};
  bool found = false;
  float bestFov = 70.0f;
  for (const auto &entry : std::filesystem::directory_iterator(usersDir, ec)) {
    if (!entry.is_directory(ec))
      continue;
    if (entry.path().filename() == "Shared")
      continue;
    std::filesystem::path opt =
        entry.path() / "games" / "com.mojang" / "minecraftpe" / "options.txt";
    if (!std::filesystem::exists(opt, ec))
      continue;
    auto ft = std::filesystem::last_write_time(opt, ec);
    if (!found || ft > bestTime) {
      std::ifstream f(opt);
      std::string line;
      while (std::getline(f, line)) {
        if (line.rfind("gfx_field_of_view:", 0) == 0) {
          float v = std::strtof(line.c_str() + 18, nullptr);
          if (v >= 30.0f && v <= 110.0f) {
            bestFov = v;
            bestTime = ft;
            found = true;
          }
          break;
        }
      }
    }
  }
  if (found) {
    g_FovDeg = bestFov;
  }
}

static bool IsValidReadablePtr(const void *p) {
  uintptr_t v = reinterpret_cast<uintptr_t>(p);
  return v >= 0x10000ULL && v < 0x00007FFFFFFFFFFFULL;
}

template <typename Ret, typename... Args>
Ret CallVFunc(size_t index, void *obj, Args... args) {
  auto **vtable = *reinterpret_cast<void ***>(obj);
  auto fn = reinterpret_cast<Ret(__fastcall *)(void *, Args...)>(vtable[index]);
  return fn(obj, args...);
}

struct ChunkPos2i {
  int32_t x = 0;
  int32_t z = 0;
};

struct RawHitResultHeader {
  float startPos[3];
  float rayDir[3];
  int32_t type;
  int8_t facing;
  uint8_t pad[3];
  int32_t blockX;
  int32_t blockY;
  int32_t blockZ;
};

struct MsvcStringRaw {
  union {
    char buf[16];
    const char *ptr;
  };
  size_t size;
  size_t res;

  std::string ToStdString() const {
    if (size == 0 || size > 4096)
      return "";
    if (res >= 16) {
      if (!IsValidReadablePtr(ptr))
        return "";
      return std::string(ptr, size);
    }
    return std::string(buf, size);
  }
};

uint8_t GetTagIdSafe(const void *tagPtr) {
  if (!IsValidReadablePtr(tagPtr))
    return 0;
  __try {
    auto **vt = *reinterpret_cast<void **const *>(tagPtr);
    if (!IsValidReadablePtr(vt))
      return 0;
    auto fn = reinterpret_cast<uint8_t(__fastcall *)(const void *)>(vt[5]);
    return fn(tagPtr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

void ReadCompoundStates(uintptr_t compoundPtr,
                        std::vector<Bedrockmetica::BlockStateProp> &outProps) {
  uintptr_t head = *reinterpret_cast<const uintptr_t *>(compoundPtr + 8);
  if (!IsValidReadablePtr(reinterpret_cast<const void *>(head)))
    return;
  uintptr_t root = *reinterpret_cast<const uintptr_t *>(head + 8);
  if (!IsValidReadablePtr(reinterpret_cast<const void *>(root)))
    return;

  std::vector<uintptr_t> stack;
  stack.reserve(32);
  stack.push_back(root);

  int visited = 0;
  while (!stack.empty() && visited < 64) {
    uintptr_t node = stack.back();
    stack.pop_back();
    if (!IsValidReadablePtr(reinterpret_cast<const void *>(node)))
      continue;
    uint8_t isNil = *reinterpret_cast<const uint8_t *>(node + 0x19);
    if (isNil != 0)
      continue;
    visited++;

    uintptr_t left = *reinterpret_cast<const uintptr_t *>(node + 0x00);
    uintptr_t right = *reinterpret_cast<const uintptr_t *>(node + 0x10);
    stack.push_back(right);
    stack.push_back(left);

    const auto *keyStr = reinterpret_cast<const MsvcStringRaw *>(node + 0x20);
    std::string k = keyStr->ToStdString();
    if (k.empty())
      continue;

    uint8_t varIdx = *reinterpret_cast<const uint8_t *>(node + 0x68);
    if (varIdx == 0xFF)
      continue;

    const void *tagPtr = reinterpret_cast<const void *>(node + 0x40);
    uint8_t tagId = GetTagIdSafe(tagPtr);
    Bedrockmetica::BlockStateProp prop;
    prop.key = std::move(k);
    if (tagId == 1) {
      prop.type = Bedrockmetica::BlockStateType::Byte;
      prop.intVal =
          static_cast<int32_t>(*reinterpret_cast<const int8_t *>(node + 0x48));
      outProps.push_back(std::move(prop));
    } else if (tagId == 3) {
      prop.type = Bedrockmetica::BlockStateType::Int;
      prop.intVal = *reinterpret_cast<const int32_t *>(node + 0x48);
      outProps.push_back(std::move(prop));
    } else if (tagId == 8) {
      prop.type = Bedrockmetica::BlockStateType::String;
      const auto *valStr = reinterpret_cast<const MsvcStringRaw *>(node + 0x48);
      prop.strVal = valStr->ToStdString();
      outProps.push_back(std::move(prop));
    }
  }
}

bool ReadSerializationIdImpl(
    const void *blockPtr, std::string &outName,
    std::vector<Bedrockmetica::BlockStateProp> &outProps) {
  uintptr_t serOff =
      static_cast<uintptr_t>(GET_OFFSET("Block::serializationId"));
  if (serOff == 0)
    serOff = 0xF0;
  uintptr_t comp = reinterpret_cast<uintptr_t>(blockPtr) + serOff;
  uintptr_t head = *reinterpret_cast<const uintptr_t *>(comp + 8);
  if (!IsValidReadablePtr(reinterpret_cast<const void *>(head)))
    return false;
  uintptr_t root = *reinterpret_cast<const uintptr_t *>(head + 8);
  if (!IsValidReadablePtr(reinterpret_cast<const void *>(root)))
    return false;

  std::vector<uintptr_t> stack;
  stack.reserve(16);
  stack.push_back(root);
  int visited = 0;
  while (!stack.empty() && visited < 32) {
    uintptr_t node = stack.back();
    stack.pop_back();
    if (!IsValidReadablePtr(reinterpret_cast<const void *>(node)))
      continue;
    if (*reinterpret_cast<const uint8_t *>(node + 0x19) != 0)
      continue;
    visited++;

    stack.push_back(*reinterpret_cast<const uintptr_t *>(node + 0x10));
    stack.push_back(*reinterpret_cast<const uintptr_t *>(node + 0x00));

    const auto *keyStr = reinterpret_cast<const MsvcStringRaw *>(node + 0x20);
    std::string k = keyStr->ToStdString();
    uint8_t varIdx = *reinterpret_cast<const uint8_t *>(node + 0x68);
    if (varIdx == 0xFF)
      continue;

    const void *tagPtr = reinterpret_cast<const void *>(node + 0x40);
    if (k == "name" && GetTagIdSafe(tagPtr) == 8) {
      const auto *nameRaw =
          reinterpret_cast<const MsvcStringRaw *>(node + 0x48);
      outName = nameRaw->ToStdString();
    } else if (k == "states" && GetTagIdSafe(tagPtr) == 10) {
      ReadCompoundStates(node + 0x40, outProps);
    }
  }
  return !outName.empty();
}

bool ReadSerializationIdSeh(
    const void *blockPtr, std::string &outName,
    std::vector<Bedrockmetica::BlockStateProp> &outProps) {
  __try {
    return ReadSerializationIdImpl(blockPtr, outName, outProps);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool TryReadPtrAt(const void *base, uintptr_t off, void **outPtr) {
  if (!IsValidReadablePtr(base) || !outPtr)
    return false;
  __try {
    void *val =
        *reinterpret_cast<void *const *>(reinterpret_cast<uintptr_t>(base) + off);
    if (IsValidReadablePtr(val)) {
      *outPtr = val;
      return true;
    }
    return false;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool TryReadVec3fAt(const void *base, uintptr_t off, ::BedrockGame::Vec3f *outVec) {
  if (!IsValidReadablePtr(base) || !outVec)
    return false;
  __try {
    *outVec = *reinterpret_cast<const ::BedrockGame::Vec3f *>(
        reinterpret_cast<uintptr_t>(base) + off);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void EmitQuadFace(void *tess, float x0, float y0, float z0, float x1, float y1,
                  float z1, float x2, float y2, float z2, float x3, float y3,
                  float z3) {
  g_TessVertex(tess, x0, y0, z0);
  g_TessVertex(tess, x1, y1, z1);
  g_TessVertex(tess, x2, y2, z2);
  g_TessVertex(tess, x3, y3, z3);
}

void EmitLine(void *tess, float x0, float y0, float z0, float x1, float y1,
              float z1) {
  g_TessVertex(tess, x0, y0, z0);
  g_TessVertex(tess, x1, y1, z1);
}

}

void Initialize() {
  LoadFovFromOptions();
  g_TessBegin =
      reinterpret_cast<TessBeginFn>(GET_SIG_ADDRESS("Tessellator::begin"));
  g_TessColor =
      reinterpret_cast<TessColorFn>(GET_SIG_ADDRESS("Tessellator::color"));
  g_TessVertex =
      reinterpret_cast<TessVertexFn>(GET_SIG_ADDRESS("Tessellator::vertex"));
  g_RenderMeshImm = reinterpret_cast<RenderMeshImmFn>(
      GET_SIG_ADDRESS("MeshHelpers::renderMeshImmediately"));
}

void UpdateFromRenderLevel(void *levelRenderer, void *screenContext) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_LevelRenderer = levelRenderer;
  g_ScreenContext = screenContext;
  uintptr_t ciOff =
      static_cast<uintptr_t>(GET_OFFSET("LevelRenderer::clientInstance"));
  if (ciOff == 0)
    ciOff = 0x1B8;
  void *ci = nullptr;
  if (TryReadPtrAt(levelRenderer, ciOff, &ci)) {
    g_ClientInstance = ci;
  }
}

void UpdateFromLevelRendererPlayer(void *lrp, void *baseCtx,
                                   void *blockSource) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_LevelRendererPlayer = lrp;
  if (IsValidReadablePtr(blockSource)) {
    g_BlockSource = blockSource;
  }
  void *scn = nullptr;
  if (TryReadPtrAt(baseCtx, 0x28, &scn)) {
    g_ScreenContext = scn;
  }
  uintptr_t camOff =
      static_cast<uintptr_t>(GET_OFFSET("LevelRendererPlayer::cameraPos"));
  if (camOff == 0)
    camOff = 0x658;
  TryReadVec3fAt(lrp, camOff, &g_CameraOrigin);

  uintptr_t ciOff = static_cast<uintptr_t>(
      GET_OFFSET("LevelRendererPlayer::clientInstance"));
  if (ciOff == 0)
    ciOff = 0x1160;
  void *ci = nullptr;
  if (TryReadPtrAt(lrp, ciOff, &ci)) {
    g_ClientInstance = ci;
  }
}

bool IsRenderReady() {
  return g_TessBegin && g_TessColor && g_TessVertex && g_RenderMeshImm &&
         IsValidReadablePtr(g_ScreenContext) &&
         IsValidReadablePtr(g_LevelRendererPlayer);
}

void *GetClientInstance() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_ClientInstance;
}

void *GetBlockSource() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_BlockSource;
}

void *GetScreenContext() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_ScreenContext;
}

void *GetTessellator() {
  void *scn = GetScreenContext();
  if (!IsValidReadablePtr(scn))
    return nullptr;
  __try {
    uintptr_t off =
        static_cast<uintptr_t>(GET_OFFSET("ScreenContext::tessellator"));
    if (off == 0)
      off = 0xB0;
    void *t = *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(scn) + off);
    return IsValidReadablePtr(t) ? t : nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

void *GetSelectionBoxMaterial() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  if (!IsValidReadablePtr(g_LevelRendererPlayer))
    return nullptr;
  uintptr_t off =
      static_cast<uintptr_t>(GET_OFFSET("LevelRendererPlayer::selectionBoxMat"));
  if (off == 0)
    off = 0x10E8;
  return reinterpret_cast<void *>(
      reinterpret_cast<uintptr_t>(g_LevelRendererPlayer) + off);
}

void *GetSelectionOverlayMaterial() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  if (!IsValidReadablePtr(g_LevelRendererPlayer))
    return nullptr;
  uintptr_t off = static_cast<uintptr_t>(
      GET_OFFSET("LevelRendererPlayer::selectionOverlayMat"));
  if (off == 0)
    off = 0x1088;
  return reinterpret_cast<void *>(
      reinterpret_cast<uintptr_t>(g_LevelRendererPlayer) + off);
}

Vec3f GetCameraOrigin() {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_CameraOrigin;
}

bool TryReadCarriedItemSeh(void *ci, int lvlPlayerIdx, uintptr_t &outPlayer,
                           uintptr_t &outStack, uintptr_t &outItemPtr,
                           uint8_t &outCount, uint8_t &outValid,
                           int32_t &outSlot, bool &outDisabled) {
  outPlayer = 0;
  outStack = 0;
  outItemPtr = 0;
  outCount = 0;
  outValid = 0;
  outSlot = -1;
  outDisabled = false;
  __try {
    void *player = CallVFunc<void *>(static_cast<size_t>(lvlPlayerIdx), ci);
    outPlayer = reinterpret_cast<uintptr_t>(player);
    if (!IsValidReadablePtr(player))
      return false;

    uintptr_t supplies = *reinterpret_cast<const uintptr_t *>(outPlayer + 0x5E0);
    if (!IsValidReadablePtr(reinterpret_cast<const void *>(supplies)))
      return false;

    if (*reinterpret_cast<const uint8_t *>(supplies + 0xB0) != 0) {
      outDisabled = true;
      return true;
    }

    outSlot = *reinterpret_cast<const int32_t *>(supplies + 0x10);
    void *inv = *reinterpret_cast<void *const *>(supplies + 0xB8);
    if (!IsValidReadablePtr(inv))
      return false;

    void *itemStack = CallVFunc<void *>(7, inv, outSlot);
    outStack = reinterpret_cast<uintptr_t>(itemStack);
    if (!IsValidReadablePtr(itemStack))
      return false;

    outItemPtr = *reinterpret_cast<const uintptr_t *>(outStack + 0x08);
    outCount = *reinterpret_cast<const uint8_t *>(outStack + 0x22);
    outValid = *reinterpret_cast<const uint8_t *>(outStack + 0x23);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool IsLocalPlayerBareHanded() {
  void *ci = GetClientInstance();
  if (!IsValidReadablePtr(ci)) {
    Logger::warn("BareHand: ClientInstance null", "Capture");
    return false;
  }
  int idx = GET_OFFSET("ClientInstance::getLocalPlayerVIdx");
  if (idx <= 0)
    idx = 173;

  uintptr_t pPtr = 0, sPtr = 0, itemPtr = 0;
  uint8_t count = 0, valid = 0;
  int32_t slot = -1;
  bool disabled = false;
  bool ok = TryReadCarriedItemSeh(ci, idx, pPtr, sPtr, itemPtr, count, valid,
                                  slot, disabled);
  char msg[224];
  snprintf(msg, sizeof(msg),
           "BareHand ok=%d dis=%d slot=%d player=0x%llX stack=0x%llX item=0x%llX cnt=%u val=%u",
           ok ? 1 : 0, disabled ? 1 : 0, static_cast<int>(slot),
           static_cast<unsigned long long>(pPtr),
           static_cast<unsigned long long>(sPtr),
           static_cast<unsigned long long>(itemPtr),
           static_cast<unsigned>(count), static_cast<unsigned>(valid));
  Logger::info(msg, "Capture");
  if (!ok)
    return false;
  if (disabled)
    return true;
  return (itemPtr == 0) || (count == 0) || (valid == 0);
}

bool GetAimedHit(HitInfo &outHit) {
  void *ci = GetClientInstance();
  if (!IsValidReadablePtr(ci))
    return false;
  __try {
    int lvlIdx = GET_OFFSET("ClientInstance::getLevelVIdx");
    if (lvlIdx <= 0)
      lvlIdx = 167;
    void *lvl = CallVFunc<void *>(static_cast<size_t>(lvlIdx), ci);
    if (!IsValidReadablePtr(lvl))
      return false;

    int hitIdx = GET_OFFSET("Level::getHitResultVIdx");
    if (hitIdx <= 0)
      hitIdx = 335;
    const auto *hr =
        CallVFunc<const RawHitResultHeader *>(static_cast<size_t>(hitIdx), lvl);
    if (!IsValidReadablePtr(hr))
      return false;

    outHit.type = hr->type;
    outHit.facing = hr->facing;
    outHit.blockPos = {hr->blockX, hr->blockY, hr->blockZ};
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool GetLocalPlayerPos(Vec3f &outPos) {
  outPos = GetCameraOrigin();
  return IsValidReadablePtr(g_LevelRendererPlayer);
}

bool IsChunkColumnLoaded(void *blockSource, int32_t cx, int32_t cz) {
  if (!IsValidReadablePtr(blockSource))
    return false;
  __try {
    int idx = GET_OFFSET("BlockSource::getChunkVIdx");
    if (idx <= 0)
      idx = 40;
    BlockPos3i bp{(cx << 4) + 8, 64, (cz << 4) + 8};
    void *chunk =
        CallVFunc<void *>(static_cast<size_t>(idx), blockSource, &bp);
    return IsValidReadablePtr(chunk);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

const void *GetBlockPtr(void *blockSource, int32_t x, int32_t y, int32_t z) {
  if (!IsValidReadablePtr(blockSource))
    return nullptr;
  __try {
    int idx = GET_OFFSET("BlockSource::getBlockVIdx");
    if (idx <= 0)
      idx = 2;
    BlockPos3i bp{x, y, z};
    const void *blk =
        CallVFunc<const void *>(static_cast<size_t>(idx), blockSource, &bp);
    return IsValidReadablePtr(blk) ? blk : nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

bool ReadBlockSerialization(
    const void *blockPtr, std::string &outName,
    std::vector<Bedrockmetica::BlockStateProp> &outProps) {
  outName.clear();
  outProps.clear();
  if (!IsValidReadablePtr(blockPtr))
    return false;
  return ReadSerializationIdSeh(blockPtr, outName, outProps);
}

void DrawWireBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                   float maxZ, float r, float g, float b, float a) {
  if (!IsRenderReady())
    return;
  void *scn = GetScreenContext();
  void *tess = GetTessellator();
  void *mat = GetSelectionBoxMaterial();
  if (!scn || !tess || !mat)
    return;

  Vec3f origin = GetCameraOrigin();
  float x0 = minX - origin.x, y0 = minY - origin.y, z0 = minZ - origin.z;
  float x1 = maxX - origin.x, y1 = maxY - origin.y, z1 = maxZ - origin.z;

  __try {
    uintptr_t colOff =
        static_cast<uintptr_t>(GET_OFFSET("ScreenContext::colorHolder"));
    if (colOff == 0)
      colOff = 0x30;
    auto *colHolder = *reinterpret_cast<float **>(
        reinterpret_cast<uintptr_t>(scn) + colOff);
    if (IsValidReadablePtr(colHolder)) {
      colHolder[0] = r;
      colHolder[1] = g;
      colHolder[2] = b;
      colHolder[3] = a;
      *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(colHolder) +
                                   0x10) = 1;
    }

    auto *tessOff =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1CC);
    tessOff[0] = 0.0f;
    tessOff[1] = 0.0f;
    tessOff[2] = 0.0f;
    auto *tessScale =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1D8);
    tessScale[0] = 1.0f;
    tessScale[1] = 1.0f;
    tessScale[2] = 1.0f;

    g_TessBegin(tess, 0, 4, 24, false);
    g_TessColor(tess, r, g, b, a);

    EmitLine(tess, x0, y0, z0, x1, y0, z0);
    EmitLine(tess, x1, y0, z0, x1, y0, z1);
    EmitLine(tess, x1, y0, z1, x0, y0, z1);
    EmitLine(tess, x0, y0, z1, x0, y0, z0);

    EmitLine(tess, x0, y1, z0, x1, y1, z0);
    EmitLine(tess, x1, y1, z0, x1, y1, z1);
    EmitLine(tess, x1, y1, z1, x0, y1, z1);
    EmitLine(tess, x0, y1, z1, x0, y1, z0);

    EmitLine(tess, x0, y0, z0, x0, y1, z0);
    EmitLine(tess, x1, y0, z0, x1, y1, z0);
    EmitLine(tess, x1, y0, z1, x1, y1, z1);
    EmitLine(tess, x0, y0, z1, x0, y1, z1);

    uint8_t emptyTexVariant[48] = {};
    g_RenderMeshImm(scn, tess, mat, emptyTexVariant);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void DrawFilledBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                     float maxZ, float r, float g, float b, float a) {
  if (!IsRenderReady())
    return;
  void *scn = GetScreenContext();
  void *tess = GetTessellator();
  void *mat = GetSelectionOverlayMaterial();
  if (!scn || !tess || !mat)
    return;

  Vec3f origin = GetCameraOrigin();
  float x0 = minX - origin.x, y0 = minY - origin.y, z0 = minZ - origin.z;
  float x1 = maxX - origin.x, y1 = maxY - origin.y, z1 = maxZ - origin.z;

  __try {
    uintptr_t colOff =
        static_cast<uintptr_t>(GET_OFFSET("ScreenContext::colorHolder"));
    if (colOff == 0)
      colOff = 0x30;
    auto *colHolder = *reinterpret_cast<float **>(
        reinterpret_cast<uintptr_t>(scn) + colOff);
    if (IsValidReadablePtr(colHolder)) {
      colHolder[0] = r;
      colHolder[1] = g;
      colHolder[2] = b;
      colHolder[3] = a;
      *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(colHolder) +
                                   0x10) = 1;
    }

    auto *tessOff =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1CC);
    tessOff[0] = 0.0f;
    tessOff[1] = 0.0f;
    tessOff[2] = 0.0f;
    auto *tessScale =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1D8);
    tessScale[0] = 1.0f;
    tessScale[1] = 1.0f;
    tessScale[2] = 1.0f;

    g_TessBegin(tess, 0, 1, 24, false);
    g_TessColor(tess, r, g, b, a);

    EmitQuadFace(tess, x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1);
    EmitQuadFace(tess, x0, y1, z1, x1, y1, z1, x1, y1, z0, x0, y1, z0);
    EmitQuadFace(tess, x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0);
    EmitQuadFace(tess, x1, y0, z1, x1, y1, z1, x0, y1, z1, x0, y0, z1);
    EmitQuadFace(tess, x0, y0, z1, x0, y1, z1, x0, y1, z0, x0, y0, z0);
    EmitQuadFace(tess, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1);

    uint8_t emptyTexVariant[48] = {};
    g_RenderMeshImm(scn, tess, mat, emptyTexVariant);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void DrawThickBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                    float maxZ, float thickness, float r, float g, float b,
                    float a) {
  if (!IsRenderReady())
    return;
  void *scn = GetScreenContext();
  void *tess = GetTessellator();
  void *mat = GetSelectionOverlayMaterial();
  if (!scn || !tess || !mat)
    return;

  Vec3f origin = GetCameraOrigin();
  float x0 = minX - origin.x, y0 = minY - origin.y, z0 = minZ - origin.z;
  float x1 = maxX - origin.x, y1 = maxY - origin.y, z1 = maxZ - origin.z;
  float t = thickness * 0.5f;

  auto emitCuboid = [&](float ax0, float ay0, float az0, float ax1, float ay1,
                        float az1) {
    EmitQuadFace(tess, ax0, ay0, az0, ax1, ay0, az0, ax1, ay0, az1, ax0, ay0,
                 az1);
    EmitQuadFace(tess, ax0, ay1, az1, ax1, ay1, az1, ax1, ay1, az0, ax0, ay1,
                 az0);
    EmitQuadFace(tess, ax0, ay0, az0, ax0, ay1, az0, ax1, ay1, az0, ax1, ay0,
                 az0);
    EmitQuadFace(tess, ax1, ay0, az1, ax1, ay1, az1, ax0, ay1, az1, ax0, ay0,
                 az1);
    EmitQuadFace(tess, ax0, ay0, az1, ax0, ay1, az1, ax0, ay1, az0, ax0, ay0,
                 az0);
    EmitQuadFace(tess, ax1, ay0, az0, ax1, ay1, az0, ax1, ay1, az1, ax1, ay0,
                 az1);
  };

  __try {
    uintptr_t colOff =
        static_cast<uintptr_t>(GET_OFFSET("ScreenContext::colorHolder"));
    if (colOff == 0)
      colOff = 0x30;
    auto *colHolder = *reinterpret_cast<float **>(
        reinterpret_cast<uintptr_t>(scn) + colOff);
    if (IsValidReadablePtr(colHolder)) {
      colHolder[0] = r;
      colHolder[1] = g;
      colHolder[2] = b;
      colHolder[3] = a;
      *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(colHolder) +
                                   0x10) = 1;
    }

    auto *tessOff =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1CC);
    tessOff[0] = 0.0f;
    tessOff[1] = 0.0f;
    tessOff[2] = 0.0f;
    auto *tessScale =
        reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1D8);
    tessScale[0] = 1.0f;
    tessScale[1] = 1.0f;
    tessScale[2] = 1.0f;

    g_TessBegin(tess, 0, 1, 288, false);
    g_TessColor(tess, r, g, b, a);

    emitCuboid(x0 - t, y0 - t, z0 - t, x1 + t, y0 + t, z0 + t);
    emitCuboid(x0 - t, y0 - t, z1 - t, x1 + t, y0 + t, z1 + t);
    emitCuboid(x0 - t, y1 - t, z0 - t, x1 + t, y1 + t, z0 + t);
    emitCuboid(x0 - t, y1 - t, z1 - t, x1 + t, y1 + t, z1 + t);

    emitCuboid(x0 - t, y0 - t, z0 - t, x0 + t, y0 + t, z1 + t);
    emitCuboid(x1 - t, y0 - t, z0 - t, x1 + t, y0 + t, z1 + t);
    emitCuboid(x0 - t, y1 - t, z0 - t, x0 + t, y1 + t, z1 + t);
    emitCuboid(x1 - t, y1 - t, z0 - t, x1 + t, y1 + t, z1 + t);

    emitCuboid(x0 - t, y0 - t, z0 - t, x0 + t, y1 + t, z0 + t);
    emitCuboid(x1 - t, y0 - t, z0 - t, x1 + t, y1 + t, z0 + t);
    emitCuboid(x0 - t, y0 - t, z1 - t, x0 + t, y1 + t, z1 + t);
    emitCuboid(x1 - t, y0 - t, z1 - t, x1 + t, y1 + t, z1 + t);

    uint8_t emptyTexVariant[48] = {};
    g_RenderMeshImm(scn, tess, mat, emptyTexVariant);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

static bool TryReadCameraForwardSeh(void *ci, Vec3f &outFwd) {
  __try {
    int lvlIdx = GET_OFFSET("ClientInstance::getLevelVIdx");
    if (lvlIdx <= 0)
      lvlIdx = 167;
    void *lvl = CallVFunc<void *>(static_cast<size_t>(lvlIdx), ci);
    if (!IsValidReadablePtr(lvl))
      return false;

    int hitIdx = GET_OFFSET("Level::getHitResultVIdx");
    if (hitIdx <= 0)
      hitIdx = 335;
    const auto *hr =
        CallVFunc<const RawHitResultHeader *>(static_cast<size_t>(hitIdx), lvl);
    if (!IsValidReadablePtr(hr))
      return false;

    float fx = hr->rayDir[0];
    float fy = hr->rayDir[1];
    float fz = hr->rayDir[2];
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 0.001f && len < 10.0f && !std::isnan(len)) {
      outFwd = {fx / len, fy / len, fz / len};
      return true;
    }
    return false;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void UpdateCameraForward() {
  void *ci = GetClientInstance();
  if (!IsValidReadablePtr(ci))
    return;
  Vec3f fwd{};
  if (TryReadCameraForwardSeh(ci, fwd)) {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_CameraForward = fwd;
  }
}

bool WorldToScreen(float wx, float wy, float wz, float screenW, float screenH,
                   float &outX, float &outY, float &outDist) {
  if (screenW <= 1.0f || screenH <= 1.0f || !IsValidReadablePtr(g_LevelRendererPlayer))
    return false;

  Vec3f origin{};
  Vec3f fwd{};
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    origin = g_CameraOrigin;
    fwd = g_CameraForward;
  }

  float rx = fwd.y * 0.0f - fwd.z * 1.0f;
  float ry = fwd.z * 0.0f - fwd.x * 0.0f;
  float rz = fwd.x * 1.0f - fwd.y * 0.0f;
  float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
  if (rLen < 0.001f) {
    rx = 1.0f;
    ry = 0.0f;
    rz = 0.0f;
  } else {
    rx /= rLen;
    ry /= rLen;
    rz /= rLen;
  }

  float ux = ry * fwd.z - rz * fwd.y;
  float uy = rz * fwd.x - rx * fwd.z;
  float uz = rx * fwd.y - ry * fwd.x;

  float dx = wx - origin.x;
  float dy = wy - origin.y;
  float dz = wz - origin.z;
  outDist = std::sqrt(dx * dx + dy * dy + dz * dz);

  float zCam = dx * fwd.x + dy * fwd.y + dz * fwd.z;
  if (zCam < 0.05f)
    return false;

  float xCam = dx * rx + dy * ry + dz * rz;
  float yCam = dx * ux + dy * uy + dz * uz;

  const float fovY = g_FovDeg * (3.14159265f / 180.0f);
  float tanHalfFovY = std::tan(fovY * 0.5f);
  float tanHalfFovX = tanHalfFovY * (screenW / screenH);

  float ndcX = xCam / (zCam * tanHalfFovX);
  float ndcY = yCam / (zCam * tanHalfFovY);

  if (ndcX < -1.35f || ndcX > 1.35f || ndcY < -1.35f || ndcY > 1.35f)
    return false;

  outX = (ndcX + 1.0f) * 0.5f * screenW;
  outY = (1.0f - ndcY) * 0.5f * screenH;
  return true;
}

bool ProjectLine3D(float x0, float y0, float z0, float x1, float y1, float z1,
                   float screenW, float screenH, float &sx0, float &sy0,
                   float &sx1, float &sy1) {
  if (screenW <= 1.0f || screenH <= 1.0f ||
      !IsValidReadablePtr(g_LevelRendererPlayer))
    return false;

  Vec3f origin{};
  Vec3f fwd{};
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    origin = g_CameraOrigin;
    fwd = g_CameraForward;
  }

  float rx = fwd.y * 0.0f - fwd.z * 1.0f;
  float ry = fwd.z * 0.0f - fwd.x * 0.0f;
  float rz = fwd.x * 1.0f - fwd.y * 0.0f;
  float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
  if (rLen < 0.001f) {
    rx = 1.0f;
    ry = 0.0f;
    rz = 0.0f;
  } else {
    rx /= rLen;
    ry /= rLen;
    rz /= rLen;
  }

  float ux = ry * fwd.z - rz * fwd.y;
  float uy = rz * fwd.x - rx * fwd.z;
  float uz = rx * fwd.y - ry * fwd.x;

  auto toCam = [&](float wx, float wy, float wz, float &xc, float &yc,
                   float &zc) {
    float dx = wx - origin.x;
    float dy = wy - origin.y;
    float dz = wz - origin.z;
    xc = dx * rx + dy * ry + dz * rz;
    yc = dx * ux + dy * uy + dz * uz;
    zc = dx * fwd.x + dy * fwd.y + dz * fwd.z;
  };

  float xc0, yc0, zc0, xc1, yc1, zc1;
  toCam(x0, y0, z0, xc0, yc0, zc0);
  toCam(x1, y1, z1, xc1, yc1, zc1);

  const float nearZ = 0.05f;
  if (zc0 < nearZ && zc1 < nearZ)
    return false;
  if (zc0 < nearZ) {
    float t = (nearZ - zc0) / (zc1 - zc0);
    xc0 += (xc1 - xc0) * t;
    yc0 += (yc1 - yc0) * t;
    zc0 = nearZ;
  } else if (zc1 < nearZ) {
    float t = (nearZ - zc1) / (zc0 - zc1);
    xc1 += (xc0 - xc1) * t;
    yc1 += (yc0 - yc1) * t;
    zc1 = nearZ;
  }

  const float fovY = g_FovDeg * (3.14159265f / 180.0f);
  float tanHalfFovY = std::tan(fovY * 0.5f);
  float tanHalfFovX = tanHalfFovY * (screenW / screenH);

  auto proj = [&](float xc, float yc, float zc, float &sx, float &sy) {
    float ndcX = xc / (zc * tanHalfFovX);
    float ndcY = yc / (zc * tanHalfFovY);
    sx = (ndcX + 1.0f) * 0.5f * screenW;
    sy = (1.0f - ndcY) * 0.5f * screenH;
  };

  proj(xc0, yc0, zc0, sx0, sy0);
  proj(xc1, yc1, zc1, sx1, sy1);

  if ((sx0 < -screenW && sx1 < -screenW) ||
      (sx0 > screenW * 2.0f && sx1 > screenW * 2.0f) ||
      (sy0 < -screenH && sy1 < -screenH) ||
      (sy0 > screenH * 2.0f && sy1 > screenH * 2.0f))
    return false;

  return true;
}

namespace {

struct NativeMsvcString {
  union {
    char buf[16];
    const char *ptr;
  } bx;
  size_t size;
  size_t cap;
};

struct HashedStringRaw {
  uint64_t hash;
  NativeMsvcString str;
  const void *lastMatch;
};

struct ResourceLocationRaw {
  uint64_t type;
  NativeMsvcString path;
  uint64_t pathHash;
  uint64_t fullHash;
};

static uint64_t ComputeFnv1a64(const char *s, size_t len) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(s[i]);
    h *= 0x100000001b3ULL;
  }
  return h;
}

static void InitNativeMsvcString(NativeMsvcString &dst, const std::string &src,
                                 std::string &backing) {
  backing = src;
  dst.size = backing.size();
  if (backing.size() < 16) {
    std::memset(dst.bx.buf, 0, sizeof(dst.bx.buf));
    std::memcpy(dst.bx.buf, backing.data(), backing.size());
    dst.cap = 15;
  } else {
    dst.bx.ptr = backing.c_str();
    dst.cap = backing.size();
  }
}

using FnLookupByName = void *(__fastcall *)(void *lookupMap, void *outWeakPtr,
                                            const HashedStringRaw *name,
                                            bool logMissing);
using FnBlockTessCtor = void *(__fastcall *)(void *bt, void *blockSource);
using FnBlockTessPrepare = void(__fastcall *)(void *bt,
                                              const BlockPos3i *origin);
using FnBlockTessSetupCache = void(__fastcall *)(void *bt,
                                                 const BlockPos3i *origin);
using FnBlockTessInWorld = bool(__fastcall *)(void *bt, void *tess,
                                              const void *block,
                                              const BlockPos3i *pos,
                                              bool checkCull);
using FnBlockTessExtra = bool(__fastcall *)(void *bt, void *tess,
                                            const void *block,
                                            const BlockPos3i *pos,
                                            BlockPos3i *outPos);
using FnTessCtor = void *(__fastcall *)(void *tess, const void *weakMeshCtx);
using FnTexGroupGetTex = void *(__fastcall *)(void *texGroupPlus18,
                                              void *outTexPtr,
                                              const ResourceLocationRaw *loc,
                                              bool forceReload, void *a5,
                                              void *a6);

static bool g_NativeAttempted = false;
static bool g_NativeReady = false;
static FnLookupByName g_LookupByName = nullptr;
static void *g_BlockLookupMap = nullptr;
static FnBlockTessCtor g_BlockTessCtor = nullptr;
static FnBlockTessPrepare g_BlockTessPrepare = nullptr;
static FnBlockTessSetupCache g_BlockTessSetupCache = nullptr;
static FnBlockTessInWorld g_BlockTessInWorld = nullptr;
static FnBlockTessExtra g_BlockTessExtra = nullptr;
static FnTessCtor g_TessCtor = nullptr;
static FnTexGroupGetTex g_TexGroupGetTex = nullptr;
static void **g_BgfxCtxPtr = nullptr;

static std::mutex g_NativeTessMutex;
static std::vector<uint8_t> g_StandaloneBtBuf;
static std::vector<uint8_t> g_StandaloneTessBuf;
static bool g_StandaloneBtInit = false;
static bool g_StandaloneTessInit = false;

static uintptr_t ResolveRipDisp(uintptr_t instrAddr, int dispOffset,
                                int instrLen) {
  if (!IsValidReadablePtr(reinterpret_cast<void *>(instrAddr)))
    return 0;
  int32_t disp = *reinterpret_cast<const int32_t *>(instrAddr + dispOffset);
  return instrAddr + instrLen + static_cast<intptr_t>(disp);
}

static void EnsureNativeSigs() {
  if (g_NativeAttempted)
    return;
  g_NativeAttempted = true;

  g_LookupByName =
      reinterpret_cast<FnLookupByName>(GET_SIG("BlockTypeRegistry::lookupByName"));
  uintptr_t mapRef =
      reinterpret_cast<uintptr_t>(GET_SIG("BlockTypeRegistry::mMap_ref"));
  if (mapRef)
    g_BlockLookupMap = reinterpret_cast<void *>(ResolveRipDisp(mapRef, 3, 7));

  g_BlockTessCtor =
      reinterpret_cast<FnBlockTessCtor>(GET_SIG("BlockTessellator::ctor"));
  g_BlockTessPrepare =
      reinterpret_cast<FnBlockTessPrepare>(GET_SIG("BlockTessellator::prepare"));
  g_BlockTessSetupCache = reinterpret_cast<FnBlockTessSetupCache>(
      GET_SIG("BlockTessellator::setupCache"));
  g_BlockTessInWorld = reinterpret_cast<FnBlockTessInWorld>(
      GET_SIG("BlockTessellator::tessellateInWorld"));
  g_BlockTessExtra = reinterpret_cast<FnBlockTessExtra>(
      GET_SIG("BlockTessellator::tessellateExtra"));
  g_TessCtor = reinterpret_cast<FnTessCtor>(GET_SIG("Tessellator::ctor"));
  g_TexGroupGetTex =
      reinterpret_cast<FnTexGroupGetTex>(GET_SIG("TextureGroup::getTexture"));

  uintptr_t bgfxRef =
      reinterpret_cast<uintptr_t>(GET_SIG("bgfx_s_ctx_ref"));
  if (bgfxRef)
    g_BgfxCtxPtr = reinterpret_cast<void **>(ResolveRipDisp(bgfxRef, 3, 7));

  g_NativeReady = (g_LookupByName && g_BlockLookupMap && g_BlockTessCtor &&
                   g_BlockTessPrepare && g_BlockTessSetupCache &&
                   g_BlockTessInWorld && g_TessCtor && g_TessBegin);

  Logger::info(
      g_NativeReady
          ? "Native FakeWorld + BlockTessellator hooks resolved."
          : "Native FakeWorld + BlockTessellator hooks incomplete.",
      "BedrockGame");
}

static const void *LookupLegacySeh(const HashedStringRaw *hs) {
  void *weakOut[2] = {nullptr, nullptr};
  __try {
    g_LookupByName(g_BlockLookupMap, weakOut, hs, false);
    return weakOut[0];
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

static bool ReadLegacyPermsSeh(const void *legacy, const void **outDef,
                               const void *const **outBegin,
                               const void *const **outEnd) {
  __try {
    uintptr_t p = reinterpret_cast<uintptr_t>(legacy);
    *outDef = *reinterpret_cast<const void *const *>(p + 0x240);
    *outBegin = *reinterpret_cast<const void *const *const *>(p + 0x228);
    *outEnd = *reinterpret_cast<const void *const *const *>(p + 0x230);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static const void *ReadPtrArrayElemSeh(const void *const *arr, size_t idx) {
  __try {
    return arr[idx];
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

static const void *ResolveRuntimeBlock(
    const std::string &name,
    const std::vector<Bedrockmetica::BlockStateProp> &props) {
  if (!g_LookupByName || !g_BlockLookupMap || name.empty())
    return nullptr;

  std::string fullName = name;
  if (fullName.find(':') == std::string::npos)
    fullName = "minecraft:" + fullName;

  std::string backing;
  HashedStringRaw hs{};
  InitNativeMsvcString(hs.str, fullName, backing);
  hs.hash = ComputeFnv1a64(backing.data(), backing.size());
  hs.lastMatch = nullptr;

  const void *legacy = LookupLegacySeh(&hs);
  if (!IsValidReadablePtr(legacy))
    return nullptr;

  const void *defBlock = nullptr;
  const void *const *permBegin = nullptr;
  const void *const *permEnd = nullptr;
  if (!ReadLegacyPermsSeh(legacy, &defBlock, &permBegin, &permEnd))
    return nullptr;

  if (props.empty()) {
    if (IsValidReadablePtr(defBlock))
      return defBlock;
    if (IsValidReadablePtr(permBegin) && permBegin < permEnd) {
      const void *first = ReadPtrArrayElemSeh(permBegin, 0);
      if (IsValidReadablePtr(first))
        return first;
    }
    return nullptr;
  }

  if (!IsValidReadablePtr(permBegin) || !IsValidReadablePtr(permEnd) ||
      permEnd <= permBegin) {
    return IsValidReadablePtr(defBlock) ? defBlock : nullptr;
  }

  size_t count = static_cast<size_t>(permEnd - permBegin);
  if (count > 512)
    count = 512;

  const void *bestBlock = IsValidReadablePtr(defBlock) ? defBlock : nullptr;
  int bestScore = -1;

  for (size_t i = 0; i < count; ++i) {
    const void *cand = ReadPtrArrayElemSeh(permBegin, i);
    if (!IsValidReadablePtr(cand))
      continue;

    std::string candName;
    std::vector<Bedrockmetica::BlockStateProp> candProps;
    if (!ReadBlockSerialization(cand, candName, candProps))
      continue;

    int score = 0;
    for (const auto &want : props) {
      for (const auto &have : candProps) {
        if (have.key != want.key || have.type != want.type)
          continue;
        bool eq = false;
        if (want.type == Bedrockmetica::BlockStateType::String)
          eq = (have.strVal == want.strVal);
        else
          eq = (have.intVal == want.intVal);
        if (eq)
          score += 2;
        else
          score -= 1;
        break;
      }
    }

    if (score > bestScore) {
      bestScore = score;
      bestBlock = cand;
      if (score == static_cast<int>(props.size()) * 2)
        break;
    }
  }

  return bestBlock;
}

struct FakeWorldContext {
  int32_t width = 0;
  int32_t height = 0;
  int32_t depth = 0;
  const void *airBlock = nullptr;
  std::vector<const void *> grid;
};

static thread_local const FakeWorldContext *tls_FakeWorld = nullptr;

static const void *__fastcall FakeBsGetBlockLayer(void *self,
                                                  const BlockPos3i *pos,
                                                  uint32_t layer) {
  (void)self;
  const FakeWorldContext *fw = tls_FakeWorld;
  if (!fw)
    return nullptr;
  if (layer != 0 || !pos)
    return fw->airBlock;
  if (pos->x < 0 || pos->y < 0 || pos->z < 0 || pos->x >= fw->width ||
      pos->y >= fw->height || pos->z >= fw->depth)
    return fw->airBlock;
  size_t idx =
      (static_cast<size_t>(pos->y) * static_cast<size_t>(fw->depth) +
       static_cast<size_t>(pos->z)) *
          static_cast<size_t>(fw->width) +
      static_cast<size_t>(pos->x);
  const void *b = fw->grid[idx];
  return b ? b : fw->airBlock;
}

static const void *__fastcall FakeBsGetBlock(void *self, const BlockPos3i *pos) {
  return FakeBsGetBlockLayer(self, pos, 0);
}

static const void *__fastcall FakeBsGetExtraBlock(void *self,
                                                  const BlockPos3i *pos) {
  (void)self;
  (void)pos;
  const FakeWorldContext *fw = tls_FakeWorld;
  return fw ? fw->airBlock : nullptr;
}

static uint8_t GetBlockTerrainLayer(const void *blk) {
  if (!IsValidReadablePtr(blk))
    return 0;
  __try {
    const void *legacy = *reinterpret_cast<const void *const *>(
        reinterpret_cast<uintptr_t>(blk) + 0x68);
    if (!IsValidReadablePtr(legacy))
      return 0;
    return *reinterpret_cast<const uint8_t *>(
        reinterpret_cast<uintptr_t>(legacy) + 0x162);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

static bool InitStandaloneTessellatorSeh(void *buf) {
  uintptr_t emptyWeak[2] = {0, 0};
  __try {
    g_TessCtor(buf, emptyWeak);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool InitStandaloneTessellator() {
  if (g_StandaloneTessInit)
    return true;
  if (!g_TessCtor)
    return false;
  g_StandaloneTessBuf.assign(0x2E0, 0);
  if (!InitStandaloneTessellatorSeh(g_StandaloneTessBuf.data()))
    return false;
  g_StandaloneTessInit = true;
  return true;
}

static bool InitStandaloneBlockTessCtorSeh(void *buf, void *fakeBs) {
  __try {
    g_BlockTessCtor(buf, fakeBs);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool InitStandaloneBlockTessellator(void *fakeBs) {
  if (!g_BlockTessCtor)
    return false;
  if (!g_StandaloneBtInit) {
    g_StandaloneBtBuf.assign(0x48000, 0);
    if (!InitStandaloneBlockTessCtorSeh(g_StandaloneBtBuf.data(), fakeBs))
      return false;
    g_StandaloneBtInit = true;
  } else {
    *reinterpret_cast<void **>(g_StandaloneBtBuf.data() + 0x8) = fakeBs;
    *reinterpret_cast<void **>(g_StandaloneBtBuf.data() + 0x6C8) = fakeBs;
  }
  return true;
}

static bool PrepareSubchunkSeh(void *bt, const BlockPos3i *origin) {
  __try {
    g_BlockTessPrepare(bt, origin);
    g_BlockTessSetupCache(bt, origin);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool TessBeginSeh(void *tess) {
  __try {
    g_TessBegin(tess, 0, 1, 0x2EE0, true);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static void TessBlockSeh(void *bt, void *tess, const void *blk,
                         const BlockPos3i *pos, bool extraPass) {
  __try {
    if (extraPass && g_BlockTessExtra) {
      BlockPos3i tmpPos = *pos;
      g_BlockTessExtra(bt, tess, blk, pos, &tmpPos);
    } else {
      g_BlockTessInWorld(bt, tess, blk, pos, true);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

static void ExtractTessQuads(void *tess, float cx, float cy, float cz,
                             std::vector<NativeQuadVertex> &outTris) {
  uintptr_t t = reinterpret_cast<uintptr_t>(tess);
  const float *posBegin = *reinterpret_cast<const float *const *>(t + 0x10);
  const float *posEnd = *reinterpret_cast<const float *const *>(t + 0x18);
  if (!IsValidReadablePtr(posBegin) || !IsValidReadablePtr(posEnd) ||
      posEnd <= posBegin)
    return;

  size_t vertCount = static_cast<size_t>(posEnd - posBegin) / 3;
  if (vertCount < 4)
    return;

  uint8_t hasColor = *reinterpret_cast<const uint8_t *>(t + 0x1B4);
  uint8_t hasUv0 = *reinterpret_cast<const uint8_t *>(t + 0x194);
  const uint32_t *colBegin =
      hasColor ? *reinterpret_cast<const uint32_t *const *>(t + 0x70) : nullptr;
  const float *uvBegin =
      hasUv0 ? *reinterpret_cast<const float *const *>(t + 0xA0) : nullptr;

  auto mkV = [&](size_t vi) -> NativeQuadVertex {
    NativeQuadVertex v{};
    v.x = posBegin[vi * 3 + 0] - cx;
    v.y = posBegin[vi * 3 + 1] - cy;
    v.z = posBegin[vi * 3 + 2] - cz;
    if (uvBegin) {
      v.u = uvBegin[vi * 2 + 0];
      v.v = uvBegin[vi * 2 + 1];
    } else {
      v.u = -1.0f;
      v.v = 0.0f;
    }
    v.abgr = colBegin ? colBegin[vi] : 0xFFFFFFFFu;
    return v;
  };

  size_t quadCount = vertCount / 4;
  outTris.reserve(outTris.size() + quadCount * 6);
  for (size_t q = 0; q < quadCount; ++q) {
    size_t b = q * 4;
    NativeQuadVertex v0 = mkV(b + 0);
    NativeQuadVertex v1 = mkV(b + 1);
    NativeQuadVertex v2 = mkV(b + 2);
    NativeQuadVertex v3 = mkV(b + 3);
    outTris.push_back(v0);
    outTris.push_back(v1);
    outTris.push_back(v2);
    outTris.push_back(v0);
    outTris.push_back(v2);
    outTris.push_back(v3);
  }
}

static bool TessellateFakeWorldPass(void *bt, void *tess,
                                    const FakeWorldContext &fw,
                                    NativePreviewMesh &outMesh) {
  float cx = static_cast<float>(fw.width) * 0.5f;
  float cy = static_cast<float>(fw.height) * 0.5f;
  float cz = static_cast<float>(fw.depth) * 0.5f;

  static const uint8_t kLayers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  for (int32_t sy = 0; sy < fw.height; sy += 16) {
    int32_t ey = (sy + 16 < fw.height) ? (sy + 16) : fw.height;
    for (int32_t sz = 0; sz < fw.depth; sz += 16) {
      int32_t ez = (sz + 16 < fw.depth) ? (sz + 16) : fw.depth;
      for (int32_t sx = 0; sx < fw.width; sx += 16) {
        int32_t ex = (sx + 16 < fw.width) ? (sx + 16) : fw.width;

        BlockPos3i origin{sx, sy, sz};
        if (!PrepareSubchunkSeh(bt, &origin))
          return false;

        uintptr_t btAddr = reinterpret_cast<uintptr_t>(bt);
        *reinterpret_cast<int32_t *>(btAddr + 0xEC) = 0;
        *reinterpret_cast<uint8_t *>(btAddr + 0x478F8) = 1;
        float *constColor = reinterpret_cast<float *>(btAddr + 0x478E8);
        constColor[0] = 1.0f;
        constColor[1] = 1.0f;
        constColor[2] = 1.0f;
        constColor[3] = 1.0f;

        for (uint8_t layer : kLayers) {
          *reinterpret_cast<int32_t *>(btAddr + 0x80) =
              static_cast<int32_t>(layer);
          *reinterpret_cast<uint8_t *>(btAddr + 0x478FC) =
              (layer == 0) ? 1 : 0;

          if (!TessBeginSeh(tess))
            return false;

          float *tessOff =
              reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1CC);
          tessOff[0] = 0.0f;
          tessOff[1] = 0.0f;
          tessOff[2] = 0.0f;
          float *tessScale =
              reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(tess) + 0x1D8);
          tessScale[0] = 1.0f;
          tessScale[1] = 1.0f;
          tessScale[2] = 1.0f;

          for (int32_t y = sy; y < ey; ++y) {
            for (int32_t z = sz; z < ez; ++z) {
              for (int32_t x = sx; x < ex; ++x) {
                size_t idx =
                    (static_cast<size_t>(y) * static_cast<size_t>(fw.depth) +
                     static_cast<size_t>(z)) *
                        static_cast<size_t>(fw.width) +
                    static_cast<size_t>(x);
                const void *blk = fw.grid[idx];
                if (!blk || blk == fw.airBlock)
                  continue;
                BlockPos3i pos{x, y, z};
                TessBlockSeh(bt, tess, blk, &pos, layer == 5);
              }
            }
          }

          bool isTrans = (layer == 5 || layer == 6 || layer == 8 || layer == 9);
          ExtractTessQuads(tess, cx, cy, cz,
                           isTrans ? outMesh.transTris : outMesh.opaqueTris);

          if (outMesh.opaqueTris.size() + outMesh.transTris.size() >= 900000)
            return true;
        }
      }
    }
  }
  return true;
}

static bool CloneBlockSourceSeh(void *realBs, uint8_t *fakeBs, void **fakeVt,
                                size_t vtCount) {
  __try {
    std::memcpy(fakeBs, realBs, 0x240);
    void **realVt = *reinterpret_cast<void ***>(realBs);
    if (!IsValidReadablePtr(realVt))
      return false;
    std::memcpy(fakeVt, realVt, vtCount * sizeof(void *));
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static void *FindTextureGroupFromClientSeh(void *ci) {
  if (!IsValidReadablePtr(ci) || !g_TexGroupGetTex)
    return nullptr;

  __try {
    auto checkCand = [](void *cand) -> void * {
      if (!IsValidReadablePtr(cand))
        return nullptr;
      void *tgPlus18 =
          reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(cand) + 0x18);
      if (!IsValidReadablePtr(tgPlus18))
        return nullptr;
      uintptr_t *vt = *reinterpret_cast<uintptr_t **>(tgPlus18);
      if (!IsValidReadablePtr(vt))
        return nullptr;
      if (vt[2] == reinterpret_cast<uintptr_t>(g_TexGroupGetTex))
        return tgPlus18;
      return nullptr;
    };

    void *direct =
        *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(ci) + 0x6C0);
    if (void *tg = checkCand(direct))
      return tg;

    for (uintptr_t off = 0x100; off <= 0x900; off += 8) {
      void *cand =
          *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(ci) + off);
      if (void *tg = checkCand(cand))
        return tg;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return nullptr;
}

static uint16_t ResolveAtlasHandleSeh(void *tgPlus18,
                                      const ResourceLocationRaw *loc) {
  __try {
    uintptr_t texPtrOut[4] = {};
    g_TexGroupGetTex(tgPlus18, texPtrOut, loc, false, nullptr, nullptr);
    uintptr_t bedrockTex = texPtrOut[0];
    if (!IsValidReadablePtr(reinterpret_cast<void *>(bedrockTex)))
      return 0xFFFF;

    uintptr_t *vecBegin = *reinterpret_cast<uintptr_t **>(bedrockTex + 0x38);
    uintptr_t *vecEnd = *reinterpret_cast<uintptr_t **>(bedrockTex + 0x40);
    if (!IsValidReadablePtr(vecBegin) || !IsValidReadablePtr(vecEnd) ||
        vecEnd <= vecBegin)
      return 0xFFFF;

    uintptr_t *clientTexPtr = reinterpret_cast<uintptr_t *>(vecBegin[0]);
    if (!IsValidReadablePtr(clientTexPtr))
      return 0xFFFF;
    uintptr_t clientTex = clientTexPtr[0];
    if (!IsValidReadablePtr(reinterpret_cast<void *>(clientTex)))
      return 0xFFFF;

    return *reinterpret_cast<const uint16_t *>(clientTex + 0xAA);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0xFFFF;
  }
}

static void *LookupBgfxTextureD3D12Seh(void *sCtx, uint16_t handle,
                                       bool useStride80) {
  __try {
    uintptr_t renderCtx = *reinterpret_cast<const uintptr_t *>(
        reinterpret_cast<uintptr_t>(sCtx) + 0x19F58);
    if (!IsValidReadablePtr(reinterpret_cast<void *>(renderCtx)))
      return nullptr;

    uintptr_t addr = useStride80
                         ? (renderCtx + 0x9B2178 + static_cast<size_t>(handle) * 0x80)
                         : (renderCtx + 0x93A178 + static_cast<size_t>(handle) * 0x78);
    void *res = *reinterpret_cast<void *const *>(addr);
    if (IsValidReadablePtr(res))
      return res;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return nullptr;
}

}

bool BuildNativePreviewMesh(const Bedrockmetica::SchematicDocument &doc,
                            NativePreviewMesh &outMesh) {
  outMesh.opaqueTris.clear();
  outMesh.transTris.clear();
  outMesh.valid = false;

  EnsureNativeSigs();
  if (!g_NativeReady) {
    Logger::info("Native FakeWorld: g_NativeReady is false.", "BedrockGame");
    return false;
  }

  void *realBs = GetBlockSource();
  if (!IsValidReadablePtr(realBs)) {
    Logger::info("Native FakeWorld: GetBlockSource invalid.", "BedrockGame");
    return false;
  }
  if (doc.width <= 0 || doc.height <= 0 || doc.depth <= 0 ||
      doc.palette.empty() || doc.parts.empty()) {
    Logger::info("Native FakeWorld: empty doc dimensions/palette/parts.",
                 "BedrockGame");
    return false;
  }

  std::lock_guard<std::mutex> lock(g_NativeTessMutex);
  Logger::info("Native FakeWorld: resolving air block...", "BedrockGame");

  const void *airBlock = ResolveRuntimeBlock("minecraft:air", {});
  if (!airBlock) {
    Logger::info("Native FakeWorld: failed to resolve minecraft:air.",
                 "BedrockGame");
    return false;
  }

  Logger::info("Native FakeWorld: resolving palette...", "BedrockGame");
  std::vector<const void *> runtimePal(doc.palette.size(), nullptr);
  bool anyResolved = false;

  for (size_t i = 1; i < doc.palette.size(); ++i) {
    const auto &entry = doc.palette[i];
    if (entry.name.empty() || entry.name == "minecraft:air" ||
        entry.name == "air")
      continue;
    const void *blk = ResolveRuntimeBlock(entry.name, entry.states);
    if (!blk || blk == airBlock)
      continue;
    runtimePal[i] = blk;
    anyResolved = true;
  }

  if (!anyResolved) {
    outMesh.valid = true;
    return true;
  }

  FakeWorldContext fw{};
  fw.width = doc.width;
  fw.height = doc.height;
  fw.depth = doc.depth;
  fw.airBlock = airBlock;
  size_t total = static_cast<size_t>(doc.width) *
                 static_cast<size_t>(doc.height) *
                 static_cast<size_t>(doc.depth);
  fw.grid.assign(total, airBlock);

  for (const auto &part : doc.parts) {
    if (!part.visible)
      continue;
    for (const auto &kv : part.subchunks) {
      const auto &sc = kv.second;
      int32_t baseX = part.offsetX + sc.cx * 16;
      int32_t baseY = part.offsetY + sc.cy * 16;
      int32_t baseZ = part.offsetZ + sc.cz * 16;
      for (int32_t sy = 0; sy < 16; ++sy) {
        int32_t wy = baseY + sy;
        if (wy < 0 || wy >= fw.height)
          continue;
        for (int32_t sz = 0; sz < 16; ++sz) {
          int32_t wz = baseZ + sz;
          if (wz < 0 || wz >= fw.depth)
            continue;
          for (int32_t sx = 0; sx < 16; ++sx) {
            int32_t wx = baseX + sx;
            if (wx < 0 || wx >= fw.width)
              continue;
            uint16_t pid = sc.blocks[(sy << 8) | (sz << 4) | sx];
            if (pid > 0 && pid < runtimePal.size() && runtimePal[pid]) {
              size_t gIdx =
                  (static_cast<size_t>(wy) * static_cast<size_t>(fw.depth) +
                   static_cast<size_t>(wz)) *
                      static_cast<size_t>(fw.width) +
                  static_cast<size_t>(wx);
              fw.grid[gIdx] = runtimePal[pid];
            }
          }
        }
      }
    }
  }

  Logger::info("Native FakeWorld: cloning BlockSource...", "BedrockGame");
  alignas(16) uint8_t fakeBs[0x280] = {};
  void *fakeVt[160] = {};
  if (!CloneBlockSourceSeh(realBs, fakeBs, fakeVt, 160)) {
    Logger::info("Native FakeWorld: CloneBlockSourceSeh failed.", "BedrockGame");
    return false;
  }

  fakeVt[1] = reinterpret_cast<void *>(&FakeBsGetBlockLayer);
  fakeVt[2] = reinterpret_cast<void *>(&FakeBsGetBlock);
  fakeVt[4] = reinterpret_cast<void *>(&FakeBsGetExtraBlock);
  *reinterpret_cast<void ***>(fakeBs) = fakeVt;

  Logger::info("Native FakeWorld: init standalone Tessellator...", "BedrockGame");
  if (!InitStandaloneTessellator()) {
    Logger::warn("Native FakeWorld: Tessellator init failed.", "BedrockGame");
    return false;
  }
  Logger::info("Native FakeWorld: init standalone BlockTessellator...",
               "BedrockGame");
  if (!InitStandaloneBlockTessellator(fakeBs)) {
    Logger::warn("Native FakeWorld: BlockTessellator init failed.",
                 "BedrockGame");
    return false;
  }

  Logger::info("Native FakeWorld: starting tessellation pass...", "BedrockGame");
  tls_FakeWorld = &fw;
  bool ok = TessellateFakeWorldPass(g_StandaloneBtBuf.data(),
                                    g_StandaloneTessBuf.data(), fw, outMesh);
  tls_FakeWorld = nullptr;

  outMesh.valid = ok && (!outMesh.opaqueTris.empty() || !outMesh.transTris.empty());
  Logger::info("Native FakeWorld: tessellation finished.", "BedrockGame");
  return outMesh.valid;
}

void *GetLiveTerrainAtlasD3D12Resource() {
  EnsureNativeSigs();
  if (!g_BgfxCtxPtr || !IsValidReadablePtr(g_BgfxCtxPtr))
    return nullptr;

  void *sCtx = *g_BgfxCtxPtr;
  if (!IsValidReadablePtr(sCtx))
    return nullptr;

  void *ci = GetClientInstance();
  void *tgPlus18 = FindTextureGroupFromClientSeh(ci);
  if (!tgPlus18 || !g_TexGroupGetTex) {
    Logger::info("LiveAtlas: TextureGroup or getTexture missing.", "BedrockGame");
    return nullptr;
  }

  std::string backing;
  ResourceLocationRaw loc{};
  loc.type = 0;
  InitNativeMsvcString(loc.path, "atlas.terrain", backing);
  loc.pathHash = ComputeFnv1a64(backing.data(), backing.size());
  loc.fullHash = loc.pathHash;

  Logger::info("LiveAtlas: calling ResolveAtlasHandleSeh...", "BedrockGame");
  uint16_t handle = ResolveAtlasHandleSeh(tgPlus18, &loc);
  if (handle == 0xFFFF || handle >= 4096) {
    Logger::info("LiveAtlas: invalid handle resolved.", "BedrockGame");
    return nullptr;
  }

  Logger::info("LiveAtlas: looking up D3D12 resource...", "BedrockGame");
  void *res = LookupBgfxTextureD3D12Seh(sCtx, handle, false);
  if (!res)
    res = LookupBgfxTextureD3D12Seh(sCtx, handle, true);
  Logger::info(res ? "LiveAtlas: resource found." : "LiveAtlas: resource null.",
               "BedrockGame");
  return res;
}

}
