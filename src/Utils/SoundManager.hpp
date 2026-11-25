#pragma once

#include <string>
#include <windows.h>

enum class UISound {
    Click,
    TabNav,
    Toggle,
    FloorStep,
    Success,
    Error,
    OpenMenu,
    CloseMenu
};

namespace SoundManager {
    void Initialize();
    void SetSoundEngine(void* pSoundEngine);
    void Play(UISound sound);
    void SetEnabled(bool enabled);
    bool IsEnabled();
}
