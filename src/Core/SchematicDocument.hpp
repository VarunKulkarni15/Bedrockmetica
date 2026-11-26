#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Bedrockmetica {

enum class BlockStateType : uint8_t {
  Byte = 1,
  Int = 3,
  String = 8
};

struct BlockStateProp {
  std::string key;
  BlockStateType type = BlockStateType::Int;
  int32_t intVal = 0;
  std::string strVal;

  bool operator==(const BlockStateProp &o) const {
    return key == o.key && type == o.type && intVal == o.intVal &&
           strVal == o.strVal;
  }
};

struct BlockPaletteEntry {
  std::string name;
  std::vector<BlockStateProp> states;
  uint32_t approxAbgr = 0xFF8C929Cu;
  bool isSolid = true;

  std::string CanonicalKey() const;
};

struct Subchunk16 {
  int32_t cx = 0;
  int32_t cy = 0;
  int32_t cz = 0;
  uint16_t blocks[4096] = {};
  uint16_t nonAirCount = 0;
};

struct SchematicPart {
  std::string name = "Main";
  int32_t offsetX = 0;
  int32_t offsetY = 0;
  int32_t offsetZ = 0;
  bool visible = true;
  std::unordered_map<uint64_t, Subchunk16> subchunks;
};

struct MaterialCount {
  std::string displayName;
  std::string rawId;
  int32_t required = 0;
  uint32_t approxAbgr = 0xFF8C929Cu;
};

struct SchematicDocument {
  std::string name;
  std::string sourcePath;
  int32_t width = 0;
  int32_t height = 0;
  int32_t depth = 0;
  int32_t originX = 0;
  int32_t originY = 0;
  int32_t originZ = 0;
  int32_t totalNonAirBlocks = 0;
  std::vector<BlockPaletteEntry> palette;
  std::unordered_map<std::string, uint16_t> paletteLookup;
  std::vector<SchematicPart> parts;
  std::vector<uint8_t> thumbRgba;
  uint16_t thumbW = 0;
  uint16_t thumbH = 0;

  void Reset(const std::string &docName = "Untitled");
  uint16_t InternBlock(const std::string &blockName,
                       const std::vector<BlockStateProp> &states);
  void SetBlock(size_t partIdx, int32_t lx, int32_t ly, int32_t lz,
                uint16_t paletteId);
  uint16_t GetBlock(size_t partIdx, int32_t lx, int32_t ly, int32_t lz) const;
  void RecomputeStats();
  std::vector<MaterialCount> BuildMaterialTally() const;
};

uint64_t PackSubchunkKey(int32_t cx, int32_t cy, int32_t cz);
uint32_t ResolveBlockApproxColor(const std::string &blockId);
std::string FormatBlockDisplayName(const std::string &blockId);

}
