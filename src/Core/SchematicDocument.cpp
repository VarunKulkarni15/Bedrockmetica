#include "SchematicDocument.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Bedrockmetica {

uint64_t PackSubchunkKey(int32_t cx, int32_t cy, int32_t cz) {
  uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx) & 0x1FFFFFu);
  uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cy) & 0xFFFFFu);
  uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz) & 0x1FFFFFu);
  return (ux << 41) | (uy << 21) | uz;
}

std::string BlockPaletteEntry::CanonicalKey() const {
  std::string key = name;
  for (const auto &s : states) {
    key.push_back('|');
    key += s.key;
    key.push_back('=');
    if (s.type == BlockStateType::String) {
      key += s.strVal;
    } else {
      key += std::to_string(s.intVal);
    }
  }
  return key;
}

uint32_t ResolveBlockApproxColor(const std::string &blockId) {
  std::string lo = blockId;
  for (char &c : lo) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  auto has = [&](const char *sub) { return lo.find(sub) != std::string::npos; };

  if (has("air") || has("structure_void"))
    return 0x00000000u;
  if (has("cherry") && has("leaves"))
    return 0xFFB898F6u;
  if (has("cherry"))
    return 0xFFAEC2E8u;
  if (has("leaves") || has("azalea") || has("moss") || has("cactus"))
    return 0xFF48A852u;
  if (has("grass_block"))
    return 0xFF56B65Eu;
  if (has("dirt") || has("mud") || has("podzol"))
    return 0xFF4E6B8Cu;
  if (has("oak"))
    return 0xFF669CC8u;
  if (has("spruce"))
    return 0xFF3E5A7Eu;
  if (has("birch"))
    return 0xFF9AC8D8u;
  if (has("dark_oak"))
    return 0xFF283C54u;
  if (has("crimson"))
    return 0xFF523886u;
  if (has("warped"))
    return 0xFF888A2Cu;
  if (has("mangrove"))
    return 0xFF383E80u;
  if (has("bamboo"))
    return 0xFF58C6D4u;
  if (has("red_nether"))
    return 0xFF362EB0u;
  if (has("nether_brick"))
    return 0xFF2E2244u;
  if (has("deepslate"))
    return 0xFF58524Eu;
  if (has("stone_bricks") || has("cobblestone") || has("andesite") ||
      has("stone"))
    return 0xFF8A7E78u;
  if (has("calcite") || has("quartz") || has("white") || has("snow"))
    return 0xFFE6ECF0u;
  if (has("gold") || has("glowstone") || has("lantern"))
    return 0xFF5CD6FFu;
  if (has("diamond") || has("prismarine") || has("sea_lantern"))
    return 0xFFECE268u;
  if (has("redstone") || has("red_") || has("tnt"))
    return 0xFF343ADCu;
  if (has("lapis") || has("blue") || has("water"))
    return 0xFFD87C38u;
  if (has("emerald") || has("green") || has("lime"))
    return 0xFF62CA58u;
  if (has("glass") || has("ice"))
    return 0xFFF4DCACu;
  if (has("copper"))
    return 0xFF628AD0u;
  if (has("obsidian") || has("black") || has("coal"))
    return 0xFF3C282Cu;
  if (has("sand"))
    return 0xFFA2D8E6u;
  if (has("terracotta") || has("brick"))
    return 0xFF5864AAu;
  return 0xFF9E948Cu;
}

std::string FormatBlockDisplayName(const std::string &blockId) {
  std::string s = blockId;
  size_t colon = s.find(':');
  if (colon != std::string::npos) {
    s = s.substr(colon + 1);
  }
  std::string out;
  out.reserve(s.size());
  bool capNext = true;
  for (char c : s) {
    if (c == '_') {
      out.push_back(' ');
      capNext = true;
    } else {
      if (capNext) {
        out.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        capNext = false;
      } else {
        out.push_back(c);
      }
    }
  }
  return out;
}

void SchematicDocument::Reset(const std::string &docName) {
  name = docName;
  sourcePath.clear();
  width = 0;
  height = 0;
  depth = 0;
  originX = 0;
  originY = 0;
  originZ = 0;
  totalNonAirBlocks = 0;
  palette.clear();
  paletteLookup.clear();
  parts.clear();
  thumbRgba.clear();
  thumbW = 0;
  thumbH = 0;

  BlockPaletteEntry air;
  air.name = "minecraft:air";
  air.approxAbgr = 0;
  air.isSolid = false;
  palette.push_back(air);
  paletteLookup[air.CanonicalKey()] = 0;

  SchematicPart mainPart;
  mainPart.name = "Main";
  parts.push_back(std::move(mainPart));
}

