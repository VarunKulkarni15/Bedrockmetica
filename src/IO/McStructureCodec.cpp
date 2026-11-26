#include "McStructureCodec.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace Bedrockmetica::McStructureCodec {

namespace {

enum TagType : uint8_t {
  TAG_End = 0,
  TAG_Byte = 1,
  TAG_Short = 2,
  TAG_Int = 3,
  TAG_Long = 4,
  TAG_Float = 5,
  TAG_Double = 6,
  TAG_Byte_Array = 7,
  TAG_String = 8,
  TAG_List = 9,
  TAG_Compound = 10,
  TAG_Int_Array = 11,
  TAG_Long_Array = 12
};

struct NbtReader {
  const uint8_t *data = nullptr;
  size_t size = 0;
  size_t pos = 0;

  bool CanRead(size_t n) const { return pos <= size && n <= (size - pos); }

  bool ReadU8(uint8_t &out) {
    if (!CanRead(1))
      return false;
    out = data[pos++];
    return true;
  }

  bool ReadI16(int16_t &out) {
    if (!CanRead(2))
      return false;
    uint16_t v = static_cast<uint16_t>(data[pos]) |
                 (static_cast<uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    out = static_cast<int16_t>(v);
    return true;
  }

  bool ReadU16(uint16_t &out) {
    if (!CanRead(2))
      return false;
    out = static_cast<uint16_t>(data[pos]) |
          (static_cast<uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    return true;
  }

  bool ReadI32(int32_t &out) {
    if (!CanRead(4))
      return false;
    uint32_t v = static_cast<uint32_t>(data[pos]) |
                 (static_cast<uint32_t>(data[pos + 1]) << 8) |
                 (static_cast<uint32_t>(data[pos + 2]) << 16) |
                 (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    out = static_cast<int32_t>(v);
    return true;
  }

  bool ReadString(std::string &out) {
    uint16_t len = 0;
    if (!ReadU16(len))
      return false;
    if (!CanRead(len))
      return false;
    out.assign(reinterpret_cast<const char *>(data + pos), len);
    pos += len;
    return true;
  }

  bool SkipBytes(size_t n) {
    if (!CanRead(n))
      return false;
    pos += n;
    return true;
  }

  bool SkipTagPayload(uint8_t type, int depth = 0) {
    if (depth > 64)
      return false;
    switch (type) {
    case TAG_End:
      return true;
    case TAG_Byte:
      return SkipBytes(1);
    case TAG_Short:
      return SkipBytes(2);
    case TAG_Int:
    case TAG_Float:
      return SkipBytes(4);
    case TAG_Long:
    case TAG_Double:
      return SkipBytes(8);
    case TAG_Byte_Array: {
      int32_t len = 0;
      if (!ReadI32(len) || len < 0)
        return false;
      return SkipBytes(static_cast<size_t>(len));
    }
    case TAG_String: {
      uint16_t len = 0;
      if (!ReadU16(len))
        return false;
      return SkipBytes(len);
    }
    case TAG_List: {
      uint8_t elemType = 0;
      int32_t count = 0;
      if (!ReadU8(elemType) || !ReadI32(count) || count < 0)
        return false;
      for (int32_t i = 0; i < count; ++i) {
        if (!SkipTagPayload(elemType, depth + 1))
          return false;
      }
      return true;
    }
    case TAG_Compound: {
      while (true) {
        uint8_t childType = 0;
        if (!ReadU8(childType))
          return false;
        if (childType == TAG_End)
          return true;
        std::string childName;
        if (!ReadString(childName))
          return false;
        if (!SkipTagPayload(childType, depth + 1))
          return false;
      }
    }
    case TAG_Int_Array: {
      int32_t len = 0;
      if (!ReadI32(len) || len < 0)
        return false;
      return SkipBytes(static_cast<size_t>(len) * 4);
    }
    case TAG_Long_Array: {
      int32_t len = 0;
      if (!ReadI32(len) || len < 0)
        return false;
      return SkipBytes(static_cast<size_t>(len) * 8);
    }
    default:
      return false;
    }
  }
};

struct NbtWriter {
  std::vector<uint8_t> buf;

  void WriteU8(uint8_t v) { buf.push_back(v); }

  void WriteU16(uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  }

  void WriteI32(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    buf.push_back(static_cast<uint8_t>(u & 0xFF));
    buf.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
  }

  void WriteString(const std::string &s) {
    uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 65535));
    WriteU16(len);
    buf.insert(buf.end(), s.begin(), s.begin() + len);
  }

  void WriteTagHeader(uint8_t type, const std::string &name) {
    WriteU8(type);
    WriteString(name);
  }
};

bool ParseStatesCompound(NbtReader &r,
                         std::vector<BlockStateProp> &outStates) {
  while (true) {
    uint8_t t = 0;
    if (!r.ReadU8(t))
      return false;
    if (t == TAG_End)
      return true;
    std::string k;
    if (!r.ReadString(k))
      return false;
    if (t == TAG_Byte) {
      uint8_t v = 0;
      if (!r.ReadU8(v))
        return false;
      BlockStateProp p;
      p.key = std::move(k);
      p.type = BlockStateType::Byte;
      p.intVal = static_cast<int32_t>(v);
      outStates.push_back(std::move(p));
    } else if (t == TAG_Int) {
      int32_t v = 0;
      if (!r.ReadI32(v))
        return false;
      BlockStateProp p;
      p.key = std::move(k);
      p.type = BlockStateType::Int;
      p.intVal = v;
      outStates.push_back(std::move(p));
    } else if (t == TAG_String) {
      std::string v;
      if (!r.ReadString(v))
        return false;
      BlockStateProp p;
      p.key = std::move(k);
      p.type = BlockStateType::String;
      p.strVal = std::move(v);
      outStates.push_back(std::move(p));
    } else {
      if (!r.SkipTagPayload(t, 1))
        return false;
    }
  }
}

struct RawMcPaletteEntry {
  std::string name;
  std::vector<BlockStateProp> states;
};

bool ParseBlockPaletteList(NbtReader &r,
                           std::vector<RawMcPaletteEntry> &outPal) {
  uint8_t elemType = 0;
  int32_t count = 0;
  if (!r.ReadU8(elemType) || !r.ReadI32(count) || count < 0)
    return false;
  if (count == 0)
    return true;
  if (elemType != TAG_Compound) {
    for (int32_t i = 0; i < count; ++i) {
      if (!r.SkipTagPayload(elemType, 1))
        return false;
    }
    return true;
  }

  outPal.reserve(static_cast<size_t>(count));
  for (int32_t i = 0; i < count; ++i) {
    RawMcPaletteEntry entry;
    while (true) {
      uint8_t t = 0;
      if (!r.ReadU8(t))
        return false;
      if (t == TAG_End)
        break;
      std::string k;
      if (!r.ReadString(k))
        return false;
      if (t == TAG_String && k == "name") {
        if (!r.ReadString(entry.name))
          return false;
      } else if (t == TAG_Compound && k == "states") {
        if (!ParseStatesCompound(r, entry.states))
          return false;
      } else {
        if (!r.SkipTagPayload(t, 2))
          return false;
      }
    }
    outPal.push_back(std::move(entry));
  }
  return true;
}

bool ParsePaletteDefaultCompound(NbtReader &r,
                                 std::vector<RawMcPaletteEntry> &outPal) {
  while (true) {
    uint8_t t = 0;
    if (!r.ReadU8(t))
      return false;
    if (t == TAG_End)
      return true;
    std::string k;
    if (!r.ReadString(k))
      return false;
    if (t == TAG_List && k == "block_palette") {
      if (!ParseBlockPaletteList(r, outPal))
        return false;
    } else {
      if (!r.SkipTagPayload(t, 2))
        return false;
    }
  }
}

bool ParsePaletteCompound(NbtReader &r,
                          std::vector<RawMcPaletteEntry> &outPal) {
  while (true) {
    uint8_t t = 0;
    if (!r.ReadU8(t))
      return false;
    if (t == TAG_End)
      return true;
    std::string k;
    if (!r.ReadString(k))
      return false;
    if (t == TAG_Compound && k == "default") {
      if (!ParsePaletteDefaultCompound(r, outPal))
        return false;
    } else {
      if (!r.SkipTagPayload(t, 2))
        return false;
    }
  }
}

bool ParseBlockIndicesList(NbtReader &r, std::vector<int32_t> &outLayer0) {
  uint8_t listElemType = 0;
  int32_t layerCount = 0;
  if (!r.ReadU8(listElemType) || !r.ReadI32(layerCount) || layerCount < 0)
    return false;

  for (int32_t layer = 0; layer < layerCount; ++layer) {
    if (listElemType == TAG_List) {
      uint8_t innerType = 0;
      int32_t count = 0;
      if (!r.ReadU8(innerType) || !r.ReadI32(count) || count < 0)
        return false;
      if (layer == 0 && innerType == TAG_Int) {
        outLayer0.resize(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
          if (!r.ReadI32(outLayer0[static_cast<size_t>(i)]))
            return false;
        }
      } else {
        for (int32_t i = 0; i < count; ++i) {
          if (!r.SkipTagPayload(innerType, 2))
            return false;
        }
      }
    } else if (listElemType == TAG_Int_Array) {
      int32_t count = 0;
      if (!r.ReadI32(count) || count < 0)
        return false;
      if (layer == 0) {
        outLayer0.resize(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
          if (!r.ReadI32(outLayer0[static_cast<size_t>(i)]))
            return false;
        }
      } else {
        if (!r.SkipBytes(static_cast<size_t>(count) * 4))
          return false;
      }
    } else {
      if (!r.SkipTagPayload(listElemType, 2))
        return false;
    }
  }
  return true;
}

bool ParseStructureCompound(NbtReader &r, std::vector<int32_t> &outLayer0,
                            std::vector<RawMcPaletteEntry> &outPal) {
  while (true) {
    uint8_t t = 0;
    if (!r.ReadU8(t))
      return false;
    if (t == TAG_End)
      return true;
    std::string k;
    if (!r.ReadString(k))
      return false;
    if (t == TAG_List && k == "block_indices") {
      if (!ParseBlockIndicesList(r, outLayer0))
        return false;
    } else if (t == TAG_Compound && k == "palette") {
      if (!ParsePaletteCompound(r, outPal))
        return false;
    } else {
      if (!r.SkipTagPayload(t, 2))
        return false;
    }
  }
}

bool ParseInt3List(NbtReader &r, int32_t out3[3]) {
  uint8_t elemType = 0;
  int32_t count = 0;
  if (!r.ReadU8(elemType) || !r.ReadI32(count) || count < 0)
    return false;
  if (elemType != TAG_Int || count != 3) {
    for (int32_t i = 0; i < count; ++i) {
      if (!r.SkipTagPayload(elemType, 2))
        return false;
    }
    return false;
  }
  return r.ReadI32(out3[0]) && r.ReadI32(out3[1]) && r.ReadI32(out3[2]);
}

bool ParseMcStructureBytes(const std::vector<uint8_t> &bytes, int32_t outSize[3],
                           int32_t outOrigin[3],
                           std::vector<int32_t> &outLayer0,
                           std::vector<RawMcPaletteEntry> &outPal) {
  outSize[0] = outSize[1] = outSize[2] = 0;
  outOrigin[0] = outOrigin[1] = outOrigin[2] = 0;

  NbtReader r{bytes.data(), bytes.size(), 0};
  uint8_t rootType = 0;
  if (!r.ReadU8(rootType) || rootType != TAG_Compound)
    return false;
  std::string rootName;
  if (!r.ReadString(rootName))
    return false;

  while (true) {
    uint8_t t = 0;
    if (!r.ReadU8(t))
      return false;
    if (t == TAG_End)
      break;
    std::string k;
    if (!r.ReadString(k))
      return false;
    if (t == TAG_List && k == "size") {
      if (!ParseInt3List(r, outSize))
        return false;
    } else if (t == TAG_List && k == "structure_world_origin") {
      if (!ParseInt3List(r, outOrigin))
        return false;
    } else if (t == TAG_Compound && k == "structure") {
      if (!ParseStructureCompound(r, outLayer0, outPal))
        return false;
    } else {
      if (!r.SkipTagPayload(t, 1))
        return false;
    }
  }

  return outSize[0] > 0 && outSize[1] > 0 && outSize[2] > 0;
}

bool ReadFileBytes(const std::string &path, std::vector<uint8_t> &outBytes) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open())
    return false;
  std::streamsize sz = f.tellg();
  if (sz <= 0 || sz > 256 * 1024 * 1024)
    return false;
  outBytes.resize(static_cast<size_t>(sz));
  f.seekg(0, std::ios::beg);
  return static_cast<bool>(
      f.read(reinterpret_cast<char *>(outBytes.data()), sz));
}

}

