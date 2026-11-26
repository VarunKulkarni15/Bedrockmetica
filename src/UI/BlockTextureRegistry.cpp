#include "BlockTextureRegistry.hpp"
#include "../Core/SchematicDocument.hpp"
#include "../Utils/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <wincodec.h>

namespace BlockTextureRegistry {

namespace {

struct BrarchiveEntry {
  std::string name;
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct BrarchiveFile {
  std::filesystem::path path;
  uint64_t dataStart = 0;
  std::unordered_map<std::string, BrarchiveEntry> entries;
};

struct RawBlockTexSpec {
  std::string up;
  std::string down;
  std::string north;
  std::string south;
  std::string east;
  std::string west;
  std::string side;
  std::string all;
};

struct TerrainTexSpec {
  std::string path;
  uint32_t tintRgb = 0;
  bool hasTint = false;
};

static std::mutex g_RegMutex;
static bool g_CpuBuilt = false;
static bool g_GpuReady = false;
static std::vector<uint8_t> g_AtlasRgba;
static int g_NextSlot = 0;

static std::unordered_map<std::string, BlockFaceUVs> g_TileCache;
static std::unordered_map<std::string, RawBlockTexSpec> g_BlocksJsonMap;
static std::unordered_map<std::string, TerrainTexSpec> g_TerrainJsonMap;
static std::unordered_map<std::string, BlockFaces> g_ResolvedCache;

static ID3D11ShaderResourceView *g_Dx11Srv = nullptr;
static ID3D12Resource *g_Dx12Tex = nullptr;
static ImTextureID g_AtlasTexId = 0;

std::string ToLowerStr(const std::string &s) {
  std::string out = s;
  for (char &c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string StripJsonComments(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  bool inStr = false;
  bool esc = false;
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (inStr) {
      out.push_back(c);
      if (esc) {
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        inStr = false;
      }
      continue;
    }
    if (c == '"') {
      inStr = true;
      out.push_back(c);
      continue;
    }
    if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
      i += 2;
      while (i < in.size() && in[i] != '\n')
        ++i;
      out.push_back('\n');
      continue;
    }
    if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
      i += 2;
      while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/'))
        ++i;
      ++i;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

struct JsonCursor {
  const std::string &s;
  size_t p = 0;

  void SkipWs() {
    while (p < s.size() &&
           (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
      ++p;
  }

  bool Expect(char ch) {
    SkipWs();
    if (p < s.size() && s[p] == ch) {
      ++p;
      return true;
    }
    return false;
  }

  bool ParseString(std::string &out) {
    SkipWs();
    if (p >= s.size() || s[p] != '"')
      return false;
    ++p;
    out.clear();
    while (p < s.size()) {
      char c = s[p++];
      if (c == '"')
        return true;
      if (c == '\\' && p < s.size()) {
        char n = s[p++];
        out.push_back(n);
      } else {
        out.push_back(c);
      }
    }
    return false;
  }

  bool SkipValue(int depth = 0) {
    if (depth > 64)
      return false;
    SkipWs();
    if (p >= s.size())
      return false;
    char c = s[p];
    if (c == '"') {
      std::string dummy;
      return ParseString(dummy);
    }
    if (c == '{') {
      ++p;
      SkipWs();
      if (p < s.size() && s[p] == '}') {
        ++p;
        return true;
      }
      while (p < s.size()) {
        std::string k;
        if (!ParseString(k) || !Expect(':') || !SkipValue(depth + 1))
          return false;
        SkipWs();
        if (p < s.size() && s[p] == ',') {
          ++p;
          continue;
        }
        return Expect('}');
      }
      return false;
    }
    if (c == '[') {
      ++p;
      SkipWs();
      if (p < s.size() && s[p] == ']') {
        ++p;
        return true;
      }
      while (p < s.size()) {
        if (!SkipValue(depth + 1))
          return false;
        SkipWs();
        if (p < s.size() && s[p] == ',') {
          ++p;
          continue;
        }
        return Expect(']');
      }
      return false;
    }
    while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']' &&
           s[p] != ' ' && s[p] != '\r' && s[p] != '\n' && s[p] != '\t')
      ++p;
    return true;
  }
};

uint32_t ParseHexRgb(const std::string &hex) {
  if (hex.empty())
    return 0;
  std::string h = (hex[0] == '#') ? hex.substr(1) : hex;
  if (h.size() != 6)
    return 0;
  uint32_t val = 0;
  for (char c : h) {
    val <<= 4;
    if (c >= '0' && c <= '9')
      val |= static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f')
      val |= static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      val |= static_cast<uint32_t>(c - 'A' + 10);
  }
  return val;
}

bool ParseTerrainEntryValue(JsonCursor &jc, TerrainTexSpec &out) {
  jc.SkipWs();
  if (jc.p >= jc.s.size())
    return false;
  char c = jc.s[jc.p];
  if (c == '"') {
    return jc.ParseString(out.path);
  }
  if (c == '{') {
    jc.p++;
    jc.SkipWs();
    if (jc.p < jc.s.size() && jc.s[jc.p] == '}') {
      jc.p++;
      return true;
    }
    while (jc.p < jc.s.size()) {
      std::string k;
      if (!jc.ParseString(k) || !jc.Expect(':'))
        return false;
      if (k == "path") {
        if (!jc.ParseString(out.path))
          return false;
      } else if (k == "overlay_color") {
        std::string hex;
        if (!jc.ParseString(hex))
          return false;
        out.tintRgb = ParseHexRgb(hex);
        out.hasTint = true;
      } else {
        if (!jc.SkipValue(2))
          return false;
      }
      jc.SkipWs();
      if (jc.p < jc.s.size() && jc.s[jc.p] == ',') {
        jc.p++;
        continue;
      }
      return jc.Expect('}');
    }
    return false;
  }
  if (c == '[') {
    jc.p++;
    jc.SkipWs();
    if (jc.p < jc.s.size() && jc.s[jc.p] == ']') {
      jc.p++;
      return true;
    }
    bool gotFirst = false;
    while (jc.p < jc.s.size()) {
      if (!gotFirst) {
        if (!ParseTerrainEntryValue(jc, out))
          return false;
        gotFirst = true;
      } else {
        if (!jc.SkipValue(2))
          return false;
      }
      jc.SkipWs();
      if (jc.p < jc.s.size() && jc.s[jc.p] == ',') {
        jc.p++;
        continue;
      }
      return jc.Expect(']');
    }
    return false;
  }
  return jc.SkipValue(1);
}

void ParseTerrainTextureJson(const std::string &rawJson) {
  std::string clean = StripJsonComments(rawJson);
  JsonCursor jc{clean, 0};
  if (!jc.Expect('{'))
    return;
  while (jc.p < clean.size()) {
    std::string k;
    if (!jc.ParseString(k) || !jc.Expect(':'))
      return;
    if (k == "texture_data") {
      if (!jc.Expect('{'))
        return;
      jc.SkipWs();
      if (jc.p < clean.size() && clean[jc.p] == '}') {
        jc.p++;
      } else {
        while (jc.p < clean.size()) {
          std::string texKey;
          if (!jc.ParseString(texKey) || !jc.Expect(':') || !jc.Expect('{'))
            return;
          TerrainTexSpec spec;
          jc.SkipWs();
          if (jc.p < clean.size() && clean[jc.p] == '}') {
            jc.p++;
          } else {
            while (jc.p < clean.size()) {
              std::string fk;
              if (!jc.ParseString(fk) || !jc.Expect(':'))
                return;
              if (fk == "textures") {
                if (!ParseTerrainEntryValue(jc, spec))
                  return;
              } else {
                if (!jc.SkipValue(2))
                  return;
              }
              jc.SkipWs();
              if (jc.p < clean.size() && clean[jc.p] == ',') {
                jc.p++;
                continue;
              }
              if (!jc.Expect('}'))
                return;
              break;
            }
          }
          if (!spec.path.empty()) {
            g_TerrainJsonMap[ToLowerStr(texKey)] = std::move(spec);
          }
          jc.SkipWs();
          if (jc.p < clean.size() && clean[jc.p] == ',') {
            jc.p++;
            continue;
          }
          if (!jc.Expect('}'))
            return;
          break;
        }
      }
    } else {
      if (!jc.SkipValue(1))
        return;
    }
    jc.SkipWs();
    if (jc.p < clean.size() && clean[jc.p] == ',') {
      jc.p++;
      continue;
    }
    break;
  }
}

void ParseBlocksJson(const std::string &rawJson) {
  std::string clean = StripJsonComments(rawJson);
  JsonCursor jc{clean, 0};
  if (!jc.Expect('{'))
    return;
  while (jc.p < clean.size()) {
    std::string blkKey;
    if (!jc.ParseString(blkKey) || !jc.Expect(':'))
      return;
    jc.SkipWs();
    if (jc.p < clean.size() && clean[jc.p] == '{') {
      jc.p++;
      RawBlockTexSpec spec;
      jc.SkipWs();
      if (jc.p < clean.size() && clean[jc.p] == '}') {
        jc.p++;
      } else {
        while (jc.p < clean.size()) {
          std::string fk;
          if (!jc.ParseString(fk) || !jc.Expect(':'))
            return;
          if (fk == "textures") {
            jc.SkipWs();
            if (jc.p < clean.size() && clean[jc.p] == '"') {
              jc.ParseString(spec.all);
            } else if (jc.p < clean.size() && clean[jc.p] == '{') {
              jc.p++;
              jc.SkipWs();
              if (jc.p < clean.size() && clean[jc.p] == '}') {
                jc.p++;
              } else {
                while (jc.p < clean.size()) {
                  std::string dir;
                  std::string texName;
                  if (!jc.ParseString(dir) || !jc.Expect(':') ||
                      !jc.ParseString(texName)) {
                    if (!jc.SkipValue(3))
                      return;
                  } else {
                    if (dir == "up")
                      spec.up = texName;
                    else if (dir == "down")
                      spec.down = texName;
                    else if (dir == "north")
                      spec.north = texName;
                    else if (dir == "south")
                      spec.south = texName;
                    else if (dir == "east")
                      spec.east = texName;
                    else if (dir == "west")
                      spec.west = texName;
                    else if (dir == "side")
                      spec.side = texName;
                  }
                  jc.SkipWs();
                  if (jc.p < clean.size() && clean[jc.p] == ',') {
                    jc.p++;
                    continue;
                  }
                  if (!jc.Expect('}'))
                    return;
                  break;
                }
              }
            } else {
              if (!jc.SkipValue(2))
                return;
            }
          } else {
            if (!jc.SkipValue(2))
              return;
          }
          jc.SkipWs();
          if (jc.p < clean.size() && clean[jc.p] == ',') {
            jc.p++;
            continue;
          }
          if (!jc.Expect('}'))
            return;
          break;
        }
      }
      g_BlocksJsonMap[ToLowerStr(blkKey)] = std::move(spec);
    } else {
      if (!jc.SkipValue(1))
        return;
    }
    jc.SkipWs();
    if (jc.p < clean.size() && clean[jc.p] == ',') {
      jc.p++;
      continue;
    }
    break;
  }
}

std::vector<std::filesystem::path> FindVanillaPackDirs() {
  std::vector<std::filesystem::path> packs;
  wchar_t exeBuf[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) == 0)
    return packs;
  std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
  std::error_code ec;
  std::filesystem::path vanilla = exeDir / L"data" / L"resource_packs" / L"vanilla";
  if (std::filesystem::exists(vanilla, ec))
    packs.push_back(vanilla);
  return packs;
}

bool LoadBrarchiveIndex(const std::filesystem::path &path, BrarchiveFile &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  uint8_t hdr[16] = {};
  if (!f.read(reinterpret_cast<char *>(hdr), 16))
    return false;
  uint64_t magic = 0;
  uint32_t count = 0;
  std::memcpy(&magic, hdr, 8);
  std::memcpy(&count, hdr + 12, 4);
  if (magic != 0x200000001ULL || count == 0 || count > 20000)
    return false;

  std::vector<uint8_t> table(static_cast<size_t>(count) * 256);
  if (!f.read(reinterpret_cast<char *>(table.data()),
              static_cast<std::streamsize>(table.size())))
    return false;

  out.path = path;
  out.dataStart = 16ULL + static_cast<uint64_t>(count) * 256ULL;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t *rec = table.data() + static_cast<size_t>(i) * 256;
    uint8_t nameLen = rec[0];
    if (nameLen == 0 || nameLen > 240)
      continue;
    BrarchiveEntry e;
    e.name.assign(reinterpret_cast<const char *>(rec + 1), nameLen);
    std::memcpy(&e.offset, rec + 248, 4);
    std::memcpy(&e.size, rec + 252, 4);
    out.entries[ToLowerStr(e.name)] = std::move(e);
  }
  return true;
}

bool ReadTextFile(const std::filesystem::path &p, std::string &out) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f.is_open())
    return false;
  auto sz = f.tellg();
  if (sz <= 0 || sz > 16 * 1024 * 1024)
    return false;
  out.resize(static_cast<size_t>(sz));
  f.seekg(0, std::ios::beg);
  return static_cast<bool>(f.read(out.data(), sz));
}

bool DecodeImageRgba(IWICImagingFactory *wic, const std::vector<uint8_t> &raw,
                     int &outW, int &outH, std::vector<uint8_t> &outRgba) {
  if (raw.size() >= 8 && raw[0] == 0x89 && raw[1] == 'P' && raw[2] == 'N' &&
      raw[3] == 'G') {
    if (!wic)
      return false;
    IWICStream *stream = nullptr;
    if (FAILED(wic->CreateStream(&stream)))
      return false;
    HRESULT hr = stream->InitializeFromMemory(const_cast<BYTE *>(raw.data()),
                                              static_cast<DWORD>(raw.size()));
    IWICBitmapDecoder *decoder = nullptr;
    if (SUCCEEDED(hr))
      hr = wic->CreateDecoderFromStream(
          stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    IWICBitmapFrameDecode *frame = nullptr;
    if (SUCCEEDED(hr))
      hr = decoder->GetFrame(0, &frame);
    IWICBitmapSource *rgbaSrc = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr))
      hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame, &rgbaSrc);
    if (SUCCEEDED(hr))
      hr = rgbaSrc->GetSize(&w, &h);
    if (SUCCEEDED(hr) && w > 0 && h > 0 && w <= 2048 && h <= 8192) {
      outW = static_cast<int>(w);
      outH = static_cast<int>(h);
      outRgba.resize(static_cast<size_t>(w) * h * 4);
      hr = rgbaSrc->CopyPixels(nullptr, w * 4,
                               static_cast<UINT>(outRgba.size()),
                               outRgba.data());
    } else {
      hr = E_FAIL;
    }
    if (rgbaSrc)
      rgbaSrc->Release();
    if (frame)
      frame->Release();
    if (decoder)
      decoder->Release();
    stream->Release();
    return SUCCEEDED(hr);
  }
  if (raw.size() >= 18) {
    uint8_t imgType = raw[2];
    uint16_t w = 0, h = 0;
    std::memcpy(&w, raw.data() + 12, 2);
    std::memcpy(&h, raw.data() + 14, 2);
    uint8_t bpp = raw[16];
    uint8_t desc = raw[17];
    if (imgType == 2 && (bpp == 24 || bpp == 32) && w > 0 && h > 0 &&
        w <= 512 && h <= 2048) {
      size_t offset = 18 + raw[0];
      size_t bytesPerPx = bpp / 8;
      if (offset + static_cast<size_t>(w) * h * bytesPerPx <= raw.size()) {
        outW = w;
        outH = h;
        outRgba.resize(static_cast<size_t>(w) * h * 4);
        bool topOrigin = (desc & 0x20) != 0;
        for (int y = 0; y < h; ++y) {
          int srcY = topOrigin ? y : (h - 1 - y);
          for (int x = 0; x < w; ++x) {
            const uint8_t *sp = raw.data() + offset +
                                (static_cast<size_t>(srcY) * w + x) * bytesPerPx;
            uint8_t *dp = outRgba.data() + (static_cast<size_t>(y) * w + x) * 4;
            dp[0] = sp[2];
            dp[1] = sp[1];
            dp[2] = sp[0];
            dp[3] = (bpp == 32) ? sp[3] : 255;
          }
        }
        return true;
      }
    }
  }
  return false;
}

