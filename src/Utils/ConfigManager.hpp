#pragma once

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace ConfigManager {

struct ConfigData {
    int menuToggleKey = 'M';
    int uiSizePreset = 1;
    int textSizePct = 115;
    int ghostOpacityPct = 65;
    bool soundFxEnabled = true;
    int verifyMode = 0;
};

inline std::mutex& Mutex() {
    static std::mutex mtx;
    return mtx;
}

inline std::vector<std::string> GetCandidatePaths() {
    std::vector<std::string> paths;
    HMODULE hMod = GetModuleHandleA("Bedrockmetica.dll");
    if (hMod) {
        char modBuf[MAX_PATH] = {};
        DWORD len = GetModuleFileNameA(hMod, modBuf, MAX_PATH);
        if (len > 0) {
            std::string dir(modBuf, len);
            size_t slash = dir.find_last_of("\\/");
            if (slash != std::string::npos) {
                paths.push_back(dir.substr(0, slash) + "\\bedrockmetica.cfg");
            }
        }
    }
    char localBuf[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", localBuf, MAX_PATH) > 0) {
        paths.push_back(std::string(localBuf) + "\\bedrockmetica.cfg");
    }
    char tempBuf[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempBuf) > 0) {
        paths.push_back(std::string(tempBuf) + "bedrockmetica.cfg");
    }
    return paths;
}

inline ConfigData& CachedData() {
    static ConfigData data;
    return data;
}

inline bool& IsLoadedFlag() {
    static bool loaded = false;
    return loaded;
}

inline void ClampData(ConfigData& d) {
    if (d.menuToggleKey <= 0 || d.menuToggleKey > 255) d.menuToggleKey = 'M';
    d.uiSizePreset = std::clamp(d.uiSizePreset, 0, 2);
    d.textSizePct = std::clamp(d.textSizePct, 85, 155);
    d.ghostOpacityPct = std::clamp(d.ghostOpacityPct, 25, 100);
    d.verifyMode = std::clamp(d.verifyMode, 0, 1);
}

inline ConfigData Load() {
    std::lock_guard<std::mutex> lock(Mutex());
    if (!IsLoadedFlag()) {
        IsLoadedFlag() = true;
        ConfigData& d = CachedData();
        for (const auto& p : GetCandidatePaths()) {
            FILE* f = nullptr;
            if (fopen_s(&f, p.c_str(), "r") == 0 && f != nullptr) {
                char line[128] = {};
                while (fgets(line, sizeof(line), f)) {
                    int val = 0;
                    if (sscanf_s(line, "menu_key=%d", &val) == 1) d.menuToggleKey = val;
                    else if (sscanf_s(line, "ui_size=%d", &val) == 1) d.uiSizePreset = val;
                    else if (sscanf_s(line, "text_size=%d", &val) == 1) d.textSizePct = val;
                    else if (sscanf_s(line, "ghost_opacity=%d", &val) == 1) d.ghostOpacityPct = val;
                    else if (sscanf_s(line, "sound_fx=%d", &val) == 1) d.soundFxEnabled = (val != 0);
                    else if (sscanf_s(line, "verify_mode=%d", &val) == 1) d.verifyMode = val;
                }
                fclose(f);
                ClampData(d);
                break;
            }
        }
    }
    return CachedData();
}

inline void Save(const ConfigData& incoming) {
    std::lock_guard<std::mutex> lock(Mutex());
    IsLoadedFlag() = true;
    ConfigData d = incoming;
    ClampData(d);
    CachedData() = d;
    for (const auto& p : GetCandidatePaths()) {
        FILE* f = nullptr;
        if (fopen_s(&f, p.c_str(), "w") == 0 && f != nullptr) {
            fprintf(f, "menu_key=%d\n", d.menuToggleKey);
            fprintf(f, "ui_size=%d\n", d.uiSizePreset);
            fprintf(f, "text_size=%d\n", d.textSizePct);
            fprintf(f, "ghost_opacity=%d\n", d.ghostOpacityPct);
            fprintf(f, "sound_fx=%d\n", d.soundFxEnabled ? 1 : 0);
            fprintf(f, "verify_mode=%d\n", d.verifyMode);
            fclose(f);
            break;
        }
    }
}

inline void SaveMenuToggleKey(int vk) {
    ConfigData d = Load();
    d.menuToggleKey = vk;
    Save(d);
}

}
