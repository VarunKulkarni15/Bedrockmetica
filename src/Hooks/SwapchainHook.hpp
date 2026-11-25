#pragma once

#include <atomic>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <string>
#include <windows.h>

namespace SwapchainHook {

enum class GraphicsAPI { Unknown, D3D11, D3D12 };

bool Initialize();
void Shutdown();

bool IsInitialized();
GraphicsAPI GetCurrentAPI();
const char *GetCurrentAPIName();
const char *GetCurrentAPIName();
HWND GetGameHwnd();

bool IsInWorld();
bool IsGameplayHud();
std::string GetCurrentScreenName();

bool IsMenuOpen();
void SetMenuOpen(bool open);
void ToggleMenu();

ImFont *GetFontRegular();
ImFont *GetFontBold();
ImFont *GetFontMinecraft();
int GetMenuToggleKey();
void SetMenuToggleKey(int vk);
std::string GetMenuToggleKeyName();
void BeginKeybindCapture();
void CancelKeybindCapture();
bool IsCapturingKeybind();

ImFont *GetFontRegular();
ImFont *GetFontBold();
ImFont *GetFontMinecraft();

extern int g_MenuToggleKey;

}
