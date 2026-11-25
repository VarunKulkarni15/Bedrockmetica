#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace CrashGuard {
    struct CrashRecord {
        DWORD exceptionCode;
        void* address;
        std::string contextName;
        std::string timestamp;
    };

    void Initialize();
    void Shutdown();

    // Panic Unload controls
    void RequestPanicUnload();
    bool IsPanicUnloadRequested();
    void WaitForUnloadSignal(DWORD timeoutMs = INFINITE);
    void SetPanicKey(int vkKey);
    int GetPanicKey();
    void CheckHotkey(int vkKey);

    // Crash statistics & inspection
    uint32_t GetPreventedCrashCount();
    std::vector<CrashRecord> GetCrashRecords();
    void RecordCrash(DWORD code, void* addr, const char* context);

    // SEH Protected Execution Trampoline
    extern "C" bool SEH_Execute(void (*fn)(void*), void* param, const char* contextName);

    template<typename Callable>
    bool TryExecute(Callable&& callable, const char* contextName = "SafeBlock") {
        struct Invoker {
            Callable* fn;
            static void Invoke(void* p) {
                auto* self = static_cast<Invoker*>(p);
                (*(self->fn))();
            }
        } invoker{ &callable };

        return SEH_Execute(&Invoker::Invoke, &invoker, contextName);
    }
}
