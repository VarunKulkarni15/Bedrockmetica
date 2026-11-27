#include "PreviewRenderer.hpp"
#include "../Core/SchematicDocument.hpp"
#include "../IO/BmetCodec.hpp"
#include "../IO/McStructureCodec.hpp"
#include "../Utils/Logger.hpp"
#include "BlockTextureRegistry.hpp"
#include "GeneratedBlockModels.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3dcompiler.h>
#include <mutex>
#include <vector>

namespace PreviewRenderer {

namespace {

constexpr int kRtWidth = 896;
constexpr int kRtHeight = 576;

struct PreviewVertex {
  float x, y, z;
  float u, v;
  uint8_t r, g, b, a;
};

struct alignas(16) PreviewCB {
  float mvp[16];
};

static std::mutex g_Mutex;
static std::string g_LoadedPath;
static bool g_HasMesh = false;
static bool g_MeshDirty = false;
static bool g_RenderDirty = true;
static float g_MaxDim = 1.0f;

static std::vector<PreviewVertex> g_OpaqueVerts;
static std::vector<PreviewVertex> g_TransVerts;

static float g_Yaw = 0.72f;
static float g_Pitch = 0.56f;
static float g_Zoom = 1.0f;
static float g_Aspect = static_cast<float>(kRtWidth) / kRtHeight;
static bool g_WasAtlasReady = false;

static const char *kVsSrc = R"(
cbuffer CB : register(b0) {
  float4x4 mvp;
};
struct VSIn {
  float3 pos : POSITION;
  float2 uv  : TEXCOORD0;
  float4 col : COLOR0;
};
struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
  float4 col : COLOR0;
};
PSIn main(VSIn i) {
  PSIn o;
  o.pos = mul(mvp, float4(i.pos, 1.0f));
  o.uv  = i.uv;
  o.col = i.col;
  return o;
}
)";

static const char *kPsSrc = R"(
Texture2D atlasTex : register(t0);
SamplerState smp   : register(s0);
struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
  float4 col : COLOR0;
};
float4 main(PSIn i) : SV_Target {
  float4 tex = (i.uv.x >= 0.0f) ? atlasTex.Sample(smp, i.uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
  clip(tex.a - 0.12f);
  float outA = (i.col.a >= 0.99f) ? 1.0f : (tex.a * i.col.a);
  return float4(tex.rgb * i.col.rgb, outA);
}
)";

static bool g_IsDx11 = false;
static bool g_IsDx12 = false;
static bool g_GpuReady = false;

static ID3D11Device *g_Dx11Dev = nullptr;
static ID3D11Texture2D *g_Dx11ColorTex = nullptr;
static ID3D11RenderTargetView *g_Dx11Rtv = nullptr;
static ID3D11ShaderResourceView *g_Dx11Srv = nullptr;
static ID3D11Texture2D *g_Dx11DepthTex = nullptr;
static ID3D11DepthStencilView *g_Dx11Dsv = nullptr;
static ID3D11VertexShader *g_Dx11Vs = nullptr;
static ID3D11PixelShader *g_Dx11Ps = nullptr;
static ID3D11InputLayout *g_Dx11Layout = nullptr;
static ID3D11Buffer *g_Dx11Cb = nullptr;
static ID3D11SamplerState *g_Dx11Sampler = nullptr;
static ID3D11RasterizerState *g_Dx11Rast = nullptr;
static ID3D11DepthStencilState *g_Dx11DssOpaque = nullptr;
static ID3D11DepthStencilState *g_Dx11DssTrans = nullptr;
static ID3D11BlendState *g_Dx11BlendOpaque = nullptr;
static ID3D11BlendState *g_Dx11BlendTrans = nullptr;
static ID3D11Buffer *g_Dx11VbOpaque = nullptr;
static ID3D11Buffer *g_Dx11VbTrans = nullptr;
static UINT g_Dx11OpaqueCount = 0;
static UINT g_Dx11TransCount = 0;

static ID3D12Device *g_Dx12Dev = nullptr;
static ID3D12Resource *g_Dx12ColorTex = nullptr;
static ID3D12Resource *g_Dx12DepthTex = nullptr;
static ID3D12DescriptorHeap *g_Dx12RtvHeap = nullptr;
static ID3D12DescriptorHeap *g_Dx12DsvHeap = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_Dx12Rtv{};
static D3D12_CPU_DESCRIPTOR_HANDLE g_Dx12Dsv{};
static ID3D12RootSignature *g_Dx12RootSig = nullptr;
static ID3D12PipelineState *g_Dx12PsoOpaque = nullptr;
static ID3D12PipelineState *g_Dx12PsoTrans = nullptr;
static ID3D12Resource *g_Dx12VbOpaque = nullptr;
static ID3D12Resource *g_Dx12VbTrans = nullptr;
static D3D12_VERTEX_BUFFER_VIEW g_Dx12VbvOpaque{};
static D3D12_VERTEX_BUFFER_VIEW g_Dx12VbvTrans{};
static UINT g_Dx12OpaqueCount = 0;
static UINT g_Dx12TransCount = 0;
static std::vector<ID3D12Resource *> g_Dx12RetiredVbs;

static ImTextureID g_PreviewTexId = 0;

void ComputeMvpMatrix(float *outM16) {
  float fovY = 42.0f * (3.14159265f / 180.0f);
  float tanHalf = std::tan(fovY * 0.5f);
  float aspect = std::max(0.25f, g_Aspect);
  float nearZ = 0.25f;
  float farZ = std::max(400.0f, g_MaxDim * 12.0f);

  float dist =
      std::max(6.0f, g_MaxDim * 1.78f) / std::clamp(g_Zoom, 0.35f, 3.0f);
  float cp = std::cos(g_Pitch);
  float sp = std::sin(g_Pitch);
  float cy = std::cos(g_Yaw);
  float sy = std::sin(g_Yaw);

  float eyeX = dist * cp * sy;
  float eyeY = dist * sp;
  float eyeZ = dist * cp * cy;

  float fx = -eyeX;
  float fy = -eyeY;
  float fz = -eyeZ;
  float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (fLen > 0.0001f) {
    fx /= fLen;
    fy /= fLen;
    fz /= fLen;
  }

  float rx = fy * 0.0f - fz * 1.0f;
  float ry = fz * 0.0f - fx * 0.0f;
  float rz = fx * 1.0f - fy * 0.0f;
  float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
  if (rLen > 0.0001f) {
    rx /= rLen;
    ry /= rLen;
    rz /= rLen;
  } else {
    rx = 1.0f;
    ry = 0.0f;
    rz = 0.0f;
  }

  float ux = ry * fz - rz * fy;
  float uy = rz * fx - rx * fz;
  float uz = rx * fy - ry * fx;

  float view[16] = {
      rx,   ry,   rz,   -(rx * eyeX + ry * eyeY + rz * eyeZ),
      ux,   uy,   uz,   -(ux * eyeX + uy * eyeY + uz * eyeZ),
      fx,   fy,   fz,   -(fx * eyeX + fy * eyeY + fz * eyeZ),
      0.0f, 0.0f, 0.0f, 1.0f,
  };

  float proj[16] = {0};
  proj[0] = 1.0f / (aspect * tanHalf);
  proj[5] = 1.0f / tanHalf;
  proj[10] = farZ / (farZ - nearZ);
  proj[11] = -(farZ * nearZ) / (farZ - nearZ);
  proj[14] = 1.0f;

  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += proj[row * 4 + k] * view[k * 4 + col];
      }
      outM16[col * 4 + row] = sum;
    }
  }
}

