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

void DrawWireBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                   float maxZ, float r, float g, float b, float a);
void DrawFilledBox3D(float minX, float minY, float minZ, float maxX, float maxY,
                     float maxZ, float r, float g, float b, float a);

}