bool ReadSummary(const std::string &path, BmetCodec::BmetSummary &outSummary) {
  outSummary = {};
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(path, bytes))
    return false;

  int32_t sz[3]{};
  int32_t origin[3]{};
  std::vector<int32_t> layer0;
  std::vector<RawMcPaletteEntry> pal;
  if (!ParseMcStructureBytes(bytes, sz, origin, layer0, pal))
    return false;

  outSummary.valid = true;
  outSummary.name = std::filesystem::path(path).stem().string();
  outSummary.width = sz[0];
  outSummary.height = sz[1];
  outSummary.depth = sz[2];

  std::unordered_map<std::string, MaterialCount> tallyMap;
  int32_t nonAir = 0;
  for (int32_t idx : layer0) {
    if (idx < 0 || static_cast<size_t>(idx) >= pal.size())
      continue;
    const std::string &blkName = pal[static_cast<size_t>(idx)].name;
    if (blkName.empty() || blkName == "minecraft:air")
      continue;
    nonAir++;
    auto &mc = tallyMap[blkName];
    if (mc.rawId.empty()) {
      mc.rawId = blkName;
      mc.displayName = FormatBlockDisplayName(blkName);
      mc.approxAbgr = ResolveBlockApproxColor(blkName);
    }
    mc.required++;
  }

  outSummary.totalBlocks = nonAir;
  std::vector<MaterialCount> list;
  list.reserve(tallyMap.size());
  for (auto &kv : tallyMap) {
    list.push_back(std::move(kv.second));
  }
  std::sort(list.begin(), list.end(),
            [](const MaterialCount &a, const MaterialCount &b) {
              return a.required > b.required;
            });
  if (list.size() > 24)
    list.resize(24);
  outSummary.topMaterials = std::move(list);
  return true;
}

