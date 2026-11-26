#pragma once

#include "../Core/SchematicDocument.hpp"
#include <string>

namespace Bedrockmetica::BmetCodec {

struct BmetSummary {
  std::string name;
  int32_t width = 0;
  int32_t height = 0;
  int32_t depth = 0;
  int32_t totalBlocks = 0;
  std::vector<MaterialCount> topMaterials;
  bool valid = false;
};

bool SaveToFile(const std::string &filePath, const SchematicDocument &doc);
bool LoadFromFile(const std::string &filePath, SchematicDocument &outDoc);
bool ReadSummary(const std::string &filePath, BmetSummary &outSummary);
bool RenameInFile(const std::string &oldPath, const std::string &newName,
                  std::string &outNewPath);

}