void BuildMeshFromDoc(const Bedrockmetica::SchematicDocument &doc) {
  g_OpaqueVerts.clear();
  g_TransVerts.clear();
  g_HasMesh = false;
  if (doc.width <= 0 || doc.height <= 0 || doc.depth <= 0 || doc.parts.empty())
    return;

  float halfW = static_cast<float>(doc.width) * 0.5f;
  float halfH = static_cast<float>(doc.height) * 0.5f;
  float halfD = static_cast<float>(doc.depth) * 0.5f;
  g_MaxDim =
      static_cast<float>(std::max({doc.width, doc.height, doc.depth, 4}));

  std::vector<BlockTextureRegistry::BlockFaces> palFaces(doc.palette.size());
  std::vector<int> palShape(doc.palette.size(), 0);
  std::vector<bool> palTrans(doc.palette.size(), false);
  std::vector<const GeneratedBlockModels::BakedBlockModel *> palBaked(
      doc.palette.size(), nullptr);
  std::vector<std::string> palShort(doc.palette.size());

  auto classifyShape = [](const std::string &name) -> int {
    if (name.find("light_block") != std::string::npos ||
        name.find("structure_void") != std::string::npos ||
        name.find("barrier") != std::string::npos)
      return -1;
    if (name.find("tall_grass") != std::string::npos ||
        name.find("short_grass") != std::string::npos ||
        name.find("tallgrass") != std::string::npos ||
        name.find("fern") != std::string::npos ||
        name.find("flower") != std::string::npos ||
        name.find("poppy") != std::string::npos ||
        name.find("dandelion") != std::string::npos ||
        name.find("azure_bluet") != std::string::npos ||
        name.find("oxeye_daisy") != std::string::npos ||
        name.find("cornflower") != std::string::npos ||
        name.find("allium") != std::string::npos ||
        name.find("blue_orchid") != std::string::npos ||
        name.find("peony") != std::string::npos ||
        name.find("rose_bush") != std::string::npos ||
        name.find("sapling") != std::string::npos ||
        name.find("sugar_cane") != std::string::npos ||
        name.find("reeds") != std::string::npos ||
        name.find("seagrass") != std::string::npos) {
      if (name.find("wildflowers") == std::string::npos &&
          name.find("cactus_flower") == std::string::npos &&
          name.find("flower_pot") == std::string::npos)
        return 1;
    }
    if (name.find("_slab") != std::string::npos &&
        name.find("double_slab") == std::string::npos)
      return 3;
    if (name.find("_wall") != std::string::npos)
      return 4;
    if (name.find("glass_pane") != std::string::npos ||
        name.find("iron_bars") != std::string::npos)
      return 5;
    if (name.find("banner") != std::string::npos)
      return 6;
    if (name.find("_stairs") != std::string::npos)
      return 7;
    if (name.find("wheat") != std::string::npos ||
        name.find("carrots") != std::string::npos ||
        name.find("potatoes") != std::string::npos ||
        name.find("beetroot") != std::string::npos ||
        name.find("nether_wart") != std::string::npos)
      return 8;
    if (name.find("_fence") != std::string::npos &&
        name.find("fence_gate") == std::string::npos)
      return 9;
    if (name.find("vine") != std::string::npos ||
        name.find("glow_lichen") != std::string::npos)
      return 11;
    return 0;
  };

  for (size_t i = 1; i < doc.palette.size(); ++i) {
    const std::string &bn = doc.palette[i].name;
    std::string sn = bn;
    if (sn.rfind("minecraft:", 0) == 0)
      sn = sn.substr(10);
    palShort[i] = sn;
    palFaces[i] = BlockTextureRegistry::ResolveBlockFaces(bn);
    palShape[i] = classifyShape(bn);
    if (palShape[i] == 0) {
      std::string jn = sn;
      if (jn == "grass_path")
        jn = "dirt_path";
      else if (jn == "waterlily")
        jn = "lily_pad";
      else if (jn == "snow_layer")
        jn = "snow";
      else if (jn == "chain")
        jn = "iron_chain";
      const auto *bm = GeneratedBlockModels::FindBlock(jn);
      if (!bm && jn == "iron_chain")
        bm = GeneratedBlockModels::FindBlock("chain");
      palBaked[i] = bm;
    }
    palTrans[i] = (bn.find("water") != std::string::npos &&
                   bn.find("waterlily") == std::string::npos) ||
                  (bn.find("glass") != std::string::npos) ||
                  (bn.find("ice") != std::string::npos);
  }

  auto getBlk = [&](int32_t x, int32_t y, int32_t z) -> uint16_t {
    if (x < 0 || y < 0 || z < 0 || x >= doc.width || y >= doc.height ||
        z >= doc.depth)
      return 0;
    return doc.GetBlock(0, x, y, z);
  };
  auto isOpaqueFull = [&](int32_t x, int32_t y, int32_t z) -> bool {
    uint16_t pid = getBlk(x, y, z);
    if (pid == 0 || pid >= palShape.size())
      return false;
    return palShape[pid] == 0 && palBaked[pid] == nullptr && !palTrans[pid] &&
           doc.palette[pid].isSolid;
  };

  auto computeAo = [&](int32_t bx, int32_t by, int32_t bz, int nx, int ny,
                       int nz, int ux, int uy, int uz, int vx, int vy, int vz,
                       int su, int sv) -> float {
    int px = bx + nx;
    int py = by + ny;
    int pz = bz + nz;
    int s1 = isOpaqueFull(px + ux * su, py + uy * su, pz + uz * su) ? 1 : 0;
    int s2 = isOpaqueFull(px + vx * sv, py + vy * sv, pz + vz * sv) ? 1 : 0;
    int c = (s1 && s2)
                ? 1
                : (isOpaqueFull(px + ux * su + vx * sv, py + uy * su + vy * sv,
                                pz + uz * su + vz * sv)
                       ? 1
                       : 0);
    int occ = (s1 && s2) ? 3 : (s1 + s2 + c);
    static const float kAoLut[4] = {1.00f, 0.84f, 0.68f, 0.50f};
    return kAoLut[std::clamp(occ, 0, 3)];
  };

  bool atlasReady = BlockTextureRegistry::IsReady();

  for (int32_t x = 0; x < doc.width; ++x) {
    for (int32_t y = 0; y < doc.height; ++y) {
      for (int32_t z = 0; z < doc.depth; ++z) {
        uint16_t pid = getBlk(x, y, z);
        if (pid == 0 || pid >= palFaces.size())
          continue;

        const auto &faces = palFaces[pid];
        int shape = palShape[pid];
        if (shape < 0)
          continue;
        bool isTrans = palTrans[pid];
        bool isWater =
            isTrans && doc.palette[pid].name.find("water") != std::string::npos;
        std::vector<PreviewVertex> &dst =
            isTrans ? g_TransVerts : g_OpaqueVerts;

        uint32_t abgr = doc.palette[pid].approxAbgr;
        uint8_t cr = static_cast<uint8_t>(abgr & 0xFF);
        uint8_t cg = static_cast<uint8_t>((abgr >> 8) & 0xFF);
        uint8_t cb = static_cast<uint8_t>((abgr >> 16) & 0xFF);
        uint8_t ca = isWater ? 185 : (isTrans ? 148 : 255);

        float bx0 = static_cast<float>(x) - halfW;
        float by0 = static_cast<float>(y) - halfH;
        float bz0 = static_cast<float>(z) - halfD;
        float bx1 = bx0 + 1.0f;
        float by1 = by0 + 1.0f;
        float bz1 = bz0 + 1.0f;

        auto pushQuad = [&](float x0, float y0, float z0, float x1, float y1,
                            float z1, float x2, float y2, float z2, float x3,
                            float y3, float z3, float a0, float a1, float a2,
                            float a3, float shade,
                            const BlockTextureRegistry::BlockFaceUVs &uv) {
          bool useTex = atlasReady && uv.valid;
          float baseR = useTex ? 255.0f : static_cast<float>(cr);
          float baseG = useTex ? 255.0f : static_cast<float>(cg);
          float baseB = useTex ? 255.0f : static_cast<float>(cb);
          float u0 = useTex ? uv.u0 : -1.0f;
          float v0 = useTex ? uv.v0 : 0.0f;
          float u1 = useTex ? uv.u1 : -1.0f;
          float v1 = useTex ? uv.v1 : 0.0f;

          auto mkV = [&](float vx, float vy, float vz, float tu, float tv,
                         float ao) -> PreviewVertex {
            float k = shade * ao;
            return PreviewVertex{
                vx,
                vy,
                vz,
                tu,
                tv,
                static_cast<uint8_t>(std::clamp(baseR * k, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(baseG * k, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(baseB * k, 0.0f, 255.0f)),
                ca};
          };

          PreviewVertex p0 = mkV(x0, y0, z0, u0, v0, a0);
          PreviewVertex p1 = mkV(x1, y1, z1, u1, v0, a1);
          PreviewVertex p2 = mkV(x2, y2, z2, u1, v1, a2);
          PreviewVertex p3 = mkV(x3, y3, z3, u0, v1, a3);

          if (a0 + a2 >= a1 + a3) {
            dst.push_back(p0);
            dst.push_back(p1);
            dst.push_back(p2);
            dst.push_back(p0);
            dst.push_back(p2);
            dst.push_back(p3);
          } else {
            dst.push_back(p1);
            dst.push_back(p2);
            dst.push_back(p3);
            dst.push_back(p1);
            dst.push_back(p3);
            dst.push_back(p0);
          }
        };

        if (shape == 1) {
          const auto &uv = faces.north.valid ? faces.north : faces.up;
          pushQuad(bx0 + 0.12f, by1, bz0 + 0.12f, bx1 - 0.12f, by1, bz1 - 0.12f,
                   bx1 - 0.12f, by0, bz1 - 0.12f, bx0 + 0.12f, by0, bz0 + 0.12f,
                   1.0f, 1.0f, 0.85f, 0.85f, 0.92f, uv);
          pushQuad(bx1 - 0.12f, by1, bz0 + 0.12f, bx0 + 0.12f, by1, bz1 - 0.12f,
                   bx0 + 0.12f, by0, bz1 - 0.12f, bx1 - 0.12f, by0, bz0 + 0.12f,
                   1.0f, 1.0f, 0.85f, 0.85f, 0.88f, uv);
          continue;
        }

        auto emitBox = [&](float ax0, float ay0, float az0, float ax1,
                           float ay1, float az1, bool fullCube) {
          if (!fullCube || !isOpaqueFull(x, y + 1, z)) {
            float a0 =
                fullCube ? computeAo(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, -1, 1, 1)
                         : 1.0f;
            float a1 =
                fullCube ? computeAo(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, -1, 1, 1)
                         : 1.0f;
            float a2 = fullCube
                           ? computeAo(x, y, z, 0, 1, 0, 1, 0, 0, 0, 0, 1, 1, 1)
                           : 1.0f;
            float a3 =
                fullCube ? computeAo(x, y, z, 0, 1, 0, -1, 0, 0, 0, 0, 1, 1, 1)
                         : 1.0f;
            pushQuad(ax0, ay1, az0, ax1, ay1, az0, ax1, ay1, az1, ax0, ay1, az1,
                     a0, a1, a2, a3, 1.00f, faces.up);
          }
          if (!fullCube || !isOpaqueFull(x, y - 1, z)) {
            pushQuad(ax0, ay0, az1, ax1, ay0, az1, ax1, ay0, az0, ax0, ay0, az0,
                     0.9f, 0.9f, 0.9f, 0.9f, 0.55f, faces.down);
          }
          if (!fullCube || !isOpaqueFull(x, y, z + 1)) {
            float a0 =
                fullCube ? computeAo(x, y, z, 0, 0, 1, -1, 0, 0, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a1 = fullCube
                           ? computeAo(x, y, z, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1)
                           : 1.0f;
            float a2 =
                fullCube ? computeAo(x, y, z, 0, 0, 1, 1, 0, 0, 0, -1, 0, 1, 1)
                         : 1.0f;
            float a3 =
                fullCube ? computeAo(x, y, z, 0, 0, 1, -1, 0, 0, 0, -1, 0, 1, 1)
                         : 1.0f;
            pushQuad(ax0, ay1, az1, ax1, ay1, az1, ax1, ay0, az1, ax0, ay0, az1,
                     a0, a1, a2, a3, 0.80f, faces.south);
          }
          if (!fullCube || !isOpaqueFull(x, y, z - 1)) {
            float a0 =
                fullCube ? computeAo(x, y, z, 0, 0, -1, 1, 0, 0, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a1 =
                fullCube ? computeAo(x, y, z, 0, 0, -1, -1, 0, 0, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a2 = fullCube ? computeAo(x, y, z, 0, 0, -1, -1, 0, 0, 0, -1,
                                            0, 1, 1)
                                : 1.0f;
            float a3 =
                fullCube ? computeAo(x, y, z, 0, 0, -1, 1, 0, 0, 0, -1, 0, 1, 1)
                         : 1.0f;
            pushQuad(ax1, ay1, az0, ax0, ay1, az0, ax0, ay0, az0, ax1, ay0, az0,
                     a0, a1, a2, a3, 0.80f, faces.north);
          }
          if (!fullCube || !isOpaqueFull(x + 1, y, z)) {
            float a0 = fullCube
                           ? computeAo(x, y, z, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1)
                           : 1.0f;
            float a1 =
                fullCube ? computeAo(x, y, z, 1, 0, 0, 0, 0, -1, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a2 =
                fullCube ? computeAo(x, y, z, 1, 0, 0, 0, 0, -1, 0, -1, 0, 1, 1)
                         : 1.0f;
            float a3 =
                fullCube ? computeAo(x, y, z, 1, 0, 0, 0, 0, 1, 0, -1, 0, 1, 1)
                         : 1.0f;
            pushQuad(ax1, ay1, az1, ax1, ay1, az0, ax1, ay0, az0, ax1, ay0, az1,
                     a0, a1, a2, a3, 0.90f, faces.east);
          }
          if (!fullCube || !isOpaqueFull(x - 1, y, z)) {
            float a0 =
                fullCube ? computeAo(x, y, z, -1, 0, 0, 0, 0, -1, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a1 =
                fullCube ? computeAo(x, y, z, -1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1)
                         : 1.0f;
            float a2 =
                fullCube ? computeAo(x, y, z, -1, 0, 0, 0, 0, 1, 0, -1, 0, 1, 1)
                         : 1.0f;
            float a3 = fullCube ? computeAo(x, y, z, -1, 0, 0, 0, 0, -1, 0, -1,
                                            0, 1, 1)
                                : 1.0f;
            pushQuad(ax0, ay1, az0, ax0, ay1, az1, ax0, ay0, az1, ax0, ay0, az0,
                     a0, a1, a2, a3, 0.74f, faces.west);
          }
        };

        auto canConnWallFence = [&](int32_t nx, int32_t ny, int32_t nz) {
          if (isOpaqueFull(nx, ny, nz))
            return true;
          uint16_t np = getBlk(nx, ny, nz);
          if (np == 0 || np >= palShape.size())
            return false;
          return palShape[np] == 4 || palShape[np] == 9 ||
                 doc.palette[np].name.find("fence_gate") != std::string::npos;
        };
        auto canConnPane = [&](int32_t nx, int32_t ny, int32_t nz) {
          if (isOpaqueFull(nx, ny, nz))
            return true;
          uint16_t np = getBlk(nx, ny, nz);
          if (np == 0 || np >= palShape.size())
            return false;
          return palShape[np] == 5 || palShape[np] == 4 ||
                 doc.palette[np].name.find("glass") != std::string::npos;
        };

        auto pushSubQuad = [&](float x0, float y0, float z0, float x1, float y1,
                               float z1, float x2, float y2, float z2, float x3,
                               float y3, float z3, float shade,
                               const BlockTextureRegistry::BlockFaceUVs &uv,
                               float pu0, float pv0, float pu1, float pv1) {
          bool useTex = atlasReady && uv.valid;
          float baseR = useTex ? 255.0f : static_cast<float>(cr);
          float baseG = useTex ? 255.0f : static_cast<float>(cg);
          float baseB = useTex ? 255.0f : static_cast<float>(cb);
          float tw = uv.u1 - uv.u0;
          float th = uv.v1 - uv.v0;
          float u0 = useTex ? (uv.u0 + (pu0 / 16.0f) * tw) : -1.0f;
          float v0 = useTex ? (uv.v0 + (pv0 / 16.0f) * th) : 0.0f;
          float u1 = useTex ? (uv.u0 + (pu1 / 16.0f) * tw) : -1.0f;
          float v1 = useTex ? (uv.v0 + (pv1 / 16.0f) * th) : 0.0f;

          auto mkV = [&](float vx, float vy, float vz, float tu,
                         float tv) -> PreviewVertex {
            return PreviewVertex{
                vx,
                vy,
                vz,
                tu,
                tv,
                static_cast<uint8_t>(std::clamp(baseR * shade, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(baseG * shade, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(baseB * shade, 0.0f, 255.0f)),
                ca};
          };

          PreviewVertex p0 = mkV(x0, y0, z0, u0, v0);
          PreviewVertex p1 = mkV(x1, y1, z1, u1, v0);
          PreviewVertex p2 = mkV(x2, y2, z2, u1, v1);
          PreviewVertex p3 = mkV(x3, y3, z3, u0, v1);
          dst.push_back(p0);
          dst.push_back(p1);
          dst.push_back(p2);
          dst.push_back(p0);
          dst.push_back(p2);
          dst.push_back(p3);
        };

        auto emitCuboid = [&](float fx0, float fy0, float fz0, float fx1,
                              float fy1, float fz1, float su0, float sv0,
                              float su1, float sv1, float tu0, float tv0,
                              float tu1, float tv1,
                              const BlockTextureRegistry::BlockFaceUVs &sideUv,
                              const BlockTextureRegistry::BlockFaceUVs &topUv,
                              const BlockTextureRegistry::BlockFaceUVs &botUv) {
          float ax0 = bx0 + fx0 / 16.0f;
          float ay0 = by0 + fy0 / 16.0f;
          float az0 = bz0 + fz0 / 16.0f;
          float ax1 = bx0 + fx1 / 16.0f;
          float ay1 = by0 + fy1 / 16.0f;
          float az1 = bz0 + fz1 / 16.0f;
          pushSubQuad(ax0, ay1, az0, ax1, ay1, az0, ax1, ay1, az1, ax0, ay1,
                      az1, 1.00f, topUv, tu0, tv0, tu1, tv1);
          pushSubQuad(ax0, ay0, az1, ax1, ay0, az1, ax1, ay0, az0, ax0, ay0,
                      az0, 0.55f, botUv, tu0, tv0, tu1, tv1);
          pushSubQuad(ax0, ay1, az1, ax1, ay1, az1, ax1, ay0, az1, ax0, ay0,
                      az1, 0.80f, sideUv, su0, sv0, su1, sv1);
          pushSubQuad(ax1, ay1, az0, ax0, ay1, az0, ax0, ay0, az0, ax1, ay0,
                      az0, 0.80f, sideUv, su0, sv0, su1, sv1);
          pushSubQuad(ax1, ay1, az1, ax1, ay1, az0, ax1, ay0, az0, ax1, ay0,
                      az1, 0.90f, sideUv, su0, sv0, su1, sv1);
          pushSubQuad(ax0, ay1, az0, ax0, ay1, az1, ax0, ay0, az1, ax0, ay0,
                      az0, 0.74f, sideUv, su0, sv0, su1, sv1);
        };

        if (shape == 3) {
          bool topSlab = false;
          for (const auto &st : doc.palette[pid].states) {
            if (st.key == "top_slot_bit" && st.intVal != 0)
              topSlab = true;
            if ((st.key == "vertical_half" ||
                 st.key == "minecraft:vertical_half") &&
                st.strVal == "top")
              topSlab = true;
          }
          if (topSlab)
            emitBox(bx0, by0 + 0.5f, bz0, bx1, by1, bz1, false);
          else
            emitBox(bx0, by0, bz0, bx1, by0 + 0.5f, bz1, false);
        } else if (shape == 4) {
          bool cN = canConnWallFence(x, y, z - 1);
          bool cS = canConnWallFence(x, y, z + 1);
          bool cW = canConnWallFence(x - 1, y, z);
          bool cE = canConnWallFence(x + 1, y, z);
          bool straightNS = cN && cS && !cW && !cE && getBlk(x, y + 1, z) == 0;
          bool straightEW = cW && cE && !cN && !cS && getBlk(x, y + 1, z) == 0;
          if (!straightNS && !straightEW) {
            emitBox(bx0 + 0.25f, by0, bz0 + 0.25f, bx1 - 0.25f, by1,
                    bz1 - 0.25f, false);
          }
          if (cN)
            emitBox(bx0 + 0.3125f, by0, bz0, bx1 - 0.3125f, by0 + 0.875f,
                    bz0 + 0.5f, false);
          if (cS)
            emitBox(bx0 + 0.3125f, by0, bz0 + 0.5f, bx1 - 0.3125f, by0 + 0.875f,
                    bz1, false);
          if (cW)
            emitBox(bx0, by0, bz0 + 0.3125f, bx0 + 0.5f, by0 + 0.875f,
                    bz1 - 0.3125f, false);
          if (cE)
            emitBox(bx0 + 0.5f, by0, bz0 + 0.3125f, bx1, by0 + 0.875f,
                    bz1 - 0.3125f, false);
        } else if (shape == 5) {
          bool cN = canConnPane(x, y, z - 1);
          bool cS = canConnPane(x, y, z + 1);
          bool cW = canConnPane(x - 1, y, z);
          bool cE = canConnPane(x + 1, y, z);
          emitBox(bx0 + 0.4375f, by0, bz0 + 0.4375f, bx1 - 0.4375f, by1,
                  bz1 - 0.4375f, false);
          if (cN)
            emitBox(bx0 + 0.4375f, by0, bz0, bx1 - 0.4375f, by1, bz0 + 0.4375f,
                    false);
          if (cS)
            emitBox(bx0 + 0.4375f, by0, bz0 + 0.5625f, bx1 - 0.4375f, by1, bz1,
                    false);
          if (cW)
            emitBox(bx0, by0, bz0 + 0.4375f, bx0 + 0.4375f, by1, bz1 - 0.4375f,
                    false);
          if (cE)
            emitBox(bx0 + 0.5625f, by0, bz0 + 0.4375f, bx1, by1, bz1 - 0.4375f,
                    false);
        } else if (shape == 6) {
          emitBox(bx0 + 0.46f, by0, bz0 + 0.46f, bx1 - 0.46f, by1, bz1 - 0.46f,
                  false);
          emitBox(bx0 + 0.12f, by0 + 0.15f, bz0 + 0.42f, bx1 - 0.12f,
                  by0 + 0.94f, bz0 + 0.48f, false);
        } else if (shape == 7) {
          int dir = 0;
          bool upside = false;
          for (const auto &st : doc.palette[pid].states) {
            if (st.key == "weirdo_direction")
              dir = st.intVal;
            if (st.key == "upside_down_bit" && st.intVal != 0)
              upside = true;
          }
          float baseY0 = upside ? by0 + 0.5f : by0;
          float baseY1 = upside ? by1 : by0 + 0.5f;
          float stepY0 = upside ? by0 : by0 + 0.5f;
          float stepY1 = upside ? by0 + 0.5f : by1;
          emitBox(bx0, baseY0, bz0, bx1, baseY1, bz1, false);
          if (dir == 0)
            emitBox(bx0 + 0.5f, stepY0, bz0, bx1, stepY1, bz1, false);
          else if (dir == 1)
            emitBox(bx0, stepY0, bz0, bx0 + 0.5f, stepY1, bz1, false);
          else if (dir == 2)
            emitBox(bx0, stepY0, bz0 + 0.5f, bx1, stepY1, bz1, false);
          else
            emitBox(bx0, stepY0, bz0, bx1, stepY1, bz0 + 0.5f, false);
        } else if (shape == 8) {
          const auto &uv = faces.north.valid ? faces.north : faces.up;
          pushQuad(bx0 + 0.25f, by1, bz0, bx0 + 0.25f, by1, bz1, bx0 + 0.25f,
                   by0, bz1, bx0 + 0.25f, by0, bz0, 1.0f, 1.0f, 0.85f, 0.85f,
                   0.92f, uv);
          pushQuad(bx1 - 0.25f, by1, bz0, bx1 - 0.25f, by1, bz1, bx1 - 0.25f,
                   by0, bz1, bx1 - 0.25f, by0, bz0, 1.0f, 1.0f, 0.85f, 0.85f,
                   0.92f, uv);
          pushQuad(bx0, by1, bz0 + 0.25f, bx1, by1, bz0 + 0.25f, bx1, by0,
                   bz0 + 0.25f, bx0, by0, bz0 + 0.25f, 1.0f, 1.0f, 0.85f, 0.85f,
                   0.88f, uv);
          pushQuad(bx0, by1, bz1 - 0.25f, bx1, by1, bz1 - 0.25f, bx1, by0,
                   bz1 - 0.25f, bx0, by0, bz1 - 0.25f, 1.0f, 1.0f, 0.85f, 0.85f,
                   0.88f, uv);
        } else if (shape == 9) {
          bool cN = canConnWallFence(x, y, z - 1);
          bool cS = canConnWallFence(x, y, z + 1);
          bool cW = canConnWallFence(x - 1, y, z);
          bool cE = canConnWallFence(x + 1, y, z);
          emitBox(bx0 + 0.375f, by0, bz0 + 0.375f, bx1 - 0.375f, by1,
                  bz1 - 0.375f, false);
          if (cN) {
            emitBox(bx0 + 0.4375f, by0 + 0.75f, bz0, bx1 - 0.4375f,
                    by0 + 0.9375f, bz0 + 0.375f, false);
            emitBox(bx0 + 0.4375f, by0 + 0.375f, bz0, bx1 - 0.4375f,
                    by0 + 0.5625f, bz0 + 0.375f, false);
          }
          if (cS) {
            emitBox(bx0 + 0.4375f, by0 + 0.75f, bz0 + 0.625f, bx1 - 0.4375f,
                    by0 + 0.9375f, bz1, false);
            emitBox(bx0 + 0.4375f, by0 + 0.375f, bz0 + 0.625f, bx1 - 0.4375f,
                    by0 + 0.5625f, bz1, false);
          }
          if (cW) {
            emitBox(bx0, by0 + 0.75f, bz0 + 0.4375f, bx0 + 0.375f,
                    by0 + 0.9375f, bz1 - 0.4375f, false);
            emitBox(bx0, by0 + 0.375f, bz0 + 0.4375f, bx0 + 0.375f,
                    by0 + 0.5625f, bz1 - 0.4375f, false);
          }
          if (cE) {
            emitBox(bx0 + 0.625f, by0 + 0.75f, bz0 + 0.4375f, bx1,
                    by0 + 0.9375f, bz1 - 0.4375f, false);
            emitBox(bx0 + 0.625f, by0 + 0.375f, bz0 + 0.4375f, bx1,
                    by0 + 0.5625f, bz1 - 0.4375f, false);
          }
        } else if (palBaked[pid] != nullptr) {
          const GeneratedBlockModels::BakedBlockModel *bm = palBaked[pid];
          int rotX = static_cast<int>(bm->rotX);
          int rotY = static_cast<int>(bm->rotY);
          bool isRod = (palShort[pid].find("lightning_rod") != std::string::npos ||
                        palShort[pid].find("end_rod") != std::string::npos);
          for (const auto &st : doc.palette[pid].states) {
            if (st.key == "hanging" && st.intVal != 0) {
              const auto *hv =
                  GeneratedBlockModels::FindVariant(palShort[pid] + "_hanging");
              if (hv)
                bm = hv;
            }
            if (st.key == "facing_direction") {
              int fd = st.intVal;
              if (isRod) {
                if (fd == 0) {
                  rotX = 2;
                  rotY = 0;
                } else if (fd == 1) {
                  rotX = 0;
                  rotY = 0;
                } else if (fd == 2) {
                  rotX = 1;
                  rotY = 0;
                } else if (fd == 3) {
                  rotX = 1;
                  rotY = 2;
                } else if (fd == 4) {
                  rotX = 1;
                  rotY = 3;
                } else if (fd == 5) {
                  rotX = 1;
                  rotY = 1;
                }
              } else {
                if (fd == 2)
                  rotY = 0;
                else if (fd == 5)
                  rotY = 1;
                else if (fd == 3)
                  rotY = 2;
                else if (fd == 4)
                  rotY = 3;
              }
            }
            if (st.key == "minecraft:cardinal_direction" ||
                st.key == "cardinal_direction") {
              if (st.strVal == "north")
                rotY = 0;
              else if (st.strVal == "east")
                rotY = 1;
              else if (st.strVal == "south")
                rotY = 2;
              else if (st.strVal == "west")
                rotY = 3;
            }
          }

          const auto &sideUv = faces.north.valid ? faces.north : faces.up;
          const auto &topUv = faces.up.valid ? faces.up : sideUv;
          const auto &botUv = faces.down.valid ? faces.down : sideUv;
          const auto &geom = GeneratedBlockModels::kGeoms[bm->geomIdx];

          auto rotPt = [](float &px, float &py, float &pz, int axis, float deg,
                          float ox, float oy, float oz) {
            if (axis == 0 || std::fabs(deg) < 0.01f)
              return;
            float rad = deg * 0.0174532925f;
            float cs = std::cos(rad);
            float sn = std::sin(rad);
            float dx = px - ox;
            float dy = py - oy;
            float dz = pz - oz;
            if (axis == 1) {
              px = ox + dx;
              py = oy + dy * cs - dz * sn;
              pz = oz + dy * sn + dz * cs;
            } else if (axis == 2) {
              px = ox + dx * cs + dz * sn;
              py = oy + dy;
              pz = oz - dx * sn + dz * cs;
            } else if (axis == 3) {
              px = ox + dx * cs - dy * sn;
              py = oy + dx * sn + dy * cs;
              pz = oz + dz;
            }
          };

          for (uint16_t ci = 0; ci < geom.count; ++ci) {
            const auto &c = GeneratedBlockModels::kCuboids[geom.start + ci];
            float x0 = c.fx0, y0 = c.fy0, z0 = c.fz0;
            float x1 = c.fx1, y1 = c.fy1, z1 = c.fz1;

            struct QuadPts {
              float p[4][3];
              float shade;
              const BlockTextureRegistry::BlockFaceUVs *tex;
            };
            QuadPts qList[6] = {
                {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}},
                 1.00f,
                 &topUv},
                {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}},
                 0.55f,
                 &botUv},
                {{{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}},
                 0.80f,
                 &sideUv},
                {{{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}},
                 0.80f,
                 &sideUv},
                {{{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}},
                 0.90f,
                 &sideUv},
                {{{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}},
                 0.74f,
                 &sideUv},
            };

            for (int fi = 0; fi < 6; ++fi) {
              const auto &bf = c.faces[fi];
              if (!bf.valid)
                continue;
              QuadPts q = qList[fi];
              for (int vi = 0; vi < 4; ++vi) {
                rotPt(q.p[vi][0], q.p[vi][1], q.p[vi][2], c.rotAxis, c.rotAngle,
                      c.ox, c.oy, c.oz);
                if (rotX != 0)
                  rotPt(q.p[vi][0], q.p[vi][1], q.p[vi][2], 1,
                        static_cast<float>(rotX) * 90.0f, 8.0f, 8.0f, 8.0f);
                if (rotY != 0)
                  rotPt(q.p[vi][0], q.p[vi][1], q.p[vi][2], 2,
                        -static_cast<float>(rotY) * 90.0f, 8.0f, 8.0f, 8.0f);
                q.p[vi][0] = bx0 + q.p[vi][0] / 16.0f;
                q.p[vi][1] = by0 + q.p[vi][1] / 16.0f;
                q.p[vi][2] = bz0 + q.p[vi][2] / 16.0f;
              }
              pushSubQuad(q.p[0][0], q.p[0][1], q.p[0][2], q.p[1][0], q.p[1][1],
                          q.p[1][2], q.p[2][0], q.p[2][1], q.p[2][2], q.p[3][0],
                          q.p[3][1], q.p[3][2], q.shade, *q.tex, bf.u0, bf.v0,
                          bf.u1, bf.v1);
            }
          }
        } else if (shape == 11) {
          const auto &uv = faces.north.valid ? faces.north : faces.up;
          bool placed = false;
          if (isOpaqueFull(x, y, z - 1)) {
            pushQuad(bx0, by1, bz0 + 0.04f, bx1, by1, bz0 + 0.04f, bx1, by0,
                     bz0 + 0.04f, bx0, by0, bz0 + 0.04f, 1.0f, 1.0f, 0.9f, 0.9f,
                     0.85f, uv);
            placed = true;
          }
          if (isOpaqueFull(x, y, z + 1)) {
            pushQuad(bx0, by1, bz1 - 0.04f, bx1, by1, bz1 - 0.04f, bx1, by0,
                     bz1 - 0.04f, bx0, by0, bz1 - 0.04f, 1.0f, 1.0f, 0.9f, 0.9f,
                     0.85f, uv);
            placed = true;
          }
          if (isOpaqueFull(x - 1, y, z)) {
            pushQuad(bx0 + 0.04f, by1, bz0, bx0 + 0.04f, by1, bz1, bx0 + 0.04f,
                     by0, bz1, bx0 + 0.04f, by0, bz0, 1.0f, 1.0f, 0.9f, 0.9f,
                     0.82f, uv);
            placed = true;
          }
          if (isOpaqueFull(x + 1, y, z)) {
            pushQuad(bx1 - 0.04f, by1, bz0, bx1 - 0.04f, by1, bz1, bx1 - 0.04f,
                     by0, bz1, bx1 - 0.04f, by0, bz0, 1.0f, 1.0f, 0.9f, 0.9f,
                     0.82f, uv);
            placed = true;
          }
          if (!placed) {
            pushQuad(bx0, by1, bz0 + 0.04f, bx1, by1, bz0 + 0.04f, bx1, by0,
                     bz0 + 0.04f, bx0, by0, bz0 + 0.04f, 1.0f, 1.0f, 0.9f, 0.9f,
                     0.85f, uv);
          }
        } else {
          if (isTrans) {
            uint16_t nUp = getBlk(x, y + 1, z);
            float topY = (isWater && nUp != pid) ? (by1 - 0.125f) : by1;
            if (nUp != pid)
              pushQuad(bx0, topY, bz0, bx1, topY, bz0, bx1, topY, bz1, bx0,
                       topY, bz1, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, faces.up);
            if (getBlk(x, y - 1, z) != pid)
              pushQuad(bx0, by0, bz1, bx1, by0, bz1, bx1, by0, bz0, bx0, by0,
                       bz0, 0.9f, 0.9f, 0.9f, 0.9f, 0.6f, faces.down);
            if (getBlk(x, y, z + 1) != pid)
              pushQuad(bx0, topY, bz1, bx1, topY, bz1, bx1, by0, bz1, bx0, by0,
                       bz1, 1.0f, 1.0f, 1.0f, 1.0f, 0.85f, faces.south);
            if (getBlk(x, y, z - 1) != pid)
              pushQuad(bx1, topY, bz0, bx0, topY, bz0, bx0, by0, bz0, bx1, by0,
                       bz0, 1.0f, 1.0f, 1.0f, 1.0f, 0.80f, faces.north);
            if (getBlk(x + 1, y, z) != pid)
              pushQuad(bx1, topY, bz1, bx1, topY, bz0, bx1, by0, bz0, bx1, by0,
                       bz1, 1.0f, 1.0f, 1.0f, 1.0f, 0.90f, faces.east);
            if (getBlk(x - 1, y, z) != pid)
              pushQuad(bx0, topY, bz0, bx0, topY, bz1, bx0, by0, bz1, bx0, by0,
                       bz0, 1.0f, 1.0f, 1.0f, 1.0f, 0.74f, faces.west);
          } else {
            emitBox(bx0, by0, bz0, bx1, by1, bz1, true);
          }
        }

        if (g_OpaqueVerts.size() + g_TransVerts.size() >= 900000)
          break;
      }
      if (g_OpaqueVerts.size() + g_TransVerts.size() >= 900000)
        break;
    }
    if (g_OpaqueVerts.size() + g_TransVerts.size() >= 900000)
      break;
  }

  g_HasMesh = (!g_OpaqueVerts.empty() || !g_TransVerts.empty());
  g_MeshDirty = true;
  g_RenderDirty = true;
}

void UploadDx11Buffers() {
  if (!g_Dx11Dev)
    return;
  if (g_Dx11VbOpaque) {
    g_Dx11VbOpaque->Release();
    g_Dx11VbOpaque = nullptr;
  }
  if (g_Dx11VbTrans) {
    g_Dx11VbTrans->Release();
    g_Dx11VbTrans = nullptr;
  }
  g_Dx11OpaqueCount = 0;
  g_Dx11TransCount = 0;

  auto mkVb = [&](const std::vector<PreviewVertex> &verts,
                  ID3D11Buffer **outBuf, UINT &outCount) {
    if (verts.empty())
      return;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(PreviewVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = verts.data();
    if (SUCCEEDED(g_Dx11Dev->CreateBuffer(&bd, &init, outBuf))) {
      outCount = static_cast<UINT>(verts.size());
    }
  };
  mkVb(g_OpaqueVerts, &g_Dx11VbOpaque, g_Dx11OpaqueCount);
  mkVb(g_TransVerts, &g_Dx11VbTrans, g_Dx11TransCount);
  g_MeshDirty = false;
}

void UploadDx12Buffers() {
  if (!g_Dx12Dev)
    return;
  if (g_Dx12VbOpaque) {
    g_Dx12RetiredVbs.push_back(g_Dx12VbOpaque);
    g_Dx12VbOpaque = nullptr;
  }
  if (g_Dx12VbTrans) {
    g_Dx12RetiredVbs.push_back(g_Dx12VbTrans);
    g_Dx12VbTrans = nullptr;
  }
  while (g_Dx12RetiredVbs.size() > 8) {
    g_Dx12RetiredVbs.front()->Release();
    g_Dx12RetiredVbs.erase(g_Dx12RetiredVbs.begin());
  }
  g_Dx12OpaqueCount = 0;
  g_Dx12TransCount = 0;

  auto mkVb = [&](const std::vector<PreviewVertex> &verts,
                  ID3D12Resource **outBuf, D3D12_VERTEX_BUFFER_VIEW &outView,
                  UINT &outCount) {
    if (verts.empty())
      return;
    UINT byteSize = static_cast<UINT>(verts.size() * sizeof(PreviewVertex));
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = byteSize;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (SUCCEEDED(g_Dx12Dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(outBuf)))) {
      void *mapped = nullptr;
      D3D12_RANGE rr{0, 0};
      if (SUCCEEDED((*outBuf)->Map(0, &rr, &mapped))) {
        std::memcpy(mapped, verts.data(), byteSize);
        (*outBuf)->Unmap(0, nullptr);
      }
      outView.BufferLocation = (*outBuf)->GetGPUVirtualAddress();
      outView.SizeInBytes = byteSize;
      outView.StrideInBytes = sizeof(PreviewVertex);
      outCount = static_cast<UINT>(verts.size());
    }
  };
  mkVb(g_OpaqueVerts, &g_Dx12VbOpaque, g_Dx12VbvOpaque, g_Dx12OpaqueCount);
  mkVb(g_TransVerts, &g_Dx12VbTrans, g_Dx12VbvTrans, g_Dx12TransCount);
  g_MeshDirty = false;
}

} // namespace

void EnsureDX11(ID3D11Device *device) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!device || g_GpuReady)
    return;
  g_Dx11Dev = device;
  g_IsDx11 = true;

  D3D11_TEXTURE2D_DESC cdesc = {};
  cdesc.Width = kRtWidth;
  cdesc.Height = kRtHeight;
  cdesc.MipLevels = 1;
  cdesc.ArraySize = 1;
  cdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  cdesc.SampleDesc.Count = 1;
  cdesc.Usage = D3D11_USAGE_DEFAULT;
  cdesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device->CreateTexture2D(&cdesc, nullptr, &g_Dx11ColorTex)))
    return;
  if (FAILED(
          device->CreateRenderTargetView(g_Dx11ColorTex, nullptr, &g_Dx11Rtv)))
    return;
  if (FAILED(device->CreateShaderResourceView(g_Dx11ColorTex, nullptr,
                                              &g_Dx11Srv)))
    return;

  D3D11_TEXTURE2D_DESC ddesc = cdesc;
  ddesc.Format = DXGI_FORMAT_D32_FLOAT;
  ddesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device->CreateTexture2D(&ddesc, nullptr, &g_Dx11DepthTex)))
    return;
  if (FAILED(
          device->CreateDepthStencilView(g_Dx11DepthTex, nullptr, &g_Dx11Dsv)))
    return;

  ID3DBlob *vsBlob = nullptr;
  ID3DBlob *psBlob = nullptr;
  if (FAILED(D3DCompile(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr,
                        "main", "vs_5_0", 0, 0, &vsBlob, nullptr)))
    return;
  if (FAILED(D3DCompile(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &psBlob, nullptr))) {
    vsBlob->Release();
    return;
  }

  device->CreateVertexShader(vsBlob->GetBufferPointer(),
                             vsBlob->GetBufferSize(), nullptr, &g_Dx11Vs);
  device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                            nullptr, &g_Dx11Ps);

  D3D11_INPUT_ELEMENT_DESC il[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  device->CreateInputLayout(il, 3, vsBlob->GetBufferPointer(),
                            vsBlob->GetBufferSize(), &g_Dx11Layout);
  vsBlob->Release();
  psBlob->Release();

  D3D11_BUFFER_DESC cbd = {};
  cbd.ByteWidth = sizeof(PreviewCB);
  cbd.Usage = D3D11_USAGE_DYNAMIC;
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  device->CreateBuffer(&cbd, nullptr, &g_Dx11Cb);

  D3D11_SAMPLER_DESC sd = {};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  device->CreateSamplerState(&sd, &g_Dx11Sampler);

  D3D11_RASTERIZER_DESC rd = {};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  device->CreateRasterizerState(&rd, &g_Dx11Rast);

  D3D11_DEPTH_STENCIL_DESC dsd = {};
  dsd.DepthEnable = TRUE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  dsd.DepthFunc = D3D11_COMPARISON_LESS;
  device->CreateDepthStencilState(&dsd, &g_Dx11DssOpaque);
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  device->CreateDepthStencilState(&dsd, &g_Dx11DssTrans);

  D3D11_BLEND_DESC bsd = {};
  bsd.RenderTarget[0].BlendEnable = FALSE;
  bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  device->CreateBlendState(&bsd, &g_Dx11BlendOpaque);
  bsd.RenderTarget[0].BlendEnable = TRUE;
  bsd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bsd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bsd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bsd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bsd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  bsd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  device->CreateBlendState(&bsd, &g_Dx11BlendTrans);

  g_PreviewTexId = reinterpret_cast<ImTextureID>(g_Dx11Srv);
  g_GpuReady = (g_Dx11Vs && g_Dx11Ps && g_Dx11Layout && g_Dx11Srv);
}