bool LoadFile(const std::string &path, SchematicDocument &outDoc) {
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(path, bytes))
    return false;

  int32_t sz[3]{};
  int32_t origin[3]{};
  std::vector<int32_t> layer0;
  std::vector<RawMcPaletteEntry> pal;
  if (!ParseMcStructureBytes(bytes, sz, origin, layer0, pal))
    return false;

  std::string docName = std::filesystem::path(path).stem().string();
  outDoc.Reset(docName);
  outDoc.sourcePath = path;
  outDoc.width = sz[0];
  outDoc.height = sz[1];
  outDoc.depth = sz[2];
  outDoc.originX = origin[0];
  outDoc.originY = origin[1];
  outDoc.originZ = origin[2];

  std::vector<uint16_t> palRemap(pal.size(), 0);
  for (size_t i = 0; i < pal.size(); ++i) {
    if (!pal[i].name.empty() && pal[i].name != "minecraft:air") {
      palRemap[i] = outDoc.InternBlock(pal[i].name, pal[i].states);
    }
  }

  size_t expected = static_cast<size_t>(sz[0]) * static_cast<size_t>(sz[1]) *
                    static_cast<size_t>(sz[2]);
  size_t limit = std::min(expected, layer0.size());
  for (size_t i = 0; i < limit; ++i) {
    int32_t rawIdx = layer0[i];
    if (rawIdx < 0 || static_cast<size_t>(rawIdx) >= palRemap.size())
      continue;
    uint16_t pid = palRemap[static_cast<size_t>(rawIdx)];
    if (pid == 0)
      continue;
    int32_t lz = static_cast<int32_t>(i % static_cast<size_t>(sz[2]));
    size_t rem = i / static_cast<size_t>(sz[2]);
    int32_t ly = static_cast<int32_t>(rem % static_cast<size_t>(sz[1]));
    int32_t lx = static_cast<int32_t>(rem / static_cast<size_t>(sz[1]));
    outDoc.SetBlock(0, lx, ly, lz, pid);
  }

  outDoc.RecomputeStats();
  return true;
}

