#include "BedrockGame.hpp"
#include "../Utils/SignatureAndOffsetManager.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <windows.h>

namespace BedrockGame {

namespace {

using TessBeginFn = void(__fastcall *)(void *tess, uint8_t mode,
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

bool IsLocalPlayerBareHanded() {
  void *ci = GetClientInstance();
  if (!IsValidReadablePtr(ci))
    return false;
  __try {
    int idx = GET_OFFSET("ClientInstance::getLocalPlayerVIdx");
    if (idx <= 0)
      idx = 173;
    void *player = CallVFunc<void *>(static_cast<size_t>(idx), ci);
    if (!IsValidReadablePtr(player))
      return false;

    int itemIdx = GET_OFFSET("Actor::getCarriedItemVIdx");
    if (itemIdx <= 0)
      itemIdx = 77;
    void *itemStack = CallVFunc<void *>(static_cast<size_t>(itemIdx), player);
    if (!IsValidReadablePtr(itemStack))
      return true;

    uintptr_t itemPtr =
        *reinterpret_cast<const uintptr_t *>(reinterpret_cast<uintptr_t>(itemStack) + 0x08);
    uint8_t count =
        *reinterpret_cast<const uint8_t *>(reinterpret_cast<uintptr_t>(itemStack) + 0x22);
    return (itemPtr == 0) || (count == 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
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
    ChunkPos2i cp{cx, cz};
    void *chunk =
        CallVFunc<void *>(static_cast<size_t>(idx), blockSource, &cp);
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

    g_TessBegin(tess, 4, 24, false);
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

    g_TessBegin(tess, 1, 24, false);
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

}
