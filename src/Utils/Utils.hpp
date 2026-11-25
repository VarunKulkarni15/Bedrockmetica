#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <windows.h>

namespace Utils {
    // Compile-time & runtime 32-bit FNV-1a hash
    constexpr unsigned int hash(std::string_view str) {
        unsigned int hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    inline std::string GetModuleDir(HMODULE hModule) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(hModule, path, MAX_PATH)) {
            std::filesystem::path fsPath(path);
            return fsPath.parent_path().string();
        }
        return "";
    }

    inline std::string ResolveFontPath(HMODULE hModule, const std::string& fontName = "font.ttf") {
        std::string modDir = GetModuleDir(hModule);
        std::vector<std::string> searchPaths = {
            "src/Assets/Fonts/" + fontName,
            "Assets/Fonts/" + fontName,
            modDir + "/src/Assets/Fonts/" + fontName,
            modDir + "/Assets/Fonts/" + fontName,
            modDir + "/Fonts/" + fontName,
            modDir + "/../../src/Assets/Fonts/" + fontName,
            modDir + "/" + fontName
        };

        char localAppData[MAX_PATH] = {};
        if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
            searchPaths.push_back(std::string(localAppData) + "/Microsoft/Windows/Fonts/" + fontName);
        }

        for (const auto& p : searchPaths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                return p;
            }
        }
        return "";
    }

    inline HWND FindMinecraftWindow() {
        HWND hwnd = FindWindowA(nullptr, "Minecraft");
        if (!hwnd) {
            hwnd = FindWindowA("ApplicationFrameWindow", "Minecraft");
        }
        if (!hwnd) {
            hwnd = FindWindowA(nullptr, "Minecraft: Bedrock Edition");
        }
        return hwnd;
    }
}
