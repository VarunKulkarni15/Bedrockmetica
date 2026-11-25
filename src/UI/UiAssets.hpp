#pragma once

#include <cstdint>
#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <string>

namespace UiAssets {

enum class PlateId {
    ObsidianWindow = 0,
    GoldHeader,
    BlueBanner,
    SlatePanel,
    SlateCard,
    GoldCard,
    GoldPill,
    EmeraldCTA,
    RedDanger,
    DarkInset,
    ToastSuccess,
    ToastInfo,
    ToastWarning,
    ToastError,
    Count
};

enum class BadgeId {
    Cube = 0,
    Check,
    Star,
    Alert,
    Info,
    Count
};

enum class ThumbId {
    Pagoda = 0,
    TradingHall,
    Bridge,
    Vault,
    Count
};

enum class BlockIconId {
    Generic = 0,
    StoneBricks,
    SprucePlanks,
    RedNetherBricks,
    Calcite,
    CherryLeaves,
    Lantern,
    GoldBlock,
    OakPlanks,
    OakFence,
    MossCarpet,
    GlassPane,
    Lectern,
    SeaLantern,
    Hopper,
    QuartzPillar,
    RedTerracotta,
    DarkOakStair,
    Glowstone,
    DeepslateTiles,
    RedstoneBlock,
    StickyPiston,
    Observer,
    CopperGrate,
    Count
};

using SrvAllocFn = void(*)(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);

void EnsureDX11(ID3D11Device* device);
void EnsureDX12(ID3D12Device* device, ID3D12CommandQueue* queue, SrvAllocFn allocFn);
void Shutdown();

bool AreTexturesReady();
ImTextureID GetPlateTex(PlateId id);
ImTextureID GetBadgeTex(BadgeId id);
ImTextureID GetThumbTex(ThumbId id);
ImTextureID GetBlockIconTex(BlockIconId id);
BlockIconId ResolveBlockIcon(const std::string& blockName);

constexpr int kPlateTexSize = 48;
constexpr float kPlateCornerPx = 12.0f;

}