bool SaveFile(const std::string &path, const SchematicDocument &doc) {
  if (doc.width <= 0 || doc.height <= 0 || doc.depth <= 0)
    return false;

  NbtWriter w;
  w.WriteTagHeader(TAG_Compound, "");

  w.WriteTagHeader(TAG_Int, "format_version");
  w.WriteI32(1);

  w.WriteTagHeader(TAG_List, "size");
  w.WriteU8(TAG_Int);
  w.WriteI32(3);
  w.WriteI32(doc.width);
  w.WriteI32(doc.height);
  w.WriteI32(doc.depth);

  w.WriteTagHeader(TAG_List, "structure_world_origin");
  w.WriteU8(TAG_Int);
  w.WriteI32(3);
  w.WriteI32(doc.originX);
  w.WriteI32(doc.originY);
  w.WriteI32(doc.originZ);

  w.WriteTagHeader(TAG_Compound, "structure");

  size_t totalVol = static_cast<size_t>(doc.width) *
                    static_cast<size_t>(doc.height) *
                    static_cast<size_t>(doc.depth);
  std::vector<int32_t> layer0(totalVol, -1);

  for (int32_t x = 0; x < doc.width; ++x) {
    for (int32_t y = 0; y < doc.height; ++y) {
      for (int32_t z = 0; z < doc.depth; ++z) {
        uint16_t pid = 0;
        for (size_t p = 0; p < doc.parts.size(); ++p) {
          if (!doc.parts[p].visible)
            continue;
          uint16_t v = doc.GetBlock(p, x - doc.parts[p].offsetX,
                                    y - doc.parts[p].offsetY,
                                    z - doc.parts[p].offsetZ);
          if (v != 0) {
            pid = v;
            break;
          }
        }
        if (pid > 0 && static_cast<size_t>(pid) < doc.palette.size()) {
          size_t idx = (static_cast<size_t>(x) * static_cast<size_t>(doc.height) *
                        static_cast<size_t>(doc.depth)) +
                       (static_cast<size_t>(y) * static_cast<size_t>(doc.depth)) +
                       static_cast<size_t>(z);
          layer0[idx] = static_cast<int32_t>(pid - 1);
        }
      }
    }
  }

  w.WriteTagHeader(TAG_List, "block_indices");
  w.WriteU8(TAG_List);
  w.WriteI32(2);

  w.WriteU8(TAG_Int);
  w.WriteI32(static_cast<int32_t>(totalVol));
  for (int32_t v : layer0) {
    w.WriteI32(v);
  }

  w.WriteU8(TAG_Int);
  w.WriteI32(static_cast<int32_t>(totalVol));
  for (size_t i = 0; i < totalVol; ++i) {
    w.WriteI32(-1);
  }

  w.WriteTagHeader(TAG_List, "entities");
  w.WriteU8(TAG_Compound);
  w.WriteI32(0);

  w.WriteTagHeader(TAG_Compound, "palette");
  w.WriteTagHeader(TAG_Compound, "default");
  w.WriteTagHeader(TAG_List, "block_palette");
  w.WriteU8(TAG_Compound);
  int32_t palCount =
      (doc.palette.size() > 1) ? static_cast<int32_t>(doc.palette.size() - 1) : 0;
  w.WriteI32(palCount);

  for (size_t i = 1; i < doc.palette.size(); ++i) {
    const auto &pe = doc.palette[i];
    w.WriteTagHeader(TAG_String, "name");
    w.WriteString(pe.name);

    w.WriteTagHeader(TAG_Compound, "states");
    for (const auto &st : pe.states) {
      if (st.type == BlockStateType::Byte) {
        w.WriteTagHeader(TAG_Byte, st.key);
        w.WriteU8(static_cast<uint8_t>(st.intVal & 0xFF));
      } else if (st.type == BlockStateType::Int) {
        w.WriteTagHeader(TAG_Int, st.key);
        w.WriteI32(st.intVal);
      } else {
        w.WriteTagHeader(TAG_String, st.key);
        w.WriteString(st.strVal);
      }
    }
    w.WriteU8(TAG_End);

    w.WriteTagHeader(TAG_Int, "version");
    w.WriteI32(18153475);

    w.WriteU8(TAG_End);
  }

  w.WriteTagHeader(TAG_Compound, "block_position_data");
  w.WriteU8(TAG_End);

  w.WriteU8(TAG_End);
  w.WriteU8(TAG_End);

  w.WriteU8(TAG_End);
  w.WriteU8(TAG_End);

  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return false;
  out.write(reinterpret_cast<const char *>(w.buf.data()),
            static_cast<std::streamsize>(w.buf.size()));
  return out.good();
}

}
