#pragma once

#include "SchematicDocument.hpp"
#include "../SDK/BedrockGame.hpp"
#include <cstdint>
#include <string>

namespace Bedrockmetica::CaptureManager {

enum class CapturePhase {
  Idle = 0,
  ArmedNeedsPos1,
  HasPos1NeedsPos2,
  BoxLockedReadyToSave
};

void SetActive(bool active);
bool IsActive();
void ResetCapture();

CapturePhase GetPhase();
bool HasCapturedBox();
int GetCapturedPercent();
int32_t GetBoxWidth();
int32_t GetBoxHeight();
int32_t GetBoxDepth();
int32_t GetCapturedBlockCount();

bool HandleBareHandRightClick();
void TickWorldUpdate();
void RenderWorldVisuals();

bool FinalizeAndSaveBmet(const std::string &name, const std::string &dirPath,
                         std::string &outSavedPath,
                         SchematicDocument &outDoc);

}
