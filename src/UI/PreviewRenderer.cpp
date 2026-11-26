#include "PreviewRenderer.hpp"
#include "../Core/SchematicDocument.hpp"
#include "../IO/BmetCodec.hpp"
#include "../IO/McStructureCodec.hpp"
#include "BlockTextureRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PreviewRenderer {

namespace {

struct PreviewQuad {
  float v[4][3];
  float cx, cy, cz;
  float nx, ny, nz;
  float u0, v0, u1, v1;
  uint8_t r, g, b;
  float shade;
  bool hasTex;
};

struct SortedQuad {
  float depth;
  ImVec2 pts[4];
  float u0, v0, u1, v1;
  ImU32 col;
  bool hasTex;
};

static std::string g_LoadedPath;
static bool g_HasMesh = false;
static float g_MaxDim = 1.0f;
static std::vector<PreviewQuad> g_Quads;

void BuildMeshFromDoc(const Bedrockmetica::SchematicDocument &doc) {
  g_Quads.clear();
  g_HasMesh = false;
  if (doc.width <= 0 || doc.height <= 0 || doc.depth <= 0 || doc.parts.empty())
    return;

  int64_t vol = static_cast<int64_t>(doc.width) * doc.height * doc.depth;
  int step = 1;
  if (vol > 250000)
    step = 3;
  else if (vol > 45000)
    step = 2;

  float halfW = static_cast<float>(doc.width) * 0.5f;
  float halfH = static_cast<float>(doc.height) * 0.5f;
  float halfD = static_cast<float>(doc.depth) * 0.5f;
  g_MaxDim = static_cast<float>(
      std::max({doc.width, doc.height, doc.depth, 4}));

  std::vector<BlockTextureRegistry::BlockFaces> palFaces(doc.palette.size());
  for (size_t i = 1; i < doc.palette.size(); ++i) {
    palFaces[i] = BlockTextureRegistry::ResolveBlockFaces(doc.palette[i].name);
  }

  auto getBlk = [&](int32_t x, int32_t y, int32_t z) -> uint16_t {
    if (x < 0 || y < 0 || z < 0 || x >= doc.width || y >= doc.height ||
        z >= doc.depth)
      return 0;
    return doc.GetBlock(0, x, y, z);
  };

  const float s = static_cast<float>(step);
  for (int32_t x = 0; x < doc.width; x += step) {
    for (int32_t y = 0; y < doc.height; y += step) {
      for (int32_t z = 0; z < doc.depth; z += step) {
        uint16_t pid = getBlk(x, y, z);
        if (pid == 0 || pid >= palFaces.size())
          continue;

        const auto &faces = palFaces[pid];
        uint32_t abgr = doc.palette[pid].approxAbgr;
        uint8_t cr = static_cast<uint8_t>(abgr & 0xFF);
        uint8_t cg = static_cast<uint8_t>((abgr >> 8) & 0xFF);
        uint8_t cb = static_cast<uint8_t>((abgr >> 16) & 0xFF);

        float bx0 = static_cast<float>(x) - halfW;
        float by0 = static_cast<float>(y) - halfH;
        float bz0 = static_cast<float>(z) - halfD;
        float bx1 = bx0 + s;
        float by1 = by0 + s;
        float bz1 = bz0 + s;

        auto addFace = [&](float x0, float y0, float z0, float x1, float y1,
                           float z1, float x2, float y2, float z2, float x3,
                           float y3, float z3, float nx, float ny, float nz,
                           float shade,
                           const BlockTextureRegistry::BlockFaceUVs &uv) {
          PreviewQuad q{};
          q.v[0][0] = x0; q.v[0][1] = y0; q.v[0][2] = z0;
          q.v[1][0] = x1; q.v[1][1] = y1; q.v[1][2] = z1;
          q.v[2][0] = x2; q.v[2][1] = y2; q.v[2][2] = z2;
          q.v[3][0] = x3; q.v[3][1] = y3; q.v[3][2] = z3;
          q.cx = (x0 + x2) * 0.5f;
          q.cy = (y0 + y2) * 0.5f;
          q.cz = (z0 + z2) * 0.5f;
          q.nx = nx;
          q.ny = ny;
          q.nz = nz;
          q.shade = shade;
          q.hasTex = uv.valid;
          q.u0 = uv.u0;
          q.v0 = uv.v0;
          q.u1 = uv.u1;
          q.v1 = uv.v1;
          q.r = cr;
          q.g = cg;
          q.b = cb;
          g_Quads.push_back(q);
        };

        if (getBlk(x, y + step, z) == 0) {
          addFace(bx0, by1, bz0, bx1, by1, bz0, bx1, by1, bz1, bx0, by1, bz1,
                  0.0f, 1.0f, 0.0f, 1.0f, faces.up);
        }
        if (getBlk(x, y - step, z) == 0) {
          addFace(bx0, by0, bz1, bx1, by0, bz1, bx1, by0, bz0, bx0, by0, bz0,
                  0.0f, -1.0f, 0.0f, 0.55f, faces.down);
        }
        if (getBlk(x, y, z + step) == 0) {
          addFace(bx0, by1, bz1, bx1, by1, bz1, bx1, by0, bz1, bx0, by0, bz1,
                  0.0f, 0.0f, 1.0f, 0.78f, faces.south);
        }
        if (getBlk(x, y, z - step) == 0) {
          addFace(bx1, by1, bz0, bx0, by1, bz0, bx0, by0, bz0, bx1, by0, bz0,
                  0.0f, 0.0f, -1.0f, 0.72f, faces.north);
        }
        if (getBlk(x + step, y, z) == 0) {
          addFace(bx1, by1, bz1, bx1, by1, bz0, bx1, by0, bz0, bx1, by0, bz1,
                  1.0f, 0.0f, 0.0f, 0.86f, faces.east);
        }
        if (getBlk(x - step, y, z) == 0) {
          addFace(bx0, by1, bz0, bx0, by1, bz1, bx0, by0, bz1, bx0, by0, bz0,
                  -1.0f, 0.0f, 0.0f, 0.66f, faces.west);
        }

        if (g_Quads.size() >= 18000)
          break;
      }
      if (g_Quads.size() >= 18000)
        break;
    }
    if (g_Quads.size() >= 18000)
      break;
  }
  g_HasMesh = !g_Quads.empty();
}

}

