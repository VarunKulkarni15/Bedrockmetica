#include "BmetCodec.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Bedrockmetica::BmetCodec {

namespace {

constexpr uint32_t kBmetMagic = 0x54454D42u;
constexpr uint16_t kBmetVersion = 2;

void WriteU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

void WriteU16(std::vector<uint8_t> &buf, uint16_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void WriteU32(std::vector<uint8_t> &buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void WriteI32(std::vector<uint8_t> &buf, int32_t v) {
  WriteU32(buf, static_cast<uint32_t>(v));
}

void WriteStr(std::vector<uint8_t> &buf, const std::string &s) {
  uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 65535));
  WriteU16(buf, len);
  buf.insert(buf.end(), s.begin(), s.begin() + len);
}

struct Reader {
  const uint8_t *data = nullptr;
  size_t size = 0;
  size_t pos = 0;

  bool Has(size_t n) const { return pos + n <= size; }

  bool ReadU8(uint8_t &out) {
    if (!Has(1))
      return false;
    out = data[pos++];
    return true;
  }

  bool ReadU16(uint16_t &out) {
    if (!Has(2))
      return false;
    out = static_cast<uint16_t>(data[pos]) |
          (static_cast<uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    return true;
  }

  bool ReadU32(uint32_t &out) {
    if (!Has(4))
      return false;
    out = static_cast<uint32_t>(data[pos]) |
          (static_cast<uint32_t>(data[pos + 1]) << 8) |
          (static_cast<uint32_t>(data[pos + 2]) << 16) |
          (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    return true;
  }

  bool ReadI32(int32_t &out) {
    uint32_t u = 0;
    if (!ReadU32(u))
      return false;
    out = static_cast<int32_t>(u);
    return true;
  }

  bool ReadStr(std::string &out) {
    uint16_t len = 0;
    if (!ReadU16(len) || !Has(len))
      return false;
    out.assign(reinterpret_cast<const char *>(data + pos), len);
    pos += len;
    return true;
  }
};

uint32_t ComputeCrc32(const uint8_t *buf, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= buf[i];
    for (int b = 0; b < 8; ++b) {
      uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

void EncodeRleSubchunk(std::vector<uint8_t> &out, const uint16_t blocks[4096]) {
  size_t i = 0;
  while (i < 4096) {
    uint16_t val = blocks[i];
    size_t run = 1;
    while (i + run < 4096 && blocks[i + run] == val && run < 65535) {
      run++;
    }
    WriteU16(out, static_cast<uint16_t>(run));
    WriteU16(out, val);
    i += run;
  }
}

bool DecodeRleSubchunk(Reader &rd, uint32_t byteCount, uint16_t blocks[4096]) {
  if (!rd.Has(byteCount))
    return false;
  size_t endPos = rd.pos + byteCount;
  size_t idx = 0;
  while (rd.pos < endPos && idx < 4096) {
    uint16_t run = 0, val = 0;
    if (!rd.ReadU16(run) || !rd.ReadU16(val))
      return false;
    size_t stop = std::min<size_t>(4096, idx + run);
    for (; idx < stop; ++idx) {
      blocks[idx] = val;
    }
  }
  rd.pos = endPos;
  return idx == 4096;
}

std::string SanitizeFilename(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_' || c == '-' || c == ' ') {
      out.push_back(c);
    }
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  while (!out.empty() && out.front() == ' ')
    out.erase(out.begin());
  if (out.empty())
    out = "schematic";
  return out;
}

}

bool SaveToFile(const std::string &filePath, const SchematicDocument &doc) {
  std::vector<uint8_t> payload;
  payload.reserve(65536);

  WriteU32(payload, kBmetMagic);
  WriteU16(payload, kBmetVersion);
  uint16_t flags = (!doc.thumbRgba.empty() && doc.thumbW > 0 && doc.thumbH > 0)
                       ? 1
                       : 0;
  WriteU16(payload, flags);
  WriteStr(payload, doc.name);
  WriteI32(payload, doc.width);
  WriteI32(payload, doc.height);
  WriteI32(payload, doc.depth);
  WriteI32(payload, doc.originX);
  WriteI32(payload, doc.originY);
  WriteI32(payload, doc.originZ);
  WriteI32(payload, doc.totalNonAirBlocks);

  if (flags & 1) {
    WriteU16(payload, doc.thumbW);
    WriteU16(payload, doc.thumbH);
    uint32_t tsz = static_cast<uint32_t>(doc.thumbRgba.size());
    WriteU32(payload, tsz);
    payload.insert(payload.end(), doc.thumbRgba.begin(), doc.thumbRgba.end());
  }

  uint32_t palCount = static_cast<uint32_t>(doc.palette.size());
  WriteU32(payload, palCount);
  for (const auto &pe : doc.palette) {
    WriteStr(payload, pe.name);
    WriteU32(payload, pe.approxAbgr);
    WriteU8(payload, pe.isSolid ? 1 : 0);
    uint16_t propCount = static_cast<uint16_t>(
        std::min<size_t>(pe.states.size(), 65535));
    WriteU16(payload, propCount);
    for (uint16_t pi = 0; pi < propCount; ++pi) {
      const auto &st = pe.states[pi];
      WriteStr(payload, st.key);
      WriteU8(payload, static_cast<uint8_t>(st.type));
      if (st.type == BlockStateType::String) {
        WriteStr(payload, st.strVal);
      } else {
        WriteI32(payload, st.intVal);
      }
    }
  }

  uint32_t partCount = static_cast<uint32_t>(doc.parts.size());
  WriteU32(payload, partCount);
  std::vector<uint8_t> rleScratch;
  for (const auto &part : doc.parts) {
    WriteStr(payload, part.name);
    WriteI32(payload, part.offsetX);
    WriteI32(payload, part.offsetY);
    WriteI32(payload, part.offsetZ);
    WriteU8(payload, part.visible ? 1 : 0);
    WriteU32(payload, static_cast<uint32_t>(part.subchunks.size()));
    for (const auto &[key, sc] : part.subchunks) {
      WriteI32(payload, sc.cx);
      WriteI32(payload, sc.cy);
      WriteI32(payload, sc.cz);
      WriteU16(payload, sc.nonAirCount);
      rleScratch.clear();
      EncodeRleSubchunk(rleScratch, sc.blocks);
      WriteU32(payload, static_cast<uint32_t>(rleScratch.size()));
      payload.insert(payload.end(), rleScratch.begin(), rleScratch.end());
    }
  }

  uint32_t crc = ComputeCrc32(payload.data(), payload.size());
  WriteU32(payload, crc);

  std::error_code ec;
  std::filesystem::path p(filePath);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open())
    return false;
  ofs.write(reinterpret_cast<const char *>(payload.data()),
            static_cast<std::streamsize>(payload.size()));
  return ofs.good();
}

bool LoadFromFile(const std::string &filePath, SchematicDocument &outDoc) {
  std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
  if (!ifs.is_open())
    return false;
  std::streamsize fsz = ifs.tellg();
  if (fsz < 36)
    return false;
  ifs.seekg(0, std::ios::beg);
  std::vector<uint8_t> buf(static_cast<size_t>(fsz));
  if (!ifs.read(reinterpret_cast<char *>(buf.data()), fsz))
    return false;

  Reader rd{buf.data(), buf.size() - 4, 0};
  uint32_t expectedCrc =
      static_cast<uint32_t>(buf[buf.size() - 4]) |
      (static_cast<uint32_t>(buf[buf.size() - 3]) << 8) |
      (static_cast<uint32_t>(buf[buf.size() - 2]) << 16) |
      (static_cast<uint32_t>(buf[buf.size() - 1]) << 24);
  if (ComputeCrc32(buf.data(), buf.size() - 4) != expectedCrc)
    return false;

  uint32_t magic = 0;
  uint16_t ver = 0, flags = 0;
  if (!rd.ReadU32(magic) || magic != kBmetMagic)
    return false;
  if (!rd.ReadU16(ver) || ver != kBmetVersion)
    return false;
  if (!rd.ReadU16(flags))
    return false;

  outDoc.Reset();
  outDoc.sourcePath = filePath;
  if (!rd.ReadStr(outDoc.name))
    return false;
  if (!rd.ReadI32(outDoc.width) || !rd.ReadI32(outDoc.height) ||
      !rd.ReadI32(outDoc.depth))
    return false;
  if (!rd.ReadI32(outDoc.originX) || !rd.ReadI32(outDoc.originY) ||
      !rd.ReadI32(outDoc.originZ))
    return false;
  if (!rd.ReadI32(outDoc.totalNonAirBlocks))
    return false;

  if (flags & 1) {
    uint32_t tsz = 0;
    if (!rd.ReadU16(outDoc.thumbW) || !rd.ReadU16(outDoc.thumbH) ||
        !rd.ReadU32(tsz) || !rd.Has(tsz))
      return false;
    outDoc.thumbRgba.assign(rd.data + rd.pos, rd.data + rd.pos + tsz);
    rd.pos += tsz;
  }

  uint32_t palCount = 0;
  if (!rd.ReadU32(palCount) || palCount == 0 || palCount > 65536)
    return false;
  outDoc.palette.clear();
  outDoc.paletteLookup.clear();
  outDoc.palette.reserve(palCount);
  for (uint32_t i = 0; i < palCount; ++i) {
    BlockPaletteEntry pe;
    uint8_t solidByte = 1;
    uint16_t propCount = 0;
    if (!rd.ReadStr(pe.name) || !rd.ReadU32(pe.approxAbgr) ||
        !rd.ReadU8(solidByte) || !rd.ReadU16(propCount))
      return false;
    pe.isSolid = (solidByte != 0);
    for (uint16_t pi = 0; pi < propCount; ++pi) {
      BlockStateProp prop;
      uint8_t typeByte = 3;
      if (!rd.ReadStr(prop.key) || !rd.ReadU8(typeByte))
        return false;
      prop.type = static_cast<BlockStateType>(typeByte);
      if (prop.type == BlockStateType::String) {
        if (!rd.ReadStr(prop.strVal))
          return false;
      } else {
        if (!rd.ReadI32(prop.intVal))
          return false;
      }
      pe.states.push_back(std::move(prop));
    }
    outDoc.paletteLookup[pe.CanonicalKey()] = static_cast<uint16_t>(i);
    outDoc.palette.push_back(std::move(pe));
  }

  uint32_t partCount = 0;
  if (!rd.ReadU32(partCount) || partCount > 4096)
    return false;
  outDoc.parts.clear();
  for (uint32_t p = 0; p < partCount; ++p) {
    SchematicPart part;
    uint8_t vis = 1;
    uint32_t scCount = 0;
    if (!rd.ReadStr(part.name) || !rd.ReadI32(part.offsetX) ||
        !rd.ReadI32(part.offsetY) || !rd.ReadI32(part.offsetZ) ||
        !rd.ReadU8(vis) || !rd.ReadU32(scCount))
      return false;
    part.visible = (vis != 0);
    for (uint32_t si = 0; si < scCount; ++si) {
      Subchunk16 sc;
      uint32_t rleBytes = 0;
      if (!rd.ReadI32(sc.cx) || !rd.ReadI32(sc.cy) || !rd.ReadI32(sc.cz) ||
          !rd.ReadU16(sc.nonAirCount) || !rd.ReadU32(rleBytes))
        return false;
      if (!DecodeRleSubchunk(rd, rleBytes, sc.blocks))
        return false;
      part.subchunks[PackSubchunkKey(sc.cx, sc.cy, sc.cz)] = sc;
    }
    outDoc.parts.push_back(std::move(part));
  }

  outDoc.RecomputeStats();
  return true;
}

bool ReadSummary(const std::string &filePath, BmetSummary &outSummary) {
  SchematicDocument doc;
  if (!LoadFromFile(filePath, doc))
    return false;
  outSummary.name = doc.name;
  outSummary.width = doc.width;
  outSummary.height = doc.height;
  outSummary.depth = doc.depth;
  outSummary.totalBlocks = doc.totalNonAirBlocks;
  outSummary.topMaterials = doc.BuildMaterialTally();
  if (outSummary.topMaterials.size() > 8) {
    outSummary.topMaterials.resize(8);
  }
  outSummary.valid = true;
  return true;
}

bool RenameInFile(const std::string &oldPath, const std::string &newName,
                  std::string &outNewPath) {
  SchematicDocument doc;
  if (!LoadFromFile(oldPath, doc))
    return false;
  doc.name = newName;
  std::filesystem::path oldP(oldPath);
  std::string cleanStem = SanitizeFilename(newName);
  std::filesystem::path newP = oldP.parent_path() / (cleanStem + ".bmet");
  std::error_code ec;
  if (newP != oldP && std::filesystem::exists(newP, ec)) {
    for (int idx = 2; idx < 100; ++idx) {
      std::filesystem::path cand =
          oldP.parent_path() / (cleanStem + "_" + std::to_string(idx) + ".bmet");
      if (!std::filesystem::exists(cand, ec)) {
        newP = cand;
        break;
      }
    }
  }
  if (!SaveToFile(newP.string(), doc))
    return false;
  if (newP != oldP) {
    std::filesystem::remove(oldP, ec);
  }
  outNewPath = newP.string();
  return true;
}

}
