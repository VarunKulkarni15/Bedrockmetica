#pragma once

#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace Logger {
    enum class Level {
        Debug,
        Info,
        Success,
        Warning,
        Error
    };

    struct LogEntry {
        Level level;
        std::string time;
        std::string tag;
        std::string message;
    };

    inline std::vector<LogEntry> g_LogHistory;
    inline std::mutex g_LogMutex;
    inline FILE* g_LogFile = nullptr;
    inline bool g_LogFileAttempted = false;
    inline std::string g_LogFilePath;

    inline void EnsureLogFileOpenLocked() {
        if (g_LogFileAttempted) return;
        g_LogFileAttempted = true;

        std::vector<std::string> candidates;
        HMODULE hMod = GetModuleHandleA("Bedrockmetica.dll");
        if (hMod) {
            char modBuf[MAX_PATH] = {};
            DWORD len = GetModuleFileNameA(hMod, modBuf, MAX_PATH);
            if (len > 0) {
                std::string dir(modBuf, len);
                size_t slash = dir.find_last_of("\\/");
                if (slash != std::string::npos) {
                    candidates.push_back(dir.substr(0, slash) + "\\bedrockmetica.log");
                }
            }
        }
        char tempBuf[MAX_PATH] = {};
        if (GetTempPathA(MAX_PATH, tempBuf) > 0) {
            candidates.push_back(std::string(tempBuf) + "bedrockmetica.log");
        }

        for (const auto& cand : candidates) {
            FILE* f = nullptr;
            if (fopen_s(&f, cand.c_str(), "w") == 0 && f != nullptr) {
                g_LogFile = f;
                g_LogFilePath = cand;
                break;
            }
        }
    }

    inline std::string GetLogFilePath() {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        return g_LogFilePath;
    }

    inline std::string CurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        tm localTime{};
        localtime_s(&localTime, &time);
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M:%S", &localTime);
        return std::string(buf);
    }

    inline void Log(Level level, const std::string& tag, const std::string& message) {
        std::string timeStr = CurrentTimeString();
        const char* prefix = "[INFO]";
        WORD color = 7;

        switch (level) {
        case Level::Debug:
            prefix = "[DEBUG]";
            color = 8;
            break;
        case Level::Info:
            prefix = "[INFO]";
            color = 11;
            break;
        case Level::Success:
            prefix = "[SUCCESS]";
            color = 10;
            break;
        case Level::Warning:
            prefix = "[WARN]";
            color = 14;
            break;
        case Level::Error:
            prefix = "[ERROR]";
            color = 12;
            break;
        }

        std::string fullMsg = "[" + timeStr + "] " + prefix + " [" + tag + "] " + message + "\n";

        OutputDebugStringA(fullMsg.c_str());

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
            SetConsoleTextAttribute(hConsole, color);
            std::cout << fullMsg;
            SetConsoleTextAttribute(hConsole, 7);
        }

        {
            std::lock_guard<std::mutex> lock(g_LogMutex);
            EnsureLogFileOpenLocked();
            if (g_LogFile) {
                fwrite(fullMsg.data(), 1, fullMsg.size(), g_LogFile);
                fflush(g_LogFile);
            }
            g_LogHistory.push_back({ level, timeStr, tag, message });
            if (g_LogHistory.size() > 500) {
                g_LogHistory.erase(g_LogHistory.begin());
            }
        }
    }

    inline void debug(const std::string& msg, const std::string& tag = "Bedrockmetica") {
        Log(Level::Debug, tag, msg);
    }

    inline void info(const std::string& msg, const std::string& tag = "Bedrockmetica") {
        Log(Level::Info, tag, msg);
    }

    inline void success(const std::string& msg, const std::string& tag = "Bedrockmetica") {
        Log(Level::Success, tag, msg);
    }

    inline void warn(const std::string& msg, const std::string& tag = "Bedrockmetica") {
        Log(Level::Warning, tag, msg);
    }

    inline void error(const std::string& msg, const std::string& tag = "Bedrockmetica") {
        Log(Level::Error, tag, msg);
    }

    inline std::vector<LogEntry> GetRecentLogs() {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        return g_LogHistory;
    }
}
