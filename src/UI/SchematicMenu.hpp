#pragma once

namespace SchematicMenu {

void Initialize();
void Render(float screenWidth, float screenHeight, bool menuOpen);

bool IsCaptureModeActive();
void SetCaptureModeActive(bool active);
void TriggerCaptureClickNotice();

bool HasOpenModal();
void CloseActiveModal();

}
