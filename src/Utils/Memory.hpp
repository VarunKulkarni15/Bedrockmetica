#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <psapi.h>
#include <windows.h>
#include "Logger.hpp"

// Lightweight member_at helper
namespace hat {
    template<typename T, typename Ptr>
    inline T& member_at(Ptr* base, std::ptrdiff_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
    }

    template<typename T, typename Ptr>
    inline const T& member_at(const Ptr* base, std::ptrdiff_t offset) {
        return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(base) + offset);
    }
}

namespace Memory {
    inline uintptr_t findSig(const std::string& signature, const std::string& name = "") {
        MODULEINFO modInfo{};
        HMODULE hModule = GetModuleHandleA(nullptr);
        if (!hModule) {
            hModule = GetModuleHandleA("Minecraft.Windows.exe");
        }
        if (!hModule) return 0;

        GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO));
        auto base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        auto size = static_cast<size_t>(modInfo.SizeOfImage);

        // Parse signature string (e.g. "48 8B C4 ? ? 00")
        std::vector<int16_t> pattern;
        std::istringstream stream(signature);
        std::string byteStr;

        while (stream >> byteStr) {
            if (byteStr == "?" || byteStr == "??") {
                pattern.push_back(-1); // Wildcard
            } else {
                pattern.push_back(static_cast<int16_t>(std::stoul(byteStr, nullptr, 16)));
            }
        }

        if (pattern.empty()) return 0;

        const auto* scanBytes = reinterpret_cast<const uint8_t*>(base);
        const size_t patternSize = pattern.size();

        for (size_t i = 0; i <= size - patternSize; ++i) {
            bool found = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (pattern[j] != -1 && scanBytes[i + j] != static_cast<uint8_t>(pattern[j])) {
                    found = false;
                    break;
                }
            }
            if (found) {
                uintptr_t match = base + i;
                if (!name.empty()) {
                    std::ostringstream ss;
                    ss << "Found pattern '" << name << "' at 0x" << std::hex << match;
                    Logger::debug(ss.str(), "Memory");
                }
                return match;
            }
        }

        if (!name.empty()) {
            Logger::warn("Pattern '" + name + "' not found in module", "Memory");
        }
        return 0;
    }

    inline uintptr_t offsetFromSig(uintptr_t address, int offset) {
        if (!address) return 0;
        // Resolve relative rip-relative address: [RIP + offset + 4] + *(int32_t*)(address + offset)
        auto relOffset = *reinterpret_cast<int32_t*>(address + offset);
        return address + offset + sizeof(int32_t) + relOffset;
    }
}
