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

struct CornerMarkerInfo {
  bool valid = false;
  int num = 0;
  float wx = 0.0f;
  float wy = 0.0f;
  float wz = 0.0f;
  int64_t setMs = 0;
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
void GetCornerMarkers(CornerMarkerInfo &outPos1, CornerMarkerInfo &outPos2);

bool HandleBareHandRightClick();
void TickWorldUpdate();
void RenderWorldVisuals();

bool FinalizeAndSaveBmet(const std::string &name, const std::string &dirPath,
                         std::string &outSavedPath,
                         SchematicDocument &outDoc);

}
