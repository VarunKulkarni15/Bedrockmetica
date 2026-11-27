#pragma once

#include "../Core/SchematicDocument.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace BedrockGame {

struct BlockPos3i {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

struct Vec3f {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct HitInfo {
  int32_t type = 3;
  int8_t facing = -1;
  BlockPos3i blockPos{};
  bool isBlockHit() const { return type == 0; }
};

void Initialize();

void UpdateFromRenderLevel(void *levelRenderer, void *screenContext);
void UpdateFromLevelRendererPlayer(void *lrp, void *baseCtx, void *blockSource);

bool IsRenderReady();
void *GetClientInstance();
void *GetBlockSource();
void *GetScreenContext();
void *GetTessellator();
void *GetSelectionBoxMaterial();
void *GetSelectionOverlayMaterial();
Vec3f GetCameraOrigin();

bool IsLocalPlayerBareHanded();
bool GetAimedHit(HitInfo &outHit);
bool GetLocalPlayerPos(Vec3f &outPos);

bool IsChunkColumnLoaded(void *blockSource, int32_t cx, int32_t cz);
const void *GetBlockPtr(void *blockSource, int32_t x, int32_t y, int32_t z);
bool ReadBlockSerialization(const void *blockPtr, std::string &outName,
                            std::vector<Bedrockmetica::BlockStateProp> &outProps);

struct NativeQuadVertex {
  float x, y, z;
  float u, v;
  uint32_t abgr;
};

struct NativePreviewMesh {
  std::vector<NativeQuadVertex> opaqueTris;
  std::vector<NativeQuadVertex> transTris;
  bool valid = false;
};

bool BuildNativePreviewMesh(const Bedrockmetica::SchematicDocument &doc,
                            NativePreviewMesh &outMesh);
void *GetLiveTerrainAtlasD3D12Resource();

void DrawWireBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                   float maxZ, float r, float g, float b, float a);
void DrawFilledBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                     float maxZ, float r, float g, float b, float a);
void DrawThickBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                    float maxZ, float thickness, float r, float g, float b,
                    float a);
void UpdateCameraForward();
bool WorldToScreen(float wx, float wy, float wz, float screenW, float screenH,
                   float &outX, float &outY, float &outDist);
bool ProjectLine3D(float x0, float y0, float z0, float x1, float y1, float z1,
                   float screenW, float screenH, float &sx0, float &sy0,
                   float &sx1, float &sy1);

}
