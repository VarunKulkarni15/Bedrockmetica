#pragma once

#include "../Core/SchematicDocument.hpp"
#include "BmetCodec.hpp"
#include <string>

namespace Bedrockmetica::McStructureCodec {

bool ReadSummary(const std::string &path, BmetCodec::BmetSummary &outSummary);
bool LoadFile(const std::string &path, SchematicDocument &outDoc);
bool SaveFile(const std::string &path, const SchematicDocument &doc);

}