void EnsureDX12(ID3D12Device *device, UiAssets::SrvAllocFn allocFn) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!device || !allocFn || g_GpuReady)
    return;
  g_Dx12Dev = device;
  g_IsDx12 = true;

  D3D12_HEAP_PROPERTIES defHeap = {};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC cdesc = {};
  cdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  cdesc.Width = kRtWidth;
  cdesc.Height = kRtHeight;
  cdesc.DepthOrArraySize = 1;
  cdesc.MipLevels = 1;
  cdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  cdesc.SampleDesc.Count = 1;
  cdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clearCol = {};
  clearCol.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (FAILED(device->CreateCommittedResource(
          &defHeap, D3D12_HEAP_FLAG_NONE, &cdesc,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearCol,
          IID_PPV_ARGS(&g_Dx12ColorTex))))
    return;

  D3D12_RESOURCE_DESC ddesc = cdesc;
  ddesc.Format = DXGI_FORMAT_D32_FLOAT;
  ddesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clearDep = {};
  clearDep.Format = DXGI_FORMAT_D32_FLOAT;
  clearDep.DepthStencil.Depth = 1.0f;
  if (FAILED(device->CreateCommittedResource(
          &defHeap, D3D12_HEAP_FLAG_NONE, &ddesc,
          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearDep,
          IID_PPV_ARGS(&g_Dx12DepthTex))))
    return;

  D3D12_DESCRIPTOR_HEAP_DESC rtvHDesc = {};
  rtvHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHDesc.NumDescriptors = 1;
  if (FAILED(device->CreateDescriptorHeap(&rtvHDesc,
                                          IID_PPV_ARGS(&g_Dx12RtvHeap))))
    return;
  g_Dx12Rtv = g_Dx12RtvHeap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(g_Dx12ColorTex, nullptr, g_Dx12Rtv);

  D3D12_DESCRIPTOR_HEAP_DESC dsvHDesc = {};
  dsvHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHDesc.NumDescriptors = 1;
  if (FAILED(device->CreateDescriptorHeap(&dsvHDesc,
                                          IID_PPV_ARGS(&g_Dx12DsvHeap))))
    return;
  g_Dx12Dsv = g_Dx12DsvHeap->GetCPUDescriptorHandleForHeapStart();
  device->CreateDepthStencilView(g_Dx12DepthTex, nullptr, g_Dx12Dsv);

  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.Num32BitValues = 16;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC smp = {};
  smp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  smp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  smp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  smp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 1;
  rsDesc.pStaticSamplers = &smp;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ID3DBlob *sigBlob = nullptr;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &sigBlob, nullptr)))
    return;
  device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
                              sigBlob->GetBufferSize(),
                              IID_PPV_ARGS(&g_Dx12RootSig));
  sigBlob->Release();

  ID3DBlob *vsBlob = nullptr;
  ID3DBlob *psBlob = nullptr;
  if (FAILED(D3DCompile(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr,
                        "main", "vs_5_0", 0, 0, &vsBlob, nullptr)))
    return;
  if (FAILED(D3DCompile(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &psBlob, nullptr))) {
    vsBlob->Release();
    return;
  }

  D3D12_INPUT_ELEMENT_DESC il[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = g_Dx12RootSig;
  psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  psoDesc.InputLayout = {il, 3};
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.DepthClipEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  psoDesc.SampleDesc.Count = 1;

  device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_Dx12PsoOpaque));

  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_Dx12PsoTrans));

  vsBlob->Release();
  psBlob->Release();

  D3D12_CPU_DESCRIPTOR_HANDLE cpuH{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpuH{};
  allocFn(&cpuH, &gpuH);
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(g_Dx12ColorTex, &srvDesc, cpuH);

  g_PreviewTexId = static_cast<ImTextureID>(gpuH.ptr);
  g_GpuReady = (g_Dx12PsoOpaque && g_Dx12PsoTrans && g_PreviewTexId != 0);
  Logger::info(g_GpuReady ? "Hardware Z-Buffer D3D12 PreviewRenderer ready"
                          : "D3D12 PreviewRenderer init failed",
               "Preview");
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_Mutex);
  auto rel = [](auto *&p) {
    if (p) {
      p->Release();
      p = nullptr;
    }
  };
  rel(g_Dx11VbOpaque);
  rel(g_Dx11VbTrans);
  rel(g_Dx11BlendOpaque);
  rel(g_Dx11BlendTrans);
  rel(g_Dx11DssOpaque);
  rel(g_Dx11DssTrans);
  rel(g_Dx11Rast);
  rel(g_Dx11Sampler);
  rel(g_Dx11Cb);
  rel(g_Dx11Layout);
  rel(g_Dx11Vs);
  rel(g_Dx11Ps);
  rel(g_Dx11Dsv);
  rel(g_Dx11DepthTex);
  rel(g_Dx11Srv);
  rel(g_Dx11Rtv);
  rel(g_Dx11ColorTex);
  g_Dx11Dev = nullptr;

  rel(g_Dx12VbOpaque);
  rel(g_Dx12VbTrans);
  for (auto *r : g_Dx12RetiredVbs) {
    if (r)
      r->Release();
  }
  g_Dx12RetiredVbs.clear();
  rel(g_Dx12PsoOpaque);
  rel(g_Dx12PsoTrans);
  rel(g_Dx12RootSig);
  rel(g_Dx12DsvHeap);
  rel(g_Dx12RtvHeap);
  rel(g_Dx12DepthTex);
  rel(g_Dx12ColorTex);
  g_Dx12Dev = nullptr;

  g_PreviewTexId = 0;
  g_GpuReady = false;
}