static std::vector<BrarchiveFile> g_Archives;
static std::vector<std::filesystem::path> g_Packs;
static IWICImagingFactory *g_Wic = nullptr;

BlockFaceUVs AllocAtlasTile(const std::string &texPathRaw, uint32_t tintRgb,
                            bool hasTint) {
  std::string key = ToLowerStr(texPathRaw);
  if (hasTint)
    key += "#tint";
  auto it = g_TileCache.find(key);
  if (it != g_TileCache.end())
    return it->second;

  if (g_NextSlot >= kTilesPerRow * kTilesPerRow)
    return {};

  std::string baseName = texPathRaw;
  auto slashPos = baseName.find_last_of('/');
  if (slashPos != std::string::npos)
    baseName = baseName.substr(slashPos + 1);
  baseName = ToLowerStr(baseName);

  std::vector<uint8_t> rawBytes;
  for (const std::string &ext : {".png", ".tga"}) {
    std::string fname = baseName + ext;
    for (const auto &arc : g_Archives) {
      auto eit = arc.entries.find(fname);
      if (eit != arc.entries.end() && eit->second.size > 0 &&
          eit->second.size < 4 * 1024 * 1024) {
        std::ifstream f(arc.path, std::ios::binary);
        if (f.is_open()) {
          f.seekg(static_cast<std::streamoff>(arc.dataStart + eit->second.offset),
                  std::ios::beg);
          rawBytes.resize(eit->second.size);
          if (!f.read(reinterpret_cast<char *>(rawBytes.data()),
                      eit->second.size)) {
            rawBytes.clear();
          } else {
            break;
          }
        }
      }
    }
    if (!rawBytes.empty())
      break;
    std::error_code ec;
    for (const auto &pack : g_Packs) {
      std::filesystem::path loose = pack / L"textures" / L"blocks" / fname;
      if (std::filesystem::exists(loose, ec)) {
        std::ifstream f(loose, std::ios::binary | std::ios::ate);
        auto sz = f.tellg();
        if (sz > 0 && sz < 4 * 1024 * 1024) {
          rawBytes.resize(static_cast<size_t>(sz));
          f.seekg(0, std::ios::beg);
          if (f.read(reinterpret_cast<char *>(rawBytes.data()), sz))
            break;
          rawBytes.clear();
        }
      }
    }
    if (!rawBytes.empty())
      break;
  }

  if (rawBytes.empty()) {
    g_TileCache[key] = {};
    return {};
  }

  int sw = 0, sh = 0;
  std::vector<uint8_t> rgba;
  if (!DecodeImageRgba(g_Wic, rawBytes, sw, sh, rgba) || sw <= 0 || sh <= 0) {
    g_TileCache[key] = {};
    return {};
  }

  int slot = g_NextSlot++;
  int tx = slot % kTilesPerRow;
  int ty = slot / kTilesPerRow;

  int frameH = (sh > sw && (sh % sw) == 0) ? sw : sh;
  float tr = hasTint ? static_cast<float>((tintRgb >> 16) & 0xFF) / 255.0f : 1.0f;
  float tg = hasTint ? static_cast<float>((tintRgb >> 8) & 0xFF) / 255.0f : 1.0f;
  float tb = hasTint ? static_cast<float>(tintRgb & 0xFF) / 255.0f : 1.0f;

  for (int y = 0; y < kTileSize; ++y) {
    int sy = std::min(frameH - 1, (y * frameH) / kTileSize);
    for (int x = 0; x < kTileSize; ++x) {
      int sx = std::min(sw - 1, (x * sw) / kTileSize);
      const uint8_t *sp = rgba.data() + (static_cast<size_t>(sy) * sw + sx) * 4;
      size_t dstIdx =
          (static_cast<size_t>(ty * kTileSize + y) * kAtlasSize +
           static_cast<size_t>(tx * kTileSize + x)) *
          4;
      g_AtlasRgba[dstIdx + 0] =
          static_cast<uint8_t>(std::clamp(sp[0] * tr, 0.0f, 255.0f));
      g_AtlasRgba[dstIdx + 1] =
          static_cast<uint8_t>(std::clamp(sp[1] * tg, 0.0f, 255.0f));
      g_AtlasRgba[dstIdx + 2] =
          static_cast<uint8_t>(std::clamp(sp[2] * tb, 0.0f, 255.0f));
      g_AtlasRgba[dstIdx + 3] = sp[3];
    }
  }

  BlockFaceUVs uv;
  uv.u0 = static_cast<float>(tx * kTileSize) / static_cast<float>(kAtlasSize);
  uv.v0 = static_cast<float>(ty * kTileSize) / static_cast<float>(kAtlasSize);
  uv.u1 =
      static_cast<float>((tx + 1) * kTileSize) / static_cast<float>(kAtlasSize);
  uv.v1 =
      static_cast<float>((ty + 1) * kTileSize) / static_cast<float>(kAtlasSize);
  uv.valid = true;
  g_TileCache[key] = uv;
  return uv;
}

