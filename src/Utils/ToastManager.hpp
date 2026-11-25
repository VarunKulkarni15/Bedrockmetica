#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <imgui.h>

enum class ToastType {
    Info,
    Success,
    Warning,
    Error
};

struct ToastNotification {
    ToastType type;
    std::string title;
    std::string message;
    std::string badge;
    float totalDuration;
    float timeRemaining;
};

namespace ToastManager {
    void Initialize();
    void Show(ToastType type, const std::string& title, const std::string& message, const std::string& badge = "[M]", float duration = 4.5f);
    void UpdateAndRender(float screenWidth, float screenHeight, float deltaTime);
    void Clear();
}
