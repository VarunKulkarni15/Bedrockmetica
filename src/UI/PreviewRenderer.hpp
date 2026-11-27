#pragma once

#include "UiAssets.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <string>

namespace PreviewRenderer {

void EnsureDX11(ID3D11Device *device);
void EnsureDX12(ID3D12Device *device, UiAssets::SrvAllocFn allocFn);
void Shutdown();

void SetStructurePath(const std::string &path, const std::string &format);
void Invalidate();

void RenderViewport(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize, float yaw,
                    float pitch, float zoom, float alpha);

void FlushRenderDX11(ID3D11DeviceContext *ctx);
void FlushRenderDX12(ID3D12GraphicsCommandList *cmd);

bool HasMesh();
int GetQuadCount();

}