void BuildCpuAtlasOnce() {
  if (g_CpuBuilt)
    return;
  g_CpuBuilt = true;
  g_AtlasRgba.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 0);

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                   IID_PPV_ARGS(&g_Wic));

  g_Packs = FindVanillaPackDirs();
  std::error_code ec;
  for (const auto &pack : g_Packs) {
    std::string bJson, tJson;
    if (ReadTextFile(pack / L"blocks.json", bJson))
      ParseBlocksJson(bJson);
    if (ReadTextFile(pack / L"textures" / L"terrain_texture.json", tJson))
      ParseTerrainTextureJson(tJson);

    std::filesystem::path texDir = pack / L"__brarchive" / L"textures";
    if (std::filesystem::exists(texDir, ec)) {
      BrarchiveFile mainBf;
      if (LoadBrarchiveIndex(texDir / L"blocks.brarchive", mainBf))
        g_Archives.push_back(std::move(mainBf));
      std::filesystem::path subBlocks = texDir / L"blocks";
      if (std::filesystem::exists(subBlocks, ec)) {
        for (const auto &f : std::filesystem::directory_iterator(subBlocks, ec)) {
          if (f.is_regular_file(ec) && f.path().extension() == L".brarchive") {
            BrarchiveFile subBf;
            if (LoadBrarchiveIndex(f.path(), subBf))
              g_Archives.push_back(std::move(subBf));
          }
        }
      }
    }
  }

  for (const auto &kv : g_TerrainJsonMap) {
    if (g_NextSlot >= kTilesPerRow * kTilesPerRow)
      break;
    bool tint = kv.second.hasTint || (kv.first == "grass_top") ||
                (kv.first.find("leaves") != std::string::npos);
    uint32_t rgb = kv.second.hasTint ? kv.second.tintRgb : 0x79C05A;
    AllocAtlasTile(kv.second.path, rgb, tint);
  }

  Logger::info("BlockTextureRegistry built atlas (" +
                   std::to_string(g_NextSlot) + " tiles loaded)",
               "Textures");
}

