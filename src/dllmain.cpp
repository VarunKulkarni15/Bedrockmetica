#include <windows.h>

#include "Hooks/SwapchainHook.hpp"
#include "Utils/ConfigManager.hpp"
#include "Utils/CrashGuard.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Offset/OffsetInit.hpp"
#include "Utils/Sig/SigInit.hpp"
#include "Utils/SignatureAndOffsetManager.hpp"
#include "Utils/SoundManager.hpp"
#include "Utils/ToastManager.hpp"

static HMODULE g_hModule = nullptr;

DWORD WINAPI MainThread(LPVOID lpParam) {
  g_hModule = static_cast<HMODULE>(lpParam);

  CrashGuard::Initialize();
  ToastManager::Initialize();

  ConfigManager::ConfigData cfg = ConfigManager::Load();
  SwapchainHook::SetMenuToggleKey(cfg.menuToggleKey);
  SoundManager::SetEnabled(cfg.soundFxEnabled);

  OffsetInit::init260();
  SigInit::init260();
  Mgr.scanAllSignatures();
  SoundManager::Initialize();

  if (!SwapchainHook::Initialize()) {
    Logger::error("Failed to initialize SwapchainHook.", "Init");
    CrashGuard::RequestPanicUnload();
  } else {
    Logger::success(
        "Bedrockmetica initialized successfully in Minecraft Bedrock.", "Init");
    std::string keyName = SwapchainHook::GetMenuToggleKeyName();
    ToastManager::Show(ToastType::Success, "BEDROCKMETICA LOADED",
                       "Enter a world and press [" + keyName + "] to open schematic menu.",
                       "[" + keyName + "]", 5.5f);
  }

  CrashGuard::WaitForUnloadSignal();

  Logger::warn("Panic Unload signal received! Detaching cleanly...",
               "Shutdown");

  SwapchainHook::Shutdown();
  CrashGuard::Shutdown();

  Sleep(150);
  FreeLibraryAndExitThread(g_hModule, 0);
  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hModule);
    CreateThread(nullptr, 0,
                 reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), hModule,
                 0, nullptr);
    break;
  case DLL_PROCESS_DETACH:
    if (lpReserved == nullptr) {
      SwapchainHook::Shutdown();
      CrashGuard::Shutdown();
    }
    break;
  }
  return TRUE;
}