void SetStructurePath(const std::string &path, const std::string &format) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  bool atlasNow = BlockTextureRegistry::IsReady();
  if (path == g_LoadedPath && (atlasNow == g_WasAtlasReady))
    return;
  g_WasAtlasReady = atlasNow;
  g_LoadedPath = path;
  g_OpaqueVerts.clear();
  g_TransVerts.clear();
  g_HasMesh = false;
  g_MeshDirty = true;
  g_RenderDirty = true;
  if (path.empty())
    return;

  Bedrockmetica::SchematicDocument doc;
  bool ok = (format == ".bmet")
                ? Bedrockmetica::BmetCodec::LoadFromFile(path, doc)
                : Bedrockmetica::McStructureCodec::LoadFile(path, doc);
  if (ok) {
    BuildMeshFromDoc(doc);
  }
}

void Invalidate() {
  std::lock_guard<std::mutex> lock(g_Mutex);
  g_LoadedPath.clear();
  g_OpaqueVerts.clear();
  g_TransVerts.clear();
  g_HasMesh = false;
  g_MeshDirty = true;
  g_RenderDirty = true;
}

void RenderViewport(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize, float yaw,
                    float pitch, float zoom, float alpha) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!draw || !g_HasMesh || !g_GpuReady || g_PreviewTexId == 0)
    return;

  float newAspect = std::max(0.25f, vpSize.x / std::max(1.0f, vpSize.y));
  if (std::abs(yaw - g_Yaw) > 0.0002f || std::abs(pitch - g_Pitch) > 0.0002f ||
      std::abs(zoom - g_Zoom) > 0.0002f ||
      std::abs(newAspect - g_Aspect) > 0.002f) {
    g_Yaw = yaw;
    g_Pitch = pitch;
    g_Zoom = zoom;
    g_Aspect = newAspect;
    g_RenderDirty = true;
  }

  draw->AddImage(g_PreviewTexId, vpPos,
                 ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y),
                 ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                 IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
}