BlockFaceUVs ResolveTerrainKey(const std::string &texKey) {
  if (texKey.empty())
    return {};
  std::string k = ToLowerStr(texKey);
  auto tit = g_TerrainJsonMap.find(k);
  if (tit != g_TerrainJsonMap.end()) {
    bool tint = tit->second.hasTint || (k == "grass_top") ||
                (k.find("leaves") != std::string::npos);
    uint32_t rgb = tit->second.hasTint ? tit->second.tintRgb : 0x79C05A;
    return AllocAtlasTile(tit->second.path, rgb, tint);
  }
  return AllocAtlasTile(k, 0, false);
}

std::string NormalizeBlockAlias(const std::string &id) {
  std::string s = ToLowerStr(id);
  if (s.rfind("minecraft:", 0) == 0)
    s = s.substr(10);
  if (s == "grass_block")
    return "grass";
  if (s == "oak_planks" || s == "spruce_planks" || s == "birch_planks" ||
      s == "jungle_planks" || s == "acacia_planks" || s == "dark_oak_planks")
    return "planks";
  if (s == "stone_bricks" || s == "mossy_stone_bricks" ||
      s == "cracked_stone_bricks" || s == "chiseled_stone_bricks")
    return "stonebrick";
  if (s == "oak_log")
    return "log";
  if (s == "dark_oak_stairs")
    return "dark_oak_stairs";
  if (s == "glass_pane")
    return "glass_pane";
  return s;
}

}

