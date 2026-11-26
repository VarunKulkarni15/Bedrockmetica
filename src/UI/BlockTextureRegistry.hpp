#pragma once

#include "UiAssets.hpp"
#include <cstdint>
#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <string>

namespace BlockTextureRegistry {

struct BlockFaceUVs {
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  bool valid = false;
};

struct BlockFaces {
  BlockFaceUVs up{};
  BlockFaceUVs down{};
  BlockFaceUVs north{};
  BlockFaceUVs south{};
  BlockFaceUVs east{};
  BlockFaceUVs west{};
  uint32_t avgAbgr = 0xFF8C929Cu;
};

void EnsureDX11(ID3D11Device *device);
void EnsureDX12(ID3D12Device *device, ID3D12CommandQueue *queue,
                UiAssets::SrvAllocFn allocFn);
void Shutdown();

bool IsReady();
ImTextureID GetAtlasTexId();
const uint8_t *GetAtlasRgbaData();
constexpr int kAtlasSize = 1024;
constexpr int kTileSize = 16;
constexpr int kTilesPerRow = kAtlasSize / kTileSize;

BlockFaces ResolveBlockFaces(const std::string &rawBlockId);

}