void FlushRenderDX11(ID3D11DeviceContext *ctx) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!ctx || !g_IsDx11 || !g_GpuReady)
    return;
  BlockTextureRegistry::SyncGpuDX11(ctx);
  if (g_MeshDirty)
    UploadDx11Buffers();
  if (!g_RenderDirty)
    return;
  g_RenderDirty = false;

  const float clearCol[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  ctx->ClearRenderTargetView(g_Dx11Rtv, clearCol);
  ctx->ClearDepthStencilView(g_Dx11Dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
  if (!g_HasMesh)
    return;

  PreviewCB cb{};
  ComputeMvpMatrix(cb.mvp);
  D3D11_MAPPED_SUBRESOURCE mr{};
  if (SUCCEEDED(ctx->Map(g_Dx11Cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr))) {
    std::memcpy(mr.pData, &cb, sizeof(cb));
    ctx->Unmap(g_Dx11Cb, 0);
  }

  D3D11_VIEWPORT vp{
      0.0f, 0.0f, static_cast<float>(kRtWidth), static_cast<float>(kRtHeight),
      0.0f, 1.0f};
  ctx->RSSetViewports(1, &vp);
  ctx->RSSetState(g_Dx11Rast);
  ctx->OMSetRenderTargets(1, &g_Dx11Rtv, g_Dx11Dsv);
  ctx->IASetInputLayout(g_Dx11Layout);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(g_Dx11Vs, nullptr, 0);
  ctx->VSSetConstantBuffers(0, 1, &g_Dx11Cb);
  ctx->PSSetShader(g_Dx11Ps, nullptr, 0);
  ctx->PSSetSamplers(0, 1, &g_Dx11Sampler);

  if (BlockTextureRegistry::IsReady()) {
    auto *srv = reinterpret_cast<ID3D11ShaderResourceView *>(
        BlockTextureRegistry::GetAtlasTexId());
    ctx->PSSetShaderResources(0, 1, &srv);
  }

  UINT stride = sizeof(PreviewVertex);
  UINT offset = 0;
  if (g_Dx11VbOpaque && g_Dx11OpaqueCount > 0) {
    ctx->OMSetDepthStencilState(g_Dx11DssOpaque, 0);
    ctx->OMSetBlendState(g_Dx11BlendOpaque, nullptr, 0xFFFFFFFF);
    ctx->IASetVertexBuffers(0, 1, &g_Dx11VbOpaque, &stride, &offset);
    ctx->Draw(g_Dx11OpaqueCount, 0);
  }
  if (g_Dx11VbTrans && g_Dx11TransCount > 0) {
    ctx->OMSetDepthStencilState(g_Dx11DssTrans, 0);
    ctx->OMSetBlendState(g_Dx11BlendTrans, nullptr, 0xFFFFFFFF);
    ctx->IASetVertexBuffers(0, 1, &g_Dx11VbTrans, &stride, &offset);
    ctx->Draw(g_Dx11TransCount, 0);
  }
}

void FlushRenderDX12(ID3D12GraphicsCommandList *cmd) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!cmd || !g_IsDx12 || !g_GpuReady)
    return;
  BlockTextureRegistry::SyncGpuDX12(cmd);
  if (g_MeshDirty)
    UploadDx12Buffers();
  if (!g_RenderDirty)
    return;
  g_RenderDirty = false;

  D3D12_RESOURCE_BARRIER bar = {};
  bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bar.Transition.pResource = g_Dx12ColorTex;
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  cmd->ResourceBarrier(1, &bar);

  const float clearCol[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  cmd->ClearRenderTargetView(g_Dx12Rtv, clearCol, 0, nullptr);
  cmd->ClearDepthStencilView(g_Dx12Dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                             nullptr);

  if (g_HasMesh) {
    D3D12_VIEWPORT vp{
        0.0f, 0.0f, static_cast<float>(kRtWidth), static_cast<float>(kRtHeight),
        0.0f, 1.0f};
    D3D12_RECT sc{0, 0, kRtWidth, kRtHeight};
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->OMSetRenderTargets(1, &g_Dx12Rtv, FALSE, &g_Dx12Dsv);
    cmd->SetGraphicsRootSignature(g_Dx12RootSig);

    PreviewCB cb{};
    ComputeMvpMatrix(cb.mvp);
    cmd->SetGraphicsRoot32BitConstants(0, 16, cb.mvp, 0);

    if (BlockTextureRegistry::IsReady()) {
      D3D12_GPU_DESCRIPTOR_HANDLE srvH{
          static_cast<UINT64>(BlockTextureRegistry::GetAtlasTexId())};
      cmd->SetGraphicsRootDescriptorTable(1, srvH);
    }

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (g_Dx12VbOpaque && g_Dx12OpaqueCount > 0) {
      cmd->SetPipelineState(g_Dx12PsoOpaque);
      cmd->IASetVertexBuffers(0, 1, &g_Dx12VbvOpaque);
      cmd->DrawInstanced(g_Dx12OpaqueCount, 1, 0, 0);
    }
    if (g_Dx12VbTrans && g_Dx12TransCount > 0) {
      cmd->SetPipelineState(g_Dx12PsoTrans);
      cmd->IASetVertexBuffers(0, 1, &g_Dx12VbvTrans);
      cmd->DrawInstanced(g_Dx12TransCount, 1, 0, 0);
    }
  }

  bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  cmd->ResourceBarrier(1, &bar);
}

bool HasMesh() { return g_HasMesh; }

int GetQuadCount() {
  return static_cast<int>((g_OpaqueVerts.size() + g_TransVerts.size()) / 6);
}

}