void EnsureDX11(ID3D11Device *device) {
  std::lock_guard<std::mutex> lock(g_RegMutex);
  if (!device || g_GpuReady)
    return;
  BuildCpuAtlasOnce();

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kAtlasSize;
  desc.Height = kAtlasSize;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA init = {};
  init.pSysMem = g_AtlasRgba.data();
  init.SysMemPitch = kAtlasSize * 4;

  ID3D11Texture2D *tex = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &init, &tex)))
    return;
  HRESULT hr = device->CreateShaderResourceView(tex, nullptr, &g_Dx11Srv);
  tex->Release();
  if (SUCCEEDED(hr) && g_Dx11Srv) {
    g_AtlasTexId = reinterpret_cast<ImTextureID>(g_Dx11Srv);
    g_GpuReady = true;
  }
}

void EnsureDX12(ID3D12Device *device, ID3D12CommandQueue *queue,
                UiAssets::SrvAllocFn allocFn) {
  std::lock_guard<std::mutex> lock(g_RegMutex);
  if (!device || !queue || !allocFn || g_GpuReady)
    return;
  BuildCpuAtlasOnce();

  ID3D12CommandAllocator *alloc = nullptr;
  ID3D12GraphicsCommandList *cmd = nullptr;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc))))
    return;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc,
                                       nullptr, IID_PPV_ARGS(&cmd)))) {
    alloc->Release();
    return;
  }

  D3D12_HEAP_PROPERTIES defHeap = {};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = kAtlasSize;
  texDesc.Height = kAtlasSize;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  if (FAILED(device->CreateCommittedResource(
          &defHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_Dx12Tex)))) {
    cmd->Release();
    alloc->Release();
    return;
  }

  UINT64 uploadBytes = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr,
                                &uploadBytes);

  D3D12_HEAP_PROPERTIES upHeap = {};
  upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC upDesc = {};
  upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  upDesc.Width = uploadBytes;
  upDesc.Height = 1;
  upDesc.DepthOrArraySize = 1;
  upDesc.MipLevels = 1;
  upDesc.SampleDesc.Count = 1;
  upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ID3D12Resource *uploadBuf = nullptr;
  if (FAILED(device->CreateCommittedResource(
          &upHeap, D3D12_HEAP_FLAG_NONE, &upDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&uploadBuf)))) {
    g_Dx12Tex->Release();
    g_Dx12Tex = nullptr;
    cmd->Release();
    alloc->Release();
    return;
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE rr{0, 0};
  if (SUCCEEDED(uploadBuf->Map(0, &rr, reinterpret_cast<void **>(&mapped)))) {
    for (int row = 0; row < kAtlasSize; ++row) {
      std::memcpy(mapped + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                  g_AtlasRgba.data() + static_cast<size_t>(row) * kAtlasSize * 4,
                  kAtlasSize * 4);
    }
    uploadBuf->Unmap(0, nullptr);
  }

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = g_Dx12Tex;
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = uploadBuf;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;

  cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER bar = {};
  bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bar.Transition.pResource = g_Dx12Tex;
  bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmd->ResourceBarrier(1, &bar);

  cmd->Close();
  ID3D12CommandList *lists[] = {cmd};
  queue->ExecuteCommandLists(1, lists);

  ID3D12Fence *fence = nullptr;
  if (SUCCEEDED(
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence, 1);
    if (fence->GetCompletedValue() < 1 && ev) {
      fence->SetEventOnCompletion(1, ev);
      WaitForSingleObject(ev, 5000);
    }
    if (ev)
      CloseHandle(ev);
    fence->Release();
  }

  uploadBuf->Release();
  cmd->Release();
  alloc->Release();

  D3D12_CPU_DESCRIPTOR_HANDLE cpuH{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpuH{};
  allocFn(&cpuH, &gpuH);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(g_Dx12Tex, &srvDesc, cpuH);

  g_AtlasTexId = static_cast<ImTextureID>(gpuH.ptr);
  g_GpuReady = true;
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_RegMutex);
  if (g_Dx11Srv) {
    g_Dx11Srv->Release();
    g_Dx11Srv = nullptr;
  }
  if (g_Dx12Tex) {
    g_Dx12Tex->Release();
    g_Dx12Tex = nullptr;
  }
  if (g_Wic) {
    g_Wic->Release();
    g_Wic = nullptr;
  }
  g_AtlasTexId = 0;
  g_GpuReady = false;
}