uint16_t SchematicDocument::InternBlock(
    const std::string &blockName, const std::vector<BlockStateProp> &states) {
  if (blockName.empty() || blockName == "minecraft:air" ||
      blockName == "air") {
    return 0;
  }
  BlockPaletteEntry entry;
  entry.name = blockName;
  entry.states = states;
  std::sort(entry.states.begin(), entry.states.end(),
            [](const BlockStateProp &a, const BlockStateProp &b) {
              return a.key < b.key;
            });
  std::string key = entry.CanonicalKey();
  auto it = paletteLookup.find(key);
  if (it != paletteLookup.end()) {
    return it->second;
  }
  if (palette.size() >= 65535) {
    return 0;
  }
  entry.approxAbgr = ResolveBlockApproxColor(blockName);
  entry.isSolid =
      (blockName.find("glass") == std::string::npos &&
       blockName.find("leaves") == std::string::npos &&
       blockName.find("water") == std::string::npos);
  uint16_t idx = static_cast<uint16_t>(palette.size());
  palette.push_back(std::move(entry));
  paletteLookup[key] = idx;
  return idx;
}

void SchematicDocument::SetBlock(size_t partIdx, int32_t lx, int32_t ly,
                                 int32_t lz, uint16_t paletteId) {
  if (partIdx >= parts.size() || lx < 0 || ly < 0 || lz < 0)
    return;
  int32_t cx = lx >> 4;
  int32_t cy = ly >> 4;
  int32_t cz = lz >> 4;
  int32_t sx = lx & 15;
  int32_t sy = ly & 15;
  int32_t sz = lz & 15;
  uint64_t key = PackSubchunkKey(cx, cy, cz);
  auto &map = parts[partIdx].subchunks;
  auto it = map.find(key);
  if (paletteId == 0) {
    if (it == map.end())
      return;
    int32_t idx = (sy << 8) | (sz << 4) | sx;
    if (it->second.blocks[idx] != 0) {
      it->second.blocks[idx] = 0;
      if (it->second.nonAirCount > 0)
        it->second.nonAirCount--;
      if (it->second.nonAirCount == 0)
        map.erase(it);
    }
    return;
  }
  if (it == map.end()) {
    Subchunk16 sc;
    sc.cx = cx;
    sc.cy = cy;
    sc.cz = cz;
    auto [insIt, ok] = map.emplace(key, sc);
    it = insIt;
  }
  int32_t idx = (sy << 8) | (sz << 4) | sx;
  if (it->second.blocks[idx] == 0) {
    it->second.nonAirCount++;
  }
  it->second.blocks[idx] = paletteId;
}

uint16_t SchematicDocument::GetBlock(size_t partIdx, int32_t lx, int32_t ly,
                                     int32_t lz) const {
  if (partIdx >= parts.size() || lx < 0 || ly < 0 || lz < 0)
    return 0;
  uint64_t key = PackSubchunkKey(lx >> 4, ly >> 4, lz >> 4);
  const auto &map = parts[partIdx].subchunks;
  auto it = map.find(key);
  if (it == map.end())
    return 0;
  int32_t idx = ((ly & 15) << 8) | ((lz & 15) << 4) | (lx & 15);
  return it->second.blocks[idx];
}

void SchematicDocument::RecomputeStats() {
  int32_t total = 0;
  for (const auto &p : parts) {
    for (const auto &[k, sc] : p.subchunks) {
      total += sc.nonAirCount;
    }
  }
  totalNonAirBlocks = total;
}

std::vector<MaterialCount> SchematicDocument::BuildMaterialTally() const {
  std::unordered_map<std::string, int32_t> counts;
  std::unordered_map<std::string, uint32_t> colors;
  for (const auto &p : parts) {
    for (const auto &[k, sc] : p.subchunks) {
      for (int i = 0; i < 4096; ++i) {
        uint16_t pid = sc.blocks[i];
        if (pid == 0 || pid >= palette.size())
          continue;
        const auto &pe = palette[pid];
        counts[pe.name] += 1;
        colors[pe.name] = pe.approxAbgr;
      }
    }
  }
  std::vector<MaterialCount> out;
  out.reserve(counts.size());
  for (const auto &[rawId, cnt] : counts) {
    MaterialCount mc;
    mc.rawId = rawId;
    mc.displayName = FormatBlockDisplayName(rawId);
    mc.required = cnt;
    mc.approxAbgr = colors[rawId];
    out.push_back(std::move(mc));
  }
  std::sort(out.begin(), out.end(),
            [](const MaterialCount &a, const MaterialCount &b) {
              return a.required > b.required;
            });
  return out;
}

}