void SetStructurePath(const std::string &path, const std::string &format) {
  if (path == g_LoadedPath)
    return;
  g_LoadedPath = path;
  g_Quads.clear();
  g_HasMesh = false;
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
  g_LoadedPath.clear();
  g_Quads.clear();
  g_HasMesh = false;
}

void RenderViewport(ImDrawList *draw, ImVec2 vpPos, ImVec2 vpSize, float yaw,
                    float pitch, float zoom, float alpha) {
  if (!draw || !g_HasMesh || g_Quads.empty())
    return;

  float cosY = std::cos(yaw);
  float sinY = std::sin(yaw);
  float cosP = std::cos(pitch);
  float sinP = std::sin(pitch);

  float fit = std::min(vpSize.x, vpSize.y) * 0.68f / std::max(1.0f, g_MaxDim);
  float scale = fit * zoom;
  float cxScreen = vpPos.x + vpSize.x * 0.5f;
  float cyScreen = vpPos.y + vpSize.y * 0.52f;

  static std::vector<SortedQuad> sorted;
  sorted.clear();
  sorted.reserve(g_Quads.size() / 2);

  int a = static_cast<int>(255.0f * alpha);
  for (const auto &q : g_Quads) {
    float nrx = q.nx * cosY - q.nz * sinY;
    float nrz = q.nx * sinY + q.nz * cosY;
    float nry = q.ny * cosP - nrz * sinP;
    float nfz = q.ny * sinP + nrz * cosP;
    if (nfz <= 0.0f)
      continue;

    float crx = q.cx * cosY - q.cz * sinY;
    float crz = q.cx * sinY + q.cz * cosY;
    float cfz = q.cy * sinP + crz * cosP;

    SortedQuad sq;
    sq.depth = cfz;
    sq.u0 = q.u0;
    sq.v0 = q.v0;
    sq.u1 = q.u1;
    sq.v1 = q.v1;
    sq.hasTex = q.hasTex && BlockTextureRegistry::IsReady();

    for (int i = 0; i < 4; ++i) {
      float vx = q.v[i][0];
      float vy = q.v[i][1];
      float vz = q.v[i][2];
      float rx = vx * cosY - vz * sinY;
      float rz = vx * sinY + vz * cosY;
      float ry = vy * cosP - rz * sinP;
      sq.pts[i] = ImVec2(cxScreen + rx * scale, cyScreen - ry * scale);
    }

    if (sq.hasTex) {
      int sh = static_cast<int>(255.0f * q.shade);
      sq.col = IM_COL32(sh, sh, sh, a);
    } else {
      int rr = static_cast<int>(q.r * q.shade);
      int gg = static_cast<int>(q.g * q.shade);
      int bb = static_cast<int>(q.b * q.shade);
      sq.col = IM_COL32(rr, gg, bb, a);
    }
    sorted.push_back(sq);
  }

  std::sort(sorted.begin(), sorted.end(),
            [](const SortedQuad &a1, const SortedQuad &b1) {
              return a1.depth < b1.depth;
            });

  ImTextureID atlas = BlockTextureRegistry::GetAtlasTexId();
  for (const auto &sq : sorted) {
    if (sq.hasTex) {
      draw->AddImageQuad(atlas, sq.pts[0], sq.pts[1], sq.pts[2], sq.pts[3],
                         ImVec2(sq.u0, sq.v0), ImVec2(sq.u1, sq.v0),
                         ImVec2(sq.u1, sq.v1), ImVec2(sq.u0, sq.v1), sq.col);
    } else {
      draw->AddQuadFilled(sq.pts[0], sq.pts[1], sq.pts[2], sq.pts[3], sq.col);
    }
  }
}

bool HasMesh() { return g_HasMesh; }

int GetQuadCount() { return static_cast<int>(g_Quads.size()); }

}