bool IsReady() { return g_GpuReady && g_AtlasTexId != 0; }

ImTextureID GetAtlasTexId() { return g_AtlasTexId; }

const uint8_t *GetAtlasRgbaData() {
  return g_AtlasRgba.empty() ? nullptr : g_AtlasRgba.data();
}

BlockFaces ResolveBlockFaces(const std::string &rawBlockId) {
  std::lock_guard<std::mutex> lock(g_RegMutex);
  std::string norm = NormalizeBlockAlias(rawBlockId);
  auto cit = g_ResolvedCache.find(norm);
  if (cit != g_ResolvedCache.end())
    return cit->second;

  BlockFaces res;
  res.avgAbgr = Bedrockmetica::ResolveBlockApproxColor(rawBlockId);

  std::string rawShort = ToLowerStr(rawBlockId);
  if (rawShort.rfind("minecraft:", 0) == 0)
    rawShort = rawShort.substr(10);

  auto bit = g_BlocksJsonMap.find(rawShort);
  if (bit == g_BlocksJsonMap.end())
    bit = g_BlocksJsonMap.find(norm);

  if (bit != g_BlocksJsonMap.end()) {
    const auto &sp = bit->second;
    BlockFaceUVs allUv = ResolveTerrainKey(sp.all);
    BlockFaceUVs sideUv =
        sp.side.empty() ? allUv : ResolveTerrainKey(sp.side);
    res.up = sp.up.empty() ? allUv : ResolveTerrainKey(sp.up);
    res.down = sp.down.empty() ? allUv : ResolveTerrainKey(sp.down);
    res.north = sp.north.empty() ? sideUv : ResolveTerrainKey(sp.north);
    res.south = sp.south.empty() ? sideUv : ResolveTerrainKey(sp.south);
    res.east = sp.east.empty() ? sideUv : ResolveTerrainKey(sp.east);
    res.west = sp.west.empty() ? sideUv : ResolveTerrainKey(sp.west);
  }

  if (!res.up.valid) {
    BlockFaceUVs direct = ResolveTerrainKey(rawShort);
    if (!direct.valid)
      direct = ResolveTerrainKey(norm);
    if (direct.valid) {
      res.up = res.down = res.north = res.south = res.east = res.west = direct;
    }
  }

  g_ResolvedCache[norm] = res;
  return res;
}

}
