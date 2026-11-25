#include "CrashGuard.hpp"
#include "Logger.hpp"
#include <mutex>
#include <sstream>

namespace CrashGuard {
    static std::atomic<bool> g_PanicUnloadRequested{ false };
    static std::atomic<uint32_t> g_PreventedCrashCount{ 0 };
    static int g_PanicKey = VK_END;
    static HANDLE g_UnloadEvent = nullptr;

    static std::mutex g_RecordMutex;
    static std::vector<CrashRecord> g_CrashRecords;

    static const char* GetExceptionCodeString(DWORD code) {
        switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION (0xC0000005)";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (0xC000008C)";
        case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT (0x80000003)";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT (0x80000002)";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO (0xC000008E)";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D)";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR (0xC0000006)";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO (0xC0000094)";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW (0xC00000FD)";
        default: return "UNKNOWN_EXCEPTION";
        }
    }

    void Initialize() {
        if (!g_UnloadEvent) {
            g_UnloadEvent = CreateEventA(nullptr, TRUE, FALSE, "BedrockmeticaPanicUnload");
        }
        g_PanicUnloadRequested = false;
        Logger::info("CrashGuard initialized with MSVC SEH protection. Panic Key: [END]", "CrashGuard");
    }

    void Shutdown() {
        if (g_UnloadEvent) {
            CloseHandle(g_UnloadEvent);
            g_UnloadEvent = nullptr;
        }
    }

    void RequestPanicUnload() {
        if (!g_PanicUnloadRequested.exchange(true)) {
            Logger::warn("Panic Unload requested! Initiating graceful client detachment...", "CrashGuard");
            if (g_UnloadEvent) {
                SetEvent(g_UnloadEvent);
            }
        }
    }

    bool IsPanicUnloadRequested() {
        return g_PanicUnloadRequested.load();
    }

    void WaitForUnloadSignal(DWORD timeoutMs) {
        if (g_UnloadEvent) {
            WaitForSingleObject(g_UnloadEvent, timeoutMs);
        } else {
            Sleep(timeoutMs);
        }
    }

    void SetPanicKey(int vkKey) { g_PanicKey = vkKey; }
    int GetPanicKey() { return g_PanicKey; }

    void CheckHotkey(int vkKey) {
        if (vkKey == g_PanicKey) {
            RequestPanicUnload();
        }
    }

    uint32_t GetPreventedCrashCount() {
        return g_PreventedCrashCount.load();
    }

    std::vector<CrashRecord> GetCrashRecords() {
        std::lock_guard<std::mutex> lock(g_RecordMutex);
        return g_CrashRecords;
    }

    void RecordCrash(DWORD code, void* addr, const char* context) {
        g_PreventedCrashCount.fetch_add(1);

        std::string timeStr = Logger::CurrentTimeString();
        std::ostringstream ss;
        ss << "[SEH CAUGHT] " << GetExceptionCodeString(code) << " at address 0x" << std::hex << addr
           << " in context: '" << (context ? context : "Unknown") << "'";
        Logger::error(ss.str(), "CrashGuard");

        std::lock_guard<std::mutex> lock(g_RecordMutex);
        g_CrashRecords.push_back({ code, addr, context ? context : "Unknown", timeStr });
        if (g_CrashRecords.size() > 100) {
            g_CrashRecords.erase(g_CrashRecords.begin());
        }
    }

    static DWORD ExceptionFilter(DWORD code, EXCEPTION_POINTERS* ep, const char* context) {
        void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
        RecordCrash(code, addr, context);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    extern "C" bool SEH_Execute(void (*fn)(void*), void* param, const char* contextName) {
        __try {
            fn(param);
            return true;
        }
        __except (ExceptionFilter(GetExceptionCode(), GetExceptionInformation(), contextName)) {
            return false;
        }
    }
}
